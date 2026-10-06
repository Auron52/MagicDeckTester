# Site-9 continuation "index mismatch" -- ROOT-CAUSED AND FIXED (2026-10-05)

Status: **FIXED** in `fix(search): number a BP-NODE child the way a from-scratch apply counts it`
(`MTG_BP_NODE_SHADOW`, default ON, `=0` reverts). USER decision recorded with it: *"if it is
preventing correct lines we should be fixing it."* Found while fixing the Bruna Stage-5d claude-play
sweep findings (`docs/design/analysis-Bruna.md`, "Claude-play sweep", finding E).

## Repro

```
./build.sh
MTG_ROLLOUT_AURA_SWAP=1 MTG_FD_ORACLE=1 ./build/Release/mtg decks/Bruna/Bruna.cod \
    --profile decks/Bruna/Bruna.profile.json --games 1 --seed 4205 --game-index 201 \
    --depth 5 --budget-ms 20
# before: [fd-diverge] seed=4205 realized_win=6 predicted_win=5   after: avg 5.0000, no diverge
# MTG_BP_NODE_SHADOW=0 reproduces the old behaviour.
```

Fixture: `test/scenarios/bruna_site9_node_child_numbering.json` (the T5 board; T5 with the fix,
T6 under `MTG_BP_NODE_SHADOW=0` -- the control).

## What the first diagnosis got wrong

It read the defect as "the scoring rollout and the executor build the site-9 list on different
boards". They do not: `MTG_SITE9_TRACE=5` (print-only, both worlds) shows the executor's list and the
rollout's list at the same board are byte-identical (`#0 Greaves + swap`, `#1 Greaves`, `#2 swap`).
The boards with the Pilgrims tapped were OTHER plans (different land / wish target). The real
mismatch is in the breakpoint NUMBERING, not in the list.

## Root cause

The committed T5 plan was a **BP-NODE child** (`MTG_BP_NODE`, default ON, hosting sites 3 and 10):

1. The base plan `[Thicket] Glittering Wish (-> Almost Perfect), Arcanum Wings` is applied with a
   capture. A base plan (`bp_choice < 0`) **never advances `bp_seen`**, and **site 9 is not even
   counted for it** -- its gate short-circuits on `plan.bp_choice >= 0` (Wings' fresh swap ability
   opens site 9 only for variants).
2. The wish's tutor-to-hand arms the deferred re-solve; the node pends the base there and stamps each
   child `bp_choice = k, bp_at = snap.bp_seen` = **0**, then RESUMES it from the snapshot -- past
   site 9. Child k=1 at the deferred site was `swap(Almost Perfect)`: seven land/rock mana pay Wish +
   Wings + swap, both Pilgrims attack, 9+4 + 1 = 14 = lethal. The node returned that as a T5 win
   (before any root dump), recording the swap into `breakpoint_actions`.
3. The executor applies the child **from scratch**, counting as a variant does: site 9 is occurrence
   0 == `bp_at`, so `bp_choice = 1` landed on **site 9's** list -> Lightning Greaves. The recorded
   swap then replayed on top, and Greaves + swap tapped both Pilgrims: no attack, realised T6. The
   fd-pred replay (also from scratch) shows the same wrong line.

So: **a node child is numbered in the base plan's count, while every from-scratch apply of it (the
executor, the fd-pred replay, any re-score) numbers in the variant's count.** The two differ by every
class-on occurrence the base walked past uncounted -- site 9 is the one that is never even evaluated
for a base, and the one whose executor twin re-derives by index rather than replaying a script.

## Fix

`ApplyPlanDirect` keeps `bp_seen_shadow`: the count a choice-carrying plan would have at the same
point -- every class-on `bp_searched_plan` occurrence, plus site 9 under the variant's own
`bp_seen == 0` condition (evaluated only in a node-hosting apply, `bp_capture != nullptr`, so nothing
else pays the battlefield scan). The node capture records the shadow. Consequences:

* resumed scoring is unchanged: the resume restores the same index the child now carries, so the
  deferred site is still the eligible one;
* the executor counts site 9 as occurrence 0 != `bp_at` (=1), applies nothing there (site 9 is
  searched-only), and replays the recorded continuation -- the line that was scored;
* a from-scratch rollout re-apply of the child agrees with both.

Both node hosts (main 1 and the second-main host) use the same capture, so both are fixed.

## Which decks can move with the experiment lever OFF

The defect predates `MTG_ROLLOUT_AURA_SWAP`: it needs (a) a plan that puts a permanent with a newly
activatable ability onto the battlefield (site 9 gate: walkers, Equipment equip abilities such as
Lightning Greaves / Kitty's Equipment, Wings' swap, sac outlets, ...) and (b) a node-hosted deferred
site later in the same apply (site 3: tutor-to-hand / plain cantrip; site 10: put-in-hand), with the
node child committed. A shadow-mask count of ANY class-on occurrence before the pend (e.g. Snow's
site-8 Frost Augur / Scrying Sheets look before a site-10 put) shifts the number the same way.

Measured (one pooled batch, `MTG_BP_NODE_SHADOW` 1 vs 0 per job, every suite deck x {d5 b20, d3 b10}
x 60 games, seed 7001, `MTG_FD_ORACLE=1`): **29 of 31 decks byte-identical.** Moved:

* **Snow** -- 7 digests, 1 game slower (gi24 T6 -> T7, both cells). Verdict: the OLD arm's T6 came
  from the defect itself -- an unverified T4 commit whose node child targeted site 10 was applied by
  the executor at SITE 8 (Frost Augur's look) with site 10's index, an unscored continuation that
  happened to be good. With the fix the untargeted site 8 re-solves (the designed route for an
  estimate commit) and the targeted site 10 plays the scored choice. Recovers at d8 b0 (T6):
  expressible, budget churn.
* **Melira** -- 2 digests per cell, same turns.

0 `[fd-diverge]` in either arm over the batch. The orchestrator's smoke/regression rerun is the
authoritative per-deck read.
