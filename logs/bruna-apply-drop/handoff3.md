# Bruna APPLY-DROP: handoff 3 (2026-11-21 20:45 UTC, agent 3 -> agent 4, context cap)

Read `handoff.md` in this directory first (handoffs 1-2 plus the "Agent 3 progress" section at its end).

## Done (with hashes)
- wip/bruna-apply-drop (worktree `.claude/worktrees/bruna-apply-drop`), pushed:
  - c8f363ac6: fix 1 MTG_CREATURE_LEFTOVER_EXACT + fix 2 MTG_AURA_HOST_LINE_KEEP (default ON, heurarm slots, unit tests).
  - **7b621e156**: fix-2 CPU cut in `AuraHostHoldKeepsLine` (ManaPayment.cpp). It now returns "hold" early when no
    other untapped unrestricted source makes a colour the rest of the line owes, and runs the UNHELD payment first:
    the hold is kept when the unheld payment spares the host or does not keep the rest payable. UNBUILT and UNMEASURED.
  - Progress commit: logs handoff.md "Agent 3 progress".
- wip/bruna-rule-override (worktree `.claude/worktrees/bruna-ro`, rebased onto 7b621e156), pushed. These are
  measurement-only levers, default OFF, USER-owned:
  - 554f971e8 `MTG_PAY_LINE_CRIT_LAND` / `MTG_PAY_LINE_CRIT_OPENER` / `MTG_PAY_LINE_CRIT_BEHIND_DORKS` (heurarm slots
    PAY_LINE_CRIT_*; `crit_hold` block in TapForCostSharedOnceImpl after ps_hold; key placement just before the
    plain-tier key). The old a251d56ad rung is restored behind LAND. OPENER makes it live when
    g_line_opens_unseen (tutor lines). BEHIND_DORKS puts a critical land at key[0]=499, behind the creature band
    (64+) and ahead of the fodder tier (500+).
  - 173dbddf7 / 5c3622969 / e4fd2d3c7: unit test "Bruna: line-critical land variants (measurement levers)". The
    s30051257 Sol Ring + Wings board is checked under BEHIND_DORKS; the s30051968 Remote Farm + Forest + Wish opener
    board under LAND+OPENER. Each lever must fix its board when ON and reproduce the drop when OFF. NOT YET COMPILED:
    the first build failed on `counters.push_back`, now `counters.Add`.

## Measurements so far
- fix12 binary (c8f363ac6) = build/scratch/bad/fix12/{mtg,mtg-test}.
  - Units: 3805/3805 pass. The new leftover and WG tests FAIL with their flag =0.
- Repros (build/scratch/bad/repros_fix.log, APPLY-DROP base -> fix12):
  - Fix 1: s30054069 T4 12->3, s30051277 T3 8->2, s30051111 T5 60->28. All three go back to base with fix 1 off.
  - Fix 2: s30053757 T2 8->0, s30051079 T3 16->0.
  - Unchanged residue: s30051257 8, s30051968 3, s30051794 20, s30051713 30, s30051119 3, s30054921 9 (+ORDER 10).
- Gate slice: build/scratch/bad/gate/{manifest.json,run.sh,out.txt,err.txt}. It holds the gate's own bruna + boars +
  sea_creatures jobs of claude-queue-20261120c, MTG_MANA_ORACLE=2, on = both fixes, off = neither.
  - It ran beside the Hinata tail at 20 threads, MTG_RSS_CAP_GB=16 (>= 22 GB free).
  - **IDENTITY: the off arm equals the gate exactly** (APPLY-DROP Bruna 1,891, Boars 12, Sea 37).
  - **On arm: Bruna 1,891 -> 1,221 (-35%).** Also ORDER 112->92, DOM-CREATURE 668->623, PHANTOM 8,426->8,238 and
    CONTINUATION-DIFFERS 458->608. Avg win turn 4.8266 -> 4.8256; units 0.9995.
  - Sea 37 -> 29 (avg identical).
  - Boars 12 -> 12, play digest-identical in all 87 jobs. Its DOMINATED-CREATURE 1,606 -> 1,947 comes from the
    oracle's own what-if payments, not from engine play.
- A/B ab2 (build/scratch/bad/ab/manifest_ab2.json from mk_ab2.py, seed base 710000, chunks of 25):
  - Arms: on = both fixes, off = neither.
  - Decks: bruna 10k, boars 5k, slivers 6k, sea 4k, edf 4k, fungusv3 4k, jumbo 4k, alchemist 2.5k pairs searched
    (suite d5 budget); 2HG rows; and d0 10k pairs each.
  - It ran beside the Hinata tail at 24 threads, cap 16, using the fix12 binary.
  - **Its per-game wins were NOT written** (`--log-dir` is the JSON trace flag; per-game wins need
    `--game-log-dir`). Job-level nets come from `python3 agg2.py ab2.out`:
    - Nets: bruna -8, brunad0 -8, bruna2hg -1, edf2hg -1, alchemistd0 -1, **sead0 +1 (fails the bar)**, everything
      else 0. 283 jobs differ in digest (diffjobs2.txt).
    - CPU on/off (units unchanged, so it is fix 2's scratch work): **edfd0 1.38, edf 1.12, edf2hg 1.10, jumbo 1.09,
      sea 1.05**. Bruna/boars/slivers/fungus are within ~2%. That is why 7b621e156 exists.

## Running / queued jobs (none of them big; nothing is half-edited)
- `build_fix3.sh` in the lane. The coordinator resubmitted it at 20:41 with --slots 2, PID **363518**; log
  `build/scratch/bad/build_fix3.log`.
  - It builds the bruna-apply-drop worktree, deletes the stale ManaPayment.o first, and copies mtg + mtg-test to
    **build/scratch/bad/fix3/**.
  - It then runs `mtg-test -tc='Bruna*'` into fix3/units_bruna.log.
  - DONE = a line `bruna units rc=<n>` in the log (or `rc=<nonzero>`).
  - My waiter on it (task bwwo1wqz0) keys on the log; it dies with me.
- `build_ro.sh` in the lane, 4 slots, launched as PID 291990, log `build/scratch/bad/build_ro2.log`, DONE = `rc=<n>`.
  It builds bruna-ro (deletes the stale ManaPayment.o / test_bruna.o first; waits 200 s after the ro_edit_ts stamp).

## What remains, in order
1. fix3 built: run all units (`fix3/mtg-test`, 1 lane slot) and the repros
   (`bash scripts/box_lane.sh --slots 3 --cap-gb 2 -- build/scratch/bad/repros2.sh build/scratch/bad/fix3/mtg build/scratch/bad/r_fix3 3`).
   - Expect the same counts as fix12. If s30053757 / s30051079 come back, the CPU cut narrowed fix 2 too far.
2. A/B rerun on fix3 WITH per-game wins: `build/scratch/bad/ab/run3.sh build/scratch/bad/fix3/mtg <threads> <cap> ab3`.
   - Run it under the box lock at 40, or beside a tail at cap 16 if >= 22 GB free; DONE marker `ab/DONE_ab3`.
   - Then `python3 agg2.py ab3.out` (nets, CPU) and `compare.py` adapted to `wins_ab3/<job>.wins` (per-game
     slower list). The arms are always paired by gi.
   - Bar: every deck net <= 0, searched AND d0 (sead0 was +1 on fix12: root-cause that game); CPU back to ~1.00 on
     EDF/Jumbo/Sea. If EDF d0 is still costly, profile which site calls the helper (walks vs executor) and cut further.
3. Every slower game: `python3 ab/slow_replay.py <slower list> 710000 <manifest>`. Then run that manifest in the lane
   (`mtg --batch ... --threads <=4 --game-log-dir ...`, d8 b0, both arms) and check each recovers. CLEAR clairvoyance
   only may stay, verified with MTG_NC_SEARCH=1 + the owning prune off.
4. `MTG_PGO=0 bash test/regression.sh --both` under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`
   with MTG_RSS_CAP_GB=40, from the bruna-apply-drop worktree.
   - Audit every moved key, then `--both --accept` and `python3 test/check_gt_logs.py`.
   - Split c8f363ac6 + 7b621e156 into fix-1 / fix-2 commits and fix the "about half of its 1,891" comment in
     ManaPayment.cpp with the measured -35% (1,891 -> 1,221).
   - Push wip/bruna-apply-drop only.
5. Rule overrides (bruna-ro, after its build): units (the new test), then the repros with MTG_PAY_LINE_CRIT_BEHIND_DORKS=1
   (7a) and MTG_PAY_LINE_CRIT_LAND=1 MTG_PAY_LINE_CRIT_OPENER=1 (7b). Targets: s30051257 / s30051119 for 7a;
   s30051968 / s30051794 for 7b.
   - Then an all-deck measurement. These are all-deck play changes; the old CRIT_LAND cost Cyclone +3.
   - Rebase bruna-ro onto the accepted-GT commit, run `regression.sh --both` once per variant with the env lever on
     (no accept). Then run a held-out A/B on the decks that move.
   - Report numbers; DO NOT adopt (USER decides).

## Brief rulings (verbatim from the agent-3 brief)
- Adoption bar. Every deck must net <= 0 vs the committed tree, both searched AND at d0; there is no "lighter d0
  bar". Every slower game must recover at d8 b0. Only CLEAR clairvoyance (verified with MTG_NC_SEARCH=1 + the owning
  prune off) may stay unrecovered. Cut any CPU cost the fix adds; the suite 3x limit is not a CPU licence.
- Box. One big engine process at a time: gates, tiers and A/Bs run under `flock
  /workspaces/MagicDeckTester/build/scratch/claude_box.lock`, with MTG_RSS_CAP_GB=40. ... If your big run would
  otherwise wait hours, check free -g. With >= 22 GB available you may run it beside that tail at MTG_RSS_CAP_GB=16,
  and say so in your report.
- Small work goes through `scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>`, max 4 slots: a few single games, d8
  b0 replays, units, builds. Never a full tier there. A job given N slots runs at most N processes.
  (Coordinator 20:41: 4-slot jobs need 14 GB and blocked the lane while a gate held ~32 GB; prefer --slots 2 then.)
- Build with ./build.sh only. Wait ~200 s after a src edit before building (9p clock lag) and delete stale .o files.
- Never wrap commands in timeout. Waiters key on PIDs or DONE files, never pgrep -f / pkill -f; use run_in_background
  waiters, never polling turns.
- Never change src in a tree a gate or tier is running from. Never edit a script a running bash is reading.
- Rebase, never merge.
- Never push the shared branch phase-1-2-deck-analyzer; the parent lands it. Push wip/bruna-apply-drop freely.
- References are commit-only: never revert, delete or move anything under references/.
- A USER-owned decision is never settled by you. Report measurements and recommendations; don't adopt the
  rule-override fixes.
- Step 7 (a) Sol Ring's {1} paid off the only colour source instead of Avacyn's Pilgrim: needs creature-before-land
  ranking (breaks the standing rule). (b) Wish/Armory + Sol Ring with the only Forest: needs the line-critical-land
  rule (dropped 11-04) plus tutor line order. "Unless the USER objects (not yet answered; take the default and
  measure), measure a fix for each and report the numbers. Do NOT adopt either; the USER decides."
- Context: hand off past ~250K; end 250-350K, never >400K.

## Gotchas found this session
- `git worktree add <relative path>` from inside a worktree nests it. Use an absolute
  /workspaces/MagicDeckTester/.claude/worktrees/<name> path.
- Batch `--log-dir` = JSON game traces; per-game `<job>.wins` need `--game-log-dir`.
- CounterList has `Add(type, n)`, not push_back.

## Update (after handoff was written)
- fix3 BUILD DONE: build_fix3.log `rc=0`; build/scratch/bad/fix3/{mtg,mtg-test} hold 7b621e156. `mtg-test -tc='Bruna*'`
  49/49 pass. Step 1 now starts at the full unit run + repros on fix3.
