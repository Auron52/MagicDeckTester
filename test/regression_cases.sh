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
  [fungus]=decks/Fungus/Fungus.cod
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
  [fivecolour]=decks/FiveColour/FiveColour.cod
  [stompy]=decks/StompySurprise/StompySurprise.cod
  [minotaur]=decks/Minotaur/Minotaur.cod
  [kitty]=decks/KittyEquipment/KittyEquipment.cod
  [dragons]=decks/Dragons/Dragons.cod
  [breaching]=decks/BreachingDragonstorm/BreachingDragonstorm.cod
  [critter]=decks/CritterLifegain/CritterLifegain.cod
  [melira]="decks/Melira Pod/Melira Pod.cod"
  [angels]=decks/Angels/Angels.cod
  [fluctuator]=decks/Fluctuator/Fluctuator.cod
  [snow]=decks/Snow/Snow.cod
  [giants]=decks/Giants/Giants.cod
)
declare -A DECK_PROF=(
  [fungus]=decks/Fungus/Fungus.profile.json
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
  [fivecolour]=decks/FiveColour/FiveColour.profile.json
  [stompy]=decks/StompySurprise/StompySurprise.profile.json
  [minotaur]=decks/Minotaur/Minotaur.profile.json
  [kitty]=decks/KittyEquipment/KittyEquipment.profile.json
  [dragons]=decks/Dragons/Dragons.profile.json
  [breaching]=decks/BreachingDragonstorm/BreachingDragonstorm.profile.json
  [critter]=decks/CritterLifegain/CritterLifegain.profile.json
  [melira]="decks/Melira Pod/Melira Pod.profile.json"
  [angels]=decks/Angels/Angels.profile.json
  [fluctuator]=decks/Fluctuator/Fluctuator.profile.json
  [snow]=decks/Snow/Snow.profile.json
  [giants]=decks/Giants/Giants.profile.json
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
  # fungus: Hinata's sizing, adopted wholesale rather than probed (user call 2026-09-20).
  # MEASURED 2026-09-22 before adopting: this deck is the suite's most expensive by a clear margin
  # -- 281.8s CPU here (2nd behind snow's 520.5s) and 1,331.1s in REGRESSION, where it is 1st at
  # 2.6x snow. Smoke makespan 71s -> 112s, regression 159s -> 318s; both well inside their 15 min /
  # 45 min budgets, which is why it goes in now and the cost is an optimization target rather than
  # a blocker (USER: "Fungus should be in the suite unless there is a performance blocker").
  "fungus  0 1001 1000 0"
  "fungus  3 1001  150 10"
  "fungus  5 1001   75 20"
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
  # breaching: BreachingDragonstorm -- Temur cascade/free-cast combo (double sac-land burst into
  # Wanderer/Creative Technique chains; wins ~T3.5, 62% T3 at converged budgets). Pooled-probe
  # 2026-09-03 vs dragonstorm as in-batch anchor: d0 ~0.007 ms/game, d3 b10 ~0.38 s/game,
  # d5 b20 ~0.56 s/game (~7x/14x dragonstorm) -- kitty-class cost. Generic provider, no keep
  # model / value leaf yet (Stage 4 pending): the d5 cell will move when a value leaf is adopted
  # (its case omits depth so value_play owns it -- expected churn, regenerate GT then).
  "breaching 0 1001 1000 0"
  "breaching 3 1001  150 10"
  "breaching 5 1001   75 20"
  # critter: CritterLifegain -- mono-white lifegain aggro (Soul Warden shell into Ajani's Pridemate /
  # Voice of the Blessed / Archangel of Thune / Heliod; wins ~T5). Probed single-thread 2026-09-08 at
  # the gate budgets: d3 b10 ~0.20 s/game, d5 b20 ~0.43 s/game -- breaching-class cost, so it takes
  # breaching's shape. CritterLifegainProvider (Generic + walker-aware legend keep); no keep model /
  # value leaf yet (the d5 cell will move when a value leaf is adopted -- regenerate GT then).
  "critter 0 1001 1000 0"
  "critter 3 1001  150 10"
  "critter 5 1001   75 20"
  # melira: RESTORED 2026-09-08 (user: "performance should be close to other decks"). Dropped
  # 2026-09-06 at smoke d3 31 s/game, d5 26 s/game (72% of the tier's core-time from one deck).
  # The wall was the 1-ply search LEAF re-running the greedy subset walk at every rollout turn
  # (93% of a d3 game, unprunable: 99.5% of visited subsets survive every feasibility filter);
  # the deck now ships `search_leaf_depth: 0` in its profile (quality-neutral on 3000/2000
  # paired games) plus byte-identical walk/tutor opts. Measured 2026-09-08 in 1000-game batches:
  # d3 0.65-0.78 s/game, d5 1.2 s/game -- third-costliest deck, under fivecolour (1.27 / 1.93)
  # and fluctuator (1.67 / 3.10). Full record: docs/design/analysis-Melira Pod.md SESSION 2026-09-08.
  "melira 0 1001 1000 0"
  "melira 3 1001   50 10"
  "melira 5 1001   25 20"
  # angels: ADDED 2026-09-18 (mono-white Angels tribal; analysis-Angels.md). Full kitty/critter
  # shape because it is one of the CHEAPEST decks in the matrix -- measured single-threaded at the
  # tier's own budgets: d0 ~0 s, d3 b10 0.066 s/game, d5 b20 0.060 s/game (cf. dragons 0.98 / 2.00).
  # Smoke's share is ~26 core-s. THAT PREDICTION CAME TRUE AND IS SPENT: the note here used to say
  # the deck shipped the leafless single-pass shape (leaf: none) and that the d3/d5 cells would move
  # if a value leaf were ever adopted. One was, on 2026-09-19 with the v2 decklist (598848fa /
  # 4e81a3e7), and all 20 keys were rebaselined then. Angels now ships a regenerated value sidecar
  # (0.32x the leafless cost) plus an exhaustive keep model; note its value_play block carries only
  # expected_buckets/mull_gen_*, so enabled stays false and the d5 rows run the ENGINE default depth
  # rather than a block-locked one -- the sidecar's mere PRESENCE is what activates the hybrid leaf.
  # Added specifically to close a hole the ledger documents: with no GT key, NOTHING in the repo
  # noticed when an engine change moved this deck's play, and its claude-play sweep record could go
  # stale silently. That had to be checked by hand once already (2026-09-18, digests, Saga work).
  "angels 0 1001 1000 0"
  "angels 3 1001  250 10"
  "angels 5 1001  150 20"
  # 2HG gate (user request 2026-09-04): "<deck>2hg" = the SAME deck/profile at starting_life 30
  # + opponent_heads 2 (regression.sh strips the suffix for file lookup and adds the job fields).
  # Small on purpose -- just enough that a change which obviously ignores 2HG (second-face
  # targeting, "each opponent" x heads) moves a committed digest. Searched d3 cases for the decks
  # with 2HG-RELEVANT cards: hinata (Crackle both heads / Soulfire second flip), antilife
  # (Cutter/Silence "each other player" + partner gift, Aria/Grove x2), minotaur (Fanatic
  # devotion x2), goblins (Chainwhirler ping x2), knights (Adeline token per head), fivecolour
  # (Deathrite drain x2), fluctuator (Drannith Stinger's "whenever you cycle another card,
  # deals 1 damage to EACH OPPONENT" -- 2 per cycle under two heads, so the kill needs ~15 cycles
  # against 30 team life instead of 20 against 20), and angels -- which qualifies on a DIFFERENT
  # AXIS and is the reason this block was extended on 2026-09-20. Angels is the ONLY deck in the
  # repo carrying a `life_above_start_anthem_*` card (Righteous Valkyrie, and it plays FOUR), so
  # the conditional-anthem pass in SpellEffects.h (~3216-3246) -- read by every combat, board-eval
  # and SBA site through ComputeLordBonus -- had NO 2HG coverage anywhere. That pass compares life
  # against gamesetup::StartingLife() + 7, i.e. 27 at std but 37 at 2HG; a regression to the
  # literal 27 is INVISIBLE at std and catastrophic at 2HG, where starting life is already 30 and
  # the anthem would switch on at turn 0 with zero lifegain, permanently, for the whole team.
  # VERIFIED BY INJECTION, not assumed (2026-09-20): hardcoding the two compares in
  # SpellEffects.h to 20 + N moved angels2hg_smoke_d3_s1001 from 5.1900/46e221cf95fafbe2 to
  # 5.1000/e1fbb67860ed2f9a (the always-on anthem is worth ~0.09 turns) while leaving
  # angels_smoke_d3_s1001 BYTE-IDENTICAL at 4.6680/4df60477ed79b23d. Reverting restored both.
  # So this row is the only thing in the repo that fails on that bug. Not heads-scaling: the deck has no "each opponent"
  # effect the engine models (Lightstall Inquisitor's ETB is disclosed structurally inert), so this
  # case is bought entirely by the starting-life axis. Every OTHER deck gets a small SEARCHED canary -- d3 at 50 games (user,
  # 2026-09-04: d0 "isn't all that useful"; fewer games at search depth instead) -- so a
  # heads-plumbing break in the search/rollout path moves a committed digest too.
  "hinata2hg     3 1001   75 10"
  "antilife2hg   3 1001  100 10"
  "minotaur2hg   3 1001  100 10"
  "goblins2hg    3 1001   75 10"
  "knights2hg    3 1001   75 10"
  "fivecolour2hg 3 1001   40 10"
  "fluctuator2hg 3 1001   75 10"
  # angels2hg: antilife/minotaur sizing. Angels is one of the cheapest decks in the matrix
  # (d3 b10 ~0.066 s/game), so the whole relevant-tier treatment costs ~7 core-s here.
  "angels2hg     3 1001  100 10"
  "slivers2hg         3 1001 50 10"
  "burn2hg            3 1001 50 10"
  "th2hg              3 1001 50 10"
  "dragonstorm2hg     3 1001 50 10"
  "auras2hg           3 1001 50 10"
  "creature_giving2hg 3 1001 50 10"
  "mirrorwing2hg      3 1001 50 10"
  "stompy2hg          3 1001 50 10"
  "kitty2hg           3 1001 50 10"
  "dragons2hg         3 1001 50 10"
  "breaching2hg       3 1001 50 10"
  "critter2hg         3 1001 50 10"
  "melira2hg          3 1001 25 10"
  # fluctuator: free-cycling combo (Fluctuator makes every cycler {0}; Drannith Stinger turns each
  # cycle into a ping). Measured single-thread at the GATE budgets 2026-09-05: d0 0.0001 s/game,
  # d3 b10 1.34 s/game, d5 b20 2.81 s/game, with NO game over 30 s at any of them -- the heavy tail
  # this deck shows at b200 is budget-driven, not structural, so the gate sizing is safe. th/hinata
  # counts => ~7 core-min added to the pooled batch.
  "fluctuator 0 1001 1000 0"
  "fluctuator 3 1001  150 10"
  "fluctuator 5 1001   75 20"
  # giants: mono-red Giant tribal (Stinkdrinker cost reduction into Inferno Titan / Surtland
  # Flinger fling / Tectonic Giant modal). Measured single-thread at the GATE budgets 2026-09-22,
  # post-rebase: d0 1000 games in 2 ms (free), d3 b10 0.521 s/game, d5 b20 1.098 s/game, and NO
  # game over 30 s across 1400 games -- no heavy tail. th/hinata counts => ~2.7 core-min added.
  # Cost note: the deck carries TWO searched axes of its own (MTG_FLING_AXIS, MTG_TECTONIC_AXIS)
  # plus a tutor decline arm; measured at these budgets the two axes are +9% wall at d3 / +26% at
  # d5 and buy -0.060 / -0.067 turns, so the sizing above already includes them.
  "giants 0 1001 1000 0"
  "giants 3 1001  150 10"
  "giants 5 1001   75 20"
  # snow: ADDED 2026-09-20, at the USER's ask, and the sizing is the whole design question -- this
  # is one of the most expensive decks in the repo per game and its cost is strongly SEED-dependent
  # (measured d3 b10: 0.89 s/game at seed 1001 but 2.17-2.49 at the regression seeds; d5 b20:
  # 1.91 at 1001, 1.78-4.38 elsewhere). So the counts are sized per TIER off the tier's own seeds,
  # not off one measurement, and the d0 row is free (1000 games in 13 ms).
  #
  # THE BINDING CONSTRAINT IS THE SINGLE SLOWEST GAME, not the mean: a pooled batch's makespan
  # cannot go below its longest game, and Snow has d5 games up to 98 s at other seeds. At seed 1001
  # these exact counts were measured with ZERO games over the 30 s SLOW-GAME threshold, which is
  # why the smoke tier can afford full-sized rows where the overnight tier gets the tail instead.
  #
  # SIZE OFF A PROBE THAT RUNS WHAT THE SUITE RUNS. A standalone `--batch` probe priced these rows
  # at 89/96 core-s; in the tier they cost 185/195, because regression.sh passes
  # --lookahead-bottoming at every searched depth and the probe did not. Play is identical either
  # way (same avg, same digest) -- the 2x is bottoming-lookahead work alone. MEASURED IN THE TIER:
  # ~380 core-s added, smoke makespan 32s -> 46s. Budget is 15 min, so this is ~5% of it.
  #
  # WHY IT IS WORTH IT: Snow is the deck that ships breakpoint condemnation, and a condemnation
  # interaction silently deleted 54% of its value-leaf labels for four days (cd00879e) with nothing
  # to go red, precisely because Snow had no suite coverage. This closes that hole.
  "snow 0 1001 1000 0"
  "snow 3 1001  100 10"
  "snow 5 1001   50 20"
)

# regression: ~8-9 min pre-commit sweep -- two seeds at d3/d5, d0 single seed.
# GATE budgets: every deck at d3=10/d5=20 (NPV=900) = old units => BYTE-IDENTICAL to the
# pre-rebase GT, FULL game counts kept. Converged + stable; the deeper budgets are in OVERNIGHT
# (see SMOKE block). slivers must stay low regardless -- it spins on its heavy tail (s2002 d5
# was 16.5 min at budget 200) for zero benefit. d0 has no search. Only antilife is new here.
REGRESSION_CASES=(
  # fungus: Hinata's sizing (d0 full + d3/d5 at both seeds). 1,331.1s CPU = the suite's heaviest.
  # HALF OF IT IS SIX GAMES: seeds 2085/2031/3095 are pathological at BOTH d3 and d5 (d3_s2002
  # gi83 alone is 208s of that case's 507s). See docs/design/fungus-token-search-cost.md.
  "fungus  0 2002 1000 0"
  "fungus  3 2002  200 10"
  "fungus  3 3003  200 10"
  "fungus  5 2002  100 20"
  "fungus  5 3003  100 20"
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
  # breaching: kitty-shaped two-seed sweep at reduced counts (~0.38/0.56 s/game at the gate
  # budgets => ~7 min ST added, pools inside the existing makespan).
  "breaching 0 2002 1000 0"
  "breaching 3 2002  300 10"
  "breaching 3 3003  300 10"
  "breaching 5 2002  250 20"
  "breaching 5 3003  250 20"
  # critter: breaching-shaped two-seed sweep (~0.20/0.43 s/game at the gate budgets).
  "critter 0 2002 1000 0"
  "critter 3 2002  300 10"
  "critter 3 3003  300 10"
  "critter 5 2002  250 20"
  "critter 5 3003  250 20"
  # melira: restored 2026-09-08 (see the SMOKE block).
  "melira 0 2002 1000 0"
  "melira 3 2002   75 10"
  "melira 3 3003   75 10"
  "melira 5 2002   40 20"
  "melira 5 3003   40 20"
  # angels: ADDED 2026-09-18 (see the SMOKE block). ~77 core-s for this tier.
  "angels 0 2002 1000 0"
  "angels 3 2002  300 10"
  "angels 3 3003  300 10"
  "angels 5 2002  250 20"
  "angels 5 3003  250 20"
  # 2HG gate, second seed (see the SMOKE block for the design): the seven 2HG-relevant decks at
  # d3, plus ONE d5 case (hinata2hg -- the value_play/deep-search path under two heads, covering
  # the Crackle declared-count search where the second face changes lethality). No canaries here
  # (smoke owns them); the deep default-settings 2HG coverage is the OVERNIGHT block below.
  "hinata2hg     3 2002  100 10"
  "hinata2hg     5 2002   50 20"
  "antilife2hg   3 2002  150 10"
  "minotaur2hg   3 2002  150 10"
  "goblins2hg    3 2002  100 10"
  "knights2hg    3 2002  100 10"
  "fivecolour2hg 3 2002   50 10"
  "fluctuator2hg 3 2002  100 10"
  # angels2hg: antilife/minotaur sizing again (~10 core-s at 0.066 s/game).
  "angels2hg     3 2002  150 10"
  # fluctuator: gate budgets at both seeds (~14 core-min). See the SMOKE block for measured costs.
  "fluctuator 0 2002 1000 0"
  "fluctuator 3 2002  150 10"
  "fluctuator 3 3003  150 10"
  "fluctuator 5 2002   75 20"
  "fluctuator 5 3003   75 20"
  # giants: see the SMOKE block. Unlike Snow, this deck is NOT seed-sensitive -- the regression
  # seeds are actually CHEAPER than the smoke seed, so the smoke counts carry across unchanged.
  # MEASURED IN THE TIER'S OWN SEEDS 2026-09-22: d3 b10 63.3/61.0 core-s at s2002/s3003
  # (0.422/0.407 s/game), d5 b20 79.3/51.4 (1.057/0.685). ~255 core-s total; budget is 45 min.
  # No game over 30 s, so it does not become the pole.
  "giants 0 2002 1000 0"
  "giants 3 2002  150 10"
  "giants 3 3003  150 10"
  "giants 5 2002   75 20"
  "giants 5 3003   75 20"
  # snow: see the SMOKE block. The regression seeds are the EXPENSIVE ones for this deck -- d3 b10
  # measured 2.489/2.167 s/game and d5 b20 1.783/2.515 at s2002/s3003, ~2.5x the smoke seed -- so
  # the counts are cut to 60/30 rather than carried across from smoke. MEASURED IN THE TIER:
  # d3 119.7/113.9 core-s, d5 59.2/100.3, ~394 core-s total, makespan 69s -> 97s. Budget is 45 min.
  # The worst single game in the measured windows is 59.6 s, under the tier's own makespan, so Snow
  # does not become the pole.
  "snow 0 2002 1000 0"
  "snow 3 2002   60 10"
  "snow 3 3003   60 10"
  "snow 5 2002   30 20"
  "snow 5 3003   30 20"
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
  # fungus: the OVERNIGHT block is deliberately NOT here yet. Its sizing is ready
  # (scripts/fungus_regression_add.sh: d0 x4 @2000, d3 x4 @400 b10, d5 x4 @300 b20, ~1.8 CPU-hours
  # at this deck's measured rates), but adding cases without baselining them in the same change is
  # the exact gap d41561a0 was written to close -- it strands NEW keys for whoever runs that mode
  # next. It goes in with its baseline once the Fungus cost work settles, since that work moves
  # every digest anyway.
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
  # breaching: kitty-shaped 4-seed sweep at 2x gate budgets. Probed at the ACTUAL overnight
  # budgets 2026-09-03: d3 b20 ~0.43 s/game, d5 b40 ~0.60 s/game (the deck's search plateaus at
  # d3, so 2x budget costs almost nothing over the gates) => ~2.1 core-hours added. Counts obey
  # the seed-spacing rule (1000/500 < 1001).
  "breaching 0  4004 2000 0"
  "breaching 0  6006 2000 0"
  "breaching 0  8008 2000 0"
  "breaching 0 10010 2000 0"
  "breaching 3 4004 1000 20"
  "breaching 3 5005 1000 20"
  "breaching 3 6006 1000 20"
  "breaching 3 7007 1000 20"
  "breaching 5 4004  500 40"
  "breaching 5 5005  500 40"
  "breaching 5 6006  500 40"
  "breaching 5 7007  500 40"
  # critter: breaching-shaped 4-seed sweep at 2x gate budgets (~1.5 core-hours at the probe rates).
  "critter 0  4004 2000 0"
  "critter 0  6006 2000 0"
  "critter 0  8008 2000 0"
  "critter 0 10010 2000 0"
  "critter 3 4004 1000 20"
  "critter 3 5005 1000 20"
  "critter 3 6006 1000 20"
  "critter 3 7007 1000 20"
  "critter 5 4004  500 40"
  "critter 5 5005  500 40"
  "critter 5 6006  500 40"
  "critter 5 7007  500 40"
  # melira: restored 2026-09-08 (see the SMOKE block). The four-seed d3/d5 sweep that was
  # ~7.5 CPU-hours from one deck now projects to ~25 core-minutes.
  "melira 0 4004 2000 0"
  "melira 0 6006 2000 0"
  "melira 0 8008 2000 0"
  "melira 0 10010 2000 0"
  "melira 3 4004  200 10"
  "melira 3 5005  200 10"
  "melira 3 6006  200 10"
  "melira 3 7007  200 10"
  "melira 5 4004  150 20"
  "melira 5 5005  150 20"
  "melira 5 6006  150 20"
  "melira 5 7007  150 20"
  # angels: ADDED 2026-09-18 (see the SMOKE block). Full kitty/critter overnight shape -- it
  # projects to ~11 core-minutes against an 8 h tier budget, so there is nothing to trim for.
  "angels 0  4004 2000 0"
  "angels 0  6006 2000 0"
  "angels 0  8008 2000 0"
  "angels 0 10010 2000 0"
  "angels 3 4004 1000 20"
  "angels 3 5005 1000 20"
  "angels 3 6006 1000 20"
  "angels 3 7007 1000 20"
  "angels 5 4004  500 40"
  "angels 5 5005  500 40"
  "angels 5 6006  500 40"
  "angels 5 7007  500 40"
  # 2HG gate, deep tier (user request 2026-09-04): the six 2HG-relevant decks at DEFAULT
  # SETTINGS -- depth-5 rows drop the depth key so each deck's value_play block owns the play
  # depth (fivecolour runs d6), at the SAME per-deck overnight d5 budget as the deck's own rows.
  # Counts at ~1/3 of each deck's own d5 sample: the 2HG gate is a digest tripwire plus a real
  # deep-search 2HG behaviour sample, not a statistics tier (~55 min ST added, pools inside the
  # existing makespan). Seed spacing rule holds (all counts < the 1001 base spacing).
  "hinata2hg     5 4004 100 20"
  "hinata2hg     5 5005 100 20"
  "hinata2hg     5 6006 100 20"
  "hinata2hg     5 7007 100 20"
  "antilife2hg   5 4004 300 20"
  "antilife2hg   5 5005 300 20"
  "antilife2hg   5 6006 300 20"
  "antilife2hg   5 7007 300 20"
  "minotaur2hg   5 4004 150 40"
  "minotaur2hg   5 5005 150 40"
  "minotaur2hg   5 6006 150 40"
  "minotaur2hg   5 7007 150 40"
  "goblins2hg    5 4004 300 40"
  "goblins2hg    5 5005 300 40"
  "goblins2hg    5 6006 300 40"
  "goblins2hg    5 7007 300 40"
  "knights2hg    5 4004 300 40"
  "knights2hg    5 5005 300 40"
  "knights2hg    5 6006 300 40"
  "knights2hg    5 7007 300 40"
  "fivecolour2hg 5 4004 100 20"
  "fivecolour2hg 5 5005 100 20"
  "fivecolour2hg 5 6006 100 20"
  "fivecolour2hg 5 7007 100 20"
  "fluctuator2hg 5 4004  80 40"
  "fluctuator2hg 5 5005  80 40"
  "fluctuator2hg 5 6006  80 40"
  "fluctuator2hg 5 7007  80 40"
  # angels2hg: minotaur2hg's exact shape -- 150 games against the deck's own 500-game d5 rows,
  # at the same b40. This is the tier that actually exercises Righteous Valkyrie's anthem under
  # a 37-life threshold over a full game, rather than just tripping a digest. ~1 core-min.
  "angels2hg     5 4004 150 40"
  "angels2hg     5 5005 150 40"
  "angels2hg     5 6006 150 40"
  "angels2hg     5 7007 150 40"
  # fluctuator: breaching-shaped 4-seed sweep at 2x gate budgets. Probed at the ACTUAL overnight
  # budgets 2026-09-05: d3 b20 ~2.78 s/game, d5 b40 ~4.29 s/game, NO game over 30 s at either
  # => ~2.7 core-hours added, breaching's bracket. Counts obey the seed-spacing rule
  # (500/250 < 1001, 2000 < 2002).
  "fluctuator 0  4004 2000 0"
  "fluctuator 0  6006 2000 0"
  "fluctuator 0  8008 2000 0"
  "fluctuator 0 10010 2000 0"
  "fluctuator 3 4004  500 20"
  "fluctuator 3 5005  500 20"
  "fluctuator 3 6006  500 20"
  "fluctuator 3 7007  500 20"
  "fluctuator 5 4004  250 40"
  "fluctuator 5 5005  250 40"
  "fluctuator 5 6006  250 40"
  "fluctuator 5 7007  250 40"
  # snow: see the SMOKE block. This is the tier with room -- the whole Snow block is ~5,400 core-s
  # (1.5 core-h, doubling the bare-probe figure for --lookahead-bottoming as the smoke block
  # explains), i.e. ~170 s of makespan on 32 cores against an 8 h budget, so the deck gets its real
  # coverage here rather than in the two fast gates. NOTE these keys have NO GROUND TRUTH YET: they
  # are deliberately left to be created by the next overnight run, because a value leaf for this
  # deck would move every searched Snow row and re-accepting three tiers is cheaper once than twice.
  # Measured at these exact counts and seeds (bare probe, so ~2x in the tier):
  # d3 b10 1.756-2.445 s/game, d5 b20 2.786-4.376, worst single game 98.4 s (ovn_d5_s4004). Budgets
  # stay at the GATE levels (10/20) rather than the tier's usual 20/40 -- Snow's tail is
  # budget-driven (the d3/d5 b200 cells in the ledger reach 5-15 MINUTES per game), and this deck is
  # where a doubled budget stops being free.
  "snow 0 4004 1000 0"
  "snow 3 4004  150 10"
  "snow 3 5005  150 10"
  "snow 3 6006  150 10"
  "snow 3 7007  150 10"
  "snow 5 4004  100 20"
  "snow 5 5005  100 20"
  "snow 5 6006  100 20"
  "snow 5 7007  100 20"
)
