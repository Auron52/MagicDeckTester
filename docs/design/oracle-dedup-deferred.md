# Mana oracle: post-payment board dedup -- DEFERRED (2026-11-03)

**Goal.** Cut the oracle's CAPPED verdicts (a plan whose realisation exceeds the 2,000-apply ceiling; a FAIL
class under the USER's 2026-11-03 ruling) without the 20,000-node retry (`35a74a635`, held: ~3.7x oracle CPU on
the Goblins CAPPED set and ~66M extra applies on LandfallElementals gi4793). CAPPED comes from the payment
permutation explosion in `Realize` (src/ai/ManaOracle.cpp): many payment orders reach the same board.

**Mechanism tried.** `PostPayOf` (ManaOracle.cpp ~2085) applies an option's payment on a copy of the frontier's
board; `Realize` (~2194, seen-table ~2393) skips an option whose post-payment board equals an earlier one. A
skipped frontier is dropped (the prune keeps the first of equal boards); a skipped outcome is rebuilt from the
earlier option's outcome with the skipped option's own script, so the outcome list, its order and `r.hit` stay
the same. `MTG_MANA_ORACLE_DEDUP_VERIFY=1` re-runs every skipped apply and reports any divergence.

| Variant | Wrong skips (verify) | Records | CAPPED / applies |
|---|---|---|---|
| exact, within one frontier | 0 (never fires) | identical | unchanged |
| exact, across frontiers in a layer | Goblins 0 / 67,340 | Goblins identical | Goblins 74: 1,826 -> 1,803 |
| + plan position | LFE 6,647 / 1.4M | LFE gi193: 6+6 DOMINATED-LAND records differ | LFE slice 59 -> 0 |
| + the executor's `available` pool | LFE 6,647 / 1.4M (unchanged) | same LFE difference | -- |
| the prune's identity after eaten bodies are removed | Goblins 8,791 / 500,919 | matched by luck; unsound | Goblins 74: 1,826 -> 8 |

**Why it is parked.** The only provably exact form finds nothing. The forms that find something are wrong on
~0.5-1.7% of skips: two payment orders leave equal boards (and an equal `available` pool) but continue
differently, so the payment leaves state OUTSIDE the board that later steps read. That state is unidentified.
The oracle is the checker the mana gate trusts, so an unsound skip is rejected outright (no A/B can buy it).

**To resume.** Find what a payment leaves outside `GameState` + `available` that a later step reads (candidates:
executor-side per-turn records, a payment's floating-mana provenance, the plan cursor). Use the verify mode on
LandfallElementals gi193. Once it is in the key, re-measure the cross-frontier form on the Goblins CAPPED set.
Work branch: `wip/oracle-perf` (head `cadfa351a`, WIP `8f3ffb28c` / `9f76c2a29`; its handoff
`docs/design/handoff-oracle-dedup-mana.md` holds the full numbers).
