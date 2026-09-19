# Breakpoints should key on CARDS ENTERING HAND, not on what was cast

**Status: OPEN, audited 2026-08-25, not started.** USER design direction: *"it makes sense to audit
when the draws are happening or perhaps even to go so far as designing breakpoints around when we
put cards in hand."* This doc is the audit that motivates it and is self-contained.

## The defect class

A breakpoint exists so the turn can be re-decided once new castable material appears. Today the
engine decides whether to arm one by asking **"what card was CAST?"** -- every clause in the
executor's arming list (`AIEngine.cpp`, `note_draw_engine`) and in the rollout's predicate
(`PlanOpensBreakpoint`, `TurnSolver.cpp`) tests `d->params.X` or `d->tmpl` on the CAST card:

    DrawUntilNonland | cascade_max_mv | stages_cards | impulse_exile
    solo_target_trick + (cast_draw | creates_treasures)     [site 5]
    tutor_to_hand | damage_equals_top_mv                    [MTG_ACQ_RESOLVE]
    tutor_to_top                                            [MTG_TOP_RESOLVE, default OFF]
    etb_dig_count                                           [MTG_ACQ_DIG, "Cast path only"]
    is_equipment + a live draw_on_equipment_etb watcher      [site 6]

That is a taxonomy of **causes**, maintained by hand, one bit per card class. The thing it is
trying to approximate -- "a card became available mid-phase" -- is an **effect**. Every time the two
drift apart, a line becomes unreachable at ANY depth or budget, and the failure is SILENT: the game
still plays a sensible turn, and no win-turn average says "we drew a card with mana open and did not
cast it."

Two instances found by accident rather than by looking, which is the argument for fixing the shape
rather than the instances:

* **Stoneforge put** (fixed 2026-08-25, `MTG_SF_PUT_BP`): site 6 tested
  `a.kind == Action::Kind::CastFromHand`, so putting an Equipment onto the battlefield under a
  Puresteel Paladin drew a card the turn could never act on.
* **`etb_dig_count` is documented as "Cast path only (see flag note)"** -- an admission of the same
  bug, left in place.

## The audit (2026-08-25)

All 30 sites that append to a hand were enumerated (`grep 'hand\.push_back\|hand\.insert'` over
`src/core` and `src/ai`). Classified against the arming list:

| hand-entry route | function | armed today? |
|---|---|---|
| the turn's draw step | `GameEngine::DrawStep` | n/a -- precedes the main phase, every plan sees it |
| cantrip / EI / impulse / Treasure Hunt | `ResolveExpressiveIteration`, `StageTopLibraryCard`, `ResolveDrawUntilNonland` | YES (sites 0/1/2/3) |
| tutor to hand, cast route | `PerformTutor` | YES (`MTG_ACQ_RESOLVE`) |
| ETB dig, cast route | `PerformEtbDig` | YES (`MTG_ACQ_DIG`) -- **cast route only** |
| **ETB of a creature PUT into play** | `PerformTutor` / `PerformEtbDig` reached from a put | **NO** |
| **activated ability -> hand** | `ApplyGarthActivate` | **NO** |
| **planeswalker ability -> hand** | `ApplyLoyaltyAbility` | **NO** |
| **death trigger -> hand** | `OnCreatureDies` | **NO** |
| **land bounced to hand** | `BounceKarooLand` | **NO** |
| **dig to hand** | `SoulfireDig` | **NO** |

### The live instance: Goblins

**Goblin Matron carries `tutor_to_hand`,** and Goblins puts it onto the battlefield WITHOUT casting
it by two routes in the deck: **Goblin Lackey** (`combat_damage_puts_subtype_from_hand`) and
**Muxus, Goblin Grandee** (`etb_reveal_put_subtypes`). Either way Matron's ETB tutors a Goblin into
hand and nothing arms, because no card was cast -- so the tutored Goblin cannot be cast that turn,
at any depth or budget. Goblins has been in the suite far longer than KittyEquipment.

Knights' **Acclaimed Contender** (`etb_dig_count`) is the same story via the documented cast-only gate.

## Why this is worth a refactor rather than more per-site patches

The USER's bar (`no-lossy-truncation`, extended 2026-08-25): *"I'm looking for the highest degree of
correctness we can manage and especially don't want this truncated if I decide to run a high-depth
high-budget set of games (which could be relevant when choosing between two cards that are very
close in terms of effectiveness and rarely diverge)."* A rare truncation is not a rare problem for
deck screening -- it hides exactly the games where two close arms diverge, which is where the
comparison is decided.

**There is no choke point today.** `ap.hand.push_back(...)` is open-coded at all 30 sites; no
`EnterHand()` helper exists. That absence is the mechanical reason the taxonomy drifts, and closing
it is most of the fix.

## THE RULE, 2026-09-19 (USER) -- this SUPERSEDES the "Proposed shape" below

> **USER:** *"So that way there is no question about when the breakpoints should open.
> It is exactly when there are NEW OPTIONS TO CONSIDER."*

and, spelling out what "new options" means: *"that means either hand status or ability status has
changed"*, where *"ability status meaning we have a new ability we can activate **that we could not
before**."*

That last clause is the whole specification in four words -- **that we could not before**. It makes
the test a DELTA, never a property of a card or an event, which is why it cannot drift.

Everything below is its mechanics. Note what it buys beyond
correctness: the arming question stops being a judgment call per card class, so there is nothing
left to get wrong as cards are added -- which is precisely the failure mode the audit above
documents ten times over.

**USER, verbatim:** *"I would actually like to change our approach for breakpoints to be 100%
general:*

1. *Hand or staged cards changed.*
2. *New ability can be activated.*

*So 2 would not trigger for Thallid because it has no counters or a tap ability on a creature
without haste because those cannot be activated. On the other hand if you say cast a spell that
added the spore counters or gave the creature with the tap ability haste the breakpoint would
open."*

**Read condition 2 carefully, because it is not what the engine implements today and the difference
is the whole point. It is keyed on the ABILITY BECOMING ACTIVATABLE, not on a permanent entering.**
The two come apart in both directions, and today's site 9 is wrong in both:

| situation | today (site 9: entered_this_turn + has-an-activation) | the rule |
|---|---|---|
| cast Utopia Mycon, no Saproling on board | **ARMS** (no victim test -- see the over-arm section) | no arm: not activatable |
| cast a plain Thallid (0 spore counters) | no arm (spore counters correctly checked) | no arm -- agrees |
| a Sporesower trigger takes a Thallid to 3 counters | **NO ARM** -- nothing entered | **ARM**: the spore ability just became activatable |
| a haste grant lands on a creature with a {T} ability | **NO ARM** -- nothing entered | **ARM**: the {T} ability just became activatable |
| Psychotrope saccing a Saproling to draw, later turn | **NO ARM** (see the Fungus section) | **ARM** by condition 1: a card entered hand |

So the rule is a **state-delta over the LEGAL ACTION SET**, and both of its conditions are the same
shape: *a new option appeared that the plan was not chosen against*. Condition 1 is new options in
hand; condition 2 is new options on the battlefield. `entered_this_turn` is merely one CAUSE of
condition 2, and keying on the cause is exactly the taxonomy-drift defect this whole document is
about -- the engine has simply been making the same mistake one zone over.

**The natural implementation, and it is condemnation's mirror image.** A plan is chosen against the
set of actions available at that moment. Condemnation asks *"what was in that set and was declined?"*
Condition 2 asks *"what is in the set now that was never in it?"* -- so the same snapshot serves
both, and `PostEntryActivationPending` generalises by DROPPING its `entered_this_turn` filter and
comparing against that snapshot instead of against entry. That also deletes the missing-victim bug
rather than patching it: an ability with no legal victim was never in the new set to begin with.

**Cost is the open question, and the USER has already supplied the answer's shape** (*"Activated
ability breakpoints could potentially be pruned by a provider heuristic"*): the engine computes the
delta generically and a provider may decline a class it has measured as worthless. Size the delta
computation before assuming it is affordable -- recomputing the activatable set after every state
change is the naive reading and is certainly too expensive; the snapshot-diff above is the cheap one
because it only has to run where a breakpoint would be considered anyway.

## Proposed shape (2026-08-25 -- superseded in part by the rule above; steps 1 and 3 still stand)

1. Introduce one `EnterHand(state, controller, card, Reason)` choke point and route all 30 sites
   through it. Mechanical, individually reviewable, byte-identical on its own.
2. Arm the deferred re-solve THERE, keyed on the effect. This subsumes site 6, `MTG_SF_PUT_BP`, the
   ACQ family and every "NO" row above, and cannot drift as cards are added.
3. Keep `BpSiteMask` numbering intact, or renumber it deliberately -- the rollout and the executor
   MUST agree on which breakpoints are counted, or a committed continuation replays at the wrong
   one. That is the single largest hazard in this change.

### Things to measure, not assume

* **Cost.** Arming on every hand-entry is strictly more armings than today. The draw step must stay
  excluded, and rollout-interior entries need the same sink-depth gate the cast route uses. Measure
  in deterministic GameWorkMeter units (`test/tools/kitty_ab/cost.py`) -- NOT wall clock, which this
  box cannot measure under contention.
* **Size the hole first.** Before the refactor, a counter at the unarmed routes above, run over all
  suite decks, says which of them actually fire and how often. That decides whether this is a
  Goblins fix or an everything fix.
* **The log reporter has the same blind spot.** `GameEngine::ResolveStack`'s draw reporter is
  param-keyed too, so `drawn_card_used.py` reads 0 mid-main draws on Goblins even though the deck
  tutors. Fixing the arming without fixing the reporter leaves the census unable to confirm it.

## 2026-09-19 -- THE RULING RESTATED, PLUS THE COST ANSWER THIS DOC WAS MISSING

**USER, 2026-09-19:** *"I do want all draw or put-in-hand effects to create a breakpoint."* Same
direction as the 2026-08-25 steer above, now stated without the hedge. And the refinement that
answers this doc's own main objection:

**USER:** *"Activated ability breakpoints could potentially be pruned by a provider heuristic to say
'you don't need a breakpoint here'."*

That is the split the repo already runs everywhere else, applied to arming: **the ENGINE arms on the
EFFECT (a card entered hand -- generic, complete, cannot drift as cards are added), and the PROVIDER
prunes the ones this deck does not need (per-deck, reviewable, and the only place a
cost/quality judgment is allowed to live).** It dissolves the "Cost" bullet above: arming
generically is no longer a bet that every new arming pays for itself, because a deck that measures a
class as worthless can decline it by NAME of the class rather than by the engine forgetting to arm.
Note the asymmetry that makes this safe in the direction the no-lossy bar cares about -- a missing
ARM is unreachable at any budget and silent, while an unhelpful arm is only cost.

### The live instance that prompted it: Fungus / Psychotrope Thallid

Psychotrope Thallid is `{1}, Sacrifice a Saproling: Draw a card` (`sac_creature_outlet` +
`sac_outlet_draw`). It is the deck's ONLY draw. **It opens no breakpoint on any turn but the one it
enters**, measured 2026-09-19:

* `MTG_BP_PROBE=1`, 12 games, seed 70900: **104,036 breakpoint continuation enumerations**, and
  `MTG_BP_SITES=0` leaves that count **byte-identical** -- so every one comes from an UNCONDITIONAL
  site (8 or 9). The deck holds no snow card, so all of it is **site 9, post-entry activation**.
* Site 9 requires `entered_this_turn`, so it fires on the turn Utopia Mycon / Psychotrope is CAST
  and never again. The sac-for-draw itself is not a breakpoint class at all: `sac_outlet_draw`
  appears exactly ONCE in `TurnSolver.cpp` (line ~16689, a damage-bound term) and nowhere in the
  breakpoint code. Psychotrope is modelled via `sac_creature_outlet`, so it does not even reach
  `PermAbilityMode::SacDraw` (which is keyed on `sac_draw_cost`).
* The only ACTIVATED-draw class is site 8, narrowly keyed on the snow `tap_draw_cost` (Scrying
  Sheets / Frost Augur). It is the exact template for the generalisation -- including the part only
  that route knows, `site_activated=true` (`TurnSolver.cpp` ~26500), and the lockstep warning beside
  it: the EXECUTOR twin must bind the same `CantripOrderScope` or played != scored.

**Why it matters here specifically (USER):** *"That is the primary thing that condemnation helps
with, since it avoids reconsidering cards. That won't help for a lot of normal games, but it could
be quite useful on the ones where we have a lot of mana, Psychotrope and Saprolings."* That board is
the expensive-node shape in the Fungus phase-A tail (see `fungus-token-search-cost.md`), so the
missing class and the label cost tail are the same problem seen from two ends.

### The OPPOSITE defect, found in the same pass: site 9 arms when the ability cannot be activated

Worth fixing alongside, because it is the same "judge the effect, not the card" error pointing the
other way -- an OVER-arm rather than a missing one.

`TurnSolver::PostEntryActivationPending` (~10265) accepts a sac outlet on the mana cost alone:

```cpp
if (pp.sac_creature_outlet && (!pp.sac_creature_cost.has_value()
                               || affordable(pp.sac_creature_cost, p.card)))
{ if (!pp.sac_outlet_self_only) { return true; } ... }
```

It never consults `sac_creature_requires_subtype` and never checks that a legal victim EXISTS.
**Utopia Mycon has no mana cost**, so `!has_value()` makes this unconditionally true the turn it
enters -- and Mycon is a 4-of at mv 1, normally cast well before any Saproling exists (spore
counters need three upkeeps). So the deck arms a breakpoint for an ability that provably cannot be
activated, repeatedly, on the cheapest and most-cast card in the list.

The fix is already written: `CanonicalSacVictim` (`SpellEffects.h` ~7393) resolves a legal victim
honouring `need_sub`, and is called from `TurnSolver.cpp` ~16625 and `DecisionProviders.cpp` ~20218
-- just not from this gate. Same class as the recorded lesson that a gate must judge the object the
decision CONSUMES, not the outlet that offers it.

**NOT YET MEASURED**: the above is read from the code plus the deck's card data. Before fixing,
count how many site-9 arms have no legal victim (a counter at the gate, over the suite), so the size
of the over-arm is a number rather than an argument -- the same discipline the "Size the hole first"
bullet asks for in the other direction.

### ...and the CORRECT pattern is already in this function, four lines earlier

**USER, 2026-09-19**, giving the prune's motivating case: *"breakpoints after playing a Thallid
wouldn't be necessary, since activating it is not possible."* Exactly right -- a freshly-cast
Thallid has ZERO spore counters and cannot pay the 3-counter cost, so arming there is pure cost.

**The engine already does this correctly for the spore ability, and the two clauses are adjacent:**

```cpp
// Spore outlet (the Thallid family): the cost is COUNTERS, not mana ...      <- CORRECT: checks supply
if (pp.spore_saproling_cost > 0 && p.spore_counters >= pp.spore_saproling_cost) { return true; }
...
if (pp.sac_creature_outlet && (!pp.sac_creature_cost.has_value()             <- BROKEN: no victim test
                               || affordable(pp.sac_creature_cost, p.card))) { ... return true; }
```

`CardHasPostEntryActivation` likewise omits `spore_saproling_cost` from the wave-0 fan-out, so a
plain Thallid never fans out either. Both halves of the spore path are right.

**So for THIS deck the prune the user describes needs no provider hook at all -- it is the engine's
own affordability gate, applied to the sac outlet with the same rigour it already applies to the
spore counters.** That is the cheaper and more general fix, and it should be tried FIRST: a provider
hook that declined the Mycon arm would be a per-deck patch over a generic gate that is simply
incomplete. Reserve `ProviderPrunesBreakpointAt(...)` for the case that survives a correct
affordability test -- an arming that IS legal but that a deck knows is not worth re-solving for.

## 2026-09-19 -- HALF 2 IS IMPLEMENTED AND DEFAULT ON (`MTG_BP_ABILITY_DELTA`)

**Status change: the ABILITY half of the user's rule is built, gated, and shipping ON.** The HAND
half is separately live as of the same day, from the other work stream: site 10 (`put_in_hand`,
`MTG_BP_PUT_IN_HAND`) went DEFAULT ON at USER direction 2026-09-18 -- *"We do need to open the
breakpoints regardless. Then the idea is to see whether condemnation can help at all."* So both
conditions of *"1. Hand or staged cards changed. 2. New ability can be activated"* now have an
engine implementation. What remains open is condemnation, not the rule.

### What changed

`TurnSolver::PostEntryActivationPending` no longer asks *"did a permanent enter this turn?"*. It
asks the user's question directly: **is an ability activatable NOW that was not activatable when the
plan started?**

* `OwnPermanentNumbers` (a list of card numbers) became
  `SnapshotActivatableAbilities` (a sorted list of one key per ACTIVATABLE ABILITY).
* Both the snapshot and the gate build their keys through **one shared enumerator**,
  `CollectActivationKeys`. This is the load-bearing detail: with two copies, an ability could look
  "new" because the two sides asked different questions rather than because anything changed. Same
  reasoning as `deferred_site_index` being a single lambda.
* `entered_this_turn` is simply dropped. The proxy is subsumed -- a permanent the plan cast has no
  keys in the snapshot, so all of its keys are new.
* `MTG_BP_ABILITY_DELTA=0` restores the old proxy exactly (and with it, the victim bug, deliberately
  -- the hatch has to be a byte-exact revert to be worth having). Routed through the `heurarm` slot
  `BP_ABILITY_DELTA` so both arms fit ONE pooled batch instead of a forbidden per-arm wave.

### Why a DECLINED activation still cannot be re-opened

This was the property the `entered_this_turn` filter existed to protect -- the first smoke of site 9
re-fired on a walker cast the turn before, and the greedy continuation overrode the plan's own
loyalty choice. The delta rule protects it **structurally rather than incidentally**: an activation
the plan declined was activatable at plan start, so its key is in the snapshot, so it is not new, so
it does not arm. The guarantee is strictly stronger than the proxy's.

### Measured (Fungus, 12 games, seed 70900, `MTG_BP_PROBE=1`)

| `MTG_BP_ABILITY_DELTA` | BP continuation enums | avg turns |
|---|---|---|
| `0` (entered-this-turn proxy) | 104,036 | 5.4167 |
| `1` (delta rule) | **47,132** | 5.4167 |

**A 55% cut in breakpoint work at identical play.** The removed 57k were overwhelmingly the
over-arm this doc documents above: Utopia Mycon cast with no Saproling to sacrifice. Fixing the gate
to ask whether a legal victim exists (`CanonicalSacVictim`, the function this doc already named)
removes them at the source. Gates: scenarios 103/0; **smoke ALL PASS 83/0/0-new with
`play-changed=0` and GT unchanged**, so the saving is pure cost reclamation, not a behaviour swap.

**This is the strongest available evidence for the USER's 2026-09-19 remark** that *"realistically we
may not need condemnation if we do the change for the ability breakpoints"*. Condemnation prunes
breakpoints AFTER opening them; the delta rule declines to open the junk ones at all, and on this
deck that is worth more than condemnation has ever been measured to be worth anywhere (see
`breakpoint-condemnation-status.md`, whose reach/yield/reinvestment ceiling section bounds the
filter's best case well under this).

### HONEST GAP: the ADDITIVE half is implemented but NOT yet demonstrated

Every deck probed so far shows the delta rule **only ever removing arms**, never adding one:

| deck | delta=1 | delta=0 |
|---|---|---|
| Fungus | 47,132 | 104,036 |
| Melira Pod | 11,289 | 12,332 |
| KittyEquipment | 22,341 | 22,341 |
| Dragons | 4 | 4 |

The additive cases -- a Sporesower trigger taking a resident Thallid to its third counter, a haste
grant landing on a {T} ability -- are what motivated the rule, and **none of them fired in these
samples.** There is a structural reason to expect them to be rare rather than absent: mana
availability generally *decreases* across a plan (the snapshot is taken with lands untapped), and
spore counters move at UPKEEP, outside any plan apply. So the common "an ability became affordable"
direction mostly cannot happen mid-plan.

**Do not record the additive half as working on the strength of the cost win, which is entirely the
subtractive half.** It needs a scenario that constructs the case directly -- a resident creature
with a {T} ability plus a haste grant cast in the same plan is the cheapest one, and
`tap_ability_self_funding_payable` is the nearest existing scenario to model it on. Until that
exists this is *reasoned, not measured*.

### Still open after this

* **Condemnation on Fungus is still 0.0%** -- `MTG_BP_CLASSIFY` is default OFF, which is the whole
  reason, and flipping it globally is the live USER call in `breakpoint-condemnation-status.md`.
* **Fungus is not in the regression suite**, so neither smoke nor regression covers its play. Its
  only signals are the probe above and the 7 references (2 of which carry PRE-EXISTING play-drift).
* The site-10 hand half and this ability half are now two separate arming sites answering one rule.
  Worth asking later whether they should be one site; they are numbered separately today because
  `BpSiteMask` is simultaneously searchability AND `bp_at` numbering, and renumbering breaks the
  apply/executor lockstep.
