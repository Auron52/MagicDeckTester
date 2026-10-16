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
| `MTG_FLUCT_ORDER_FUNDED` (heurarm `FLUCT_ORDER_FUNDED`) | ON | the amendment that makes the order meet the d0 bar: a set the payer cannot pay in full keeps the Stinger / Fluctuator (§6.2, §6.4). USER question |
| `MTG_DIG_DRAWN_LAND_D0` (heurarm `DIG_DRAWN_LAND_D0`) | **OFF** | the depth-0 runner half of the drawn land: measured worse at d0 (§6.4) |
| `MTG_FLUCT_KEEP_NEEDED` (heurarm `FLUCT_KEEP_NEEDED`) | ON | amendment to keep-untapped: the land is kept only while it is what pays the Stinger (§6.7a) |
| `MTG_DIG_HOOK_BPALL_EXEMPT` (heurarm `DIG_HOOK_BPALL_EXEMPT`) | ON | a drawn-land hook site is exempt from the bp_all uniform policy (§6.7b); inert without the hook |
| `MTG_LANDDRAW_CENSUS` | off | diagnostic only: per real turn, a land drawn by a dig with the drop open (§5) |
| `DecisionProvider::CastOrderTotalByName` | Fluctuator: with the order | the canonical name tie-break (exact copy of integration `2ac8db0b`); play-neutral here |

Every play lever is `=0`-hatched and byte-identical to `16dfb7e4` when off (measured, §6).
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
there: it is the d0 runner. **Measured worse and shipped OFF** (`MTG_DIG_DRAWN_LAND_D0`, §6.4): the greedy plays every
such land, where the searched re-solve weighs the land against next turn's cycle.

### 3.3 The USER's "occasionally an untapped land" (`MTG_FLUCT_KEEP_UNTAPPED`)

*"The only cards that need to be protected from cycling are Unearth and Drannith Stinger and occasionally an untapped
land"* (ledger §7.5) was never built. The occasion is the open drop with no Stinger on the battlefield: ONE
untapped-entering land in hand (a colour source -- Capital City's filter -- over colourless Blasted Landscape, then
the lowest copy number) is ranked LAST in `SelectDigSource`. It is an ORDER, not a refusal: when it is the only
cyclable card it is still cycled, so no line is lost; it only stops being spent ahead of fodder that cannot be the
drop. Off once a Stinger is out ("everything is still free game") and once the drop is spent.

**Amended in round 5 (`MTG_FLUCT_KEEP_NEEDED`, §6.7a):** "occasionally" now means "while the land is what pays the
Stinger" -- the untapped battlefield cannot pay the deck's Drannith Stinger (in hand, else in the library) and the
land, dropped, would (colour-exact, `TurnSolver::DropLandEnablesCost`). A land kept with the Stinger already payable
became a phantom drop at every later re-solve and cost five seeds a turn at any budget.

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

Per REAL turn that reached the end step (a winning turn ends earlier and is not counted): **drawn** = a dig drew a
land while the drop was open; **lost** = of those, the drop went unused; **held** = lost with a dug land still in
hand at the end; **held live** = held and not going off (so the USER's no-fuel rule did not hold it); **enabling** =
lost, and at some dig a dug land in hand would have made a held nonland castable that was not, not going off;
**enabling (going off)** = the same while the hold-fuel rule applied. Round 2, same seeds as §6.

| deck | cell | arm | drawn | lost | held | held live | enabling | enabling (going off) |
|---|---|---|---|---|---|---|---|---|
| fluctuator | shipped (8,000) | old | 936 | 918 | 215 | 215 | 33 | 9 |
| | | p12f | 1011 | **874** | 232 | 232 | 40 | 9 |
| | d3 (12,000) | old | 1390 | 1367 | 289 | 289 | 68 | 16 |
| | | p12f | 1476 | **1290** | 315 | 315 | 70 | 14 |
| | d0 (40,000) | old | 5393 | **5393** | 789 | 789 | 358 | 329 |
| | | p12f (d0 drop ON) | 5436 | 4601 | 0 | 0 | 248 | 322 |
| fluctuator2hg | shipped (4,000) | old | 363 | 353 | 89 | 89 | 14 | 3 |
| | | p12f | 389 | **329** | 89 | 89 | 20 | 2 |
| | d3 (6,000) | old | 524 | 515 | 108 | 108 | 17 | 3 |
| | | p12f | 562 | **475** | 104 | 104 | 20 | 2 |
| | d0 (20,000) | old | 2163 | **2163** | 371 | 371 | 158 | 100 |
| | | p12f (d0 drop ON) | 2178 | 1789 | 0 | 0 | 110 | 91 |

How to read it:
* **Before, the drop was never taken after a dug land** at d0 (5,393 of 5,393) and almost never at searched depths
  (918 of 936) -- causes (1)-(3) in action.
* **After, at searched depths the drop is OFFERED, and the search takes it or declines it.** Lost drops fall (918 ->
  874, 1367 -> 1290, 353 -> 329, 515 -> 475), and what stays "held live" is the search declining a drop it was offered:
  the land is next turn's cycle. Sampled (shipped seed 11501201 T3): the chain drew Capital City with the drop open
  and a {1} Hollow One in hand; the land-draw re-solve fired, offered "Capital City + Hollow One", and the search kept
  the City (and the Farmland) as cyclers -- and killed T4 with them. `drawn` RISES because keep-untapped keeps the
  untapped land in hand longer.
* **"enabling" is not "should have played".** The held card is often a second Fluctuator (castable, does nothing --
  with `MTG_NO_REDUNDANT_REDUCER` the count drops 40 -> 28 at shipped) or a {1} Hollow One.
* At d0 the "after" rows are with the d0 runner's drop ON (round 2): held drops go to 0 -- and play gets worse (§6.4),
  which is why that half ships OFF.

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

### 6.3 Identity

Every other suite deck's smoke rows (d0 40 games, d3 b10 and shipped 20 games each; 2HG variants included) plus
fluctuator / fluctuator2hg with the levers OFF (80 / 40 games per cell), seeds from 11900001, the new binary against
the `16dfb7e4` binary, per game, win turn AND play digest: **125 / 125 jobs identical** in round 1 and again in
round 2 (binary `9d1f20dc`). Every new hook is base-false or Fluctuator-only, so this is the expected result.

### 6.4 Round 2 (binary `9d1f20dc`, same seeds and sizes): the funded amendment

| deck | cell | arm | avg | faster | slower | net | play-changed | units |
|---|---|---|---|---|---|---|---|---|
| fluctuator | shipped | old | 3.5594 | | | | | 1.0000 |
| | | **p1f** (order + funded) | 3.5593 | 1 | 0 | **-1** | 470 | 1.0003 |
| | | p12 (order + drawn land + keep, no funded) | 3.5576 | 42 | 27 | -14 | 1192 | 1.1424 |
| | | **p12f** (+ funded) | 3.5577 | 42 | 28 | **-13** | 1164 | 1.1445 |
| | | p12f + NRR (report only) | 3.5530 | 72 | 21 | -51 | 1625 | 1.0675 |
| | d3 b10 | old | 3.5756 | | | | | 1.0000 |
| | | **p1f** | 3.5757 | 1 | 2 | **+1** | 707 | 1.0009 |
| | | p12 | 3.5745 | 74 | 62 | -13 | 1739 | 1.0554 |
| | | **p12f** | 3.5745 | 72 | 60 | **-13** | 1664 | 1.0577 |
| | | p12f + NRR | 3.5705 | 127 | 68 | -61 | 2420 | 1.0146 |
| | d0 | old | 5.1582 | | | | | - |
| | | **p1f** | 5.1435 | 563 | 135 | **-588** | 6444 | - |
| | | p12 (with the d0 runner's drop) | 5.1646 | 710 | 957 | +258 | 7831 | - |
| | | p12f (with the d0 runner's drop) | 5.1448 | 598 | 210 | -534 | 6943 | - |
| | | p12f + NRR | 5.1289 | 1504 | 649 | -1169 | 14237 | - |
| fluctuator2hg | shipped | old | 3.5017 | | | | | 1.0000 |
| | | **p1f** | 3.5017 | 0 | 0 | **0** | 249 | 0.9974 |
| | | **p12f** | 3.5000 | 20 | 13 | **-7** | 561 | 1.1690 |
| | | p12f + NRR | 3.4955 | 34 | 10 | -25 | 761 | 1.0944 |
| | d3 b10 | old | 3.5260 | | | | | 1.0000 |
| | | **p1f** | 3.5260 | 2 | 2 | **0** | 373 | 0.9993 |
| | | **p12f** | 3.5258 | 38 | 37 | **-1** | 834 | 1.0549 |
| | | p12f + NRR | 3.5213 | 61 | 34 | -28 | 1176 | 1.0166 |
| | d0 | old | 5.0484 | | | | | - |
| | | **p1f** | 5.0322 | 291 | 52 | **-325** | 2946 | - |
| | | p12 (with the d0 runner's drop) | 5.0537 | 360 | 470 | +106 | 3667 | - |
| | | p12f (with the d0 runner's drop) | 5.0342 | 306 | 94 | -286 | 3187 | - |
| | | p12f + NRR | 5.0161 | 770 | 282 | -646 | 6308 | - |

* **The funded amendment closes the order's d0 loss:** +204 -> **-588** (fluctuator), +67 -> **-325** (2HG), and
  it is inert at every searched cell (p12 vs p12f: identical net within one game at shipped and d3).
* **The d0 runner's drawn-land drop is WORSE** than the order alone at d0: p12f -534 against p1f -588, and 2HG -286
  against -325 -- about +54 / +39 net, and round 1's p12-vs-p1 showed keep-untapped at d0 is worth about +3, so the drop
  is the cause. The greedy plays every such land, and on this deck the land in hand is also next turn's cycle (the
  USER: *"often you don't even want to play the third land, because it costs a cycling card"*). The searched
  chain-end re-solve weighs exactly that trade; the d0 runner cannot. **Split into its own lever,
  `MTG_DIG_DRAWN_LAND_D0`, default OFF** (it cannot move a searched cell: depth 0 only -- round 2's p12 at shipped and
  d3 is play-digest identical to round 1's, which had no d0 runner at all).
* **The order alone at d3 is +1** (p1f: 1 faster, 2 slower -- gi 1542 4->5 and gi 4129 5->6, the same two games p1
  lost in round 1, so they are the ORDER's, not the amendment's). The shipped combination p12f is -13 there.
* Reference bench (the shipped policy on the USER's 10 references, recorded mulligan forced): **10 / 10 equal to the
  human** on both the old arm and the new one (3,4,4,3,4,4,4,3,3,3) -- the three §7.12 shortfalls had already closed
  before this work.

### 6.5 Round 3 (binary `e9c8b97c`): the shipped defaults at d0, and the recovery stages

* **d0, the shipped defaults** (order + drawn land + keep-untapped + funded ON, the d0 runner's drop OFF): fluctuator
  **-585** net (565 faster / 139 slower, 40,000 games), fluctuator2hg **-324** (293 / 54, 20,000). An equivalence
  sample (1,000 games at shipped and at d3) is play-digest identical to round 2's p12f, as it must be.
* **Recovery stages** (stage 1 `--depth W --budget-ms 100`, W = max(5, the old arm's win turn); stage 2 `--depth 8
  --budget-ms 0`), every slower game of the candidate arms, 522 games:

  | deck | cell | arm | slower | stage 1 | stage 2 | NOT recovered |
  |---|---|---|---|---|---|---|
  | fluctuator | d0 | final | 139 | 139 | 0 | 0 |
  | | d0 | p1f | 135 | 135 | 0 | 0 |
  | | d3 | p1f | 2 | 2 | 0 | 0 |
  | | d3 | p12f | 60 | 48 | 0 | **12** |
  | | shipped | p12f | 28 | 12 | 0 | **16** |
  | fluctuator2hg | d0 | final | 54 | 54 | 0 | 0 |
  | | d0 | p1f | 52 | 52 | 0 | 0 |
  | | d3 | p1f | 2 | 2 | 0 | 0 |
  | | d3 | p12f | 37 | 28 | 0 | **9** |
  | | shipped | p12f | 13 | 3 | 0 | **10** |

  **The order + funded (p1f) recovers every slower game; the drawn-land hook (p12f) left 47 that do not recover even
  at depth 8 / unlimited** -- about 25 distinct seeds (441, 2732, 3695, 3857, 4917, 5172, ... appear in several cells),
  each with the SAME result at stage 1 and stage 2: the signature of a representation limit, not budget.

### 6.6 Root cause of the 47 -- and the fix (`8cc30904`)

gi 3695 (old: T2 Capital City + Fluctuator, cycle, T3 kill; new: T2 Capital City + a {2} cycle, T4 kill). Attribution
on the single game: keep-untapped OFF -> still T4; drawn land OFF -> T3; `MTG_BP_BASE_CANON=0 MTG_BP_NESTED_CANON=0` on
both arms -> both T3. So it is the drawn-land hook's breakpoints and the CANON default that resolves them:

* a breakpoint the hook ADDS (the land-draw re-solve, the nonland re-solve that only the open drop made castable, the
  chain-end re-solve) is resolved for a base plan by `MTG_BP_BASE_CANON` -- the continuation list's FRONT, typically
  "drop the land, cast the {1} Hollow One" -- and for a variant at a breakpoint it does not target, by the nested canon;
* so the old line (play nothing at each of those points) needs the EMPTY choice at SEVERAL breakpoints at once, which
  is outside the one-deviation neighbourhood (ledger §7.12): unreachable at any budget. And the evaluation of the T2
  Fluctuator line ran through those defaults, so the search mis-valued it and chose the T4 line.

**Fix:** at a hook-only site the unbranched default is **EMPTY** (`g_bp_hook_site_empty_default`, read by
`TurnSolver::BpUnbranchedCanon`). Before the hook those breakpoints did not exist, so EMPTY is exactly the old line:
the base plan plays byte-for-byte what it used to, and every land line is an explicit searched VARIANT. That restores
completeness (the old line is always reachable) and keeps the new lines findable. gi 3695 / 441 / 3857 equal the old
arm again; the two §4.3 scenarios still win T3 at d3 / b10 (as variants); the unit tests now pin "base plan = old
line, land line = a variant iff the hook is on". Every other site and deck: false, byte-identical. Measured in
round 4 (§6.7).

### 6.7 Round 4 (binary `8cc30904`): the shipped defaults at the searched cells -- and the last 7 seeds

Same seeds and sizes as rounds 1-2 (paired against the old arm = every lever off, 16dfb7e4's play):

| deck | cell | games | old avg | new avg | faster | slower | net | units |
|---|---|---|---|---|---|---|---|---|
| fluctuator | d3 | 12,000 | 3.5756 | 3.5732 | 76 | 49 | **-28** | 1.061 |
| fluctuator | shipped | 8,000 | 3.5594 | 3.5565 | 44 | 20 | **-23** | 1.160 |
| fluctuator2hg | d3 | 6,000 | 3.5260 | 3.5235 | 39 | 24 | **-15** | 1.056 |
| fluctuator2hg | shipped | 4,000 | 3.5017 | 3.4977 | 21 | 5 | **-16** | 1.184 |

Identity: 125 / 125 jobs of every other deck (d0, d3, shipped) play-digest identical to 16dfb7e4. References: 566 of
566 replay (179 ok, 387 repaired, 0 play-drift / board-diverged / enum-gap / shuffle-dead / contract-fail), every
reference except KittyEquipment v2 (its `claude_s8_gi7` costs ~20 min PER DECISION on the 16dfb7e4 binary too --
measured 2026-10-15, not this branch's cost -- so a full sweep would hold the box lock for hours on one core).

Recovery: 98 slower games; **87 recover at stage 1, 0 more at stage 2, 11 do not** (7 distinct seeds: fluctuator
3845, 4461, 7063, 7623 at d3 and at shipped, 4306, 5294, 7885 at shipped only; fluctuator2hg: all recover). Every one
gives the SAME win turn at d8/b0 as at the cell -- a representation limit again. Single-game lever isolation at
d8/b0 (each lever off alone, everything else on):

| gi | old | final | order off | drawn off | keep-untapped off | funded off | BP_DEPTH 3 |
|---|---|---|---|---|---|---|---|
| 3845, 7623 | 3 | 4 | 4 | 4 | **3** | 4 | 4 |
| 4461, 7063 | 4 | 5 | 5 | 5 | **4** | 5 | 5 |
| 4306 | 7 | 8 | 8 | 8 | **7** | 8 | 8 |
| 5294 | 4 | 5 | 5 | **4** | 5 | 5 | 5 |
| 7885 | 5 | 6 | 6 | **5** | 6 | 6 | 6 |

Two mechanisms, each traced on its game by handing the board to the search at the frame (`--choices-then-auto`) and
printing the continuation lists (`MTG_BP_CONT_TRACE`) and the root's tails (`MTG_FS_ROOT_DUMP`):

**(a) Keep-untapped kept a land the Stinger did not need (5 seeds).** gi 3845: Canyon Slough + Capital City on the
battlefield already pay Drannith Stinger's {1}{R}, yet the chain kept the drawn second Capital City (cycled Hollow One
in its place). Holding it put "land=Capital City: ..." at the FRONT of every later dig re-solve's list -- at the
second-Fluctuator draw `[#0 land=Capital City: Fluctuator, #1 Fluctuator, #2 land=Capital City, #3 (nothing)]` -- so
the canonical line dropped the City and cast the second Fluctuator, the Stinger drawn later could not be paid, and the
chain fell back on cycling the Stinger and an Unearth rebuy, three pings short. The winning line needs the old choice
at two breakpoints (no Fluctuator, and no City drop at the Stinger), outside the one-deviation neighbourhood.
**Amendment (`MTG_FLUCT_KEEP_NEEDED`, default ON):** the land is kept only while the untapped battlefield CANNOT pay
the deck's Stinger (in hand, else in the library) and the land, dropped, would -- colour-exact on copies
(`TurnSolver::DropLandEnablesCost`). That is the §4.3 occasion exactly (Polluted Mire + Irrigated Farmland, no {R}
without the City's filter); a land that buys nothing is fodder again, as before. All five seeds return to the old
win turn at d8/b0 (3845 3, 4461 4, 7063 4, 7623 3, 4306 7); with the amendment off, all five stay a turn slower.

**(b) The bp_all policy reached the hook sites (2 seeds).** gi 7885 T5 (and 5294 T4): without the hook the root's
winning child at T5 is a `bp_all` variant (Plan::bp_all: "candidate k at EVERY breakpoint" -- here, cycle on and cast
nothing, the free Hollow One included; the root dump shows it returning a verified win where the final arm's twin is
declined). With the hook, the chain's Capital City draw (drop open; the City would pay Enlightened Tutor's {W}) is a
new breakpoint whose list leads with land lines, and bp_all applied its k there too: it dropped the City, and the
kill was gone. A walk of the T5 line one cycle at a time (`--choices-then-auto` at each frame) agrees: handed to the
search before the City draw the final arm reads T6; once the City has been drawn (its site passed) it finds T5. The
fix below restores both seeds, and turning it off alone loses both again -- the attribution test.
**Fix (`MTG_DIG_HOOK_BPALL_EXEMPT`, default ON):** a hook site is exempt from the bp_all policy -- it takes its EMPTY
default (the pre-hook line exactly) and its land lines stay bp_at-targeted variants. Every pre-hook bp_all line is
therefore reachable again; nothing the hook added is lost. Byte-identical wherever the hook is off (the predicate
reads the hook-site flag).

Measured in round 5 (§6.8).

### 6.8 Round 5 (binary `7d1c7f80`): the shipped defaults, every cell -- THE BAR IS MET

ONE pooled batch (143 jobs, 273,260 games, 20/20 workers busy), three arms per cell on the same seeds: `old` (every
lever off = 16dfb7e4's play), `r4` (round 4's defaults: the two amendments off) and `final` (this binary's defaults).

| deck | cell | games | old avg | final avg | faster | slower | **net** | play changed | units | r4 net (slower) |
|---|---|---|---|---|---|---|---|---|---|---|
| fluctuator | d0 | 40,000 | 5.1582 | 5.1435 | 563 | 135 | **-588** | 6,452 | -- | -585 (139) |
| fluctuator | d3 | 12,000 | 3.5756 | 3.5736 | 61 | 37 | **-24** | 1,149 | 1.053 | -28 (49) |
| fluctuator | shipped | 8,000 | 3.5594 | 3.5561 | 36 | 10 | **-26** | 743 | 1.140 | -23 (20) |
| fluctuator2hg | d0 | 20,000 | 5.0484 | 5.0322 | 291 | 52 | **-325** | 2,949 | -- | -324 (54) |
| fluctuator2hg | d3 | 6,000 | 3.5260 | 3.5242 | 32 | 21 | **-11** | 603 | 1.051 | -15 (24) |
| fluctuator2hg | shipped | 4,000 | 3.5017 | 3.4980 | 20 | 5 | **-15** | 384 | 1.161 | -16 (5) |

* **Net <= 0 in every deck x cell** (d0 here includes the order + funded + keep-needed; the d0 runner's drop stays OFF).
* **Recovery: all 260 slower games recover at stage 1** (`--depth W --budget-ms 100`, W = max(5, old win turn));
  stage 2 had nothing left to run. Per cell: 135/135, 37/37, 10/10, 52/52, 21/21, 5/5.
* The `r4` arm reproduces round 4's numbers exactly on the new binary (-28 / -23 / -15 / -16, same faster/slower
  counts), so the amendments are the whole difference: they remove 12 + 10 + 3 slower games at the searched cells and
  cut the search work (units 1.061 -> 1.053, 1.160 -> 1.140, 1.056 -> 1.051, 1.184 -> 1.161).
* **Identity:** 125 / 125 jobs of every other deck (d0, d3, shipped) play-digest identical to 16dfb7e4.
* **Census** (`MTG_LANDDRAW_CENSUS`, real turns whose dig drew a land with the drop open): the drawn land is played
  on 96 of 1,464 such turns at d3 (old: 23 of 1,390) and 69 of 980 at shipped (old: 18 of 936); 2HG 42 / 34 (old 9 /
  10). d0 is unchanged by construction (the d0 runner's drop is OFF): 0 used in either arm.
* **Cost.** Search units +5% at d3 and +14% / +16% at shipped: the hook's new breakpoints are searched work (the
  budget is deterministic, so this is what the drawn-land lines cost, not a wall artefact). Wall, summed per job in
  the one pooled batch (indicative only -- a shared box): shipped old 469 s / r4 712 s / final 669 s; d3 old 1,685 s /
  r4 2,130 s / final 2,603 s. The d3 final-over-r4 step (+22% wall on -1% units) pointed at the keep-needed
  payability reads (two GameState copies per candidate land per cycle); round 6 memoises them (§6.9) -- and shows
  the wall step was noise.

### 6.9 Round 6 (`85b5b9c5`, the memoised keep-needed reads): identical games; wall sums are noise

`FluctKeepPayable` caches the two reads (Stinger payable now; payable with the land dropped) on the active player's
battlefield signature, which is all they depend on -- between cycles the chain changes only hand, library and
graveyard. Round 5's manifest re-run unchanged on that binary: **143 / 143 jobs game-for-game identical** (win turn
and play digest), units identical. The per-job wall sums, however, moved by 0.58x-1.51x between the two runs --
including the `old` arm, whose code path the change never touches (shipped old 469 s -> 317 s, 2HG d0 old 51 s ->
68 s) -- and the within-batch ratios flipped (d3 final / r4: 1.22 in round 5, 0.87 in round 6; shipped: 0.94, 1.30).
So summed per-game wall in a pooled batch on this shared box cannot attribute a 20% step either way, and this
document makes no wall claim; units (deterministic) are the cost measure above. A clean wall A/B would be its own
measurement (both arms `MTG_PGO=0`, an otherwise idle box).

## 7. Defaults as shipped and open USER questions

| lever | shipped | why |
|---|---|---|
| `MTG_FLUCT_ORDER` | ON | Part 1, the USER's order; with FUNDED it meets the bar at every cell (§6.4, §6.8) |
| `MTG_FLUCT_ORDER_FUNDED` | ON | the documented default (USER question 1); the order alone fails d0 (+204 / +67) |
| `MTG_FLUCT_DRAWN_LAND` | ON | Part 2, the drawn land considerable as the drop (search side); meets the bar with the two amendments (§6.8) |
| `MTG_FLUCT_KEEP_UNTAPPED` | ON | the USER's protection, as an order (§3.3) |
| `MTG_FLUCT_KEEP_NEEDED` | ON | its amendment (§6.7a; USER question 6) |
| `MTG_DIG_HOOK_BPALL_EXEMPT` | ON | a representation fix for the hook (§6.7b); inert without it |
| `MTG_DIG_DRAWN_LAND_D0` | **OFF** | the d0 runner's greedy drop: measured worse (§6.4) -- beyond the USER's words |
| `MTG_NO_REDUNDANT_REDUCER` | **OFF** | report-only arm (USER question 3) |

Round 5 (§6.8) is the adoption measurement: net <= 0 in every deck x cell, every slower game recovers at stage 1,
125 / 125 identity, 566 / 566 references (179 ok, 387 repaired, 0 drift of any kind; KittyEquipment v2 excluded, see
§6.7). Units 1066 / 1066, scenarios 162 / 162 (both §4.3 scenarios win T3 at d3 / b10).

### Open USER questions (surfaced, not settled; the documented default is taken meanwhile)

1. **Adopt `MTG_FLUCT_ORDER_FUNDED`?** It keeps your order whenever the cast set can be paid, and only decides
   which casts survive a set the pool over-credited. Default as coded: ON (the documented default, so the order meets
   the d0 bar); `=0` is your order exactly as approved. Without it the order alone is +204 / +67 net at d0 (§6.1).
2. **Cast a free Hollow One rather than cycle it when no Stinger is out?** (§4: a turn-to-win tie the search
   breaks toward cycling; your §7.7(c) remark -- "hollow one should not be held the vast majority of the time" --
   reads as "cast it".) Not built. Default: unchanged (the search decides).
3. **`MTG_NO_REDUNDANT_REDUCER` ON?** (skip a second Fluctuator entirely; report-only arm.) Default: OFF.
4. **Fix the Capital City double count in the engine?** The pool and the colour-exact test credit a fed
   any-colour filter's colour on top of the land that feeds it (§6.2). The real fix is in the shared payment core
   and also touches Snow's Arcum's Astrolabe, so it needs its own measurement on both decks. Default: not built
   (the FUNDED amendment works around it for Fluctuator only).
5. **Open `DigUsesDrawnLandDrop` for other dig decks** (treasure_hunt, FiveColour's Triome cycling, Auras' Horizon
   Canopy, Dragons' Mind Stone)? Default: no -- base false, each would need its own A/B.
6. **Is "occasionally an untapped land" = "while that land is what pays the Stinger"?** That is how
   `MTG_FLUCT_KEEP_NEEDED` reads your rule (§6.7a): kept while the untapped battlefield cannot pay Drannith Stinger
   and the land would; otherwise ordinary fodder. Built because the literal "always keep one" cost five seeds a turn
   at any budget. Default as coded: ON; `=0` is the literal reading.
