# Dragonstorm APPLY-DROP round 4 (ds-apply4) -- handoff 1 (2026-11-27)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/ds-apply4`, branch `wip/ds-apply4` (off land-1129 42fe79749).
Push ONLY `wip/ds-apply4` (`git push -q origin wip/ds-apply4`); never the shared branch (the parent lands it). Never
touch references/. Scratch `S=/workspaces/MagicDeckTester/build/scratch/ds-apply4/`. Tested binary: `$S/bin_l4/`
(mtg + mtg-test = commit 3bd740edd's src; src/decks symlinks beside it). Base for comparison: `$S/bin_dbg/` = 42fe79749
+ the MTG_DS2_PAYDBG snippet (logs/ds-apply3/dbg.snippet; play identical, prints only).

## Done
- **3bd740edd** (code, landable): two levers, both DEFAULT ON, `=0` byte-identical, heurarm slots at the enum tail +
  Name table (LINE_SEQ_REST, UNSEEN_SCARCE_RITUAL_NET), readers in src/ai/EngineFlags.h.
  * `MTG_LINE_SEQ_REST`: LineSeq/LineSeqScope/NoteLineSeqCast (ManaPayment.h/.cpp; bound in ApplyPlanDirect and
    AIEngine::TakeTurn beside LineNoncreatureLeftScope, stepped at both cast sites beside NoteLineNoncreatureCast;
    every LineUnpaidCostScope binds none -> continuations / walks / record replays never read it, lockstep).
    LineSeqPayingIndex + LineSeqPayOn + LineSeqRestPays (the ordered WITNESS walk) after LumpAwareBillPayableOpener.
    Readers: keep_owed's judge (summed OR walk) and KeepOwedFloatPay (top of TapForCostShared; owed-float keep).
  * `MTG_UNSEEN_SCARCE_RITUAL_NET`: SpendFloatingTowardCost's unseen-keep-scarce branch (SpellEffects.h) nets the later
    pips by the paying ritual's output before the scarce colours' spare.
  * Unit tests in test/unit/test_dragonstorm_line_keep.cpp: s30152803 T4 (LINE_SEQ_REST on pays / off fails),
    s30093026 T4 (RITUAL_NET on keeps {B} / off spends). Line-keep 11/11; full suite 4333/4333 (logs/ds-apply4/units.txt).
- **b8191cb3b** (logs + doc): mechanism census and event repros. Round 4 section appended to
  docs/design/dragonstorm-apply-drop-2026-11-26.md (placeholders GATE4 / AB4 / VERDICT4 still to fill); round 3's
  KOLD_EVENTS placeholder filled.
- Event repros (logs/ds-apply4/events_l4.txt): Kolaghan-declared 123 -> 82, all AD 139 -> 98 on the 18 round-3
  leftovers; 8 events fixed (30093026, 30115282, 30127991, 30130531, 30131013, 30132752, 30134453, 30152803);
  30133545 15 -> 18 and 30145461 30 -> 36 (same failing decision both arms, more plans examined); win turns unchanged;
  `=0` logs identical to base on all 18 (logcmp_base_off.txt).
- Mechanism finding: the brief's two-Apex hypothesis was WRONG; s30093026's loss is a continuation Song's float-only
  drain under MTG_UNSEEN_KEEP_SCARCE (keep mask B|R, red's spare not netted by Song's own five). See the doc section.

## Running (mine) -- the chain
- `$S/chain4.sh` pid 1476379 (detached, setsid): waits for `/workspaces/MagicDeckTester/build/scratch/land-1129/LANDED`
  (the parent holds the push lease; NO big run before that file exists), then under claude_box.lock:
  1. gate: `logs/ds-apply4/run_gate.sh $S/bin_l4/mtg $S/gate.json $S/gate_out` (906 jobs, 180,218 games: off / on
     72k each + leave-one-out arms no_line_seq_rest / no_unseen_scarce_ritual_net on gi < 105000, 18k each;
     MTG_MANA_ORACLE=2). ~2.5 h. Markers: `$S/gate_out/DONE` (rc=), `$S/chain.gate_end`.
  2. held-out A/B: `logs/ds-apply4/run_ab.sh $S/bin_l4/mtg $S/ab.json $S/ab_out` (mk_ab.py --n 400 --hg 200 --wf 3000
     --base 5410000; 128 shapes, 87,600 games). ~1 h. Markers `$S/ab_out/DONE`, `$S/chain.ab_end`, `$S/chain.DONE`.
  `$S/chain.start` appears when the gate starts. Check `heartbeat` lines in `$S/gate_out/flags.err` / `$S/ab_out/wins.err`.
- Waiter: `while kill -0 1476379 ...; [ -f $S/chain.DONE ]` (run_in_background), never pgrep -f.

## Remaining, in order
1. When `$S/gate_out/DONE`: `python3 logs/ds-apply4/gate_report.py $S/gate_out` (per arm; note gate_report keys the
   dropped card on the FIRST cast in "engine dropped [...]"; round 3 classified Kolaghan on the last -- use
   `logs/ds-apply4/minirep.py $S/gate_out` too). Also units/cpu per arm from batch.out. Fill GATE4.
2. When `$S/chain.DONE`: `python3 logs/ds-apply4/ab_report.py $S/ab_out --list-slower` (per-deck net, slower/faster,
   units, cpu). Fill AB4.
3. Every slower game: d8 b0 replay both arms (`--depth 8 --budget-ms 0 --ignore-play-profile`, abandon_units 30000000;
   2HG adds starting_life 30 / opponent_heads 2; off arm env MTG_LINE_SEQ_REST=0 MTG_UNSEEN_SCARCE_RITUAL_NET=0) via
   `bash scripts/box_lane.sh --slots 2 -- ...`. A non-recovering one -> MTG_NC_SEARCH=1 at play settings.
4. Fill VERDICT4; `git add -f logs/ds-apply4` + docs; commit; `python3 scripts/check_references_safe.py`;
   `git push -q origin wip/ds-apply4`.
5. Final report to the parent: per shape the mechanism + flags/events fixed, gate table off/on (+LOO attribution),
   per-deck A/B, d8 b0 verdicts, hashes, what remains.

## What remains in the cluster (not fixed)
- s30133545 (two-Apex): oracle rollback decision #5 keep-floating {B} (engine's first 4 decisions kept) -- probably a
  continuation payment (no seq there by design: continuations bind their own hold) spending the {B}; not traced on
  the new binary (would need a debug build: logs/ds-apply3/dbg.snippet after the SwapLeftoverForLine call).
- s30145461 (Territory, 36 flags, decision #11), s30092109, s30102129 (three Apex), s30135813, s30140711, s30141736,
  s30142750, s30149079, s30157929: unchanged.
- KeepOwedFloatPay / the walk do not run in breakpoint continuations (deliberate: record replays carry only
  rec_line_hold, so binding a sequence there would break executor/rollout lockstep). Extending it needs the sequence
  recorded with the record (Action::rec_line_hold).

## USER rulings (verbatim from the brief)
- Dragonstorm: "spend the counters if they are required for the line".
- Adoption bar: judge the average at play settings. Adopt if many decks gain, mitigate the few that get worse
  afterwards, ignore seed noise and clairvoyant losses; d0 barely counts.
- "cut CPU"; suite headroom never justifies a fix's CPU cost.
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything must stay findable at unlimited budget.
- NO GREEDY PICK INSIDE THE SEARCH WINDOW (CLAUDE.md): the engine is heuristic-free; heuristics only in a
  DecisionProvider and only to restrict/order options. A payer payment choice that provably avoids stranding a named
  line is the round-3 shape and is fine.
- USER-owned decisions are surfaced in your report, never settled by you.
- Do NOT accept GT (the parent accepts GT under the combined landing binary). Do not run regression/smoke tiers.
- Parent (mid-task): until `/workspaces/MagicDeckTester/build/scratch/land-1129/LANDED` exists, do NOT start a gate,
  A/B or other big run under claude_box.lock (Release builds and lane repros <= 2 slots are fine). The chain honours it.
- Box: ONE big run at a time under `flock build/scratch/claude_box.lock` with MTG_RSS_CAP_GB=40 MTG_PGO=0; small jobs
  via `scripts/box_lane.sh --slots N` (N <= 2; run it with `bash`, the script is not executable); builds
  `flock claude_box.lock flock build.lock ./build.sh` (test-only: `./build.sh release -- --target mtg-test`); wait ~200 s
  after src edits; never `timeout`; waiters on PID / DONE files, never pgrep -f; context hand-off past ~250K.

## Measurements so far
- CPU: not measured yet (the gate's units/cpu per arm and the A/B are the measurement).
