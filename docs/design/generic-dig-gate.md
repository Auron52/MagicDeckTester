# The generic dig gate — cycling is unreachable for any deck without a bespoke provider

**Status: BUILT 2026-09-29 on the user's explicit go** (*"even with the merge I would like to fix
the cycling issue"* / *"That should not be inaccessible"*). The heuristic shipped is **narrower than
the one designed below**, on three further rulings the user gave while it was being built — see
"What the user changed during the build", which is the part to read if you are here for the
rationale rather than the history.

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

## What the user changed during the build (2026-09-29) — and why each one narrowed it

The design above is what was proposed. Three rulings during implementation cut it back, and all
three ran in the same direction: **the engine keeps the capability; the provider keeps almost none
of the appetite.**

**1. The heuristic is the empty-of-gas case and nothing else.** *"We shouldn't cycle when we have a
lot of useful things in hand"*, then *"It's mostly to find something useful when you have none."*
Step 2 above proposed the union of Auras' two conditions (surplus lands **or** no gas). The
surplus-land half is gone for the generic provider: one castable nonland in hand is enough to
decline the dig, because the mana a dig spends is the mana that card needs and the card it throws
away may *be* that card. Auras keeps its own wider gate — its source is a land already in play,
whose only other use is mana, so spending it while flooded is near-free *there*. That is exactly
what a per-deck override is for.

**2. The searched dig axis is NOT opened for the generic provider.** Step 4 proposed
`DigDecisionSearched() → true`, reading the standing 2026-08-28 directive (*"it should be
searched otherwise"*). The user's objection is specific to this axis: *"You might draw a better
threat off Basri, but that would be mostly just when using clairvoyance. I believe a
non-clairvoyant engine would not want to do so (or only very rarely)."* The rollout knows the draw
order, and a dig is the one decision whose entire payoff **is** the next card — so a clairvoyant
rollout cycles precisely when it can see the top card is better, manufacturing a gain no real game
reproduces. It is the effect treasure_hunt's flood-engine adoption already measured and discounted
(non-clairvoyant better by −0.034/−0.123; clairvoyant "better" by +0.11..+0.125 of fake known-draw
speed). **It was measured here too, and it was not free:** with the axis on, FiveColour ran
+0.0200 *slower* over 200 games; with it off, FiveColour matches its own baseline average exactly
while still cycling 20 times.

**The capability stays in the engine in all cases, including the search.** The user was explicit:
*"these are just heuristics for the provider. We should leave the capability in the engine"*,
*"(in all cases)"*, *"(and in the search, but the provider can trim the branching there)."* Nothing
was removed. The rollout still performs digs — its loop runs whenever `ShouldConsiderDig` passes,
so the search continues to MODEL the dig and the executor replays the recorded line. What the
provider declines is only the per-plan dig/no-dig **fan-out**, which is branching, not capability.
`MTG_GENERIC_DIG=0` is a measurement escape hatch restoring the old stubs byte-identically; it is
not a capability switch and nothing should ship reading it.

**3. Basri is expected to be a near-no-op on WhiteKnights, and that is the correct answer rather
than a shortfall.** *"A 2/1 Knight is always useful in goldfishing"* — so the empty-of-gas rule
will essentially never fire for it. The real value is out of model: *"It would be most helpful for
stuck boards in real games. In those situations the 2/1 may not be useful, but something else in
the deck could be."* A goldfish has no stuck boards. So round T's finding (1 Basri for 1 Venerable
Knight = 100.0% identical play) **survives this change**, and Basri's cycling joins Aether Vial's
instant-speed deploy in the category of real value the measurement cannot price.

### Measured result of the shipped version

200 games/deck, d5, seed 4242, `MTG_TRACE=dig` counting realised digs in real games:

| deck | digs ON | digs OFF | avg ON | avg OFF | play |
|---|---|---|---|---|---|
| FiveColour | 14 Zagoth Triome + 6 Jetmir's Garden | 0 | 4.9350 | 4.9350 | changed |
| Pirates | 10 Fiery Islet | 0 | 4.3750 | 4.3750 | changed |
| EldraziDisplacerFlicker | **0** (Cloud of Faeries is always castable) | 0 | 4.2400 | 4.2600 | changed |
| Auras | 25 Horizon Canopy | 25 Horizon Canopy | 4.1550 | 4.1550 | **identical** |
| treasure_hunt | 9 | 9 | 4.1150 | 4.1150 | **identical** |
| WhiteKnights | 0 (no dig source) | 0 | 4.1050 | 4.1050 | **identical** |

Two things to read carefully here.

* **The four already-overriding decks are untouched**, dig-for-dig and digest-for-digest — the
  change reaches only providers that were declining to ask.
* **EldraziDisplacerFlicker takes zero digs yet its play changed.** That is not a contradiction and
  it is worth knowing: `BpDigFanoutPending` / `BpDigFanoutForPlan` gate breakpoint site-4 fan-out on
  `HasAnyDigSource`, so a deck that merely *holds* a cycler now gets a wider plan set at that site
  whether or not it ever digs. Cost measured at +0.3% core-ms on that deck.

**Counting realised digs was the instrument, deliberately.** A capability whose only symptom is
"the ability is never offered" cannot be validated by a digest diff, because *zero variants emitted*
and *no change wanted* look identical ([[digest-equality-can-mean-broken]]). `MTG_TRACE=dig` in
`AIEngine::PerformDig` is permanent for that reason.

## Why this matters beyond Basri

Any future deck holding a cycling card inherits the gap silently, and the failure mode is invisible:
the card is implemented, the parameter is parsed, the tests pass, and the ability is simply never used.
A screen of such a card measures it **as if the ability did not exist** — which is precisely how round
N2 on WhiteKnights came to record "cycling's option value and the legend rule cancel to within noise"
for a measurement in which cycling never fired once. Round T corrected that record; this document
removes the cause.
