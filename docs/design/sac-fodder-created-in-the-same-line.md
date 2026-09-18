# Sac-for-mana fodder created in the SAME line

**Status: diagnosed, NOT fixed. Deferred 2026-09-18 ahead of the Fungus value-leaf generation.**

## The report

USER, 2026-09-17, on `logs/play/rejections/Fungus_cod_s5_gi4_t4.json`:

> "Should be able to activate and sac in same line ideally, to avoid extra breakpoints."

and again 2026-09-18, seed 9 / T5, a three-item line:

> ```
> 1 Thallid Shell-Dweller ⟳ ability
> 2 Utopia Mycon ⟳ sacrifice
> 3 Sporesower Thallid
> ```
> "Only commits the Sporesower Thallid"

Both are one shape. The human wants:

1. remove three spore counters → **create a Saproling** (free, no mana, no {T}),
2. **sacrifice that Saproling** to Utopia Mycon → add one mana of any colour,
3. spend that mana, with the lands, on a cast the lands alone cannot afford.

## Reproduction

`logs/s9probe/*.json` (scratch) — the minimal board is three Forests, a Thallid
Shell-Dweller on three spore counters, a Utopia Mycon, and a Sporesower Thallid
({2}{G}{G}) in hand. Three Forests are three mana; the line needs four.

| line | verdict |
|---|---|
| `cast=Thallid Shell-Dweller` (the spore activation alone) | **accept** |
| `cast=Thallid Shell-Dweller;cast=Sporesower Thallid` (4 lands, no sac needed) | **accept** |
| `cast=Sporesower Thallid` (4 lands) | **accept** |
| `cast=Thallid Shell-Dweller;sacout=Utopia Mycon;cast=Sporesower Thallid` (3 lands) | **illegal** — "can't pay {2}{G}{G}" |
| `cast=Thallid Shell-Dweller;sacout=Utopia Mycon` (3 lands, no cast) | legal_not_enumerated |

So activations and casts combine fine in general. What does not exist is the
**dependency**: fodder that the line itself creates.

### The decisive pair

Two boards identical in every respect except **where the Saproling comes from**
(both shipped as fixtures — `test/scenarios/fungus_sac_fodder_on_board.json` and
`fungus_sac_fodder_same_line_GAP.json`):

| fodder | line | verdict |
|---|---|---|
| a Saproling token already on the battlefield | `sacout=Utopia Mycon;cast=Sporesower Thallid` | **accept** |
| the line makes it (`cast=Thallid Shell-Dweller` = remove three spores) | `cast=Thallid Shell-Dweller;sacout=Utopia Mycon;cast=Sporesower Thallid` | **illegal** |

The sac-for-mana machinery — the outlet, the any-colour fan, the payment coupling,
the plan match — is entirely sound. The single missing capability is *fodder created
within the same line*.

Writing that pair required adding **token staging** to the scenario harness
(`"token": true` on a battlefield entry). Tokens have no `cards.json` entry, so
`make_card` could not build one and a token board was previously inexpressible as a
fixture — which is why Fungus, a Saproling-token deck, had **zero** fixtures.

`MTG_SAC_OUTLET_PAY` (§2b) does **not** fix it — verified directly, same verdict
with the lever on. §2b makes existing fodder a payment source; it reads the
battlefield at payment time, and at payment time the Saproling has not been made.

## Root cause — three sites, not one

**1. The action is never emitted.** `TurnSolver.cpp` (the sac-outlet candidate
loop, ~line 16290):

```cpp
const int victim_id = CanonicalSacVictim(state, state.active_player_index,
                                         src.card.m_number, need_sub, ...);
if (victim_id < 0) { continue; }   // no legal victim to sacrifice
```

Candidates are collected against the board **as it stands**. With no Saproling out,
`CanonicalSacVictim` returns -1 and the `SacForMana` action is never emitted at all —
so no subset can contain it, and nothing downstream gets a chance to notice that a
co-selected spore activation would have supplied a victim.

Note `SubsetOversubscribesSacFodder` (~line 7599) *already* reasons about this
correctly in the other direction: its `plan_can_add` lambda explicitly credits
"any token the action creates that carries the filter subtype", including
`spore_token_subtypes`. The subset guard understands mid-plan replenishment; the
candidate emitter does not.

**2. The apply order is wrong even if it were emitted.** `apply_continuation_precasts`
(~line 23393) applies `SacForMana` in a **pre-pass, before the casts** — which is
correct and load-bearing for the ordinary case (the mana must float before anything
pays with it). A spore activation is not in that pre-pass, so the sacrifice would run
*before* its victim exists.

**3. CheckLine's affordability walk does not apply board activations.** The
declared-order walk (~line 48110) re-creates a freshly-cast mana **rock** on its
scratch battlefield so later casts can tap it, and untaps lands for an ETB untapper —
but a `board_act` entry that creates a token contributes nothing. So even with 1 and 2
fixed, the viewer would still reject the line.

## Why it was not fixed on 2026-09-18

A correct fix touches the candidate emitter, the apply ordering, and CheckLine —
three sites in the most load-bearing part of the engine — and it is not
Fungus-specific: `state.deck_has_sac_mana_outlet` is also true for Goblins
(Skirk Prospector), where Krenko's tap makes Goblins in the same line. That is a
play change for a deck in the regression suite and needs its own full cycle.

Landing it unvalidated immediately before an overnight value-leaf generation would
fit the learned evaluator to a barely-tested engine. The generation was the user's
stated priority for that night, so the line stayed broken and this was written
instead.

**Staleness is not a reason to hold the generation.** A value leaf fitted to the
engine without this line stays usable after the line lands (the Hinata regen
precedent: staleness is neutral).

## Severity, stated honestly

* **In the viewer it is ergonomic, not a lost play — VERIFIED, not assumed.** The
  human can commit the spore activation on its own (verdict `accept`), and the second
  half then validates `accept` against the board that produces (probe `j_workaround`:
  same three lands, spores spent, Saproling out). So the play is reachable in two
  commits. That is exactly the "extra breakpoints" the user is asking to avoid — the
  cost is clicks, not lines.
* **In autonomous play it is a genuinely missing line.** The search cannot reach it
  either, for the same reason 1 above. How much that costs Fungus is UNMEASURED.

## The shape of the fix

Emit the `SacForMana` candidate when a *co-enumerable* action creates a
filter-matching creature, reusing `SubsetOversubscribesSacFodder::plan_can_add`'s
existing token-subtype scan rather than writing a second one; resolve the victim at
apply time instead of baking `sac_victim_id`; hoist token-creating `PermAbility`
activations ahead of `SacForMana` in the pre-pass; and teach CheckLine's walk to push
the created token onto its scratch battlefield, in the same place it already pushes a
freshly-cast rock.

This is the "fuse create+spend, payability-gated" shortcut doctrine
(`docs/design/`, the Clue-fusion strand) applied to a sac outlet.
