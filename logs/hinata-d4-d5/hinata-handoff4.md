# Hinata d5E0 GT accept -- handoff 4 (2026-11-22, Hinata agent 3; written for a Codex agent with no context)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/hinata-d4-d5`, branch `wip/hinata-d4-d5`.
Read FIRST: `logs/hinata-d4-d5/hinata-handoff3.md` (gate result, the 23 slower games and how each recovers,
the exact accept command + note, USER rulings verbatim) and `/workspaces/MagicDeckTester/CLAUDE.md`.
Ship commit: 0c993e9a9 (`decks/Hinata/Hinata.value.json`: ladder emulated, beam_width 0, target_depth 5).
Last commit before this file: c3eb0a931.

## USER rulings (verbatim; also in handoff3)
1. 2026-11-21: "This doesn't make much sense. We shouldn't be getting better results with Depth 4 than Depth 5 unless
   perhaps this is just variance. If they are similarily constrained below 4 turns lookahead they should act the same and
   going further should not make the results worse. We need to dig into this rather than just wave it off."
2. 2026-11-22: "if it is that much better then we should work on it. Can we not try an escalating budget to see if it
   recovers rather than d8 b0 if that is unreachable? I don't like leaving gaps and this is the alternative."
3. 2026-11-22 12:35 (via coordinator): "Claude usage is at 5%, so keep running until it runs out, but keep a Codex
   handoff current."

## Status (18:25 UTC): DONE except the final push
- Rebased onto origin cd330ec5a (Merfolk landed). gate2: build 0, units 0, `--both` 624 passed, 8 FAIL (all Hinata), with
  digests byte-identical to the 4a7f65983 gate, and the slower-game listing identical (the 23 from handoff3). Logs:
  `/workspaces/MagicDeckTester/build/scratch/claude_hinata_d4d5/gate2/`.
- Accepted smoke + regression: **a579747bf** (GT + job_cost + gt_logs + doc section 5). check_gt_logs clean (1409).
- check_references_safe exit 0; wip branch pushed. NOTHING LEFT for this agent; the coordinator lands it.

## Next commands, in order
1. Wait until origin has the Merfolk fix:
   `git -C $WT fetch -q origin && git -C $WT log origin/phase-1-2-deck-analyzer --oneline -30 | grep 'Merfolk ENUM-GAP: ship the generic option'`
2. `cd $WT && git rebase origin/phase-1-2-deck-analyzer` (NEVER merge). On a conflict in `test/regression_gt.txt` or
   `test/job_cost.tsv`, take origin's side for every key Hinata does not own (re-accepting fixes Hinata's own keys).
   Then run `python3 test/check_gt_logs.py`.
3. `setsid nohup /workspaces/MagicDeckTester/build/scratch/claude_hinata_d4d5/gate2/run.sh >/dev/null 2>&1 &`.
   Wait on `gate2/DONE` (or `kill -0 $(cat gate2/pid)`), never on `pgrep -f`, and with no timeout wrapper.
4. Inspect `gate2/regression.log` (`grep -n 'FAIL ' gate2/regression.log`):
   - only `hinata*` keys may fail;
   - they should match handoff3's table (smoke d3 5.52, smoke d5 5.43, 2hg smoke d5 5.28, reg d5 s30001 5.20,
     reg d3 s2002 5.60, reg d5 s2002 5.4067, reg d5 s3003 5.6867, 2hg reg d5 s2002 5.3867);
   - run `bash test/classify_turn_later.sh smoke` and `... regression` (under the box lock). The slower set must equal
     handoff3's 23, or each new game must recover at 4x/16x, or by an escalating budget / d8 b0.
5. Accept, using the exact command and note in handoff3 step 1. Accept promotes the last run and does not re-run it.
6. `python3 test/check_gt_logs.py`; `python3 scripts/check_references_safe.py`.
7. Update `docs/design/hinata-d4-vs-d5-2026-11-21.md` section 5 ("What the USER decides"): record the USER ruling, the
   ship (0c993e9a9), the gate and the slower-game result. Commit GT + doc.
8. `git push --force-with-lease origin wip/hinata-d4-d5`, after checking that all of origin's wip commits are
   contained. NEVER push phase-1-2-deck-analyzer (the coordinator lands it through the push queue).

## Box rules
- One big engine process at a time, on `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`.
- Small jobs go through `scripts/box_lane.sh --slots <=2`.
- Never change src in a tree a run reads from. References are commit-only.

## Open USER items (surface them; do not act)
- Anti-Lifegain, Creature Giving and Dragonstorm ship the same depth-5-only W3 value beam. Not measured.
- s61001 gi33 (seed 61034): unpriced plan enumeration. A perf defect, axis not attributed.
- Whether to remove the beam's `depth == 5` gate from the code rather than per-deck `beam_width 0`.
