# F7-3 handoff: the d8 b0 regressions (g1026, 2HG g41) are prefix-RESUME lockstep bugs

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f7`, branch `wip/maze-f7`. The scratch directory is
`/workspaces/MagicDeckTester/build/scratch/maze/f73/` (H below). The F7 history is in `logs/maze/F7-handoff.md`; its
"F7-3" section is the running checkpoint.

## Diagnosis
F7-2's guess was "steps recorded after the re-armed passes keep ord 0". That is NOT the mechanism: every top-level
record site is inside a `bp_sink_push`/`bp_sink_pop` pair.

The real mechanism is the wave loop's **prefix-resume cache** (FSLine `prefix_cache`, TurnSolver.cpp ~119057, and
the other two hosts). A variant is captured at its varied breakpoint: the deferred loop's top, `bp_seen ==
plan.bp_at`, in `bp_capture`. The later ranks are then RESUMED from that snapshot (`ApplyPlanDirect(..., &bp,
nullptr, &snap)`). The resume did not reproduce the tail of the full apply. Since the committed line's
`breakpoint_actions` can come from a resumed apply, three things followed:
- the scored line could differ from the full apply;
- the recorded line could differ from the full apply;
- the executor's replay could differ from both.

F7's re-arm makes the varied breakpoint a LATER pass very often (g1026's T6 line was `bp_choice=2@2`), so it exposed
the bugs. But all three are pre-existing omissions, not Maze-specific.

## The three tries (all in the working tree; not yet committed when this file was written; see Commits)
1. **try1: `main_bp_ord` was not carried.** The resume restored `*out_breakpoint = bp_resume->sink` (records
   already stamped 0, 1, ...) but restarted `main_bp_ord` at -1. The resumed pass therefore stamped ordinal 0 again,
   and the executor replayed it inside segment 0, ahead of segment 1. In g1026, Citadel Gate went ahead of Golgari
   Guildgate and took its drop.
   - Fix: `BpPrefixSnap::main_bp_ord` (TurnSolver.cpp:69988). It is captured at :79993 and :80038 and restored at
     :79421.
   - Diagnostic: `FdPlanText` now prints `k4@2:Card` (AIEngine.cpp:582).
   - Test hook: `TurnSolver::BpResumeRecordsForTest` (TurnSolver.h:3253, TurnSolver.cpp:93717).
   - Unit test: "Maze's End F7-3: a prefix-resumed apply records the main-level segments the full apply records"
     (test/unit/test_mazes_end_i3.cpp:384). It compares the full apply's record list (kind:card@ord) with the
     resumed apply's, for every capture over bp_at 0..2, bp_pre, and ch 0..3. It also requires >= 1 capture after
     an earlier segment, so it cannot pass vacuously.
   - Why try1 was insufficient: the new test FAILED (try1/; at=1 pre=1 ch=2: full had 2 records, resumed had 1).
2. **try2: `deferred_put_armed` (site 10) was not carried.** `trick_armed` and `equip_armed` were carried "so the
   resumed tail re-solves at the SAME site index", but `put_armed` was missed. A resumed site-10 pass (put-in-hand
   or hand-entry tutors, F7 extra-drop re-arm) therefore re-solved as site 3.
   - Fix: `BpPrefixSnap::put_armed` (:69963). It is captured at :79973 and :80018 and restored at :79396.
   - Why try2 was insufficient: the test still FAILED (try2/unit_f7.log). Full: `4:Maze's End@0 30:Maze's End@1`;
     resumed: `4:Maze's End@0` only.
3. **try3 (queued): the ENTRY-time keys were recomputed from the snapshot state.** `pre_plan_acts` is
   MTG_BP_NEW_ONLY's "the base plan could have carried this activation"; `pre_plan_keys` is site 9's. On a resume
   both were computed from the SNAPSHOT, so the Maze's End the prefix had played read as pre-existing, and NEW_ONLY
   dropped its activation.
   - Fix: both are carried in BpPrefixSnap and used on resume (:71721, :71753). They are captured next to
     `main_bp_ord`.
   - Not carried (judged irrelevant): `LineReducerCredit` (0 unless MTG_LINE_REDUCER_KEEP), `canon_opens_pre`
     (audit only), and `newonly_pin` (deck-constant). If try3 still fails the test, look next at the other
     entry-time or main-block values that the deferred loop reads.
- All three are general correctness fixes. They are not deck-gated and have no hatch. They are pre-existing for any
  deck whose wave loop resumes a site-10 variant, or a variant after an earlier main-level segment, so **OTHER
  DECKS MAY MOVE in the tiers.** Per the brief: if they do, do not accept. Report which decks moved and how.

## Commits
- The F7-3 WIP commit on wip/maze-f7 (pushed) holds all of the above plus this file. Its hash is in the commit log
  (`git log -1 wip/maze-f7`). It is not yet validated: try3 had not yet built when this file was written.

## Running chain (try3)
- Launched with `MTG_HEAVY_TAIL_SAMPLES=40 scripts/heavy_run.sh H/chain.sh`. The heavy_run PID is **1671414**
  (parent shell 1671412). It was QUEUED on build/heavy.lock at ~17:20 UTC (~10 waiters; the earlier tries waited
  ~1.5-3 h).
- Stages, written to `H/stage`:
  1. build (H/build.log), then copy to H/mtg.f73 and H/mtg-test.f73.
  2. `mtg-test -tc="Maze's End F7*"` (H/unit_f7.log). On failure it writes `H/DONE = "UNITFAIL F7"` and stops.
  3. Full units (H/unit.log, H/unit.DONE).
  4. b1 = the 10 d8 b0 games (both arms, first) + the F7-shape held-out A/B in ONE pool. Arms: off =
     `MTG_BP_DROP_REARM=0`, f7 = default. Rows: d5 b20 1500 g (seeds 211001+gi), 2HG d5 b20 400 (212001+gi), d3
     b10 400 (213001+gi), d0 500 (214001+gi). Output H/b1.out / b1.err / b1.DONE.
  5. b2 = any NEW slower A/B game at d8 b0 (H/b2.json, H/b2.out).
  6. Gate: `H/check.py b1.out [b2.out]` writes H/gate.txt. Exit 0 = PASS (every d8 f7 game <= its off twin, both
     played; every A/B tag net <= 0; units rc=0).
  7. Only on PASS: `MTG_PGO=0 bash test/regression.sh --both` (H/tiers.log, H/tiers.DONE). It does NOT accept.
- Final marker `H/DONE`: one of `OK`, `BUILDFAIL`, `UNITFAIL F7`, `GATEFAIL <rc>`, `MK1FAIL`, `MK2FAIL`.
- try1/ and try2/ hold the earlier build and unit logs.
- Caveat for the A/B: both arms run the try3 binary, so the resume fixes are in BOTH arms. The off arm is not the
  committed tree. For "vs the committed tree", use the tiers (GT).

## Remaining, in order
1. Wait for H/DONE (waiter on PID 1671414 or the DONE file; never `pgrep -f`). If it says UNITFAIL F7, read
   H/unit_f7.log: the test prints both record lists. Fix the next omission and relaunch the same way.
2. d8 recovery of the 10 slower games (d5 g11, g506, g645, g673, g675, g889, g1026, g1244, g1430; 2HG g41). Every f7
   game must tie or beat off. See H/gate.txt. Any game that does not: root-cause it; "draws diverge" is not an
   explanation. Repro command: `mtg.f73 "decks/Maze's End/Maze's End.cod" --profile ... --seed 212027
   --game-index 1026 --games 1 --depth 8 --budget-ms 0 --ignore-play-profile --threads 1`, with `MTG_FD_TRACE=1`.
3. Held-out A/B: per-deck net <= 0 (H/gate.txt), and every NEW slower game recovers at d8 b0 (b2).
4. Tiers: if only `mazesend*` keys move, run `bash test/regression.sh --both --accept --deck=mazesend,mazesend2hg`
   with a ONE-LINE ACCEPT_ACK, then `python3 test/check_gt_logs.py`. If OTHER decks move (possible, see above), do
   NOT accept. Report the decks and their deltas.
5. `scripts/ref_bench.py --deck maze_s_end` at the shipped d5 b20 on H/mtg.f73 (one thread, outside the lock). All
   11 references must tie or beat the USER (F7-2 measured 11/11 tied).
6. Commit and push wip/maze-f7 only; the parent pushes the shared branch. Write the results into this file and into
   F7-handoff.md.

## USER rulings (verbatim, as given in my brief)
- "the top priority are optimizations and fixes"
- "Naturally, I'm expecting that this means we are matching the references and all other bars. If not, we should
  focus on doing so first." (This gates the Maze's End mulligan profile.)
- Adoption bar: per-deck net <= 0 on a large held-out sample, plus every slower game recovering at d8 b0. "+1 is
  not neutral. 0 is neutral."
- No unbudgeted-only changes. No timeouts. Waiters key on a PID or a DONE marker, never `pgrep -f`. One heavy job at a
  time via `scripts/heavy_run.sh` (build/heavy.lock), at full nproc threads, with MTG_RSS_CAP_GB=20 and
  MTG_BUILD_JOBS=12; single-thread single-game probes run outside the lock.
- Push wip/maze-f7 only; never push the shared branch. Rebase, never merge.
- F7's brief (verbatim): "a budget may re-price work, it may not make a line unfindable"; fix "as a searched
  capability at every budget (no default-off lever; no greedy pick in the search window)"; "P5b's fold is a USER
  decision, so propose, don't ship".

## UPDATE: try3 chain FINISHED (H/DONE = OK) right after the handoff was written
- **Units:** `Maze's End F7*` passed, including the new F7-3 resume test. The full suite returned `unit rc=0`.
- **d8 b0 recovery: all 10 recovered** (H/gate.txt; off -> f7):
  - better: g506 6->5, g889 6->5, g1244 6->5
  - tie: g11 6, g645 6, g673 5, g675 5, g1430 5, **g1026 6** (was 7), **2HG g41 6** (was 7)
- **Held-out A/B** (one pool, arms innermost; gate PASS). No new slower game at d8 (b2 empty or passed).

  | row | games | off avg | f7 avg | better / worse | net | units/game (off -> f7) |
  |---|---|---|---|---|---|---|
  | d5 b20 | 1500 | 5.6613 | 5.5920 | 107 / 6 | -104 | 135,810 -> 132,974 |
  | 2HG d5 b20 | 400 | 5.5425 | 5.4875 | 22 / 0 | -22 | 114,180 -> 114,075 |
  | d3 b10 | 400 | 5.6550 | 5.6075 | 19 / 0 | -19 | 76,339 -> 74,838 |
  | d0 | 500 | 6.9840 | 6.9840 | 0 / 0 | 0 | 0 -> 0 |

- **Tiers** (H/tiers.log; `tiers rc=1`, REGRESSION DETECTED; 357 passed, 11 failed): **NOT ACCEPTED**, because OTHER
  decks moved.
  - The other decks moved DIGEST ONLY, with an unchanged aggregate:
    - snow smoke d3/d5, snow regression d5 s2002/s3003
    - niv smoke d3/d5, niv2hg smoke d5, niv regression d5 s3003
    - martyr smoke d3/d5
    - cyclone regression d5 s2002
    - edf regression d3 s2002
    - landfall regression d5 s2002
  - This is the expected footprint of the general resume fixes (try2/try3).
  - mazesend: smoke d5 5.7500 -> 5.6818; regression d5 s2002 5.6902 -> 5.6033; regression d5 s3003 5.6933 -> 5.5933.
    The other mazesend keys and mazesend2hg moved digest only.
  - The audit counts searched slower=2 (both mazesend: regression d5 s2002 gi16 5->6 and gi120 5->6), faster=31, and
    play-changed=114.
- **Still owed:**
  - the d8 b0 check of mazesend_regression_d5_s2002 gi16/gi120 (seed 2002+gi);
  - the d8 b0 check of the other decks' play-changed games, which is needed for the accept call. Accepting those
    decks needs the parent/USER, since they moved;
  - the ref_bench at d5 b20 on H/mtg.f73;
  - commit/push and the GT accept, after that decision.

## CLOSED by F7-4 (see F7-handoff.md "F7-4"): every owed item above done; GT accepted for the 21 moved keys.
