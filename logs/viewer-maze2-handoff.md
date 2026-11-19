# viewer-maze2 handoff (2026-11-19)

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/viewer-maze2`, branch `wip/viewer-maze2` (based on
origin/phase-1-2-deck-analyzer f1342be37). Subagent rule: push wip/* only, never the shared branch.

## USER words (verbatim, relayed by the coordinator)
1. "See reference 4 for Maze's end. There is a viewer bug where I cannot choose the second search target if I activate 2 maze's end."
2. "Message for Manabond makes no sense: Dig — Manabond (end step): put 3 land(s), discard 4 card(s)
   Look at the top 1 cards — put one highlighted card into your hand (it becomes playable this turn), or take nothing. The rest go to the bottom."
3. "See seed 5. The reference there didn't give me a choice on whether to activate Maze's End at the end."
   "(I mean it was the right play, but it should be a dialog)"

## Root causes
1. **Second target (ref s4_gi3, decision 24).** NOT the engine cap: `--validate-line "land=Azorius Guildgate;act=Maze's End;act=Maze's End"`
   returns all 190 = C(19,2)+19 target pairs (66 past the 200-plan emit cap, reached via `matched_plans`).
   The bug is the client walk: CheckLine keys subs "<card> →", so two activations share ONE key; `LineBuild.nextDimension`
   read only a key's first sub (`subOf = filter()[0]`), so after the first pick nothing was left to ask and
   `stepChoose` committed `remaining[0]`. Also, the dedup sig is order-independent, so "first sub == A" lost every {A,B}
   listed as (B,A). Replay side was fine: ref 4's plan 3 replays BOTH targets (final events: Simic Guildgate + Heap Gate).
2. **Dig text.** The dig chooser is reused by ~30 non-dig questions; `digPanelHtml` fell through to the generic
   "look at the top N ..." text for Manabond, all three off-main Maze's End prompts, Burgeoning, Mihail {1},
   pay-or-sacrifice (Gateway Plaza), Herald's Horn "(upkeep reveal)", Timeless Witness "(return from graveyard)",
   the Arcanum Wings aura swap, and five BARE-NAME sources (attack dig -> said "into your hand" though it goes onto the
   battlefield; sac-outlet haste/-X target; Rimefeather ice counter; Tectonic Giant impulse keep; Scrying Sheets reveal).
   Bare-name sources cannot get a tag: `source` is the frame identity in `viewer_protocol_check.py frame_ident`.
3. **s5 off-main win.** `TryOffMainAltWinActivation` (SpellEffects.h) takes a winning off-main activation as a dominance
   fold with no human branch; in ref s5_gi4 it fired inside `PerformEndStepManabond` after Manabond's dump (decision 26),
   so the human was never asked.

## Done
- `0b974260a` (pushed to origin/wip/viewer-maze2): multiset-consuming dimension walk in `tools/play/linebuild.js`
  (`filterByChoice` consumes the matched sub on a copy; key asked while it narrows; `dim.nth/of` -> "(2 of 2)");
  `index.html` routes every narrowing via `LB.filterByChoice`, `attachPrefFor(card, nth)`; `LB.digPrompt(d)` wording
  table + rendering in `digPanelHtml`; tests `checkRepeatedCardDimension` (fails on the old walk) and `checkDigPrompts`
  in `test/viewer_linebuild_check.js` (all pass, 886 refs 0 FAIL); `test/viewer_protocol_check.py` derives the deck folder
  by inverting server.js `safeStem` -- references/Maze_s_End was "unknown deck dir" CONTRACT-FAIL, i.e. NO Maze's End
  reference was replayed by anything before this.
- WIP commit (pushed, UNBUILT/UNTESTED -- see the next commit hash in `git log`), C++: `src/core/GameLogger.h` (`g_play_dig_role` + `DigRoleScope`),
  role scopes at the 5 bare-name sites (SpellEffects.cpp attack dig; SpellEffects.h outlet/impulse/reveal_top/ice),
  `src/main.cpp WriteDigDecisionJson` emits `"role"` + notes for every new kind, `src/core/SpellEffects.h`:
  `MazeOffMainHumanPickAt` (explicit default), human branch in `TryOffMainAltWinActivation` (asks over the full
  unpruned menu, default = the Gate the fold would take, so old refs replay to the same win via heuristic_default),
  decline memory `MazeHumanDeclinedBoard()` so the bank + GameEngine's later win check do not re-ask the same board;
  unit test "human play is ASKED before the off-main win" in `test/unit/test_mazes_end_f1.cpp`;
  `tools/play/DECISIONS.md` rows.

## Remaining, in order
1. Build result. QUEUED (not yet started) behind other agents on build/heavy.lock: flock PID **1156905** (parent bash 1156904),
   log `/workspaces/MagicDeckTester/build/scratch/viewer-maze2/build1.log`, DONE marker = the line `BUILD EXIT <rc>`
   appended to that log (wait: `until grep -q "BUILD EXIT" <log>; do sleep 30; done`, or `kill -0 1156905`). If the
   session that launched it died, the flock may be gone: re-run
   `flock /workspaces/MagicDeckTester/build/heavy.lock env MTG_BUILD_JOBS=12 ./build.sh` in the worktree. The C++ has
   NEVER been compiled -- expect possible compile errors. Fix them.
2. Run units (the new F1 test). Amend/fix-up the WIP commit; push wip/viewer-maze2.
3. `flock build/heavy.lock bash test/regression.sh smoke` in the worktree: expect NO GT key to move (all changes are
   human-play-only: `MazeOffMainHumanAsks`, JSON role/notes).
4. Replay: `cd /workspaces/MagicDeckTester && MTG_BIN=<worktree build/Release/mtg> python3 .claude/worktrees/viewer-maze2/test/viewer_protocol_check.py --only Maze_s_End --threads 7`
   (cwd MUST be the primary: the Maze refs are untracked there). Baseline with the primary's binary
   (`build/scratch/viewer-maze2/vpc_base2.log`): 4 ok, 2 repaired, 1 ENUM-GAP (claude_s12345681_gi12345680:
   Guild Summit target 'Thran Portal' no longer offered -- PRE-EXISTING, surfaced only because the refs are now gated;
   regression's VPC --strict will FAIL on it until root-caused). Expect s5_gi4 to become "repaired" (new win prompt
   answered by default) with the same T5 win; check every other ref unchanged. Ideally also a full VPC sweep, since
   the role/notes change touches every deck's dig JSON (identity unchanged, so should be neutral).
5. Optional: viewer_client_check.js (jsdom, needs binary).
6. Report to coordinator; the viewer (port 8080, primary) runs the PRIMARY's code, so the USER sees the fixes only
   after this lands on the shared branch and the primary is fast-forwarded and the viewer restarted (commit refs first).

## Notes
- references/Maze_s_End/* are untracked in the primary but check_references_safe.py says all are on
  origin/references-autosave (OK).
- Scratch: `build/scratch/viewer-maze2/` (val.out = the real 190-variant validation payload for decision 24).
