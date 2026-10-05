# Combined-branch regression gate (soldiers-analysis, rebased onto d264c556; 2026-10-05)

Status: **measured. Smoke + regression GT ACCEPTED (local commit, NOT pushed). Overnight NOT accepted -- one bar fails (kittyv2, below).** Self-contained; written by the agent that owned the
gate while the user was away (no question blocked the work -- defaults taken are listed at the end).

## What was gated

Branch `soldiers-analysis` at `0303292c`, rebased onto `origin/phase-1-2-deck-analyzer` `d264c556`:

| commit | what |
|---|---|
| a740bfa0 | breakpoint continuations apply their board activations; Snow casts Slumber after the fixer (`MTG_SNOW_ORDER_WATCHER`) |
| 9be68a01 | Snow plays the user's cast order (Augur before Sheets); Kitty one total order; equips at end of phase |
| a9aa209d | Arcum's Astrolabe is a colour converter, not a prepay producer (Snow s3003 gi5, smoke gi69) |
| a4609dbb .. 4bc7b49f | Soldiers deck (cards, provider, discard buckets, all-three-tier rows, GT) incl. 25c37100 (same-line sac fodder) |
| 3c79563a | Vial-put order twin made structural + outcome-gated (`VialOrderChangesOutcome`); continuation twins |
| 4753295b | canon continuations apply casts only (`MTG_CANON_CONT_NOACTS`); Vial-order twins join the exact-duplicate skip (`MTG_VIAL_TWIN_DEDUP`) |
| 0303292c | inside a leaf rollout the Vial-order twin keeps its pre-3c79563a gate (`MTG_VIAL_TWIN_ROLLOUT_LEGACY`) |

The pre-rebase gate (same commits on `27f10ea7`) found and fixed two d8b0 non-recoveries (melira overnight
s4004 gi107, minotaur overnight s5005 gi971 -- root cause: an unchallengeable canon continuation applying
trade-off activations, fixed by 4753295b) and two churn games (minotaur regression s2002 gi295 -- duplicate
Vial twins, fixed by 4753295b; soldiers regression s3003 gi72 -- twin cost inside leaf rollouts tipping the
FullSearchLine start gate, fixed by 0303292c). Those fixes are carried unchanged; this gate re-measures
everything on the rebased tip. Origin's own commits since the last overnight accept (a13ab286 on 3de66a1f)
include the kittyv2 and fungusb keep-profile adoptions (smoke + regression rebaselined on origin, overnight
NOT) and the SBA-ordering fix bb3e3281 + death-path perf commits.

Bars (user): every deck net <= 0 vs GT (game-turns, loss = 9); every searched game slower than GT recovers
at `--depth 8 --budget-ms 0`; budget churn is minimised, not excused.

## Step 0 -- build + unit + scenarios + GT consistency (rebased tip)

`./build.sh` clean; `mtg-test` 398/398 (2,641,914 assertions); scenarios 139/139; `check_gt_logs.py`
659 consistent, 0 stale, 0 missing. No rebase breakage; no fix commit needed.

## Smoke (rebased tip) -- every deck net <= 0

Game-turn deltas vs GT (loss = 9), searched / d0. Only decks that moved are listed; every other deck is
score-identical (some digest-only play changes: fivecolour2hg, fungus, whiteknights).

| deck | searched | d0 | searched slower / faster |
|---|---:|---:|---|
| kitty | -24 | 0 | 0 / 24 |
| kittyv2 | -24 | -14 | 0 / 24 |
| kitty2hg | -5 | -- | 0 / 5 |
| snow | -3 | -8 | 1 / 4 |
| fungusb | -2 | -1 | 0 / 2 |
| melira | -1 | -- | 0 / 1 |
| melira2hg | -1 | -- | 0 / 1 |
| minotaur2hg | -1 | -- | 0 / 1 |

Soldiers, Minotaur and Pirates smoke are score-identical to GT.

## Regression (rebased tip) -- Snow searched +1 (verdict below), every other deck <= 0

| deck | searched | d0 | searched slower / faster |
|---|---:|---:|---|
| kitty | -99 | 0 | 0 / 99 |
| kittyv2 | -73 | -17 | 1 / 72 |
| fungusb | -2 | 0 | 1 / 3 |
| melira | -1 | -- | 1 / 2 |
| snow | **+1** | -12 | 3 / 2 |

Digest-only (score-identical) cells: fivecolour, minotaur, minotaur2hg, pirates, selesnya, soldiers (d3/d5
s2002). Soldiers / Minotaur / Pirates regression are score-identical to GT, i.e. the two pre-rebase churn
fixes hold on the rebased base.

## Every searched slower game, smoke + regression (ONE pooled batch: x1 control, 4x budget, d8 b0)

Repro = `seed = job.seed + gi`, `game_index = gi`, case manifest's own depth/budget (d5 rows omit depth so
`value_play` owns it). x1 reproduces the tier result in every case.

| game | GT | x1 | 4x | d8 b0 | verdict |
|---|---:|---:|---:|---:|---|
| snow smoke d3 s1001 gi69 | 8 | loss | 8 | 8 | churn, recovers (d8b0 85 min, 53.7M units) |
| fungusb regression d5 s2002 gi10 | 5 | 6 | 5 | 5 | churn, recovers |
| kittyv2 regression d3 s3003 gi87 | 7 | 8 | 8 | 7 | recovers at d8b0 only (same as pre-rebase gate) |
| melira regression d3 s2002 gi14 | 4 | 5 | 4 | 4 | churn, recovers |
| snow regression d3 s2002 gi40 | 6 | 7 | 6 | 6 | churn from the user's Slumber slot (below) |
| snow regression d3 s3003 gi8 | 5 | 6 | 5 | 5 | draw divergence from the user's order (below) |
| snow regression d5 s3003 gi8 | 5 | 6 | 5 | 5 | same game at d5 |

## Snow regression searched +1 -- verdict: churn from the USER's cast order, not a defect

Three slower games (gi8 at d3 and d5, gi40 at d3) against two faster (gi15 at d3 and d5). Each was isolated
on the tip binary with the per-flag levers and compared, per turn, to an origin `d264c556` build run from
its own worktree:

* **s3003 gi8 (d3 + d5).** T4: origin casts `Ice-Fang Coatl, Marit Lage's Slumber`, the tip `Slumber, Coatl`.
  Slumber's enter-scry now precedes Coatl's ETB draw, so **the draws diverge** from T4 on (a physically
  different game, not like-for-like). It is the USER's order doing exactly what it was asked to do
  (Slumber ahead of the snow permanents it scries on): the game returns to 5 only with BOTH
  `MTG_SNOW_CAST_ORDER=0` and `MTG_SNOW_ORDER_WATCHER=0` (either alone still puts Slumber first). It recovers
  at 4x and at d8 b0 anyway.
* **s2002 gi40 (d3).** The T1 search is truncated after its d1 pass (`searched_depth=1`), and two T1 lines
  tie at win=6: `Island + Frost Augur` (origin) and `Scrying Sheets, pass` (tip). The tie breaks the other
  way under `MTG_SNOW_ORDER_WATCHER` (=0 restores origin's pick and the T6 win). Recovers at 4x and d8 b0.
  A sound fix would be a search-shape change (deeper T1 at b10, or a tie-break preferring the developing
  line) -- out of scope for a gate, and the latter is a heuristic the user owns.
* **Sizing the watcher slot on held-out seeds** (ONE pooled batch, per-job `flags`, seeds 9300000-9303599:
  4 x 150 d3/b10 + 4 x 100 d5 play, watcher ON vs OFF, 1,000 paired games): **net 0** -- 2 games differ
  (s9300000 gi80 on 5 / off 6; s9302000 gi97 on loss / off 8, a horizon knife-edge), units +0.25%.
  So the slot is play-neutral at scale, and the tier's +1 is sampling noise on 180 searched games of a
  zero-mean order effect. The order itself (9be68a01) was measured net-better on held-out seeds
  (d3 -0.013, d5 -0.020, 4/0 seeds).

Snow regression total is -11 (searched +1, d0 -12); across smoke + regression Snow searched is -2. Not
fixed: changing it would mean overriding a user-owned cast order (CLAUDE.md: a user order is never dropped
by an agent). **Surfaced as an open question below.**

## Reference-replay gate (regression tier)

`73 ok, 365 repaired, 1 play-drift, 1 shuffle-dead, 1 board-diverged, 1 enum-gap (442 refs)` -- the SAME
four failing references as origin `d264c556` records (`docs/design/reference-gate-red-on-origin-tip.md`):
Mirrorwing v2 s51_gi50 play-drift (5 vs 4), Hinata2 s1_gi0 ENUM-GAP, Snow s4_gi3 BOARD-DIVERGED (tap state
only), Mirrorwing v1 s29_gi28 shuffle-dead. Nothing new. The one tally move (origin 74 ok / 364 repaired)
is **Pirates claude_s4_gi3: ok -> repaired**, win turn unchanged (5 = 5): 2 stale indices re-anchored by
content at T2 (`main_phase` 16 -> 0). Pirates runs Aether Vial; the T2 decision follows a Vial charge, and
consistent with the branch's Vial-put order twins/dedup changing the plan LIST (not the outcome) -- not separately bisected.
Informational per the skill (re-save via the viewer when convenient); not a gate failure.

## Overnight

**Origin check (user constraint):** immediately before launch, `git fetch` -> `origin/phase-1-2-deck-analyzer`
= `d264c556`, unchanged. All code changes were finished before launch (none since). Run once: 385 jobs,
batch makespan 50m14s, heartbeat 24/24.

### Deltas vs GT (game-turns, loss = 9; searched / d0)

| deck | searched | d0 | searched slower / faster | note |
|---|---:|---:|---|---|
| kitty | -511 | +2 | 0 / 511 | d0 +2 is greedy churn (light-touch tier) under the Kitty total order |
| kittyv2 | -1227 | -5344 | 118 / 651 | **GT STALE**: origin adopted a new kittyv2 keep profile (05b035c1) and rebaselined only smoke + regression |
| kittyv2 **vs origin d264c556 arm** | **-341** | **-120** | **4 / 345** | the right baseline (rule 5): same 12 cells, origin binary from its own worktree |
| melira | -32 | -- | 7 / 35 | |
| snow | -13 | -7 | 1 / 14 | |
| minotaur | -6 | -- | 0 / 6 | |
| goblins | -3 | -- | 0 / 3 | |
| fivecolour | -2 | -- | 0 / 2 | |
| soldiers | -2 | -- | 0 / 2 | |
| whiteknights | -2 | -- | 0 / 2 | |
| dragons | -1 | -- | 0 / 1 | |
| minotaur2hg | -1 | -- | 0 / 1 | |
| selesnya | +1 | -- | 1 / 0 | run-to-run batch nondeterminism, NOT a play change (below) |

Digest-only (score-identical) cells: fivecolour2hg, fungus, pirates (7), stompy (4), plus same-score games
inside the decks above. The origin arm reproduces the overnight GT for every non-kittyv2 slower game
(x1 = GT turn), so all of those moves are this branch's.

### Slower-game recovery -- the TWO-STAGE method (USER ruling 2026-10-05)

Replaces "send every slower game to d8 b0 (+ a 4x arm)":
* **Stage 1** -- per game, `depth = W` (that game's GT win turn; GT loss -> 8), `budget_ms 100`, in ONE pooled
  batch. Depth W from turn T covers turns T..T+W-1 >= W, so a win by turn W is a real terminal line, never a
  leaf estimate: a stage-1 recovery is conclusive.
* **Stage 2** -- ONLY games that do not recover at stage 1 go to `--depth 8 --budget-ms 0` (one pooled batch).
* The 4x arm is dropped unless needed for a churn root-cause.

(Smoke + regression above were checked the older way -- x1 + 4x + d8 b0 -- before the ruling; every game
recovered at d8 b0, which is the stricter of the two, so no re-check was needed.)

Overnight searched slower games (13 = 9 vs GT + 4 kittyv2 vs the origin arm; W = the baseline's turn):

| game | base W | tip | stage 1 (dW b100) | stage 2 (d8 b0) | verdict |
|---|---:|---:|---:|---:|---|
| melira d3 s5005 gi54 | 5 | 6 | 4 | -- | recovers |
| melira d5 s5005 gi54 | 5 | 6 | 4 | -- | recovers |
| melira d3 s6006 gi11 | 5 | 6 | 5 | -- | recovers |
| melira d5 s6006 gi11 | 5 | 6 | 5 | -- | recovers |
| melira d3 s6006 gi110 | 4 | 6 | 4 | -- | recovers |
| melira d3 s7007 gi116 | 5 | 6 | 6 | 5 | recovers at d8 b0 |
| melira d5 s7007 gi116 | 5 | 6 | 6 | 5 | recovers at d8 b0 (same game) |
| snow d3 s5005 gi52 | 5 | 6 | 5 | -- | recovers |
| selesnya d3 s4004 gi929 | 5 | 6 | 5 | -- | recovers; and a full re-run of the cell gives 5 (nondeterminism) |
| kittyv2 d3 s5005 gi190 | 4 | 5 | 4 | -- | recovers |
| **kittyv2 d3 s4004 gi136** | 4 | 5 | 5 | **5** | **DOES NOT RECOVER** (origin d8 b0: 4) |
| **kittyv2 d3 s5005 gi92** | 5 | 6 | 6 | **6** | **DOES NOT RECOVER** |
| **kittyv2 d5 s5005 gi92** | 5 | 6 | 6 | **6** | same game |

### kittyv2 gi136 / gi92 -- root cause: the Kitty TOTAL order makes the winning line inexpressible

Like-for-like (kept hand and draws identical). Origin's unlimited search finds T4 for gi136:
`T1 Remote Farm + Bone Saw | T2 Plains + Kite Shield, Dragonfire Blade, Dwalin | T3/T4 attack`. The tip's
d8 b0 search verifies only T5 (`searched_depth=5 verified=1`, ~1.5k units: it is not budget). The difference
is the cast ORDER inside the turn: **Dwalin, Weaponmaster -- "Whenever Dwalin enters or attacks, put a hone
counter on each Equipment you control"** (`hone_counters_on_enter_or_attack`) wants the Equipment cast BEFORE
him; the user's total order (9be68a01: land, Sol Ring, Sigarda's Aid, Puresteel, Sram, Cid, Stoneforge,
**other creatures, Equipment**) casts him first, so the same-turn Equipment never gets its counter and the
T4 line does not exist at any budget. Confirmed: with `MTG_SEARCH_ORDER=1` (orderings searched) the tip
recovers BOTH games at d8 b0 (gi136 4, gi92 5).

This is the Snow `watcher` shape inverted (an enter-trigger that acts on permanents ALREADY in play wants
them first). The order is USER-OWNED, so it is not amended here; proposed amendment below. Fixing it would be
a code change after the overnight, which under the user's constraint means this overnight cannot be
accepted -- so it is not.

Scale, for the decision: on these 12 cells the branch is -341 searched / -120 d0 game-turns vs origin with 4
slower games of 1,500 searched; the order is a large net win that loses exactly this Dwalin-first shape.

### selesnya d3 s4004 -- the open batch nondeterminism, not this branch

The tier run scored gi929 6 (avg 5.2740, digest 46d53ee7...). A standalone game, a 1-job batch AND a full
re-run of the whole 1000-game cell in a pooled batch on the SAME binary all give gi929 **5** (re-run avg
5.2730 = GT avg; per-game digest of gi929 identical to GT). Origin's binary also gives 5. This is
`docs/design/batch-run-to-run-nondeterminism.md` (open, concurrency-gated); Selesnya is otherwise untouched
by the branch.

### Overnight verdict

**NOT ACCEPTED.** Every bar passes except "every slower game recovers at d8 b0" for kittyv2 gi136 and gi92,
root-caused above to a user-owned order. The tier stays as committed (still stale for kittyv2's keep
profile, as on origin). Next step after the user's ruling: amend (or not) the Kitty order, re-run smoke +
regression, then ONE overnight, accept.

## Open questions for the user (none blocked anything; default taken in brackets)

1. **Kitty total order -- move Dwalin, Weaponmaster AFTER the Equipment** (his ETB hones Equipment already in
   play; same shape as Snow's Slumber "watcher" slot, inverted). Recovers kittyv2 overnight gi136 / gi92 at
   d8 b0 in the `MTG_SEARCH_ORDER=1` probe. [Default: NOT applied -- user-owned order; overnight NOT accepted
   pending this.] Alternative if you prefer not to amend: accept the overnight as-is with these two games
   recorded as order-inexpressible.
2. **Snow regression searched +1** comes from your Snow cast order (gi8 draw divergence, gi40 truncated-T1
   tie); both recover at 4x and d8 b0, watcher slot net 0 on 1000 held-out games. [Default: accepted as
   order churn; deck total -11 in that tier, searched -3 in smoke.]
3. **Canon continuation default is unchallengeable** (carried from the pre-rebase gate, 4753295b): offering
   canon-with-activations and casts-only as siblings at every unbranched occurrence is the principled form of
   `MTG_CANON_CONT_NOACTS`. [Default: not built -- a search-shape change with branching cost.]
4. **Selesnya overnight d3 s4004** flips run-to-run (batch nondeterminism). [Default: nothing accepted from it.]
5. **Pirates claude_s4_gi3** is now `repaired` (indices only, win turn 5 unchanged). [Default: left; re-save
   via the viewer when convenient -- references are user-owned.]
