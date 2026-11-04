# Bruna mana-clean: handoff (2026-11-04, context cap)

## Status in one line
Four mechanisms are written and WIP-committed but **not yet measured**. Nothing here can land yet: no slice
result, no unit-test result, no A/B and no smoke.

## Where things are
- Branch `wip/mc-bruna`, worktree `.claude/worktrees/mc-bruna`, based on `wip/mc-lfe` 7ce73a439 (it carries c75ba845d,
  the subtype-restricted oracle model). Rebase onto origin/phase-1-2-deck-analyzer once wip/mc-lfe lands. Nothing is pushed.
- WIP commits: 5fbe3f386 (burst lead + the Troyan and Aura-host oracle changes + test), a645c71b2 (line-critical
  land + test), plus this doc.
- Scratch: `build/scratch/mc-bruna/`
  - `base/mtg`: the wip/mc-lfe binary, the baseline.
  - `run_slice.sh <tag> <bin> [games] [gi0]`: a Bruna oracle slice (seed 30000000+gi, 8 threads). Output goes to
    `<worktree>/build/scratch/mana_oracle/mcbr-<tag>/{on,off}.err`, and the report to `logs/mana_oracle/mcbr-<tag>/`.
  - `tools/cls.py <on.err> [CLASS] [N] [-s]`: class counts, the APPLY-DROP depth/rescued split, the dropped card,
    and the top signatures of CLASS.
  - `ab/run_ab.sh <tag> <bin> "<envA>" "<envB>" <seed> <deck:games>...` + `ab/cmp.py <dir>`: a paired A/B of one
    binary under two env arms, with MTG_DUMP_WINS and perf instructions (copied from mc-lfe).

## RUNNING JOB (leave it alive)
- `chainA.sh`, PID in `build/scratch/mc-bruna/chainA.pid` (3157996 at handoff). Its stage is in `chainA.status`,
  its log in `chainA.log`.
- The chain waits for build1 to finish (`build1.log`, compiled with all of 5fbe3f386). It then runs build2 through
  buildq, which adds the line-critical land. It copies the binary to `build/scratch/mc-bruna/A/mtg` and runs
  `mtg-test -tc="Bruna*"` (log in `unitA.log`). Last it runs the slice `A` (gi 0-1999), ending with `A.DONE` and
  the status `DONE`.
- Baseline for comparison: `mcbr-base` (same slice on base/mtg).
- Build note: `build1.log` was at 253/255 at handoff.

## Mechanisms (all default ON behind a flag, all unmeasured)
1. **MTG_PAY_BURST_LEAD** (`src/ai/ManaPayment.cpp` ~3507, `burst_lead` before `greedy`; flag reader in
   EngineFlags.h `PayBurstLeadOn`).
   - The defect: a creature payment taps Somberwald Sage first (the restricted-first rank -500). It taps it for
     the FIRST coloured pip in WUBRG order, so the burst is always W.
   - The fix: pay first the colour that the unrestricted sources are shortest of. That counts this payment's
     pips plus the line's later pips (LineOwedAfterOwnPips), with the non-creature supply as the tiebreak. It
     only changes pip order.
   - Target: the "Sage non-W" APPLY-DROPs (35% of the baseline slice's APPLY-DROPs) and part of
     DOMINATED-CREATURE.
   - PLAY CHANGE, Bruna only (the Sage is the only creature-only one-colour burst source in cards.json).
2. **MTG_PAY_LINE_CRITICAL_LAND** -- **DROPPED (successor 3, coordinator decision 2026-11-04): code and unit test
   DELETED, no flag left.** Moved-deck A/B moved1: Unpredictable Cyclone 300 g 4.9100 -> 4.9200 (net +3), inert on its
   own Wish + Sol Ring target, and smoke pinned cyclone d3 gi36 T5->T6 on it. Historical notes follow.
   (Was: ManaPayment.cpp ~2588 `crit_hold`; local EnvOn.)
   - The rule: a land that pays a GENERIC pip in a line, and is the last source of a colour the line still owes,
     taps after every other land. It keeps the CCO hold's band, so it stays ahead of the creatures.
   - Target: the Glittering Wish + Sol Ring drops (Forest pays {1} where Remote Farm should).
   - PLAY CHANGE for EVERY deck in line play, so expect smoke moves outside Bruna.
   - Caveat: it is inactive when `g_line_opens_unseen`. A Wish line may be such a line, so it may not fix the
     Wish case. Check that.
   - NOTE: the needs harm (`MTG_NEEDS_TAP_ORDER`) is default OFF. Do not fix generic-pip choices through `harm`.
3. **MTG_ORACLE_BIG_SPELL** (`src/ai/ManaOracle.cpp` 55 OracleCreatureTap, 1084/1105 BuildSources, 2034 the refine
   memo key).
   - Troyan's {G}{U} bundle becomes oracle supply for a spell that qualifies under BigSpellManaUsable. It is
     booked into the big-spell reserve, and the float-only payment drains that reserve first, as the payer does.
   - It also kills the false "restricted-mana source Troyan" UNMODELED notes for spells that do not qualify.
   - Target: the UNMODELED class. That was 35 in the slice; the r2 run had 245.
   - Oracle-only, but MTG_PAY_REFINE reaches BuildSources from play. Prove identity: run smoke with
     MTG_PAY_BURST_LEAD=0 MTG_PAY_LINE_CRITICAL_LAND=0, and every row must equal GT.
4. **MTG_ORACLE_AURA_HOST_APART** (ManaOracle.cpp ~2219 in Realize's plan_srcs).
   - A plan Aura's host is keyed apart from its twins.
   - Target: the ORACLE-GAP gi319 shape. With two Botanical Sanctums, the engine pays Wild Growth with #12 and
     keeps host #13 untapped.
   - Not covered: Auras cast in a CONTINUATION (Open the Armory -> Wild Growth), which plan.actions does not
     list.

## Baseline (base/mtg = wip/mc-lfe, Bruna gi 0-1999, identity True)

| class | count |
|---|---|
| APPLY-DROP | 752 |
| APPLY-DROP-ORDER | 276 |
| DOMINATED-CREATURE | 272 |
| CAPPED | 199 |
| UNMODELED | 35 |
| ORACLE-GAP | 22 |
| ENUM-GAP | 18 |
| **ISSUES total** | **1,574** |

- Pass r2's rate was 15,201 / 18,000 games. That is about 1,689 per 2,000, so c75ba845d already removed the Sage
  UNMODELED notes.
- APPLY-DROP rollback depth: depth1 646 (177 rescued by MTG_PAY_ROLLBACK), depth2 94, depth3+ 12.
- APPLY-DROP by dropped card: Arcanum Wings 270, Glittering Wish 224, Open the Armory 141.
- APPLY-DROP families (567 at a partial count):

  | family | count |
  |---|---|
  | Sage non-W | 199 |
  | Karoo in the oracle's pay | 159 |
  | Sol Ring in plan | 139 |
  | Wild Growth host-hold | 46 |
  | other | 24 |

- Sol Ring + Arcanum Wings is a land-before-creature shape: Sanctum pays Sol Ring's {1} while Pilgrim could. Fixing
  it would need a creature ahead of a land. That is the standing "no creature ever ranks ahead of any land" law,
  and six variants of it have been refuted. Not attempted. Treat it as re-pay-only for now.
- Wild Growth host-hold: the per-cast RESERVE THE AURA'S OWN HOST (AIEngine.cpp ~10033, plus the rollout twin and
  PrepayAuraHostHold) strands the line's colour, e.g. Birds pays Wild Growth and Wings loses its {U}. A line-aware
  hold that applies only "if the line stays coverable" is a candidate. It needs lockstep across the executor and
  rollout.
- APPLY-DROP-ORDER (the USER's cast order). The oracle's order, by what it puts first:

  | oracle order puts first | flags | share |
  |---|---|---|
  | a dork, then Greaves for haste | 90 | 42% |
  | Greaves | 56 | 26% |
  | a dork, no Greaves | 33 | 15% |
  | Sol Ring | 24 | 11% |
  | other | 11 | 5% |

  These are the numbers to surface to the USER. Do not change the order.
- DOMINATED-CREATURE: about 28% are Karoo-drop plans. The payer taps a creature (or the restricted Sage) while the
  land the Karoo will bounce gets tapped ahead and its mana floats unused. A fix would need the payer to know a
  Karoo drop is pending; PlanTraits has no land info. Not attempted.
- CAPPED: a probe at MTG_MANA_ORACLE_NODES=8000 (vs the default 2000) on gi 291/899/797/1197 resolved EVERY CAPPED
  flag (62/13/26/7 -> 0), found no new issue, and cost almost no extra oracle time (24.5 s -> 24.1 s, 55.8 -> 59.5,
  17.9 -> 19.2, 6.6 -> 8.8). Outputs are in `build/scratch/mc-bruna/cap/`. mana_clean.py inherits the environment,
  so `MTG_MANA_ORACLE_NODES=8000` can be set on a gate run. Raising the engine default affects every deck's gate
  cost: that is a USER question. Next: measure the cost of nodes=8000 on the whole 2,000-game slice.

## Next steps
1. When chainA is DONE, compare `mcbr-A` with `mcbr-base` using `tools/cls.py`. Check `unitA.log`: the two new
   Bruna tests must pass. Attribute each change by flag: rerun the slice with one flag =0 at a time if needed.
2. Identity of the oracle-only changes: run smoke with MTG_PAY_BURST_LEAD=0 MTG_PAY_LINE_CRITICAL_LAND=0. Every row
   must be byte-identical to GT.
3. Play changes: `THREADS=8 bash test/regression.sh --smoke` and list the moved rows; do not accept. Then a held-out
   A/B with `ab/run_ab.sh`: arm A sets both pay flags to 0, arm B uses the defaults. Seed 42000000: Bruna 1,500
   games, plus every deck smoke moves.
4. Every slower game must recover at `--depth 8 --budget-ms 0`, or be shown to be clear clairvoyance under
   MTG_NC_SEARCH=1. Measure instructions with perf stat and MTG_PGO=0 on both arms.
5. Report how many of the depth-1 drops remain re-pay-only after 1+2. The Sol Ring/Pilgrim shape is the expected
   residue.
6. FINAL: run the gate on a fresh block. Copy the primary checkout's `test/mana_clean.json` to a scratch `--state`,
   so the next block is >= 41000. Run `python3 scripts/mana_clean.py run decks/Bruna --games 5000 --threads 8
   --state <copy>`, optionally with MTG_MANA_ORACLE_NODES=8000 (disclose it).

## Successor 2 progress (2026-11-04 07:30 UTC)
- Rebased onto wip/mc-lfe 86a3539b1 (its GT accepted). Commits: burst lead / line-critical / handoff WIP, plus
  `WIP: debug test (temporary)` (BrunaDBG test case: DROP before squash) and `WIP: burst lead counts a choice source
  once (Hall deficit)`.
- Slice A (all 4 fixes, pre-Hall): ISSUES 1,574 -> 1,446. APPLY-DROP 752->632, ORACLE-GAP 22->10, UNMODELED 35->19,
  DOMINATED-CREATURE 272->262, APPLY-DROP-ORDER 276->306 (play diverged), CAPPED 199, ENUM-GAP 18. identity True.
  Sage-non-W drops 278->171. Wish+Sol Ring drops 64->64: fix 2 is INERT on its target because Wish/Armory are
  tutor_to_hand => ParamKeyedDrawClass => g_line_opens_unseen => LineGenericOrderLive() false.
- Unit test "Sage's burst takes the colour..." FAILED on binary A (Wings unpayable after Bruna); diagnosing (dbg.log).
- Hall change: burst_lead's supply counted a choice source (Sanctum, Birds) once per colour; gi411 tied W/U and kept W.
- attr.sh (PID in attr.pid): slices mcbr-A-{noburst,nocrit,nobig,noaura}, 2 threads each, binary A.
- ATTRIBUTION (binary A, gi 0-1999, one flag =0 each; ISSUES total, A = 1,446):
  | flag off | ISSUES | delta vs A (= the fix's effect, sign flipped) | digest |
  |---|---|---|---|
  | BURST_LEAD | 1,587 | fix -141 (APPLY-DROP -114, DOM-CREATURE -27) | moved |
  | LINE_CRITICAL_LAND | 1,482 | fix -36 (APPLY-DROP -22, ORDER -7, DOMC -7) | moved |
  | ORACLE_BIG_SPELL | 1,399 | fix +47 (ORDER +30, DOMC +16: REAL newly visible Troyan-for-Bruna gaps; UNMOD -2, GAP -2) | same digest, units +34 (!) |
  | ORACLE_AURA_HOST_APART | 1,454 | fix -8 (ORACLE-GAP -10) | identical |
- Unit test fixed (board without a bounce land: TapForCostShared does not commit a leftover). Slice A2 (Hall) running,
  then chainS.sh: smoke default -> smoke_def.txt, smoke with both pay flags 0 -> smoke_off.txt.
- Hall change measured (slice A2): digest identical to A, APPLY-DROP 632 -> 635. Reverted (530c6d8c6). The remaining
  "Sage non-W" depth-1 drops (117 lines, 24 games) are mostly a land-before-creature or float-leftover shape (gi411:
  even a {U} Sage leaves Sanctum to pay the generic, the law; gi131 still flags).
- Depth-1 non-rescued APPLY-DROP (all are "re-make the payment before the failing one" by construction): base 469 ->
  A 359. Families in A: Sage non-W 116, other 62, Wish+Sol Ring 58 (fix 2 inert: opens-unseen), Karoo in oracle pay
  48, Sol Ring in plan 35, Wild Growth host-hold 24, dork-ahead-of-land 16. tools/fam.py.
- chainS2.sh (PID chainS2.pid): build6 -> C/mtg, units (unitC*.log, unitC_all.log), smoke default, smoke pay-off.
- Bruna held-out A/B (ab/bruna1, seed 42000000, 1500 games, binary A; arm A = BURST=0 CRIT=0 BIG=0, arm B =
  defaults): net(B-A) = -1 turn (1 faster, 0 slower). units 23,199,300 -> 23,191,598 (-0.03%); instructions:u
  2.6232e12 -> 2.6453e12 (+0.84%, MTG_PGO=0, same binary both arms).

## Successor 2 handoff (2026-11-04 ~09:40 UTC, context cap) -- READ THIS FIRST
State: branch wip/mc-bruna rebased onto wip/mc-lfe 86a3539b1 (its GT accepted; NOT yet on origin -- rebase onto
origin/phase-1-2-deck-analyzer once it lands). Nothing pushed. src tree = the 4 fixes (Hall change reverted).
Binary of this src: build/scratch/mc-bruna/C/mtg (also the worktree's build/Release). A/mtg = same fixes pre-rebase
(identical Bruna digest). Units: all 2564 pass (unitC_all.log); each new Bruna test FAILS with its own flag =0.

Mechanisms (file:line on this branch):
1. MTG_PAY_BURST_LEAD -- src/ai/ManaPayment.cpp ~3496-3600 (`burst_lead` lambda, used in `greedy`); reader
   EngineFlags.h ~622 PayBurstLeadOn. Test test_bruna.cpp ~180 (board Sage/Remote Farm/Forest/Forest/Island).
2. MTG_PAY_LINE_CRITICAL_LAND -- ManaPayment.cpp ~2577-2632 (`crit_hold`, key[2] band ~2674). Test test_bruna.cpp ~205.
3. MTG_ORACLE_BIG_SPELL -- ManaOracle.cpp ~55 (OracleCreatureTap), ~1084/1105 (BuildSources), ~2034 (refine memo
   key); reader EngineFlags.h ~588.
4. MTG_ORACLE_AURA_HOST_APART -- ManaOracle.cpp ~2219 (Realize plan_srcs); reader EngineFlags.h ~596.

Results:
- Slice (Bruna gi 0-1999, 30000000+gi): base ISSUES 1,574 -> all fixes 1,446. Per-flag (flag=0 vs all on): burst
  -141, crit -36, big +47 (newly visible REAL Troyan-for-Bruna gaps: ORDER +30, DOMC +16; UNMOD -2, GAP -2),
  aura -8 (ORACLE-GAP 20 -> 10). Table above.
- Smoke, both pay flags =0 (smoke_off.txt): 169/169 PASS -> fixes 3 and 4 are play-identical in the suite.
  (BIG did shift Bruna search units by +34 on the 2000-game slice with an identical digest: MTG_PAY_REFINE reads it.)
- Smoke, defaults (smoke_def.txt, binary A2 = C + the since-reverted Hall change, which only touches Bruna's Sage
  path and left the Bruna slice digest identical): 10 FAIL rows, all from fixes 1-2:
  cyclone d0 (digest only, gi916), cyclone d3 5.00->5.02 (gi36 T5->T6), fivecolour d3 (digest, gi29), dragons d5
  (digest, gi88/244), melira d0 (digest), pirates d0 (digest), bruna d0 (digests), bruna d5 4.7133->4.7067 (gi99
  T6->T5), edf d0 (digest), galadriel d0 (digest). GT NOT accepted.
- Slower game cyclone d3 s1001 gi36 (seed 1037, gi 36): caused by fix 2 (CRIT=0 -> T5). At --depth 8 --budget-ms 0
  the fix-2 binary wins T4 -> RECOVERS.
- Bruna held-out A/B (ab/bruna1, seed 42000000, 1500 g): net -1 (1 faster, 0 slower); units -0.03%; instr +0.84%.
- Fix 2 is INERT on its stated target (Wish + Sol Ring drops 64 -> 64): Glittering Wish / Open the Armory are
  tutor_to_hand -> ParamKeyedDrawClass -> g_line_opens_unseen -> LineGenericOrderLive() false. Reaching them needs
  a "certain prefix" line hold (casts up to the first opener) bound at ~20 executor+rollout sites: USER-sized.
- Hall-deficit refinement of burst_lead (count a choice source once): slice digest identical, APPLY-DROP +3 ->
  refuted, reverted (530c6d8c6).
- Depth-1 non-rescued APPLY-DROP (each re-makes the payment BEFORE the failing one, i.e. re-pay-only under
  LINE_REPAY): base 469 -> 359 after the fixes (families: Sage non-W 116 [24 games], other 62, Wish+Sol Ring 58,
  Karoo in oracle pay 48, Sol Ring in plan 35, Wild Growth host-hold 24, dork-ahead-of-land 16).

RUNNING: chainAB.sh (PID in build/scratch/mc-bruna/chainAB.pid) -> ab/run_ab.sh moved1, binary C/mtg, seed 42000000,
arm A = BURST=0 CRIT=0 BIG=0, arm B = defaults: Cyclone 300, Melira 400, EDF 400, FiveColour 500, Pirates 1000,
Galadriel 1000, Dragons 1000 (4 threads/arm). Done marker ab/moved1/DONE; result ab/moved1/cmp.txt (net per deck,
slower gi list, instructions per arm).

Remaining steps, in order:
1. Read ab/moved1/cmp.txt. Every deck net <= 0, else drop fix 2 (commit first; remove the crit_hold block and its
   test by editing). Every slower game: rerun at --depth 8 --budget-ms 0 (MTG_DUMP_WINS=1, --ignore-play-profile
   only for smoke rows) on the fix binary; clear clairvoyance only via MTG_NC_SEARCH=1. Instructions delta per deck
   (MTG_PGO=0 already set in run_ab.sh); justify anything > ~1%.
2. Scoped regression on the moved decks: `THREADS=8 bash test/regression.sh --deck=cyclone,fivecolour,dragons,
   melira,pirates,edf,galadriel,bruna` (check the flag spelling in regression.sh); list moved rows, do NOT accept.
3. Squash into reviewable commits: (a) pay burst lead + its test, (b) pay line-critical land + its test (if kept),
   (c) oracle Troyan big-spell + aura host apart, (d) this doc. Drop the WIP debug-test / Hall / revert commits.
   The src TREE must not change in the squash (verify `git rev-parse HEAD:src` before/after).
4. Gate: needs build/PGO fresh for HEAD:src (`PGO_THREADS=8 bash build/scratch/buildq.sh pgo` from the worktree, after
   the squash). cp /workspaces/MagicDeckTester/test/mana_clean.json build/scratch/mc-bruna/mana_clean_state.json, then
   `MTG_RSS_CAP_GB=43 python3 scripts/mana_clean.py run decks/Bruna --games 5000 --threads 8 --state
   build/scratch/mc-bruna/mana_clean_state.json` (next block game_index >= 41000). Then rerun a sample of the CAPPED
   games with `python3 scripts/mana_oracle.py decks/Bruna --nodes 8000 --seed <s> --game-index <gi> --games 1`
   and report how many CAPPED clear.
5. Do NOT push. Do not change Bruna's cast order; do not build the Pilgrim-before-land exception or LINE_REPAY.

Landable now: fixes 3 (BIG_SPELL) and 4 (AURA_HOST_APART) as oracle changes (smoke identical); fix 1 (BURST_LEAD) on
Bruna's evidence (slice -141, A/B net -1, +0.84% instr) pending the moved-deck A/B (it only reaches Bruna's Sage).
NOT yet landable: fix 2 (LINE_CRITICAL_LAND): all-deck play change, inert on its target, small Bruna gain (-36),
moved-deck A/B running.
USER questions: (a) raise MTG_MANA_ORACLE_NODES default (8000 cleared every CAPPED on 4 probe games at ~no cost)?
(b) BIG_SPELL surfaces ~46 new real Bruna flags (Troyan should pay Bruna instead of Birds/Pilgrim) -- fix in the payer?
(c) APPLY-DROP-ORDER shares (USER's cast order) above; (d) LINE_REPAY / Pilgrim-before-land remain parked.

## Lesson
`git checkout <file>` to revert one hunk wiped every uncommitted edit in that file (burst lead had to be re-applied
from memory). Commit WIP before any revert, and revert hunks by editing.

## Successor 3 (2026-11-04 ~10:45 UTC) -- fixes 1, 3, 4 made landable; fix 2 DROPPED
- Coordinator decision: moved-deck A/B moved1 (seed 42000000, A = fixes off, B = all on) -- Dragons/EDF/FiveColour/
  Galadriel/Pirates/Melira net 0, Unpredictable Cyclone 300 g 4.9100 -> 4.9200 (net +3). Fix 2 (LINE_CRITICAL_LAND)
  caused it and was inert on its own target, so its code and unit test are DELETED (no flag left).
- Pre-squash HEAD kept as `backup/mc-bruna-pre-squash-3` (77d8c7e9f). Squashed to: BIG_SPELL, AURA_HOST_APART,
  BURST_LEAD (+ test), docs; rebased onto origin/phase-1-2-deck-analyzer 19d9d85e8 with no conflicts.
- Release build of the rebased tree: mtg-test 2564/2564 pass; the BURST_LEAD test FAILS with MTG_PAY_BURST_LEAD=0.
- Smoke (THREADS=8 MTG_PGO=0, build/scratch/mc-bruna/smoke_s3.txt): 167 pass, 2 FAIL, both Bruna: bruna d0
  digest-only (5.9380), bruna d5 4.7133 -> 4.7067; searched slower=0 faster=1. Every other deck byte-identical.
  GT NOT accepted.
- Scoped regression (`--deck=bruna,bruna2hg`, reg_s3.txt): 2 PASS (the s30221 rows), 4 FAIL, all bruna and all
  digest-only at an unchanged average (d0 s2002 5.8900, d3 s2002 4.7400, d5 s2002 4.8600, d5 s3003 4.7000).
  Searched slower=0 faster=0, play-changed=4 at the same score (gi30 x2, gi136, gi58); d0 slower=0. bruna2hg has a
  smoke row only (PASS, identical). Viewer protocol: 0 play-drift/board-diverged/enum-gap. GT NOT accepted.
- No slower game in smoke or regression, so there was nothing to recover at --depth 8 --budget-ms 0.
- PGO built for HEAD:src a232e4274a61 (PGO_THREADS=8).
- Gate `bruna-mc-s3` (state build/scratch/mc-bruna/mana_clean_state3.json; report logs/mana_clean/bruna-mc-s3/report.md):
  5,000 games gi 41000-45999, seed 30041000, identity byte-identical, verdict issues, 3,975 issues.

  | class (per game)     | pass1 (23k, invalid) | pass1-r2 (18k) | s3 (5k) | s3 vs r2 |
  |----------------------|------:|------:|------:|------:|
  | APPLY-DROP           | 0.4098 | 0.4341 | 0.3644 | -16% |
  | APPLY-DROP-ORDER     | 0.1244 | 0.1140 | 0.1244 | +9% |
  | DOMINATED-CREATURE   | 0.1481 | 0.1399 | 0.1542 | +10% |
  | ORACLE-GAP           | 0.0382 | 0.0371 | 0.0176 | -53% |
  | UNMODELED            | 0.0309 | 0.0136 | 0.0344 | (one game) |
  | CAPPED               | 0.1012 | 0.0930 | 0.0884 | -5% |
  | ENUM-GAP             | 0.0133 | 0.0128 | 0.0116 | -9% |
  | **issues**           | 0.8658 | 0.8445 | 0.7950 | -5.9% |

  UNMODELED: 110 of the 172 flags are ONE game (gi43577, T4 "too many actions to try every order"); without it
  0.0124/g. No "restricted-mana source" Troyan note remains. ORDER/DOMC up is consistent with BIG_SPELL exposing
  real Troyan-for-Bruna payer gaps (held for the USER). Confounds: different game block, and the base moved
  (r2 src 644270cdc4ce; s3 includes wip/mc-lfe + origin's equip-route audit).
