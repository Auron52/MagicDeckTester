# burn: the USER's cast order, main-2 rule, and the spectacle plan credit (2026-10-15)

**Status:** built, unit-tested, measured -- see "Measurement" and "Defaults as shipped" below.
Branch `worktree-agent-ab91716804a6cfac5` (base: integration tip `16dfb7e4`). Nothing pushed.

The USER's ruling, verbatim (2026-10-14, `cast-order-review-2026-10-14.md`, "burn -- SETTLED"):

> "The burn order seems reasonable, though I wouldn't cast any spell but Light Up the Stage, Searing
> Blaze (if missing a land drop) or Eidolon in main 2 without playing them off main 2 Light up the
> Stage."

Order: **land > Goblin Guide > Monastery Swiftspear > Lightning Bolt > Shard Volley > Searing Blaze >
Searing Blood > Skullcrack > Light Up the Stage > Eidolon of the Great Revel.** The draft it settles is
`cast-order-drafts-2026-10-14/burn.md`; the Light Up the Stage worked example is
`cast-order-ideal-with-ranges.md` (USER 2026-08-17); the Shard Volley hold (`shard-volley-hold.md`, a USER
rule) is unchanged.

Every part is its own heurarm lever, so one pooled batch carries every arm:

| lever | what | default |
|---|---|---|
| `MTG_BURN_ORDER` | part 1, the order (+ the land drop's two exceptions) | see "Defaults" |
| `MTG_BURN_M2_RULE` | part 2, the main-2 rule (broad "played off" reading) | see "Defaults" |
| `MTG_BURN_M2_NARROW` | part 2's narrower "played off" reading (exiled cards only) | OFF (USER question) |
| `MTG_SPECTACLE_PLAN_CREDIT` | part 3, the engine fix | see "Defaults" |
| `MTG_BURN_BLOOD_FIRST` | PROPOSED amendment: Searing Blood before Searing Blaze when Blood cannot kill alone | see "Defaults" (USER question) |
| `MTG_KILL_TRIGGER_COMBO` | engine fix the measurement found: the wins projection credits a Searing Blood finished by the plan's own creature damage | see "Defaults" |
| `MTG_SPECTACLE_CREDIT_SEARCH_ONLY` | part 3's credit in the search's enumeration only (not the greedy walk) | OFF (measured alternative) |

## Part 1 -- the order (`MTG_BURN_ORDER`)

`BurnProvider::CastOrderRank` reads PARAMS; the NAME is only the final tie-break:

| rank | tier | cards |
|---|---|---|
| 10 + mv | haste creature (prowess sees every later spell) | Goblin Guide, Monastery Swiftspear |
| 20 + 2*mv | burn (DirectDamage) by mana value | Lightning Bolt, Shard Volley (22); Searing Blaze, Searing Blood, Skullcrack (24) |
| 30 | spectacle (Light Up the Stage) | after every burn spell and creature, before Eidolon |
| 40 | on-cast self-damage (Eidolon) | the main's last cast |
| 100 + generic | anything not named (a future card) | after every named card |

* **Total order.** BurnProvider opts into the shared name tie-break (`DecisionProvider::CastOrderTotalByName`
  / `CastOrderNameTieLess`, cherry-picked from the integration tree, 2ac8db0b): an equal rank breaks by card
  name in the shared comparator, so no two different cards ever fall to plan / hand order (USER cross-deck
  ruling: "Let's fully order the cards in each list inasmuch as possible. I don't like the randomness of
  choosing the order."). Both apply worlds and the enumeration's sequential payability walk read it. (A
  first version broke the tie in `BurnProvider::ArrangeCastOrder`; switching to the shared hook is the
  integration's one-copy request, verified line-identical over 600 paired d0 games with every lever on.) The unit test permutes the hand
  (200 seeded shuffles of all nine names + all 120 permutations of a five-card subset holding every tie)
  and checks the realised order never moves.
* **Order-opaque sets sort by rank** (`OrderOpaqueCastsByRank`): Light Up stages cards, which made every
  set holding it "plan order" before.
* **Shard Volley joins the burn** -- a new hook, `DecisionProvider::SacLandCastsInOrder`: a
  sacrifice-a-land cast is sequenced at its own rank in BOTH apply worlds (`ApplyPlanDirect`'s
  `apply_plan_actions` opaque + clean routes; `AIEngine::TakeTurn`'s main opaque + clean loops and its
  breakpoint-continuation loop; the `PlanCastOrder` projection) instead of trailing every cast, and the
  Spectacle hoist (which cast it ahead of everything, Swiftspear included) stands down. Its land is
  paid from a tapped land first (`PerformSacrificeLandCost`), so the position costs no mana. The
  explicit (searched / human-queued) route is untouched. The Volley hold rule is unchanged.
* **The land drop's two exceptions.**
  * *Before Searing Blaze whenever Blaze is cast and a land is in hand* and *after Light Up the Stage when
    the turn pays for it without the land* -- a new hook, `DecisionProvider::LandPlanTieKey`, gives every
    plan of `EnumeratePlansWithLand` a key among EQUAL plans (same win / same value): Light-Up holder 0,
    land-up-front 1, Blaze-without-the-land 2 (the bank, `PreferHoldLandDrop`, keeps its ordinary holder
    at 0 -- the bank exists FOR Blaze). A tie-break only: a strictly better plan still wins.
  * *Depth 0:* the d0 runner plays its drop before it plans, so `BurnProvider::LandDropAfterDrawEngine`
    holds it when the board alone pays every haste creature + burn spell in hand plus Light Up (no
    Searing Blaze in hand), and the new `DecisionProvider::HeldDropOpensDrawContinuation` plays that held
    drop at the START of Light Up's continuation (the staged copy first, keeping the hand land) -- the
    d0 continuation is a greedy re-solve that plays no land, and leaving the drop to the second pass
    spent the turn's mana without it (d0 probe seed 11699747 gi 246: Volley, Light Up, then a Searing
    Blood that could not kill, where land + the exiled Blaze's landfall was the kill; fixed).

### The amendment the measurement asked for: Searing Blood before Searing Blaze when Blood cannot kill alone (`MTG_BURN_BLOOD_FIRST`)

The USER's list puts **Searing Blaze before Searing Blood** (same cost; by name). The probes found games
that list loses, and games the plain reversal loses, and the difference is the opponent's creature:

* **It survives Blood alone (a 3/3): Blood must come FIRST.** Seed 9027 gi 26 (shipped), T4: opponent at
  9 with ONE 3/3; land, Searing Blaze (3 to the face, 3 to the 3/3 -- it dies), and Searing Blood has no
  opposing creature left, so it stays in hand (win T5). The generic order cast Blood first: 2 to the 3/3,
  then Blaze kills it, and Blood's "When that creature dies this turn, Searing Blood deals 3 damage to
  the creature's controller" fires -- 6 to the face, the T4 kill. burn2hg seed 11670487 gi 486 is the
  same board under the main-2 rule: the old line cast Blood in main 1 and the finishing Blaze in MAIN 2;
  the rule (Blaze in main 2 only while no land has entered) moves that Blaze to main 1, where the list
  then casts it BEFORE Blood (T5 -> T6, not recovered at d8 b0 -- structural).
* **Blood alone kills it (a 1/1): Blaze must come FIRST, the USER's list.** burn2hg seed 11671355 gi
  1354, T6, opponent at 9 with a 1/1: land, Blaze (3 to the face, kills the 1/1), then Blood on our OWN
  Swiftspear for its prowess -- 6 more in combat, the kill. Blood first takes the 1/1 and strands Blaze
  ("target creature that player controls"): T6 -> T7 under an unconditional reversal.

So the amendment is CONDITIONAL (`BurnProvider::CastOrderActionRank`): Searing Blood moves one slot
earlier (before Searing Blaze) exactly when FindBurnKillTarget's pick -- the target both apply worlds give
it -- survives Blood alone; otherwise, and with no opposing creature, the USER's list stands. The order
then reads land > Goblin Guide > Swiftspear > Bolt > Shard Volley > {**Searing Blood > Searing Blaze** if
Blood cannot kill alone, else Searing Blaze > Searing Blood} > Skullcrack > Light Up > Eidolon. Default
OFF -- a USER decision. (The running A/B's arms `o1b` / `o123b` measured the first, UNCONDITIONAL form;
the conditional form is measured in the follow-up batch as `o1c` / `o123c`.)

### Not built: Eidolon last across BOTH mains (an option for the USER)

"Eidolon is the turn's last cast" holds within each main (rank 40). Across mains, an Eidolon cast in main
1 still pings a main-2 Light Up (draft scenario s2a: Eidolon main 1, Light Up main 2 for {R} -> 2 to us).
Restricting Eidolon to main 2 would make it the turn's last cast exactly and looks sound in a goldfish (no
haste, no prowess, the opponent casts nothing, main 2's mana is main 1's), but the USER did not ask for a
main-1 restriction, so it is reported as an option, not built.

## Part 2 -- the main-2 rule (`MTG_BURN_M2_RULE`, `MTG_BURN_M2_NARROW`)

A provider RESTRICTION of the post-combat options (providers may restrict or order options); main 1 is
untouched and fully searched. New hooks `DecisionProvider::RestrictsMain2Casts` / `Main2CastAllowed`,
read in `CollectActions(is_pre_combat = false)` -- the one collector every main-2 consumer goes through
(the search's enumerations and continuations, the rollout's playout, the d0 runner, the executor), so
executor and rollout agree by construction. Human play is never narrowed. Allowed in main 2:

* Light Up the Stage (`stages_cards`), Eidolon (`on_cast_trigger_damage`);
* Searing Blaze (`landfall_damage`) while no land has entered this turn (`lands_played_this_turn == 0`),
  so a land a main-2 Light Up exiles can still turn landfall on;
* everything "played off a main-2 Light Up": the BROAD reading (default) admits every spell once a
  main-2 Light Up was cast this turn (the cards it exiled AND hand spells cast after it in that main);
  the NARROW reading (`MTG_BURN_M2_NARROW`) admits only the cards it exiled.

The marker is `GameState::m2_stage_turn` + `m2_prestaged_mask[2]` (which deck cards were already staged
when the first main-2 stager was cast), stamped at the CAST in both worlds (`NoteMain2StagerCast`:
TurnSolver's `apply_one` from `ApplyPlanDirect`'s phase, `AIEngine::CastSpellFromHand` from a phase member
scoped by `TakeTurn`), written only when the provider restricts main 2, and folded into `BuildSimKey`
(and the dominance key) only while live -- every other deck keeps its exact keys.

## Part 3 -- the spectacle plan credit (`MTG_SPECTACLE_PLAN_CREDIT`, EngineFlags.h)

**Why the line was missing.** Light Up the Stage is {2}{R}, spectacle {R} "if an opponent lost life this
turn". Both subset evaluators priced each candidate at the cost of the state BEFORE the plan, so in a
main where the opponent had not lost life yet Light Up read as {2}{R} in every plan -- including the
plans whose own burn spell, cast first, turns spectacle on. Worse, the odometer's scalar mana bound
(`ManaPruneBound`) cut the subset before either evaluator saw it: {Swiftspear, Shard Volley, Light Up} is
1 + 1 + 3 = 5 on a 3-land board. The only spectacle line was Plan B's two-card {burn, Light Up}.

**The credit** (`BuildSpectacleCreditCtx`, shared by `Solve::consider` and
`EnumeratePlans::eval_and_push`): a spectacle cast is priced at its spectacle cost (`SpectacleLiveCost`:
the alternative cost plus cost increases, CR 118.9d) when the plan also casts a FACE-DAMAGE spell
(`Action::direct_damage > 0`, CollectActions' own measure of damage that reaches the opponent) that the
REALISED order puts before it. That order is knowable only when the deck rank-sorts its opaque sets
(`OpaqueCastOrderActive`), and then it is `CastOrderLess` -- the comparator both apply worlds sort by; a
sac-land burn counts only when it is sequenced at its rank (`SacLandCastsInOrder`), never on the strength
of the Spectacle hoist (the executor's continuation loop has none). The scalar bound is loosened by the
most the credit can forgive (sound -- it prunes less, never more), and the selection-exact gate, whose
terms bake the printed cost, stands down while the credit is live. Combat damage in a post-combat frame
needs nothing new: `opponent_lost_life_this_turn` is already true there and the candidate already
carries the {R} cost.

A faithful rules credit, not a heuristic: the apply casts the burn first and pays {R}. Executor and
rollout read the same plan, the same comparator and the same live cost. Only a deck holding a spectacle
card can move (Light Up the Stage is in burn alone).

**The repro** -- seed 9295 gi 294, T3, opponent at 13, board 2 Mountains + Goblin Guide x2 + Swiftspear,
hand Mountain + Swiftspear + Shard Volley + Light Up: land, Swiftspear, Volley (3), Light Up {R}, attack
2 + 2 + 3 + 3 = 10 -- lethal. Base: T4 at every budget (`validate_line`: legal_not_enumerated). With parts
1 + 3: **T3 at the shipped settings (d6 b20) and at d8 b0**; scenario
`test/scenarios/burn_spectacle_swiftspear_volley_lightup.json` pins it (fails on the base binary).

### Found by the measurement: the kill-trigger combo credit (`MTG_KILL_TRIGGER_COMBO`, EngineFlags.h)

The main-2 rule exposed a projection gap. Searing Blood is credited as face damage by `CollectActions`
only when it kills its creature ALONE (`direct_damage = 3` then; `0` against a 3/3 or a 4/4). A plan that
casts Blood on a 4/4 and finishes it with a second Blood or a Searing Blaze really does deal Blood's 3 to
the face (the death trigger), but the "wins this turn" projection did not see it, so the plan read as
short of lethal -- and the Shard Volley hold's exception (a) ("the plan wins this turn") then pruned the
Volley that was the kill. Without the main-2 rule the same Volley was cast in MAIN 2, after the triggers
had resolved and the board showed the opponent at Volley range; the rule moves it to main 1, where only
the projection can see it. burn2hg seeds 11671958 gi 1957 and 11671187 gi 1186: one turn slower under
parts 1 + 2, NOT recovered at d8 b0 (a structural miss, not a budget one).

`KillTriggerComboCredit` (TurnSolver.cpp, read by both subset evaluators): with exactly ONE opposing
creature (every creature-damage spell then hits it), walk the plan's creature-damage casts in the realised
order (`CastOrderLess`); each kill-trigger cast made while the creature is still alive and not already
credited adds its `death_trigger_damage` once the running creature damage reaches the toughness. A
projection-accuracy fix -- the apply already resolves the trigger -- and 0 (byte-identical) for every
plan without such a cast, i.e. every other deck. Measured as the `...k` arms.

### A measured alternative: the credit in the search only (`MTG_SPECTACLE_CREDIT_SEARCH_ONLY`)

At depth 0 the credit newly admits {Shard Volley, Light Up {R}} in main 1 of the GREEDY walk, and the
Volley hold's exception (b) ("it turns Spectacle on for a spell in the same plan", `shard-volley-hold.md`)
lets the greedy sacrifice a land on T2-T3 for it, where it used to keep Volley and cast Light Up for {R}
in main 2 off combat damage. The search prices that land; the greedy does not. This lever keeps the credit
in `EnumeratePlans` only (the asymmetry Plan B always had) and leaves `Solve` (the d0 runner and every
rollout leaf) at the pre-plan cost; BurnProvider's d0 land hold reads it too, so the hold never banks a
drop for a line Solve cannot find. Measured as the `...s` arms; default OFF.

## Rules review (`.claude/skills/mtg-rules.md`)

* Spectacle is an alternative cost (CR 702.137a), chosen while casting (601.2b) and legal iff an opponent
  lost life this turn; cost increases still apply (118.9d) -- `SpectacleLiveCost`.
* Damage dealt to a player is life loss (120.3a); each burn spell resolves before the next is cast
  (sorcery-speed Light Up needs an empty stack), so its damage precedes Light Up's cost determination.
  Searing Blood's face damage is a delayed trigger on its creature's death -- it resolves before Light Up
  can be cast too. It fires whichever spell's damage kills the creature this turn ("When that creature
  dies this turn"), so a Blood followed by a Blaze or a second Blood that finishes the creature deals its
  3 -- the kill-trigger combo credit. A second Blood on a creature already dead has no target (it is not
  cast), which is why the walk stops at the death. Eidolon's ping hits US, not an opponent: never a trigger.
* Shard Volley's land sacrifice is an additional cost (601.2h), payable with a land already tapped for
  mana -- the slot costs no mana.
* Light Up's exiled land is a land PLAY (305.2) in either main, so the main-2 rule never touches lands.
* Searing Blaze's landfall is checked on resolution ("a land entered ... this turn").
* Prowess triggers on CAST (702.108a), so Swiftspear must be on the battlefield first; Eidolon reads the
  spell's mana value (202.3), not the cost paid -- Light Up is MV 3 at {R} too.

## Tests

* `test/unit/test_burn_cast_order.cpp` -- the ranks; the realised order under 200 seeded shuffles and all
  120 permutations of a five-card subset (hand order never moves it); the OFF control (ties fall to plan
  order and Volley trails); the Blood-first amendment; the land keys; the d0 hold (and its OFF arms); the
  main-2 rule per card, broad and narrow, the stamp; the rule through `EnumerateMainPlans` (main 2 offers
  no Bolt, main 1 keeps everything); the 9295 plan with the credit ON and absent OFF.
* `test/scenarios/burn_spectacle_swiftspear_volley_lightup.json` (discriminating: fails on base),
  `burn_order_eidolon_after_lightup.json`, `burn_order_volley_before_eidolon.json` (guards).
* Full unit suite, full scenario suite (163/163): pass.

## Measurement

MEASUREMENT_PLACEHOLDER

## Defaults as shipped

DEFAULTS_PLACEHOLDER

## Questions for the USER

QUESTIONS_PLACEHOLDER
