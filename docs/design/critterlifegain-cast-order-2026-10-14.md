# CritterLifegain: the USER-settled cast order (2026-10-14)

`MTG_CRITTER_ORDER` (heurarm slot `CRITTER_ORDER`). Draft and probes:
`docs/design/cast-order-drafts-2026-10-14/CritterLifegain.md`. Code: `CritterLifegainProvider`
(`src/ai/DecisionProviders.cpp`, the block after `LegendKeepIndex`). Unit cover:
`test/unit/test_critterlifegain_cast_order.cpp`.

## 1. The ruling

USER, verbatim: *"For CritterLifegain Ajani, Strength of the Pride should go after Ajani's pridemate because it
produces them. The activation that produces them should go right after, whereas the +1 can go where you put it
originally. Heliod is a trickier one and might require some adjusting. It can go right where it is if it is a
creature. Otherwise it probably should go after other creatures until it is one. I'm not entirely sure on that,
though. If it isn't a creature after the next 2 have entered then it might actually be better to play it before.
This would be rare, but we could search or evaluate this case. Otherwise it seems fine."*

And for every deck: *"Let's fully order the cards in each list inasmuch as possible. I don't like the randomness of
choosing the order."*

## 2. The order as implemented (TOTAL, everything in main 1)

| slot | rank | card(s) | rule (card params, never names) |
|---|---|---|---|
| - | land | the land drop | first (no draw in the deck; unchanged) |
| 10 | 641 | Soul Warden, Soul's Attendant | enter-WATCHER (`any_creature_enters_lifegain` / `own_creature_enters_lifegain`), mv 1, then name |
| 10 | 642 | Auriok Champion, Daxos | enter-WATCHER, mv 2, then name |
| 11 | 709 | Archangel of Thune | TEAM payoff (`lifegain_each_own_creature_counters`) |
| 12 | 810 | Voice of the Blessed | UNIT payoff (`lifegain_self_counters`), MORE WHITE PIPS first ({W}{W}: devotion for Heliod) |
| 12 | 818 | Ajani's Pridemate | UNIT payoff, one white pip |
| 13 | 836 | Ajani, Strength of the Pride **cast for its -2** | `CastOrderActionRank`: the cast's `loyalty_ability` is the one whose effect makes the Pridemate token |
| 14 | 899 | Heliod, Sun-Crowned | DEVOTION god (`creature_requires_devotion`): its static slot; placed by `ArrangeCastOrder` (section 3) |
| 15 | 961 | Ocelot Pride, Serra Ascendant | other creature, mv 1, then name |
| 15 | 963 | Ranger-Captain of Eos | other creature, mv 3 |
| 16 | 1028 | Ajani **cast for its +1 or with no activation** | walker: the slot the draft gave it |
| 17 | 1090 | Unexpectedly Absent | everything else |

* **Totality.** rank = slot x 64 + sub (sub = mana value; for the unit payoffs (7 - white pips) x 8 + mv). Two
  different names on one rank fall to the NAME (lexicographic) -- the shared comparator through the new
  `DecisionProvider::CastOrderTiesByName` (default false, so every other deck is byte-identical), and again at the
  top of `ArrangeCastOrder`, so the sites that sort by the bare action rank (`SubsetPayableSequential`) agree. Name
  order happens to be the USER's within every tie: "Soul Warden" < "Soul's Attendant" (space before apostrophe),
  "Auriok Champion" < "Daxos", "Ocelot Pride" < "Serra Ascendant". Never plan or hand order: the unit test permutes
  the plan 26 ways and the realised order is the same each time.
* **The flip (draft section 3, kept: "Otherwise it seems fine").** With the board holding at least 2 more watchers
  than payoffs and no team payoff on it, the payoff block -- Thune, Voice, Pridemate, Ajani -2, Heliod -- keeps its
  internal order and moves ahead of the new watchers (slots 4, 5, 6, 7, then the watchers at 9). Census over the
  active player's battlefield: watchers = creatures with the watcher params; payoffs = `lifegain_self_counters`
  (the Ajani token resolves to the real Pridemate entry) or `lifegain_target_own_counter` (a non-creature Heliod
  counts); any `lifegain_each_own_creature_counters` permanent turns the flip off.
* **Main 1.** `MainPhaseOverride` returns Main1 for every card (the deck has `uses_second_main=no`, so this is
  play-inert; the report stops showing m2 for Serra Ascendant and Heliod).
* **Report.** `CastOrderTierName` labels every rank (the report printed "(unclassified)" for 8 and 9).
* **`MTG_CRITTER_ORDER=0`** restores the 2026-09-08 mined order (`MTG_CRITTER_WATCHER_ORDER`: watchers 8,
  Pridemate / Voice 9, Generic for the rest) with no arrangement, no name tie-break, no main-phase pin and no
  ordering search -- byte-identical to ff3e205b (measured, section 5).

## 3. Heliod

`ArrangeCastOrder` (both apply worlds, through `ApplyDeckCastArrangement`: executor `TakeTurn`, rollout
`ApplyPlanDirect`, breakpoint continuations, `PlanCastOrder`'s projection) places the first devotion god of a cast
list:

* Devotion at a position = the board's white devotion (`DevotionTo`: permanents' printed pips, a planeswalker's
  included, tokens none) + the pips of every permanent spell cast before it + Heliod's own pip -- exactly what
  `RefreshDevotionCreatures` evaluates when it enters (the refresh runs above the enter-watcher gate, so a Heliod
  entering at devotion 5 enters as a creature and every watcher fires on it).
* **AT SLOT:** a creature at its static slot (after Ajani -2) -> it stays.
* **WALKED (1 or 2):** otherwise it moves right after the 1st or 2nd later CREATURE entry of the list at which it
  would enter as a creature.
* **TWO-WAY:** if it still would not be a creature after those 2, the canonical order puts it after the LAST
  creature of the list, and the search ALSO tries it FIRST (before every watcher: a non-creature enchantment on the
  battlefield for every gain event). `WantsCastOrderingSearch` opens the cast-ordering expansion for this deck and
  `CastOrderingCandidates` returns the two orders -- after-the-creatures first -- ONLY for a plan in this case;
  every other plan comes back untouched (the hook returns true with no orderings, and the expansion then keeps the
  base plan as built). The d0 runner and the playout beyond the horizon take the canonical order (the permitted
  greedy sites). Under the SearchOrder gate (human play, `MTG_UNPRUNED`) the opt-in stands down, so the viewer keeps
  the full permutation fan exactly as before.
* `CastOrderRankLatest(Heliod)` = the end of the creature block, so a breakpoint inside that range never condemns a
  Heliod in hand as already passed.

## 4. Ajani

The cast carries its same-turn activation (`Action::loyalty_ability`); both worlds apply it right after the walker
resolves and the legend rule runs (executor `walker_cast_activation` after `cast_by_name` + `resolve_now`, rollout
`ApplyCastLoyaltyActivation` inside the cast's apply), before the next cast -- so a -2 cast at slot 13 makes the
Pridemate token there, ahead of Heliod and the bodies, and every watcher on the battlefield fires on the token.
Which ability makes the token is read off `loyalty_abilities[i].effect`, never the name.

## 5. Measurement

Paired per game (seed of game gi = 11200001 + 100000 x k + 30000 x cell + gi; critter k = 0, critter2hg k = 1 at
starting life 30 / two heads), loss scored 9, held-out seeds. Cells: **shipped** (no depth / budget key: the
deck's d5 b20, profile attached so the value leaf and the keep table are in place), **d3 b10**, **d0**. Scripts:
`scripts/critter_order_ab/` (`make_ab.py` day 1, `make_ab2.py` / `run_ab2.sh` the resumed pooled batch,
`recovery.py` / `run_recovery.sh`); scratch under `build/scratch/critter-order/`.

* Day 1 (2026-10-14) ran critter shipped as two binaries side by side (ff3e205b vs af7434cc).
* The resumed run (2026-10-15, through the fleet's box lock, `--threads 20`) is ONE pooled batch holding both arms:
  the old arm is `MTG_CRITTER_ORDER=0` per job on the new build. That arm is byte-identical to ff3e205b,
  checked chunk by chunk against the real base binary in every cell (below).

| deck | cell | games | old | new | new - old | t | faster | slower | **net** | work units old -> new |
|---|---|---|---|---|---|---|---|---|---|---|
| critter | shipped d5 b20 | 4000 | 4.3652 | 4.3645 | -0.00075 | -1.73 | 3 | 0 | **-3** | 5.41M -> 5.46M (1.009x) |
| critter | d3 b10 | 6000 | 4.3742 | 4.3735 | -0.00067 | -2.00 | 4 | 0 | **-4** | 41.24M -> 41.30M (1.002x) |
| critter | d0 | 20000 | 4.6053 | 4.6044 | -0.00095 | -3.31 | 26 | 7 | **-19** | - |
| critter2hg | shipped d5 b20 | 2000 | 4.6640 | 4.6635 | -0.00050 | -1.00 | 1 | 0 | **-1** | 5.70M -> 5.71M (1.002x) |
| critter2hg | d3 b10 | 3000 | 4.6817 | 4.6817 | 0 | 0.00 | 1 | 1 | **0** | 26.15M -> 26.17M (1.001x) |
| critter2hg | d0 | 10000 | 4.8108 | 4.8096 | -0.00120 | -3.47 | 12 | 0 | **-12** | - |

Per-deck net, all cells: **critter -26, critter2hg -13**. Work is flat (at most 1.009x).

**Identity.**
* `MTG_CRITTER_ORDER=0` vs ff3e205b:
  * 81 / 81 chunks of the five resumed cells (the first 300 / 600 / 2000 games, half for 2HG) digest-identical;
  * the first 518 critter2hg shipped games 9 / 9 chunks;
  * day 1's critter cells 8 / 8 chunks.
* The diag build (fb96134a, the trace note) plays exactly af7434cc: 11 / 11 chunks.
* Every other suite deck, ff3e205b vs the new build, at d0 (40 games), d3 b10 (20) and shipped (20):
  **96 / 96 jobs identical** in digest and average. Work units differ by under 0.3% on the three KittyEquipment
  lists with identical play (the shared-cache capacity effect seen in earlier A/Bs, not this change).

**References.**
* `test/viewer_protocol_check.py --strict --only CritterLifegain`: both binaries 29 ok, 1 repaired, 0 play-drift /
  enum-gap / board-diverged / shuffle-dead (30 refs incl. the archived v1 list).
* `scripts/ref_bench.py` (the shipped search on each reference's opening hand): search 4.350 (v2, 20) / 4.600 (v1,
  10) on both arms, 0 / 30 short of the human.

**Probe boards** (the draft's, plus Heliod / Ajani boards; `run_probes.sh`, old -> new):
* Thune + Ocelot 8 -> 14 counters at d3 b50 AND unlimited (the attack 6 -> 10).
* The flip board Pridemate 5/5 -> 6/6 (life 5 -> 4); at opponent life 20 and d5 b0 it wins T6 where the old order
  wins T7.
* Heliod needing Ocelot 4 -> 6.
* Heliod at devotion 4 6 -> 8.
* Ajani -2 now cast between Pridemate and Heliod.

## 6. Every slower game

| deck | cell | gi | seed | old | new | stage 1 (`--depth max(5, old) --budget-ms 100`) | stage 2 (`--depth 8 --budget-ms 0`) | mechanism |
|---|---|---|---|---|---|---|---|---|
| critter2hg | d3 b10 | 210 | 11330211 | 4 | 5 | **4** | 4 | T2 plan differs (new: Attendant x2, old: Ocelot) -- search churn |
| critter | d0 | 269 | 11260270 | 4 | 5 | **4** | 4 | Heliod two-way, canonical [Ocelot, Heliod] vs old [Heliod, Ocelot] |
| critter | d0 | 1239 | 11261240 | 5 | 6 | **5** | 5 | Heliod two-way, [Serra, Heliod] vs [Heliod, Serra] |
| critter | d0 | 2384 | 11262385 | 4 | 5 | **4** | 4 | the FLIP: Heliod (a creature at its flipped slot) before Auriok Champion; one more counter, one less life on a board with two Serra Ascendants (the 30-life clause) |
| critter | d0 | 4871 | 11264872 | 4 | 5 | **4** | 4 | Heliod two-way, [Ocelot, Heliod] |
| critter | d0 | 5562 | 11265563 | 4 | 5 | **4** | 4 | Heliod two-way, [Ocelot, Heliod] |
| critter | d0 | 13045 | 11273046 | 5 | 6 | **5** | 5 | Heliod two-way, [Serra, Heliod] |
| critter | d0 | 15923 | 11275924 | 5 | 6 | **5** | 5 | Heliod two-way, [Serra, Heliod] |

**All 8 recover at stage 1.** The bar is met.

Six of the seven d0 games are one mechanism. In Heliod's two-way case the d0 runner takes the canonical order,
"after the creatures". With one body after it, Heliod is not a creature there either, so it simply misses that
body's gain events, where the old plan order happened to cast it first. That observation is the proposed
amendment in section 8.

## 7. How often each case occurs

Executor cast lists holding Heliod and at least one other cast (the `MTG_USER_ORDER_TRACE` notes; critter shipped from
day 1's ~2,000-game census, every other cell from the resumed batch's new arm):

| deck | cell | at slot | walked past 1 | walked past 2 | two-way: after (canonical) | two-way: FIRST (searched pick) | flip live (payoff + watcher cast) |
|---|---|---|---|---|---|---|---|
| critter | shipped (~2000 g) | 91 | 6 | 0 | 13 | 10 | 26 |
| critter | d3 b10 (6000) | 270 | 19 | 0 | 34 | 15 | 86 |
| critter | d0 (20000) | 926 | 44 | 0 | 64 (+1 single-position) | - (no search) | 337 |
| critter2hg | shipped (2000) | 104 | 3 | 0 | 10 | 3 | 39 |
| critter2hg | d3 b10 (3000) | 168 | 3 | 0 | 17 | 2 | 64 |
| critter2hg | d0 (10000) | 481 | 18 | 0 | 28 | - | 192 |

* About 85% of Heliod lists are a creature at its slot. The walk is about 4% and never needs the second creature.
* The two-way case is about 10%. At searched depths the search picks FIRST in about a third of them.

## 8. Proposed amendment: `MTG_CRITTER_HELIOD_FIRST` (default OFF -- the USER's call)

* **The rule.** In the two-way case, when Heliod would not be a creature even after the LAST creature of the list,
  FIRST dominates after-the-creatures:
  * it is a non-creature enchantment either way, so no watcher fires on its entry in either;
  * FIRST is on the battlefield for every gain event of the turn;
  * nothing else in the deck reads its position.
* **What changes.** The canonical order becomes FIRST, and there is nothing left to search. The two-way case where
  after-the-creatures DOES make it a creature (a 3rd+ creature) keeps the USER's searched choice.
* **Built** in 116810b5, with a unit case.
* **Measurement:** see 8a.

## 9. Defaults as shipped and questions for the USER

* **`MTG_CRITTER_ORDER` ships default ON.** It meets the bar: every deck's net <= 0 in every cell (critter -26,
  critter2hg -13), every slower game recovers at stage 1, play is byte-identical for every other deck, the `=0`
  hatch restores ff3e205b exactly, and work is flat.
* **`MTG_CRITTER_HELIOD_FIRST` ships default OFF** until the USER rules (section 8).
* **Questions:**
  1. Adopt `MTG_CRITTER_HELIOD_FIRST`? It is a dominance, and it is aimed at exactly the six d0 slower games.
  2. The flip moves the whole payoff block, Ajani -2 included, so the token stays "right after Pridemate". Kept.
  3. Two-way FIRST = position 0, before every watcher.
  4. The walk's counter cost: on the walk-past-2 probe board, Heliod after Ocelot and Serra gets 2 counters where
     Heliod first gets 4 (both win T6). In play, walked-past-2 never occurred (section 7), and walked-past-1 lost
     no game. One d0 slower game (gi 2384) comes from the flip interacting with Serra Ascendant's 30-life clause
     (one less life); it recovers at stage 1.
