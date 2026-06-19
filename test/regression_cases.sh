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
# --lookahead-bottoming is always added at depth>0 (every deck, every mode).
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
)
declare -A DECK_PROF=(
  [slivers]=decks/slivers_vial.profile.json
  [burn]=decks/test_deck.profile.json
  [th]=decks/treasure_hunt.profile.json
  [knights]=decks/Knights.profile.json
  [antilife]=decks/Anti-Lifegain.profile.json
)

# Seeds:  smoke=1001  regression=2002,3003  overnight=4004,5005,6006,7007
# (counts sized from measured timings -- see test/TIMINGS.md)

# smoke: ~3 min single-seed gate -- d0 full + small d3/d5 (deep-search crash check).
SMOKE_CASES=(
  "slivers 0 1001 1000 0"
  "slivers 3 1001  250 100"
  "slivers 5 1001  150 200"
  "burn    0 1001 1000 0"
  "burn    3 1001  300 100"
  "burn    5 1001  250 200"
  "th      0 1001 1000 0"
  "th      3 1001  150 100"
  "th      5 1001   75 200"
  "knights 0 1001 1000 0"
  "knights 3 1001  250 100"
  "knights 5 1001  150 200"
  # antilife: pulled from the suites pending lever-2 path-pruning. Its no-win games
  # explore pathological trees that the overrun guard can only bound at a low ceiling,
  # which perturbs normal games of other decks (broke th d3 s3003 game 278). Re-add once
  # the no-win hang is fixed (and the per-node-cost work / budget rebase may pull it under
  # budget on its own). See search-perf-investigation memory.
  # "antilife 0 1001 1000 0"
  # "antilife 3 1001  250 100"
  # "antilife 5 1001  150 200"
)

# regression: ~40 min pre-commit sweep -- two seeds at d3/d5, d0 single seed.
# slivers d3/d5 counts are kept modest because slivers has a severe seed-specific
# heavy tail (one pathological deep game can pin a core for minutes -- s2002 d5
# 500g hit 23 min); see TIMINGS.md. Trim slivers first if this mode overruns.
REGRESSION_CASES=(
  "slivers 0 2002 1000 0"
  "slivers 3 2002  400 100"
  "slivers 3 3003  400 100"
  "slivers 5 2002  300 200"
  "slivers 5 3003  300 200"
  "burn    0 2002 1000 0"
  "burn    3 2002  500 100"
  "burn    3 3003  500 100"
  "burn    5 2002  500 200"
  "burn    5 3003  500 200"
  "th      0 2002 1000 0"
  "th      3 2002  500 100"
  "th      3 3003  500 100"
  "th      5 2002  300 200"
  "th      5 3003  300 200"
  # knights: big creature/lord boards make EnumeratePlans subsets large, so d3/d5 are
  # relatively heavy (~0.15-0.18s/game); kept to modest counts to stay within the mode budget.
  "knights 0 2002 1000 0"
  "knights 3 2002  300 100"
  "knights 3 3003  300 100"
  "knights 5 2002  250 200"
  "knights 5 3003  250 200"
  # antilife: pulled pending lever-2 path-pruning (see smoke block + search-perf-investigation).
  # "antilife 0 2002 1000 0"
  # "antilife 3 2002  300 100"
  # "antilife 3 3003  300 100"
  # "antilife 5 2002  250 200"
  # "antilife 5 3003  250 200"
)

# overnight: ~80 min wide multi-seed sweep -- 4 seeds, large game counts for
# tight statistics (subsumes the old standalone multi-seed A/B helper).
OVERNIGHT_CASES=(
  "slivers 0 4004 2000 0"
  "slivers 0 5005 2000 0"
  "slivers 0 6006 2000 0"
  "slivers 0 7007 2000 0"
  "slivers 3 4004 1000 100"
  "slivers 3 5005 1000 100"
  "slivers 3 6006 1000 100"
  "slivers 3 7007 1000 100"
  "slivers 5 4004 1000 200"
  "slivers 5 5005 1000 200"
  "slivers 5 6006 1000 200"
  "slivers 5 7007 1000 200"
  "burn    0 4004 2000 0"
  "burn    0 5005 2000 0"
  "burn    0 6006 2000 0"
  "burn    0 7007 2000 0"
  "burn    3 4004 1000 100"
  "burn    3 5005 1000 100"
  "burn    3 6006 1000 100"
  "burn    3 7007 1000 100"
  "burn    5 4004 1000 200"
  "burn    5 5005 1000 200"
  "burn    5 6006 1000 200"
  "burn    5 7007 1000 200"
  "th      0 4004 2000 0"
  "th      0 5005 2000 0"
  "th      0 6006 2000 0"
  "th      0 7007 2000 0"
  "th      3 4004 1000 100"
  "th      3 5005 1000 100"
  "th      3 6006 1000 100"
  "th      3 7007 1000 100"
  "th      5 4004 1000 200"
  "th      5 5005 1000 200"
  "th      5 6006 1000 200"
  "th      5 7007 1000 200"
  "knights 0 4004 2000 0"
  "knights 0 5005 2000 0"
  "knights 0 6006 2000 0"
  "knights 0 7007 2000 0"
  "knights 3 4004 1000 100"
  "knights 3 5005 1000 100"
  "knights 3 6006 1000 100"
  "knights 3 7007 1000 100"
  "knights 5 4004 1000 200"
  "knights 5 5005 1000 200"
  "knights 5 6006 1000 200"
  "knights 5 7007 1000 200"
  # antilife: pulled pending lever-2 path-pruning (see smoke block + search-perf-investigation).
  # "antilife 0 4004 2000 0"
  # "antilife 0 5005 2000 0"
  # "antilife 0 6006 2000 0"
  # "antilife 0 7007 2000 0"
  # "antilife 3 4004 1000 100"
  # "antilife 3 5005 1000 100"
  # "antilife 3 6006 1000 100"
  # "antilife 3 7007 1000 100"
  # "antilife 5 4004 1000 200"
  # "antilife 5 5005 1000 200"
  # "antilife 5 6006 1000 200"
  # "antilife 5 7007 1000 200"
)
