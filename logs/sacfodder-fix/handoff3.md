# Handoff 3: MTG_SAC_FODDER_MENU fix-up 5 (agent 5)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix`, branch `wip/sacfodder-fix` (pushed).
Scratch: `/workspaces/MagicDeckTester/build/scratch/claude_sacfodder_fix4/` (tools/: mk_ab.py, cmp2.py, run1.sh,
run1b.sh = run1.sh with `BIN=<binary>`).

## USER ruling (verbatim, 2026-11-21)
"I don't quite get the issues, but this seems like an issue we need to fix/extend until it is a net benefit."
(Earlier rulings, still binding: logs/sacfodder-fix/handoff.md and handoff2.md.)

## The problem in plain words
Goblins' Skirk Prospector turns a creature into one red mana. The lever lets the search count those creatures as
mana when it lists what a turn can cast ("credited plans"). That finds two faster wins on the dev seeds. But three
games get one turn slower: 622286, 622411 and 624011, each 3 -> 4. In 622286 the turn-1 search without the lever finds
the turn-3 kill inside its work budget. With the lever, the same search spends its whole budget and never reaches it.
The engine then plays a line it has not verified, and wins on turn 4. So the lever does not hide the winning line. It
makes the search slower to reach it, and under a finite budget that costs a turn.

## What this agent tried (every arm default OFF; dev seeds Goblins gi 620000-624999, Fungus 630000-634999, x5000)
Each attempt tests one guess about WHERE the extra search work comes from.

| arm | guess it tests | Goblins faster/slower net | units | Fungus net | units |
|---|---|---|---|---|---|
| onP (memo-key fix 7f322b6d3 + STAGE_PLAYOUT) | baseline after the memo fix | 2/3 **+1** | x1.099 (cpu x1.156) | 0 | x1.052 |
| onPE (+WAVE_EARLY) | the per-plan cost checks on credited plans | 2/3 +1 | x1.101 | 0 | x1.053 |
| onPC (+WAVE_CTX) | pass 1's list differing from the lever-off list at all | 2/3 +1 | x1.119 | 0 | x1.058 |
| onPw0 (wave off) | -- | 2/4 +2 | x1.067 | +3 | x1.034 |
| onPI (+WAVE_INWIN, 29567011b) | pass-1 answers not being cached across depths | 2/3 +1 | x1.099 | 0 | x1.052 |
| onPCI (+CTX +INWIN) | both | 2/3 +1 | x1.119 | 0 | x1.058 |

The same three games stay slower in every arm, and the same two stay faster (621953 4->3, 624564 6->5).

What it rules out:
- The memo-key fix (7f322b6d3) is right but changes no game: pass 2 still never takes a better line on these seeds.
- CTX gives pass 1 EXACTLY the lever-off menu, and 622286 still loses (single-game trace rc3: turn-1 pass 1 stop=1,
  win=9, vs lever-off win=3 with stop=0). So the extra work is NOT in the first-pass menus at the searched nodes.
- INWIN (wave-only dirty nodes keep their in-window answers under a pass-1 tag) changes nothing, so the lost cache
  stores are not the cause.
- Earlier (handoff2): not the apply's fodder walk (NOAPPLY), not beyond-horizon playouts (STAGE_PLAYOUT), not the
  SolveWithLookahead root credits (ROOT arm still lost the three).

## Attempt 3: WAVE_BPENUM (4205a7bc7), ab5 (binary mtg.b6, scratch ab5/)
Breakpoint enumerations are the one place inside pass 1 where credited plans are still listed. The old STAGE_BPENUM
arm measured harmful (0/9 +9), but on 2e87aadd5, BEFORE the memo-key fix, so it was re-taken:
| arm | Goblins f/s net | units | Fungus net | units |
|---|---|---|---|---|
| onP | 2/3 +1 | x1.099 (cpu x1.156) | 0 | x1.052 (cpu x1.081) |
| onPB (+WAVE_BPENUM) | 2/9 **+7** | x1.100 | 0/1 +1 | x1.051 |
Still clearly harmful even with the memo fix (622036 3->6, 622749 3->5, 624402 3->7, 624395 6->no win). Out.
Why it fails is untraced. One candidate: wave latents never move g_devseq_hard_n, so a pass 1 that met only wave/canon
latents under a hybrid host has its pass 2 deferred to the hybrid's LAST wave (DevSeqDeferredP2), which a spent budget
skips. BPENUM makes such pass-1s much more common.

## d8 b0 (onP, binary mtg.b6, `--depth 8 --budget-ms 0 --ignore-play-profile`, scratch d8b0/)
622286 -> 3, 622411 -> 3, 624011 -> 3: every slower game RECOVERS at unlimited budget (lever-off is 3 in each). The
lever never makes the line unfindable; the loss is purely about budget efficiency.

## Status: PARKED (stuck-iteration rule)
Three attempts since handoff2 (EARLY/CTX in ab3, INWIN in ab4, BPENUM in ab5) moved none of the three slower games.
Per the rule, the line is parked here with the evidence above rather than iterated further.
Best arm on the dev seeds: onP (MENU + STAGE_PLAYOUT, memo fix): Goblins net +1 (2 faster, 3 slower), Fungus net 0,
units x1.099 / x1.052. That does NOT meet the bar (every deck net <= 0), so the lever stays default OFF and no GT accept
is owed.

## Remaining, if the USER wants the line reopened (in order)
1. Untested hypothesis, the only lead left: MEMO FRAGMENTATION. The admission-mode fold (7f322b6d3) gives pass-1
   searched enumerations (mode 2) and playout enumerations (mode 1, STAGE_PLAYOUT) different BuildBreakpointKey keys,
   where lever-off shares one key (mode 0). Under CTX the two lists are identical, yet they are still computed twice.
   That is the one cost CTX does not remove, and it fits "CTX gives the exact lever-off menu and still overspends". Test:
   under CTX, key modes 1 and 2 alike, and on a pass-1 hit replay the latent when the fodder context is live at the
   state (computed on hit rather than from the stored entry). Measure 622286 single-game first (rc3 trace: lever-off
   turn-1 pass 1 win=3 stop=0 at ~51K units; onP/onPC win=9 stop=1 at ~54K).
2. If that recovers the three: fresh held-out sample (one pooled batch, e.g. gi 640000+ Goblins / 650000+ Fungus, plus
   other decks with a sac-for-mana outlet), d8 b0 on any new slower game, cut units (x1.099 on Goblins is too high).
3. Adoption: delete the dead arms (WAVE_EARLY, WAVE_CTX if unused, WAVE_INWIN, WAVE_BPENUM, NEEDED_ONLY=0 path, TEMP
   MTG_SFM_DIAG_TRACE / MTG_SFM_DIAG_NOAPPLY), default ON, `bash test/regression.sh --both`, accept GT,
   `python3 test/check_gt_logs.py`, write the owed "Fix-up 4/5" section of docs/design/sac-fodder-menu-2026-11-18.md.

## Commits (wip/sacfodder-fix, pushed)
- 7f322b6d3 memo-key fix + EARLY/CTX arms (built as mtg.7f322; units 3813/3813)
- 29567011b WAVE_INWIN arm (mtg.b5; units 3813/3813)
- 4205a7bc7 WAVE_BPENUM arm; SacFodderAdmitMode's mode 2 reads SfmWaveDropHere (mtg.b6; units 3813/3813)
- this handoff

## Running jobs
None. All scratch output is under build/scratch/claude_sacfodder_fix4/ (ab3, ab4, ab5, rc3, d8b0).
