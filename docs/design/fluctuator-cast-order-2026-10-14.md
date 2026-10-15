# Fluctuator: the USER's cast order, cycling placed last, and a land drawn by cycling as the drop (2026-10-14/15)

Base: `16dfb7e4` (work/integ-2026-10-14). Branch `worktree-agent-a162d1093980f7af0`. Deck `decks/Fluctuator/`
(42 cycling lands, 18 spells, single main). Draft and probes: `docs/design/cast-order-drafts-2026-10-14/Fluctuator.md`.

**USER, 2026-10-14:** *"I guess the Fluctuator order is okay. The order doesn't matter much in that deck, except that
cycling always goes at the end. (that should also be placed) Why can't we use a land drawn by cycling? We should be
able to consider it whether this is in a go-off heuristic or actual search. And yes, let's look into potential
problems."* Plus the cross-deck ruling: *"Let's fully order the cards in each list inasmuch as possible. I don't like
the randomness of choosing the order."*

## 1. What was built

| lever | default | what |
|---|---|---|
| `MTG_FLUCT_ORDER` (heurarm `FLUCT_ORDER`) | ON | the USER's TOTAL order (§2) |
| `MTG_FLUCT_DRAWN_LAND` (heurarm `FLUCT_DRAWN_LAND`) | ON | a land the dig chain draws is considerable as the drop (§3) |
| `MTG_FLUCT_KEEP_UNTAPPED` (heurarm `FLUCT_KEEP_UNTAPPED`) | ON | the USER's "occasionally an untapped land" protection, as an ORDER (§3.3) |
| `MTG_LANDDRAW_CENSUS` | off | diagnostic only: per real turn, a land drawn by a dig with the drop open (§5) |

Every one of the three play levers is `=0`-hatched and byte-identical to `16dfb7e4` when off (measured, §6).
`DecisionProvider::DigUsesDrawnLandDrop` is a new provider hook, **base false**, so every other dig deck
(treasure_hunt, FiveColour, Auras, Dragons, ...) is byte-identical by construction (and measured, §6).

## 2. Part 1 -- the order

`FluctuatorProvider::CastOrderRank` / `CastOrderTierName` / `ArrangeCastOrder` / `MainPhaseOverride` (Main1) /
`OrderOpaqueCastsByRank` (`src/ai/DecisionProviders.cpp`, "FluctuatorProvider::CastOrderRank").

| pos | card | param | rank |
|---|---|---|---|
| land | the land drop | structural: played before every cast | - |
| 1 | Unearth `{B}` | `reanimate_creature_max_mv > 0` | 64 |
| 2 | Enlightened Tutor `{W}` | `tutor_to_top` | 128 |
| 3 | Drannith Stinger `{1}{R}` | `cycle_trigger_damage_each_opponent > 0` | 192 |
| 4 | Fluctuator `{2}` | `reduces_cycling_activation > 0` | 256 |
| 5 | Hollow One `{5}` | `cost_less_per_cycle_or_discard > 0` | 320 |
| 6 | a SECOND Fluctuator | the same, with a copy on the battlefield (`CastOrderRank`) or earlier in the same cast set (`ArrangeCastOrder`) | 384 |
| last | CYCLING | not a rank -- see 2.1 | - |
| - | Forsake the Worldly | `goldfish_inert`: never cast; generic tier after slot 6 | 448+ |

**Totality.** The six slots are distinct, and `ArrangeCastOrder` re-sorts every cast set by (slot, NAME) after the
shared comparator, so no two different cards ever fall to plan/hand order -- including an unrecognised card a future
decklist adds (generic tier, after slot 6, by name). Pinned by `test_fluctuator_order.cpp`: all 360 permutations of
{Unearth, Tutor, Stinger, Fluctuator, Hollow One, Fluctuator} realise the same order.

### 2.1 Cycling placed last -- every site checked

Cycling is an activation, not a cast, so it cannot be a rank; it is last STRUCTURALLY, and every site that can cycle
was read for a path where a cycle precedes a cast of the same window:

| site | where | ordering |
|---|---|---|
| rollout | `ApplyPlanDirect`'s dig loop (`TurnSolver.cpp`, "Dig when stuck") | runs after the plan's casts; a cast after N cycles is a site-4 continuation = a NEW window whose casts again precede its next cycle |
| executor, committed line | `AIEngine::replay_recorded` (DigDraw) | the recorded DigDraws are replayed after the main plan's casts (end-of-main catch-all), each DigDraw's nested continuation (PlayLand, then casts) after its own cycle |
| executor, d0 / uncommitted | `AIEngine::UseSurplusLandAbilities` | called after the cast loop |
| searched dig axis | `Plan::dig_choice` | only gates whether the loop runs |
| breakpoint DigDraw replay | `breakpoint_casts` | recorded in rollout order |
| human play | `AppendHumanPlayDigPlans` | each cycle is a standalone single-DigDraw line appended after the cast plans; the viewer's `↻ cycle` badge is the human's own sequencing (deliberately not forced) |
| activation lists | `IsTrailingActivation` | DigDraw is not a trailing activation kind |

No site needed a change. `--cast-order-report` now prints the dig as the explicit `[LAST]` row (and marks the main
column inert on a single-main deck); `test_fluctuator_order.cpp` pins it behaviourally: a Stinger cast with a cycle
chain pings for EVERY card cycled.

## 3. Part 2 -- a land drawn by cycling is considerable as the drop

### 3.1 The four causes, verified against the code

1. **A land draw never opens a re-solve** -- `resolve_now = !drew_land || DigResolveEvenOnLand` (Fluctuator's only
   exception is the pending Unearth rebuy). CONFIRMED.
2. **`AnyHandCastableNow` ignores the open drop** -- it reads `AvailableManaPool` only and skips lands. CONFIRMED,
   with a twist: it uses `ManaPool::CanPay`, whose dual `wild` pays ANY pip, so on the draft's own §4.3 pair
   (Polluted Mire + Irrigated Farmland) it reads Drannith Stinger as ALREADY castable and the old loop re-solved there
   by accident (the base binary does find that exact board). On a board with no dual (Polluted Mire + Blasted
   Landscape), or when the City is drawn AFTER the Stinger, the line was unreachable at any budget: the scenarios
   `fluctuator_drawn_land_{casts,after}_stinger.json` give **no win** on `16dfb7e4` and a **T3 kill** now.
3. **The drop is lost** -- CONFIRMED, at every depth. A `uses_second_main=no` deck never runs a post-combat main
   (`AIEngine.cpp`: "a uses_second_main=no deck never runs one"), so the executor's depth-0 "play a land if a drop
   still remains" second-main branch is never reached, and at depth > 0 the search owns every land decision. The
   census (§5) shows it directly: at d0 EVERY turn whose dig drew a land with the drop open ended with the drop
   unused (5,393 of 5,393 turns, 40,000 games).
4. **`SelectDigSource` ranks every land as rank-3 fodder**, ahead of Hollow One (4). CONFIRMED.

### 3.2 The fix (provider hook, base false)

`DecisionProvider::DigUsesDrawnLandDrop(state)` -- true for Fluctuator under `MTG_FLUCT_DRAWN_LAND`. With it the dig
loop:

* **(1) re-solves at a land draw** when the drop is open and THAT land, played, makes a held nonland castable that
  is not castable now -- `TurnSolver::DropOpenLandEnablesCast(state, &land)`. Payment is COLOUR-EXACT (the real payer
  on a copy), so the pool's dual phantom cannot answer "already castable". A tapped-entering land never qualifies
  (34 of the 42 lands), so no nested re-solve fires where nothing can change;
* **(2) counts the open drop at a nonland draw**: the `DigResolveOnlyWhenCastable` skip now also requires
  `!DropOpenLandEnablesCast(state)` -- a Stinger drawn with an untapped Capital City in hand is not "uncastable";
* **(3) opens ONE site-4 re-solve at the chain's END** when the drop is still open and a land the CHAIN drew is
  still in hand (a land held at plan time was the plan's own land decision; nothing about it changed). This is the
  pre-Fluctuator case too: a {2} cycle that draws a tapped land, played now, is next turn's mana. If that re-solve
  changes the state (a land played, a card cast) the chain resumes -- an untapped land pays a {2} cycle, a cast
  Stinger makes every further cycle a ping. At most once per apply.

All three are SEARCHED breakpoints: the site-4 continuation list already carries every land-drop entry and the
explicit EMPTY ("play no land") world, and the variant machinery reaches them, so the engine chooses nothing
(NO-GREEDY rule). `HoldFuelWhileComboing` ("Don't play any fuel once you are going off. Just cycle everything.")
still skips all three while going off, exactly as it skips the cast re-solves, and its greedy land-drop site is
unchanged. Nothing is gated, so nothing becomes unfindable at `--budget-ms 0`; the new breakpoints are priced by the
budget like every other site-4 re-solve.

**Why a provider hook and not the shared loop:** the change adds breakpoints, which re-price every dig deck's search
even where it is sound; a hook keeps treasure_hunt / FiveColour / Auras / Dragons byte-identical by construction and
leaves each of them an opt-in with its own measurement.

**Rollout and executor stay in lockstep by construction:** the re-solves record their PlayLand + casts under the last
DigDraw, and the executor replays that script.

**The depth-0 runner's half (the "go-off heuristic" side of the USER's sentence).** d0 has no search and no re-solve
in its reactive dig (`AIEngine::UseSurplusLandAbilities`), so under the same hook, at depth 0 only, when the dig drew
a land and the drop is still open (and the hold-fuel land rule does not hold it), it plays the shared ranker's land --
the same greedy drop it made at the start of the main -- and requests the second pass, so the main re-solves with
that land's mana (the `LandDropAfterDrawEngine` replay's shape, measured on treasure_hunt). Greedy is permitted
there: it is the d0 runner.

### 3.3 The USER's "occasionally an untapped land" (`MTG_FLUCT_KEEP_UNTAPPED`)

*"The only cards that need to be protected from cycling are Unearth and Drannith Stinger and occasionally an untapped
land"* (ledger §7.5) was never built. The occasion is the open drop with no Stinger on the battlefield: ONE
untapped-entering land in hand (a colour source -- Capital City's filter -- over colourless Blasted Landscape, then
the lowest copy number) is ranked LAST in `SelectDigSource`. It is an ORDER, not a refusal: when it is the only
cyclable card it is still cycled, so no line is lost; it only stops being spent ahead of fodder that cannot be the
drop. Off once a Stinger is out ("everything is still free game") and once the drop is spent.

## 4. Part 3 -- the free Hollow Ones (seeds 5007 T2, 5009 T3, 5026 T3, 5019 T3)

Re-run on the BASE binary at d3/b10 with the continuation lists printed (`MTG_BP_CONT_TRACE=<turn>`) and at
**d8 / budget 0 (unlimited)**: every one of the four games is **action-for-action identical** at d3/b10 and d8/b0, and
wins on the same turn (5007 T3, 5009/5026/5019 T4).

| seed | turn | what happens | root cause |
|---|---|---|---|
| 5007 | T2 | no Stinger out (drawn, uncastable: tapped out). Hollow One #1 cast free at cycle 6; #2 cycled at cycle 10; a third, drawn at 25, cycled at the next cycle | the re-solve at the Fluctuator draw offered `#0 Hollow One + Hollow One`, `#1 Hollow One`; the committed line took #1 |
| 5009 | T3 | no Stinger out; two Hollow Ones drawn at cycles 11 and 14, each cycled at the very next cycle | the re-solve at each draw offered `#0 Hollow One` (the list's FRONT, i.e. the base plan's canon); the search took the EMPTY world instead |
| 5026 | T3 | no Stinger out; the Hollow One held from T2 becomes free at cycle 3 and is cycled at cycle 10 | same: offered at every nonland-draw re-solve, declined |
| 5019 | T3 | no Stinger out; a Hollow One drawn at cycle 6 (free) left in hand when the chain stopped (stop-on-threat) | offered at that draw's re-solve, declined |

**Verdict: FINDABLE and not chosen -- not a defect.** None of the three suspects is the cause: the hold-fuel rule
does not fire (it returns false with no Stinger on the battlefield, so every re-solve runs); the re-solve gates fire
(a nonland draw with a {0} Hollow One in hand is "castable"); and `SelectDigSource` only reaches the Hollow One (rank 4)
once the hand holds no land or Forsake. **The scoring:** in all four the kill turn is the same either way (the next
turn's chain alone took the opponent to -3, -14, -7 and -4), so turn-to-win scores casting and cycling EQUAL, and the
line that cycles digs one card deeper. That is a tie the metric cannot break -- and it is the opposite of the
USER's §7.7(c) preference ("hollow one should not be held the vast majority of the time": i.e. cast it, do not
spend it as fodder). See USER QUESTION 2.

## 5. Census (`MTG_LANDDRAW_CENSUS`) -- before / after

TBD (filled from the pooled batch).

## 6. Measurement

**Shape.** ONE pooled `mtg --batch` per round through the box lock, 20 threads, every arm of every cell in the same
queue, the arms set per job by heurarm flags (the `old` arm = the three levers OFF on the new binary, which §6.3 shows
is byte-identical to `16dfb7e4`). Held-out seeds from **11500001**, paired by `seed + gi`; loss = 9; `MTG_DUMP_WINS`
via `--game-log-dir`. Cells: **shipped** (no depth -> the profile's value_play policy, b20, value leaf + keep table
in place), **d3 b10**, **d0**. Sizes: fluctuator 8,000 / 12,000 / 40,000, fluctuator2hg half (twice the brief's
minimum). Units = the batch's deterministic search work (`units=`), as a ratio to `old`; wall is not reported (box
contention).

### 6.1 Round 1 (binary `a55e4201`+`df0a91bc`): the arms as specified

| deck | cell | arm | games | avg | faster | slower | net | play-changed | units |
|---|---|---|---|---|---|---|---|---|---|
| fluctuator | shipped | old | 8000 | 3.5594 | | | | | 1.0000 |
| | | p1 (order) | | 3.5591 | 2 | 0 | **-2** | 500 | 0.9989 |
| | | p12 (order + drawn land + keep-untapped) | | 3.5576 | 42 | 27 | **-14** | 1192 | 1.1424 |
| | | p12 without keep-untapped | | 3.5572 | 36 | 19 | -17 | 843 | 1.1256 |
| | | p12 + NO_REDUNDANT_REDUCER (report only) | | 3.5530 | 72 | 21 | -51 | 1652 | 1.0673 |
| | d3 b10 | old | 12000 | 3.5756 | | | | | 1.0000 |
| | | p1 | | 3.5756 | 3 | 3 | **0** | 785 | 0.9989 |
| | | p12 | | 3.5745 | 74 | 62 | **-13** | 1739 | 1.0554 |
| | | p12 without keep-untapped | | 3.5744 | 59 | 45 | -14 | 1318 | 1.0469 |
| | | p12 + NRR | | 3.5704 | 128 | 68 | -62 | 2489 | 1.0124 |
| | d0 | old | 40000 | 5.1582 | | | | | - |
| | | p1 | | 5.1633 | 680 | 888 | **+204** | 7351 | - |
| | | p12 | | 5.1634 | 682 | 892 | **+207** | 7390 | - |
| | | p12 + NRR | | 5.1472 | 1586 | 1322 | -440 | 14672 | - |
| fluctuator2hg | shipped | old | 4000 | 3.5017 | | | | | 1.0000 |
| | | p1 | | 3.5017 | 0 | 0 | **0** | 266 | 0.9979 |
| | | p12 | | 3.5000 | 20 | 13 | **-7** | 577 | 1.1685 |
| | | p12 + NRR | | 3.4955 | 34 | 10 | -25 | 777 | 1.0941 |
| | d3 b10 | old | 6000 | 3.5260 | | | | | 1.0000 |
| | | p1 | | 3.5255 | 4 | 1 | **-3** | 408 | 0.9970 |
| | | p12 | | 3.5255 | 38 | 35 | **-3** | 863 | 1.0533 |
| | | p12 + NRR | | 3.5210 | 61 | 32 | -30 | 1206 | 1.0152 |
| | d0 | old | 20000 | 5.0484 | | | | | - |
| | | p1 | | 5.0518 | 347 | 430 | **+67** | 3440 | - |
| | | p12 | | 5.0518 | 349 | 432 | **+68** | 3460 | - |
| | | p12 + NRR | | 5.0339 | 813 | 617 | -290 | 6592 | - |

**The order alone (p1) FAILS the bar at d0** (+204 and +67) while it is neutral at every searched cell -- the
draft's "outcome-identical in base plans" was wrong at depth 0. The drawn-land fix adds nothing at d0 in round 1
(the d0 runner had no route to it yet; see 6.2) and is a gain at every searched cell.

### 6.2 Root cause of the order's d0 loss -- and the amendment

Read on d0 slower games gi 4, 38, 97, 183 (both arms, same seed, `MTG_SOLVE_PROJ_TRACE`):

* gi 4, T3, Capital City + two Blasted Landscapes, Fluctuator + Unearth (a Stinger in the yard) + Tutor in hand.
  The d0 greedy scores `{Fluctuator, Unearth}` at eval 200 (`supply=3`) -- but the real payer can pay ONE of them:
  the City's {B} needs a Landscape to feed it, leaving one land for Fluctuator's {2}. The payer casts the first-ranked
  card and drops the rest. Generic order (tie at 20, plan order): Fluctuator; the USER's order: Unearth. T4 -> T5.
* gi 38 / 97 / 183: the same shape with `{Enlightened Tutor, Drannith Stinger}` -- the Tutor (slot 2) survives,
  the Stinger (slot 3) is dropped, the kill slips a turn.

**Root cause (a modelling defect, not the order):** the flat pool -- and the colour-exact affordability test on top
of it -- books Capital City's FED colour as a unit ON TOP OF the land that feeds it. The conversion is net zero (feed
{1}, get one coloured), so a board of City + N other lands has N+1 mana only colourless, and N with one coloured; the
pool credits N+1 with one coloured. `AnyColorFilterFedSlots` caps the NUMBER of conversions but not this. Every set the
d0 greedy (and the beyond-horizon playout) admits on that phantom is paid in rank order and loses its tail -- so on this
deck the rank decides WHICH card is cast, not only when. The draft's "zero-generic pips first" assumed the set is paid.

**The amendment (`MTG_FLUCT_ORDER_FUNDED`), which keeps the USER's order wherever the set can be paid:** in
`ArrangeCastOrder`, walk the set with the REAL payer on a copy in the USER's order; if it pays in full nothing changes;
if it does not, Unearth and Enlightened Tutor move after Hollow One, so the survivors of an unpayable set are the
Stinger and the Fluctuator. (A cast-order RANGE cannot do this: the range ladder projects payability with the same
over-crediting pool, so it reads the phantom set as paying at its ideal rung.) Measured in round 2 (§6.4).

ROUND 2 also adds the d0 runner's drawn-land drop (§3.2) -- round 1's census showed the drop is lost at d0 too.

## 7. Defaults as shipped and open USER questions

TBD.
