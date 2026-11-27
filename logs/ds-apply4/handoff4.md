# ds-apply4 CPU cut 2 -- handoff 4 (2026-11-27 ~20:55 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/ds-apply4`, branch `wip/ds-apply4`. Scratch
`S=/workspaces/MagicDeckTester/build/scratch/ds-apply4`, this round's dir `C=$S/cpu2`.
Base for this round: 777cba077 (binary `$S/bin_l6`). Context: logs/ds-apply4/cpu_cut.txt, handoff.md,
docs/design/dragonstorm-apply-drop-2026-11-26.md Round 4 ("CPU, and the cut").

## Done
- **c14125eb4** (WIP, local, NOT pushed): the "c3" source. All exact (same payments, same boards, same backtracker
  calls in the same order):
  1. `LineSeqRestPaysOn(GameState&)`: the walk spends a board the caller discards (KOF's kept / quiet trial; the lump
     keep-owed rung's `*scratch` and `*kept`); `LineSeqRestPays(const&)` is a pooled-copy wrapper. `LineSeqPayOn` runs
     its LAST (m, pass) attempt on the walk board itself (a false return ends the walk, which discards it).
  2. Pooled `RampScratch` boards: KOF in-place trial + kept, the walk wrapper, LineSeqPayOn's earlier attempts
     (`std::swap` a won scratch in, so the pool keeps storage), and `LumpAwareBillPayable`'s combo board.
  3. KOF: the float-pays-the-rest `-1` and a new empty-keep `-1` (no owed colour floats beyond this bill's pips) run
     before the rest's made-colour loop (all three exits are `-1`, so order is free).
- Binaries: `$S/bin_c1` (1 only), `$S/bin_c2` (1+2), `$S/bin_c3` (= c14125eb4), `$S/bin_c4` (c3 with item 2's
  LumpAwareBillPayable pooling REVERTED to make_unique), `$S/bin_pr` (probe build, never committed).
  Source snapshots: `$C/ManaPayment.c{1,2,3}.cpp`.
- Units on bin_c3: `scripts/unit_par.sh` 4333 / 4333 PASS (`$C/units_c3.txt`).

## Measurements (user instructions, `perf stat -e instructions:u`, `$C/ins.sh`, results in `$C/ins_all.txt`)
Repeat noise: bin_l6 DS on 182.629 / 182.626 G; 45-deck off 1147.404 / 1147.407 G (+-0.002%).
```
Dragonstorm play, 200 games (prof/pg_on|pg_off.json)   levers on    levers off   on/off
777cba077 (bin_l6, cpu_cut.txt)                        182.622 G    179.702 G    +1.62%
c1  walk in place                                      182.080 G        -
c2  + pooled boards                                    181.315 G    179.548 G    +0.98%
c3  + -1 tests first   (= c14125eb4)                   181.271 G    179.540 G    +0.96%
c4  = c3 without LumpAwareBillPayable pooling          181.804 G    179.740 G    +1.15%
45 other decks x 25 games (prof/nds_on|nds_off.json)
777cba077 (bin_l6)                                     1148.301 G   1147.404 G   +0.078%
c1                                                         -        1147.428 G
c2                                                         -        1147.609 G
c3                                                     1148.109 G   1147.595 / 1147.604 G   +0.045%
c4                                                     (running)    (running)
```
Savings per item (DS on arm): walk in place -0.55 G (-0.30%); pooling -0.77 G (-0.42%; of which LumpAwareBillPayable
pooling -0.53 G on, -0.20 G off); -1 reorder -0.04 G (-0.02%, below the 0.1% bar but free).
**Open trade-off:** LumpAwareBillPayable pooling also runs on the OFF arm (base code) and costs the 45 other decks
+0.18-0.20 G (+0.017%) while saving Dragonstorm 0.20 G (off) / 0.53 G (on). Only Worldfire of the 45 holds a lump
(Lotus Bloom) in its list; `$C/wf_off.json` is being measured on l6 / c3 / c4 to see whether it is Worldfire.
Choose c3 (keep) or c4 (revert) on those numbers, or find why copy-assign into the pooled board costs more there.

Identity so far: 45-deck on AND off arms, bin_c3 vs bin_l6: every digest and units equal (45 + 45 jobs).
valall on bin_c3: 117 / 1124 jobs compared, 0 digest diffs, 0 units diffs (partial, still running).

Remaining-cost profile on c2 (`$C/pr_c2.txt`, perf dwarf, 100 DS games): KOF's summed judge (LumpAwareBillPayable)
0.34%, the KOF walk 0.24%, the lump-rung walk ~0.19%, KOF board copies 0.18%, KOF prelude ~0.17%.
**Memo probe** (`$S/bin_pr`, `$C/probe.err`, 200 DS games): KOF in-place entries 67,925, of which 73% repeat a key
(BoardHash + cost + at + act names); walks 49,574, 91% repeat. A verdict memo would cut most of the remaining
~0.96%, BUT it is not provably exact: TapForCostBacktrack's tier-1 mana cache has known key holes (payer context /
plan traits; SpellEffects.cpp McT2PayWord comment), so the walk is not a pure function of the board, and skipping the
walk's backtracker calls changes later cache contents. NOT implemented; it is a question for the parent / USER
(exact-modulo-cache-path vs strict). Other ideas measured or judged < 0.1%: stopped per the brief.

## Running jobs (all mine, lane jobs; kill by PID only, never pkill -f)
- valall on bin_c3: lane pid 1937930, batch pid 1937945. Output `$C/val_c3.out` / `.err`, DONE marker
  `$C/val_c3.DONE`. Manifest `$C/valall.json` (1124 jobs, 18,500 games = val6a 648 + val6b 118 + valC 358 off-arm
  DS A/B blocks and non-DS o0 blocks). Compare: `python3 $C/cmpall.py $C/val_c3.out $C/val_ref.out $C/valall.json`
  (ref = val6a.out + val6b.out (bin_l6) + ab_out/batch.out (bin_l4 off arm = base)). ~1.5-2 h total from 20:15.
- c4 counts: lane pid 1964210, ins.sh pid 1964221 (then n_c4off, n_c4on into `$C/ins_all.txt`).
- Worldfire counts: lane pid 1966090 (wf_l6, wf_c3, wf_c4 into `$C/ins_all.txt`).

## Remaining, in order
1. Read wf_* and n_c4* in `$C/ins_all.txt`; pick c3 vs c4 (if c4: copy `$C/ManaPayment.c3.cpp`, revert only the
   LumpAwareBillPayable hunk, rebuild, and re-run valall + units on that binary).
2. When `$C/val_c3.DONE`: run cmpall.py; must be 0 digest/avg diffs (units diffs also expected 0).
3. Off-arm identity vs base is covered by valall's valC part (off arm vs ab_out bin_l4 off) and the 200 off__g* jobs.
4. Append the table above to logs/ds-apply4/cpu_cut.txt; add a "second CPU cut" paragraph after line ~476 of
   docs/design/dragonstorm-apply-drop-2026-11-26.md ("...the next CPU item if it is wanted."); amend c14125eb4 into a
   final commit (drop "WIP"); `git add -f logs/ds-apply4`; `python3 scripts/check_references_safe.py`;
   `git push -q origin wip/ds-apply4`.
5. Final report to the parent: per change its saving, the off/on table, identity results, units, commits, the memo
   question.

## Rules (verbatim from the brief)
- GOAL: cut the remaining cost with EXACT changes only (no change to any payment the levers choose; no heuristic
  narrowing; everything findable at unlimited budget). [...] Measure each with user-instruction counts (CPU seconds
  swing +/-7% on this shared box; use the same instruction-count method as cpu_cut.txt). Keep only what pays.
- VERIFY each kept change: (a) play byte-identical to 777cba077's binary -- every play digest on the 6,000 Dragonstorm
  A/B games (std + 2HG) and a sample of non-Dragonstorm blocks (reuse the previous agent's val*.json manifests in $S);
  (b) levers =0 still byte-identical to the base; (c) full unit suite from YOUR worktree's scripts/unit_par.sh;
  (d) final instruction table off vs on for Dragonstorm (200 games) and the 45-other-deck set, like cpu_cut.txt.
- BOX RULES (hard): a mana_clean gate owns the big lane now (claude_box.lock) for several hours: do NOT take the box
  lock. All your runs go through `bash scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>` with N <= 2 (so 2-thread
  batches). ./build.sh only (never raw cmake); on 9p wait ~200 s after edits before building. Never `timeout`; waiters
  use `kill -0 <pid>` or a DONE marker via run_in_background, never pgrep -f, never polling turns. No GT accept, no
  tiers, NEVER push the shared branch phase-1-2-deck-analyzer; push only wip/ds-apply4 after
  `python3 scripts/check_references_safe.py`. The parent lands it with GT.
- CONTEXT LIMIT (USER rule): at ~250K tokens finish the step, commit, write the handoff; never exceed 400K. If cuts
  stop paying (each idea < 0.1%), stop and report rather than grinding.
- USER: "cut CPU" -- suite headroom never justifies CPU.
- USER rulings carried from handoff.md (verbatim there): Dragonstorm "spend the counters if they are required for the
  line"; adoption bar = average at play settings; a sound collapse defaults ON, a lossy narrowing needs its A/B,
  everything findable at unlimited budget; NO GREEDY PICK INSIDE THE SEARCH WINDOW; USER-owned decisions are surfaced,
  never settled; do NOT accept GT; do not run regression/smoke tiers.
- Lane memory: the box has ~11-13 GB free; use `--cap-gb 3` for engine batches and `--cap-gb 4` for builds
  (`$C/build.sh <tag>` does the build + copy to `$S/bin_<tag>`; `SLEEP=0` skips its 200 s 9p wait).

## Update (after the handoff was written)
- n_c4off = 1147.611 G: c4 (LumpAwareBillPayable pooling reverted) shows the SAME +0.2 G on the 45 other decks'
  off arm as c2/c3. So that +0.017% is NOT the LumpAwareBillPayable pooling (most likely a code-generation /
  inlining shift in TapForCostShared from c2's edits; the executed play is identical, every digest/units equal).
  c4 is worse than c3 on Dragonstorm (+0.53 G on, +0.20 G off) and no better elsewhere: **keep c3 (c14125eb4)**;
  step 1 of "Remaining" is settled -- no rebuild needed. The Worldfire runs (wf_*) and n_c4on are now informational.
