# Dragonstorm: cast in the USER's order everywhere it is decided (`MTG_DRAGONSTORM_USER_ORDER`)

Status: IMPLEMENTED behind ONE lever, `MTG_DRAGONSTORM_USER_ORDER` (heurarm slot `DRAGONSTORM_USER_ORDER`,
appended), **default ON**; `=0` is byte-identical to the parent commit `d6a760e5` (27 jobs / 540 games:
Dragonstorm d0 / d3 b10 / shipped, Hinata2 d0 / d3, Mirrorwing d0 / d3). The pooled suite A/B is owed
(not run here). 2026-10-10.

## The directive

USER, 2026-10-10:

> "We should do things in the order I said, once they become available, so after the breakpoint the cast
> order should follow my order. If it wasn't doing that it should be fixed."

And the Ruby Medallion ruling the same day:

> "Ruby medallion should be flexible. It should be cast as early as possible while still maintaining the
> line. It has to be cast later if cost does not allow it to be earlier."

## The order, and every reading taken

Sources: `dragonstorm-search-pruning.md` "Canonical go-off cast order (user-specified)";
`dragonstorm-cast-order-search.md`; `gi22-durdle-and-irencrag-apex.md` (rules 1-5);
`dragonstorm-plan-execution-fidelity-bug.md` ("funding sources should be cast first"); the ruling above.

    Ruby Medallion  <  Rite of Flame  <  Pyretic Ritual  <  Desperate Ritual (no splice)  <  Seething Song
      <  Desperate Ritual (spliced)  <  Apex of Power  <  hard-cast Dragon  <  Irencrag Feat  <  Dragonstorm

Readings (each a choice the USER should confirm; none settles a USER decision silently):

1. **Medallion = the EARLIEST slot at which the whole planned line still pays** (its {2} payable there AND
   every later cast paid after it, with the Medallion's discount credited to the red spells after it), never
   after Irencrag. If no slot lets the whole line pay, the slot that pays the MOST of it, earliest on ties.
   **Several Medallions:** each placed as early as possible in turn (the lexicographically smallest slot
   vector that pays) -- they may end up apart ("staggered"); the old search inserted them as one block.
2. **"Once they become available":** a card joins the order when it is castable -- a card exiled by Apex
   joins at the post-Apex continuation, which is ordered by the same rules.
3. **Ritual order** is read off card parameters, which reproduces the USER's list for this deck: printed
   cost ascending (Rite {R} < Pyretic {1}{R} < Seething {2}{R}), the Arcane splice base (Desperate) after a
   same-cost plain ritual, a SPLICED cast after Seething (more splices first).
4. **Splice legality wins over the literal slot of a plain Desperate.** A copy that a splice reveals must
   still be in HAND when the spliced copy is cast. With two copies, "plain Desperate (4) before Seething,
   spliced Desperate (6) after" is illegal -- the plain one is the revealed one. The plain copy then moves to
   just after the spliced cast, and only as far as legality needs (with a spare copy in hand it keeps slot
   4). The old engine executed such illegal splices (21 of 498 d0 phases; it never checked the hand).
5. **Apex is an enabler**: after the rituals, before a Dragon, Irencrag and Dragonstorm. Irencrag -> Apex
   (the trap) is never cast, and a subset only payable through the trap is not a line.
6. **Hard-cast Dragon**: after the rituals and Apex; before Irencrag when Dragonstorm closes; when there is
   no Dragonstorm, Irencrag goes immediately before the (last) Dragon (rule 2: Irencrag -> Dragon is a kept,
   low-value line). Two hard-cast Dragons keep plan order between them (the Scourge/Lathliss ETB order is
   unchanged from before).
7. **Irencrag second-to-last**: after it only the closer (Dragonstorm or a Dragon) is ever offered; with the
   cast cap spent nothing is.
8. **Where the USER's order cannot pay**: MTG_PAYABLE_ORDER's nearest payable permutation (USER-adopted),
   judged with the same exact walk, which refuses the trap and illegal splices.
9. **Search**: the cast-ordering search now tries the canonical arranged order and, per rule 5 ("splice-after
   Seething preferred ... individual-before is the fallback. Needs search."), the same order with the spliced
   Desperates in front of Seething. It no longer tries every Medallion slot nor a raw hand-order "identity".

## Audit (before = parent commit, after = this change; same seeds)

Instrument: `MTG_USER_ORDER_TRACE=1` (diagnostic, default off; `=2` adds hand / pool / cap) prints a
`[uo-order]` line per executor cast-order decision (order, exact-walk payability, the Medallion's slot and
its earliest payable slot) and a `[uo-cast]` line per real cast (paid / staged / splice). Audit script:
`logs/userorder/audit.py` (gitignored scratch; the classes are defined in its header). One pooled 4-thread
batch per arm: d0 = 300 games (seeds 7000001+), d3 b10 = 200 (7100001+), shipped (d5 b20 from the
profile) = 200 (7200001+). Counts are PHASES (game, turn, main) showing the class; pre / post = before /
after an Apex resolved in that phase.

| class | d0 before | d0 after | d3 before | d3 after | ship before | ship after |
|---|---|---|---|---|---|---|
| ritual order not per USER list | 48 / 8 | 0 / 0 | 54 / 8 | 0 / 0 | 36 / 6 | 0 / 0 |
| hard-cast Dragon before a ritual | 5 / 16 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 |
| hard-cast Dragon before Apex | 3 / 5 | 0 / 0 | 0 | 0 | 0 | 0 |
| Irencrag before Apex (trap) | 7 / 0 | 0 / 0 | 0 | 0 | 0 | 0 |
| Irencrag -> non-closer spell | 7 / 0 | 0 / 0 | 0 / 1 | 0 / 0 | 0 | 0 |
| Irencrag last (nothing after) | 1 / 0 | 1 / 0 (a) | 1 / 0 | 1 / 0 (a) | 0 | 0 |
| Medallion later than its earliest payable slot | 5 / 12 | 0 / 0 | 2 / 0 | 0 / 0 | 0 | 0 |
| Medallion earlier than payable (drops the line) | 0 | 0 | 3 / 0 | 0 / 0 | 5 / 0 | 0 / 0 |
| plain Desperate before a spliced one | 21 / 0 (b) | 3 / 0 (c) | 0 | 1 / 0 (c) | 0 | 3 / 0 (c) |
| Dragonstorm not last (a later decision) | 0 / 2 | 0 / 4 (d) | 0 | 0 | 0 | 0 |
| hand ritual / Medallion cast after Apex, not in the pre-Apex plan | 0 / 15 | 0 / 16 (e) | 0 | 0 | 0 / 2 | 0 / 2 (e) |
| d0 failed-cast re-solve spin (phases with >= 10 failed casts) | 2 | 0 | 0 | 0 | 0 | 0 |
| Irencrag -> Dragon (allowed, rule 2) | 3 / 0 | 3 / 0 | 8 / 2 | 9 / 4 | 3 / 1 | 3 / 1 |

Example seeds (game seed, turn): ritual order d0 (7000003, 5), d3 (7100008, 4), ship (7200001, 6); Dragon
before a ritual (7000018, 6); trap (7000028, 5), (7000082, 3), (7000288, 3 -- 331 spin lines); Medallion
later (7000047, 5), (7100111, 3); Medallion earlier than payable (7200139, 3: Medallion | Seething | Scourge
on two Mountains -- Seething and Scourge never cast). Repro: `--seed <s> --game-index <s - base> --games 1`
with `--depth 0 --ignore-play-profile` (d0), `--depth 3 --budget-ms 10 --ignore-play-profile` (d3) or
`--budget-ms 20` (shipped), base = 7000001 / 7100001 / 7200001.

Remaining, each explained:
- (a) Irencrag -> Karrthus (7000051 T5) / Kolaghan (7100151 T5): the USER's order, but the subset is
  colour-infeasible (Irencrag floats only red; the Dragon needs {B}/{G}). An enumeration colour-admission gap,
  not an order defect.
- (b) The old comparator put the plain copy first (cheapest cost) and executed the splice off a copy that had
  left the hand.
- (c) Legal: a spare copy stays in hand (three Desperates, two cast).
- (d) Post-breakpoint LEFTOVERS: a main-plan cast still in hand after the Apex continuation re-solve is cast
  after it (7000158 T8: Scourge after the continuation's Dragonstorm; 7000043 T5). The continuation re-solve
  did not include it; the executor's main loop then casts the plan's remainder. A decision-structure issue,
  in both arms; see "Found, not fixed".
- (e) Subset choices: the d0 greedy's pre-Apex plan left a ritual / Medallion in hand that the post-Apex
  re-solve then cast. A ritual's net mana is the same either side of Apex, so this is not an ordering loss.

## What changed (all behind the one lever unless noted)

Provider (`src/ai/DecisionProviders.{h,cpp}`, `DragonstormProvider`):
- `CastOrderRank`: Medallion 110, Rite 120, Pyretic 140, Desperate 150, Seething 160, Apex 180, creature 190,
  Irencrag 200, Dragonstorm 210 (card-parameter classes). `=0` -> `GenericProvider::CastOrderRank`.
- `CastOrderActionRank` (new hook, read by the shared `CastOrderLess`): a spliced cast 170+ (more splices
  first).
- `ArrangeCastOrder` (new hook; one shared entry `ApplyDeckCastArrangement` in `ManaPayment.cpp`): splice
  legality, Irencrag before a lone Dragon closer, each Medallion at its earliest payable slot.
- `ExactOrderWalk` (new hook): `PayableCastOrderCore` (MTG_PAYABLE_ORDER) judges with the exact walk (and an
  exact-aware order-free bound) at the apply site.
- `CastOrderingCandidates` (new hook): the search's orderings (canonical arranged + the splice fallback),
  computed after the plan's Lotus Bloom sacrifice; replaces `DragonstormCastOrderings` for this deck.
- `CastUnderCastCap` (new hook at the `CollectActions` choke point): after Irencrag resolves only
  Dragonstorm / a Dragon is offered; a spent cap offers nothing.
- `OrderedSubsetRealisable` (new hook, both subset evaluators -- Solve's consider and EnumeratePlans, after
  the restrictor test, only for a subset holding a restrictor or a spliced cast): a subset whose Apex is
  payable only after Irencrag, or whose splice only by revealing a copy already cast, is not a line.

Engine (`src/ai/ManaPayment.{h,cpp}`, `AIEngine.cpp`, `TurnSolver.cpp`):
- `ExactOrderFirstUnpayable`: FirstUnpayablePos plus, positionally, a same-turn colour reducer's discount,
  Rite's graveyard escalation, Apex's ten from hand (none off the exile), the Irencrag cast cap (and the
  trap), splice legality, and a line-aware generic payment (each colour's surplus over the pips still owed
  first -- the plain walk spent the Unclaimed Territory's {B} on a ritual's {1}, s7000261 T5).
- `ApplyDeckCastArrangement` called right after the comparator sort at every cast-order site: executor main
  (clean / opaque), breakpoint continuation (clean / opaque), the pod / pe continuation appliers; the rollout's
  `apply_plan_actions` (clean / opaque); the sequential-payability rescue; `CanonicalNonSacCastOrder`; the
  earliest-win report. A no-op for every other provider (byte-identical).
- The diagnostic trace (`MTG_USER_ORDER_TRACE`).

Unit cover: `test/unit/test_dragonstorm_user_order.cpp` (11 cases): rank list on/off; comparator incl. the
spliced Desperate; a plain Desperate keeps its slot with a spare copy; Medallion unpayable first / payable
after one ritual (and the plain payable fallback does NOT find that slot: its walk sees no discount, its
order-free bound says nothing pays, it returns Medallion-first untouched); apply level (ApplyPlanDirect):
the Medallion line casts every spell where the generic order strands the Dragon; an Apex set holding
Irencrag (Apex before Irencrag; the walk refuses the trap); Irencrag before a lone Dragon (apply level); the
searched orderings; the cast-cap gate; the trap subset; the payable fallback never buys payability with an
illegal splice. Full suite 916/916.

## Ruby Medallion: what the old machinery did

- **MTG_PAYABLE_ORDER's nearest payable permutation does NOT yield the earliest payable Medallion slot.** Its
  walk (FirstUnpayablePos) never credits a same-turn Medallion's discount, so a Medallion is pure cost to it:
  on a tight line its order-free bound concludes no permutation pays and it returns the order untouched
  (unit: two Mountains, {Medallion, Rite, Pyretic, Seething, Scourge}: Medallion first, the rest stranded);
  where a permutation does pay it is the nearest by inversions, which may move a different card. Its domain
  does cover the set (a ritual is a credited producer under MTG_PAYABLE_ORDER_RITUALS). Hence the explicit
  placement, and the exact walk inside the fallback.
- **The depth>0 search did NOT keep the earliest slot that preserves the line.** It tried every slot
  earliest-first and kept the FIRST of any score tie, so a Medallion-first slot that could not be paid --
  dropping the rest of the line -- won whenever the win turn came out the same (ship 7200139 T3; 5 shipped /
  3 d3 phases in 200 games). It also kept a raw hand-order identity and put a plain Desperate after Seething.
  Now the generator emits the arranged order (Medallion at its earliest payable slot) plus the splice fallback.

## Sanity A/B (NOT a verdict; the pooled suite A/B is owed)

One pooled batch, lever on vs off, paired seeds (d0 7600001+, d3 b10 7700001+), 4 threads:

| cell | games | turn sum on | turn sum off | delta | faster | slower | wall on/off | units on/off |
|---|---|---|---|---|---|---|---|---|
| d0 | 300 | 1503 | 1530 | -27 (-0.090/game) | 18 | 3 | -- | -- |
| d3 b10 | 100 | 451 | 453 | -2 (-0.020/game) | 1 | 0 | 0.81x | 0.86x |

Every slower game, root-caused:
- **s7600011 gi10 (d0, 6 -> unwon).** Turn 6, after the first Apex, the continuation casts Medallion |
  Seething | Apex (the USER's order; generic: Seething | Medallion | Apex) -- the Medallion now discounts
  Seething, leaving 2 mana instead of 1 after the exiled Apex. With that extra mana the d0 Solve's
  rituals-for-payoff guard (`s_ritual_payoff_guard`, TurnSolver) calls Rite of Flame a SURPLUS ritual in
  {Apex, Rite, Desperate x2, Seething} and rejects the subset (the guard exempts only a storm tutor, not
  Apex), and the accel-prefix collapse never enumerates the Rite-less prefix -- so no Apex line survives and
  the turn stalls. Verified: exempting impulse-exile from the guard as well restores T6 on this arm (and, the
  same tweak, turns the OFF arm unwon -- the guard / prefix interaction is fragile on both arms). A
  pre-existing d0-greedy defect, exposed by the extra mana. NOT fixed here (outside the order; see below).
- **s7600233 gi232 (d0, 6 -> 7).** T3 d0 plan {Rite, Rite, Pyretic, Scourge} on one Mountain. Generic order
  casts Scourge FIRST (creature 10) -- unpaid -- then burns the rituals for nothing. The USER's order realises
  the plan: Scourge on T3, attacking T4-T6. Its combat firebreathing then spends one storage-land mana a turn,
  so on T6 Seething x2 + Dragonstorm is a mana short; kill T7. The old T6 kill rested on its T3 Dragon cast
  failing.
- **s7600052 gi51 (d0, 5 -> 6).** The old T5 kill used an ILLEGAL splice: Pyretic, Desperate (plain),
  Desperate splicing the copy that had just been cast (two copies), Irencrag, Dragonstorm. The legal order
  (spliced first, {2}{R}{R}) cannot be paid on that board, so the subset is refused and the d0 Solve plays
  another line.

## Found, not fixed

1. **Rituals-for-payoff guard vs Apex** (gi10 above): `s_ritual_payoff_guard` treats an Apex line as a fixed
   payoff and calls a cheap ritual surplus; with the accel-prefix collapse no Apex subset survives. Proposed:
   exempt `impulse_exile` like `tutor_to_battlefield` (an Apex's leftover mana and storm count are spent by the
   exile), behind its own lever, measured on both arms.
2. **Post-breakpoint leftovers** (audit note d): the executor (and its rollout twin) cast the pre-breakpoint
   plan's remaining casts after the continuation re-solve, e.g. a Dragon after the continuation's
   Dragonstorm. Either the remainder should be offered to the re-solve and not cast after it, or the re-solve
   should run after the remainder; both change executor/rollout semantics and need their own A/B.
3. **Splice legality is not enforced by the engine itself** with the lever off: the executor pays (k+1) times
   the cost and floats (k+1) times the mana without checking the revealed copies are in hand, and the
   comparator casts the plain copy first. Under the lever the order and the enumeration respect it; with
   `=0` the old behaviour (21 illegal d0 phases / 300 games) remains.
4. **Colour admission**: Irencrag -> Karrthus / Kolaghan subsets pass the enumerator although Irencrag floats
   only red (audit note a).
5. **The d0 failed-cast re-solve spin** (deliberate, step 31; MTG_EXEC_FAILED_CAST_NO_RESOLVE default OFF):
   re-solving an unpaid draw-engine cast up to the breakpoint-call cap. The trap subsets that fed it are now
   refused (0 spin phases after vs 2), but the mechanism is unchanged.
6. **Searched plans whose casts mostly fail** ("unpaid, decided order cannot pay": 24 d3 / 19 shipped phases):
   subsets the optimistic enumeration credits admit and the search picks on a score tie with doing less.
   Unchanged in kind by this work (fewer after: 47 -> 24 d3, 31 -> 19 shipped).

## Follow-up round (2026-10-10): the found defects, fixed

USER standing directive: *"We probably should fix any bugs up front."* Each fix is its own commit with its own
lever (`EnvOn("MTG_...", true)` + a heurarm slot appended after `DRAGONSTORM_USER_ORDER`), a unit test in
`test/unit/test_dragonstorm_followups.cpp`, `=0` byte-identical to the previous commit on the ident set (27 jobs;
45 for the payable-order levers, adding the rock / ritual decks), and a small paired sanity A/B, lever on vs
off, fresh seeds, 4 threads -- NOT a verdict; the pooled suite A/B is still owed and no regression tier was run
(the box was shared with a value-leaf generation).

| commit | lever | what it fixes | paired sanity (on vs off) |
|---|---|---|---|
| f1cd15e4 | `MTG_RITUAL_FLOAT_COLOR_EXACT` | first-round item 4: both subset evaluators credited a FIXED-colour ritual float (Irencrag's seven {R}) as wild, so Irencrag -> Karrthus / Kolaghan passed the enumerator. The fixed colour is now credited as that colour (plus the Rite escalation); wild stays for a chosen-colour ritual. Reaches Dragonstorm, Hinata2, Cyclone. | ds d0 300: -2 (1 faster, 0 slower); ds d3 100, hin d0 300 / d3 100, cyc d0 300 / d3 100: 0/0 |
| 6328ba07 | `MTG_DRAGONSTORM_BP_LEFTOVERS` | item 2: after an Apex breakpoint the interrupted list's remaining casts were cast AFTER the continuation, i.e. after its Dragonstorm (s7000158 T8). Design below. | ds d0 300 / d3 100: 0/0; the audit's "Dragonstorm not last [same decision]" 1 -> 0 |
| b3b068ca | `MTG_RITUAL_GUARD_APEX` | item 1: the d0 / rollout rituals-for-payoff guard judged Apex a fixed-cost payoff and called a cheap ritual surplus; with the accel-prefix collapse no Apex line survived (s7600011 T6). An impulse-exile plan now keeps its rituals; a cast RESTRICTOR still has to earn its place (exempting it too let the generic rank realise the Irencrag -> Apex trap on the `USER_ORDER=0` arm). | ds d0 300: -7 (5 faster, 0 slower); d3 100: -1 (1 faster, 0 slower) |
| bde3bff6 | `MTG_EXACT_WALK_CCO` | found classifying item 6: the exact walk booked an Unclaimed Territory as wild, so it "paid" a noncreature spell's coloured pip (Rite's {R} off a lone Territory). Now {C} for a noncreature cast. | ds d0 300 / d3 100: 0/0 |
| 916002fc | `MTG_PAYABLE_ORDER_EXACT` | P2: MTG_PAYABLE_ORDER's producer domain was judged with the plain walk outside Dragonstorm. The plain walk ignores a cast cap (hinata d0 gi1427 T6: "Irencrag, Ornithopter, Crackle") and spends a land a later cast needs on a generic pip (gi1680 T7: Sol Ring / Gamble swapped, tutoring with the Sol Ring in hand for the random discard). Every deck now uses the exact walk there. | hin d0 300: -1 (1 faster, 0 slower); hin d3 100, ds d0, cyc d0 + d3, Mirrorwing d0 + d3, Dragons d0 + d3, Giants d0 + d3, Snow / WhiteKnights / Angels / Bruna / KittyEquipment / StompySurprise / FiveColour d0 300: all 0/0. gi1427 and gi1680 both 9 (unwon) -> 8, equal to `MTG_PAYABLE_ORDER_RITUALS=0`. |
| ba2230bf | `MTG_PAYABLE_ORDER_DYNAMIC_DECLINE` | P3 (latent): MTG_PAYABLE_ORDER walked an order holding a live Hinata / Soulfire per-target discount, whose stamped cost is only right at the enumeration's position; LadderProjectable has always declined that class. Now declined at both sites. | hin d0 300 / d3 100: 0/0 (d3 search units -0.4%: it fires, no win turn moves); ds, cyc, Mirrorwing d0 300: 0/0 |
| 48b894fb | -- | test only: two first-round tests state what the payable-order fallback does with the USER order off, "the plain walk"; `MTG_PAYABLE_ORDER_EXACT` changed that arm (it now finds the paying order), so they pin it off for that statement and also assert the lever's own result. | -- |
| (this commit) | `MTG_DRAGONSTORM_IRENCRAG_CLOSER` | found by this round's closing audit ("Irencrag last (no spell after)", s7000035 T5): {Medallion, Desperate, Seething x2, Apex, Irencrag} -- admitted because the order-free credit lets Irencrag's seven red "fund" an Apex the USER order casts first; `MTG_RITUAL_GUARD_APEX` made the line selectable -- exiled no castable closer, and the leftover Irencrag was cast alone after the empty continuation. USER rule 7 at two sites (one reader, `DragonstormIrencragCloserOn`): a subset with the restrictor, NO closer and NO Apex is refused; the post-Apex merge does not cast a LEFTOVER Irencrag that no closer follows and no Apex precedes (it stays in hand). A subset WITH an Apex stays admitted: refusing it too was the first version, refuted by s8500074 T6 / s8500148 T4 (the second exile held the Scourges; refused, the d0 runner did not cast that Apex at all -- 7 -> unwon, 8 -> unwon). | seeds 8600001+: ds d0 300 0/0, d3 100 0/0; the 8500001+ sample that refuted version 1: d0 300 0/0, d3 100 0/0. Audit: "Irencrag last" 1 -> 0. |

`=0` of `MTG_DRAGONSTORM_USER_ORDER` is no longer byte-identical to `d6a760e5` once these levers are on: the colour
credit, the guard and the payable-order levers reach the `USER_ORDER=0` arm too. Each lever's own `=0` is
byte-identical to its parent commit.

### Item 2 design: merge, else carry (executor and rollout in lockstep)

`MergeBreakpointLeftovers` (ManaPayment.cpp) is called from the executor's `resolve_draw_breakpoint` and from
`ApplyPlanDirect`'s Apex branch; the provider opts in with `MergesBreakpointLeftovers` (Dragonstorm, Apex sites,
USER order on). The leftovers are the interrupted list's remaining hand casts. They are ordered TOGETHER with
the continuation's casts by the USER's order -- the same comparator, arrangement and payable fallback as any
decided order -- so every cast after the breakpoint follows the order, which is the directive's own wording.
When adding them would cost a continuation cast its payment they are CARRIED behind the continuation's own
casts instead, and a nested Apex in the continuation takes them up again. The interrupted list then truncates
(it was absorbed), as it already did for a committed line.

Why not the two alternatives the first round listed: offering the remainder to the re-solve changes WHICH
subset is searched (a new decision, which the d0 runner could only make greedily -- the no-greedy rule); running
the re-solve after the remainder casts the remainder before the exiled cards are known, the order the USER ruled
out. A first DROP design (leftovers that do not fit are dropped) did not fix s7000158: the d0 second pass re-cast
the Scourge after Dragonstorm because the walk could not see the nested Apex's mana. Carrying fixed it.

### Item 6 ("searched plans whose casts mostly fail"), classified

- **"unpaid, decided order cannot pay"** (the bulk: d3 24, shipped 19 of 200 games each, unchanged): the
  enumeration's sequenced ritual credit (mode 1) is OPTIMISTIC by documented design -- the exact mode 2 was
  rejected twice for drifting 21 references. Not a defect of this work.
- **"projected payable, cast unpaid"**, each one traced:
  - s7000192 T4 (d0) and s7100046 T4 (d3): the SOLVER books the Unclaimed Territory as wild in the noncreature
    pool and plans a ritual line only its {C} can touch. That is `MTG_CCO_NONCREATURE_POOL`, default OFF by a
    user-directed withdrawal (TurnSolver.cpp; measured neutral on Goblins). Left as the USER set it; flagged for
    the USER. Under `MTG_EXACT_WALK_CCO` the walk now reads both orders as unpayable (they moved to "cannot pay").
  - s7100081 T2 (d3): the order DOES pay -- the Territory's {C} on the Medallion's {2}, the floating {R}{R} on
    Pyretic -- but the executor's payer spends the turn-scoped float first (`SpendFloatingTowardCost` before any
    tap), pays the {2} with {R}{R}, and Pyretic is unpayable off the Territory. An executor payment divergence;
    the autonomous twin of the human-only `MTG_PAY_LINE_FLOAT_HOLD` would close it. NOT fixed: payment layer,
    every deck, needs the suite A/B.
  - s7100151 T5 (d3, as of 6328ba07): the opposite payment miss -- the Medallion's {2} paid with Bazaar +
    Territory, losing Kolaghan's {B}. No longer reproduces (the game now wins T5 on another line).

### Item 3: lever-off illegal splices in other decks

Only Dragonstorm holds a splice card (Desperate Ritual). Lava Spike is Arcane, but no deck holds it with a splice
card. With `MTG_DRAGONSTORM_USER_ORDER=0` the old behaviour remains (the engine itself does not check the
revealed copies).

### The six ab3-slower Dragonstorm d0 games

Win turn (9 = unwon): ab3 old -> ab3 head -> this branch. Bisect on this branch's binary, starting from the
old settings and adding ONE of USER_ORDER / PAYABLE_ORDER_RITUALS / ORDER_OPAQUE.

| game | old | ab3 head | now | lever that slows it | class |
|---|---|---|---|---|---|
| gi445 s1190446 | 6 | 7 | 7 | RITUALS or OPAQUE (each alone); not USER_ORDER | old won by luck: its hand order left the d0 plan's Apex unpaid; realising the plan's Apex spends the turn on an exile the d0 valuation chose |
| gi1011 s1191012 | 5 | 7 | **5** | -- | exposed a defect: the guard vs Apex; fixed by `MTG_RITUAL_GUARD_APEX` |
| gi1412 s1191413 | 4 | 5 | **3** | -- | old luck + a leftover Irencrag cast after the continuation's Utvara; fixed by `MTG_DRAGONSTORM_BP_LEFTOVERS` + `MTG_RITUAL_GUARD_APEX` (now faster than old) |
| gi1590 s1191591 | 6 | 7 | **6** | -- | restored by `MTG_RITUAL_GUARD_APEX` |
| gi1686 s1191687 | 6 | 7 | 7 | USER_ORDER or RITUALS; not OPAQUE | old won by luck (its T3 Dragon cast failed on the generic rank), and it exposes a defect: once the Dragon attacks, the greedy combat pump taps the Sandstone Needle (a depletion land, sacrificed T3) -- see below |
| gi1701 s1191702 | 8 | 9 | 9 | RITUALS or OPAQUE; not USER_ORDER | old won by luck, the gi445 shape |

The first round's slower games: s7600011 is fixed (6, both arms); s7600233 is the gi1686 mechanism with a storage
land (Mercadian Bazaar).

### Closing audit (same seeds and settings as the first round's; the binary with every lever above)

| class (decision column: same / later) | d0 (300) | d3 b10 (200) | shipped (200) |
|---|---|---|---|
| Dragonstorm not last | 0 / 3 | -- | -- |
| Irencrag last (no spell after) | 0 | 0 | 0 |
| Irencrag -> Dragon (allowed, USER rule 2) | 3 / 1 | 9 / 4 | 3 / 1 |
| unpaid cast, decided order cannot pay | 4 / 2 | 24 / 0 | 19 / 0 |
| unpaid cast, decided order projected payable | 1 (s7000035 T3) | 1 (s7100081 T2) | 0 |
| illegal splice | 0 | 0 | 0 |

s7000035 T3 is an optimistic-enumeration plan (only its Desperate pays; the Apex does not, so its leftovers are
never taken -- the merge runs only after a PAID Apex -- and the Irencrag after it is attempted on three mana).
s7000035 T5 with the closer lever: the continuation is empty and Irencrag stays in hand (`=0`: cast alone, 13 ->
17 red floating for nothing). The game is unwon on every arm (no Dragonstorm drawn). s7100081 T2 is the executor float-first payment below.

### Still open after this round

1. **Combat firebreathing spends non-renewable mana** (gi1686, s7600233). `ApplyFirebreathing` pumps with ALL
   leftover mana on the premise "combat is the last mana use, unused mana is lost". That premise is false for a
   depletion land (a counter is spent, and the land is sacrificed at zero) and a storage land (banked counters).
   Proposed: the pump payer refuses those sources unless the pump is lethal -- an engine-wide combat policy, so
   it needs the suite A/B; not done here.
2. **Executor float-first generic** (s7100081 T2, above).
3. **`MTG_CCO_NONCREATURE_POOL`** stays OFF (USER-withdrawn); on Dragonstorm it lets the search plan ritual
   lines off an Unclaimed Territory's {C}. USER decision.
4. **d0 second pass after a closer** ("Dragonstorm not last [later decision]", 3 phases / 300 d0 games:
   s7000043 T4, s7000204 T4, s7000289 T7): after the turn's Dragonstorm the d0 runner's next pass casts an
   expiring exiled card (s7000043: a third Medallion after a T4 win). Harmless where traced; a d0 subset choice.
