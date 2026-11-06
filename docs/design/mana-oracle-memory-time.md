# Mana oracle: the memory blow-up (FIXED 2026-11-05) and the slow-game time (FIXED 2026-11-06)

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

## 2. Time: root cause and fix (levers (b), (c), (d) LANDED 2026-11-06; (a) not needed yet)

The fixed Galadriel batch still took 34 min for 32 games (one game 2,265 s), and five decks (Snow,
LandfallElementals, AccomplishedAlchemist, NivMizzetCombo, EldraziDisplacerFlicker) stalled on single games of
1-4 h. 98% of oracle time was in plans that end CAPPED.

**(d) Profile** (`perf`, Profile build, Galadriel seed 30000596 alone: 19 min 49 s, 81 plans, 464,224 applies,
**45.7 M payment options**). About 97% of the time is `MinimalPayments`' option DFS, and almost all of that is
`PayEnum::Verify` paying each option on a scratch board (`PayAndRecord`, 58% inclusive). Within that,
`MidTurnStateKeyOfPayment` keys each result to discard duplicates (`BuildSimKeyBase` 24%, `LandTypeChangedToIsland`
12% self). Applying scripts is about 1%. The cause: every kept frontier was enumerated in full (18-22k options,
each verified), to apply a few hundred of them.

**(c) Lazy enumeration** (USER 2026-11-06: "that sounds fine"). A frontier's DFS stops at
`room - next.size() + 1` options (`MinimalPayments(..., max_opts)`, `PayEnum::want`). The extra one tells Realize
whether the layer is cut. A kept frontier met once the layer is already full is not enumerated, because it can
only cut the layer. The scripts built, their order, every outcome and the CAPPED verdict are those of the full
enumeration. **What is lost:** the "payment enumeration capped" UNMODELED *reason tally*, on plans already CAPPED
(the note is not computed past the cut). No flag moves. Every UNMODELED emission is guarded by `r.capped`
(AuditPlan returns before it, and AuditLine reports the cap first), so a CAPPED plan never emitted UNMODELED for
that note anyway.

**(b) The retry continues from the cut.** Under a larger ceiling a layer's scripts are the same list cut later.
A capped pass leaves a `RealizeCursor`, holding:
- the kept frontiers of the layer it was building and where the cut fell;
- the scripts before the cut;
- the boards the applied part of the next layer reached;
- the outcomes and notes so far.

The retry rebuilds that layer from the cut and applies only the new scripts. The notes are restored to fresh-run
order, because AuditPlan reads the first one. `MTG_ORACLE_RESUME_CHECK=1` (diagnostic) runs the fresh retry too
and aborts on any difference.

**(a) Per-game audit memo: not built.** With (b)+(c) the oracle is ~16x cheaper. Revisit only if a deck still
stalls.

**Measured** (the 32 Galadriel games of section 1, 32 threads; "old" = f6697cedf):

| | old | (b)+(c) |
|---|---|---|
| wall | 34 min 18 s | **2 min 29 s** |
| oracle ms (summed over threads) | 12,161,274 | 770,310 |
| options enumerated | 408,879,187 | 5,688,534 |
| applies | 6,134,047 | 5,566,047 |
| result lines, oracle lines (897), UNMODELED=367, CAPPED=235 | | **identical** |
| "payment enumeration capped" tally | 286,403 | 12,878 |
| peak RSS | 6.7 GB | 14.9 GB (6.0 GB at 8 threads) |

The higher peak is concurrency, not a leak: the 32 games now overlap in their worst plans. Each 20,000-apply
retry holds its `outcomes` boards (~300-700 MB, section 1), and the peak scales with the thread count. With
`MTG_ORACLE_RESUME_CHECK=1`, 284 of 284 resumed retries equalled the fresh ones (Galadriel).
SelesnyaLifegain (the 32 games of section 1, with the check on): all 32 finished in **5 min 20 s**. That
includes gi 8437, which was stopped at 4.3 h on the old binary. The 31 games both runs finished agree on every
result and all 2,541 oracle lines, and 1,683 of 1,683 resumed retries equalled the fresh ones.

## 3. Reproduce

The manifests are under `build/scratch/oracle-mem/` (`galadriel.json`, `selesnyalifegain.json`). They hold one
job per game, with seed `30000000 + gi` and `game_index gi`. Run with
`MTG_MANA_ORACLE=2 build/PGO/mtg --batch <manifest> --threads 32`. The census patch is on branch `oracle-mem`
(worktree `.claude/worktrees/oracle-mem`), commit it there if it is needed again.
