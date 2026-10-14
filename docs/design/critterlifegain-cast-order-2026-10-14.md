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

## RESUME (2026-10-14, stopped for the night at the coordinator's request -- the box was needed)

**Base commit:** ff3e205b (work/integ-2026-10-14 tip). Branch `worktree-agent-a239469ed898c5c26`, local only.
Commits: af7434cc (the order, default ON), fb96134a (diag trace note), the WIP commit holding this note and
`scripts/critter_order_ab/`.

**Built (build volume, regenerable):** `build/scratch/critter-order/bin/mtg.base` (ff3e205b), `mtg.new` (af7434cc),
`mtg.diag` (fb96134a, play-identical to mtg.new + the trace note). Unit suite 1061 / 1061 (8 new cases).

**Measured -- COMPLETE:**
* Probe boards (draft section 4 + four new Heliod / Ajani boards; `scripts/critter_order_ab/run_probes.sh`): Thune
  probe 8 -> 14 counters at d3 b50 and unlimited; flip probe Pridemate first 6/6 (4 life) vs 5/5 (5 life), and at
  opponent life 20 d5 b0 the new order wins T6 where base wins T7; Heliod "needs Ocelot" 4 -> 6; Heliod dev-4
  (bp9 off) 6 -> 8; Ajani -2 now cast between Pridemate and Heliod. **Finding for the USER:** on the walk-past-2
  board (Warden + Attendant out, hand Heliod + Ocelot + Serra) the walked order gives Heliod 2 counters where the
  old Heliod-first gave 4 (both win T6): walking Heliod until it is a creature trades every gain event of the
  creatures it waits for against the watchers firing on its own entry. Same shape on the two-way boards (the
  canonical after-the-creatures order: 0-1 counters vs 1-3). Whether that costs turns is what the A/B decides.
* References (`test/viewer_protocol_check.py --strict --only CritterLifegain`): base and new identical, 29 ok, 1
  repaired, 0 drift / enum-gap / board-diverged (30 refs incl. v1). `scripts/ref_bench.py`: search 4.350 (20 v2) /
  4.600 (10 v1) on both arms, 0 / 30 short of the human.

**Measured -- PARTIAL (the pooled A/B was stopped part-way; both arms' scratch is in
`build/scratch/critter-order/ab_base|ab_new/`, batch.out / batch.err):**
* critter shipped (d5 b20, profile attached): 4000 / 4000 games paired -- 4.3652 -> 4.3645, **3 faster / 0 slower
  (net -3)**, units 1.009x.
* critter2hg shipped: 518 / 2000 paired -- 0 / 0, units 1.21x (PARTIAL).
* critter d3 b10, d0; critter2hg d3, d0: NOT RUN (queued behind the shipped cells).
* Hatch (`MTG_CRITTER_ORDER=0` on the new binary vs base): 8 / 8 chunks digest-identical, 400 / 400 games (PARTIAL).
* Identity (every other suite deck): 9 / 9 jobs identical so far (PARTIAL; the shipped jobs of 9 decks).
* Heliod case census (executor lists holding Heliod + another cast, `MTG_USER_ORDER_TRACE` notes; PARTIAL, about
  2,850 shipped games): critter 120 lists -- at slot 91, walked1 6, walked2 0, two-way 23 (after 13, searched FIRST
  10); critter2hg 60 lists -- at slot 52, walked 0, two-way 8 (after 7, FIRST 1). Flip lists (live flip with a payoff
  and a watcher both cast): critter 26, critter2hg 21.

**Left:**
1. Re-run the pooled A/B in full: `bash scripts/critter_order_ab/run_ab.sh 6` (builds both manifests: critter
   ship 4000 / d3 6000 / d0 20000, critter2hg half; identity 20 / 20 / 40 games per cell for every other suite deck
   at seeds 11500001 + 1000 x deck + 300 x cell; hatch on the first 400 / 600 / 2000 critter games; seeds 11200001 +
   100000 x k + 30000 x cell + gi; chunks of 50). The keep tables are now cached for every deck path it uses, so
   parse takes seconds. Analyse: `python3 -I scripts/critter_order_ab/make_ab.py analyse <ab_base> <ab_new>`
   (writes `<ab_new>/moved.json`).
2. Recovery of every slower game: `python3 -I scripts/critter_order_ab/recovery.py build <ab_new> <dir>`, run its
   manifest on `mtg.new` (MTG_DUMP_WINS=1, cwd = the worktree), then `recovery.py analyse`.
3. Heliod census in full: `bash scripts/critter_order_ab/run_census.sh 3` (mtg.diag; the count parser is fixed --
   card names carry spaces).
4. Root-cause any unrecovered slower game; the walk finding above is the first suspect.
5. The rest of this doc (measurement, defaults) and the CritterLifegain section of `cast-order-rankings.md`.

**Open questions for the USER (taking the conservative default meanwhile):**
* The flip moves the WHOLE payoff block, Ajani -2 included (the -2 token is a Pridemate, and leaving it behind would
  break "right after Pridemate"). Default: yes.
* Two-way FIRST means position 0 (before every watcher), as the coordinator specified; the canonical order is after
  the creatures.
