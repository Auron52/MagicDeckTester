# LandfallElementals mana-clean: restricted-mana oracle model + engine drops (checkpoint)

## READ FIRST
- Branch `wip/mc-lfe`, worktree `.claude/worktrees/mc-lfe` (from origin/phase-1-2-deck-analyzer 5e9408d2e, src 9d774fd9).
- Input: `logs/mana_clean/pass1-landfallelementals/report.md` (7,000 games, src 7ec55bb5) and
  `logs/mana_clean/pass1-minotaur/report.md` (73,000 games).
- Scratch: `build/scratch/mc-lfe/` (baseline binary `base/mtg` = origin src, slice driver `run_slices.sh <tag> <bin>`,
  flag counter `tools/count.py <on.err> <max_gi> [-s]`). Slice outputs: `<worktree>/build/scratch/mana_oracle/mclfe-<tag>-{lfe,mino}/`.
- Slices: LFE seeds 30000000+gi, gi 0-299; Minotaur gi 0-3999 (same seeds as the gate's first games).

## Task A: the oracle's model of SUBTYPE-restricted mana (Smokebraider, Giada)
Before: Smokebraider's tap put two units of ONE colour into the ANY-creature pool (`floating_creature_mana`), so a
unit this payment left over paid a later Metallic Mimic (a Shapeshifter in hand) -- the laundering the engine refuses.
That produced every ENUM-GAP (7/7: Hellhound off Smokebraider R, then Mimic {2} off the leftover + one land) and the
"R(creature-only)" labels. Its ability half ("or activate abilities of Elementals") was not modelled, so every table
holding Smokebraider was noted UNMODELED ("subtype-restricted mana from Smokebraider", 14.9M notes), and a summoning-
sick Smokebraider was noted "restricted-mana source" (4.07M notes) although it makes no mana.
Now (ManaOracle.cpp / ManaPayment.cpp / ManaOracle.h):
- `OracleCreatureTap`: a `mana_only_subtype` source books into the subtype reserve (`floating_bigspell_mana` +
  `_pp`, the payer's own home for it); an any-combination source books its units WILD. Ability half checked with
  `SubtypeAbilityManaOk` (the payer's predicate).
- `BuildSources`: ability route for `mana_only_subtype_abilities` sources; one face for an any-combination subtype
  source (wild = every colour pair); summoning-sick restricted sources are not notes; the unconditional
  "subtype-restricted mana" note is gone.
- Frontier carries the ability SOURCE (`Hook::f_ability_src`, captured in ManaPayment.cpp's frontier stop) into
  `MinimalPayments(..., ability_src)` -> `CreatureAbilityPayScope(src)`.
- Float-only payment (oracle threads only, `manaoracle::t_float_only`): reserve wild goes to the pips the general
  float cannot cover (exact cover) and the any-combination reserve's leftover is re-booked wild (commit_leftover's
  `sub_any_combo` form), so the oracle's board is the engine's.
- Supply: the restricted reserves' wild units count as supply in `MinimalPayments` and the refine table.
- Refine memo key: one bit per subtype source for whether it may pay this payment (spell / ability identity).
- NOTE: `MTG_PAY_REFINE` (default ON) runs `RefinePayment` -> `BuildSources` / `OracleCreatureTap` from the real
  payer, so this is reachable from PLAY. Identity must be checked (smoke), not assumed.

## Minotaur ORACLE-GAPs (48): NOT restricted mana (subagent trace, 7 repros)
- 45: the oracle's frontier prune (ManaOracle.cpp ~2083-2103) drops the engine's own poorer board, and the oracle
  cannot decline a payment it can make, so nothing covers the engine. 32 of them rest on a real PLAY bug: a
  Burning-Fist Minotaur pump paid with an EMPTY hand (TurnSolver.cpp ~65664 pays, then SpellEffects.h ~16944
  `if (ap.hand.empty()) break;`), wasting {1}{R}. 13: a Rakdos Carnarium continuation bounces an untapped land the
  oracle taps first.
- 3: copy folding vs the bounceland's positional pick (BuildSources keys copies together; BounceKarooLand picks
  "tapped first, then lowest index").
- Proposed (not done): re-Realize without the prune on a would-be GAP; key land copies apart while a bounce land
  drop is pending (thread_local set only in Realize). Empty-hand pump = play fix with its own A/B.

## Results of A (slices, same seeds, base = origin src 9d774fd9 vs A)
LandfallElementals gi 0-299 (oracle ms 1,445,600 -> 1,076,507, -26%):
| class | base | A |
|---|---|---|
| UNMODELED | 1185 | 23 (all "engine tapped a mana source outside any payment") |
| ENUM-GAP | 0 (7 in the 7,000-game gate) | 0 -- all 7 were the oracle laundering a leftover unit into Mimic |
| APPLY-DROP | 102 | 145 (real verdicts surfaced from the old UNMODELED decisions) |
| APPLY-DROP-ORDER | 10 | 1 |
| ORACLE-GAP | 49 | 35 |
| CAPPED | 374 | 363 |
| CONTINUATION-DIFFERS | 1562 | 196 (the leftover now floats wild, as the engine's does) |
| DOMINATED-CREATURE | 0 | 0 |
Notes: 72,323 "restricted-mana source" + 303,593 "subtype-restricted mana" notes -> 0. Play digest with the oracle
OFF identical (bccdf3197799c24b; units 6,676,519 vs 6,676,520). Minotaur gi 0-3999: every class unchanged
(APPLY-DROP 49, ORACLE-GAP 11), digest identical (60b8bcb1ccc7215c). Unit suite 2542/2543 (the one failure,
`tier2 overflow: concurrent per-thread stores`, is a flaky concurrency test that also fails on the base binary
and passes when re-run alone). Smoke: see Status.

## B: the LFE APPLY-DROPs after A (145 in the slice: depth1 132, depth2 13, reorder 1)
143 of 145 pay through Smokebraider in the oracle's script. Top shape (verified, seed 30000033 gi33 T4,
`{land=Mountain + cast:Risen Reef + cast:Mossborn Hydra}`): the engine pays Risen Reef {1}{G}{U} with Smokebraider
(G+U, correct) and SECLUDED COURTYARD for the generic {1}, leaving Islet/Myriad/Mountain and no {G} for Hydra
{2}{G}. Mountain for the {1} pays both. Cause: the narrow-restricted-first rank (`pay_narrow_first`,
NarrowRestrictedMask, ManaPayment.cpp ~2479-2510) spends a chosen-type land ahead of an unrestricted source
for a GENERIC pip, blind to a colour the rest of the line owes that only the restricted sources can make.
Same family in the other top signatures (Fulminator Mage / Risen Reef first, then Brushfire {R}{G} / Hydra {G}).
Classification: restricted-mana-specific (the restricted-first ordering), not the generic staged re-pay
(LINE_REPAY, owned on wip/mc-dragons). NOT FIXED YET -- proposed: the narrow-restricted-first rank stands down for a
generic pip while the line still owes a colour this source can make and the unrestricted untapped sources
cannot cover (needs the line's owed colours: LineOwedAfterOwnPips). Then the adoption bar (held-out A/B on LFE +
every deck with restricted mana, d8 b0 recovery of slower games, units/instr per deck).

## Minotaur APPLY-DROPs (403 in the gate): 0 restricted-specific, all generic (subagent, 203 repros)
- 372: the joint main-phase prepay (TurnSolver::BatchPrepayMainCasts, TurnSolver.cpp ~57674 PrepayColoursFit)
  settles Boros Reckoner's {R/W} pips as W, but each cast then re-pays its own hybrid cost from the float and the
  hybrid wrapper (ManaPayment.cpp ~8643-8686, ExpandHybrids bits 0 = all R first) takes the R -> Rageblood /
  Slaughter-Priest lose their {R}. MTG_NO_BATCH_PAY=1 on the same seed/turns: 427 -> 35. Fix direction: pay each
  cast with the prepay's decided hybrid expansion. Generic (Plains would behave the same).
- 15 per-cast tap order (Neheb's {1} off the Mountain, leaving Carnarium which cannot pay {R/W}{R/W});
  16 bounce look-ahead (tap the land a Carnarium is about to return).

## Status / next steps
- DONE: A committed on wip/mc-lfe (5d6d33be4). Smoke on A: RUNNING at handoff (pid 2886884, file
  build/scratch/mc-lfe/smokeA.pid, log build/scratch/mc-lfe/smokeA.log); 137 of ~169 rows finished, 137
  byte-identical to GT, 0 moved. Check the rest with
  `python3 build/scratch/mc-lfe/tools/cmpgt.py test/regression_gt.txt build/scratch/mc-lfe/smokeA.log`
  (from the worktree). If a row moves, A is a PLAY change via MTG_PAY_REFINE and needs the A/B before it lands.
- NEXT: (1) read the smoke result; (2) implement the B fix above, scoped to the restricted-first rank; (3) A/B per
  the adoption bar; (4) Minotaur ORACLE-GAP oracle fixes (prune retry, bounce copy keying) and the empty-hand
  Burning-Fist pump play bug are surfaced to the USER, not done.

## Successor 2 (2026-11-04) -- progress
- A: smoke 169/169 identical, 0 play-changed. Re-committed src-only as b8f0df7d3 (handoff doc separate).
- TEMP DIAG (remove before landing): `MTG_TAPDBG=2` prints [tapdbg]/[paydbg]/[nddbg] inside the oracle's EngineApply
  (`manaoracle::t_eapply_dbg`, ManaOracle.cpp EngineApply; TapDbgReal in ManaPayment.cpp).
- B ROOT CAUSE (trace, seed 30000033 gi33 T4, scratch r33d.err): NOT the generic {1}. The engine casts Reef BEFORE the
  Mountain; Reef's {U} pip candidates are Fiery Islet (20 + pain band 64 = 84), Secluded Courtyard 59, Smokebraider (65,
  creature band) -> the Courtyard pays {U}, Smokebraider pays {G} + the generic, and Hydra's {G} has no source left.
  Fix (uncommitted, MTG_PAY_CCO_LINE_RESERVE default ON): while LineOwedAfterOwnPips owes a colour the unrestricted
  untapped supply cannot make, a cco land that makes it gets key[0]=66 (after the creature band). Ordering only.
- Task 3 (uncommitted, MTG_HYBRID_FLOAT_ORDER default ON): the hybrid wrapper tries assignments in order of the
  shortfall each leaves against the line's later demand on the float. Minotaur seed 30000038 gi38 T5: 8 drops -> 0.
- Task 4 pump (uncommitted, MTG_PUMP_HAND_CAP default ON): PumpPayableActivations / PumpCostForActivations
  (TurnSolver.h) cap a mode-1 (discard) pump's paid activations at the hand size, both pay sites (TurnSolver.cpp
  trailing pass, AIEngine.cpp executor).
- A/B tooling: build/scratch/mc-lfe/ab/run_ab.sh <tag> <bin> "<envA>" "<envB>" <seed> <deck:games>..., cmp.py <dir>.
- Minotaur oracle slice gi0-3999 (binary C = A + hybrid + pump): APPLY-DROP 49 -> 3, ORACLE-GAP 11 -> 2, DOMINATED-LAND
  1165 -> 1008; oracle-off identity holds.
- Held-out A/B (seed 41000000, Minotaur 10,000 games, one binary, flags per arm, 2 threads/arm, scratch ab/mino34):
  HYB vs off: net 0, 0 slower / 0 faster (digest moves); instructions +0.0004%, units -0.003%.
  PUMP vs off: net 0, 0 slower / 0 faster; instructions +0.05%, units +0.075%.
- B attempt 1 (held cco land in key[0]=66, behind the creature band): seed 33 fixed, but LFE slice APPLY-DROP 145 -> 583
  (Smokebraider then pays Hydra's {G}; the generic's Mountain/Island rank-10 tie takes the Mountain the line's {R}
  needs; seed 30000097 gi97 T4). Attempt 2: the held land stays in the land band at key[2] += 2*kPainBand.
- COMMITTED: pump cap 67b3d46ef, hybrid order d8c7bbdb2 (A re-committed b8f0df7d3).
- Smoke, hybrid+pump only (binary D with MTG_PAY_CCO_LINE_RESERVE=0; log scratch smokeHP.log): 165 pass, 4 moved, 0
  slower: fivecolour d0 s1001 (same avg), minotaur d0 s1001 5.3060 -> 5.3050, melira d0 s1001 6.2610 -> 6.2590,
  melira d5 s1001 (same avg). GT NOT accepted.
- B attempt 2 (binary E): LFE slice APPLY-DROP 145 -> 132, oracle-off play digest identical to A (bccdf3197799c24b).
  Seed 33 fixed. Remaining top: seed 30000163 (Fulminator Mage {1}{B/R}{B/R} paid with no float: bits 0 = {B}{B} takes
  the Courtyard's B -- the held land still ranks ahead of Smokebraider's creature band; the R/R assignment is never
  tried because B/B succeeds), seed 146 / 10 (Reef + Brushfire, Waterlogged Grove keep-floating). Not fixed.
- RUNNING: LFE cco A/B (ab/lfecco, 1500 games), hybrid A/B on Melira Pod + FiveColour (ab/hybmf, 1500 each).
- COMMITTED B (attempt 2) 2868deca5; diag removed from src (binary E still carries it, inert unless MTG_TAPDBG=2).
- A/B results: LFE cco 1,500 games net 0 (0/0), instr +0.17%, units -0.04%. Hybrid on FiveColour 1,500 net 0 (0/0),
  Melira Pod 637 games net 0 (0/0; stopped early by me -- d5 b120 is ~12 games/min/arm on 2 threads).
- Final smoke (binary E, scratch smokeE.log): 164 pass, 5 moved, 0 slower, 3 faster: the four above + elementals d0
  s1001 (same avg). Unit suite 2543/2543 (run build/Release/mtg-test from the worktree; a copy run from scratch fails
  88 path-dependent tests).
- NOT DONE (task 4 oracle-only): frontier-prune retry on a would-be GAP (ManaOracle.cpp ~2083) and land-copy keying
  while a bounce land drop is pending. Remaining LFE drops: seed 30000163 (no-float hybrid {B/R}: bits 0 = {B}{B}
  succeeds on the Courtyard's creature-B, so R/R is never tried -- a line-aware hybrid order for TAPPED payments, or
  the held cco land ranking behind a restricted dork, are the two levers), seeds 146/10 (Reef + Brushfire with a
  Waterlogged Grove keep-floating).
- RUNNING at handoff (successor 2): gate slice on FRESH seeds 42000000 with binary build/scratch/mc-lfe/E/mtg --
  LFE 600 games then Minotaur 6000 games (scripts/mana_oracle.py, 8 threads; logs build/scratch/mc-lfe/G-lfe.log,
  G-mino.log; reports logs/mana_oracle/mclfe-G-{lfe,mino}/report.md in the worktree).
- Same-seed before/after (slices): LFE gi0-299 APPLY-DROP 145 (A) -> 132; Minotaur gi0-3999 APPLY-DROP 49 -> 3,
  ORACLE-GAP 11 -> 2 (Minotaur measured on binary C = A + hybrid + pump).
- NEXT: read the G slice; scoped regression `THREADS=8 bash test/regression.sh --deck=minotaur,melira,fivecolour,elementals`
  (not run); GT not accepted (coordinator). Open: remaining LFE drop shapes above; task-4 oracle-only gap fixes.
