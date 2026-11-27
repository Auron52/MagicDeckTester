# MTG_ATTACK_LORD_CAST "why it fails" + Galadhrim Brigade 814 -- handoff (2026-11-27 ~10:30 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast`, branch `wip/elves-lordcast`, REBASED onto
origin/phase-1-2-deck-analyzer f2e307fe3 (HeuristicArm.h conflict: both sides added slots, kept both, HEAD's first).
Scratch `SC=/workspaces/MagicDeckTester/build/scratch/elves-lordcast`. Push wip/* only, never the shared branch.
Lever default stays OFF (USER's call). Investigation: deliverable is mechanism + proposed fix with evidence.

## USER rulings (verbatim)
- 2026-11-27, on MTG_ATTACK_LORD_CAST: "I would leave it off for now, but we probably should figure out why it doesn't work."
- 2026-11-27, on Brigade 814: "Why would you play 2 Galadhrim Brigades when you could play one and Squad it? I guess the
  only case I can think of would be if we played it for three and gained mana as a result, but it seems to me like the
  other option would almost always win out."
- (earlier, kept from the round-1 handoff) "I don't know what this is, but I suppose we could try it."

## Done
- Rebase (hashes rewritten; the old head ebffd51bf = the round-2 d8 b0 commit, now the commit just below the WIP).
- WIP commit (this file + the amendment code, see "Commits" in git log): amendments A/B/C in src/ai/TurnSolver.cpp,
  all under MTG_ATTACK_LORD_CAST (off arm byte-identical by construction). COMPILED (build_m.log exit 0), NOT YET RUN on any game.
- Helpers (logs/lordcast/): gdiff.sh (one game per arm with logs, first divergence; BASE/BIN/PLAYARGS env), turnview.py
  (hand/board/actions of a turn), mk_cls.py (per-game manifest of a cell's slower games under arbitrary arms),
  feat.py + featall.sh (divergence features: legendary dup, bodies lost in the diverging main), run_ab3.sh (round-3
  A/B runner), cpstep.py / cpauto.py / cp814_auto.sh (Q2 claude-play replay + hand-off).

## Q1 MECHANISMS (d0; round-2 binary $SC/mtg.ab2, seeds 7460000+gi, game_index gi)
Classification batch `$SC/cls/c1.json` -> `c1.err` (arms off / onA / onA+MTG_ATTACK_REPROJECT_INTENT=0 / off+INTENT=0),
features `$SC/cls/feat.txt`.
- **A. Frozen attack intent (CONFIRMED, Boars 206 of 237 slower).** The re-projection reads who WANTS to attack from the
  PRE-plan board (point 10, MTG_ATTACK_REPROJECT_INTENT); a 0-power mana dork holds there. The kept lord
  (Raggadragga: +2/+2 to creatures with a mana ability) makes Ignoble Hierarch 2/3, but it stays "held": gi44 T4
  {Raggadragga} reads 16 (Ilharg 6+1 exalted+2 Forerunners, Forerunners 7) vs 19 dealt; off reads 20 (the plain
  pre-plan read) and wins T4; on falls to {Herald of Ilharg}, a phantom 20 read shared with off, deals 16 -> T5.
  INTENT=0 recovers 206/237 (gi44, 381, 431, 606, 667, 688, 873, 981, ...).
- **C. Legendary lord kept twice (strong, Boars 23 of the other 31; Goblins 3 flagged).** Raggadragga already on the
  battlefield; the walk keeps the cast copy without the legend rule -> anthem doubled. gi8941 T4: {Raggadragga} 17 read
  (lethal at 17), 11 dealt; off cast Decimator, won T4. Same shape: gi1319, 1738, 2387, 5458, 11723, 12583, 14943, ...
- **B. Implicit sac-fodder payment not charged (strong, Goblins 136 of 150).** Live §2b Skirk Prospector puts every
  Goblin's mana in `supply` (gi314 supply=8 = 2 Mountains + 6 Goblins); AttackTapLoss charges only explicit SacForMana
  candidates (cand_sac). gi314 T3: {Chieftain, Twinshot Sniper} reads 24 (lever +6 from Chieftain on six Goblins), the
  payer eats Muxus + Goblin King + 3 Lackeys, deals 7 (off cast Chieftain only, 18, won T3); on won T6. Off has the same
  over-read (18) but it stays below lethal; the lever lifts it over, and d0 then prefers the eval-highest "winner", i.e.
  the plan that spends (eats) most. In feat.txt every Goblins row has lostB > lostA.
- Other: Boars 8 (Herald-of-Ilharg phantom x3 13069/18110/31793, Somberwald Sage x2 21799/23633, Herald's Horn 9357,
  empty cast 9936/47693); Goblins 4 other + 7 unparsed (29815, 31344, ...). Not examined.
- Elves d0 (183 slower) NOT classified yet (round-1 note: payment colour-blind/executor-blind, mechanism 2 there).
- Searched: Elves gi1552 (d8 b0 unrecovered): off casts Taunting Elf T1 and kills T4 for 13 (Visionary+Archdruid+Elf under
  2 Brigades); onA holds the Elf, T4 casts Elf + Brigade squad 2 (Archdruid tapped to pay), deals 5, wins T5. Mechanism
  NOT found: MTG_SOLVE_PROJ_TRACE is Solve-only and prints nothing in searched play; need an EnumeratePlans-side trace of
  the T1 lookahead's T4 read. Elves 2HG gi480 and round-1 gi663 also open.

## Proposed fix (in the WIP commit, all inside the lever)
- A: ProjectPlanWalk -- with board_lord, a creature the intent HELD whose lord bonus the kept lords raise is re-asked
  (wants=2) via a local AttackIntent copy + AttackIntentScope; other verdicts stay pre-plan.
- B: AttackTapLoss -- with board_lord and a lord in the subset and a LiveSacPayOutlet: need = max(need,
  cost - (supply - y1 - fodder_units)) (dorks before fodder), fodder_eat = ceil((cost - (supply - fodder_units))/per)
  bodies removed from the walk board in SacPayFodderRank order; memo2 skipped when fodder_eat > 0.
- C: ProjectPlanWalk -- with board_lord, a legendary lord whose name is already on our battlefield is not kept.
- Amended arm = MTG_ATTACK_LORD_CAST=1 + MTG_LORDCAST_HAND=1 (Ilharg hand fix; fold into the lever if adopted).
  MTG_LORDCAST_VERIFY is inert (round 2): delete it.

## Q2 -- Brigade 814: CONFIRMED
Seed 9850815 (scaling2 round 2 = base 9850001 + 814), 2HG, Elves, binary build/scratch/elves-scal2/ab/mtg3.
The arms already split at the MULLIGAN BOTTOM (full bottoms Genesis Wave, holdall a Forest: bottoming plays candidates
out under each arm's prune). So the full arm's exact T4 board was rebuilt through claude-play (CSV
`0,1,1,0,0,0,0,0,0,7,0,0`; T3 plan 7 = Marwyn BEFORE Archdruid) and handed to the search with --choices-then-auto
(`logs/lordcast/cp814_auto.sh`, out build/scratch/elves-scal2/cp814/p{0,1}_d{5b20,8b0}/):
prune OFF -> T4 at d5 b20 and d8 b0; prune ON -> T5 at both. Board: Forest x3, Warcaller 2/2, Priest (yield 4),
Archdruid (yield 4), Marwyn 3/3 (1 counter, yield 3); hand Brigade, Brigade, Wirewood Lodge; opponents 26.
- OFF line: Lodge; Brigade squad 3 (9); 4 Elves enter -> Marwyn +4 counters and +4 Brigade pump -> her tap now yields
  ~11; Brigade squad 2 (7) paid from that; Wirewood Lodge untaps Marwyn; attack Warcaller 9 + Marwyn 17 = 26 = lethal.
- ON line (no split): one Brigade squad 7 (17 = every source incl. Marwyn + the Lodge burst), Marwyn stays tapped,
  only Warcaller attacks: 11 (26 -> 15), T5. Best single cast that keeps Marwyn attacking is squad 5: 15 + 8 = 23 < 26.
- Mechanism: the USER's guess. Marwyn's mana scales with the Elves the FIRST cast puts in (counters + Brigade anthem,
  3 -> ~11), which no single squad cast can spend because all of its cost is paid before any body enters; the second
  cast is funded by the first's bodies and the Lodge then untaps Marwyn to attack. Trace rows: T4 77 plans, kept only
  single-Brigade rows (squad 1-5 without Lodge, 1-7 with it).

## Running jobs
None. The amendment build FINISHED 2026-11-27 ~10:35 UTC: `$SC/build_m.log` ends `exit 0` (compiles clean, no errors);
binary build/Release/mtg (= build/wt/elves-lordcast/Release/mtg) holds commit 184a7cb71's src. Unit tests are NOT built
(the full build was stopped at 67/404); run `./build.sh` (full) before any unit-test run. Not yet run on any game.

## Remaining, in order
1. When build_m.log says exit 0: run the recovery manifest `$SC/cls/r1.json` (off vs onB over every round-2 d0 slower
   game: Boars 237, Goblins 150, Elves 183) on build/Release/mtg:
   `bash scripts/box_lane.sh --slots 2 --cap-gb 2 -- env MTG_DUMP_WINS=1 build/Release/mtg --batch $SC/cls/r1.json --threads 2 > $SC/cls/r1.out 2> $SC/cls/r1.err`
   Count onB <= off per class (A/B/C/other). (The binary is already built.)
2. Launch the round-3 A/B queued on the heavy lock: `bash logs/lordcast/run_ab3.sh` (run_in_background; it copies
   build/Release/mtg to $SC/mtg.ab3, writes $SC/m3.json (2,366 jobs / 330k games: boars/goblins/elves d0 50k, play 4000,
   2HG 1000, arms off/onB, BASE 7460000), out $SC/ab3.out/err, PID in $SC/run_ab3.pid, DONE marker $SC/DONE3).
   Report: `python3 logs/lordcast/report.py $SC/ab3.err $SC/ab3.out onB off --list`. Check the [play] provider lines and
   the first heartbeat.
3. Searched gi1552 / 2HG gi480 / gi663: find the T1 look-ahead's T4 read (an EnumeratePlans-side trace).
4. Elves d0 slower classification (mechanism 2: colour-blind need).
5. Delete MTG_LORDCAST_VERIFY (inert). Lever default stays OFF; report to the USER.
