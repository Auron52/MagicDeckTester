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

## Follow-up 2026-10-13: the main-1 dig deleted a main-2 cast (MTG_DIG_DEFER_MAIN2)

On a deck whose casts are split across the mains (the main-phase filter, FiveColour), main 1 is
"stuck" by construction -- the filter withheld its casts -- so this gate fired at the end of main 1
and spent the mana a withheld Main2 cast needed. FiveColour gi1106 (seed 6211107): T2 cycled Zagoth
Triome off Stomping Ground + Blood Crypt + Birds, main 2 could no longer emit Faeburrow Elder, and
the game won T5 instead of T4. The heuristic dig now WAITS for main 2 (same loop, both mains since
D7) while a withheld cast is payable; see `main-phase-classification.md`, "The apply-side half",
and `TurnSolver::Main1DigDefersToMain2`. The gate itself is unchanged.

Observation for the owner of this heuristic (not changed): the "nothing castable" probe compares
MV against the count of LANDS (`MV <= lands`), so mana creatures and rocks are invisible to it --
Faeburrow (MV 3) read as uncastable off two lands + Birds. That errs toward digging, the opposite
of the "erring toward NOT digging is the safe direction" note at the probe.

## 2026-10-13: the dig is a SEARCHED choice, never automatic (MTG_DIG_SEARCHED_CHOICE)

**USER, on gi1106's T2 Triome cycle:** *"'Main 1's automatic cycle' This seems to be a bit of a problem
in of itself. It shouldn't be automatic."* The 2026-09-29 build left the generic provider's gate as the
whole decision: with no dig axis, every plan's apply ran `ShouldConsiderDig` at the end and DUG whenever
it passed, so the search was never offered "don't". MTG_DIG_DEFER_MAIN2 (the morning's narrow fix) moved
that automatic dig to main 2 on one class of state; this removes the automatic outcome itself.

**How the ruling of 2026-09-29 and this one fit together.** That ruling declined the *dig-while-affordable*
axis for the generic provider (the clairvoyant search would cycle exactly when it can see a better top
card). That half stands: no `dig_choice 1` variant is emitted for a non-opted provider. What goes is only
the FORCED outcome. The gate stays the provider's restriction (no gate, no dig); where it passes, the search
now sees both worlds and the gate's answer is the tie-break (the base plan comes first, so equal scores keep
the provider's heuristic).

### The map: every place a cycle / sac-to-draw is performed

| site | who decides | class |
|---|---|---|
| `ApplyPlanDirect` dig loop, base plan of an opt-in deck (Auras, Fluctuator, Dragons) with its 0/1 variants | searched (dig_choice 0 / 1 / base) | (a) |
| same loop, EVERY plan of a non-opted deck (GenericProvider: FiveColour, Pirates, EDF, Soldiers, WhiteKnights; TreasureHunt) | the gate, no sibling | **(b) -- fixed: no-dig twin** |
| same loop, the PASS a host appends after the enumeration (FSLineWin's idle node, FSLineTail's do-nothing main 2, SolveWithLookahead's empty set, the NC instrument) -- the empty-of-gas state itself, every provider | the gate, no sibling | **(b) -- fixed: twin on the pass** |
| same loop, an opt-in base whose source the PRE-CAST pool could not pay (made affordable / brought in by this plan's own land drop: a Canopy / Islet / Clearing played this turn) | the gate, no sibling | **(b) -- fixed: pre-gate = "a source exists or the plan's drop is a sac-draw land"** |
| same loop, an axis clone (scry / tutor / Ponder / bounce / ... variant) | the gate, no sibling | (b) residual -- the one-axis-at-a-time gap (no-greedy audit A4/C6) |
| same loop, a wave-0 breakpoint variant whose breakpoint fired BEFORE the dig | the gate, no sibling | (b) residual, same gap. A variant whose breakpoint is still pending reaches it THROUGH the dig (site 4): the dig is the variant's premise, not a skipped choice. Deferred-wave variants are cloned from the base AND its twin, so those come in dig / no-dig pairs |
| same loop, a source DRAWN mid-apply (the pre-gate cannot see it) | the gate | (b) residual, measured 0 at d3 b10 |
| same loop, the go-off SEED plans (null seed) | the gate | (b) residual, unbounded search only (label / b0) |
| same loop, the horizon leaf's greedy plan (`Solve`, HorizonLeaf permit; m1 and now-labelled m2) | the gate | (c) permitted |
| `AIEngine::UseSurplusLandAbilities` at d0 (the d0 runner's `Solve` plan) | the gate | (c) permitted |
| `AIEngine::UseSurplusLandAbilities` on the uncommitted depth>0 path (no recorded script) | re-decided by heuristic, IGNORING the searched plan's dig_choice | **(b) / lockstep gap -- fixed: honours 0 / 1 / 2** |
| executor replay of `Action::Kind::DigDraw` on a committed line | whatever the scored line did | inherits its plan's class |
| SimulateToEndImpl's empty-main-2 apply skip (`MTG_M2_SKIP_EMPTY_APPLY`) | skipped the apply of a searched pass that DUG | **fixed: a 2 / 1 pass is not "empty"** |
| enumerated draw activations (tap-draw Frost Augur / Scrying Sheets / Mariposa / Troyan, Clue sac-draw, Recruitment Officer, Psychotrope Thallid), ETB dig, Ponder / scry | plan actions or axes | (a) |
| human play P3 / P6 cycling plans | the person | n/a |

### The change

* **Twin.** `AppendSubdecisionAxes` emits, for every base plan of a deck holding a dig source, a no-dig twin
  (`dig_choice 0`; an opt-in deck keeps its 0 + 1), and marks the base `dig_choice 2` ("the gate decides, a
  sibling exists"). The four hosts that append a pass after the enumeration do the same for the pass
  (`AppendIdleDigSiblings`). Clones made by later axes keep -1.
* **Pre-gate** under the lever: a source exists now, or the plan's own drop is a sac-draw land (sound for
  both arrivals a land drop can cause), instead of "payable from the pre-cast pool".
* **Exact dedup / fold.** A twin whose base did not dig is the base's apply exactly. `PlanIsAxisVariant`
  now covers dig variants (the 9d widening had missed them), and the **dig fold** (`DigFoldMemo`,
  `Plan::dig_group`, the bounce fold's shape) declines such a twin BEFORE the unit charge in FSLineWin,
  FSLineTail and SolveWithLookahead. Exactness gate: d3 `--budget-ms 0` digests identical with and without
  the fold on all 8 dig decks run (EDF's b0 games take ~5 min each and were left out).
* **2 is not deferred.** MTG_DIG_DEFER_MAIN2 applies to -1 only, in the apply and in the executor.
* **Executor.** `UseSurplusLandAbilities(state, resolve_stack, plan.dig_choice)`: 0 skips, 1 digs while
  affordable, 2 runs the gate without the deferral, -1 unchanged.
* `MTG_DIG_SEARCHED_CHOICE` (heurarm slot `DIG_SEARCHED_CHOICE`, appended last; reader
  `DigSearchedChoiceOn()` in EngineFlags.h). `=0` is byte-identical to the tree before (verified: 24/24
  jobs, 12 decks x 2 cells, against the pre-change binary; again 22/22 after the fold).
* `MTG_DIG_CENSUS=1` prints, at exit, how many variants were emitted / folded and which plans dug
  (`twin` = a base with a sibling, `axis1`, `leaf` = permitted, `auto` = in-window with no sibling, split by
  cause). `MTG_TRACE=digauto` names each residual.

### gi1106 and MTG_DIG_DEFER_MAIN2

gi1106 (seed 6211107) under the lever: **T4 at d8/b0, at the shipped d6/b20 and at d3/b10, with
`MTG_DIG_DEFER_MAIN2` on AND with it `=0`** (lever `=0`: T4 with the deferral, T5 without -- the pre-change
behaviour, reproduced). Main 1's pass now carries its no-dig twin, so "keep the mana for the main-2
Faeburrow" is a searched world and the search takes it.

**Is the deferral redundant?** For the decision it was built for, yes: in the pooled A/B the FiveColour
`defer0` arm (lever on, deferral `=0`) is **digest-identical to `new` on all 50 chunks (2,500 games, every
cell)**. It is not PROVABLY inert -- it still shapes the -1 plans that have no sibling (the horizon playout's
greedy plan, axis clones, unbounded-search seeds) -- but no measured game moves. Proposed for deletion as a
USER call (it is a heuristic on the playout policy now, not a correctness fix); not deleted here.

### Measured

One pooled batch (`logs/dig_agent/make_dig_ab.py`, 47,500 games), fresh seeds 8800001+, every dig deck,
arms `new` (lever on) vs `dig0` (`MTG_DIG_SEARCHED_CHOICE=0`) paired by seed / game index, plus a FiveColour
`defer0` arm (lever on, `MTG_DIG_DEFER_MAIN2=0`). Unwon = 9. Cells: shipped (value_play depth, b20) 200 /
d3 b10 300 / d0 2000 games per arm.

| deck | ship new-dig0 | d3 new-dig0 | d0 | per-deck net | units x (ship / d3) | play changed (chunks of 50) |
|---|---|---|---|---|---|---|
| FiveColour | 0 | +0.0033 (0 faster / 1 slower) | identical | **+1** | 1.021 / 1.003 | ship 1/4, d3 4/6 |
| Pirates | 0 | 0 | identical | 0 | 1.034 / 1.012 | none (digest-identical) |
| EDF | 0 | 0 | identical | 0 | 1.002 / 1.001 | none |
| Soldiers | 0 | 0 | identical | 0 | 1.015 / 0.999 | none |
| WhiteKnights | 0 | 0 | identical | 0 | 1.000 / 1.000 | none |
| treasure_hunt | 0 | +0.0100 (0 / 3) | identical | **+3** | 1.236 / 1.154 | ship 1/4, d3 2/6 |
| Auras | 0 | 0 | identical | 0 | 1.027 / 0.936 | ship 1/4, d3 1/6 |
| Fluctuator | -0.0050 (3 / 1) | -0.0067 (2 / 0) | identical | **-3** | 1.069 / 0.983 | ship 3/4, d3 6/6 |
| Dragons | 0 | 0 | identical | 0 | 0.983 / 0.977 | none |

**Every slower game recovers at stage 1** (`--depth W --budget-ms 100`, W = the dig0 arm's win turn), so
none needed stage 2: fivecolour d3 gi23 (seed 8805024, 5 vs 4), th d3 gi1 / gi3 / gi16 (seeds 8905002 /
8905004 / 8905017, 6 vs 5, 5 vs 4, 5 vs 4), fluctuator ship gi28 (seed 8940029, unwon vs 7). They are
BUDGET CHURN: th gi1's T3 decision reached searched depth 1 with the lever (T5 unverified, played
"(defer) + Land's Edge") where the lever-off search reached depth 3 and verified T5 off Thundering Falls.
Treasure Hunt is the expensive case because its gate passes on most turns, so its twins are REAL
alternatives, not duplicates the fold can drop (units x1.15-1.24). **The per-deck bar (net <= 0) is not met
on this sample for treasure_hunt (+3) and FiveColour (+1)** -- both entirely budget churn at d3 b10; the
remedy that fits the completeness rule would be a budget-staged admission of the twins (a later wave), not
a gate. Reported for the USER's decision; the lever ships default ON as instructed, `=0` is the hatch.

**The fold matters.** Before it (same arms, the ship cells and EDF d3 of the run it replaced) units ran
x1.13-1.33 on six decks and EDF d3 x1.41 -- 93-100% of the twins were exact duplicates paying a unit, a
GameState copy and an apply each (EDF: 220,551 twins for 0 digs in 40 games); and EDF lost a ship game
(gi38) to that churn alone. With the fold EDF is x1.00 and digest-identical to `=0`.

### Clairvoyance

**Which instrument can strip it.** `MTG_SHUFFLE_SALT_SEARCH` cannot: it salts mid-game SHUFFLE events
(fetch, tutor, Ponder -- `SearchShuffleSeed`), and a cycle's draw is the plain top card, not a shuffle.
`MTG_HONEST_PLAY` cannot either: it reshuffles only the FUTURE turns inside a rollout, while the root
candidate's own apply -- where the dig draws -- still reads the true library. **`MTG_NC_SEARCH` can**: every
real decision ranks its candidates by the win turn averaged over K reshuffles of the unseen library (each
candidate's apply draws from the reshuffled library, so the dig's card is unknown), then executes the best
against the true library. The executor honours the chosen twin on that path (`UseSurplusLandAbilities`).

**Clairvoyant result** (the table above): the searched choice changed realised play in few chunks and made
no deck faster except Fluctuator (an opt-in deck, where the lever's effect is the wider pre-gate, the pass's
siblings and the executor honouring the choice). So the clairvoyant search did NOT manufacture known-draw
speed here; its costs are budget churn.

**Non-clairvoyant result** (one pooled batch, `MTG_NC_SEARCH=1 MTG_NC_K=16 MTG_NC_DEPTH=1`, d3, 300 games per
arm, fresh seeds 8900001+, `scripts/dig_choice_ab/make_nc.py`): FiveColour **+3** (0 faster / 2 slower:
gi83 seed 8905084 6 vs 4, gi185 seed 8905186 6 vs 5), treasure_hunt **+1** (0 / 1: gi233 seed 9005234 5 vs
4), Pirates and Soldiers 0 (identical outcomes). NC has no budget, so these are DECISION effects: offered the
no-dig twin, the reshuffle-averaged ranker sometimes declined a dig the gate would have taken, and lost
time by it; it never gained. That is the USER's prior from the other side -- *"a non-clairvoyant engine
would not want to [cycle] (or only very rarely)"* was about adding digs; here the gate's digs, when it
passes, look right in expectation, and second-guessing them did not pay. Not root-caused (parked): K=16 is
a noisy ranker, so these three may be sampling error rather than a real preference.

### Resume (the work was parked 2026-10-13 for a value-leaf generation; un-parked 2026-10-14 -- items 1-6 DONE, see each)

The tooling is committed in `scripts/dig_choice_ab/` (written in the worktree's gitignored
`logs/dig_agent/`; the scripts hard-code that worktree's path in `WT=` / `sys.path` -- edit those two lines to
run from elsewhere): `make_dig_ab.py` (pooled A/B + `--analyse`), `make_recovery.py` (two-stage recovery),
`make_nc.py` (non-clairvoyant A/B), `make_exact.py` / `make_ident.py` (b0 exactness, byte-identity),
`census.sh` / `census2.sh` (`MTG_DIG_CENSUS`), `g1106.sh` / `g1106_all.sh`, `fdoracle.sh`, `game2.sh`
(one game, both arms, traced), `run_batch.sh` (one pooled `--batch`, 6 threads, cores 0-5). Binaries ran from
the build volume (`build/scratch/dig_agent/`), deck files from the primary checkout (the keep-table cache
keys on the sidecar's canonical path, so a worktree path rebuilds a 1-3 GB table per deck).

1. **gi1106 on the final binary.** The T4-at-three-cells result above was measured on the pre-fold build
   (v2). The fold is exact at b0 (so d8/b0 stands), but it re-prices finite budgets: re-run
   `bash logs/dig_agent/g1106_all.sh <mtg> final` (four lever combinations x d8b0 / ship / d3b10).
   **DONE 2026-10-14 (mtg.v7 = the final binary + MTG_DIG_TWIN_LAST):** T4 at all three cells with the lever on,
   with or without MTG_DIG_DEFER_MAIN2, and with MTG_DIG_TWIN_LAST=1; T4 with the lever off and the deferral on;
   T5 only with both off (the pre-fix tree). The searched dig alone repairs gi1106 at every cell.
2. **Full unit suite on the final binary** (`mtg-test`, 1032 cases). Green on the pre-fold build; the 9
   dig cases (`-tc="Dig searched choice*,Main-1 dig waits*"`) are green on the final one.
   **DONE 2026-10-14:** 1032 / 1032 on the final binary (and with MTG_DIG_TWIN_LAST=1).
3. **Residual census on the final binary**: `bash logs/dig_agent/census2.sh <mtg> <out> 40` (lever on,
   `MTG_TRACE=digauto`). The numbers in the map are from the pre-fold build; the fold does not change which
   plans dig, only which twins are applied, so they should hold.
   **DONE 2026-10-14** (40 games, d3 b10, `auto` = in-window dig with no sibling): FiveColour 48 (bp_premise 36 /
   bp_after 12), Pirates 98 (66 / 0, axis 32), Soldiers 32 (24 / 8), treasure_hunt 626 (509 / 44, axis 73), Auras
   983 (734 / 248), Fluctuator 9,193 (5,092 / 3,994, axis 107); idle 0 and base 0-1 everywhere. The classes are the
   map's residual rows: `bp_premise` is a pending breakpoint reached THROUGH the dig (its no-dig world is the
   base's twin), while `bp_after` (a wave-0 variant whose breakpoint fired before the dig) and `axis` (an axis
   clone) are the one-axis-at-a-time gap -- real automatic digs still inside the window, largest on Fluctuator.
4. **Executor lockstep**: `bash logs/dig_agent/fdoracle.sh <mtg>` counts `[fd-diverge]` (committed line
   not realised) lever on vs off, d3 b10, six dig decks x 100 games.
   **DONE 2026-10-14:** one `[fd-diverge]` in each of lever on / off / on + MTG_DIG_TWIN_LAST, the same game
   (seed 8660086: predicted 5, realised 6) -- the lever does not touch executor lockstep.
5. **The per-deck bar** for treasure_hunt (+3) and FiveColour (+1): a larger held-out sample at d3 b10 for
   those two decks (`make_dig_ab.py --decks th,fivecolour --cells d3 --seed0 <fresh> --scale 4`), and, if
   the USER wants the churn removed rather than accepted, a budget-staged admission of the twins (a later
   wave: the completeness-preserving shape, per CLAUDE.md) instead of any gate.
   **DONE 2026-10-14** -- see "Measured 2026-10-14" at the end: FiveColour now meets the bar, treasure_hunt
   does not (+2 to +4 at every d3 budget).
6. **Root-cause the three non-clairvoyant slower games** (decision effects, not budget): each arm as one game,
   e.g. `MTG_NC_SEARCH=1 MTG_NC_K=16 MTG_NC_DEPTH=1 mtg decks/FiveColour/FiveColour.cod --profile
   decks/FiveColour/FiveColour.profile.json --seed 8905084 --game-index 83 --games 1 --depth 3 --budget-ms 10
   --ignore-play-profile` with `MTG_DIG_SEARCHED_CHOICE=1` / `=0` and `MTG_TRACE=dig` (also gi185 seed 8905186;
   treasure_hunt gi233 seed 9005234). Raising K (32-64) tells sampling noise from a real preference.
   **DONE 2026-10-14: sampling noise.** Replayed each arm as one game (`game2.sh`, d3 b10): at K=16 the gaps
   reproduce (FiveColour gi83 6 vs 4, gi185 6 vs 5, treasure_hunt gi233 5 vs 4); at **K=64 all three tie**
   (gi83 6 / 6, gi185 5 / 5, gi233 5 / 5). The K=16 ranker's average over 16 reshuffles is too noisy to separate
   dig from no-dig on these boards, so the NC +3 / +1 is not a preference for declining the gate's digs.
7. **USER decisions owed**: adopt the lever default ON (clairvoyant bar unmet on two decks by budget churn
   alone; non-clairvoyant also +3 FiveColour / +1 treasure_hunt); delete MTG_DIG_DEFER_MAIN2 (inert in 2,500
   FiveColour games under the lever).

### A later wave for the twins: what each shape can buy (analysis 2026-10-13, while parked; nothing built or run)

**Where the churn comes from.** A finite budget is spent through `FullSearchLine`'s iterative-deepening
ladder: passes 1..D, each admitted by the start gate (`estimate = c_prev x c_prev / c_prev2 <= alpha x
remaining`). Where the gate passes on most turns (treasure_hunt), a twin is a REAL second child, so `c_prev`
and the growth ratio both rise and the gate refuses the deeper pass sooner (th gi1: searched depth 1 with the
lever, 3 without). The extra cost sits at REFUTED nodes, where every plan must be scored whatever its order.
The fold already removed the exact duplicates (EDF d3 x1.41 -> x1.00), so what is left is the width of a
real decision. Note the ship cells were net 0 for both decks; the +3 / +1 are the d3 b10 cell, which is a
suite cell (`fivecolour 3 1001 150 10`), so the bar applies there too.

1. **Node-local order: twins after every non-twin plan** (a `MoveOrderPlans` key just below
   `wins_this_turn`). Twins are cloned after `AppendBreakpointVariants` and from base plans only, so a twin has
   no wave-0 family to split; the other plans' shifted positions are what the post-sort `bp_base` remap
   already handles (and `w0_order_ok` already requires it). A pure reorder, so it is complete at every budget, and the B&B loops
   are result-preserving under reordering (`MoveOrderPlans`' own argument). The base always precedes its twin,
   so the fold keeps working. **It saves only where a base plan finds an in-window win** (the loop exits
   before any twin) **and on the node where the budget runs out.** At refuted nodes, where the churn is, it
   pays the same. Predicted effect: small. Cheap to build, and its b0 digests move only by tie-break.
2. **Pass-staged: the ladder is the wave.** Warm-up passes (`pass_depth < D`) admit twins only at the root
   turn's nodes; the committing pass admits them everywhere. At an unlimited budget the committing pass always
   runs, so this is complete. It also matches the turn-boundary preference (the root turn's choice first). But
   at a finite budget, a warm-up pass that ends up committed (the start gate refuses the next pass) carries
   the GATE's dig at the deeper in-window turns. That is exactly the automatic dig the USER objected to, back
   inside the window, at the budgets that cannot reach the full pass. **USER call.** Risk: the start gate's
   growth estimate is calibrated on twin-free warm-up passes, so the committing pass costs more than
   predicted (more overrun aborts) unless the estimate accounts for the twins.
3. **Settings.** If 1 is not enough and 2 is refused, the remaining cost is real width. The depth-economics
   rule (CLAUDE.md, "COLLAPSE WASTED SEARCH") makes that a settings question for the deck (its budget / depth
   cells), not a search shape.

**Proposed order once un-parked:** build 1 behind its own lever and A/B it in ONE pooled batch with step 5's
larger held-out sample (arms: `=0`, lever, lever + order; treasure_hunt + FiveColour d3 b10, plus the ship
cells and a digest check on the other dig decks). Bring 2 to the USER only if treasure_hunt / FiveColour
still miss the bar.

### Measured 2026-10-14 (un-parked): option 1, the step-5 sample, and treasure_hunt's budget map

**Option 1 (twins last, MTG_DIG_TWIN_LAST) is inert and was reverted** (e20ea7da, reverted by 71021c80). One
pooled batch (`scripts/dig_choice_ab/make_twinlast_ab.py`, 73,400 games, seeds 9300001+, arms `dig0` / `new`
/ `tl`, every dig deck at the three cells, treasure_hunt and FiveColour d3 b10 at 1,200 games per arm):
`tl` vs `new` is net 0 on all nine decks in every cell, units x0.99-1.00, and 0 of 24 d3 chunks changed
digest on either focus deck. The d3 b0 exactness cell matched on 250 / 250 games. As predicted above, a
reorder cannot reach cost paid at nodes where every plan is scored anyway.

**The searched dig on the larger sample** (`new` vs `dig0`, same batch):

| deck | ship | d3 b10 | d0 | net | units x (ship / d3) |
|---|---|---|---|---|---|
| FiveColour | 0 | 0 (1 faster / 1 slower, n=1200) | identical | **0** | 1.021 / 1.011 |
| treasure_hunt | 0 | +0.0033 (1 / 5, n=1200) | identical | **+4** | 1.507 / 1.119 |
| Fluctuator | 0 | -0.0167 (5 / 0) | identical | -5 | 0.943 / 0.901 |
| Pirates, EDF, Soldiers, WhiteKnights, Auras, Dragons | 0 | 0 | identical | 0 | 0.97-1.06 |

All six slower games recover at stage 1 (`make_recovery.py`): FiveColour gi902 and treasure_hunt gi135 /
379 / 578 / 676 / 738. **FiveColour now meets the bar** (the earlier +1 was one game in 300). treasure_hunt
does not.

**treasure_hunt's budget map** (`scripts/dig_choice_ab/make_budget_map.py`, d3, 1,200 paired games per
budget, seeds 9600001+; b10 from the batch above):

| budget | net | faster / slower | units x | slower games still slower at d3 b0 |
|---|---|---|---|---|
| b10 | +4 | 1 / 5 | 1.119 | 0 of 5 |
| b20 | +3 | 1 / 4 | 1.077 | 0 of 4 |
| b40 | +3 | 0 / 3 | 1.066 | 0 of 3 |
| b80 | +2 | 1 / 1 | 1.087 | 1 of 1 (gi722) |

All 13 slower games recover at stage 1. Replayed at d3 `--budget-ms 0`, **12 of the 13 tie: budget churn,
and it converges** (5 / 4 / 3 / 0 churn-slower games from b10 to b80, the completeness invariant at work).
The mechanism is the start gate's, as in gi1: gi135's T1 searched depth 1 with the lever (projected T5,
played Temple of Epiphany) where the lever-off search reached depth 2 and verified T4 off Sandstone Needle.
treasure_hunt holds a sac-draw land or a cycler on most turns, so its twins are real second children.

**gi722 (seed 9610723) is not churn: it is a horizon tie-break.** At d3 b0 the lever arm wins T7 against T6
(at b80 it does not win at all, against T6). T1 has no win inside the d3 horizon, so the executor falls back
to the lookahead, which ranks no-win plans by the graded leaf (`tb`, opponent life at the horizon: lower is
better). `MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=1`, last pass: lever off grades the four distinct plans
4M / 2M / 2M / 4M, and the first 2M (Fiery Islet) commits. With the lever, the no-dig line under the FIRST
plan reaches 2M as well (2M / 2M / 2M / 4M), and the order tie-break takes it (Thundering Falls). Realised,
Thundering Falls is a turn slower. Both are equal at the horizon the search was given; depth 6 sees the T6
win (stage 1). The twin did not lose a line; it produced a tie that the no-win leaf cannot break.

**Verdict for the USER.** The searched dig passes the bar on eight of the nine dig decks (Fluctuator -5,
FiveColour and six more 0). **treasure_hunt misses it at every d3 budget (+4 / +3 / +3 / +2)**, which includes
the smoke / regression cell (d3 b10) and the overnight cell (d3 b80). Nothing is unfindable: every slower
game recovers, the churn converges with budget, and the one non-churn game is a horizon tie. The remaining
routes are the USER's: accept it (the churn is the cost of a real decision the gate used to make
automatically); option 2 above (pass-staged twins, which puts the gate's dig back inside the window at
finite budget); option 3 (treasure_hunt's depth / budget settings); or, separately, a better no-win
tie-break than plan order, which would address gi722 for every lever, not just this one.

**The suite on the integrated tree (2026-10-14, 746d54ea; NOT accepted).** Smoke: 116 / 125 pass, 9 changed --
treasure_hunt d3 +1, d5 +2, th2hg d3 +2; Fluctuator -1, fluctuator2hg -2, dragons2hg -1; FiveColour d3 / d5 and
Dragons d3 digest-only. Regression: 158 / 176 pass, 18 changed; per-deck net in game-turns treasure_hunt **+8**
(d5 s2002 alone +5), Auras -3, Dragons -1, Fluctuator -1, fluctuator2hg -1, FiveColour 0, fivecolour2hg 0. Every
slower game was classified (`classify_turn_later.sh`): smoke 4 / 4 and regression 11 / 12 recover at 4x their case
budget; the twelfth, Fluctuator d3 s3003 gi81 (6 -> 7, identical draws), persists at 16x but recovers at stage 1
(`--depth 6 --budget-ms 100` -> T6; `--depth 8 --budget-ms 0` -> T6) -- a d3-horizon line change like gi722.
Unit 1032 / 1032; reference check (non-Kitty, --strict) 171 ok / 379 repaired / 0 drift over 550 refs.
The tree was NOT accepted: the USER's bar treats churn as a cost to minimise, not an exoneration, and
treasure_hunt's is now measured at its shipped-like d5 cells too, not only d3. The primary checkout was put back
on 9347f97b; the run's results are kept in `build/scratch/dig_tier_results_746d54ea/` (an accept after a
fast-forward to the same src can promote them without a re-run).
