# Knights: cast-order draft (2026-10-14)

Deck: `decks/Knights/Knights.cod`. W/x Knight tribal on Aether Vial. Every creature in the 60 is a Knight, and the
U/R lords are cast off 12 fixers (Unclaimed Territory and Secluded Courtyard are creature-only; Tournament Grounds is
Knight-only).
Provider: `KnightsProvider : VialProvider`. Single main (`uses_second_main=no`).
- No card in the list is order-opaque (no draw, staging, cascade or trick; `OrderingOpaque`, ManaPayment.cpp:2722).
  So the rank governs every cast set.
- Since the USER's 2026-10-06 ruling, every Aether Vial put is sequenced at its creature's rank.

## 1. Status: (A), a USER-agreed order shipped default ON

- `MTG_KNIGHTS_ORDER` was reviewed by the USER and adopted 2026-08-19. It is `EnvOn("MTG_KNIGHTS_ORDER", true)` at
  `src/ai/DecisionProviders.cpp:7675`. The ruling is quoted verbatim in `docs/design/cast-order-rankings.md:590-611`.
- Main phase: the USER said *"Everything should be main 1"*. That is what plays (single main). The rule-derived Phase-2
  split is the USER's own deferral (`cast-order-rankings.md:624-628`).
- **No USER order is OFF for this deck.**
- Where the code lives matters (§4.2): the order sits on the SHARED base `VialProvider::CastOrderRank` (`:7679`).
  slivers_vial resolves to that base directly, and Soldiers falls back to it when its own flag is off.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | the land drop | m1 | First. The deck has no draw. |
| 8 | Worthy Knight | m1 | Cast-trigger watcher, before the tribe (`cast_trigger_creates_tokens`, `:7687`). |
| 9 | Acclaimed Contender, when another Knight is already on the battlefield | m1 | Gated ETB digger, gate met. It goes early so the dig's breakpoint re-plans the rest of the turn (`:7694`). |
| 10 (TIE x8) | Dauntless Bodyguard, Venerable Knight, Inspiring Veteran, Marshal of Zhalfir, Adeline, Benalish Marshal, Knight Exemplar, Haytham Kenway | m1 | Generic creature tier. The tie falls to plan (hand) order, but it is provably free (§3). |
| 12 | Acclaimed Contender, when no other Knight is on the battlefield | m1 | Gate not met. It goes after the other creatures, so they satisfy it. |
| 20 | Aether Vial | m1 | Generic noncreature tier. |

The report prints m2 for Dauntless, Venerable, Worthy Knight and Contender. Those labels are inert:
- `MainPhaseFilterActive` returns false when `!state.uses_second_main` (`TurnSolver.cpp:20388`).
- `KnightsProvider` does not opt into `ClassifiesMainPhases` (default false, `DecisionProvider.h:1977`).

## 3. Proposed order: unchanged (the USER's 08-19 order)

The land drop goes FIRST. The deck has no draw, so nothing wants to precede it.

| pos | card | main | range | why |
|---|---|---|---|---|
| 1 | Worthy Knight | m1 | - | "Whenever you cast a Knight spell, create a 1/1 Human." Every Knight cast after it this turn is one more body. It also goes ahead of the Contender, so the Contender's own cast mints a token. A Vial put is not a cast, so puts gain nothing from it, but they lose nothing either. |
| 2 | Acclaimed Contender, gate met on the battlefield | m1 | - | USER: "before others if it is played". The dig resolves before the turn's other casts and Vial put, and the breakpoint re-plans them with the dug card in hand. |
| 3-10 | Dauntless, Venerable, Veteran, Zhalfir, Adeline, Benalish, Exemplar, Haytham | m1 | - | FREE block (see below). |
| 11 | Acclaimed Contender, gate not met | m1 | - | USER: "except when you don't yet control a knight". Any same-turn Knight, cast or Vial-put, satisfies the gate first. |
| 12 | Aether Vial | m1 | - | Order-inert. It charges at upkeep, so it does nothing the turn it is cast, and any land's {C} pays its {1}. |

**Why the 3-10 block is free.**
- The four lords' +1/+1 or +2/+2 (Veteran, Zhalfir, Exemplar, Benalish) and Haytham's are static continuous effects
  (CR 611.3). Adeline's power is a characteristic-defining count, and her token is made at declare-attackers.
  Everything cast this turn is summoning sick. So entry order changes nothing this turn or later.
- Every ETB or rider in the block is goldfish-inert, per its cards.json note:
  - Dauntless Bodyguard's choice and sacrifice
  - Venerable Knight's death trigger
  - Haytham's exile
  - Exemplar's first strike and indestructible
- **Colour is the payer's job, not the rank's.** Probe `probe_Knights/colour.sh` used 2 Plains + Unclaimed Territory,
  with Venerable Knight `{W}` and Inspiring Veteran `{R}{W}` in both hand orders. Both orders cast both, with the
  Territory paying only the `{R}`.

## 4. What changes and why it matters

Nothing in play changes. These are the findings behind "keep":

1. **The Contender's gate is read at the START of the main phase, not at the moment of the cast.**
   - The USER's words ("except when you don't yet control a knight") also allow an at-cast reading: once the turn's
     first Knight resolves, you control one, so the Contender would go right after it, not after the whole tribe.
   - Probe C (`probe_Knights/make_run.sh`): 8 Plains, an empty board, hand Worthy Knight + Contender + Venerable, and
     Benalish Marshal as the only Knight in the dig window.
   - Today's play at d0, d5/b20 and d8/b0: WK, Venerable, Contender. The dig takes Benalish, 2 mana floats, and
     Benalish is cast on T5.
   - The at-cast reading would cast Benalish on T4 instead of Venerable. But the T5 swing is 14 either way and the
     kill is T6 either way.
   - That line is already expressible at searched depth: the plan {WK, Contender} plus the dig's breakpoint
     continuation re-plans the rest. So the start-of-main reading is not a prune there.
   - I found no game it loses. Keep it (Q2).
2. **The shared-base hazard.**
   - The Knights clauses run for every `VialProvider` deck. They are inert on slivers_vial and Soldiers only because
     no sliver or Soldier carries `cast_trigger_creates_tokens` or `etb_dig_requires_subtypes` (in all of cards.json,
     only Worthy Knight, Acclaimed Contender and Young Pyromancer carry a `cast_trigger_*` or `etb_dig_requires_*` key).
   - The agent drafting slivers_vial will put any sliver order beside these clauses. If that order lands as a
     `VialProvider` override, Knights and WhiteKnights inherit it, and if the Knights clauses are restructured,
     Soldiers' OFF path changes.
   - Moving the clauses down to `KnightsProvider::CastOrderRank` is byte-identical for all four decks and removes
     the coupling (Q3). This has to be coordinated with the slivers_vial draft; I did not change the base.
3. **The report contradicts the USER's "everything main 1" on paper.** See the m2 labels in §2. They are inert, but
   they are exactly what a reviewer reads (G2).
4. **The Phase-2 deferral stands.** USER: *"things that don't impact combat this turn into main 2 (Dauntless,
   Venerable Knight and Aether Vial)"*, and *"Don't hardcode, but have rules to decide when they should be out."*
   Nothing here is due before an opponent exists.

## 5. Questions for the USER

1. Keep the 08-19 Knights order exactly as adopted? **Default: yes.**
2. Should the Contender's "don't yet control a Knight" be read at the moment of the cast (right after the turn's first
   Knight)? That needs `ArrangeCastOrder`. **Default: no.** No probe shows a loss, and the line is reachable through
   the dig's continuation.
3. Move the Knights clauses off the shared `VialProvider` onto `KnightsProvider` (byte-identical)?
   **Default: yes.**
4. Should the report print m1 for single-main decks instead of the inert classifier labels? **Default: yes.**

## 6. Generic-order findings

- **G1. `ClassifyMainPhase` sends a cast-trigger watcher and a gated ETB digger to m2** (Worthy Knight, Acclaimed
  Contender here).
  - A watcher in main 2 misses every main-1 Knight cast.
  - A dig that feeds this turn's casts is principle 1 (information first).
  - Both belong in m1. This is inert on single-main decks, but wrong for any multi-main deck that runs them.
- **G2. `--cast-order-report` prints classifier labels on single-main decks.** On this deck they read as a departure
  from a USER ruling that play does honour.
- **G3. Worthy Knight's rank-8 rule is generic, not Knights-specific.**
  - The rule: a cast-trigger watcher whose trigger is keyed to a creature subtype goes before the creature tier
    (principle 3, which the tier-10 comment already applies to prowess).
  - Young Pyromancer, the only other `cast_trigger_*` card, is already right, because its triggers are
    instants/sorceries ranked 20.
  - It could move to `GenericProvider` keyed on `cast_trigger_creates_tokens` with a non-empty
    `cast_trigger_subtype`.

## 7. Implementation notes

- **No rank change is proposed.**
- **Params that identify the slots (already in place):**

  | slot | key |
  |---|---|
  | Worthy Knight | `cast_trigger_creates_tokens > 0 && !cast_trigger_subtype.empty()` |
  | Contender | `etb_dig_count > 0 && !etb_dig_requires_subtypes.empty()`, plus a battlefield scan for the subtype |
  | Vial | the generic tier |
  | free block | the generic creature tier |

- **Q3 (refactor):**
  - Move the two clauses and the 8/9/12 tier names from `VialProvider::CastOrderRank` / `CastOrderTierName`
    (`DecisionProviders.cpp:7679-7721`) into `KnightsProvider` overrides, keeping the `MTG_KNIGHTS_ORDER` reader.
  - `WhiteKnightsProvider` then inherits them from `KnightsProvider`, and `VialProvider` is left to slivers_vial.
  - Check it with `--cast-order-report` on all four Vial decks and a smoke run with play-changed = 0.
- **Q2, only if the USER wants it:**
  - Add a `KnightsProvider::ArrangeCastOrder` (`DecisionProvider.h:940-946`, the Dragonstorm precedent).
  - When no Knight is on the battlefield, it moves a rank-12 Contender to just after the first Knight in the sorted
    set (cast or Vial put).
  - A scalar rank cannot express that, because it sees the board, not the plan.
- **Engine limitation:** none blocks the order as adopted.
