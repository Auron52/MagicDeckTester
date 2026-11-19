# F5-5 handoff (F5-5 -> next agent), 2026-11-19 ~13:30 UTC

Read this, then `logs/maze/F5d-handoff.md` (in this tree) for older history. Read files by grep + ranges only.
Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/f5/` (tools/) and `.../maze/f5p/`.
Box rules: ONE heavy process at a time (`flock /workspaces/MagicDeckTester/build/heavy.lock` or
`scripts/heavy_run.sh`, which hands the lock on during a tail); `MTG_BUILD_JOBS=12 ./build.sh`; batches
`--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; no timeouts; waiters on PID / DONE markers only
(`tools/waitany.sh <markers...>` in f5/, 2 h cap, re-arm); push `wip/*` only (the parent pushes the shared branch);
rebase never merge; never touch `references/` or the viewer; hand off at ~250K.

## Branches / commits
- `wip/maze-f5` (worktree `.claude/worktrees/maze-f5`): 867111d63 heapstrict leak fix, 321c48193 joint pay,
  7641435f7 refine lever stripped; handoff-only commits 7a59f73dc, 4303076a6, 75e19ad96 (NOT yet pushed; push
  `wip/maze-f5` with this file). Windows CI for 7a59f73dc (run 44468086853): GREEN.
- `wip/maze-f5p` (worktree `.claude/worktrees/maze-f5p`): 627fccdd1 prune WIP, **b0f3c8228** compiles (pushed; CI run
  44484701153 GREEN, full unit suite). **UNCOMMITTED in the worktree** (being built as b2, see below): the two soundness
  fixes + trace + 2 unit tests -- commit them once b2's units pass:
  1. a land drop with a PINNED as-enters choice (`LandHasEntryPinChoice`: Thran Portal / Manor Gate colour, Gateway
     Plaza pay-or-sac) is NOT placed on the optimistic board (the plan view carries no `rad_mode` pin, so EnterLand took
     the provider default colour) -- it is credited as `land_credit` any-colour supply instead. Repro: abp d3
     s202073 gi72 T1 "Thran Portal + Burgeoning" was dropped (base T4 win, prune T5).
  2. multi-Amulet float: `land_credit` += Amulets (controlled or cast by the plan); a PLACED drop gets `drop_wild` =
     Amulets. (The engine floats a tapped-entering land under k Amulets; the prune counted one tap.)
  3. `MTG_UNPAYABLE_PRUNE_TRACE=1` (print-only, default off): `[unpay-drop] seed= turn= phase= land= acts:` per drop.
  Tests appended to `test/unit/test_mazes_end_f5.cpp`: "a land drop with a PINNED as-enters choice ..." and "under
  Amulets of Vigor a tapped land drop floats".
  Also committed in b0f3c8228: P5 test lever-off check reads `MazeHeapFilterLive` (the filter is now also live
  through the prune); deck-property memo keyed on (card number, interned name pointer).
- `wip/maze-f5p7` (worktree `.claude/worktrees/maze-f5p7`, LOCAL only, not pushed): f5p b0f3c8228 + P7's src commits
  cherry-picked (1b374186f 37b86db2a 78197a557 fc204f2f8 edd113f3b 7eaf3a12f; conflicts resolved: levers kept both,
  F5 then P7; in EnumeratePlansWithLandUncached `ApplyProviderPlanListFilter` FIRST, then the CastDominanceFold block,
  with a comment explaining why) + 76db18d62 composition unit test. Built (binary f5p/mtg.p7i): **all P7 tests and all
  P5/F5 tests pass with the prune on**; ONLY the new composition test fails at `REQUIRE(super)` (line ~613): the
  enumerator does NOT offer the colour-unpayable superset {Burgeoning, Manabond} on Forest + Azorius Guildgate, so the
  fixture never exhibits the hazard. Needs a fixture the enumerator really over-affords: take one from the b2 trace
  (`f5p/tr2/run.err`, `[unpay-drop]` lines) -- a dropped plan P' = P + one of Burgeoning/Manabond/Exploration/Amulet.
  Then rebase f5p7 onto the committed f5p fixes.

## Composition with P7's cast-dominance fold (RESOLVED by ordering; test pending a real fixture)
P7 (wip/maze-p7, NOT yet in F5's history) calls its fold right after the plan-list filter in
`EnumeratePlansWithLandUncached` (the only call site; main-2 has no fold). F5 moved the filter into
`ApplyProviderPlanListFilter` (both hosts); the prune runs inside `MazesEndProvider::PlanListAdmit` FIRST. So in any
integrated tree the prune removes an unpayable P' BEFORE the fold looks for twins: the fold can never drop P for an
unpayable P'. Whoever integrates F5 + P7 must keep that order (the f5p7 resolution does, with a comment naming the
test). The reverse (prune dropping a smaller P while P' survives) loses nothing: P is truly unpayable.

## Results so far
- Gate verify (gm/r2, mtg.f5g): **0 GATE-MISS** (r1 had 314), rc 0, no heap-leak traces.
- d8 b0 (abandon 50M), joint fix vs base: all 6 slower games recover (d5 g164 6=6, g173 6=6, g193 5=5; d0 g297 6=6,
  g306 6=6, g473 6=6). Heap fix new vs old (mtg.f5d): d3 g47 6=6, g110 4=4. **Every A/B slower game recovers.**
- Cold wall (`cold/`): KILLED by F5-5 after base+fix (430 games each): per the coordinator's AB-ORDER finding the wall
  is confounded by arm order / a shared box; judge on UNITS. If wall is still wanted: arms innermost,
  `same_seed_inflight` 0 (or >= arms), read with `scripts/ab_wall.py <manifest> <stdout>` (wip/ab-order a25cd46d6).
- **Tiers (smoke+regression pooled, mtg.f5h = maze-f5 7641435f7, MTG_PGO=0)**: `build/scratch/maze/f5/tiers.log`,
  results in maze-f5's `test/results/{smoke,regression}.env` + `test/logs/*/wins` (do NOT run another tier in that
  tree before accepting). Moved keys: mazesend (smoke d0/d3/d5, reg d0/d3/d5 s2002/s3003), mazesend2hg smoke d5,
  and -- the joint pay is engine-wide -- kithkin (smoke d0/d3/d5, reg d0/d3/d5 s2002/s3003), selesnyav2 smoke d0,
  bruna smoke d0, edf smoke+reg d0, merfolkturns reg d5 s2002. Per key (tools/wdiff.py, run from maze-f5): non-Maze
  all faster-or-equal, 0 slower (kithkin net -1/-2/-3/-1/-1; others 0 or -1). Maze: searched slower = 1
  (mazesend_smoke_d5 gi55 5->6), d0 slower 12 (smoke gi25 234 413 552 650; reg gi76 376 440 624 671 815 947), d0
  faster 26 + 41.
- RUNNING `tools/hx.sh` (PID 1497315; marker `f5/hx/DONE`): (1) `hx/new.json` on mtg.f5h = d8 b0 new arm of those 13
  slower games + a HELD-OUT joint A/B (fix vs base `MTG_OWED_ACTS_JOINT_PAY=0`, arms innermost) on the non-Maze movers:
  kithkin d0 x1000 / d5 x300 / d3 x100, selesnyav2 / bruna / edf / merfolkturns d0 x1000 + d5 x80/80/60/100 (tags kk0
  kk5 kk3 sl0 sl5 br0 br5 ed0 ed5 mf0 mf5; generator tools/mkhx.py); (2) `hx/old.json` on mtg.f5d (joint off = the GT
  engine) = d8 b0 old arm + `tier*` replays at tier settings (check their digests equal GT's `.wins` -- proves f5d is a
  valid reference). Read: `python3 tools/anab.py hx/new.out base` (A/B), d8 = `d8*_new` vs `d8*_old` (recovers if new
  <= old turn).

## Prune A/B round 1 (b1 = b0f3c8228, f5p/abp/run.out; 2060 jobs): FAILS the bar
d5 0 (1/1), d3 **+5 (0 better / 5 worse)**, 2HG -1 (1/0), d0 identical (600/600 digests) => Maze net +4. Units/game equal
(budget-bound). Changed games: d3 g116 g18 g56 g72 g89, d5 g145 (better) g161, hg g54 (better). g72 root-caused (fix 1
above). g56: T4 base "Sylvan Scrying -> Maze's End drop -> Maze act (Amulet x2 untaps)" vs prune "Cliffgate; Scrying":
not yet attributed (maybe the Amulet float, fix 2, or budget shift) -- check with the trace.
RUNNING `f5p/chain_b2.sh` (PID 1499982; marker `f5p/chain_b2.DONE`, "rc 0" = all ok): build f5p working tree ->
`f5p/mtg.b2`, Maze units (`f5p/units.b2.log`), traced replay of the 8 changed games (`f5p/tr2/run.{out,err}`), then the
full A/B again (`f5p/abp2/run.out`, `abp2/DONE`). If the build or units fail the chain stops (see the logs).

## Remaining, in order
1. hx: A/B per non-Maze deck must be net <= 0; d8: every one of the 13 slower games new <= old; tier replays match
   GT. Root-cause any failure ("draws diverge" is not an explanation).
2. If all pass: accept GT in maze-f5 `--deck=`-scoped to the moved decks (mazesend, mazesend2hg, kithkin, selesnyav2,
   bruna, edf, merfolkturns) for smoke and regression (read `.claude/skills/regression-testing.md` accept flow; check
   the diff), then `python3 test/check_gt_logs.py`; Maze's End mismatch gate (verify_deck).
3. Ledger: append to the F5 section of `docs/design/analysis-Maze's End.md` (ends ~line 4273): heap-leak root cause
   (3 parts, 867111d63), joint pay numbers (ab3/ab4 Maze net -21; 321c48193), d8 recovery, gate verify 0, the
   arm-order wall finding (no wall verdict), the non-Maze movers + their held-out A/B, both USER rulings, refine lever
   stripped (patch logs/maze/owed-joint-refine-lever.patch). Commit, push `wip/maze-f5`, report the integrable commit.
4. Prune: commit the b2 fixes once units pass; read abp2 (`tools/anab.py abp2/run.out base`) -- bar: net <= 0 per
   deck AND every slower game recovers at d8 b0 (pool the d8s into one batch via heavy_run.sh). Use the trace to
   attribute every remaining slower game (a drop of a plan the engine CAN pay = a prune unsoundness: fix it).
   Rebase f5p onto the landed F5 head; fix the composition fixture in f5p7 and rebase it; tiers + scoped accept.

## USER rulings (VERBATIM)
- "For Guild Summit I would prioritize the cards over the treasure here." (no reservation; nothing to implement)
- "Unpayable plans I think we could try. I assume all of the plans die at some point anyway, so the only question is
  whether we can stop them early when they are causing issues. For simple decks with no rituals or equivalent (cards
  that add more mana then they cost) like this one it makes sense to drop plans that cannot pay a subset."
- Heapstrict: "Honestly the rule is even fine outside of goldfish, so let's turn it on. We don't have interaction of
  our own to cast in response even if the opponent does something."
- `MTG_OWED_JOINT_REFINE` skip: a USER decision (surfaced; refine stays ON; lever stripped, patch kept).
- Open USER questions: that lever; the pooled-A/B arm-order wall bias (now explained by AB-ORDER).
