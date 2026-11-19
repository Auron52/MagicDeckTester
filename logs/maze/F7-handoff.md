# F7 handoff: Maze's End s3 completeness hole (second land drop after a mid-turn breakpoint)

Branch `wip/maze-f7`, worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f7`, scratch
`/workspaces/MagicDeckTester/build/scratch/maze/f7/` (F below).

## Mechanism (confirmed in code)
A breakpoint continuation is a Plan, and a Plan carries ONE land drop (`bp_play_searched_land`). The site-10
extra-drop route (`TurnSolver::LandDropNewlyOpen`) arms only at the section window in `ApplyPlanDirect`, ONCE,
before the deferred re-solve loop (`while (deferred_cantrip_resolve && (top_first_pass || deferred_top_rearm ||
partition_loop) ...)`), and every armer inside a continuation is `sink_stack.empty()`-gated. So after a tutor's
site-10 continuation played Maze's End, the drop Exploration still held open was never re-offered: The Black Gate
as the second drop was unreachable at any budget. (The first continuation never offers The Black Gate directly
because MTG_BP_NEW_ONLY drops an old-card land while no land has been played this turn; with NEW_ONLY=0 it
appears, but its own follow-up drop (Maze's End) was dropped by the same missing re-arm, so it was T6 either way.)
Not a provider prune; not `lands_played_this_turn` vs `LandDropsAvailable()` (those are right).

## Done
- Commit `c7af5242a` (rebased onto origin/phase-1-2-deck-analyzer 6bbfe4df1; pre-rebase fcb3f6f67):
  - rollout: at the end of each deferred pass, `LandDropNewlyOpen(drops_at_section, ...)` re-arms site 10 for
    another pass (`deferred_drop_rearm`, like the tutor-to-top re-arm; not sink-gated).
  - executor: `exec_drop_rearm_loop` after the two no-commit end-of-main `resolve_draw_breakpoint(0)` calls.
    Committed lines need nothing (each pass is its own rec_bp_ord segment; the catch-all replays every ordinal).
  - hatch `MTG_BP_DROP_REARM` (heurarm slot BP_DROP_REARM, shared reader in EngineFlags.h), DEFAULT ON, =0 reverts.
    Its output with =0 was byte-identical to the parent binary on the repro.
  - Unit test "Maze's End F7: two land drops AFTER a mid-turn tutor breakpoint" in test/unit/test_mazes_end_i3.cpp:
    FAILS on the parent build (6 != 5) and passes with the fix.
  - Deck-gated by deck_has_static_land_drops: only Exploration has the param, and only Maze's End plays it, so no
    other deck can move. (Smoke/regression in the chain should confirm that.)
  - The nested occurrence is BRANCHED, not a front() pick: FSW trace shows child 0 cont=[Maze's End, The Black Gate]
    and the wave walker's nested slot (EMPTY@1 -> cont=[Maze's End]).

## Measurements
s3 T4 frame-0 repro (`build/scratch/maze/r1/handoff2.py references/Maze_s_End/claude_s3_gi2.json 4 -- ...`):
parent T6 at d2 b0, d5 b20/b100/b500/b2000, d8 b0. Fix: T5 at d2 b0, d5 b100/b500/b2000, d8 b0; d5 b20 still
T6 (budget only, the USER's settings question).

Held-out A/B (one pooled batch, arms innermost, `F/ab1/`, seeds 211001.., mtg.f7 with flag off vs on; loss=9):
| row | games | off | f7 | better/worse | net | units/game off -> f7 | ms/game |
|---|---|---|---|---|---|---|---|
| d5 b20 | 1500 | 5.6620 | 5.5967 | 105/9 | -98 | 135,740 -> 132,902 | 3,910 -> 3,864 |
| 2HG d5 b20 | 400 | 5.5425 | 5.4900 | 22/1 | -21 | 114,163 -> 114,058 | 3,781 -> 4,104 |
| d3 b10 | 400 | 5.6550 | 5.6125 | 17/0 | -17 | 76,319 -> 74,863 | 2,233 -> 2,077 |
| d0 | 500 | 6.9840 | 6.9840 | 0/0 | 0 | 0 | 1 |
Net <= 0 on every row; units flat to slightly lower (no extra cost to fold away, so no P5b proposal needed).
Slower games (10): d5 g11, g506, g645, g673, g675, g889, g1026, g1244, g1430; 2HG g41 (seed = base + gi).

## Queued chain (do NOT kill it)
`flock /workspaces/MagicDeckTester/build/heavy.lock F/chain2.sh`, flock PID 1334989 (parent shell 1334988).
Stages (F/chain2.stage): rebuild the rebased tree -> `F/mtg.f7r` (log F/build_r.log) -> d8 b0 recovery batch
`F/d8/manifest.json` (20 jobs: each slower game, both arms, abandon_units 50M; output F/d8/run.out, DONE F/d8/DONE)
-> full unit suite (F/unit.log, F/unit.DONE) -> `MTG_PGO=0 bash test/regression.sh --both` in the worktree
(F/tiers.log, F/tiers.DONE). Final marker: F/chain2.DONE ("OK" or "BUILDFAIL").

## Remaining, in order
1. d8 recovery: `python3 F/anab.py F/d8/run.out off` (names D8_<tag>_g<gi>_<arm>). Bar: every f7 game reaches a
   win turn <= its off twin at d8 b0, none abandoned (check run.err for abandon). Any that does not: root-cause it
   (CLAUDE.md: "draws diverge" is not an explanation).
2. Unit suite: F/unit.log must show 0 failed.
3. Tiers: only mazesend / mazesend2hg keys may FAIL; any other deck moving is a defect (the change is deck-gated).
   Then `bash test/regression.sh --both --accept --deck=mazesend,mazesend2hg` with a ONE-LINE ACCEPT_ACK note
   (the A/B above), diff test/regression_gt.txt, `python3 test/check_gt_logs.py`, commit, push wip/maze-f7 ONLY.
   Overnight GT is left to the periodic overnight run (say so in the commit).
4. Write logs/maze/F7-results.md and report to the parent: mechanism, commit, A/B table, d8 recovery, GT, head.

## USER rulings in my brief (verbatim as given by the parent)
- "a budget may re-price work, it may not make a line unfindable" (CLAUDE.md completeness invariant).
- Fix "as a searched capability at every budget (no default-off lever; no greedy pick in the search window)".
- "P5b's fold is a USER decision, so propose, don't ship".
- Box rules (USER, mandatory): ONE heavy process at a time via flock build/heavy.lock; MTG_BUILD_JOBS=12;
  batches --threads $(nproc), default cache budget, MTG_RSS_CAP_GB=20; no timeouts; waiters on PID/DONE markers,
  never pgrep -f/pkill -f; references/ read-only; don't touch the running viewer.
- Push wip/maze-f7 only, never the shared branch; rebase, never merge.

## USER decision to surface
- s3 still ends T6 at the shipped d5 b20 (T5 from b100 up): a budget/settings question for the USER, as R1 said of s6.
- MTG_BP_DROP_REARM is default ON (a completeness fix, clean win: better and not more expensive). The hatch can be
  deleted once accepted.

## F7-2 result (2026-11-19): ACCEPT BLOCKED. Two slower games do NOT recover at d8 b0
d8 b0 recovery batch (`F/d8/run.out`, mtg.f7, the arms differ only in MTG_BP_DROP_REARM; loss=9):
| game | off | f7 | verdict |
|---|---|---|---|
| d5 g11 | 6 | 6 | recovers (tie) |
| d5 g506 | 6 | 5 | better |
| d5 g645 | 6 | 6 | recovers |
| d5 g673 | 5 | 5 | recovers |
| d5 g675 | 5 | 5 | recovers |
| d5 g889 | 6 | 5 | better |
| d5 g1026 | 6 | **7** | **NOT RECOVERED** |
| d5 g1244 | 6 | 5 | better |
| d5 g1430 | 5 | 5 | recovers |
| 2HG g41 | 6 | **7** | **NOT RECOVERED** (also 120-311 s wall on 44K units) |

ROOT CAUSE (lever flip plus MTG_FD_TRACE on single-game repros under `F/rc/`). It is a plan/executor
divergence introduced by the fix, not draws. In BOTH arms the T1 committed line is `win=6 verified=1
searched_depth=6`. With the fix on, that line carries the F7 re-armed passes, and the executor does not
realise it.
- g1026 T6 plan: `bp[Exploration, Golgari Guildgate, Exploration, Citadel Gate, Maze's End(k30)]` (record
  order = rollout apply order). The executor's per-ordinal replay plays `ord0 = Exploration, Citadel Gate,
  Maze's End`, then `ord1 = Golgari Guildgate, Exploration`. Citadel takes the drop Exploration #1 opened,
  so Golgari has no drop left. It is discarded at cleanup, one Gate name is short, and the game ends T7.
- 2HG g41 T6: the same shape. The trailing `k4:Maze's End, k30:Maze's End` records replay inside ord0,
  ahead of segments 1 and 2.
- Mechanism: `bp_sink_pop` stamps rec_bp_ord only on records appended while a main-level segment is
  OPEN. The records the rollout appends after the re-armed passes (the last continuation's land and
  activations) keep the default 0, so `replay_segment` puts them in the FIRST segment. The handoff's
  claim "committed lines need nothing; the catch-all replays every ordinal" is therefore false once the
  loop runs more than one pass. Next step: print rec_bp_ord in FdPlanText to find the append site, stamp
  those records with the ordinal of the pass that made them (the DigDraw site already does `main_bp_ord +
  1`), rebuild, re-run `F/d8/manifest.json` and the held-out A/B.
- Repro: `mtg.f7 "decks/Maze's End/Maze's End.cod" --profile ... --seed 212027 --game-index 1026 --games 1
  --depth 8 --budget-ms 0 --ignore-play-profile --threads 1` with MTG_FD_TRACE=1 (MTG_BP_DROP_REARM=0 -> 6).
  For g41, use the 1-job batch `F/rc/g41.json`.

Unit suite (rebased mtg-test): 3714 test cases, 0 failed.

Per the brief, NO accept was made. chain2 (flock 1334989) was left running into units + tiers; its
results are in F/unit.log and F/tiers.log, with F/chain2.DONE when it finishes.

Reference bench (`scripts/ref_bench.py --deck maze_s_end`, mtg.f7r, committed policy = d5 b20): 11/11
tied, 0 short. s3 records win_turn 6 and the search ties it. See logs/maze/F7-2-s3.md.

## Chain outcome (the parent killed chain2 at the tiers stage: "superseded (rec_bp_ord defect)")
- Rebuilt binary: F/mtg.f7r (rebased tree). Full unit suite on it: 3714 passed, 0 failed.
- d8 b0 recovery (F/d8/run.out; strip the `D8_` prefix before anab.py): 2 of 10 slower games do NOT recover --
  d5 g1026 (seed 211001+1026): off T6, f7 T7 at d8 b0; 2HG g41 (seed 212001+41): off T6, f7 T7 at d8 b0
  (the 2HG game also ran 120 s, 44,513 units). Recovered: g506 6->5, g889 6->5, g1244 6->5; the rest equal.
  Since these are worse at unlimited budget, they are a real defect (a scored-vs-executed mismatch is the
  first suspect: e.g. the rec_bp_ord segment replay the parent flagged), not budget churn. Not yet root-caused.
- Tiers: not run.

## F7-3 (2026-11-19): the rec_bp_ord defect is a PREFIX-RESUME numbering bug
- Real mechanism (code-read, then confirmed by the d8 rerun below): the wave loop captures a variant at its varied
  breakpoint (`bp_capture`, top of the deferred loop, `bp_seen == plan.bp_at`) and resumes the later ranks from it.
  The resume restored `*out_breakpoint = bp_resume->sink` (records already stamped 0, 1, ...) but NOT `main_bp_ord`,
  so the resumed pass's push opened ordinal 0 again. g1026's committed T6 line was `bp_choice=2@2` (the second F7
  re-armed pass): its Citadel Gate + Maze's End records were stamped 0 and replayed inside segment 0, ahead of
  segment 1 (Golgari Guildgate + Exploration). Not the "unstamped trailing records" F7-2 guessed: every record site
  is inside a push/pop.
- Fix: `BpPrefixSnap::main_bp_ord`, captured at both capture sites and restored on resume. General (any deck whose
  resumed variant follows an earlier main-level segment), not deck-gated, no hatch: a lockstep correctness fix.
- `FdPlanText` now prints a record's ordinal (`k4@2:Citadel Gate`) under MTG_FD_TRACE.
- Unit test "Maze's End F7-3: a prefix-resumed apply records the main-level segments the full apply records"
  (hook `TurnSolver::BpResumeRecordsForTest`).
- Chain (scripts/heavy_run.sh, MTG_HEAVY_TAIL_SAMPLES=40): `build/scratch/maze/f73/chain.sh` -- build -> units ->
  b1 (the 10 d8 games + the F7-shape A/B, one pool) -> b2 (any NEW slower game at d8 b0) -> check.py gate -> tiers
  (only on PASS, no accept). Markers: f73/stage, f73/DONE, f73/gate.txt, f73/tiers.DONE.
- 15:3x UTC: the first chain built, and the new unit test FAILED (at=1 pre=1 ch=2: the full apply had 2 records,
  the resume had 1). Second omission in the same resume: `deferred_put_armed` (site 10) was neither captured nor
  restored, so a resumed site-10 pass re-solved as plain-cantrip site 3 and recorded a different continuation.
  trick_armed/equip_armed were carried for exactly this reason; put_armed was missed. Now carried
  (BpPrefixSnap::put_armed). This is pre-existing for every deck whose wave loop resumes a site-10 variant (the
  put-in-hand / hand-entry tutors), so other decks MAY move in the tiers. Chain relaunched (try1/ holds the first
  build log); it now runs `mtg-test -tc="Maze's End F7*"` first and stops with DONE=UNITFAIL on failure.
- ~17:15 UTC try2: F7 unit test failed again, differently: full `PlayLand Maze's End@0, ActivatePermAbility Maze's
  End@1`, resumed `PlayLand Maze's End@0` only. Third omission: the apply's ENTRY-time keys (`pre_plan_acts`,
  MTG_BP_NEW_ONLY's "the base plan could have carried it", and `pre_plan_keys`, site 9's) were recomputed from the
  SNAPSHOT state on resume, so the Maze's End the prefix had played read as pre-existing and NEW_ONLY dropped its
  activation. Both now carried in BpPrefixSnap. try3 relaunched.

## F7-4 (2026-11-19, checkpoint)
- try 3 (a0621afba + b19044588, the deferred loop's pass counter also carried): F7 units PASS, full units rc=0.
  build/scratch/maze/f73/gate.txt PASS: all 10 d8 b0 games tie or beat off (g506, g889, g1244 better; g1026 and 2HG
  g41 now tie). F7-shape held-out A/B (both arms carry the resume fixes): mazesend-d5 net -104 (107/6), 2HG -22,
  d3 -19, d0 0. The 6 slower A/B games are the same seed/gi as six of the ten d8 games, all recovered.
- ref_bench maze_s_end (mtg.f73, d5 b20 policy): 11/11 tie or beat the USER (s8 faster by 1).
- Tiers (no accept; f73/tiers.log): Maze improves (smoke d5 5.75->5.68, reg d5 s2002 5.69->5.60, s3003 5.69->5.59);
  2 slower Maze games (reg d5 s2002 gi16, gi120: 5->6). OTHER DECKS MOVED, digest only (no aggregate change):
  snow, niv, niv2hg, martyr, cyclone, edf, landfall.
- Hatch MTG_BP_RESUME_LOCKSTEP (7f4a11b9d, default ON) so both arms fit one pooled batch. Chain
  build/scratch/maze/f74/chain.sh (stage/DONE there): units, d8 b0 on gi16/gi120 vs the committed tree (both
  hatches off), tier-key replicas (hatch check), held-out A/B on overnight seeds for every moved deck + Maze vs the
  committed tree, then d8 b0 on every new slower game; gate f74/gate.txt.
- RESULT (f74/gate.txt, PASS). Units 3715/3715 from build/Release (f74/unit2.log; the chain's unit.log failed 9 Sea
  "Sin dies" cases only because the COPIED mtg-test walks up from its own path to the PRIMARY's older cards.json --
  an artifact; engine batches read cards.json from the cwd and heuristic_defaults.env is identical).
  Hatch check: old-arm tier-key replicas reproduce GT exp digests, new-arm the tier's got.
  d8 b0 vs the COMMITTED tree (old = MTG_BP_RESUME_LOCKSTEP=0 + MTG_BP_DROP_REARM=0): reg s2002 gi16 6->5, gi120 6=6;
  A/B-slower g11 6=6, g506 6->5, g673 5=5, g675 5=5, g889 6->5, g1430 5=5.
  Held-out A/B (overnight seeds 4004/5005, arms innermost, same_seed_inflight 0), net new-old: snow 0 (900 g),
  niv+niv2hg 0 (1375), martyr+2hg 0 (800), cyclone 0 (1050), edf 0 (800), landfall 0 (925) -- no game better or
  worse; Maze vs committed -146 (d5 -105 108/6, 2HG -22, d3 -19).
  ref_bench maze_s_end (mtg.f73, d5 b20): 11/11 tie or beat, s8 faster.
  ACCEPTED smoke+regression, --deck=mazesend,mazesend2hg,snow,niv,niv2hg,martyr,cyclone,edf,landfall (21 keys,
  exactly the moved ones); check_gt_logs: 1316 consistent, 0 stale. Overnight GT left for the periodic run.
