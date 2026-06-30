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

declare -A DECK_FILE=(
  [slivers]=decks/slivers_vial.txt
  [burn]=decks/test_deck.txt
  [th]=decks/treasure_hunt.txt
  [knights]=decks/Knights.cod
  [antilife]=decks/Anti-Lifegain.cod
  [hinata]=decks/Hinata2.cod
)
declare -A DECK_PROF=(
  [slivers]=decks/slivers_vial.profile.json
  [burn]=decks/test_deck.profile.json
  [th]=decks/treasure_hunt.profile.json
  [knights]=decks/Knights.profile.json
  [antilife]=decks/Anti-Lifegain.profile.json
  [hinata]=decks/Hinata2.profile.json
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
  # hinata: d0 ONLY in the gate modes. Its combo search at d3/d5 is ~1000x the other decks per game
  # with occasional multi-minute turns (combo enumeration blowup -- see search-perf memory), so a
  # single bad game could blow the 3-min smoke budget. d0 (greedy, ~3 ms/game) is cheap and catches
  # card-implementation regressions; the deep-search coverage lives in OVERNIGHT (8 h budget).
  "hinata  0 1001 1000 0"
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
  # hinata: d0 only in the gate modes (see SMOKE block) -- deep search is overnight-only.
  "hinata  0 2002 1000 0"
)

# overnight: wide multi-seed sweep -- 4 seeds, large game counts for tight statistics.
# Budgets are MORE GENEROUS than the gate modes (8 h budget allows it): deeper search
# explores rarer states and catches edge-case bugs the converged gate budgets miss --
# even where it doesn't change avg win turn. Generosity is spent on the CHEAP decks
# (th/burn at 80/80); slivers stays low (10/20 -- it spins on its heavy tail and its
# 1000g x4-seed counts make a big budget a multi-hour sink for zero benefit); knights
# gets a modest bump (20/40). See search-perf-investigation memory.
OVERNIGHT_CASES=(
  "slivers 0 4004 2000 0"
  "slivers 0 5005 2000 0"
  "slivers 0 6006 2000 0"
  "slivers 0 7007 2000 0"
  "slivers 3 4004 1000 10"
  "slivers 3 5005 1000 10"
  "slivers 3 6006 1000 10"
  "slivers 3 7007 1000 10"
  "slivers 5 4004 1000 20"
  "slivers 5 5005 1000 20"
  "slivers 5 6006 1000 20"
  "slivers 5 7007 1000 20"
  "burn    0 4004 2000 0"
  "burn    0 5005 2000 0"
  "burn    0 6006 2000 0"
  "burn    0 7007 2000 0"
  "burn    3 4004 1000 80"
  "burn    3 5005 1000 80"
  "burn    3 6006 1000 80"
  "burn    3 7007 1000 80"
  "burn    5 4004 1000 80"
  "burn    5 5005 1000 80"
  "burn    5 6006 1000 80"
  "burn    5 7007 1000 80"
  "th      0 4004 2000 0"
  "th      0 5005 2000 0"
  "th      0 6006 2000 0"
  "th      0 7007 2000 0"
  "th      3 4004 1000 80"
  "th      3 5005 1000 80"
  "th      3 6006 1000 80"
  "th      3 7007 1000 80"
  "th      5 4004 1000 80"
  "th      5 5005 1000 80"
  "th      5 6006 1000 80"
  "th      5 7007 1000 80"
  "knights 0 4004 2000 0"
  "knights 0 5005 2000 0"
  "knights 0 6006 2000 0"
  "knights 0 7007 2000 0"
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
  "antilife 0 5005 2000 0"
  "antilife 0 6006 2000 0"
  "antilife 0 7007 2000 0"
  "antilife 3 4004 1000 10"
  "antilife 3 5005 1000 10"
  "antilife 3 6006 1000 10"
  "antilife 3 7007 1000 10"
  "antilife 5 4004 1000 20"
  "antilife 5 5005 1000 20"
  "antilife 5 6006 1000 20"
  "antilife 5 7007 1000 20"
  # hinata: the deep-search home (gate modes are d0-only -- see SMOKE block). d0 full; d3/d5 kept
  # SMALL -- Hinata's combo enumeration measured ~16 s/GAME AVG at d3 budget 10 (single-thread) vs
  # ~0.4 s for burn, i.e. ~40x, NOT the ~1000x an older comment claimed; the average is dragged up
  # by a heavy tail of occasional multi-minute combo-enumeration turns (typical games are closer to
  # ~10x). So these are a crash/divergence SMOKE check, NOT a statistical sample. At 40 d3 + 25 d5
  # per seed x4 that is ~2 h of the 8 h overnight. Cut from 150/100 (2026-06-30) after measuring real
  # per-game wall time. Validate on a free machine before raising; profiling the tail could cut it.
  "hinata  0 4004 2000 0"
  "hinata  0 5005 2000 0"
  "hinata  0 6006 2000 0"
  "hinata  0 7007 2000 0"
  "hinata  3 4004   40 10"
  "hinata  3 5005   40 10"
  "hinata  3 6006   40 10"
  "hinata  3 7007   40 10"
  "hinata  5 4004   25 20"
  "hinata  5 5005   25 20"
  "hinata  5 6006   25 20"
  "hinata  5 7007   25 20"
)
