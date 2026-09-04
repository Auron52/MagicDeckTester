# Analysis ledger — Fluctuator

**Deck:** `decks/Fluctuator/Fluctuator.cod` (60 cards: 42 lands, 18 spells)
**Started:** 2026-09-04
**Branch:** `phase-1-2-deck-analyzer`
**Status:** IN PROGRESS — Stage 2 (implement gaps)

This is the git-tracked per-deck ledger required by `.claude/skills/analyze-deck.md`
("Running this at scale"). It is the durable memory a resumed session or a second
machine reads to continue. Keep it current as stages complete.

---

## 1. The deck, and what it is trying to do

| n | card | role |
|---|---|---|
| 4 | **Fluctuator** `{2}` Artifact | **The engine.** "Cycling abilities you activate cost {2} less to activate." Every cycler in this deck costs {2} or {1}, so under a Fluctuator they all cost **{0}** — cycling becomes unbounded and free. |
| 4 | **Drannith Stinger** `{1}{R}` 2/2 | **The wincon.** "Whenever you cycle another card, this creature deals 1 damage to each opponent." Cycling {1}. Fluctuator + Stinger = cycle the deck for ~30 damage in one turn. |
| 4 | **Hollow One** `{5}` 4/4 | Beater. "Costs {2} less to cast for each card you've cycled or discarded this turn" → free after 3 cycles. Cycling {2}. |
| 4 | **Unearth** `{B}` Sorcery | Rebuy. "Return target creature card with mana value 3 or less from your graveyard to the battlefield." Only legal target in this deck is **Drannith Stinger** (MV 2); Hollow One is MV 5. Cycling {2}. |
| 1 | **Forsake the Worldly** `{2}{W}` Instant | Exile target artifact or enchantment. Cycling {2}. |
| 1 | **Enlightened Tutor** `{W}` Instant | Already implemented. Finds Fluctuator (or Hollow One). |
| 42 | cycling lands | Every land in the deck cycles for `{2}`. |

**The line:** land, land, Fluctuator (T2/T3) → Drannith Stinger → cycle the library
for free, 1 damage per cycle. Enlightened Tutor finds Fluctuator. Unearth rebuys a
Stinger that was cycled away.

## 2. Full card list (Scryfall-verified 2026-09-04)

All 42 lands cycle `{2}`. All enter tapped **except Blasted Landscape and Capital City**.

| card | n | type | produces | enters tapped | cycling |
|---|---|---|---|---|---|
| Drifting Meadow | 2 | Land | W | yes | {2} |
| Polluted Mire | 4 | Land | B | yes | {2} |
| Smoldering Crater | 2 | Land | R | yes | {2} |
| Blasted Landscape | 4 | Land | C | **no** | {2} |
| Capital City | 4 | Land — Town | C (+ `{1},{T}`: any colour) | **no** | {2} |
| Fetid Pools | 4 | Land — Island Swamp | U/B | yes | {2} |
| Irrigated Farmland | 4 | Land — Plains Island | W/U | yes | {2} |
| Canyon Slough | 4 | Land — Swamp Mountain | B/R | yes | {2} |
| Sheltered Thicket | 4 | Land — Mountain Forest | R/G | yes | {2} |
| Scattered Groves | 4 | Land — Forest Plains | G/W | yes | {2} |
| Glittering Massif | 4 | Land — Mountain Plains | R/W | yes | {2} |
| Festering Thicket | 2 | Land — Swamp Forest | B/G | yes | {2} |

Colour sources: **W 14, B 14, R 14** (+ U 8, G 10 which the deck never needs — the
bicycle lands' off-colour halves). 8 colourless-only lands (Blasted Landscape,
Capital City).

## 3. Stage 1 — coverage (2026-09-04)

`python3 scripts/analyze_deck.py decks/Fluctuator/Fluctuator.cod --coverage-only`

* `missing`: **17 of 18 cards.** Only Enlightened Tutor is implemented.
* `sideboard.reachable`: false (deck has no sideboard) — correct, no wish effects.
* Enlightened Tutor's existing `deferred` note is a tutor-target heuristic belonging to
  another deck (`enabler_then_wincon` → Tainted Remedy / Aria). **This must be
  re-checked for Fluctuator** — see open item O-1.

## 4. Engine capability audit — what already exists

`CardParams::cycling_cost` exists (`src/cards/CardDatabase.h:338`) and cycling is
modelled structurally (repo convention: cards.json does **not** tag `Cycling` in
`keywords` — see `src/core/Card.h:81-87` and `audit_card_fields.py`'s
`MODELED_ELSEWHERE_KEYWORDS`).

**But cycling today is a "dig when stuck" heuristic, not a searched action.**
`ShouldConsiderDig` (`src/core/SpellEffects.h:11026`) only lets the engine cycle when
it is starved for action: it bails when a better card source is in hand, and requires
`lands_controlled >= 2`. That is exactly backwards for this deck, where cycling is
free and is the *primary* action. See §5 for the gap list.

## 5. Gaps to build (Stage 2)

| # | gap | tier | status |
|---|---|---|---|
| G-1 | 11 plain cycling lands (`basic_land` + `produces` + `enters_tapped` + `cycling_cost`) | 1 | **DONE** |
| G-2 | Capital City's `{1},{T}: Add one mana of any color` | 2 | **DEFERRED (provisional)** — see §6 |
| G-3 | **Fluctuator** — static cycling-cost reduction from a battlefield permanent | 3 | |
| G-4 | **Drannith Stinger** — "whenever you cycle another card" trigger → 1 damage to each opponent | 3 | |
| G-5 | **Hollow One** — cost reduction scaling with cards cycled/discarded this turn | 3 | |
| G-6 | **Unearth** — return creature MV≤3 from graveyard to battlefield (reanimation) | 3 | |
| G-7 | **Forsake the Worldly** — exile target artifact/enchantment | 1 | **DONE** (`goldfish_inert`, Remand precedent) |
| G-8 | **Cycling as a searched action**, not a dig-when-stuck heuristic (core-invariant issue) | 3 | |

### 5.1 Engine architecture found (recon, 2026-09-04)

**Cycling today is one half of a generic "dig when stuck" heuristic** — there is no
`Cycling` action kind and no cycling helper. Three shared predicates in
`src/core/SpellEffects.h` (`ShouldConsiderDig` :11037, `SelectDigSource` :11095,
`HasAnyDigSource` :11123) feed **three duplicated execute blocks**:

| id | site | world |
|---|---|---|
| D1 | `AIEngine::PerformDig` — `src/ai/AIEngine.cpp:4156` (cycling branch :4184-4204) | executor |
| D2 | `ApplyPlanDirect` human-play `DigDraw` — `src/ai/TurnSolver.cpp:19961-20002` | claude-play |
| D3 | `ApplyPlanDirect` rollout auto-dig loop — `src/ai/TurnSolver.cpp:20584-20678` | search rollout |

Six sites read the **raw** `cycling_cost` (no cost helper exists):
P1 `AIEngine.cpp:4188`, P2 `AIEngine.cpp:4192`, P3 `TurnSolver.cpp:19987`,
P4 `TurnSolver.cpp:20622`, P5 `SpellEffects.h:11104`, P6 `TurnSolver.cpp:24239`.
**P1/P5/P6 (affordability) must move in lockstep with P2/P3/P4 (payment)** or the
filters and the payment disagree and the loops silently `break`.

**Three findings that shape the whole build:**

1. **`GenericProvider` returns false/empty for ALL THREE dig predicates**
   (`DecisionProviders.cpp:301-303`). A brand-new deck routes to Generic, so
   **Fluctuator would never cycle at all.** This deck therefore *needs* its own
   provider — not to narrow the search (the core invariant's usual concern) but to
   *enable* a capability the generic default switches off. That is the
   "capability-narrowing default" class flagged in prior work.
2. **`SelectDigSource` returns the FIRST affordable cycler in hand order**
   (`SpellEffects.h:11098-11106`). That is precisely the arbitrary
   "first-in-enumeration-order" pick the core invariant forbids: in this deck it
   would happily cycle away the only Fluctuator. The fix belongs in the provider.
3. **Both dig loops are capped at 16 iterations** (`AIEngine.cpp` `guard`,
   `TurnSolver.cpp` `dig_guard`). Under a Fluctuator every cycle is free, so the
   deck's real turn is "cycle 30+ cards"; a hard 16 is a generic limiter on the
   deck's actual wincon.

**Precedents to copy (do not invent):**
* Cost reduction from a battlefield permanent → `reduces_creature_activation` /
  `EffectiveActivationCost` (`SpellEffects.h:9170`). Note Training Grounds floors at
  **1**; Fluctuator floors at **0**, so use the `EffectiveSpellCost` floor
  (`std::max(0, generic - N)`, `ManaPayment.cpp:718`), and pips are never touched.
* Player-action trigger → `FireSacrificeWatchers` (`SpellEffects.h:3943`) is the exact
  shape. Damage-to-opponent idiom → Mana Cannons (`SpellEffects.h:2174-2189`); it must
  set `state.opponent_lost_life_this_turn`.
* Per-turn counter → `Player::cards_drawn_this_turn` (`Player.h:38`), reset in
  **lockstep** at `GameEngine.cpp:212` *and* `TurnSolver.cpp:21011`. It is already
  incremented at all three cycle sites.
* Reanimation → **nothing exists.** Closest: the put-onto-battlefield block of
  `PerformTutorToBattlefield` (`SpellEffects.h:1502-1516`) plus the filtered graveyard
  scan of `regrow_multicolored` (`SpellEffects.h:4365-4380`).

**⚠ Prune soundness (G-5).** Hollow One's discount grows *within* a turn, so it is the
anti-monotonic class: `ManaPruneBound` (`TurnSolver.cpp:13189`) must bail (`INT_MAX`)
for it exactly as it does for affinity, and the selection-exact gate's `block`
breadcrumb at `TurnSolver.cpp:13272-13274` applies.

## 5.2 What was built (2026-09-04)

**New `CardParams` fields** (all default 0 → every other deck byte-identical):

| param | card | where it is read |
|---|---|---|
| `reduces_cycling_activation` | Fluctuator | `EffectiveCyclingCost` (`SpellEffects.h`), routed into all six cycling-cost sites P1–P6 |
| `cycle_trigger_damage_each_opponent` | Drannith Stinger | `FireCycleWatchers`, called from `OnCardCycled` at D1/D2/D3 |
| `cost_less_per_cycle_or_discard` | Hollow One | `EffectiveSpellCost` (`ManaPayment.cpp`) + the two prune bails |
| `reanimate_creature_max_mv` | Unearth | `PerformReanimateFromGraveyard` + the enumerator's no-legal-target gate |

**New shared helpers** (`src/core/SpellEffects.h`):
* `EffectiveCyclingCost` / `EffectiveCyclingCostFor` — floor **zero**, generic half only.
* `FireCycleWatchers` + `OnCardCycled` — one call so a future cycle site cannot pick up the
  counter and miss the trigger (or vice versa).
* `ReanimateTargetIndex` / `HasReanimateTarget` / `PerformReanimateFromGraveyard` — the
  scan is shared by the resolution AND the castability gate, so "the spell is offered" and
  "the spell finds a target" can never disagree.

**New per-turn counter** `Player::cards_cycled_or_discarded_this_turn`, reset in lockstep at
`GameEngine.cpp` (UntapStep) and `TurnSolver.cpp` (SimulateEndAndStartNextTurn).

**Prune soundness:** `ManaPruneBound` bails (INT_MAX) and `ManaGateTerm::block` is set for
`cost_less_per_cycle_or_discard`, because the discount grows *within* the turn.

**State-key folds:** both `TurnSolver`'s sim key and `Dominance.h` fold the new counter, each
gated on *nonzero AND the hand holding a reader*. The `Dominance` gate matters: every dig deck
(treasure_hunt / auras / dragons) drives the counter nonzero, so an unconditional fold would
have churned three unrelated decks' GT for a value none of them reads.

**New `FluctuatorProvider`** (`DecisionProviders.{h,cpp}`) — routed **above the `anti` check**
because Enlightened Tutor's `tutor_to_top` sets the anti-lifegain signature on its own (the
recorded misroute class). Signature OR-ed across four cards so a deckbuilding swap cannot lose
it. It holds three hooks; see §8.

## 6. Open items / questions surfaced to the user (NOT blocking — defaults taken)

| id | item | resolution |
|---|---|---|
| O-1 | Enlightened Tutor's `tutor_heuristic: enabler_then_wincon` names another deck's cards (Tainted Remedy / Aria). | **Resolved, no action.** `tutor_heuristic` is parsed (`CardDatabase.cpp:741`) but **read nowhere in the engine** — it is dead data. The actual pick comes from the provider's `TutorCandidates`, which Fluctuator inherits from Generic. No cross-deck contamination. |
| O-2 | **Capital City's `{1},{T}: any colour` filter mode is unimplemented** (G-2). | Modelled as `produces: [C]`, which **under**-rates the deck's fixing (the safe direction). Needs user sign-off. |
| O-3 | **Hollow One's counter covers cycles but not discards.** | Provably exact for this deck (no discard outlet in the 60; the cleanup shed is post-main and zeroed at the next untap). Needs user sign-off. |
| O-4 | **Performance:** ~16 s/game at d3/b200, worst game 95 s, against a suite budget of ~1 s. | **Partly fixed** — see §7.1. The dig loop's nested re-solve was the dominant cost *and* a play bug; fixing it took CPU down 34% and the average from 5.675 → 5.000. A heavy tail remains (6 of 40 games > 30 s), so the deck is **not yet suite-ready at the shared budget**. |

## 7.1 The dig re-solve: a Treasure-Hunt assumption that halts this deck's combo

Root cause of both the cost and a real misplay. The shared dig loop
(`TurnSolver.cpp`, `ApplyPlanDirect`) digs *through* lands and, on the first **nonland**
drawn, runs a nested breakpoint **re-solve** and then **breaks** — commented
*"once we have action we are no longer stuck."* That is correct for Treasure Hunt, where
digging is a means of finding action. It is wrong for Fluctuator, where **cycling IS the
action**: breaking stops the engine mid-combo, and because the nested re-solve itself digs,
every node of the search spawned a nested search (recursion).

The fix is a provider hook, `DecisionProvider::DigResolveOnlyWhenCastable()` (default
**false**, so every other deck is byte-identical), which `FluctuatorProvider` turns on: when
**nothing in hand is castable with the mana currently available**, skip the re-solve and keep
cycling. Sound because the re-solve exists solely to deploy a card the dig found — if nothing
is castable it cannot deploy anything, and the only state the cycle changed is one card
hand→graveyard, one card library→hand, and (for a free cycle) no mana at all. The castability
probe is deliberately **whole-hand**, not just-the-drawn-card, which is what keeps it safe when
a cycled creature turns a *held* Unearth live.

**Levers:** `MTG_FLUCT_DIG_RESOLVE=0` or `MTG_UNPRUNE=digresolve` restores the
re-solve-always form for the standing A/B.

**Measured A/B** (40 games, seed 9001, d3/b200, per-game via `MTG_DUMP_WINS`):

| arm | avg turn-to-win | CPU | games > 30 s |
|---|---|---|---|
| re-solve always (`=0`) | 5.6750 | 10m42 | 9 |
| skip when nothing castable (**shipped**) | **5.0000** | **7m12** | 6 |

**Per game: 20 of 40 FASTER, 0 slower.** Strictly dominant — it is a play-quality fix that
also happens to cut a third of the cost, not a quality-for-speed trade.

## 7.2 Multi-depth sanity (5b) — 40 games, seed 9001, b200

| depth | avg turn-to-win | unwon | note |
|---|---|---|---|
| 0 | 7.3750 | 9 / 40 | greedy rollout policy |
| 3 | 5.0000 | 0 / 40 | |
| 5 | 5.0000 | 0 / 40 | **per-game identical to d3** — the search converges by d3 |

Monotonic (wins non-decreasing, avg non-increasing) and plausible: win turns run 3–8 with the
mode at 4, which matches the deck's real clock (Fluctuator T2–3 → Drannith Stinger → cycle out).
The d0→d3 gap of 2.4 turns is large; per 5i that is a rollout-policy signal worth a look if this
deck is ever tuned further.

## 7. Verification (Stage 5)

| check | status |
|---|---|
| Stage 3 coverage clean (`missing: []`, no PARTIAL status) | **PASS** |
| Stage 4a provider audit — no existing deck misrouted | **PASS** |
| Smoke tier byte-identity (shared-code changes) | running |
| 5a nonconv / fd-diverge | not yet |
| 5b multi-depth sanity | not yet |
| 5c2 horizon-honest tie-break | not yet |
| 5d claude-play sweep | not yet |
| 5f perf gate | **FAILING — see O-4** |
| 5h viewer decision surface | not yet |

First numbers (pre-profile, CLI depth only): d0 `avg 8.00` (2/5 unwon), d3/b200 `avg 6.10`
over 10 games (all won).

## 8. Stage 6a disclosure — heuristics this deck relies on

| hook | what it decides | class | note |
|---|---|---|---|
| `FluctuatorProvider::HasAnyDigSource` | is there a cycler in hand | **enabling, not narrowing** | Generic returns false, which switches cycling off entirely |
| `FluctuatorProvider::ShouldConsiderDig` | default/horizon "should we cycle" | enabling + heuristic | free cycle → always; paid cycle → the shared gate |
| `FluctuatorProvider::SelectDigSource` | **which card to cycle** | **narrowing (returns one)** | replaces the shared helper's arbitrary *first-in-hand-order* pick. Never cycles the last Fluctuator or the last Drannith Stinger while none is on the battlefield. |
| `DigDecisionSearched() == true` | dig/no-dig is a searched axis | opens the search | |
| Unearth target | highest MV ≤ cap, ties by lowest copy number | **structurally forced here** | Drannith Stinger (MV 2) is the only creature in the 60 within the cap |

## Claude-play sweep

- commit: `71e547d8` (+ the `ReanimateTargetIndex` fix this sweep produced)
- seeds: 45001 games: 16
- flags: 0 unresolved

**16 games, one Opus agent each, seed base 45001 (disjoint from every suite seed).**
It found **one confirmed engine bug — in code written this session — and it is exactly
the class of bug this step exists to catch.**

### The bug it found (FIXED)

`ReanimateTargetIndex` (`src/core/SpellEffects.h`) read the graveyard card's mana value
off the **raw zone `Card`**. Every library/hand/graveyard `Card` is a name-only
placeholder built by `DeckLoader::MakePlaceholder`, so `.ManaValue()` reads **0 for a
5-drop**. Consequences:

* Unearth's "mana value 3 or less" cap **never fired** — it would reanimate **Hollow One
  (MV 5)**, an illegal play, and the enumerator offered the cast even when the graveyard's
  only creature was a Hollow One (no legal target, CR 601.2c).
* The documented "highest MV within the cap" pick degenerated to "lowest per-copy number".

This file's own header block documents this exact trap and names two prior instances
(Garth's Regrowth taking graveyard slot 0 because every MV read 0; Deathrite's fuel gate).
The fix routes **both** characteristics through `ZoneCard()` — the single accessor the
header prescribes — and tracks `best_mv` explicitly.

* Repro (pre-fix), now correctly offering no Unearth cast:
  `--claude-play --seed 45016 --game-index 15 --choices "1,1,0,4,0,4,0,3,1,4,1,1,2,2,5,5,5,5,2,2,2,2"`
* Cost of the bug: **avg 5.0200 → 5.0500** over 100 games at d3/b200 — the deck was
  measurably stronger than the rules allow. 5.0500 is the honest number.
* Smoke re-run after the fix: 51 passed, **0 configs changed** (byte-identity holds).

### What the sweep verified as CORRECT (repeatedly, independently)

Every one of the four new mechanics was confirmed by many agents against `cards.json`:

| mechanic | how it was confirmed |
|---|---|
| Fluctuator → `{0}` cycling | cycle plans offered **and executed with every land tapped and zero available mana** |
| Drannith Stinger ping | exactly −1 per cycle **per Stinger**; two Stingers produce two separate 1-damage events; cycles with no Stinger out deal **0**, correctly honouring "another card"; the Stinger's own cycling is `{1}`, not `{2}` |
| Hollow One discount | sharp and off-by-one clean — absent at 0/1/2 cycles, castable at exactly 3 ({5} − 3×{2} → {0}); resets per turn |
| Unearth targeting | absent with an empty graveyard **with `{B}` untapped** (so mana is not the confound), present the moment a MV-2 Stinger hits the yard, and resolves graveyard→battlefield |
| Forsake the Worldly | never offered as a cast (`goldfish_inert`), always offered as a cycler |
| enters-tapped | matches cards.json exactly — Blasted Landscape and Capital City untapped, all others tapped |

### Win-turn deltas (weak signal, but unusually one-sided)

Claude ≥ the search in **16 of 16**; strictly faster in **9**. That is far above the
skill's stated expectation ("a guided Claude is competitive but rarely faster"), so it is
worth recording as a **search-quality lead**, not dismissed:

| gi | ai | claude | | gi | ai | claude |
|---|---|---|---|---|---|---|
| 0 | 7 | 7 | | 8 | 4 | **3** |
| 1 | 5 | **3** | | 9 | 5 | **4** |
| 2 | 3 | 3 | | 10 | 4 | **3** |
| 3 | 5 | **4** | | 11 | 6 | **5** |
| 4 | 4 | 4 | | 12 | 4 | **3** |
| 5 | 5 | **4** | | 13 | 5 | **4** |
| 6 | 6 | 6 | | 14 | 3 | 3 |
| 7 | 6 | 6 | | 15 | 6 | **4** |

The recurring line the agents found and the search missed is **cycle a Drannith Stinger
into the graveyard, then Unearth it back** — 2 mana for a 2/2 plus a cycle trigger plus a
card, and (since every red source in the deck enters tapped) often the *only* route to an
early Stinger. Two agents independently identified it. This is a genuine 5e/5i follow-up.

### Dismissed, with reasons

* **Plan-list duplicates (2–3× byte-identical entries).** Reported by nearly every agent.
  One traced it to the unsurfaced `Plan::bp_choice` searched axis (`AppendBreakpointVariants`)
  and verified that choosing among the twins yields identical states. Cosmetic clutter in
  the human-play menu; no legality or state impact. Worth a viewer follow-up (`main.cpp`'s
  own comment calls byte-identical menu twins "the one thing a decision menu must never do").
* **Cycle plans never carry a land drop.** Deliberate — `AppendHumanPlayDigPlans` carries a
  2026-08-27 user directive against it; the engine re-prompts within the phase, so
  land-then-cycle is fully reachable. Four agents probed this and cleared it.
* **Combat resolves after the opponent is already at ≤0 life.** Two agents flagged it; one
  verified in source that the post-combat `CheckWinCondition` placement is deliberate
  lockstep with the search leaf (`SimulateToEndImpl`). `win_turn` is unaffected.
* **`/tmp` collisions between sweep agents.** Two agents clobbered each other's shared
  helper scripts and briefly read another game's state. Both detected it and re-derived
  their results through unique paths / direct invocations. **Process note for future
  fan-outs: give each agent a unique temp path.**

## 9. Measured but NOT adopted

**`mulligan.max_lands` (rejected).** One agent traced its win-turn gap to
`mulligan.max_lands = 5` — a hardcoded generic default (`src/ai/MulliganProfile.h:97`) that
the analyzer never scales per deck, while `AIEngine.cpp:636` hard-rejects any opener above
it. In a **42-land (70%) deck where every land is a free cantrip under Fluctuator**, that
plausibly discards the deck's best hands, and the agent showed a concrete case where the
engine mulliganed a 6-land + Fluctuator opener down to five cards.

The mechanism is real, but **the measurement refutes the fix**: 100 paired games, d3/b200,

| `max_lands` | avg turn-to-win |
|---|---|
| 5 (shipped) | **5.0200** |
| 7 (no effective cap) | 5.0800 |

Raising it is **worse**, so it does not clear the adoption bar and was not changed. Recorded
here because the reasoning is compelling and someone will propose it again. (This is also a
reminder that a claude-play misplay candidate is a *lead*, not a verdict — the aggregate at
play settings decides.)

## Approved deferrals

*(none yet — O-2 and O-3 are PROVISIONAL until the user signs them off)*
