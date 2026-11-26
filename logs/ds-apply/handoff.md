# Dragonstorm APPLY-DROP handoff 1 (2026-11-26 ~14:15 UTC)

Worktree `.claude/worktrees/ds-apply`, branch `wip/ds-apply` (from origin/phase-1-2-deck-analyzer 9e566000b). Push ONLY
wip/ds-apply. Never push / accept GT on phase-1-2-deck-analyzer (the parent lands through the push queue). Never touch
references/. Treasure Hunt APPLY-DROP is another agent's (wip/th-apply): do not touch it. Scratch:
`/workspaces/MagicDeckTester/build/scratch/ds-apply/` ($S below).

## Done (commits)
- 893647990: two levers, DEFAULT ON, `=0` byte-identical, heurarm slots LEFTOVER_SWAP_WIDE / ONDEMAND_VISIBLE_LUMP_EXACT,
  readers in src/ai/EngineFlags.h; code in src/ai/ManaPayment.cpp; unit test test/unit/test_dragonstorm_line_keep.cpp
  (added to CMakeLists; NOT YET BUILT/RUN -- see Remaining 1); design doc docs/design/dragonstorm-apply-drop-2026-11-26.md.
  * MTG_ONDEMAND_VISIBLE_LUMP_EXACT: the visible-closer lump rule (MTG_ONDEMAND_VISIBLE_LUMP, Dragonstorm provider only)
    cracks a Bloom early only if the ordinary payment leaves the later bill unpayable and the early board pays it
    (new helper `LumpAwareBillPayable`: every crack of the lumps left x complete backtracker, + ritual float). Repro
    s30152662 T5 {Song, Irencrag, Karrthus}.
  * MTG_LEFTOVER_SWAP_WIDE: SwapLeftoverForLine may move any leftover unit (not only produced) and its strand test
    credits the paying ritual's float (LineKeepRockFor(state).ritual). Repro s30087280 T4 {Desperate, Scourge, Irencrag,
    Kolaghan}.
- Mechanism census (gate claude-queue-20261125a, 728 flags = 146 (seed,turn) events in 144 games): every event has Lotus
  Bloom or Unclaimed Territory on board; dropped casts are Karrthus {4}{B}{R}{G} (77 events) / Kolaghan {4}{B}{R} (48).
  112 declared, 34 need-later (past an Apex opener). The crit line-keep rung never reaches them: LineKeepCritGate counts
  only lands/rocks/dorks (a Bloom, a Territory, a float unit are invisible) and the complete backtracker does not model
  an on-demand Bloom crack (it only knows pay-sac amount==1, i.e. Treasure).
- Event mini-gate (only the event games, oracle on the event turn only; `$S/miniT<t>.json`, `logs/ds-apply/run_turns.sh`,
  compare `logs/ds-apply/cmp.py tn_new`): on 111 games common to the base full-oracle run: base 106 events / 503 flags ->
  new 75 events / 419 flags; fixed Karrthus-D 26, Kolaghan-D 5; NO new events. ($S/mini_base is the base binary, all
  turns, 123 of 144 games -- killed when superseded; $S/tn_new is the new binary, T6-T8 groups still running at handoff.)

## Running jobs (all mine)
- GATE A/B (Dragonstorm slice of the gate, 180,218 games, arms off / on / no_leftover_swap_wide / no_ondemand_visible_lump_exact
  (LOO on the first 18k games)), under the box lock: pid 293649 (flock), binary $S/bin_new/mtg (= 893647990),
  manifest $S/gate.json (logs/ds-apply/mk_gate.py), out $S/gate_out/{batch.out,flags.err}, DONE marker
  $S/gate_out/DONE. Heartbeat was 32/32. ~1 h from 14:00.
  Report: adapt logs/worldfire-ad/gate_report.py (job= prefix is `<arm>__dragonstorm__g...`, deck name dragonstorm,
  gi from seed-30000000; arms off,on,no_leftover_swap_wide,no_ondemand_visible_lump_exact; LOO slice = gi < 105000).
- PLAY A/B chained after the gate: pid 296958 ($S/chain_ab.sh) waits on the gate pid, then
  `flock claude_box.lock ... logs/ds-apply/run_ab.sh $S/bin_new/mtg $S/ab.json $S/ab_out`. Manifest from
  `logs/ds-apply/mk_ab.py --n 400 --hg 200 --wf 3000` (seed base 5130000, seed = base + off, game_index = off; all 63
  suite decks std 400/arm + 2HG 200/arm; Dragonstorm std + 2HG 3000/arm; 86,400 games, ~3 h). DONE: $S/ab_out/DONE.
  Check `heartbeat` in $S/ab_out/wins.err in its first 10 min. Report: `python3 logs/ds-apply/ab_report.py $S/ab_out
  --list-slower`. If the gate shows a regression, kill 296958 (it is my own run) before it starts.
- Unit-test build (lane, -j2): `./build.sh release -- --target mtg-test`, log $S/build2.log, DONE $S/build2.DONE.
  Then run `build/Release/mtg-test -tc="Dragonstorm line keep*"` and the full suite (lane). The three tests are
  UNVERIFIED (expected: repro tests pass on / fail off; the no-strand control equal). Fix the test (not the levers)
  if it is the fixture that is wrong -- e.g. the s30087280 board's float / Bazaar counters.

## Remaining, in order
1. Unit tests (above); full unit suite on the new binary.
2. Gate report (off vs on, LOO) -> design doc Measurements.
3. Play A/B report; every slower game replayed at `--depth 8 --budget-ms 0 --ignore-play-profile` (abandon_units 30M;
   2HG adds starting_life 30, opponent_heads 2), both arms (flags via env MTG_LEFTOVER_SWAP_WIDE=0
   MTG_ONDEMAND_VISIBLE_LUMP_EXACT=0 for off), lane <= 2 slots; a non-recovering one -> MTG_NC_SEARCH=1 at play
   settings. Units and CPU per deck (perf instructions:u for CPU if the wall cpu is noisy).
4. Next mechanisms (the 75 events left, new binary; list with
   `grep "APPLY-DROP seed" $S/tn_new/flags.err | grep -v "not declared" | awk '!seen[$3 $7]++'`):
   a. TWO UNCLAIMED TERRITORIES (5 of 15 Karrthus-D): Karrthus's {B}{G} need both Territories' creature colours; an
      earlier ritual's generic takes a Territory's {C} (s30096797 T5, s30112157 T6, s30142602 T6, s30155973 T5,
      s30089733 T5). The crit rung cannot judge it: its rest is ONE bill with one creature flag, and a rest mixing a
      noncreature ritual with Karrthus reads the Territory as {C}. Idea: sequential witness (noncreature part, then the
      creature part) or a split judge.
   b. RITUAL FLOAT NOT CREDITED BY THE LINE-FLOAT RUNG (s30096957 T4 / s30103583 T5 {Ruby, Song, Karrthus}, Mountain
      x2 + two Blooms): Song's {1} drains the floating {B}; Mountain30 is not "spare" because it makes {R}, which
      Karrthus owes, though Song's {R}x5 covers it. In LineFloatTapFirstPlan, net `later[]` of the paying ritual's
      float (LineKeepRockFor(state).ritual) -> the Mountain is spare -> the {B} is held.
   c. Exact swap test: SwapLeftoverForLine's short test uses AvailableManaPool, which counts a Territory as wild for
      every colour (s30129376 T4: B short is "covered" by the Territory that must also make G). Under a lever, judge
      short / not-short with LumpAwareBillPayable (add a for_creature parameter; rest_creature as in
      LineReducerKeepHold) behind a cheap prefilter (an owed colour x went to generic AND some leftover y floats).
   d. Apex-line Kolaghan-D (keep-floating {B} through Apex's generic): the opener line keeps the WUBRG drain; several
      are overkill-only differences (opponent life -113 vs -114) and do not matter for play.
   e. Need-later (Karrthus past Apex; ~20 events): Apex's / Song's generic takes the Territory's {C} or a Bloom; unseen
      demand -- lowest priority.
   Each new mechanism: its own lever (default ON if sound), unit test, rerun `run_turns.sh` on the event games, then
   the gate + play A/B again (one pooled batch each, under the box lock).
5. Commit + push wip/ds-apply; final report to the parent.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm storm/counter resources).
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- Adoption bar: judge the average at play settings; adopt if many decks gain, mitigate the few that get worse
  afterwards; ignore seed noise and clairvoyant losses; d0 barely counts. Play setting > other searched shapes >> d0.
- Every slower game must recover at depth 8 budget 0 (`depth 8, budget_ms 0, ignore_play_profile true, abandon_units
  30000000`; 2HG adds starting_life 30, opponent_heads 2) or be shown clairvoyant with MTG_NC_SEARCH=1 at play settings.
- Box: ONE big engine process at a time, every big batch under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock
  env MTG_RSS_CAP_GB=40 MTG_PGO=0 ...`; small jobs via `bash scripts/box_lane.sh --slots N -- <cmd>`, N <= 2. Builds:
  `flock build/scratch/claude_build.lock ./build.sh` (coordinator 2026-11-26: while its landing tiers ran, builds as
  `MTG_BUILD_JOBS=2 bash scripts/box_lane.sh --slots 2 -- flock ... ./build.sh`; build/scratch/land-1126b/TIERS.DONE now
  exists, so that restriction has lifted, but a full-core build is a big process -> box lock). NEVER `timeout`; wait on
  a PID (kill -0) or a DONE marker, never pgrep -f / pkill -f. Wait ~200 s after a src edit before building (9p lag).
- No greedy pick inside the search window; a budget may re-price but never make a line unfindable; no unbudgeted-only
  changes; "do nothing more" stays at every root. Rebase, never merge. Commit messages end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Past ~250K context: hand off (this file).

## Traps
- `./build.sh -- --target X` fails ("unknown mode '--'"): use `./build.sh release -- --target mtg`. The mtg target alone
  is 15 steps (fast); mtg-test is 367 objects.
- A binary copied under $S/<x>/ needs `src` / `decks` symlinks beside it (done for bin_base / bin_new).
- logs/ is gitignored: commit logs/ds-apply with `git add -f`.
