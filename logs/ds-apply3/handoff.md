# Dragonstorm APPLY-DROP round 3 (ds-apply3) -- handoff 1 (2026-11-27 ~09:05 UTC)

Worktree `.claude/worktrees/ds-apply3`, branch `wip/ds-apply3` (from wip/ds-apply2 a9b9c02e8). Push ONLY wip/ds-apply3;
never the shared branch (the parent lands it). Never touch references/. Scratch `/workspaces/MagicDeckTester/build/scratch/ds-apply3/`
($S). Tested binary: `$S/bin_l3/` (mtg + mtg-test = commit 2c5615491's src; src/decks symlinks beside it).

## Done
- **2c5615491** (code, landable): `MTG_ONDEMAND_LUMP_KEEP_OWED`, heurarm slot ONDEMAND_LUMP_KEEP_OWED at the enum tail +
  Name table, reader in src/ai/EngineFlags.h, code in src/ai/ManaPayment.cpp (`keep_owed` lambda in TapForCostShared's
  step-3 lump ladder; `OnDemandPayWith`/`OnDemandTrial` gained an optional `keep` pool; `LumpAwareBillPayableOpener`),
  unit test `s30120119 T6 -- Apex taps the Mountain...` in test/unit/test_dragonstorm_line_keep.cpp (on pays, off fails).
- Mechanism census of the 23 Kolaghan-declared events (145 flags) and the fix: see `logs/ds-apply3/round3_draft.md`
  (the design-doc Round 3 section, with placeholders KOLD_EVENTS / GATE / AB / VERDICT still to fill).
- Units: full suite 4315/4315 pass (unit_par on bin_l3; note unit_par reports "shard counts do not add up" because
  every shard ran the whole suite -- 127 x 4315, all 0 failed; a unit_par/--first/--last quirk, not a failure).
- Event repros (`logs/ds-apply3/events_l3.txt`): Kolaghan-declared flags 145 -> 100; fixed s30118865 T7, s30120119 T6,
  s30129637 T5, s30142598 T5, s30155333 T5 (that game wins T4 instead of T5). `=0` game logs identical to the round-2
  binary on all 23 games.
- Gate (`$S/gate_out`, `logs/ds-apply3/gate_report.txt`; 72,000 games per arm, MTG_MANA_ORACLE=2): AD-decl flags
  270 -> 231, declared events 50 -> 46 (the 5 above fixed, 1 new: Karrthus s30106357 T5 in a moved block; 10 of 362
  block digests differ), AD-undecl 323 -> 318, APPLY-DROP-ORDER 66 -> 58, ENUM-GAP 27 -> 25, ORACLE-GAP 0 = 0, turns
  313,583 -> 313,580, units -0.019%, cpu +0.045%. Kolaghan decl 144 -> 99, Karrthus decl 35 -> 41, Karrthus undecl 279 -> 274.

## Running (mine)
- `$S/chain2.sh` pid 1192705 -> the held-out A/B: `logs/ds-apply3/run_ab.sh $S/bin_l3/mtg $S/ab.json $S/ab_out` under the
  box lock (mtg pid 1263006 at hand-off). Manifest `mk_ab.py --n 400 --hg 200 --wf 3000 --base 5310000` (87,600 games,
  128 shapes). DONE markers: `$S/ab_out/DONE` (rc=), `$S/chain.ab_end`, `$S/chain.DONE`. Check `heartbeat` in
  `$S/ab_out/wins.err` (should be 32/32). ~3 h from 09:00 UTC.

## Remaining, in order
1. Wait on `$S/chain.DONE` (run_in_background waiter on the file or `kill -0 1192705`). Report:
   `python3 logs/ds-apply3/ab_report.py $S/ab_out --list-slower` (per-deck net, units, cpu).
2. Every slower game: replay at d8 b0 both arms (`--depth 8 --budget-ms 0 --ignore-play-profile`, abandon_units 30000000;
   2HG adds starting_life 30, opponent_heads 2; off arm via env MTG_ONDEMAND_LUMP_KEEP_OWED=0), lane <= 2 slots
   (`scripts/box_lane.sh --slots 2 -- ...`). A non-recovering one -> MTG_NC_SEARCH=1 at play settings (clairvoyance test).
3. Fill the placeholders in `logs/ds-apply3/round3_draft.md` and append it to
   `docs/design/dragonstorm-apply-drop-2026-11-26.md` (after the Round 2 section; also fix that section's last paragraph
   "Remaining ..." if needed). Commit docs + logs (`git add -f logs/ds-apply3`), clean messages, push wip/ds-apply3.
4. Final report to the parent (mechanisms + counts, verdict, gate, per-deck A/B, d8 b0, hashes, what remains).

## What remains in the cluster (for the report; not attempted)
- Two-Apex lines (8 events, 42 flags): rest holds the second Apex as one bill; the exact judge credits only the paying
  Apex's ten, so both boards read unpayable and the lever stands down (hypothesis, unverified).
- Float {B} drained by a later Song's generic (s30132752, s30135813, s30152803; 24 flags): LINE_FLOAT_TAP_FIRST spare test
  nets only the paying ritual (later Song/Irencrag outputs not credited), and its spares must pay the bill's own {R} pip
  too; storage tier not released (s30135813, Mercadian Bazaar).
- Unclaimed Territory as the {B} (s30115282, s30145461, s30149079).
- Round-1 item d = this cluster's one-Apex shape (5 of 9 events fixed). Item e (Karrthus need-later past Apex) untouched:
  the lever reads only the named line.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm).
- Adoption bar: judge the average at play settings. Adopt if many decks gain, mitigate the few that get worse
  afterwards, ignore seed noise and clairvoyant losses; d0 barely counts.
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything must stay findable at unlimited budget.
- NO GREEDY PICK INSIDE THE SEARCH WINDOW: the engine is heuristic-free (CLAUDE.md). Heuristics live only in a
  DecisionProvider, and only restrict or order options.
- USER-owned decisions are surfaced in your report, never settled by you.
- Box: ONE big run at a time under `flock build/scratch/claude_box.lock` with MTG_RSS_CAP_GB=40 MTG_PGO=0; small jobs via
  `scripts/box_lane.sh --slots N` (N <= 2); builds `./build.sh` only (full builds under claude_build.lock); wait ~200 s
  after src edits before building (9p lag); never `timeout`; never pgrep -f waiters; never push the shared branch;
  never touch references/; gh is not authenticated (skip Windows CI). Context: hand off past ~250K.

## Traps
- The visible-closer lump rule (MTG_ONDEMAND_VISIBLE_LUMP) stands down when its ORDINARY trial fails ("break"), which is
  exactly the board where the lump is needed for the bill; the fix lives in the step-3 lump ladder instead.
- The oracle's APPLY-DROP flag prints the witness after the engine's apply; the hook=1 [ds2pay] lines are the oracle's.
- Debug snippet (`logs/ds-apply3/dbg.snippet`, adds `unseen=`) goes after the SwapLeftoverForLine call; `$S/bin_dbg` is
  the round-2 code + snippet (MTG_DS2_PAYDBG=<turn>).
