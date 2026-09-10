# EldraziDisplacerFlicker: the "creature over ramp at equal tail" plan-value tiebreak

Heuristic-optimization pass (`.claude/skills/heuristic-optimization.md`) on item 3 of
`docs/design/edf-shortfall-classification.md`. Branch `phase-1-2-deck-analyzer`, 2026-09-10.
**Built and measured, NOT adopted** — both levers ship default OFF and the selector stays in place.

## What the classification asked for, and the one thing it got wrong

The classification note says two of EDF's four +1 reference shortfalls were decided the same way: at
an **equal searched tail**, `plan.value` alone chose a creature over a ramp package.

```
s10_gi9  T2   win=5  val=1200  Eldrazi Displacer      vs   win=5  val=100  Fertile Ground
s11_gi10 T3   win=8  val=1200  Emiel the Blessed      vs   win=8  val=200  Fertile Ground + Overgrowth
```

Both reproduce exactly (`MTG_FS_ROOT_DUMP=2` / `=3` on the shipped binary). But the note attributes
`val` to the deck profile's `card_scores`, and **it does not come from there.** `plan.value` is the
sum of `TurnSolver::EvalCard` over the plan's casts; `EvalCard` never reads `card_scores`. The
arithmetic identifies the real source unambiguously:

* `EvalCard`'s **creature** branch returns `power x ExpectedAttacks x DMG`. Eldrazi Displacer is 3/3
  and `ExpectedAttacks` on turn 2 is 5, minus 1 for no haste: `3 x 4 x 100 = 1200`. Emiel is 4/4 and
  turn 3 gives 4, minus 1: `4 x 3 x 100 = 1200`.
* A land Aura is not a creature, a burn spell or a draw spell, so it falls through every template
  branch to the generic tail's `return DMG` — the **100 floor**. Two of them, 200.

(The profile's `card_scores` for these cards are +0.627 / -0.173 / -0.128 / +0.241 — no scaling of
those produces 1200 and 100.)

So the defect is not a stale learned artifact. It is that **the plan-ordering heuristic prices this
deck's creatures as a combat clock**, on a deck that wins by an Eldrazi Displacer / Emiel blink loop
cashed into a drain, a deck-out or a Shivan Gorge, and never attacks. Meanwhile the resource the
whole kill is bottlenecked on — mana — is priced at the floor. This provider's own `CastOrderRank`
already encodes the opposite doctrine in the ordering domain ("Ramp first: it feeds the rest"); the
VALUE domain never got the same treatment.

**This is Rule-0 heuristic territory, not a bug.** Nothing here is modelled wrongly — a 3/3 really
does have 3 power. There is no correct answer for "what is deploying the outlet worth", only a
measurably-better one.

## The variant

New hook `DecisionProvider::ComboCardValue`, consulted at the TOP of `EvalCard`, before the
per-template branches. `ArchetypeCardValue` cannot express this: it is consulted *after* those
branches, which `return`, so no provider can own a creature's value through it. Base implementation
returns false, so `EvalCard` is byte-identical for every provider that does not override it.

`EldraziFlickerProvider::ComboCardValue` prices in ONE unit — **mana-equivalents** — behind two
independently selectable halves (per-job `heurarm` slots, so one pooled batch runs the whole 2x2):

| lever | rule | Fertile Ground | Overgrowth | Displacer | Emiel | Peregrine Drake |
|---|---|---|---|---|---|---|
| `MTG_EDF_VAL_RAMP` | land Aura = `land_aura_extra_mana x remaining x DMG` | 500 @T2 | 800 @T3 | — | — | — |
| `MTG_EDF_VAL_COMBO` | untapper = mana refunded; outlet / repeatable {C} sink = 3; other body = floor | — | — | 300 | 300 | 500 |

`remaining` is `EvalCard::ExpectedAttacks`' own horizon — ramp is worth less the later it lands, for
the same reason a creature is. Every tier reads card PARAMS (`is_land_aura`,
`land_aura_extra_mana`, `etb_untap_lands`, `blink_cost`, `drain_cost`,
`exile_opponent_top_cost`), never a name, so it is correct for any wish pool or flicker list.

### Why neither half can flip the decisions alone — predicted, then confirmed

No honest ramp price beats 1200 on turn 2 (one extra mana would have to be worth twelve damage), and
no honest outlet price sinks below the 100 floor a land Aura gets today. The mis-pricing is a
**mismatch between the two sides**, so only the pair corrects it. Measured at the s10 T2 node:

| arm | Eldrazi Displacer | Fertile Ground | pick |
|---|---|---|---|
| base | 1200 | 100 | Displacer |
| `RAMP` | 1200 | 500 | Displacer |
| `COMBO` | 300 | 100 | Displacer |
| `RAMP+COMBO` | **300** | **500** | **Fertile Ground** |

## Did the two motivating decisions flip?

**s10_gi9 T2 — YES, cleanly, and onto the human's exact play.** The arms play the same game as the
shipped engine up to T2 (`ramp` and `combo` are byte-identical to base for this whole game;
digest `ed4ef6bb450e3cf8`). Under the pair the T2 cast becomes `Fertile Ground`, enchanting card 6 —
the Brushland that already carries T1's Wild Growth — which is the human's cast **and** the human's
host. Only the land drop still differs (Mariposa Military Base vs the human's second Brushland).

**s11_gi10 T3 — the decision flips, but not observably at the base node.** With the re-pricing live
the T3 cast is a land Aura in every arm that has it (Trace of Abundance / Fertile Ground); the
shipped engine casts Emiel. It cannot be shown at the *identical* state, because the pair also
changes turn 1 of that game (see the cost below). The one clean same-state observation is the
`ramp`-only arm, which is byte-identical to base for the whole game: at the real T3 node it lifts
`Fertile Ground + Overgrowth` to **exactly 1200**, tying Emiel's 1200 — and the tie breaks to the
creature, so the game is unchanged.

`MTG_EDF_VAL_FROM_TURN=<n>` was written to pin the shipped line below turn n and isolate the node.
**It cannot do that, and no turn predicate can:** the state passed to the hook is whatever is being
evaluated, so a rollout launched from turn 1 reaches simulated turn 3 and the gate opens there.
Separating real play from rollout needs an executor-side hook. The knob is kept (default 0, inert)
because it still bounds how much of the arm's effect comes from early turns.

## THIS IS NOT ONLY A TIEBREAK — the cost the framing hides

`EvalCard` is also the **d0 / greedy leaf policy**, so re-pricing cards changes the ROLLOUTS that
produce the tails, not just the root comparison at a tie. On s11_gi10 the pair makes the search
**skip its turn-1 land drop** and **discard Emiel at cleanup** (the outlet is now the
worst-ranked card in hand, so the cleanup shed takes it). The game still lands on turn 7, but the
route is plainly worse. Any future version of this idea should be scoped to the root comparison, not
to the card's value everywhere.

## Measurement 1 — the reference corpus (`scripts/ref_bench.py`, n=9, human 4.333)

| arm | search avg | shortfalls | what moved |
|---|---|---|---|
| base | 5.333 | 6 / 9 | — |
| `RAMP` | 5.333 | 6 / 9 | nothing: every reference byte-identical |
| `COMBO` | **5.222** | **5 / 9** | s5_gi4 5 -> **4**, matching the human |
| `RAMP+COMBO` | 5.444 | 7 / 9 | s3_gi2 4 -> **5** — breaks a reference that already matched |

Read it plainly: **the half that flips the two named decisions is the worst arm on the corpus, and
the half that helps is the one that does not flip them.** Neither s10 nor s11 improves its win turn
under any arm — which for s10 is exactly what the classification predicted ("would not fix s10's
turn on its own; the T4 kill still needs the bank-then-dig construct").

Note the committed `test/ref_bench.json` cache is stale at this HEAD: it records n=8 / human 4.5 /
search 5.25 / 5 short, from before `claude_s8_gi7` was committed (`92999db9`). The live bench is
n=9 and s8_gi7 is a +3 shortfall.

## Measurement 2 — the pooled A/B (`test/edf_valtb_ab.sh`)

EDF is **not in the regression suite**, so the suite is a byte-identity check here, not a quality
measurement — the levers are `EldraziFlickerProvider`-scoped and no suite deck routes through it.
The deck's own performance is measured instead: 4 arms x 8 seeds (spaced by 100, disjoint blocks)
x 100 games = 3200 games, d5 / 20 ms, profile attached, ONE pooled batch, paired by (seed, game
index). Loss-penalized avg win turn (unwon = `max_turns + 1` = 9); win% is not reported.

### Train — seeds 4200..4900 (spaced by 100, disjoint), 100 games each

Paired on (seed, game index); a negative difference means the FIRST arm is faster.

| comparison | n | mean | se | t | seeds better/worse | games changed |
|---|---|---|---|---|---|---|
| base − **both** | 793 | **+0.0567** | 0.0178 | **+3.19** | **0 / 8** | 155 |
| base − combo | 791 | +0.0278 | 0.0160 | +1.74 | 0 / 7 | 127 |
| base − ramp | 794 | −0.0025 | 0.0139 | −0.18 | 2 / 2 | 86 |
| both − combo | 790 | −0.0304 | 0.0170 | −1.79 | 6 / 0 | 125 |
| combo − ramp | 790 | −0.0241 | 0.0191 | −1.26 | 7 / 1 | 156 |

Arm means: base 5.5176, ramp 5.5208, combo 5.4893, **both 5.4622**.

* **`ramp` alone is a measured no-op** — and a well-powered one: se 0.0139 bounds the true effect
  inside ±0.03 t, and it is *not* inert (86 games change play, it just nets to nothing).
* **`both` is 0.057 turns FASTER than the shipped engine, t=+3.19, better on 8 of 8 disjoint seed
  blocks.** The unanimity matters more than the t here — this is not one lucky seed.
* `combo` alone lands between them (+0.028, 7/7 seeds, sub-significant).

### Held-out — seeds 7200..7550 (spaced by 50), 50 games each

| comparison | n | mean | se | t | seeds better/worse | games changed |
|---|---|---|---|---|---|---|
| base − **both** | 392 | **+0.0434** | 0.0251 | **+1.73** | 2 / 5 | 73 |

**Same sign, ~3/4 the size, and no longer significant.** That is the ordinary shrinkage of a
variant selected on its tuning seeds — it does not refute the train result, and it does not confirm
it either. The honest statement is: the effect is real in direction (10 of 15 seed blocks across
both sets favour `both`, none of the 8 train blocks went the other way) and is about **−0.04 to
−0.06 turns**, at the edge of what this corpus can resolve.

### Sample-size note, stated because the run did not go to plan

Both sweeps were stopped in their monster tails (train `both` 794 of 800, held-out 789 of 800; the
shipped `base` arm was itself missing 4). EldraziDisplacerFlicker has games that run **over four
hours at a 20 ms VIRTUAL budget** — the deck's known tractability problem, not a scheduling
artifact. The pairing intersects, so an unfinished game simply drops from every comparison, but it
drops the HARDEST games, which is a bias worth naming: these numbers describe the deck excluding
its ~1% most search-expensive positions.

The run also taught a scheduling lesson that is now fixed in `test/edf_valtb_ab.sh`: a single pooled
queue emitted **arm-major** behaves like the per-arm waves CLAUDE.md forbids when the deck has a
tail this heavy. The first three arms' four-hour games held 17 of 23 workers while the fourth arm
crawled on the remaining 6 — base/ramp/combo reached ~795 of 800 while `both` sat at 113. The queue
is now chunk-outermost (arm- and seed-interleaved) so every arm advances together and a partial read
is balanced. `both` was then finished in its own batch and paired offline against the stored base
games — sound because the engine is deterministic and thread-invariant, so one queue is how you keep
cores fed, not a condition of the comparison (`test/edf_refloat_report.py` now takes several files).

## Gates (selector unset)

* `./build.sh` clean.
* `bash test/scenarios.sh` — **73 passed, 0 failed**.
* `bash test/regression.sh --smoke` — **73 passed, 0 failed**, per-game audit **0 configs changed**,
  0 searched-depth slowdowns, 0 play changes. Byte-identity proved.
* `python3 test/viewer_protocol_check.py` — **0 play-drift, 0 enum-gap, 0 mull-drift, 0
  shuffle-dead, 0 contract-fail** over 307 references (11 ok / 296 repaired, the routine
  ref-predates-a-decision-point category).

## The verdict, and the tension in it

**The two measurements point opposite ways, and that is the finding.**

| | reference corpus (9 hand-played games) | deck average (793 paired games) |
|---|---|---|
| `ramp` | identical to base | −0.003 t (t=−0.18) — no-op |
| `combo` | **−0.111 t**, fixes s5_gi4 | −0.028 t (t=+1.74) |
| `both` | **+0.111 t**, BREAKS s3_gi2 | **−0.057 t (t=+3.19, 8/8 seeds)** |

`both` — the arm that actually implements the classification's proposal and actually flips the two
named decisions — makes the deck **faster on average** and **worse against the human ground truth**.
Those are different objectives and this variant separates them cleanly:

* The **average** improves because re-pricing ramp above a bodies-that-never-attack clock is right
  on the majority of boards, where the deck is mana-starved.
* The **references** get worse because the references are the games a human played *well*, and on
  those the shipped valuation was already reaching the human's turn — so the only thing the change
  can do is perturb them. It broke s3_gi2, which base matched exactly.

A −0.057 t improvement on the deck average is also small next to what the classification itself ranks
first: the value leaf is worth an estimated **−0.375 t** on this corpus (3 of the 4 shortfalls
correct themselves at a bigger virtual budget). This lever is not competing with that; it is the
"costs an afternoon" side-experiment the doc called it, and it came back with an afternoon's worth of
answer.

## Status

**NOT ADOPTED — the decision is the user's.** Both levers default OFF and the selector stays in
place. What the evidence supports, stated as a recommendation rather than an action:

1. **Do not ship `ramp`.** Measured no-op at n=794 with the power to say so.
2. **`both` is the only candidate**, at −0.04 to −0.06 t. Held-out is the same sign but t=1.73, so
   it is not yet confirmed to the bar this repo uses.
3. **Before shipping it, deal with the reference regression and the rollout scope.** It breaks
   s3_gi2, and it is not really a tie-break — it re-prices cards for the greedy leaf policy too,
   which is what makes it skip a turn-1 land drop and discard its own combo outlet on s11_gi10. A
   version scoped to the ROOT plan comparison (leaving the rollout's valuation alone) would test the
   classification's actual claim and would probably not carry those costs. That needs a root-vs-
   rollout distinction the current hook does not have.
4. **Order it behind the value leaf**, per the classification's own ranking.

Open question for the user, recorded rather than blocking: **when the deck average and the
reference corpus disagree, which is the objective?** This pass assumed neither and reported both.
