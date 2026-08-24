# TH s3003 gi301: root-cause of the one game colourless-first made slower

**Status:** FIXED 2026-08-24 — see §7. The bug was NOT a prune (§5's suspicion was wrong, and §2's
account of the mechanism was wrong too): `AIEngine::ActivateLandsEdge` deviates from the fire-count
heuristic on a rollout comparison and then leaves the **committed line stale**, the same train/serve
split `s_discard_reline` and the searched vial already fix at their own deviation sites. The game
now wins on turn 4. §§0-6 are kept as the trail that led there; read §7 for what was actually wrong.

## 0. The retraction that started this

`eaccc120`'s commit message says of this game: *"Treasure Hunt shuffles, so it reads as
draw-divergence variance."* **That is wrong — Treasure Hunt does not shuffle** (USER, 2026-08-24).
The decklist is 55 lands + 4 Treasure Hunt + 2 Land's Edge + 1 Throes of Chaos; no fetch, no tutor,
no shuffle effect. `classify_turn_later.sh` prints "variance if th shuffles, else same-draws
slowdown" and I took the wrong branch of its own disjunction without checking the deck. This is the
same failure mode as the two Wirewood Lodge mis-diagnoses: an assumption written down as a finding.

Confirmed directly instead: both arms have byte-identical mulligan sequences, identical opening
hands, and identical draws — including the two Treasure Hunt reveals, which come back in the same
order (`Steam Vents, Fiery Islet, Steam Vents, Thundering Falls, Reliquary Tower, Treasure Hunt`).
**Same draws. A genuine play divergence.**

## 1. Repro

```
seed = 3003 + 301 = 3304, game-index 301          # base+gi, per the batch repro rule
BIN decks/treasure_hunt/treasure_hunt.txt --seed 3304 --game-index 301 --games 1 \
    --depth 3 --budget-ms 10 --ignore-play-profile --threads 1 \
    --profile decks/treasure_hunt/treasure_hunt.profile.json
```

Control = `46015c28` (the commit before the rank change), fixed = `eaccc120`. The two binaries differ
**only** in `ManaSourceRank`, so attribution is exact — no bisection needed.

| arm | win turn |
|---|---|
| control (colourless ranks 10, same as mono-coloured) | **4** |
| fixed (colourless ranks 5, taps first) | **6** |

## 2. What actually diverges

One decision, at turn 3. Both arms play Reliquary Tower and cast Throes of Chaos for `{3}{R}`; the
fixed arm **additionally fires three Land's Edge discards** (opp 20 → 14). Those three lands are
exactly what turn 4 needed:

* control T4: plays Ferrous Lake, retraces Throes, casts **two** Treasure Hunts off the chain,
  discards 14 lands → opp 20 → −8, **wins T4**.
* fixed T4: no land drop (no land left in hand), retraces Throes, one reveal, chain stops. T5 tries a
  Treasure Hunt with only Reliquary Tower untapped and the payment FAILS (`{C}` cannot pay `{1}{U}`;
  visible as `[bp-pay] -> FAILED` under `MTG_BP_TRACE=1`). Wins T6.

The burn is **not** the cleanup rule. `MTG_LE_PROBE` (a temporary probe on
`LandsEdgeHeuristicFireCount`) returns 0 at turn 3 in both arms, in the executor and in every rollout
sample. It is the **searched** `lands_edge` count in the plan — both arms enumerate 0..3, the control
scores 0 best and the fixed arm scores 3 best.

> **WRONG — corrected in §7.** That last sentence is not what happens. `DiscardToLandsEdge` is fanned
> out over 0..N only under `MTG_HUMAN_PLAY` (`AppendHumanPlayLandsEdgePlans`); autonomous search never
> enumerates it. The burn comes from `AIEngine::ActivateLandsEdge`'s depth>0 **fire-all override**, a
> two-way rollout comparison that runs AFTER the plan is committed. I inferred an enumeration from the
> shape of the outcome instead of instrumenting the site — the same mistake as §0, one layer down.

## 3. It does NOT recover with depth or budget

The obvious hypothesis — search truncation — is refuted. Every cell below is the fixed arm at 6 and
the control at 4:

| depth | budgets tested |
|---|---|
| 3 | 10, 200 |
| 4, 5, 6, 7, 8 | 2000 |
| 5, 6, 8 | 20000 |

`classify_turn_later.sh` had already reported PERSISTS at 4x and 16x budget at d3; this extends it to
depth 8 and a 2000x budget. Not churn.

## 4. The finding: with prunes OFF, the two arms AGREE

This is the diagnostic that settles what kind of defect it is. Under `MTG_UNPRUNED=1` — the standing
A/B the search-primary core bar exists to preserve — the arms become identical:

| config | control | fixed |
|---|---|---|
| `MTG_UNPRUNED=1` d5 b20000 | 5 | **5** |
| `MTG_UNPRUNED=1` d6 b20000 | 5 | **5** |
| `MTG_UNPRUNED=1` d6 b60000 | 5 | **5** |
| `MTG_UNPRUNED=1` d8 b20000 | 5 | **5** |

At depth 0 the arms are also identical (the executor's Land's-Edge heuristic reads the same numbers
in both). So the tap-order change **did not remove a line from the search space** and does not change
what the search concludes when it is allowed to look at everything. It moves a knife-edge decision
*inside a heuristic-pruned window*, and the pruned window is what differs.

## 5. THIS IS A REAL BUG (USER, 2026-08-24) — and what §4 does and does not prove

An earlier draft of this file filed §2 as a "knife-edge decision inside a pruned window" and moved
on. That is too generous, and the user rejected it. Read the numbers again:

| | win turn |
|---|---|
| control, pruned | 4 |
| **unpruned (both arms)** | **5** |
| fixed, pruned | **6** |

**The fixed arm's pruned play is a turn worse than the same engine with the prunes switched off, at
every depth to 8 and every budget to 60000.** A prune is only ever allowed to be a search-economy
device; one that costs a turn *that the unpruned search finds* is a defect by the search-primary core
bar, not a tuning preference. That is the bug. It is reachable without the tap-order change (see the
mirror case below) — `eaccc120` exposed it, it did not create it — but it is a bug either way.

**Where §4's evidence stops.** "Both arms agree unpruned" is sound evidence that the *tap-order
change* is not the culprit: same input, same conclusion once nothing is narrowed. It is NOT evidence
that the unpruned answer is ground truth — unpruned enumeration is far wider, the rollouts run
unpruned too, and at TH's branching factor even b60000 may be starved. The control's pruned T4 being
*better* than unpruned T5 is exactly that starvation showing. So: T5 is a lower bound on what the
search can do here, and the fixed arm's pruned T6 is below it. That is the part that is solid.

**Prime suspect, not yet confirmed.** *(It was wrong — the trials do not tie, they read
`w_heur=5 w_all=4`, and the defect is downstream of the comparison entirely. See §7.)*
The searched `lands_edge` count is scored by rollouts whose own
Land's-Edge firing is the fixed `LandsEdgeHeuristicFireCount` rule ("fire everything at
`lands_in_hand >= ceil(opp_life / rate)`, else only hand-size excess"). From *both* the burn-3 and
burn-0 branches the rollout eventually crosses that threshold and reports a win, plausibly on the
same simulated turn — in which case the tie-break prefers the branch that already dealt 6. Meanwhile
the real cost of burning early (three fewer lands = no T4 land drop = a shorter Treasure Hunt chain)
is invisible to that rule. **Next step: dump the two candidate plans' scores at T3 and check whether
they tie on simulated win turn.** If they do, the fix is on the rollout's Land's-Edge model, not on
the tap order.

**The known mirror case.** `th-lands-edge-lethal-shortfall.md` records the same heuristic failing the
other way — it *holds* when it should fire, costing T4 vs the human's T3, also persisting under
`MTG_UNPRUNED` and at b4000. Two instances, opposite directions, same fixed rule: **how many lands to
feed Land's Edge, and whether to spend the land drop, is decided by a heuristic no amount of depth or
budget can route around.** Making it a searched decision is the standing successor plan (cf.
`docs/design/searched-cleanup-discard.md` for the same argument about cleanup discard).

## 6. Why `eaccc120` stays in the meantime

* The change is *correct on its own terms* — a `{C}`-only source pays generic pips only, so spending
  it before a coloured source is strictly better for affordability, and it fixes a real user-reported
  line that the engine enumerated and then could not pay (StompySurprise s9 T2).
* It does not narrow the search: §4.
* Aggregate, regression tier: **net −0.039** across the ten moved cells, better on seven. TH d3 s3003
  moved **+0.004 over 500 games** — i.e. this single game is essentially that cell's entire delta,
  and TH's d0 cell moved −0.004 the other way.

Nothing here argues for reverting the rank — reverting it would re-break the StompySurprise line and
would not touch the §5 bug, which is reachable without it. It argues for fixing §5.

**Status:** CLOSED — §7. `eaccc120` stays, and the bug it exposed is fixed rather than worked around.
The one caveat §5 raised still stands on its own: `th-lands-edge-lethal-shortfall.md` is a SEPARATE,
still-open case where the fire-count *heuristic* holds when it should fire. That one is not this bug
(it is not a stale line — it is the rule itself) and making the count a genuinely searched decision
remains its standing successor plan.

## 7. THE ACTUAL BUG: a deviation that leaves the committed line stale (FIXED 2026-08-24)

### 7.1 The site

`AIEngine::ActivateLandsEdge` fires Land's Edge at the end of each main phase. Its count comes from
`LandsEdgeHeuristicFireCount` (fire for lethal, else shed only the over-hand-size excess, else hold),
and then — at depth > 0, outside a rollout only — it overrides that with an **all-or-heuristic
comparison**: roll the game out once having fired the heuristic count, once having fired *every* land,
and fire everything if that wins sooner. Instrumented with a temporary `MTG_LE_TRIAL` probe, the
repro's turn 3 reads:

```
[le-trial] t3 main1 opp_life=20 rate=2 lands_in_hand=3 heur=0 w_heur=5 w_all=4 -> fire=3
```

Not a tie — §5's prime suspect was wrong. The trial ranks fire-all a **turn-4** win over hold's
turn-5 and acts on it. The realised game then takes **six**.

### 7.2 Why the projection was a fantasy

The trial is not lying about its own line; it is evaluating a line the real game will not play.
`RolloutWinTurnFrom` **clears `m_committed_line`** for the trial, so the trial re-searches turn 4 from
the board firing creates. The real game does not: it keeps replaying the line it committed *before*
the burn — and that line came out of a search whose inline executor
(`TurnSolver::ApplyPlanDirect`) auto-fires `LandsEdgeHeuristicFireCount` and **never** this override.
Every plan still queued in it therefore assumes the three lands are still in hand.

That is not a small window. `TakeTurn` truncates the committed line to the current turn only when the
win is *not* verified in horizon; when it **is** verified — precisely this game, a turn-5 win found at
depth 3 — the whole multi-turn line is committed. So the executor replays a turn-4 plan that spends a
land drop it no longer has, and a turn-5 Treasure Hunt it can no longer pay for. The
`[bp-pay] -> FAILED` in §2 was never a mana-colour problem; it is the stale plan hitting a board that
moved underneath it. It is a train/serve split: **the label came from a fresh search, the realised
game from a stale plan.**

This is why §3 found no recovery at depth 8 / budget 60000. More search does not help when the defect
is that the search's output is discarded by a later, unsearched deviation — it just builds a longer
stale line.

### 7.3 The fix

One line, and it already exists twice in this file:

```cpp
if (w_all < w_heuristic)
{
    static const bool s_le_reline = EnvOn("MTG_LE_RELINE", true);
    if (s_le_reline) { m_committed_line.clear(); }
    fire_count = lands_in_hand;
}
```

`s_discard_reline` does exactly this when the searched cleanup discard deviates from the heuristic
pick, and the searched Aether Vial charge does it when it deviates. Land's Edge is the **third**
deviation site in the engine and the only one that never got the treatment — because it predates the
other two by a year and the class had not been named yet.

A second, independent defect was found and fixed at the same site: the two trial rollouts resumed at
`ResumeAt::NewTurn`, which **skips the rest of the current turn** (combat, second main, end step, and
the cleanup discard that sheds a flooded hand) — exactly what `RolloutWinTurnFrom`'s own comment
warns a mid-turn caller must not do. They now resume at `Combat` (from main 1) or `End` (from main 2).
`MTG_LE_TRIAL_NEWTURN=1` restores the old behaviour.

### 7.4 Measurement

Treasure Hunt is the only deck in the repo carrying a Land's Edge, so no other deck can move (`rate
== 0` returns before any of this). Both levers were swept as a 2x2 (`logs/le_reline_ab/`).

**Train** — 3,825 games, seeds 1001/2002/3003, d0/d3/d5:

| arm | cells moved (of 8) | result |
|---|---|---|
| resume-point only | 0 | byte-identical to control |
| reline only | 1 | `th_d3_s3003` 3.9860 -> **3.9820** |
| both | 1 | identical to reline-only |

Exactly **one game of 3,825** changed: gi 301, the reported one, `6 -> 4`. Nothing else moved.

**Held-out** — 6,900 games on the disjoint overnight seeds 4004/5005/6006/7007, plus two budget-80
cells for the deep-search regime:

| | |
|---|---|
| pooled avg, control | 4.07536 |
| pooled avg, fixed | **4.07377** |
| delta | **-0.00159** |
| games faster | **11** |
| games slower | **0** |

Monotone-better, the same shape the searched-discard fix measured (39 faster / 0 slower). One of the
11 was unwon within the horizon and now wins on turn 8.

The resume-point lever is **byte-inert on all 18 cells / 10,725 games** — every bit of the measured
gain is the reline. It ships anyway as a correctness fix to a documented contract (it is latent, not
absent: it is inert only because TH's post-main-1 remainder happens not to change these projections),
and it is recorded here as unmeasured-value so a future reader does not credit it with the win.

### 7.5 What pins it

No `test/scenarios/*.json` fixture. A hand-built board could not be made to separate the arms: the
defect needs a *verified-in-horizon multi-turn committed line* that the deviation then invalidates,
and on a constructed board the two trials tie (`w_heur == w_all`) so the deviation never fires — a
fixture that passes with `MTG_LE_RELINE=0` would be a **dead check**, which is worse than none (see
the c4153b0e trial-pay that shipped dead). The pin is the ground truth instead: `th_regression_d3_s3003`
records game 301 = 4, and the harness's per-game diff names the exact game if the reline is ever lost.

### 7.6 The general lesson

Three sites in this engine deviate from a heuristic on the strength of a trial rollout, and a trial
rollout is run on an EMPTY committed line. **Any such deviation invalidates the committed line by
construction** — the trial measured a re-searched future, the game will replay a pre-deviation plan.
Two of the three had been fixed one at a time, each as if it were a local bug. The rule is general:
*if you deviate from what `ApplyPlanDirect` would have done, drop the line.* A fourth such site added
later will have the same bug unless the invariant is enforced where the deviation happens rather than
rediscovered per site.
