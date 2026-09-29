# The generic dig gate — cycling is unreachable for any deck without a bespoke provider

**Status: DESIGNED, NOT BUILT. Needs the user's explicit go — it is a capability change that moves
play on three shipping decks.**

**Origin.** User, 2026-09-29, on being told Basri's cycling is unreachable on WhiteKnights:
*"Cycling shouldn't be unreachable should it?"* and then the design in one line: *"It shouldn't be
unreachable, but could perhaps be dropped heuristically most of the time."*

That is right, and the gap is larger than the card that surfaced it.

## The defect

`GenericProvider` stubs out **all three** dig hooks (`src/ai/DecisionProviders.cpp:343–345`):

```cpp
bool        GenericProvider::HasAnyDigSource (const GameState&) const { return false; }
bool        GenericProvider::ShouldConsiderDig(const GameState&) const { return false; }
std::string GenericProvider::SelectDigSource(const GameState&, const ManaPool&, bool&) const { return {}; }
```

`AIEngine::UseSurplusLandAbilities` (`src/ai/AIEngine.cpp:6050`) returns immediately on the first of
those, so **the autonomous search is never offered a cycle or a sacrifice-to-draw at all** unless the
deck's provider opts in. Only `AurasProvider`, `FluctuatorProvider`, `TreasureHuntProvider` and
`DragonsProvider` override it. The two `TurnSolver` cycling sites are labelled human-play P3/P6, so a
person playing through the viewer *is* offered the cycle — only the engine is not.

**This is not a modelling gap in any card.** `cycling_cost` is parsed, honoured and fully implemented;
the shared detector `HasAnyDigSource` (`src/core/SpellEffects.h:23787`) already scans the **hand** for
`cycling_cost` and the **battlefield** for `sacrifice_draw_cost`, and is entirely deck-agnostic:

```cpp
inline bool HasAnyDigSource(const GameState& state) {
    for (const Card& c : ap.hand)
        if (d->params.cycling_cost.has_value()) return true;
    for (const Permanent& p : state.battlefield)
        if (!p.tapped && d->params.sacrifice_draw_cost.has_value()) return true;
    return false;
}
```

Everything needed to answer *"is there a dig available?"* exists and works. The generic provider simply
declines to ask.

## The sharpest illustration

**Auras can crack its Horizon Canopy. Pirates cannot crack its Fiery Islet.** Both are lands whose only
relevant text is `{1}, Sacrifice: draw a card` — the same `sacrifice_draw_cost: "{1}"` parameter. The
two decks get opposite answers purely because `AurasProvider` overrides the gate and `PiratesProvider`
does not. Nothing about the cards, the board or the game state differs.

## Blast radius — measured, and it is small and self-limiting

Seven shipping decks hold a dig source. **Four already override the gate and are unaffected.** Three do
not, and would newly gain the capability:

| deck | dig source it is currently denied | provider | status |
|---|---|---|---|
| Auras | Horizon Canopy (sac-draw `{1}`) | `AurasProvider` | **already overrides** |
| Fluctuator | 16 cyclers | `FluctuatorProvider` | **already overrides** |
| treasure_hunt | Fiery Islet, Forgotten Cave, Lonely Sandbar, Remote Isle | `TreasureHuntProvider` | **already overrides** |
| Dragons | Mind Stone (sac-draw `{1}`) | `DragonsProvider` | **already overrides** (flag-gated) |
| **FiveColour** | Jetmir's Garden, Zagoth Triome — **cycling `{3}`** | `FiveColourProvider` | **would change** |
| **Pirates** | Fiery Islet — sac-draw `{1}` | `PiratesProvider` | **would change** |
| **EldraziDisplacerFlicker** | Cloud of Faeries — **cycling `{2}`** | `EldraziFlickerProvider` | **would change** |

**Every other deck is byte-identical BY CONSTRUCTION**, and this is the property that makes the change
safe to validate: a deck with no cycler and no sac-draw permanent makes the shared detector return
`false`, which is exactly what the stub returns today. The guard is self-limiting — there is no need to
argue that other decks are unaffected, it is structural.

**WhiteKnights is a no-op today.** It holds zero dig sources, so this change is byte-identical for it
until Basri is actually added to the list. **The engine fix and the Basri list decision are therefore
completely independent and can be done in either order.**

## The design

1. **`GenericProvider::HasAnyDigSource` → `::HasAnyDigSource(s)`.** Ask the shared detector instead of
   answering `false`. No new detection logic.
2. **`GenericProvider::ShouldConsiderDig` → a deliberately conservative heuristic.** This is the user's
   *"dropped heuristically most of the time"*. `AurasProvider::ShouldConsiderDig`
   (`DecisionProviders.cpp:3760`) is the shape to copy, and its two conditions already generalise:
   * the resource is genuinely **surplus** (for a cycling land: lands already sufficient to cast the
     deck's curve, so the land is not needed as a land), or
   * the hand is **empty of gas** (no castable nonland), where drawing toward action is the only line.
   For a *nonland* cycler such as Basri the first condition needs a companion — cycle only when the
   body is redundant (e.g. the board already has a wide enough position that another 2/1 changes
   nothing) — which is exactly where this wants measuring rather than guessing.
3. **`GenericProvider::SelectDigSource`** — pick the cheapest affordable dig source; with one cycler in
   hand this is trivial, and `FluctuatorProvider::SelectDigSource` is the deck-aware precedent for more.
4. **`DigDecisionSearched() → true` for the generic case.** This follows the standing user directive of
   2026-08-28 recorded at `DecisionProviders.cpp:3762`: *"we can certainly have a heuristic to help make
   the decision… but it should be searched otherwise."* The enumerator already fans dig/no-dig plan
   variants (`Plan::dig_choice`) and lets the rollout decide; opting the generic provider in means the
   heuristic supplies the default and the **search** overrides it where it matters, which is strictly
   better than any fixed heuristic and matches what Auras/Fluctuator/Dragons already do.

## Validation plan

* **Byte-identity smoke across every deck with no dig source** — expected 0 changed, and it is provable
  rather than hoped for (see blast radius).
* **Targeted per-deck checks on the three affected decks.** FiveColour is the interesting one: a
  five-colour manabase that can finally cycle a flooded Triome for `{3}` is the textbook use of the
  ability, and FiveColour already has known flood behaviour worth watching.
* **GT rebaseline for those three decks only**, all three tiers, with a **single-line** accept note
  (see the `accept-ack-must-be-one-line` hazard — a multi-line note corrupts `regression_gt.txt`).
* Pirates additionally has ten user-played references (`references/Pirates/claude_s*_gi*.json`, 10/10
  exact as of 2026-09-28) — a change to its dig behaviour should be re-checked against those, since a
  human reference line is the one place where the *human* cycling route (TurnSolver P3/P6) and the new
  autonomous route can be compared directly.

## Why this matters beyond Basri

Any future deck holding a cycling card inherits the gap silently, and the failure mode is invisible:
the card is implemented, the parameter is parsed, the tests pass, and the ability is simply never used.
A screen of such a card measures it **as if the ability did not exist** — which is precisely how round
N2 on WhiteKnights came to record "cycling's option value and the legend rule cancel to within noise"
for a measurement in which cycling never fired once. Round T corrected that record; this document
removes the cause.
