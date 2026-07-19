# Dragonstorm plan-execution fidelity bug (found by the 5d Claude-play sweep)

**Status:** **FIXED** (commit `69a26d4`) for the storage-under-burst trigger. Found by the
Stage-5d claude-play sweep (seed 900001, 16 games, commit ~`741c78d`, 2026-07-19). One verify
agent confirmed it with a precise reproduction + root cause; ~5 other independent sweep flags
describe the same root cause. This is a real correctness bug — NOT a heuristic to tune.

**Fix (trigger 1 — storage under-burst):** storage lands now BURST ALL live counters on tap
(`amt == consumed == had`) in both mirror sites (`TurnSolver::tap_source` +
`AIEngine::tap_source`), so the executor delivers exactly what the planner credited. Validated:
smoke **18/0/0** (byte-identical — no GT deck uses `storage_land`/`sac_for_mana`); Dragonstorm
search avg **6.0 → 5.06** (the dropped-Scourge tempo loss is gone on 15/16 sweep games, matching
the claude oracle's T5). Banking excess counters is now via the RESERVE (hold an unneeded storage
land untapped), not a partial burst — accepted for this deck (it bursts in one turn); the
multi-turn "bank a partial amount while tapping" optimization is deferred (low value here).

**RESIDUAL (trigger 2 — menu offers unpayable orderings), NOT fixed, disclosed:** the plan
enumerator still offers cast-orderings where a spell is listed BEFORE the mana source (Lotus sac
/ a ritual) that funds it (e.g. Apex ordered before its rituals); selecting such a plan no-ops
that spell (and an aborted cast still taps lands → a depletion-counter leak on Sandstone Needle).
The SEARCH never picks these dominated orderings (so play win-rate is unaffected — game 6 stays
T6 for a separate search-enumeration reason, likely needing a main1/main2 split), but a human in
claude-play could. Lower priority; a follow-up should canonicalize plan action order (mana sources
before dependent spells) or reject unpayable-at-position orderings in the enumerator.

## Symptom

On a go-off turn, the main-phase plan menu advertises a plan whose `casts` summary lists
N spells, but the executor **silently drops one of them** — the dropped spell stays in
hand, its effects never happen, and the plan's advertised state delta does not occur. The
enumeration (plan summary) and the execution (ApplyPlanDirect) diverge.

Concretely (seed 900001, game-index 6, T4 pre-main, decision 6):
- Plan index 0 summary: "Desperate Ritual, Lathliss, Pyretic Ritual, Rite of Flame,
  **Scourge of Valkas**, Lotus Bloom (sac)". On execution, **Scourge is NOT cast** —
  still in hand, opponent stays at 20 (zero Scourge pings), no Lathliss token.
- Plan index 1 is the IDENTICAL 6-spell multiset (only Rite/Pyretic order swapped) and
  casts all six: Scourge enters, pings twice (opp 20→17→14), Lathliss 5/5 token created.
- So the line is provably legal + payable; the executor drops a castable spell based on
  irrelevant cast-order permutation.
- **Reproduce:** `--choices "1,32,27,-1,35,-1,0"` drops Scourge; `"...,1"` casts it
  (`./build/Release/mtg decks/Dragonstorm/Dragonstorm.cod --profile
  decks/Dragonstorm/Dragonstorm.profile.json --claude-play --seed 900001 --game-index 6
  --max-turns 9 --reveal 6 --choices "<CSV>"`).

## Root cause (two related triggers, same defect)

The greedy executor pays spells in listed plan order, and each mana-source action is
resolved myopically for the CURRENT spell's shortfall only:

1. **Storage-land under-burst** (`src/ai/TurnSolver.cpp:2485`, mirror
   `src/ai/AIEngine.cpp:2672`): the storage partial-burst computes
   `need = max(1, cost.ManaValue() - produced_total)` scoped to the single spell, removes
   only that many storage counters, and taps the land. Mercadian Bazaar's oracle allows
   removing **any number** of storage counters in ONE tap (one opportunity per turn), so
   under-bursting (1 of 2 counters → 1 red instead of 2) **strands the 2nd counter** for
   the rest of the turn — a later spell then comes up 1 red short and is dropped.
2. **Mana-source ordered after the spell it funds:** when a Lotus Bloom sac (or a ritual)
   is listed AFTER a spell in the plan, that spell cannot pay at its position (its funding
   mana hasn't been floated yet) and is silently dropped. Same manifestation for Apex of
   Power (game showed Apex dropped when ordered before its funding rituals).

Because which amount gets burst / when the source resolves depends on the running shortfall
at tap time (which shifts with irrelevant cast order), an advertised castable spell is
dropped based on cast permutation.

## Impact

Costs the search a full turn on this deck: the search's #1-ranked plan drops Scourge and
wins T6, while the correctly-mustered line (proven by plan 1 / a main1+main2 split) wins
T5. The 5d sweep's Claude beat the search **T5 vs T6 on all 16 games** for this reason.

## Fix direction (to design against the MTG-rules skill)

Make storage-land burst (and mana-source ordering) satisfy the WHOLE plan's remaining red
requirement, not just the current spell's shortfall — i.e. the executor must look ahead to
the plan's total need when deciding how many storage counters to burst / when to float a
source, so a legal advertised multiset is never dropped by ordering. Must stay lockstep
across executor (AIEngine/EffectHandler) + rollout (TurnSolver) + planner, and byte-
identical for non-storage / non-float decks. Verify with the same reproduction + a smoke
run.

## Other sweep flags (context)

- verified-FALSE (correctly dismissed): `wrong_mana_color` (Lotus floats an off-color —
  but plan 1 also does and works, so not the defect), 2× `illegal_plan` (enumerator
  offering dominated cast orderings that no-op — a subset of the same ordering issue).
- UNVERIFIED (verify agent died on session limit) but same root cause:
  `plan_execution_divergence` (Lathliss+Scourge, Apex), `planner-executor-mana-divergence`,
  `wrong-state-delta` (aborted-cast still taps lands → depletion-counter leak on Sandstone
  Needle). Worth folding into the fix + a targeted re-check.
