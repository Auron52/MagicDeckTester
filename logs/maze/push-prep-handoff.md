# Maze's End push-prep (push2) handoff -- 2026-11-20

Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/push2/` (r1/ = round 1 on ae2b9f91b, r2/ = round 2 on 2f67da724, top level = round 3).

## Integrated on wip/maze-shared (base 93634781f, head 722fdd3dd at hand-off; NO GT commit yet)
Order (rebased, never merged; branch GT-accept commits 3ceb22633 / 17d414e8f DROPPED, GT = upstream's, to regenerate):
* parent's local doc commits on `docs/design/mazes-end-work-queue.md` (all kept, incl. 3 the parent added mid-run).
* P5c (origin/wip/maze-p5c): 9b23ebd72 engine fix, 396035b5a fetch rule (P5c2), 40599130d ledger, 4ea2268d8 P5c3 fix, 2b4acfd34 ledger.
* P7 step 1 = **17d414e8f** (origin/wip/maze-p7): 1b374186f, 37b86db2a, 78197a557, 795974991 + 947a3796f (logs/maze/P7*.md records). No `P7-4-handoff.md` existed, so P7c (fc204f2f8, ca0a10718) is NOT in.
* NEW c-commit 722fdd3dd: P7's dominance-fold `norm` lambda cleared `Plan::move_front`; upstream 30c7aae7b replaced it with `std::int8_t move_tier`, so it now clears `q.move_tier = 0` (build broke without it).
Conflicts: only `src/ai/HeuristicArm.h` append-lists, three times (vs SAC_FODDER_MENU; vs STUFFY_*/SEA/AURA_CEIL/PO_UNLOCK/SEQ_TRAILING; vs RFX_FAN_CHARGE/STUFFY_*). Resolved upstream slots first, then MAZE_CAST_DOMINANCE, MAZE_LAND_DOMINANCE, same order in enum and kNames. Ledger and CMakeLists merged cleanly (both new unit files present).
Backups: `backup/maze-shared-pre-push2` (start), `backup/maze-shared-push2-on-ae2b` (round 1 incl. its GT commit dab082ada).

## chain3 (round 3, on 93634781f) -- RUNNING, do not kill
`build/scratch/maze/push2/chain3.sh`, PID 1646486. Done: build exit=0, base build exit=0 (push2-base), units exit=0 (`units.log`).
Tiers: `flock ... env MTG_PGO=0 bash test/regression.sh --both` flock PID 1897523, harness PID 1944338 (cwd maze-shared), started ~25 min before hand-off.
Output `push2/tiers.log`; marker `push2/tiers.done` ("tiers exit=N"; exit 1 is expected = moved keys).
Base binary: worktree `/workspaces/MagicDeckTester/.claude/worktrees/push2-base` (detached at **93634781f**, clean), binary `push2-base/build/Release/mtg` -> for the base arm of d8 b0 checks. Remove it with `scripts/safe_worktree_remove.sh` when done.

## What remains
1. When tiers.done lands: `grep -E '^\s+FAIL' tiers.log`. Expected moved set = the 13 decks mazesend, mazesend2hg, snow, cyclone, cyclone2hg, hydras, hydras2hg, melira, landfall, niv, niv2hg, alchemist, martyr. Any other key = finding (report with game, do not accept).
2. Slower games of moved keys vs upstream gt_logs (script `push2/slower.py` pattern; round-2 version inline in r2/slower2.txt) -> one pooled d8 b0 batch, `abandon_units: 50000000`, `ignore_play_profile: true`, `seed = base+gi`, `game_index = gi`, 2HG keeps starting_life 30 / opponent_heads 2; new arm = maze-shared binary, base arm = push2-base binary for any that do not reach the GT turn. Manifest template: `push2/r1/d8/manifest.json`.
3. `bash test/regression.sh --both --accept --deck=mazesend,mazesend2hg,snow,cyclone,cyclone2hg,hydras,hydras2hg,melira,landfall,niv,niv2hg,alchemist,martyr`; diff GT; `python3 test/check_gt_logs.py`; `python3 scripts/check_references_safe.py`; commit; push wip/maze-shared.

## Measurements so far
Round 1 (ae2b9f91b): units 3668/3668, scenarios 584/584, refs 0 drift. Moved exactly P5c's 26 keys + P7's mazesend_smoke_d3; every non-Maze key landed on P5c's own accepted digest. Aggregates: mazesend_regression_d5_s2002 5.6902->5.6630, _d5_s3003 5.7000->5.6800, _d3_s30081 5.0000->4.0000, mazesend2hg_smoke_d5 5.5333->5.6000, martyr_regression_d5_s2002 5.1800->5.1867. 12 slower Maze games at d8 b0: 9 reach the GT turn; s2002 g16 (T6) and g99 (T8) EQUAL the base binary's own d8 b0 with identical digests (base b20 beat its d8 b0); s3003 g25 = base d8 b0 T6 (P5c's d8t). Accepted then dropped when upstream moved.
Round 2 (2f67da724): units 3715/3715, scenarios 584/584. 26 keys moved, all in the 13 decks (hydras_regression_d5_s3003 no longer moves). mazesend_smoke_d3 5.3000->5.2000, smoke_d5 5.7500->5.7273, reg_d5_s2002 5.6902->5.6576, reg_d5_s3003 5.6933->5.6733, d3_s30081 5.0->4.0, martyr d5_s2002 5.1533->5.1600. Slower: smoke_d5 g28 (+2HG g28), reg_d5_s2002 g16/g32/g55/g157/g183, reg_d5_s3003 g25/g51, martyr g149. g55, s3003 g51 never d8-checked (round 2 was superseded before the d8 step); martyr g149 = base d8 b0 T6 identical digest in P5c's d8t.

## Caveats
* **2 Mill references shuffle-dead** (Mill/claude_s2_gi1.json, Mill/claude_s6_gi5.json) from round 2 onward. NOT this integration: `maze-f7` (upstream src without P5c/P7) shows the same two; round 1 on ae2b9f91b showed 0. Upstream delta ae2b9f91b..2f67da724 (opponent shuffle ordinal / opponent-lands front block) is the suspect. Agent's to fix (references are never the user's to re-save); not fixed here.
* Upstream moved three times during this work (ae2b9f91b -> 2f67da724 -> bc1d28bbd -> 93634781f), and origin is now at f467e4620 (CLAUDE.md push queue). Any src/GT delta past 93634781f needs another rebase + rebuild + smoke check.
* `chain.pid` (untracked, 1544526, not mine, dead) sits in the maze-shared root; left alone.
* GT header will read whichever HEAD the accept runs at.

## USER rulings held (verbatim)
* On P7: "I suppose P7's prune premise is fine."
