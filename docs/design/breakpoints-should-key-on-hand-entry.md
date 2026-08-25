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

## Proposed shape

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
