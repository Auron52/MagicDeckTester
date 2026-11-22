# Bruna APPLY-DROP line: agent 7 handoff (2026-11-22 12:37 UTC), written for an agent with no context

Read first: CLAUDE.md (box rules, push rules), then `logs/bruna-apply-drop/handoff6.md` in THIS tree (state up to agent 6).
Two worktrees:
- `.claude/worktrees/bruna-apply-drop`, branch `wip/bruna-apply-drop` (pushed, head a1d3ba763). Its src is being
  BUILT by agent 6's lock7 job: do NOT edit it until `build/scratch/bad/a6/lock7.status` shows "build rc=".
- `.claude/worktrees/bruna-lordproj`, branch `wip/bruna-lordproj` = a1d3ba763 + b4819189d (this file's tree).
Scratch: `B=/workspaces/MagicDeckTester/build/scratch/bad` (agent 6 `a6/`, agent 7 `a7/`).
Compare arms: `python3 $B/cmp_arms.py <wins_dir> <batch.out> <armA> <armB>` (net = A - B; an unwon game scores 9).

## Done
- a1d3ba763 pushed to origin/wip/bruna-apply-drop.
- b4819189d (wip/bruna-lordproj): `MTG_ATTACK_LORD_CAST` (heurarm slot ATTACK_LORD_CAST, default ON). The fix for
  docs/design/d0-lord-pump-lethal-projection.md. ProjectPlanWalk (TurnSolver.cpp) used to erase every body the plan
  cast before reading the board, so a lord's static pump vanished. Now a lord body (IsLordPermanent) cast this turn
  is KEPT, tapped (it still pumps; it never counts as an attacker, so there is no double count). The flag is
  kWalkLord in BuildAttackTapCtx, gated on pending_atk > 0. It covers both Solve and EnumeratePlans through
  AttackTapLoss. Unit: test_attack_projection.cpp "F3b lord cast" (on: threshold 6; off: 4).
- tok3 result (a6/tok3/cmp.txt), TOKEN_SELF on vs off: sea 0/8000, sea2hg -8/2000, spirits 0/4000, sead0
  -6317/200k (326 slower), spiritsd0 +6/200k (FAILS the bar; root cause = the lord projection above).
- Where the levers are read in SEARCHED play:
  - The projection reaches it through EnumeratePlans `wins_this_turn`, which feeds:
    - the first move-ordering key (~TurnSolver.cpp:106250);
    - the "first winning candidate returns" shortcut (~127783);
    - the HoldCastUnlessLethal / sac-mana filters;
    - the m2 lethal-probe filter (~109932).
  - TOKEN_SELF reaches it only through EvalCard -> plan.value (the ordering key after wins).
  - Both are also read in the greedy Solve playout past the horizon. But Sea, Spirits and almost every lord deck
    have a value sidecar (the learned leaf), which explains the small searched movement.

## Milestone 14:20 UTC
- build_lp done: $B/a7/lp/mtg built, F3 units pass (4648/4648).
- lock7 results (a6/lock7.status):
  - build rc=0, Bruna units rc=0.
  - perf instructions:u, fixes on/off: EDF d0 1.0575, Jumbo d0 1.0294, Sea d0 1.0127, Bruna d0 1.0012. EDF and
    Jumbo are above 1.01, so profiling is owed.
  - gi21770: swap=1 and swap=0 are BOTH unwon (9). But fix7 has NO APPLY-DROP in that game (mana oracle, T6-T8).
    The fixes-off arm wins on T8 only because it APPLY-DROPs a Sage on T5 (lucky). In fix7 it is a different d0
    line, not a payment defect. The d8 b0 recovery check is queued (below).
  - The swap A/B is running under the lock (a6/swap).
- Queued in the lane:
  - `$B/a7/g21770/d8.sh` (lane PID 940337): gi21770 d8 b0, exact=1 and exact=0 -> `$B/a7/g21770/d8.status`, `d8.DONE`.
  - `$B/a7/perf/run.sh` (lane PID 940580): per-fix split of the Ir cost (EXACT/KEEP 10 vs 01) on EDF d0 200 and
    Jumbo d0 3000 -> `$B/a7/perf/summary.txt`, `DONE`. Compare with lock7's 11 (on) and 00 (off).
    Then callgrind the costly fix: the cg5/run.sh shape, 40 EDF d0 games, on vs off.

## Milestone 15:00 UTC
- Swap A/B (a6/swap/cmp.txt) PASSES: brunad0 -2 (2 faster, 0 slower), every other deck 0, cpu ~1.00.
  MTG_CREATURE_FLOAT_SWAP stays ON.
- Perf split (a7/perf/summary.txt): ALL of the EDF d0 (+5.75%) and Jumbo d0 (+2.9%) Ir cost is
  MTG_AURA_HOST_LINE_KEEP (KEEP-only = the on numbers; EXACT-only = the off numbers).
- Queued in the lane:
  - callgrind EDF d0 40 games, KEEP 1 vs 0: `$B/a7/cg/run.sh` (lane PID 986493) -> `$B/a7/cg/edf_keep{1,0}.cg`, `DONE`.
    Next: `callgrind_annotate --inclusive=yes` on both and cut the cost inside AuraHostHoldKeepsLine
    (ManaPayment.cpp ~10928) or at its call sites (AIEngine.cpp 11240, TurnSolver.cpp 11542 / 12748, memo 11159).
    Any cut must be sound (identical play; MTG_AURA_HOLD_MEMO_VERIFY-style 0 mismatches).
  - gi21770 d8 b0 relaunched with --ignore-play-profile (lane PID 986214) -> `$B/a7/g21770/d8.status`.
- lpab is waiting on the box lock (other sessions hold it).

## Running jobs (all agent-owned, none USER-requested; waiters use DONE files / kill -0, never pgrep -f)
| job | PID | what | marker / output |
|---|---|---|---|
| lock7 (agent 6) | 858877 | waits on the box lock, then: full build of bruna-apply-drop -> $B/fix7, Bruna units, gi21770 swap 1/0, perf instructions:u (EDF/Jumbo/Sea/Bruna d0, fixes on/off), swap A/B | `$B/a6/lock7.DONE`, `$B/a6/lock7.status`, `$B/a6/swap/cmp.txt` |
| build_lp | lane 896277 | lane 2-slot build of bruna-lordproj -> $B/a7/lp/{mtg,mtg-test}, then `mtg-test -tc='F3*'` | `$B/a7/build_lp.DONE`, `$B/a7/build_lp.status` |
| lpab | 896891 | waits for build_lp (F3 units rc=0) AND lock7.DONE, then the box lock: one pooled batch `$B/a7/lpab/m.json` (mk.py, seed base 1810000). Arms off = LORD 0 + TOKEN 0; lord = LORD 1; both = LORD 1 + TOKEN 1. Shapes: 19 lord decks 4000 searched (b20, play depth) + 50k d0; sea/sea2hg/spirits 8000 searched, spirits2hg 4000; sead0/spiritsd0 200k; `both` arm only on sea*/spirits* | `$B/a7/lpab/DONE`, `cmp_lord.txt` (lord vs off), `cmp_both.txt` (both vs off) |
At 12:37 the box lock was held by another session (claude_maze_summit batch) with ~6 waiters, and lock7 was still waiting.

## Next, in order
1. lock7 done -> read lock7.status. The Bruna units must pass. gi21770 with swap=1 must win on T8 (swap=0 = unwon).
   Perf: if the EDF d0 or Jumbo d0 instructions ratio (on/off) is > ~1.01, callgrind a few d0 games and cut. The cut
   must be sound, with 0 VERIFY mismatches. Swap A/B (a6/swap/cmp.txt): every deck net <= 0, and every slower game
   must recover at d8 b0. If it fails, set MTG_CREATURE_FLOAT_SWAP OFF and report.
2. lpab done -> report SEARCHED per deck first, then d0, then CPU (the cpu column).
   - Bar: every deck net <= 0 in both comparisons.
   - Every slower game must recover at d8 b0. Replay with `$B/ab/slow_replay.py <list> 1810000 <out> <FLAG>`,
     under the lock or in the lane at <= 2 slots.
   - If both pass: flip TOKEN_SELF ON (EngineFlags.h EvalCombatTokenSelfOn false -> true; also its HeuristicArm.h
     comment) and keep LORD_CAST ON. If only lord passes: keep LORD_CAST ON and TOKEN_SELF OFF, and report.
   - NEVER adopt TOKEN_SELF alone. A failing lever stays OFF and the numbers go to the USER.
   - Also measure LORD_CAST CPU: perf stat instructions:u on Spirits d0 and Merfolk d0, with LORD_CAST 1 vs 0.
3. Cherry-pick b4819189d onto wip/bruna-apply-drop (after lock7's build). Rebase onto origin/phase-1-2-deck-analyzer
   (rebase only; include the Merfolk ENUM-GAP fix if it has landed). Build at full cores under
   `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with MTG_RSS_CAP_GB=40, then run the units. Fix
   the Pirates routing unit if it fails (it resolves decks/ next to the binary).
4. `MTG_PGO=0 MTG_RSS_CAP_GB=40 test/regression.sh --both` under the lock. Attribute every changed key. Every slower
   game must recover: test/classify_turn_later.sh (4x/16x), then d8 b0 or an escalating budget. Only CLEAR
   clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered. Then `--both --accept` with a one-line note,
   `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`, and push wip/bruna-apply-drop ONLY.
5. Report only, no work: lever 7a (Jumbo d0 +6) and lever 7b (Niv d0 +13) are USER decisions (see handoff6).

## USER rulings (verbatim)
- "Yes, we should probably fix the problem. But again, I'm more concerned about areas that help searched."
- Coordinator for the USER: size searched >= 2,000 games per shape; report searched per deck first, then d0, then
  CPU; "Don't adopt TOKEN_SELF alone."
- "The bar is every deck net <= 0 ("+1 is not neutral. 0 is neutral."), so do NOT treat it as acceptable."
- "USER: cut CPU; 3x is not a CPU licence. Prefer exact cuts (identical play)."
- "Rule-override levers 7a ... and 7b ... break standing rules, so the USER decides; report the numbers and do NOT adopt."
- "NEVER push phase-1-2-deck-analyzer." (the parent lands it through the push queue)
- Box: one big run under the lock with MTG_RSS_CAP_GB=40; small work via scripts/box_lane.sh --slots <=2; no
  timeouts; no pgrep -f; references are commit-only; build >= 200 s after src edits; hand off at ~250K context.
- USER 12:35: "keep running until it runs out, but keep a Codex handoff current" (update this file at every milestone).
