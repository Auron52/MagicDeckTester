# Mana oracle: the memory blow-up (FIXED 2026-11-05) and the slow-game time (OPEN)

The mana-clean pass (`scripts/mana_clean.py`, oracle arm `MTG_MANA_ORACLE=2`) lost two decks to the per-process
RSS cap: Galadriel (`pass1-galadriel-r2`, 43 GB in ~2 min) and SelesnyaLifegain (`pass1-selesnyalifegain`, 43 GB
after ~33 min). Galadriel is also the likely cause of the container OOM of 2026-11-04 ~12:50Z. Several more decks
(Snow, LandfallElementals, FiveColour, AccomplishedAlchemist, NivMizzetCombo, EldraziDisplacerFlicker) stalled
on single games that ran 1-2.6 h. Play is unaffected in both cases: the oracle is a checker, and the off arm is
byte-identical.

## 1. Memory: root cause and fix

**Mechanism.** `Realize` (src/ai/ManaOracle.cpp) searches payment scripts layer by layer. Every kept frontier
times every `MinimalPayments` option becomes a script of the next layer, and that layer was **fully
materialised before the apply ceiling** (`MTG_MANA_ORACLE_NODES` 2,000; CAPPED retry 20,000) was consulted. A
frontier can offer >20,000 options (the 4,096 enumeration cap is per stash choice), so the next layer can hold
millions of scripts while only `NodeCap() - nodes` of them can ever be applied.

**Measured** (diagnostic census on scratch branch `oracle-mem`, never landed; the 32 Galadriel games in flight at
the original abort, one batch, 32 threads, `MTG_RSS_CAP_GB=40`):

| Call | Scripts built | Est. size | Applies allowed | Wall |
|---|---|---|---|---|
| seed 30000126 T4 | 5,573,075 | ~9.4 GB | 2,000 | 160 s |
| seed 30000910 T4 | 2,027,521 | ~3.0 GB | 2,000 (and again at 20,000) | 58 s |
| seed 30001482 | 1,837,502 | ~2.8 GB | 2,000 / 20,000 | 61 s |
| seed 30000775 | 1,718,754 | ~2.3 GB | 2,000 / 20,000 | 65 s |

82 of 416 logged calls built a >500 MB layer. Before the fix the batch was aborted at 40.45 GB after 3 min 25 s,
with 12 of 32 games done. The engine's own pools (`fsl`, `plan`) were near zero at every abort.

**Fix (landed).** Build the next layer only up to `room = NodeCap() - nodes` scripts. If any script is cut, set
`capped`: the dropped script would have been met at the ceiling and done exactly that. The applied prefix, every
outcome, the CAPPED verdict and the UNMODELED notes are unchanged (every kept frontier is still enumerated).

**Verified.**
- The same 32 games ran to completion with peak RSS **6.7 GB** (was >40 GB, killed).
- On the 12 games both runs finished, result lines and all 287 oracle lines are identical.
- SelesnyaLifegain (the 32 games in flight at its abort): 31 of 32 finished, peak RSS **5.7 GB** (was 43 GB,
  killed). The 32nd, gi 8437 (seed 30008437 --game-index 8437), was stopped at 4.3 h. That is the TIME problem
  of section 2 (an elf board of 24 permanents with three Priests of Titania and three Archdruids; plans cap at
  20,000 applies, ~317 s per audit), not memory.

A second, smaller holder remains. `outcomes` keeps a full board per apply, about 300-700 MB per call at the
20,000 retry (112 calls). That is harmless at 6.7 GB peak; revisit only if a deck shows it.

## 2. Time: root causes (OPEN, for discussion)

The fixed Galadriel batch still took 34 min for 32 games (one game 2,265 s). The census says:

1. **98% of oracle time is in plans that end CAPPED.** Each one first runs the 2,000-apply pass, caps, and is
   redone from scratch at 20,000. That is ~10% repeat work per capped plan.
2. **The same (board, plan) is audited repeatedly.** Seed 30000596 T4 emitted 5 of its CAPPED lines three times
   each, byte-identical (same board, same plan). Each audit of that plan costs 175-298 s at the retry ceiling.
3. **Every kept frontier is enumerated in full.** That is up to 18,000-22,000 options, each verified by paying on
   a scratch board, even when only a few hundred applies remain. The memory fix kept this (to keep UNMODELED
   notes identical), so it still costs time.
4. **The combinatorics are real.** For example, Galadriel with ten-odd distinct Temples and duals: boards
   with different duals left untapped are Pareto-incomparable, so the frontier prune cannot fold them. The
   identical-source fold only helps boards of truly interchangeable sources (Crackle X on basics, elf boards).

**Candidate levers** (none implemented; numbers owed per lever):

- **(a) Per-game memo of audit results**, keyed on the full board + plan + `pre`. This is lossless if the audit is
  a pure function of those. Risk: state outside `GameState` (see `oracle-dedup-deferred.md`), so the key must be
  verified with a re-run-and-compare mode first.
- **(b) Continue the retry from the 2,000 pass instead of restarting.** This is lossless (the same prefix in the
  same order) and saves the first pass, ~10%.
- **(c) Lazy enumeration.** Stop the option DFS once `room` options exist. This changes the UNMODELED
  "payment enumeration capped" notes, but only on plans already CAPPED. That is a disclosure change, so the
  USER decides.
- **(d) Profile the enumeration** (`perf` on seed 30000596 alone) before choosing among (a)-(c).

## 3. Reproduce

The manifests are under `build/scratch/oracle-mem/` (`galadriel.json`, `selesnyalifegain.json`). They hold one
job per game, with seed `30000000 + gi` and `game_index gi`. Run with
`MTG_MANA_ORACLE=2 build/PGO/mtg --batch <manifest> --threads 32`. The census patch is on branch `oracle-mem`
(worktree `.claude/worktrees/oracle-mem`), commit it there if it is needed again.
