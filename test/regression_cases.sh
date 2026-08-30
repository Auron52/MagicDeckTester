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
  # PINNED TO THE ARCHIVED LIST, deliberately and TEMPORARILY (2026-08-30). The shipping
  # decks/Mirrorwing Dragon/ is now the Anger-4 / Oracle-3 list (3faf5c76) and its profile /
  # value leaf / keep table have not been regenerated yet. Measuring the new list against GT
  # taken on the old one is not a regression, it is a different deck -- so the suite keeps
  # running the list the GT actually describes until the rebaseline lands. Repoint both this
  # and DECK_PROF back to the top level in the same commit as that rebaseline.
  [mirrorwing]="decks/Mirrorwing Dragon/v2-instigator-entrance/Mirrorwing Dragon.cod"
  [fivecolour]=decks/FiveColour/FiveColour.cod
  [stompy]=decks/StompySurprise/StompySurprise.cod
  [minotaur]=decks/Minotaur/Minotaur.cod
  [kitty]=decks/KittyEquipment/KittyEquipment.cod
  [dragons]=decks/Dragons/Dragons.cod
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
  # Pinned to the archived list -- see the DECK_FILE note above. The value leaf and the
  # exhaustive keep table resolve directory-relative off THIS path, so pointing it at the
  # variant folder moves the whole fitted apparatus with it.
  [mirrorwing]="decks/Mirrorwing Dragon/v2-instigator-entrance/Mirrorwing Dragon.profile.json"
  [fivecolour]=decks/FiveColour/FiveColour.profile.json
  [stompy]=decks/StompySurprise/StompySurprise.profile.json
  [minotaur]=decks/Minotaur/Minotaur.profile.json
  [kitty]=decks/KittyEquipment/KittyEquipment.profile.json
  [dragons]=decks/Dragons/Dragons.profile.json
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
  # mirrorwing: Zada/Mirrorwing copy-magnet swarm (Tier-3 trick engine; wins ~T4.8). Switched
  # 2026-08-24 from the v1-twinflame-anger list to the SHIPPING list (tournament winner + Game
  # Trail mana base; the archived v1 lives on at decks/Mirrorwing Dragon/v1-twinflame-anger/).
  # Dropping Twinflame took most of the copy-token combinatorics with it: re-measured on the new
  # list single-thread, d3 b10 ~0.20 s/game and d5 b20 ~0.30 s/game -- 4-6x CHEAPER than the v1
  # list's 0.9/1.8, so this deck is no longer Hinata-class and the counts below now carry a lot of
  # headroom (deliberately left as-is; raising them is a separate, GT-moving decision). No
  # multi-minute tail at suite budgets -- the provider prunes + strive fold are what keep it
  # tractable, see analysis-Mirrorwing Dragon.md. th/hinata smoke sizing.
  "mirrorwing 0 1001 1000 0"
  "mirrorwing 3 1001  150 10"
  "mirrorwing 5 1001   75 20"
  # fivecolour: 5-colour midrange, the ONLY deck whose value_play block asks for depth 6 besides
  # burn -- so its d5 case (depth key dropped, block owns the depth) is the suite's coverage of the
  # d6 + escalation_cap 5 path adopted 2026-08-14. Costliest deck per game in the suite, measured
  # 2026-08-14 single-thread over 2800 games: d0 ~0.03 ms/game, d3 b10 ~1.19 s/game, d5(->d6) b20
  # ~1.89 s/game -- it took the same hinata/mirrorwing sizing (mirrorwing was 0.9/1.8 on the v1
  # list; since the 2026-08-24 decklist switch it is ~0.20/0.30, so fivecolour now stands alone).
  # Tail is mild: 2 games of 2800 over 30 s (worst 60.8 s), no multi-minute blowups. ~5 min ST added.
  "fivecolour 0 1001 1000 0"
  "fivecolour 3 1001  150 10"
  "fivecolour 5 1001   75 20"
  # stompy: StompySurprise elf ramp (wins ~T4.9; StompyProvider -- discard buckets + tutor
  # lethality gate). Dragonstorm-class cost (probed 2026-08-21 single-thread at gate budgets:
  # d0 ~0 ms/game, d3 b10 ~0.28 s/game, d5 b20 ~0.63 s/game; no heavy tail, worst game
  # budget-bound). th/dragonstorm smoke sizing (~90 s ST added). Covers engine paths no other
  # deck exercises: scaled elf mana + growth credit, Lodge untap burst, MDFC spell//land,
  # sac-cost tutor (Natural Order), Terastodon K projection, Call of the Wild reveal chain.
  "stompy  0 1001 1000 0"
  "stompy  3 1001  150 10"
  "stompy  5 1001   75 20"
  # minotaur: Rakdos Minotaur tribal aggro (wins ~T5.0 at searched depth; MinotaurProvider since
  # 2026-08-30 -- Generic plus the user-amended cleanup-discard bucket policy). Ships a value leaf
  # with trust at d5.
  # Probed single-thread 2026-08-23: d0 ~0.0002 s/game, d3 b10 ~0.39 s/game, d5 b20 ~0.63 s/game
  # -- between stompy and hinata. Counts mirror slivers/knights smoke sizing, giving two ~95 s
  # single-thread jobs that pool alongside the existing gate (smoke makespan was ~160 s).
  "minotaur 0 1001 1000 0"
  "minotaur 3 1001  250 10"
  "minotaur 5 1001  150 20"
  # kitty: KittyEquipment -- mono-white equipment aggro (Kor Duelist/Balan double-strike + stacked
  # equipment; wins ~T4.3-5.2). Pooled-probe 2026-08-29 vs minotaur as in-batch anchor: d0
  # ~0.0006 s/game, d3 b10 ~0.46 s/game, d5 b20 ~0.50 s/game -- 0.21x/0.13x minotaur, the
  # CHEAPEST searched deck in the suite. Ships an adopted exhaustive keep model + a value leaf.
  "kitty 0 1001 1000 0"
  "kitty 3 1001  250 10"
  "kitty 5 1001  150 20"
  # dragons: mono-red Dragons ramp (Sol Ring/Dragonspeaker into 5-8 drops; wins ~T5.7-6.2, the
  # slowest clock in the suite after hinata). Same probe: d0 ~0.0004 s/game, d3 b10 ~0.98 s/game,
  # d5 b20 ~2.0 s/game -- 0.44x/0.52x minotaur. Defaults/static keep; ships a value leaf (no trust),
  # and DragonsProvider's cleanup-discard bucket policy.
  "dragons 0 1001 1000 0"
  "dragons 3 1001  250 10"
  "dragons 5 1001  150 20"
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
  # mirrorwing: sizing inherited from the v1 list (~20 min ST budgeted); on the 2026-08-24
  # shipping list it actually costs ~2 min ST. See SMOKE block for the re-measured costs.
  "mirrorwing 0 2002 1000 0"
  "mirrorwing 3 2002  200 10"
  "mirrorwing 3 3003  200 10"
  "mirrorwing 5 2002  100 20"
  "mirrorwing 5 3003  100 20"
  # fivecolour: hinata/mirrorwing-mirror sizing (~14 min ST added; see SMOKE block for costs).
  "fivecolour 0 2002 1000 0"
  "fivecolour 3 2002  200 10"
  "fivecolour 3 3003  200 10"
  "fivecolour 5 2002  100 20"
  "fivecolour 5 3003  100 20"
  # stompy: dragonstorm-style two-seed sweep (~8 min ST added at probed per-game costs).
  "stompy  0 2002 1000 0"
  "stompy  3 2002  300 10"
  "stompy  3 3003  300 10"
  "stompy  5 2002  250 20"
  "stompy  5 3003  250 20"
  # minotaur: stompy-style two-seed sweep (~9 min ST added at the probed per-game costs; the
  # longest job, d5 x250, is ~158 s -- well inside the existing regression makespan).
  "minotaur 0 2002 1000 0"
  "minotaur 3 2002  300 10"
  "minotaur 3 3003  300 10"
  "minotaur 5 2002  250 20"
  "minotaur 5 3003  250 20"
  # kitty: minotaur-shaped two-seed sweep. At 0.21x/0.13x minotaur's per-game cost this is the
  # cheapest block in the tier (~9 min ST added, vs minotaur's ~9 min for 5x the work).
  "kitty 0 2002 1000 0"
  "kitty 3 2002  300 10"
  "kitty 3 3003  300 10"
  "kitty 5 2002  250 20"
  "kitty 5 3003  250 20"
  # dragons: same shape (~26 min ST added at the probed costs; pools inside the existing makespan).
  "dragons 0 2002 1000 0"
  "dragons 3 2002  300 10"
  "dragons 3 3003  300 10"
  "dragons 5 2002  250 20"
  "dragons 5 3003  250 20"
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
  # mirrorwing: sizing inherited from the v1 list (~60 min ST budgeted, when this was the heaviest
  # suite deck per game after slivers); on the 2026-08-24 shipping list it costs ~10 min ST.
  # Deeper budgets deliberately NOT raised until a b-sweep motivates them.
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
  # fivecolour: hinata/mirrorwing-mirror overnight sizing (~70 min ST added; now the costliest deck
  # per game in the suite). Budgets deliberately left at the gate values -- the d5 case runs at the
  # value block's depth 6, so a generous budget here would change the shipped config, not stress it.
  "fivecolour 0 4004 2000 0"
  "fivecolour 0 6006 2000 0"
  "fivecolour 0 8008 2000 0"
  "fivecolour 0 10010 2000 0"
  "fivecolour 3 4004  400 10"
  "fivecolour 3 5005  400 10"
  "fivecolour 3 6006  400 10"
  "fivecolour 3 7007  400 10"
  "fivecolour 5 4004  300 20"
  "fivecolour 5 5005  300 20"
  "fivecolour 5 6006  300 20"
  "fivecolour 5 7007  300 20"
  # stompy: creature_giving-style generosity (d3 b20 / d5 b40, ~2x the gate budgets) --
  # ~70 min ST added (d3 ~0.45 s/game, d5 b40 ~1.2 s/game est. from the gate-budget probe).
  # d0 on the 2002-spaced bases per the SEED SPACING rule above.
  "stompy  0 4004 2000 0"
  "stompy  0  6006 2000 0"
  "stompy  0  8008 2000 0"
  "stompy  0 10010 2000 0"
  "stompy  3 4004 1000 20"
  "stompy  3 5005 1000 20"
  "stompy  3 6006 1000 20"
  "stompy  3 7007 1000 20"
  "stompy  5 4004  500 40"
  "stompy  5 5005  500 40"
  "stompy  5 6006  500 40"
  "stompy  5 7007  500 40"
  # minotaur: stompy-shaped four-seed deep sweep at 2x the gate budgets (d3 b20 / d5 b40).
  # At the probed costs that is ~4x1000x0.39 s + 4x500x0.63 s ~= 47 min single-thread, pooled.
  "minotaur 0  4004 2000 0"
  "minotaur 0  6006 2000 0"
  "minotaur 0  8008 2000 0"
  "minotaur 0 10010 2000 0"
  "minotaur 3 4004 1000 20"
  "minotaur 3 5005 1000 20"
  "minotaur 3 6006 1000 20"
  "minotaur 3 7007 1000 20"
  "minotaur 5 4004  500 40"
  "minotaur 5 5005  500 40"
  "minotaur 5 6006  500 40"
  "minotaur 5 7007  500 40"
  # kitty: minotaur-shaped 4-seed deep sweep at 2x gate budgets. Probed at the ACTUAL overnight
  # budgets 2026-08-29: d3 b20 ~0.83 s/game, d5 b40 ~1.65 s/game (0.32x/0.21x minotaur)
  # => ~1.8 core-hours added.
  "kitty 0  4004 2000 0"
  "kitty 0  6006 2000 0"
  "kitty 0  8008 2000 0"
  "kitty 0 10010 2000 0"
  "kitty 3 4004 1000 20"
  "kitty 3 5005 1000 20"
  "kitty 3 6006 1000 20"
  "kitty 3 7007 1000 20"
  "kitty 5 4004  500 40"
  "kitty 5 5005  500 40"
  "kitty 5 6006  500 40"
  "kitty 5 7007  500 40"
  # dragons: same shape. Probed at b20/b40: d3 ~1.39 s/game, d5 ~3.60 s/game (0.54x/0.45x
  # minotaur) => ~3.6 core-hours added. Combined with kitty this is ~+5.4 core-hours on a tier
  # that ran ~9.6 core-hours, i.e. ~40 min wall on a free box against an 8 h budget.
  "dragons 0  4004 2000 0"
  "dragons 0  6006 2000 0"
  "dragons 0  8008 2000 0"
  "dragons 0 10010 2000 0"
  "dragons 3 4004 1000 20"
  "dragons 3 5005 1000 20"
  "dragons 3 6006 1000 20"
  "dragons 3 7007 1000 20"
  "dragons 5 4004  500 40"
  "dragons 5 5005  500 40"
  "dragons 5 6006  500 40"
  "dragons 5 7007  500 40"
)
