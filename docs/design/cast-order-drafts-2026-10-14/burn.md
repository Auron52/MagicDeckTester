# burn: cast-order draft (2026-10-14)

## 1. Status: **(D) generic tiers only**

`BurnProvider` (src/ai/DecisionProviders.h:953) overrides none of these: `CastOrderRank`, `MainPhaseOverride`,
`OrderOpaqueCastsByRank` or `ClassifiesMainPhases`. There is no `MTG_BURN_*ORDER` flag. The deck has no USER
order, on or off. The only USER input on order is the Light Up the Stage worked example (USER 2026-08-17,
cast-order-ideal-with-ranges.md:567-599: *"better main 1, but only if spectacle is enabled ... the reason being
prowess and drawing into haste"*), and that section is marked "Status: NOT implemented". The Shard Volley hold
(shard-volley-hold.md) is a USER rule. It stays as it is, and this draft does not touch it.

## 2. What plays today

| pos | card(s) | main | the rule that put it there |
|---|---|---|---|
| land | the land drop | m1 | Played first at d0. At d>0 it is searched, including a "no land now, play it after Light Up" variant. Among equal-value plans the bank tiebreak (`PreferHoldLandDrop`, at 3+ lands in play) prefers holding the land. |
| 10 | Goblin Guide, Monastery Swiftspear **(TIE)** | searched | Generic creature tier. |
| 20 | Lightning Bolt, Skullcrack, Searing Blaze, Searing Blood **(TIE)** | searched | Generic noncreature tier. Ties fall back to hand order. |
| 30 | Eidolon of the Great Revel | searched | Generic on-cast self-damage tier. It is last only within its own main phase. |
| after everything | Shard Volley | searched | **Its rank 20 is never used.** Casts that sacrifice a land are left out of the sort and cast in a trailing loop after every other cast, Eidolon included (TurnSolver.cpp:42956, PlanCastOrder :8700). One exception: if the set holds a spectacle spell that is not yet cheap, the "Spectacle hoist" casts Volley **before everything**, creatures included (:42839). |
| plan order | Light Up the Stage | searched | **The rank 20 and range [2..20] in the report are never used.** Light Up has `stages_cards`, which makes it `OrderingOpaque` (ManaPayment.cpp:2722), and burn does not opt into `OrderOpaqueCastsByRank`. So any set that holds it is cast in plan order. Its {R} spectacle line exists in main 1 only as a 2-card "Plan B" plan, {one burn spell, Light Up} (TurnSolver.cpp:49068). The general powerset prices it at the full {2}{R}. |

**Main-phase column.** For burn the report's m1/m2/both labels are display-only. Burn does not opt into
`ClassifiesMainPhases`, so `MainPhaseFilterActive` is false (TurnSolver.cpp:20381) and both mains search the whole
hand.

**Probe.** 600 games at the deck's play settings (d6/b20), seeds 9001-9600. Light Up was cast:
- in main 1 for {R} 170 times, always second, right after its Plan B trigger;
- in main 1 for {2}{R} 38 times;
- in main 2 for {R} 124 times, always first in main 2.

Scripts and logs are in build/scratch/castorder/ (`burn_probe_*`, `probe_burn/`).

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | the land drop | m1 | - | **First, with two exceptions.** (a) It goes AFTER Light Up when the turn can pay the trigger + Light Up without it. That is principle 1: the exile may contain a land, so play that one and keep the hand land. At d>0 this is already searched, so no change. (b) It always goes BEFORE Searing Blaze when a land is in hand and Blaze is cast this turn, because landfall makes Blaze 3 damage instead of 1 (Q4). |
| 10 | Monastery Swiftspear, Goblin Guide (tie) | m1 | - | Both have haste, so they attack this turn. Swiftspear must come before every noncreature spell: *"Prowess (Whenever you cast a noncreature spell, this creature gets +1/+1)"*. The tie between them provably does not matter: creature spells trigger neither prowess nor anything else here, and both are MV 1, so Eidolon pings them the same. |
| 20 | Lightning Bolt, Skullcrack, Searing Blood, Searing Blaze, **Shard Volley** (tie) | both | - | Face damage. Cast before combat it pumps prowess attackers, and the first one turns spectacle on for Light Up. The tie provably does not matter: every cost is red on an all-Mountain deck, so the mana is interchangeable. Each spell triggers prowess, Eidolon and spectacle the same way. Volley's sacrifice is paid from a tapped land (`PerformSacrificeLandCost`, tapped-first), so it costs no extra mana in any position. Blaze's landfall depends on the land drop, not on the other casts. **Change:** Volley stops trailing everything (no more Eidolon pings) and stops being hoisted ahead of Swiftspear. |
| 25 | Light Up the Stage | both | none | It comes after every face-damage spell in the set, so spectacle makes it cost {R}. It comes after the creatures, so it pumps Swiftspear. It comes before Eidolon, because it is MV 3 and would be pinged. Its re-solve then plans the rest of the turn with the exiled cards (*"you may play those cards"* this turn and next). No range is needed: the search chooses the SET, so "draw first" happens by casting a small set. Put Light Up first in a set and the continuation can spend mana that the set's later casts needed. Put it last and nothing is lost. **Main phase:** m1 when a burn spell in the same main turns spectacle on (the USER's *"better main 1, but only if spectacle is enabled"*). m2 for {R} after combat damage. Full cost only when nothing turns spectacle on. |
| 30 | Eidolon of the Great Revel | **m2** | - | *"Whenever a player casts a spell with mana value 3 or less, Eidolon ... deals 2 damage to that player"*. Every spell in this deck is MV 3 or less, and the goldfish opponent casts none, so it only ever pings us. It should be the turn's last cast. It has no haste, so waiting for main 2 costs nothing. |

## 4. What changes and why it matters

1. **Swiftspear, then a burn spell, then Light Up {R}, in one main, becomes castable.** Today the
   enumerator cannot find it: the powerset prices Light Up at {2}{R}, and Plan B has only two cards. So a
   Swiftspear held in hand falls after both spells and misses two prowess triggers.
   - **Seed 9295, game index 294, T3.** Opponent at 13. Board: 2 Mountains, Goblin Guide x2, Swiftspear. Hand:
     Mountain, Swiftspear, Shard Volley, Light Up.
   - Engine line: land, Volley, Light Up, Swiftspear. It attacks for 8 and wins T4.
   - The ideal line costs the same 3 mana: land, Swiftspear, Volley, Light Up {R}. That is 3 damage plus an attack
     of 2+2+3+3 = 13, **lethal on T3**.
   - Still T4 at d8 with unlimited budget, and still T4 with `MTG_UNPRUNED=1`.
   - A scenario run of `validate_line` returns `legal_not_enumerated` (probe_burn/scen/s1_*.json).
   - Seed 7004, game index 3, T3: the same shape, 2 damage lost (win turn unchanged).
2. **Shard Volley no longer trails Eidolon.** In 14 of 600 games Eidolon was cast and then Volley in the same main
   (e.g. seed 9118 T5, seed 9136 T4). Each time we took 2 extra damage per Eidolon in play. Seed 7052 T5:
   Swiftspear, Eidolon #2, Volley took us from 16 to 8; with Volley before Eidolon it would have been 10. With
   two Eidolons out our life reaches 6 (seed 7012). These are self-damage only; the probe never lost a game to it.
3. **Eidolon moves to the turn's last cast.** Scenario s2a: T4, 3 Mountains and Goblin Guide, hand Eidolon + Light
   Up. The engine casts Eidolon in main 1 and Light Up in main 2, and we take 2. Light Up then Eidolon, both in
   main 2, uses the same mana and takes nothing. Our life is not the metric, so the search is indifferent and
   the result follows hand order. Lowest stakes of the five.
4. **Land before Searing Blaze.**
   - 37 of 600 Blazes were cast without landfall while a Mountain was in hand. 32 of those were on the winning
     turn, where it does not matter. The other 5 lost 2 face damage while the Mountain was held back (e.g. seed
     9201 T5, seed 9262 T4).
   - Seed 7029 T4: Blaze, Light Up, **then the Mountain**, all in one main.
   - The rule that keeps lands back exists to give Blaze landfall, so holding the land and then casting Blaze for 1
     works against its own purpose.
   - The 2026-07-04 audit measured the same tiebreak (`MTG_BLAZE_CASTPRIO`): 9 of 500 games changed line and 0
     changed win turn. An agent rejected it as marginal, and that call was never put to you.

## 5. Questions for the USER

1. **Light Up the Stage at 25** (after creatures and burn, before Eidolon), either main, no range, plus the
   enumeration change that makes item 4.1 castable? Default: **yes**.
2. **Shard Volley cast with the other burn (20)** instead of after everything? The hold rule is unchanged.
   Default: **yes**.
3. **Eidolon last for the whole turn** (cast in main 2 whenever main 2 casts anything), done as a tie-break only?
   Default: **yes**.
4. **Land before Searing Blaze** whenever a land is in hand and Blaze is cast this turn, even while the deck is
   keeping lands back? This means turning the rejected `MTG_BLAZE_CASTPRIO` tie-break back on. Default: **yes**.
5. **Keep both of burn's mains fully searched** (no m1/m2 filter; the labels stay display-only)? Default: **yes**.

## 6. Generic-order findings

- **Casts that sacrifice a land run after tier 30.** The trailing loop runs after the on-cast self-damage tier,
  so any deck with both kinds of card takes the ping. Such casts should join the ordered sequence at their rank.
- **Spectacle is credited only in the 2-card Plan B.** So the generic "creatures before noncreature spells" rule
  fails in exactly the sets where spectacle fires. A param tier `spectacle_cost` -> 25 (after the
  face-damage spells), together with a powerset credit, would fix it for any spectacle deck. This is the shape
  spectacle-and-invigorate-combo-enumeration.md planned (#8, steps 1-3) before Plan B was shipped instead.
- **`ClassifyMainPhase` sends every spectacle card to Main2** (TurnSolver.cpp:20727). The USER says main 1 is
  better when a burn spell in the same main enables it, so it should be Both. Inert for burn today, but any
  deck that opts into the filter loses that main-1 line.
- **Tier 30 only works within one main.** A self-damage source cast in main 1 still pings main-2 casts.
- **The report misleads for opaque cards.** It prints a rank and range for Light Up that play never uses; it
  should say "plan order".

## 7. Implementation notes

**Param mapping** (house style):
- creatures: `IsCreature` -> 10 (generic)
- direct-damage spells (`damage` / `landfall_damage` / `death_trigger_damage`) -> 20 (generic)
- `sacrifice_land` && `direct_damage` -> 20
- `spectacle_cost` -> 25
- `on_cast_trigger_damage` -> 30 (generic)
- `landfall_damage` -> the land tie-break
- Report label for 25 via `CastOrderTierName`.

**Code changes:**
1. **`BurnProvider::CastOrderRank`**: the 25 tier.
2. **`OrderOpaqueCastsByRank() -> true`** for burn, so sets holding Light Up are sorted by rank instead of plan
   order.
3. **Put sacrifice-a-land casts into the ordered sequence**, with lockstep changes at:
   - `PlanCastOrder` (TurnSolver.cpp:8700);
   - `ApplyPlanDirect`, the clean branch (:42927), the opaque branch (:42874) and the trailing loop (:42956);
   - the `AIEngine::TakeTurn` mirror (AIEngine.cpp:5007 / 6392).

   Then remove the Spectacle hoist (:42839) for burn: rank 20 < 25 already puts Volley before Light Up, and the
   hoist is what puts it ahead of a Swiftspear in the same set. Crop Rotation pays its sacrifice before
   resolution and must keep its own path.
4. **Spectacle credit in the powerset** affordability blocks (`Solve::consider` + `EnumeratePlans::eval_and_push`)
   when the set holds a face-damage cast ranked before the spectacle card. It must ship together with change 2,
   so the apply really casts the burn first.
   - Smaller alternative: let Plan B put the hand's affordable haste creatures in front of the trigger.
   - The Volley hold's exception (b), "Volley is the only face-damage source", still holds for
     {Swiftspear, Volley, Light Up}.

**Limits:**
- The land drop is not a cast, so it has no rank (`LandDropCastOrderRank` is used only for condemnation).
  "Before Blaze" has to be a plan-level tie-break in `EnumeratePlansWithLand`'s equal-value sort: a plan that
  casts a `landfall_damage` spell from hand while holding a land skips the defer-first order.
- Eidolon-to-m2 cannot use `MainPhaseOverride`. That hook is inert without `ClassifiesMainPhases`, and the
  filter stands down whenever a prowess attacker is available. Opting in would also send Light Up to main 2
  only, which is wrong. A tie-break is the only cheap way to do it.
