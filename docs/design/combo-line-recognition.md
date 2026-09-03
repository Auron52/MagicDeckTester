# Combo-line recognition: "the combo is active, follow the line"

**Status: DESIGN ONLY — nothing in this document is implemented.** It is a self-contained proposal,
written after reading the relevant engine code and the deck's own analysis ledger. Every claim below
is marked **VERIFIED** (checked directly against `src/cards/data/cards.json`, `src/ai/TurnSolver.cpp`,
`src/ai/DecisionProviders.cpp`/`.h`, `src/ai/SearchBudget.h`, or a cited doc/commit) or **PROPOSED**
(a suggested mechanism, not built, not measured). Where something could not be verified from the
repository as it stands, that is stated explicitly rather than assumed.

This deck's primary metric is **average win turn** (lower is better); losses are folded in as
`max_turns + 1`. This document deliberately avoids win/loss framing throughout, per repo convention.

## 1. The problem, in the user's own terms

Hand-playing `EldraziDisplacerFlicker` seed 1 in the play viewer, the deck's owner reached a turn-3
board — Conservatory (played turn 1), Aether Hub with Wild Growth attached (both played turn 2), and a
second Peregrine Drake just drawn on turn 3 — and stated a line they consider an obvious win, which the
engine did not take:

> "There is a literal T3 win there and I will spell it out:
> a. Tap Conservatory for Trace of Abundance on Aether Hub. (this is valid, but the line seems to be
> rejected)
> b. Living Wish -> Cloud of Faeries
> c. Cloud of Faeries with remaining mana untapping Aether Hub and Conservatory
> d. Peregrine Drake untapping all lands
> e. Float mana, untap with second drake, now we have 7 mana two of which should be white.
> f. Emiel the Blessed + 3 to flicker drake.
> g. Continue flickering drake for a large amount of mana of all relevant colours including 1 green, 1
> black and 20 colourless, and probably like 400 other mana to pay generic costs.
> h. Utilize additional drake flickering to draw cards with conservatory using our extra mana until
> Living Wish is located.
> i. Living Wish -> Essence Depleter
> j. Cast Essence Depleter
> k. Activate Essence Depleter 20 times for the win.
> This is what I mean by a combo heuristic. We need a way to determine 'combo is active, let's follow
> the line'
> As a side note, if black is not available, we would get blue instead of black and use Dimensional
> Infiltrator instead."

Generalized: the engine needs a way to look at a board (plus hand, plus a reachable sideboard) and
recognize **"a self-funding mana loop plus a finishable payoff is assemblable this turn"**, then
**construct and follow** that specific multi-step line, rather than relying on the ordinary plan
enumerator and search to discover it by generic branching. That is the subject of this document.

### Scoping note — separating a known, already-fixed bug from the open question

Step (a)'s parenthetical ("this is valid, but the line seems to be rejected") describes enchanting a
**land** (Aether Hub) with a land Aura (Trace of Abundance). **VERIFIED**: commit `34abe486`
(`fix(edf): land auras were UNPLAYABLE -- one root cause, two symptoms`, on this branch) fixed exactly
this class of defect — `SubsetHasAuraOnUncastCreature`'s legality guard looked for a *creature* with the
aura's target `m_number`; a land aura's host is a *land*, so it never matched, and the guard silently
**rejected every subset containing a land aura**. That fix is described as shipped default-on
(smoke 48/48, regression 80/80). Whether it fully explains the specific rejection the user observed in
that exact seed/game was not independently re-verified here — this document does not build or run
anything, per its own scope — so that possibility is neither confirmed nor assumed. Either way, it is a
**different problem** from the one this document addresses: a plan being legally enumerable is a
prerequisite for the search to find it, not a reason the search would *prefer* it over ordinary
board development. The rest of this document is about the latter.

## 2. The board, from the engine's own card data

Rule 0 of this repository's card-implementation practice is to read the engine's actual model rather
than rely on memory of the printed card. All of the following is quoted verbatim from
`src/cards/data/cards.json` (the `oracle_text` field includes the engine's own bracketed modelling
notes, kept here because they govern what the recognizer can actually see).

| card | cost | type | oracle text (as modelled) | key params |
|---|---|---|---|---|
| Conservatory | — (land) | Land | "This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate." | `produces: [G, W]`, `enters_tapped: true`, `tap_investigate_cost: {4}` |
| Aether Hub | — (land) | Land | "When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any color." | `produces: [C,W,U,B,R,G]`, `etb_energy: 1`, `energy_per_colored_tap: 1` |
| Wild Growth | `{G}` | Enchantment — Aura | "Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}." | `is_land_aura: true`, `land_aura_extra_mana: 1`, `land_aura_produces: [G]` |
| Trace of Abundance | `{R/W}{G}` | Enchantment — Aura | "Enchant land\nEnchanted land has shroud.\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any color." | `is_land_aura: true`, `land_aura_extra_mana: 1`, `land_aura_grants_shroud: true` |
| Living Wish | `{1}{G}` | Sorcery | "You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living Wish." | `tutor_to_hand: true`, `tutor_types: [Creature, Land]`, `wish_from_sideboard: true`, `exiles_self_on_resolve: true` |
| Cloud of Faeries | `{1}{U}` | Creature — Faerie | "Flying\nWhen this creature enters, untap up to two lands.\nCycling {2}." | `etb_untap_lands: 2`, `cycling_cost: {2}` |
| Peregrine Drake | `{4}{U}` | Creature — Drake | "Flying\nWhen this creature enters, untap up to five lands." | `etb_untap_lands: 5` |
| Emiel the Blessed | `{2}{W}{W}` | Legendary Creature — Unicorn | "{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\nWhenever another creature you control enters, you may pay {G/W}. If you do, put a +1/+1 counter on it..." | `blink_cost: {3}`, `blink_returns_tapped: false`, `blink_own_only: true` |
| Eldrazi Displacer | `{2}{W}` | Creature — Eldrazi | "Devoid\n{2}{C}: Exile another target creature, then return it to the battlefield tapped under its owner's control." | `blink_cost: {2}{C}`, `blink_returns_tapped: true`, `blink_own_only: false` |
| Essence Depleter | `{2}{B}` | Creature — Eldrazi Drone | "Devoid\n{1}{C}: Target opponent loses 1 life and you gain 1 life." | `drain_cost: {1}{C}`, `drain_amount: 1`, `drain_self_gain: 1` |
| Dimensional Infiltrator | `{1}{U}` | Creature — Eldrazi | "Devoid\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library. If it's a land card, you may return this creature to its owner's hand." | `exile_opponent_top_cost: {1}{C}`, `exile_opponent_top_may_bounce_on_land: true` |

Two things worth flagging explicitly because they matter to recognition:

- **"Conservatory" in this engine is a two-color tapped land with an Investigate side ability** — it is
  not a mana doubler. Its only relevance to the loop is as an ordinary `{G}`/`{W}` source, and later (via
  its own `{4},{T}: Investigate` ability) as an extra `{T}`-having mana sink once a Clue token exists.
  Any recognizer must be built against this data, not against an assumption about the name.
- **Neither win condition has `{T}` in its activation cost, and neither is once-per-turn** (`Essence
  Depleter`'s `drain_cost` and `Dimensional Infiltrator`'s `exile_opponent_top_cost` are both plain mana
  costs). That is what converts unbounded mana into an unbounded number of activations, per the deck's
  own design doc, [`flicker-combo.md`](flicker-combo.md).
- **Aether Hub's "any color" mode is gated on a player-level energy counter**, and per
  [`analysis-EldraziDisplacerFlicker.md`](analysis-EldraziDisplacerFlicker.md), the pool *projection*
  currently over-credits multiple Hubs sharing one energy pool (each Hub's coloured mode is credited
  independently even though the energy is a shared, single-player resource) — a known, disclosed,
  over-credit-only gap. Any recognizer that reasons about "how much of which color is available" should
  be aware this input can already be optimistic in this deck, in the direction that only causes an
  enumerated line to later fail payment, not a false win report.

## 3. What makes the combo recognizable from board state

### 3a. VERIFIED — the recognizer that already exists, for an ASSEMBLED loop

The repository already has a board-state combo recognizer for this exact deck:
`EldraziFlickerProvider::RecogniseFlickerLoop` (`src/ai/DecisionProviders.cpp`, in the file's anonymous
namespace around line 13176), returning a `FlickerLoop` struct (same file, ~line 13123). Read precisely,
it computes:

- **outlet**: a controlled permanent whose `CardDefinition::params.blink_cost` is set (Eldrazi Displacer
  or Emiel the Blessed).
- **payload**: a creature (any controller, unless the outlet's `blink_own_only` restricts it) whose
  `params.etb_untap_lands > 0` (Cloud of Faeries or Peregrine Drake).
- **cost**: the outlet's activation cost run through `EffectiveActivationCost` (so Training Grounds'
  reduction is already folded in) → `cost_mv`.
- **refund**: the sum of the controller's top-`N` land yields (`N` = the payload's `etb_untap_lands`),
  via `FlickerTopLandYields`, which reads `PermanentManaYield` — the same yield figure land Auras (Wild
  Growth, Trace of Abundance) feed into, so an Aura's bonus is already counted.
- **net = refund − cost_mv**. The loop is recognized (`ok = true`) only if `net > 0` — i.e. it is proven,
  by direct arithmetic against the live board, to be **self-funding**.

Separately, the same function prices whichever finishing sink is on the battlefield: a Shivan-Gorge-style
`tap_damage_cost` ability, or either of this deck's two `{T}`-less sinks (`drain_cost` /
`exile_opponent_top_cost`), each again priced through `EffectiveActivationCost`. Given a recognized loop
and a sink, `FlickerGoOffCount` (same file, ~line 13282) computes the **exact number of iterations**
needed to finish — `ceil(opponent life / damage-per-activation)` for a damage/drain sink, or
`cards remaining in the opponent's library` for the deck-out sink — clamped to `kFlickerMaxIterations =
60`. That count is then injected as one additional candidate into the plan enumerator's activation-count
axis (`BlinkActivationCounts` / `ManaSinkActivationCounts`, same file, ~lines 13412–13470), alongside the
generic small counts.

**This predicate is precise and it is a genuine "combo is active" recognizer** — but it only fires once
the outlet, the payload, and a sink are **already permanents on the battlefield**. It reads `s.battlefield`
only; nothing in `hand` or the sideboard is consulted at this stage (the function's own comment: "reads
only what is already in play (never a card in hand)"). That is exactly right for its stated job — sizing
the *finishing* activation count of an assembled loop, safely and cheaply — and exactly why it does not
cover the user's scenario, where the outlet is already down (Emiel, presumably already in hand from a
natural draw) but the **payload and the sink are not yet in play, and one of them (the sink) is not even
in hand** — reaching it requires casting a *second* Living Wish mid-turn, after the loop is already
running on the mana the first several steps generated.

### 3b. PROPOSED — extending recognition to an unassembled board

What the user is describing is a **prospective** version of the same predicate: not "is a loop already
running," but "does a sequence of this turn's available casts and activations lead to a running loop
plus a reachable payoff." Stated as a predicate over the pre-turn state `s`:

```
reachable(card, s)  :=  card is already on the battlefield
                     OR  card is in hand and castable this turn
                     OR  a Living-Wish-style tutor (tutor_to_hand + wish_from_sideboard) is itself
                         reachable(...) this turn, AND card sits in the sideboard pool it would search

assemblable(s) :=  exists a sequence of casts/activations this turn such that:
                      - an outlet becomes reachable(outlet, s)
                      - a payload becomes reachable(payload, s)
                      - RecogniseFlickerLoop on the resulting board reports net > 0
                      - a sink becomes reachable(sink, s), possibly via a SECOND wish resolved
                        after the loop is already generating float mana
```

The `reachable` half of this (battlefield-or-hand) is **already computed today**, just for a narrower
purpose: `EldraziFlickerProvider::TutorCandidates`'s ranking (same file, ~line 13675) builds exactly
`have_outlet` / `have_payload` / `have_sink` booleans by scanning the controller's battlefield **and
hand** for the same three param signatures (`blink_cost`, `etb_untap_lands`, `drain_cost` /
`exile_opponent_top_cost`), and gates promoting a wish target to the top tier on `have_outlet &&
have_payload` (mode 2, adopted, `MTG_EDF_TUTOR_RANK` default 2). That is the closest existing analogue to
"prospective assembly" in the codebase, and it is a real, working, generic-by-construction mechanism —
but it stops at a single wish's target choice. It does not model:

- a **second** wish, resolved later in the same turn, feeding off mana the first wish's fetch helped
  generate (the user's steps b and i);
- whether the loop, once assembled, actually reaches **enough** iterations to fund that second wish's
  cost *and* the sink's finishing count — `have_sink` is a boolean, not an amount;
- **which** of the two interchangeable sinks (Essence Depleter vs. Dimensional Infiltrator) the board can
  actually pay for — see §6 below, which finds a concrete, verified gap here.

Constructing and checking `assemblable(s)` for arbitrary sequences is a small search problem in its own
right, which is precisely the thing §4 argues the ordinary enumerator cannot afford to do generically.
The realistic proposal is not "search all sequences," but "recognize the ONE pattern this deck's design
already narrows everything else around" — wish-then-loop-then-wish-again — as a named, provider-scoped
pattern, the same way `RecogniseFlickerLoop` names the assembled-loop pattern instead of searching for
it. That scoping choice is discussed further in §5.

## 4. Why ordinary bounded search cannot find this line — checked against the actual pruning code, not assumed

The working hypothesis going in was that the loop is many iterations deep and each individual iteration
looks low-value, so a depth-bounded search under a work-unit budget prunes it before the payoff is
visible. That hypothesis was checked directly against `src/ai/SearchBudget.h` and the iterative-deepening
machinery in `src/ai/TurnSolver.cpp`. **The verdict: the general claim — "ordinary bounded search cannot
find this line" — is CONFIRMED, but the specific mechanism named in the hypothesis is not the one
responsible. The real mechanism is different, and it is more fundamental: the search never even
constructs the winning plan as a candidate, at any budget or any depth.**

### 4a. What the start-gate / work-unit budget actually does (VERIFIED) — and why it isn't the blocker here

`SearchBudget` (`src/ai/SearchBudget.h`) counts deterministic "work units" (one per simulated turn-step)
rather than wall-clock time. `TurnSolver::FullSearchLine` and the "single-depth escalation" ladder in
`TurnSolver.cpp` both search **turn 1, then turn 2, then turn 3, ...** of lookahead as successive
*passes*, and before starting pass `k` they estimate its cost by extrapolating the measured growth ratio
of the previous two passes (default `kDefaultGrowth = 6.0` before two passes exist), then skip the pass
— committing the best result found so far — if `estimate > kStartGateAlpha * remaining_budget`
(`kStartGateAlpha = 1.10`; see the comment block above `TurnSolver::FullSearchLine`, `src/ai/TurnSolver.cpp`
~line 29552, and the per-`sub_depth` loop ~line 30440). This is a real, deliberately deterministic
mechanism, and it genuinely governs how many further **turns** ahead the search is willing to look.

But the user's line resolves entirely **within one turn** (turn 3): every cast, both wishes, and every
loop iteration happen before the turn ends. A same-turn payoff does not need multi-turn lookahead to be
seen — it is checked at the leaf of a **single** turn's plan, via `ExtraLethalDamage` /
`ProjectsAlternateWin` (an addend evaluated once a plan for *this* turn is already assembled) and,
ultimately, by literally executing the plan and observing the result. So the cross-turn start-gate is not
what is excluding this line; a depth-1 search already evaluates whether *this* turn's plan wins, and
raising `depth`/budget would not help a plan that was never proposed in the first place.

### 4b. The actual mechanism (VERIFIED): a per-decision candidate-count CAP, not a budget cutoff

The real blocker is upstream of any budget: it is in how the plan enumerator decides **which activation
counts to offer as candidates at all**, for a repeatable ability. The generic default, in
`src/ai/DecisionProvider.h` (~lines 644–693), is:

```cpp
// Generic returns 1..min(3, max_affordable) -- the same hard bound every other K-count
// activation (ActivateRevealTop, ActivatePump) carries, because more than three activations of
// a value ability in one turn is fringe and the branching is not free.
//
// THE HOOK EXISTS FOR THE ONE CASE THAT BOUND IS WRONG: a blink loop can be SELF-FUNDING.
// ... A generic cap of 3 does not merely play the deck badly, it makes the deck's
// only win line invisible to the search: the kill needs ~20 iterations and the enumerator would
// never offer one.
virtual std::vector<int> BlinkActivationCounts(...) const
{
    ...
    const int kmax = std::min(3, max_affordable);
    for (int k = 1; k <= kmax; ++k) { out.push_back(k); }
    return out;
}
```

(`ManaSinkActivationCounts` in the same file carries the identical structure and the identical comment,
for Essence Depleter's drain and Dimensional Infiltrator's exile.) This is a **width** cap on the
candidate list handed to the search, not a **depth** or **budget** cutoff. No amount of search depth or
work-unit budget matters if the number "20" (or "53") is never in the list of numbers being considered in
the first place — an unlimited-budget, unlimited-depth search over a candidate set that only ever
contains `{1, 2, 3}` still never proposes the winning plan. This is exactly the class of gap the repo's
`flow-guided-tap-order` / mana-source-reservation work calls out for *tap* choices, generalized here to
*activation-count* choices: **tapping (and activation count) is not free to turn into a search branch,
so the generic default keeps it small, and a deck that structurally needs a large count is invisible
until something proposes that count as a candidate.**

That is exactly why `EldraziFlickerProvider` had to override both hooks (§3a) rather than simply raising
the generic cap: raising the shared default from 3 to, say, 60 would multiply the branching factor of
**every** repeatable-ability decision in **every** deck by up to 20x. The deck's own tractability history
shows how quickly unnarrowed branching becomes unaffordable even with a *correct*, deck-specific
narrowing already in place: before `BlinkTargetCandidates` / `LandAuraHostCandidates` narrowed the target
and host axes, one profiled `d3` game visited **1.38×10⁹ odometer positions** (`analysis-
EldraziDisplacerFlicker.md`, Stage 5). Even with the provider's narrowing shipped, the wish-target axis
alone is measured at **~72% of one pathological game's cost** at the required search width (same doc,
"TRACTABILITY" section). So the fix here could never be "widen the generic bound" — it has to be
"a provider recognizes the specific pattern and proposes the specific number," which is what §3a already
does for the assembled-loop case, and what §3b proposes extending to the not-yet-assembled case.

### 4c. Conclusion for this section

**Confirmed**: an ordinary, generic, bounded search cannot find this line, at any budget or depth.
**Refuted as originally framed**: it is not the iterative-deepening start gate trimming a many-turn-deep
line under a work-unit budget. The actual, verified mechanism is a per-decision *candidate-count cap* —
a branching-factor control, not a depth control — that never enumerates the winning count as a candidate
in the first place. The distinction matters for design: fixing a depth/budget problem would mean
"search more"; fixing a candidate-generation problem means "propose the right candidate," which is
strictly cheaper and is the shape the repo has already chosen twice (here and in Dragonstorm, next).

## 5. The precedent: Dragonstorm's go-off recognizer and storm-hold

This deck is not the first place a "search cannot find its own combo" problem showed up. Dragonstorm — a
ritual-storm deck whose kill is a chain of mana rituals into `Dragonstorm`, fetching dragons whose ETBs
(Scourge of Valkas) deal the actual damage — hit the identical shape of bug, and the fix that shipped for
it is the strongest existing argument that a recognizer-and-follow approach works and is worth the
engineering cost.

### 5a. The go-off recognizer (VERIFIED)

Documented in [`dragonstorm-goff-lethal-recognition.md`](dragonstorm-goff-lethal-recognition.md).
Dragonstorm is a **rollout-based** deck (no `value_leaf` model), so its greedy policy (`TurnSolver::Solve`)
drives every leaf evaluation at every search depth. The bug: `Dragonstorm` the spell has
`direct_damage == 0` in the engine's model — its damage comes from the *fetched dragons'* later ETB
triggers, which are not part of the casting spell's own damage field — and `DragonstormProvider` did not
override `ExtraLethalDamage` / `HasExtraLethalModel`, so the shared `GenericProvider` default (`return 0`,
`HasExtraLethalModel() == false`) applied. Consequence: the greedy/rollout `wins` check
(`wins = projected_atk + direct_dmg + extra_lethal >= opponent.life`) **never recognized the go-off as a
win**, so greedy treated `Dragonstorm` as an ordinary board-development spell, valued it by
board-development heuristics instead of lethality, and wasted rituals on setup turns because nothing
recognized a same-turn payoff for them.

The fix, now shipped default-on (`MTG_NO_DRAGONSTORM_GOFF` off-switch): `DragonstormProvider::
ExtraLethalDamage` / `HasExtraLethalModel` (`src/ai/DecisionProviders.cpp`) now project the go-off's ETB
burst via a helper (`GoOffSim`) that mirrors the real ETB-resolution code path, so the projected number is
the number execution would actually produce (storm count → dragons fetched → Scourge-of-Valkas ping
total). Measured on the deck's own train seeds: blind (unsearched) greedy loss-penalized average win turn
moved **7.14 → 6.81**; the shipped `d3`/`d5` search moved **−0.05 to −0.10** turns, every configuration
faster and none slower; the smoke and regression suites were rebaselined with **zero** other-deck moves.

The contract that makes this safe to ship aggressively: `ExtraLethalDamage` is **only ever an addend to a
projection**, never itself a decision or a win. Both this recognizer and its EldraziFlicker analogue
(§3a) **re-simulate the chosen line with `ApplyPlanDirect` before believing a lethal** — so an over-claim
by the recognizer can, at worst, mis-rank one candidate plan against another; it can never report a
result the actual simulated game did not produce.

### 5b. Storm-hold (VERIFIED) — the harder, cautionary half of the same precedent

A second, related rule — "don't spend a ritual on a merely-fair Dragon when the storm is worth holding
for" — was tried as an **unconditional** rule first, and it **failed**: measured on the blind
(unsearched) greedy policy it improved loss-penalized average win turn by ~−0.73, but on the shipped
`d5` search it made things **worse by ~+0.37** (`docs/design/dragonstorm-d0-divergence-digest.md`). The
diagnosis, worth restating exactly because it generalizes: this is an **option-pruning** rule (it removes
a choice from the greedy policy), not an **information-adding** one (like the go-off recognizer, which
only ever adds a number). A blind rollout leaf that unconditionally holds a ritual "for the storm" cannot
see a storm payoff it has not drawn yet — it just durdles, never reaches the storm, and every leaf the
search scores through that policy looks worse, so the search itself picks worse lines even though the
*idea* was correct.

The rescue: gate the prune on the payoff being **observable** by the leaf right now — hold the ritual
only when a storm finisher (`Dragonstorm` or `Apex of Wildfire`) is **already in hand**
(`storm_in_hand`, computed in `TurnSolver::Solve`, `src/ai/TurnSolver.cpp` ~line 14218, consumed by
`SubsetWastesAccelerant` ~line 4076, gated on `s_storm_hold` / `MTG_NO_STORM_HOLD` ~line 4074). With that
gate, the same code measured blind-greedy **−0.60** *and* shipped `d5` **−0.005** (neutral, no
regression) — adopted. The general rule the repo extracted from this and states explicitly in the
divergence-digest doc: **"an option-prune is safe when the leaf can observe the reason for it; unsafe
when it holds on faith."**

### 5c. What transfers to EldraziDisplacerFlicker

- **The addend/never-a-win contract already used** (§3a's `ExtraLethalDamage` / `ProjectsAlternateWin`
  for the *assembled* loop) is the right shape for whatever numeric contribution a prospective-assembly
  score makes to a plan's evaluation, and needs no new safety argument — it inherits the same
  execution-re-verifies guarantee.
- **But a prospective-assembly recognizer is necessarily more than an addend.** Unlike the assembled-loop
  case, recognizing "this line is worth assembling" has to *steer* — it has to change which wish target
  `TutorCandidates` proposes, which land-Aura host is chosen, and which cast order is preferred — before
  any of those cards are even on the battlefield to be measured. That makes it structurally an
  **option-pruning / steering** rule, in the same family as storm-hold, not the go-off recognizer's
  purely additive family. Storm-hold's lesson therefore applies directly: an unconditional "always chase
  the combo" steer risks durdling exactly the way the unconditional slow-dragon rule did, and must be
  gated on the payoff being **observable from the current decision** (e.g., an actual castable Living
  Wish in hand right now, not merely "this deck owns Living Wishes somewhere in its 60/8 cards") — the
  direct analogue of `storm_in_hand`.
- **The divergence-digest method is the concrete, already-proven way to find and validate the exact
  steering rule**, rather than hand-guessing it. Summarized from
  [`dragonstorm-d0-divergence-digest.md`](dragonstorm-d0-divergence-digest.md): instrument the deep,
  clairvoyant search to also compute what the shallow greedy/rollout policy would have done from the same
  state (`MTG_DIVERGENCE_LOG`); cluster the resulting divergences by outcome impact (not raw count — the
  doc is explicit that "count ≠ cost," and a candidate rule must be weighted by win-turn impact, not
  frequency); map the surviving pattern onto a named provider hook; validate the candidate on the blind
  (unsearched) loss-penalized average win turn **and, separately, on the shipped search configuration**,
  because — per the storm-hold lesson — an option-pruning rule can improve one while worsening the other.
  This method is already integrated into the `analyze-deck` skill's Stage 5i and
  `scripts/rollout_divergence_digest.py`, so applying it to this deck is not new infrastructure, only new
  analysis.

## 6. What a recognizer must prove before committing to a line

Per the Dragonstorm precedent, a projection is only trustworthy if it is cheap, conservative, and never
authoritative on its own. Three separate things must be established, and each already has a cheap
existing building block to reuse rather than reinvent:

1. **Reachability of every combo piece.** VERIFIED building block: `TutorCandidates`'s `have_outlet` /
   `have_payload` / `have_sink` scan (§3b) already checks battlefield-and-hand membership by card
   *parameters*, not by name. PROPOSED extension: widen "reachable" to include "in the sideboard pool a
   currently-castable Living Wish would search," and track it per-copy (four Living Wishes means up to
   four fetches are theoretically available in one turn, not one).

2. **Net-positive-per-iteration, proven against the board the assembly would actually produce.**
   VERIFIED building block: `RecogniseFlickerLoop`'s `net = refund − cost_mv > 0` arithmetic (§3a) is
   already exactly this proof, for a board that already exists. The genuinely new difficulty is that a
   prospective check needs this arithmetic evaluated against a board that does **not** exist yet — the
   board *after* casting Trace of Abundance, resolving a wish, and casting Cloud of Faeries. PROPOSED:
   rather than inventing new projection arithmetic for a hypothetical board, apply the actual candidate
   casts to a scratch `GameState` via the same executor path the search already trusts as ground truth
   (`apply_one` / `TurnSolver::ApplyPlanDirect`), then run the existing, unmodified `RecogniseFlickerLoop`
   against the *resulting real state*. This is the same idea, and could reuse the same machinery, as the
   `MTG_EXEC_FEAS` gate described in
   [`enumeration-feasibility-via-executor.md`](enumeration-feasibility-via-executor.md), which already
   treats "apply the plan for real and see what happens" as the one deck-agnostic ground truth the
   codebase trusts for feasibility, instead of maintaining a parallel approximate projection.

3. **Termination in a win within a bounded number of iterations.** VERIFIED building block:
   `FlickerGoOffCount` (§3a) already computes an exact iteration count from opponent life or remaining
   library size and clamps it (`kFlickerMaxIterations = 60`). This part of the proof generalizes directly
   to a prospective recognizer with no change — once the loop and the sink are established (steps 1–2),
   sizing the finish is already solved.

4. **Cost — this must be cheap enough to run at every decision.** VERIFIED: today's `RecogniseFlickerLoop`
   is cheap by construction — an early "no outlet on board" bail-out, then a handful of linear scans over
   the battlefield, no allocation, no `GameState` copy (its own comment stresses this, since it runs "once
   per considered subset on the rollout hot path"). A prospective version that must apply hypothetical
   casts to a scratch state (point 2 above) is **not** free in the same way — each candidate assembly
   sequence checked this way costs a real `GameState` copy and a handful of `apply_one` calls. **This is
   flagged as an open sizing question, not resolved by this document**: the design should bound *which*
   candidate assembly sequences get this treatment (e.g., only the one named pattern — "a castable Living
   Wish plus a reachable outlet plus a reachable payload" — rather than an open search over sequences),
   the same way §3a's recognizer bounds itself to one named pattern instead of searching for it.

5. **Execution remains the sole arbiter.** Whatever score or steering signal this recognizer contributes,
   it must follow the existing contract exactly: advisory only, re-verified by actually executing the
   chosen plan (`ApplyPlanDirect`) before any win is reported. A false positive can only waste a branch or
   mis-rank a plan; it can never fabricate a result the simulated game did not produce. This is not a new
   invention — it is the same guarantee `ExtraLethalDamage` and `ProjectsAlternateWin` already give for
   the assembled-loop case, extended unchanged to the prospective case.

## 7. The payoff must be chosen from what is reachable, not hardcoded

This deck has two structurally interchangeable win conditions — Essence Depleter (needs `{B}` to cast)
and Dimensional Infiltrator (needs `{U}` to cast) — and the user's own aside states the rule directly:
*if black is not available, use blue and Dimensional Infiltrator instead.* This is a specific instance of
a hard, repo-wide rule: **a card whose effect is "choose an X" must resolve X from deck/board state at
decision time; baking one deck's answer into `cards.json` or a provider is wrong for every other deck
and no audit catches it.** A recognizer that always reaches for "the" sink by name, rather than by
reachability, would violate this rule the first time it were reused (or even just re-measured) on a board
without the assumed color.

The good news is that the existing code in this deck already gets the *structural* half of this right:
`TutorCandidates`' ranking (§3b) is explicit that it is **"GENERIC BY CONSTRUCTION -- every tier ... is
read from the card's own PARAMS, never from a name"** — "a repeatable sink" is `drain_cost` /
`exile_opponent_top_cost`, "an outlet" is `blink_cost`, "a payload" is `etb_untap_lands`. Any recognizer
built on top of this should preserve that discipline rather than special-case either card by name.

**A concrete, verified gap found while researching this document, which any prospective recognizer must
not inherit uncritically:** `EldraziFlickerProvider::TutorCandidates`'s final tie-break
(`src/ai/DecisionProviders.cpp`, ~lines 13723–13733) sorts same-tier candidates "cheaper first" by raw
`CardDefinition::card::m_mana_cost.ManaValue()`. Essence Depleter is `{2}{B}` (mana value 3); Dimensional
Infiltrator is `{1}{U}` (mana value 2). By this tie-break, **Dimensional Infiltrator always sorts ahead of
Essence Depleter**, regardless of which color the board can actually produce — the comparison is a raw
cost number, not a castability check. On a board with a live black source and no blue source (the
opposite of the user's own stated fallback), this tie-break would still rank the *uncastable* option
first. This is presented here as a verified reading of the code, not a measured bug (it has not been
shown to change any measured outcome), but it is exactly the shape of defect this document's premise
warns against, and a prospective-assembly recognizer that hands the search "go fetch the sink" should
rank or gate candidate sinks by demonstrated castability against the board's actual (or assembly-projected)
mana, not by mana value alone.

## 8. Risks and open questions

- **A recognizer that steers incorrectly is worse than none at all, and this is not a hypothetical
  concern — it already happened once on this exact axis.** `analysis-EldraziDisplacerFlicker.md` records
  that the *first* version of the wish-ranking (mode 1, ungated) took a sink on turn 2, "several turns
  before there is any mana to pour into it," while the plain unranked engine correctly prioritized ramp.
  The fix (mode 2, gating the kill tier on `have_outlet && have_payload`) is exactly the "gate the steer
  on the payoff being observable" pattern from storm-hold (§5b) — re-derived independently on this deck
  before this document was written. A prospective-assembly extension is a strictly *more* aggressive
  steer (it must fire before any of the pieces are even in play) and needs at least as much of a
  visibility gate, validated the same way: train seeds and a disjoint held-out seed set, per the
  heuristic-optimization skill, before it ships default-on. Reasoning about it, as this document has
  done, is not a substitute for measuring it.
- **Cost of the prospective check itself is unresolved** (§6, point 4) — an unbounded search over
  candidate assembly sequences reintroduces the exact branching-factor problem §4 diagnoses; the design
  needs a bound (e.g., a single named "wish → loop → wish" pattern) rather than an open search, and the
  bound's own cost has not been estimated here.
- **The color-blind tie-break in §7 is a second, narrower correctness gap.** A recognizer that inherits
  it could recommend wishing for a payoff the board cannot cast, which — because Living Wish is a
  4-copy singleton resource per card — is a real, non-trivial cost (a wasted wish is not just a wasted
  turn of mana, it permanently removes one of the deck's four fetches for that specific need) even though
  it can never produce a false win report.
- **Whether the plan enumerator can represent a same-turn "cast Wish A, play what it fetched, cast Wish
  B" sequence at all, and at what enumeration cost, is not established by this document.** The existing
  wish mechanic (`docs/design/analysis-EldraziDisplacerFlicker.md`, Stage 5's "sideboard zone + wish
  mechanic" section) confirms a single wish's fetch is available to later casts within the same plan; a
  *second*, later wish depending on mana the first wish's fetch helped generate was not traced through
  the enumerator's code as part of this research and should be verified before any implementation begins.
- **Whether the specific rejection the user observed at step (a) is fully explained by the already-shipped
  land-aura fix (`34abe486`) is unverified here** (§1's scoping note) — this document deliberately did not
  run the engine to check, since it is scoped to the recognition-heuristic question, not a re-audit of
  that fix.
- **Measurement discipline reminder**: any of the above, if built, must be reported and adopted in terms
  of average win turn deltas (with train/held-out separation and a stated sample size), never in
  win/loss language, per this repository's standing convention.
