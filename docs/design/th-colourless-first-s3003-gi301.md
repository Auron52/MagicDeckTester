# TH s3003 gi301: root-cause of the one game colourless-first made slower

**Status:** ROOT-CAUSED 2026-08-24. No action taken on `eaccc120` (the colourless-first tap order);
the open item it exposes is Treasure Hunt's Land's-Edge **prune quality**, recorded in §5.

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

**Prime suspect, not yet confirmed.** The searched `lands_edge` count is scored by rollouts whose own
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

**Status:** §5 is OPEN and unstarted. Start at the "next step" in §5 (dump the T3 candidate scores);
the repro in §1 is one second per run and needs no worktree now that the control is characterised.
