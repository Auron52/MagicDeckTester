# The Aether Vial upkeep charge is a searched decision

2026-07-31. Item from the user's #6 list ("items in #6 should be searched by default with
heuristics for sure"). The last of that list to be a genuine branch — see
`engine-heuristics-to-providers.md` for why firebreathing is not.

## The decision

Aether Vial deploys a creature whose mana value **equals** its charge counter count. So at each
upkeep the choice is real and two-sided:

- **hold** at k — keep this turn's free deploy of an MV-k creature,
- **charge** to k+1 — trade it for an MV-(k+1) deploy from next turn on.

`WantVialCharge` (`SpellEffects.h`, provider-owned) is a good rule — hold while a creature of the
current MV is in hand, else climb toward a bigger one, else pre-charge toward the deck's dominant MV
— but it cannot see which side actually wins the game. It stays as the **default, fallback and
tie-break**; the answer is now searched. *(2026-09-03: REVERSED — the shipped decider is the
hand-aware root heuristic; `MTG_SEARCHED_VIAL` and `MTG_VIAL_AXIS` both default OFF since
3efbe969, 2026-08-30, the fan measuring −108 turns over 34,325 suite games. `=1` now OPTS IN.)*

## Shape (identical to the searched cleanup discard)

`AIEngine::DecideVialCharge` rolls the game out under both answers and takes the earliest win,
**heuristic first so it owns every tie** (defect class 3 — the search accepts only strict
improvements, so enumeration order decides every tie).

- gated on `LookaheadBottoming()` — inert at d0, so the greedy path is byte-identical;
- never inside a rollout (`m_in_rollout`) — the trial's own upkeep would re-enter this pass;
- deviating from the heuristic clears `m_committed_line` (defect class 2 — the line was searched
  assuming the heuristic charge);
- `MTG_SEARCHED_VIAL=0` restores the pure heuristic.

## Two resume hazards this had to solve (defect class 1)

The charge is decided **part-way through `UpkeepStep`**, so a trial that just calls `PlayOut` starts
a fresh turn and throws the rest of this one away. Two distinct pieces get lost, and both had to be
fixed or the labels would be scored against states the real game can never reach:

1. **The rest of the upkeep.** `GameEngine::UpkeepStep` continues past the Vial loop into the
   upkeep token creation (slivers' Thrumming Hivepool) and the closing `ResolveStack`. So the
   upkeep's tail was split into `GameEngine::UpkeepTail` and a new `ResumeAt::UpkeepTail` entry
   added *before* `Draw`; the trial resumes there. `UpkeepStep` calls the tail unconditionally, so
   the split itself is byte-identical (verified: full smoke pass with `MTG_SEARCHED_VIAL=0`).
2. **The other Vials.** The decision is taken inside a loop over the battlefield, and resuming at
   `UpkeepTail` is *past* that loop — so Vials after this one would silently lose the counter they
   would really have gained. `ChargeRemainingVialsHeuristic` charges them on the heuristic in the
   trial before the rollout starts.

This is the same failure that cost Treasure Hunt a real T4 win before `ResumeAt::Cleanup` existed.
Every searched mid-turn decision needs its own resume point; there is no generic one.

## Result — held-out (overnight) seeds, monotone

| deck | cases changed | sum Δ avg win turn |
|---|---|---|
| goblins | 8 | **−0.0720** |
| knights | 6 | **−0.0130** |
| slivers | 3 | **−0.0030** |
| **total** | **17** | **−0.0880** |

Per-game audit: **88 games faster, 0 slower, 0 play-changed at d0.** Every deck without an
`upkeep_adds_charge` permanent is byte-identical, and d0 is byte-identical everywhere (depth gate).
Smoke 4 faster / 0 slower, regression 5 faster / 0 slower, references 138 ok / 0 play-drift.

`MTG_VIAL_TRACE=1` prints the per-decision win turns under both answers.
