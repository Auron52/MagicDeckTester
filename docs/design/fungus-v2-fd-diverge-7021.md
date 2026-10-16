# A confirmed rollout-vs-executor divergence on the adopted Fungus list (seed 7021)

**Status: CONFIRMED DEFECT, ROOT CAUSE OPEN.** Found 2026-10-16 by `verify_deck.py`'s `mismatch`
gate during the Fungus v2 list adoption. Written to `docs/design/` rather than agent memory per
CLAUDE.md's "deferred work goes in git" rule, because it is a real engine defect that outlives the
adoption and anyone may pick it up.

## The repro (deterministic, one command)

```
MTG_FD_ORACLE=1 ./build/Release/mtg decks/Fungus/Fungus.cod \
  --profile decks/Fungus/Fungus.profile.json \
  --games 1 --seed 7021 --depth 5 --budget-ms 20 --lookahead-bottoming --threads 1
```

```
[fd-diverge] seed=7021 realized_win=5 predicted_win=4 proven_at_turn=4 leaf_est=none
```

The search proves a turn-4 win; the real game wins on turn 5.

## What is established

**IT IS NOT BUDGET STARVATION.** More search makes it worse, not better — the wrong turn-4 win is
proved *earlier* as the budget grows, which is the opposite of the starvation signature:

| arm | result |
|---|---|
| d5 b20 | diverges, `proven_at_turn=4` |
| d5 **budget 0 (unlimited)** | diverges, `proven_at_turn=1` |
| d7 b1000 | diverges, `proven_at_turn=2` |
| d5 b20, `MTG_FADE_K_CDA_SCALING=0` | **diverges identically** — the lever does NOT own it |

**RATE: 1 game in 300 on this list; 0 in 300 on candidate-b; 0 in 300 on v1.** One pooled batch,
900 games, `MTG_FD_ORACLE=1`, d5 b20, seed base 7001 (`logs/fungus_screen/fdscan/`). Note
candidate-b also plays 4 Saproling Burst and did not reproduce it, so this is not a bare
"Saproling Burst is wrong" defect — it needs this board shape.

**THE KICKER IS NOT INVOLVED.** Saproling Migration never appears in the game. Ruled out by the
game log, not by argument.

**THE DAMAGE ARITHMETIC IS EXACT.** At the turn-4 attack the opponent goes 17 -> 1, i.e. 16 dealt:

```
Brightcap Badger            3/4   ->  3
1/1 Saproling Token               ->  1
3x Burst token, CDA 4/4           -> 12     (Saproling Burst entered fading 7, 3 activations in main 1)
                                    ----
                                      16    against 17 life -- exactly one short
```

Utopia Mycon is power 0; Saproling Burst, Wild Growth and Concordant Crossroads are noncreatures;
no Sporecrown Thallid and no Beastmaster Ascension are on the battlefield. So **nothing else on that
board can contribute power.**

## The hypothesis that the arithmetic suggested, and why the CODE DOES NOT SUPPORT IT

The one body on the board not counted above is Brightcap Badger's end-step token
(`endstep_tokens_unconditional`, `endstep_token_power: 1`) — worth exactly the missing +1. The game
log confirms it exists and confirms *when*: the second `1/1 Saproling Token` appears between the
turn-4 `MAIN_2` frame and the turn-5 `DRAW` frame, i.e. after combat. With Concordant Crossroads on
the battlefield it has haste, which would make it *look* attack-eligible to anything that does not
also check that it has not been created yet.

**But `PerformEndStepLifegainTokens` is genuinely in lockstep.** It is called from
`GameEngine::EndStep` (executor) and from `TurnSolver::SimulateEndAndStartNextTurn` (rollout), and
both create the token at the end step, after combat. Ocelot Pride's `cards.json` note calls out this
exact risk in writing — *"lockstep, or the rollout projects a board the real game keeps growing
every turn (a systematic fd-diverge)"* — and the lockstep is in place. So the simulated board is not
the divergence, and **this hypothesis is not confirmed**: the +1 arithmetic is a coincidence until
something shows otherwise.

## Where to look next

The remaining suspect is the **lethality/damage ESTIMATOR, not the simulated board** — a separate
path from `PerformEndStepLifegainTokens`. `MTG_FADE_K_DUMP=1` prints the fade-K menu's own view with
`np`/`na` (power/attackers) and `win_now`/`win_next` fields; on this game some rows read `na=7` where
the battlefield holds six creatures including the power-0 Utopia Mycon. That is the first thread to
pull: find what the estimator counts as an attacker and compare it to `CanAttackFull` at the same
state.

Concrete next steps, cheapest first:

1. Dump the estimator's attacker set at the turn-4 decision and diff it against the executor's
   declared attackers. The gap should be one body; identify it.
2. If it is the end-step token, the bug is in the estimator crediting a not-yet-created permanent,
   and the fix belongs there — NOT in `PerformEndStepLifegainTokens`, which is correct.
3. Re-run the 900-game scan after any fix; the bar is 0 fd-diverge lines, and the rate is low enough
   (1/300) that a 300-game arm cannot confirm a fix on its own. Size the confirmation at >= 3,000
   games, which costs minutes.

## Two instrument defects found alongside it, both worth fixing independently

1. **`verify_deck.py` sets `MTG_FULL_DEPTH`, which this binary does not read.** The engine prints
   `[flags] WARNING: MTG_FULL_DEPTH is set but is not a flag this binary reads -- a typo or a
   deleted flag? It will have NO effect`. So the gate's `fd_oracle` arm has not been running the
   mode its own docstring names (`MTG_FULL_DEPTH+MTG_FD_ORACLE`). The oracle still fires, so the
   gate is not useless — but it is weaker than it claims, and the fix is to drop the dead flag or
   restore whatever replaced it.
2. **Its mismatch seeds overlap.** Default seeds are `7001 7002` with many games each, so seed 7021
   is inside BOTH jobs' ranges and one divergent game was reported as two findings. Same class as
   the overnight tier's documented "bases must be spaced strictly wider than the per-case game
   count" rule, which the overnight rows already follow and this gate does not.

## Why this was not caught before the adoption

v1 plays no Saproling Burst and no Brightcap Badger, so it cannot reach this board. candidate-b
plays 1 Badger and 1 Crossroads against this list's 4 and 1 — the shape is far rarer there, and it
did not reproduce in 300 games. The list change did not create the defect; it raised the rate at
which the suite meets it. That is the suite addition doing its job.
