# Fluctuator: cast-order draft (2026-10-14)

Deck: `decks/Fluctuator/Fluctuator.cod` (42 cycling lands, 18 spells). Provider: `FluctuatorProvider : DeckProvider`.
Single main phase (`uses_second_main=no`, no `ClassifiesMainPhases` opt-in). The plan: Fluctuator makes every cycler
cost {0}, Drannith Stinger pings 1 per cycle, and the deck cycles the library in one turn. Unearth rebuys a Stinger that
cycling put in the graveyard. Hollow One is a free 4/4 after 3 cycles.

## 1. Status: (D), generic tiers only

- `FluctuatorProvider` (`src/ai/DecisionProviders.h:2018-2077`) does not override `CastOrderRank`, `CastOrderRankLatest`,
  `MainPhaseOverride` or `LandDropCastOrderRank`. Every rank comes from `GenericProvider::CastOrderRank`
  (`DecisionProviders.cpp:3013`).
- There is no order flag. The nine `MTG_FLUCT_*` flags all cover dig, fuel, rebuy and discard.
- No cast-order ruling exists in `docs/design/analysis-Fluctuator.md`, and `cast-order-rankings.md` has no Fluctuator
  section. So no USER order exists, and nothing the USER asked for is OFF.
- What the USER did state is the TURN SEQUENCE (ledger §7.5): "T2 untapped land + Fluctuator -> T3 cycle everything
  until Drannith Stinger is in hand, cast it if {1}{R} is available; otherwise cycle until Unearth is in hand and cast
  it". Other statements: "often you don't even want to play the third land, because it costs a cycling card" (§7.8),
  and "the only cards that need to be protected from cycling are Unearth and Drannith Stinger and occasionally an
  untapped land" (§7.5). I take these as binding input.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | the land drop | m1 | Generic: "no cantrip in the deck, so nothing wants to precede it -> FIRST". Searched (folded into the plan) at depth > 0. |
| 10 (TIE) | Drannith Stinger = Hollow One | report m2, plays m1 | Generic creature tier. Its stated reason is prowess, which this deck does not have. |
| 20 (TIE) | Enlightened Tutor = Unearth = Fluctuator | report m2, plays m1 | Generic "other noncreature". |
| (20) | Forsake the Worldly | - | `goldfish_inert`: never cast, only cycled. |
| after every cast | **the cycling chain (not ranked)** | m1 | The dig loop. See below. |

- Ties are settled by plan (hand) order, which nothing searches.
- The m2 column is inert. The deck has no second main, and the probe logs show every cast in MAIN_1.
- No card in the list is order-opaque (no draw / staging / cascade / retrace / impulse / solo-target param), so the
  rank sort applies to every cast set. Breakpoint condemnation is off for this provider (it does not opt into
  `CondemnsConsideredAtBreakpoint`), and so are the range ladder and m1-first (`MTG_ORDER_RANGE` and
  `MTG_ORDER_M1_FIRST` are off). The rank therefore has exactly one consumer here: `CastOrderLess` sequencing casts
  inside a window.

**How the cast order meets cycling (the question that actually matters for this deck).** Each main phase plays in
this sequence, both in the rollout (`TurnSolver.cpp:44434`, after the plan's casts) and in the executor
(`AIEngine.cpp:6786-6853`, recorded digs replayed after the casts):

1. The land drop.
2. The base plan's casts, sorted by rank.
3. The cycle chain. Each cycle draws a card. A NONLAND draw, or a pending Unearth rebuy (`DigResolveEvenOnLand`), opens
   a site-4 re-solve. That re-solve may play a held land drop and cast, in rank order. Then cycling resumes.

So the rank never puts a cast before or after a cycle:
- "Before cycling" means the base plan.
- "After N cycles" means a re-solve continuation.
- The rank only orders casts that happen at the SAME cycle count.

That splits the deck in two:
- **These must resolve BEFORE the chain, and they always do** (any base-plan cast precedes every cycle):
  - Fluctuator: cycles are free only once it is on the battlefield.
  - Drannith Stinger: it pings only for the cycles after it lands.
  - Enlightened Tutor: its to-top card is drawn by the chain's first cycle, i.e. THIS turn. The list has no same-turn
    shuffle that could bury it.
- **These want to come AFTER cycles, and no rank can put them there:**
  - Hollow One: {5} minus {2} per card cycled this turn, so {5} in any base plan and {0} after 3 cycles.
  - Unearth: it needs a Stinger that the chain cycled into the graveyard.
  - Both get there only through a re-solve. Seed 5022 T2 is the signature line: Capital City, cycle the Stinger for
    {1}, then Unearth it.
- Inside one window no card here affects another cast. There are no cast triggers, no ETB triggers on our side, and no
  spell cost reducer. So in-window order is purely a payment question:
  - A base plan with 2+ casts is paid in ONE joint solve (`BatchPrepayMainCasts`, `TurnSolver.cpp:37828`, executor
    `AIEngine.cpp:6207`), so its internal order cannot strand mana.
  - A continuation's casts are paid one at a time in rank order (`apply_plan_actions`).

## 3. Proposed order

The land drop stays FIRST among the window's casts. It can only add mana to them: Blasted Landscape and Capital City
enter untapped, and no cast in the list draws. The USER's principle 1 (draw before the land) does apply here, because
cycling IS this deck's draw. But a rank cannot express it: the draws are activations that run after every cast. The
"cycle first, then drop" line exists only as a held drop spent at a re-solve, and it is partly unreachable (§4.3, Q3).

| pos | card | main | range | why |
|---|---|---|---|---|
| land | the land drop | m1 | - | First among casts (see above). The search may hold it ("costs a cycling card"). |
| 1 | Unearth `{B}` | m1 | - | The deploy the chain waits on. It is legal only with a Stinger already in the graveyard. One coloured pip and no generic, so it pays before any generic pip can take its B source. |
| 2 | Enlightened Tutor `{W}` | m1 | - | Its card is drawn by the next cycle, so it must precede the chain (it always does). One W pip and no generic. Its order against Unearth cannot matter: different colours, no W/B dual in the list, and a fed Capital City is symmetric. |
| 3 | Drannith Stinger `{1}{R}` | m1 | - | The wincon. It must be on the battlefield before the first cycle, which is structural. Coloured, but carries a generic pip, so it comes after the zero-generic spells. |
| 4 | Fluctuator `{2}` | m1 | - | The engine. Its payoff is cycling, which runs after every cast, so no cast needs it earlier. Generic only, so it pays after the coloured spells. |
| 5 | Hollow One `{5}` - {2}/cycle | m1 | - | LAST. Its cost only falls with cycles, which come after this window. Inside a window its cost is fixed. Generic only. Its real slot, "the moment the 3rd cycle makes it free", is a re-solve question (§4.1, Q4). |
| - | Forsake the Worldly | - | - | Never cast (`goldfish_inert`: its only legal targets are our own Fluctuator / Hollow One). Cycling fodder. |

- **Ranges: none.** A range walks a card toward its cost-efficient slot when the ideal order cannot be paid. Here no
  cast changes another cast's cost. Hollow One's discount comes from cycles, and cycles are not slots, so a range has
  nothing to walk.
- **m1 for every card.** The deck plays one main. In a goldfish, casting or cycling after combat changes nothing: no
  haste, no blockers that matter, and the pings and attacks sum the same.

## 4. What changes and why it matters

Honest headline: **I found no game where the current rank loses a line.** I ran 30 single-game probes (seeds 5001-5030,
d3/b10 = the smoke cell shape, all with `--cards-json`). They averaged 3.6 turns, every game won T3-T5, and no
outcome traced to cast order. The rank changes below are for a correct reviewed order and for per-cast payment
robustness, and I expect them to be outcome-inert in base plans. The lines this deck actually loses sit in the
cast/cycle interleave, which the rank cannot reach. I list them because the brief asks how order meets cycling.

1. **Hollow One goes from the FIRST cast (10, tied) to the LAST.**
   - Inert in outcome: a base-plan Hollow One costs {5} at any rank.
   - What decides Hollow One is the re-solve. Seed 5007 T2 cast one free at cycle 6, then CYCLED two more free Hollow
     Ones as fodder (cycles 10 and 25) with no Stinger on board.
   - It is identical at d3b10, d3b200, d5b20 and d5b200, so it is not budget.
   - Seeds 5009 T3 and 5026 T3 do the same, and seed 5019 T3 left a free one in hand at the end of the chain. All four
     still won (T3/T4; the T4 chains alone took the opponent to -14 and -7), so the free 4/4 did not change a kill
     turn there.
   - `SelectDigSource` ranks Hollow One as the last fodder (rank 4). Whether it gets cast instead is up to the site-4
     continuation at the next nonland draw. A land draw never opens one.
2. **Coloured, zero-generic spells first (Unearth, Tutor), then Stinger, then generic-only.**
   - Replaces the two hand-order ties.
   - Only per-cast continuations can see it. Example: a re-solve at cycle 2 that casts Stinger plus a {1} Hollow One. If
     the {1} is paid first it can take the only R source.
   - Not observed in the probes. The payer's scarcity-first tapping and its sole-colour hold already guard most of it.
     This is robustness, not a measured fix.
3. **The land drop stays first. The line that loses is "cycle into the land, then drop it", and it is not a rank
   question.** This is a code read, not a measurement.
   - Setup: Fluctuator is out, the board is Polluted Mire + Irrigated Farmland (no red), and nothing else in hand is
     castable. The chain draws Capital City, then Drannith Stinger.
   - Dropping that Capital City casts the Stinger: feed {1} from the Mire for R, and the Farmland pays the {1}.
   - Neither draw opens a re-solve:
     - Capital City is a land, so `resolve_now` is false (`TurnSolver.cpp:44576`).
     - At the Stinger draw, `AnyHandCastableNow` (`TurnSolver.cpp:9421`) checks the current pool with the drop still
       open and finds nothing, so the loop continues.
   - Stop-on-threat then finds no reachable red route, so the chain keeps digging. It may cycle the very Capital City
     (lands are fodder rank 3, with no untapped-land protection).
   - Result: the line is unreachable at any budget. It is narrow: the deciding land must be one the chain draws, since
     a clairvoyant search already picks the right in-hand land up front. How often it occurs is unmeasured.
4. **Everything in the base plan is paid before the chain shows what it needs** (structural; no rank can fix it).
   - Seed 5019 T3: the base plan cast a second, saturated Fluctuator with both untapped lands. The chain then binned a
     Stinger with Unearth in hand and nothing left for {B}.
   - That game was T4 either way. With `MTG_NO_REDUNDANT_REDUCER=1` the cast is skipped and it is still T4, because the
     chain would have run dry. So this shows the mechanism, not a loss.
   - The lever is the plan's subset (and the built-but-unadopted no-redundant-reducer prune), not the order.
5. **Every card gets its own slot.** Today's two ties are orders nobody chose.

## 5. Questions for the USER

1. **Single main: every cast m1** (the report's m2 column is inert on this deck)? Default: **yes**.
2. **Order: land -> Unearth -> Enlightened Tutor -> Drannith Stinger -> Fluctuator -> Hollow One** (zero-generic pips
   first, Hollow One last)? The alternative is Fluctuator first ("enabler first", as in your T2/T3 line). Both play
   identically in base plans. Default: **approve as listed**.
3. **"Cycle first, then make the land drop"** (§4.3)? This would mean two things:
   - a re-solve when the drop is open and a land in hand makes a held card castable;
   - your "occasionally an untapped land" protection, built into `SelectDigSource`.

   It is a dig-loop change, not a rank, and would come with its own census and A/B. Default: **yes, census first**.
4. **Hollow One's real slot is "the moment it becomes free"** (§4.1). Should I investigate the free Hollow Ones that
   went uncast with no Stinger out? That happened in 4 of 30 probe games (3 cycled one as fodder, 1 left one in hand),
   and it was budget- and depth-invariant where I re-ran it. Nothing changes without a measurement. Default:
   **yes, investigate**.

## 6. Generic-order findings

- **The prowess rule ranks a self-discounting creature FIRST.** Tier 10's reason ("prowess catches later casts") puts
  Hollow One (`cost_less_per_cycle_or_discard`) before the very events that cheapen it. A card whose cost can only fall
  later in the turn, and which feeds nothing, belongs in a late tier (for example 25) after the same-turn casts. The
  argument is the inverse of principle 2: spells reduced by the turn's actions go after those actions. Affinity is the
  same class, but its same-turn credit (`SameTurnAffinityDiscountFor`, `TurnSolver.cpp:6276`) already reads the rank,
  so check that it stays consistent before generalising.
- **The report's land row ignores activated draws.** "No cantrip in the deck, so nothing wants to precede it -> FIRST"
  tells a dig deck's reviewer that the land-vs-draw question does not exist. Here the draws are cycling (and sac-draw
  lands elsewhere), and they run after every cast. The row should say so and point at the dig loop.
- **There is no pip-constraint tiebreak inside a tier.** Ties fall to hand order. For per-cast payment (continuations), a
  deck-agnostic "zero-generic / more pips first" key is a cheap robustness rule. It must be measured: `CastOrderLess`
  keeps cost out of creature ties on purpose (ETB order).
- **The m2 column on a single-main deck reads like a decision.** The header's `uses_second_main=no` is easy to miss.
  Print "(inert: single main)" in the column.

## 7. Implementation notes

- **New `FluctuatorProvider::CastOrderRank` + `CastOrderTierName`**, gated `MTG_FLUCT_ORDER`: default ON once the USER
  approves, `=0` falls back to `GenericProvider::CastOrderRank` for the A/B (the `MTG_DRAGONS_ORDER` precedent). Ranked
  by params:

  | slot | param test | rank |
  |---|---|---|
  | Unearth | `reanimate_creature_max_mv > 0` | 11 |
  | Enlightened Tutor | `tutor_to_top` | 12 |
  | Drannith Stinger | `cycle_trigger_damage_each_opponent > 0` | 13 |
  | Fluctuator | `reduces_cycling_activation > 0` | 14 |
  | Hollow One | `cost_less_per_cycle_or_discard > 0` | 16 |
  | Forsake the Worldly | `goldfish_inert` (never cast) | generic |
  | anything else | - | `GenericProvider::CastOrderRank` |

- **`OrderOpaqueCastsByRank`**: nothing here is order-opaque, so it is inert. Return true only as the adopted-order
  marker for the report, as Dragons does.
- **`MainPhaseOverride` -> Main1 for all**: report-only (no `ClassifiesMainPhases`, `MTG_ORDER_M1_FIRST` off). It makes
  the column match play.
- **`LandDropCastOrderRank` stays -1.** Its condition 1 ("holding the drop can never pay") is FALSE here. Every land is
  a free cycler, and the best drop can be a land the chain draws.
- **Not needed:** `CastOrderRankLatest` / ranges (no cast changes another's cost), and condemnation (not opted in).
- **Expected effect.** Base plans should be outcome-identical: joint payment, and no cast-to-cast interaction.
  Continuation casts can churn through per-cast payment. Validate on the `fluctuator` + `fluctuator2hg` smoke and
  regression cells to the usual bar.
- **Engine limitations that block the ideal order.** None of these is rank-expressible; each is a dig-loop item.
  1. Cycling is not a slot. The dig loop runs after every cast of the window (rollout `TurnSolver.cpp:44434`, executor
     `AIEngine.cpp:6786-6853`). "Fluctuator -> cycle x3 -> Hollow One" and "cycle -> drop the drawn land -> Stinger"
     can only happen as site-4 continuations.
  2. Re-solves fire only on a nonland draw or a pending rebuy (`TurnSolver.cpp:44576`;
     `FluctuatorProvider::DigResolveEvenOnLand`, `DecisionProviders.cpp:21524`). A land draw that makes a held card
     castable through the open drop does not fire one, and neither does Hollow One reaching {0}.
  3. `AnyHandCastableNow` (`TurnSolver.cpp:9421`), the gate behind `DigResolveOnlyWhenCastable`, ignores the open land
     drop. Its soundness note ("the re-solve cannot cast anything") holds only once the drop is spent.
  4. `SelectDigSource` (`DecisionProviders.cpp:21922`) has no "occasionally an untapped land" protection: every land is
     rank-3 fodder, ahead of Hollow One at 4.
- Probe artifacts (scratch, regenerable): `build/scratch/castorder/probe_Fluctuator/` (`run_probe.sh`, `scan.py`,
  `run_5019.sh`, `run_hollow.sh`). One early probe there, `20261014_083446_1001_game_0.json`, ran without
  `--cards-json` (no cards, unwon) and was discarded.
