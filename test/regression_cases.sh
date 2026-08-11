# Shared test matrix for the regression tester. Sourced by regression.sh for both
# running (compare against ground truth) and --accept (promote results to ground
# truth), so the matrix has a single source of truth. Data only -- no execution.
#
# A case is five whitespace-separated fields:  deck depth seed games budget
#   deck    key into DECK_FILE/DECK_PROF below
#   depth   lookahead depth (0 = heuristic only, no search)
#   seed    base RNG seed (disjoint across modes on purpose)
#   games   number of games
#   budget  per-decision virtual-ms search budget (ignored at depth 0)
# Lookahead bottoming is derived from depth by the engine (ON iff depth>0); no flag.
#
# Time budgets (shared across ALL decks; trim cheaper decks when adding new ones):
#   smoke      < 15 min   regression < 45 min   overnight < 8 h
# See test/TIMINGS.md for the measured per-case costs these counts are sized from.

# Per-deck folder layout (docs/design/per-deck-folder-layout.md): each deck's decklist,
# profile, and sibling models live in decks/<name>/. The engine resolves sibling artifacts
# (value/eval/constraints/keepmodel.exhaustive) directory-relative off the profile path.
declare -A DECK_FILE=(
  [slivers]=decks/slivers_vial/slivers_vial.txt
  [burn]=decks/burn/burn.txt
  [th]=decks/treasure_hunt/treasure_hunt.txt
  [knights]=decks/Knights/Knights.cod
  [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod
  [hinata]=decks/Hinata2/Hinata2.cod
  [dragonstorm]=decks/Dragonstorm/Dragonstorm.cod
  [auras]=decks/Auras/Auras.cod
  [goblins]=decks/Goblins/Goblins.cod
  [creature_giving]="decks/Creature Giving/Creature Giving.cod"
  [mirrorwing]="decks/Mirrorwing Dragon/Mirrorwing Dragon.cod"
)
declare -A DECK_PROF=(
  [slivers]=decks/slivers_vial/slivers_vial.profile.json
  [burn]=decks/burn/burn.profile.json
  [th]=decks/treasure_hunt/treasure_hunt.profile.json
  [knights]=decks/Knights/Knights.profile.json
  [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json
  [hinata]=decks/Hinata2/Hinata2.profile.json
  [dragonstorm]=decks/Dragonstorm/Dragonstorm.profile.json
  [auras]=decks/Auras/Auras.profile.json
  [goblins]=decks/Goblins/Goblins.profile.json
  [creature_giving]="decks/Creature Giving/Creature Giving.profile.json"
  [mirrorwing]="decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"
)

# Seeds:  smoke=1001  regression=2002,3003  overnight=4004,5005,6006,7007
# (counts sized from measured timings -- see test/TIMINGS.md)

# smoke: ~3 min single-seed gate -- d0 full + small d3/d5 (deep-search crash check).
# At NODES_PER_VIRTUAL_MS=900 (rebased 90->900, 2026-06-20) the GATE modes run every deck at
# d3=10/d5=20 = 9000/18000 units = the old NPV=90 budget-100/200 level => BYTE-IDENTICAL to the
# pre-rebase GT (only antilife is new). The search is converged here, so a bigger gate budget
# buys ~nothing and is non-monotonic (th budget 40 turned game s3003/gi104 from a turn-5 win
# into turn 11 -- legal but volatile), and slivers actively spins on its heavy tail. The
# deeper/generous budgets live in OVERNIGHT instead. See search-perf-investigation memory.
SMOKE_CASES=(
  "slivers 0 1001 1000 0"
  "slivers 3 1001  250 10"
  "slivers 5 1001  150 20"
  "burn    0 1001 1000 0"
  "burn    3 1001  300 10"
  "burn    5 1001  250 20"
  "th      0 1001 1000 0"
  "th      3 1001  150 10"
  "th      5 1001   75 20"
  "knights 0 1001 1000 0"
  "knights 3 1001  250 10"
  "knights 5 1001  150 20"
  "antilife 0 1001 1000 0"
  "antilife 3 1001  250 10"
  "antilife 5 1001  150 20"
  # hinata: d0 full + a small d3/d5 gate. The max-mana backtracker gate (commit 9229b25) cut its
  # combo search ~15x, so d3/d5 are now affordable in the fast gate (~0.59/1.25 s/game, tail-inclusive
  # -- no multi-minute blowups anymore). Counts match th's smoke sizing; the d5 job is the smoke long
  # pole at ~94 s single-thread (still well under the 15-min budget). Full deep coverage is OVERNIGHT.
  "hinata  0 1001 1000 0"
  "hinata  3 1001  150 10"
  "hinata  5 1001   75 20"
  # dragonstorm: cheap storm/combo deck (d0 ~0.24ms/game; d3 ~0.17 s/game, d5 ~0.28 s/game measured).
  # Small d3/d5 gate mirroring th/hinata; deeper coverage lives in regression/overnight.
  "dragonstorm 0 1001 1000 0"
  "dragonstorm 3 1001  150 10"
  "dragonstorm 5 1001   75 20"
  # auras: the cheapest deep-search deck in the suite (measured 2026-07-28, single-thread:
  # d3 b10 ~0.023 s/game, d5 b20 ~0.0086 s/game -- d5 is CHEAPER than d3 because its value_play
  # block routes d5 through the O(1) value leaf while the d0/d3 coverage cases bypass it via
  # ignore_play_profile). No heavy tail at any of the 7 suite seeds. That buys it burn-tier
  # counts rather than the smaller th/hinata/dragonstorm sizing: ~9 s added single-thread.
  "auras   0 1001 1000 0"
  "auras   3 1001  300 10"
  "auras   5 1001  250 20"
  # goblins: cheap fast aggro (wins ~turn 4-5, short games). d0 full + small d3/d5 gate,
  # sized like dragonstorm/th; deeper coverage lives in regression/overnight.
  "goblins 0 1001 1000 0"
  "goblins 3 1001  150 10"
  "goblins 5 1001   75 20"
  # creature_giving: gift-the-opponent drain (wins ~turn 4.8). Dragonstorm-class cost
  # (measured 2026-08-06 single-thread: d0 ~0.14 ms/game, d3 b10 ~0.23 s/game, d5 b20
  # ~0.48 s/game) -> th/dragonstorm smoke sizing (~70 s ST added). Covers engine paths no
  # other deck exercises: enter-watchers, stacking Wurm sweeps, DotH upkeep sac-tutor,
  # cumulative upkeep, tutor_land_to_battlefield.
  "creature_giving 0 1001 1000 0"
  "creature_giving 3 1001  150 10"
  "creature_giving 5 1001   75 20"
  # mirrorwing: Zada/Mirrorwing copy-magnet swarm (Tier-3 trick engine; wins ~T5). Hinata-class
  # cost (measured 2026-08-11 post Twinflame-policy, single-thread: d3 b10 ~0.9 s/game, d5 b20
  # ~1.8 s/game; no multi-minute tail at suite budgets -- the provider prunes + strive fold are
  # what keep it tractable, see analysis-Mirrorwing Dragon.md). th/hinata smoke sizing.
  "mirrorwing 0 1001 1000 0"
  "mirrorwing 3 1001  150 10"
  "mirrorwing 5 1001   75 20"
)

# regression: ~8-9 min pre-commit sweep -- two seeds at d3/d5, d0 single seed.
# GATE budgets: every deck at d3=10/d5=20 (NPV=900) = old units => BYTE-IDENTICAL to the
# pre-rebase GT, FULL game counts kept. Converged + stable; the deeper budgets are in OVERNIGHT
# (see SMOKE block). slivers must stay low regardless -- it spins on its heavy tail (s2002 d5
# was 16.5 min at budget 200) for zero benefit. d0 has no search. Only antilife is new here.
REGRESSION_CASES=(
  "slivers 0 2002 1000 0"
  "slivers 3 2002  400 10"
  "slivers 3 3003  400 10"
  "slivers 5 2002  300 20"
  "slivers 5 3003  300 20"
  "burn    0 2002 1000 0"
  "burn    3 2002  500 10"
  "burn    3 3003  500 10"
  "burn    5 2002  500 20"
  "burn    5 3003  500 20"
  "th      0 2002 1000 0"
  "th      3 2002  500 10"
  "th      3 3003  500 10"
  "th      5 2002  300 20"
  "th      5 3003  300 20"
  "knights 0 2002 1000 0"
  "knights 3 2002  300 10"
  "knights 3 3003  300 10"
  "knights 5 2002  250 20"
  "knights 5 3003  250 20"
  # antilife: re-added at 1/10 virtual-ms (see smoke block + search-perf-investigation memory).
  "antilife 0 2002 1000 0"
  "antilife 3 2002  300 10"
  "antilife 3 3003  300 10"
  "antilife 5 2002  250 20"
  "antilife 5 3003  250 20"
  # hinata: d0 full + d3/d5 at both seeds (affordable since the max-mana gate; see SMOKE block).
  # Sized a touch under the other decks (~0.59/1.25 s/game); heaviest job d5 100g ~125 s single-thread.
  "hinata  0 2002 1000 0"
  "hinata  3 2002  200 10"
  "hinata  3 3003  200 10"
  "hinata  5 2002  100 20"
  "hinata  5 3003  100 20"
  # dragonstorm: two seeds at d3/d5 (~0.17/0.28 s/game -> ~4 min added; well under the 45-min budget).
  "dragonstorm 0 2002 1000 0"
  "dragonstorm 3 2002  300 10"
  "dragonstorm 3 3003  300 10"
  "dragonstorm 5 2002  250 20"
  "dragonstorm 5 3003  250 20"
  # auras: burn-tier counts at gate budgets -- ~32 s added single-thread (see SMOKE block).
  "auras   0 2002 1000 0"
  "auras   3 2002  500 10"
  "auras   3 3003  500 10"
  "auras   5 2002  500 20"
  "auras   5 3003  500 20"
  # goblins: two seeds at d3/d5 + d0 (cheap fast aggro; ~4 min added, well under budget).
  "goblins 0 2002 1000 0"
  "goblins 3 2002  300 10"
  "goblins 3 3003  300 10"
  "goblins 5 2002  250 20"
  "goblins 5 3003  250 20"
  # creature_giving: dragonstorm-mirror sizing (~6.5 min ST added; see SMOKE block for costs).
  "creature_giving 0 2002 1000 0"
  "creature_giving 3 2002  300 10"
  "creature_giving 3 3003  300 10"
  "creature_giving 5 2002  250 20"
  "creature_giving 5 3003  250 20"
  # mirrorwing: hinata-mirror sizing (~20 min ST added; see SMOKE block for costs).
  "mirrorwing 0 2002 1000 0"
  "mirrorwing 3 2002  200 10"
  "mirrorwing 3 3003  200 10"
  "mirrorwing 5 2002  100 20"
  "mirrorwing 5 3003  100 20"
)

# overnight: wide multi-seed sweep -- 4 seeds, large game counts for tight statistics.
# Budgets are MORE GENEROUS than the gate modes (8 h budget allows it): deeper search
# explores rarer states and catches edge-case bugs the converged gate budgets miss --
# even where it doesn't change avg win turn. Generosity is spent on the CHEAP decks
# (th/burn at 80/80); slivers stays low (10/20 -- it spins on its heavy tail and its
# 1000g x4-seed counts make a big budget a multi-hour sink for zero benefit); knights
# gets a modest bump (20/40). See search-perf-investigation memory.
#
# SEED SPACING (fixed 2026-08-02 -- the d0 rows were overlapping). A game's identity is
# base_seed + game_index, so a case with base B and N games OWNS effective seeds [B, B+N-1].
# Two cases at the same depth whose ranges touch are NOT independent replicates -- they
# replay the same games and a "4 seeds all agree" reads as 4x more evidence than it is.
# The d0 rows run 2000 games but sat on bases spaced 1001 (4004/5005/6006/7007), so each
# overlapped its neighbour by 999 games: 8000 games reported, 5003 distinct. All 9 decks,
# 27 overlapping pairs. d0 now uses 4004/6006/8008/10010 (spacing 2002 > 2000 games).
# d3/d5 run 1000 games on the 1001-spaced bases and were already disjoint -- by ONE seed,
# so if you ever raise a d3/d5 game count above 1001 you MUST re-space those too.
# Rule: bases must be spaced STRICTLY WIDER than the per-case game count.
# See .claude/skills/regression-testing.md rule 7 and
# docs/design/searched-design-audit-blind-spots.md ("Method trap: overlapping seed bases").
OVERNIGHT_CASES=(
  "slivers 0 4004 2000 0"
  "slivers 0  6006 2000 0"
  "slivers 0  8008 2000 0"
  "slivers 0 10010 2000 0"
  "slivers 3 4004 1000 10"
  "slivers 3 5005 1000 10"
  "slivers 3 6006 1000 10"
  "slivers 3 7007 1000 10"
  "slivers 5 4004 1000 20"
  "slivers 5 5005 1000 20"
  "slivers 5 6006 1000 20"
  "slivers 5 7007 1000 20"
  "burn    0 4004 2000 0"
  "burn    0  6006 2000 0"
  "burn    0  8008 2000 0"
  "burn    0 10010 2000 0"
  "burn    3 4004 1000 80"
  "burn    3 5005 1000 80"
  "burn    3 6006 1000 80"
  "burn    3 7007 1000 80"
  "burn    5 4004 1000 80"
  "burn    5 5005 1000 80"
  "burn    5 6006 1000 80"
  "burn    5 7007 1000 80"
  "th      0 4004 2000 0"
  "th      0  6006 2000 0"
  "th      0  8008 2000 0"
  "th      0 10010 2000 0"
  "th      3 4004 1000 80"
  "th      3 5005 1000 80"
  "th      3 6006 1000 80"
  "th      3 7007 1000 80"
  "th      5 4004 1000 80"
  "th      5 5005 1000 80"
  "th      5 6006 1000 80"
  "th      5 7007 1000 80"
  "knights 0 4004 2000 0"
  "knights 0  6006 2000 0"
  "knights 0  8008 2000 0"
  "knights 0 10010 2000 0"
  "knights 3 4004 1000 20"
  "knights 3 5005 1000 20"
  "knights 3 6006 1000 20"
  "knights 3 7007 1000 20"
  "knights 5 4004 1000 40"
  "knights 5 5005 1000 40"
  "knights 5 6006 1000 40"
  "knights 5 7007 1000 40"
  # antilife: re-added at 1/10 virtual-ms (see smoke block + search-perf-investigation memory).
  "antilife 0 4004 2000 0"
  "antilife 0  6006 2000 0"
  "antilife 0  8008 2000 0"
  "antilife 0 10010 2000 0"
  "antilife 3 4004 1000 10"
  "antilife 3 5005 1000 10"
  "antilife 3 6006 1000 10"
  "antilife 3 7007 1000 10"
  "antilife 5 4004 1000 20"
  "antilife 5 5005 1000 20"
  "antilife 5 6006 1000 20"
  "antilife 5 7007 1000 20"
  # hinata: the deep-search home. The max-mana backtracker gate (commit 9229b25) cut its combo search
  # ~15x (d3 5.9->0.47 s/game at session start; ~0.59/1.25 s/game d3/d5 tail-inclusive at THIS scale),
  # so the old tiny 40/25 sample is no longer necessary. Raised to 400 d3 / 300 d5 per seed = ~2449 s
  # single-thread total across the 4 seeds -- roughly on par with burn's deep-search cost, a real
  # sample without letting Hinata dominate the makespan (measured: no multi-minute blowups remain at
  # this scale). Still kept under the other decks' 1000/seed on purpose (Hinata is ~8x their per-game
  # cost). Re-measure the tail before raising further (a rare monster game could still surprise).
  "hinata  0 4004 2000 0"
  "hinata  0  6006 2000 0"
  "hinata  0  8008 2000 0"
  "hinata  0 10010 2000 0"
  "hinata  3 4004  400 10"
  "hinata  3 5005  400 10"
  "hinata  3 6006  400 10"
  "hinata  3 7007  400 10"
  "hinata  5 4004  300 20"
  "hinata  5 5005  300 20"
  "hinata  5 6006  300 20"
  "hinata  5 7007  300 20"
  # dragonstorm: 4-seed sweep at modest gate budgets (10/20) -- it's cheap, ~11 min total across seeds.
  "dragonstorm 0 4004 2000 0"
  "dragonstorm 0  6006 2000 0"
  "dragonstorm 0  8008 2000 0"
  "dragonstorm 0 10010 2000 0"
  "dragonstorm 3 4004  500 10"
  "dragonstorm 3 5005  500 10"
  "dragonstorm 3 6006  500 10"
  "dragonstorm 3 7007  500 10"
  "dragonstorm 5 4004  300 20"
  "dragonstorm 5 5005  300 20"
  "dragonstorm 5 6006  300 20"
  "dragonstorm 5 7007  300 20"
  # auras: full burn-tier 1000g x 4 seeds -- it is cheap enough (see SMOKE block) that the deep
  # decks still own the makespan. Budgets are split on MEASUREMENT, not by mode convention:
  #   d5 stays at the gate budget 20 because it is CONVERGED there -- re-running all four overnight
  #     seeds at budget 80 reproduced all four play digests BYTE-IDENTICALLY (96deaf67/40d23059/
  #     1422766f/d0945e74) for ~the same wall time. Generosity at d5 is provably zero-value here.
  #   d3 gets the generous 80 (the burn/th overnight level): unlike d5 it is NOT converged at 10 --
  #     budget 80 moved every seed's digest and shaved ~0.004 avg turns. That is noise on the
  #     metric, but it is real extra state exploration, which is what the overnight budget is for.
  #     Cost measured at 0.066 s/game vs 0.023 (2.8x) = ~4 min single-thread across the 4 seeds.
  "auras   0 4004 2000 0"
  "auras   0  6006 2000 0"
  "auras   0  8008 2000 0"
  "auras   0 10010 2000 0"
  "auras   3 4004 1000 80"
  "auras   3 5005 1000 80"
  "auras   3 6006 1000 80"
  "auras   3 7007 1000 80"
  "auras   5 4004 1000 20"
  "auras   5 5005 1000 20"
  "auras   5 6006 1000 20"
  "auras   5 7007 1000 20"
  # goblins: wide 4-seed sweep (cheap fast aggro; d3/d5 sized like knights/dragonstorm).
  "goblins 0 4004 2000 0"
  "goblins 0  6006 2000 0"
  "goblins 0  8008 2000 0"
  "goblins 0 10010 2000 0"
  "goblins 3 4004 1000 20"
  "goblins 3 5005 1000 20"
  "goblins 3 6006 1000 20"
  "goblins 3 7007 1000 20"
  "goblins 5 4004 1000 40"
  "goblins 5 5005 1000 40"
  "goblins 5 6006 1000 40"
  "goblins 5 7007 1000 40"
  # creature_giving: goblins-style generosity (d3 b20 / d5 b40, ~2x the gate budgets) at
  # dragonstorm-style d5 counts -- d5 b40 is the deck's expensive axis (~1 s/game ST est.),
  # so 500g x 4 seeds keeps it ~35 min ST. d3 1000g <= 1001 seed spacing; d0 2000g on the
  # 2002-spaced bases (see SEED SPACING note above).
  "creature_giving 0 4004 2000 0"
  "creature_giving 0  6006 2000 0"
  "creature_giving 0  8008 2000 0"
  "creature_giving 0 10010 2000 0"
  "creature_giving 3 4004 1000 20"
  "creature_giving 3 5005 1000 20"
  "creature_giving 3 6006 1000 20"
  "creature_giving 3 7007 1000 20"
  "creature_giving 5 4004  500 40"
  "creature_giving 5 5005  500 40"
  "creature_giving 5 6006  500 40"
  "creature_giving 5 7007  500 40"
  # mirrorwing: hinata-mirror overnight sizing (~60 min ST added; heaviest suite deck per game
  # after slivers -- deeper budgets deliberately NOT raised until a b-sweep motivates them).
  "mirrorwing 0 4004 2000 0"
  "mirrorwing 0 6006 2000 0"
  "mirrorwing 0 8008 2000 0"
  "mirrorwing 0 10010 2000 0"
  "mirrorwing 3 4004  400 10"
  "mirrorwing 3 5005  400 10"
  "mirrorwing 3 6006  400 10"
  "mirrorwing 3 7007  400 10"
  "mirrorwing 5 4004  300 20"
  "mirrorwing 5 5005  300 20"
  "mirrorwing 5 6006  300 20"
  "mirrorwing 5 7007  300 20"
)
