
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
