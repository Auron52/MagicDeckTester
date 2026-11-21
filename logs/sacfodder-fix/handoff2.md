# Handoff 2: MTG_SAC_FODDER_MENU fix-up 4 (agent 4; stopped at ~270K on the coordinator's instruction)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix`, branch `wip/sacfodder-fix` (rebased onto
origin/phase-1-2-deck-analyzer 906d3c0a4; force-pushed once after the rebase). Scratch:
`/workspaces/MagicDeckTester/build/scratch/claude_sacfodder_fix4/` (tools/: mk_ab.py, cmp2.py, gdiff.py, run1.sh,
buildonce.sh = build-once mutex via `build.claim` dir + `BUILT` file).

## USER ruling (verbatim, 2026-11-21)
"I don't quite get the issues, but this seems like an issue we need to fix/extend until it is a net benefit."
(Earlier rulings, still binding, are in logs/sacfodder-fix/handoff.md: the bar is every slower game recovering at d8 b0
plus every deck net <= 0 on a large held-out sample, "+1 is not neutral. 0 is neutral."; never justify CPU by 3x headroom;
a budget may re-price work, never make a line unfindable; a budget-staged later wave is the sanctioned shape.)

## Done (commits on wip/sacfodder-fix)
- Rebase onto 906d3c0a4 (HeuristicArm.h conflict: kept both slot sets). Rebased hashes: d34de3fec (fix-up 2),
  a670be4ad, e2da93190 (fix-up 3), d1edef980, 5990a6af1. Units 3813/3813 on the rebased tree (+ staging arms).
- 2e87aadd5 WIP: staging arms MTG_SAC_FODDER_STAGE_PLAYOUT / _BPENUM / _ROOT (heurarm slots).
- 7f322b6d3 WIP (UNBUILT when committed; hold2 below builds it):
  * `SacFodderAdmitMode(state)` (TurnSolver.cpp, beside BuildBreakpointKey): 0 = lever off / no outlet in reach,
    1 = lever stands down here (beyond horizon), 2 = wave first pass, 3 = admitted. Folded into BuildBreakpointKey when
    non-zero (so lever-off keys are unchanged). FIXES A REAL DEFECT: the enumeration memo (EnumeratePlansWithLand,
    m1 and m2 hosts) keyed neither the dev-seq pass nor the admission state, so pass 1's list (credited plans dropped)
    was served back to pass 2 -- which therefore never saw credited plans (the "pass2_better=0 everywhere" of fix-up 3)
    -- and under STAGE_PLAYOUT lists could cross between playouts and searched frames.
  * enummemo::Entry::sfm_latent: a memo hit in pass 1 replays the wave's latent occurrence (before, a hit hid the drop,
    so the node was not marked dirty).
  * SacFodderMenuLeverOn() (raw lever) split from SacFodderMenuOn() (context: STAGE_PLAYOUT now also stands down in the
    d0 runner, TurnSolver::InD0Runner()).
  * Arms (heurarm, default OFF): MTG_SAC_FODDER_WAVE_EARLY (pass 1 drops a credited subset right after the flat mana
    check, before colour/walk gates), MTG_SAC_FODDER_WAVE_CTX (pass 1 enumerates the exact lever-off menu and records a
    latent wherever the fodder ctx is live). Helpers SfmWaveDropHere() / SfmWaveLatent() (wave drop now also calls
    DvsNotePassLatent). BPENUM and ROOT arms DELETED (BPENUM measured harmful, ROOT was a lossy diagnostic).
  * TEMP diagnostics still in the tree (remove before adoption): MTG_SFM_DIAG_TRACE ([sfm-2p]/[sfm-cd] stderr lines in
    FullSearchLine's two passes and the hybrid's deferred pass 2), MTG_SFM_DIAG_NOAPPLY (BuildPoSacFodder off at apply).

## Measurements (dev seeds gi 620000-624999 Goblins / 630000-634999 Fungus, x5000, play settings, vs lever off)
ab2 (build 2e87aadd5; one pooled batch, ~12 min wall at 32 threads -- the A/B loop is cheap):
| arm | Goblins f/s net | units | Fungus f/s net | units |
|---|---|---|---|---|
| on (fix-up 3, rebased) | 2/3 +1 | x1.104 (cpu x1.164) | 0/1 +1 | x1.052 |
| onP (STAGE_PLAYOUT) | 2/3 +1 | x1.099 | 0/0 **0** | x1.052 |
| onPB (+BPENUM) | 0/9 +9 | x1.098 | 0/1 +1 | x1.051 |
| onPBA (+ROOT, lossy diag) | 0/3 +3 | x1.097 | 0/0 0 | x1.050 |
Goblins slower in on/onP/onPBA: 622286, 622411, 624011 (all 3 -> 4). Faster (on/onP): 621953 4->3, 624564 6->5 -- these
come from credited plans at the SolveWithLookahead root (route (a)); ROOT removes them. Fungus: 633295 slower in `on`,
recovered by STAGE_PLAYOUT.

Attribution of the three Goblins slower games (single games, rc1/rc2 under scratch/claude_sacfodder_fix4):
- (c) the apply's fodder order walk is NOT the cause: NOAPPLY still 4/4/4; NOCREDIT (no menu credit anywhere, walk on)
  3/3/3 = off.
- (b) beyond-horizon playouts: cause of Fungus 633295 only (fixed by STAGE_PLAYOUT). Breakpoint enumerations: staging
  them (BPENUM) is harmful.
- The Goblins three are caused by credited plans INSIDE the staged FSL two-pass search: the extra pass-1 work exhausts
  the decision budget. 622286 T1 m1 trace: off pass 1 finds the T3 win (c1=3, used 51,348 incl. pass 2); pba pass 1
  ends with stop=1 (budget exhausted), win=9, so the hybrid commits a non-verified line (Aether Vial) -> T4. Wave off
  (credited plans searched directly in pass 1) loses the same three games. Causes of the extra pass-1 work: credited
  subsets go through every gate (incl. the SacFodderSubsetOrderPays walk) before the wave drops them, and every node
  that met a drop is dirty, so its answer is not stored in the in-window cache (FSLineWin's inwin store requires
  !dvs_sup).
- 622411 diverges T2 m2 (off casts Goblin Piledriver), 624011 T3 m1 (off Goblin Matron, on Lightning Bolt): not traced
  individually, same arm pattern.

d8 b0 not yet re-run on the rebased tree (fix-up 3's: every slower game recovered).

## Running job
- hold2: PID 175320 (bash: sleep 200, then `flock build/scratch/claude_box.lock bash hold2.sh`). Builds 7f322b6d3 at full
  cores (log build_hold2.log; units -> units.log), then the ab3 batch. DONE marker: `scratch/claude_sacfodder_fix4/HOLD2.DONE`
  ("BUILD FAILED" on a compile error -- the tree was committed unbuilt, so check build_hold2.log first).
  Output: ab3/batch.out, ab3/logs/*.wins. Read with
  `cd scratch/claude_sacfodder_fix4/ab3 && python3 ../tools/cmp2.py .:off .:onP .:onPE .:onPC .:onPw0`.
  Arms: off; onP (memo fix + late wave drop); onPE (+WAVE_EARLY); onPC (+WAVE_CTX); onPw0 (STAGE_PLAYOUT, wave off).

## Remaining, in order
1. Read ab3. Key questions: does the memo fix let pass 2 take lines (faster games)? Does EARLY or CTX remove the pass-1
   overspend (622286/622411/624011 back to 3)? Units vs off.
2. If pass-1 overspend persists: the dirty-node inwin loss is the next suspect -- store wave-ONLY-dirty nodes in inwin under
   a pass-1-only tag (a separate counter of wave latents distinguishes them from compound latents, so lever-off play stays
   byte-identical). Or move the whole staging to an outer pass A (exactly lever-off, ctx inert) / pass B (credited, root cut
   A.win-1, leftover budget) wrapper, which needs the same mode tag in the inwin/gamelc/lc keys.
3. Iterate on dev seeds until Goblins and Fungus are each net <= 0 (ideally < 0); d8 b0 every slower game
   (`--depth 8 --budget-ms 0 --ignore-play-profile` for Goblins); cut units (target well under x1.10).
4. Fresh held-out sample (one pooled batch, new gi range, e.g. 640000+/650000+), plus other decks with sac-for-mana outlets.
5. Adoption: default ON, delete dead arms (NEEDED_ONLY=0 path, WAVE off path, the losing EARLY/CTX, TEMP diags),
   `bash test/regression.sh --both`, audit, accept GT in this worktree, `python3 test/check_gt_logs.py`, update
   docs/design/sac-fodder-menu-2026-11-18.md (a "Fix-up 4" section is OWED: the memo defect, the attribution above).
6. Open USER-facing question: none decided by me. Note for the report: SolveWithLookahead-root credits (route (a)) are
   net positive (2 faster, 0 slower), so they stay unstaged beyond the existing move-order demotion.

## Box notes
The lane was memory-blocked for ~2 h (another session's 32 GB mana_clean batch); the big lock path built faster.
Lane jobs: `scripts/box_lane.sh --slots 4 --cap-gb 2 -- bash <script>`; keep a script's own parallelism <= 4.
