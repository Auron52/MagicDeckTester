# Perf: same-turn creature→aura enumeration is slow on aura-dense states (Auras)

Status: **open, deferred** — a performance pathology, not a correctness/crash bug. The engine
produces correct results; some searches are just very slow. Recorded here so it isn't lost.

## Symptom

On the Auras (Bogles) deck, deep search / rollout on **aura-dense hands+boards** can be pathologically
slow — a single decision can spin for a very long time:

- **Value-rows dump** (`MTG_DUMP_VALUE_ROWS`, `MTG_EVAL_ROWS_K=8`, d3 play): one game's *single*
  pre-combat-main decision ran for **>100 s** (0 rows emitted across 100 s while the K=8
  `EnumerateEarliestWins` oracle ground on that one decision). Most games are fine; a minority stall.
- **Exhaustive mulligan keep-gen** (`mtg-analyze`, d5/b20 rollouts): the Auras R=1 full pass was
  ~24 min and R=40 took ~9 h. Much of that is the sheer hand count (K=19, 367k hands), but the
  per-rollout cost is inflated by the same enumeration blow-up on aura-heavy playout states.

## Suspected mechanism

Commit **`f408f64`** ("feat(auras): enumerate same-turn creature → aura enchant lines") added
`AppendCreatureTargetAuraCandidates` (`src/ai/TurnSolver.cpp` ~L6278, called ~L6404). For each plain
Aura in hand × each creature in hand it injects a `CastFromHand` candidate that enchants a
to-be-cast creature. On an aura-dense hand (Auras runs many auras + several 1-drop hexproof
creatures) this multiplies the candidate/plan set combinatorially, and that inflated set is then
explored **K times** (K reshuffles) inside the earliest-win oracle and at every search leaf. The
result is a plan-space explosion concentrated on exactly the states Auras reaches often.

The feature itself is correct and worth keeping (it enabled the same-turn creature→aura lines, −0.02t
on Auras); the issue is purely the *cost* of enumerating it unpruned.

## Where to look / possible directions (unexplored)

- Bound the injected set: cap the number of same-turn (aura, creature) pairs, or only inject for the
  best-ranked aura/creature rather than the full cross-product.
- Dedupe by end-state earlier (many aura×creature pairs collapse to equivalent boards).
- Gate the injection behind a cheap "is this state aura-dense enough to matter" check.
- Confirm with a profile which of enumeration vs per-leaf rollout dominates on a stalled decision.

## Repro

Any Auras hand/board with multiple auras + multiple creatures in hand. Quickest trigger seen:

```
MTG_DUMP_VALUE_ROWS=/tmp/x.rows MTG_EVAL_ROWS_K=8 \
  build/Release/mtg decks/Auras/Auras.cod --games 2500 --seed 8008 --depth 3 --max-turns 8 \
  --threads 12 --ignore-play-profile
```

Most games fly; watch for a game whose row output stalls for tens of seconds on one decision.

## Related

- `f408f64` — the enumeration this note is about.
- `docs/design/auras-value-leaf.md` / `docs/design/auras-mulligan-profile.md` — where the slowness
  bit during model generation (both completed successfully despite it).
- `docs/design/same-turn-cost-reduction-fidelity.md` — a *different* same-turn issue (Hinata
  cost-reduction fidelity), unrelated to this enumeration cost.
