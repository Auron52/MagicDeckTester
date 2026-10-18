# Vampires — Stage 2 design, cluster B: lords, tokens, transform, and the vanilla bits

Research only (read-only on the repo). Worktree: `.claude/worktrees/vampires` (branch `vampires-analysis`,
`2176032dd`). Sources: Scryfall JSON in `build/scratch/vampires/scry/` (oracle text, cost, type line, P/T,
keywords) plus Scryfall rulings fetched into `build/scratch/vampires/rulings_B/`. `file:line` citations are
against the worktree at `2176032dd`.

Cards: Legion Lieutenant, Stromkirk Captain, Captivating Vampire, Bloodline Keeper // Lord of Lineage,
Vampire of the Dire Moon, Swamp.

---------------------------------------------------------------------------------------------------------

## 0. Facts these designs depend on (checked, not assumed)

1. **Every creature in the 60 is a Vampire.** I read the type lines from the scry JSON:
   - Falkenrath Pit Fighter (Vampire Warrior), Falkenrath Gorger (Vampire Berserker), Legion Lieutenant
     (Vampire Knight), Stromkirk Captain (Vampire Soldier), Stromkirk Condemned (Vampire Horror), Captivating
     Vampire (Vampire), Bloodghast (Vampire Spirit), Markov Baron (Vampire Noble), Asylum Visitor (Vampire
     Wizard), Vampire of the Dire Moon (Vampire), Vampire Socialite (Vampire Noble), Mavren Fein (Vampire
     Cleric), Drana (Vampire Ally), and Bloodline Keeper // Lord of Lineage (Vampire on both faces). That is
     all 14 creature cards.
   - Both token kinds the list makes are Vampires: the Keeper/Lord 2/2 and Mavren Fein's 1/1.
   - The non-creatures are Aether Vial (an artifact), five land kinds, and Blood tokens (artifacts, not
     Vampires).
   - The sideboard is unreachable.
2. **The goldfish opponent DOES control creatures**, so Captivating Vampire's steal is live and NOT
   inert. `GoldFishRunner::PopulateOpponentSpawns` (`src/runner/GoldFishRunner.cpp:760-785`) cycles
   `game_index % 10`:
   - slots 0-1 have no spawns;
   - slot 2 has six 1/1s (T1-T3); slot 3 has 2/2s (T1-T3);
   - slots 4, 5 and 6 have a single 1/1, 2/2 or 3/3 on T1;
   - slot 7 has a 4/4 on T3; slot 8 has a 6/6 and a 1/1 on T3; slot 9 has a 1/1, a 1/1 and a 2/2.

   They are materialised at turn start in `GameEngine.cpp:345-370`, and in the rollout's twin. They are
   **not tokens** (Brutal Cathar note). They have no definition, are colourless, have no subtypes, and
   are created with `entered_this_turn=false`. The gain-control precedent is Nicol Bolas's -2,
   `steal_creature` (`SpellEffects.h:10526-10556`). It sets `controller_index` and
   `gained_control_this_turn=true`, which is CR 302.6 summoning sickness by control, and that flag clears
   at our untap (`GameEngine.cpp:333`).
3. **Combat is ONE damage event and first strike has no reader.**
   - `ResolveCombatDamage` (`src/ai/Combat.cpp:112-275`) runs one pass. Double strike is modelled as
     `power * 2` (`Combat.cpp:170`).
   - `Keyword::FirstStrike` is parsed but never read. `DecisionProviders.cpp:5220` says *"first strike has
     no engine reader"*, and the Lyra, Rick and Moonrage Brute notes say the same.
   - Combat-damage triggers fire after the single loop.
4. **Lords.** `ComputeLordBonus` (`SpellEffects.h:4254-4627`) matches each creature's *own*
   `Card::m_subtypes`. That makes it token-safe and makes it see a subtype added at runtime.
   - `lord_excludes_self` is by address (`:4369`).
   - Lord sources are found through `LookupCached(lord.card)` plus `IsLordPermanent` (`:4235`), which is
     `lord_effect`, or a creature with a non-zero bonus and a match scope.
   - `GatherBoardSources` (`:4851`) prefilters the lists.
5. **Transforming DFC precedent** is Brutal Cathar // Moonrage Brute:
   - two `cards.json` entries;
   - the back face has `mana_cost: ""` and an explicit `"colors"` (its colour indicator);
   - the link params are `daybound_back_name` / `nightbound_front_name` (`CardDatabase.h:3854-3863`);
   - the face swap is `SwapDayboundFace` (`SpellEffects.h:17563-17569`): it replaces `p.card`, keeps
     `m_number`, and the Permanent keeps tapped / `entered_this_turn` / counters;
   - leave-normalisation is `NormalizeLeftDayboundFace` (`:17663`), called from
     `FireLeavesBattlefieldTriggers` (`:9630-9645`);
   - the viewer art list is `CardDatabase::MdfcBackFaceNames` (`CardDatabase.cpp:420-431`);
   - keyword divergences live in `src/cards/data/scryfall_divergences.json`, because Scryfall reports
     CARD-level keywords such as "Transform".
   - **Do not reuse the daybound params.** `ApplyDayboundOnEnter` would set day/night and could make a
     Keeper enter as its back face.
6. **The tap-for-token family exists and already handles a CREATURE source with summoning sickness.**
   - Params: `tap_token_*` (`CardDatabase.h:472-499`).
   - Legality: `TapTokenSourceLive` (`SpellEffects.h:5086`), meaning untapped plus `CanTapNow`
     (CR 302.6).
   - Enumeration: `Action::Kind::TapForTokenPay` (`TurnSolver.cpp:32296-32330`). It is a plan-subset
     choice, with no greedy sink since 2026-09-30.
   - Apply: `TurnSolver.cpp:48805-48838` and the executor twin.
   - Viewer verb: `taptoken=<source>`.
   - Precedent card: Speaker of the Heavens (`{0}` cost, `tap_token_color`, `tap_token_keywords`) and
     `test/unit/test_speaker_of_the_heavens.cpp`.
7. **Secluded Courtyard / Unclaimed Territory** model "chosen creature type" as "any creature", which is
   exact when every creature is of one type (point 1).
   - Courtyard's coloured mana also pays a creature-source ability through `colored_creature_ability_ok`
     and `CreatureAbilityPayScope`. That scope is opened by both `ActivatePermAbility` apply sites
     (`TurnSolver.cpp:48448`, `AIEngine.cpp:6042`).
   - Unclaimed Territory's coloured mana pays creature spells only.
8. **This deck will already have a second main.** `GoldFishRunner::DeckUsesSecondMain`
   (`GoldFishRunner.cpp:86`) returns true for `params.convoke`, and Markov Baron has convoke (cluster A1).
   So nothing from this cluster needs a new detection term (see section 8). The main-1 / main-2 split
   itself belongs to the USER.

---------------------------------------------------------------------------------------------------------

## 1. Legion Lieutenant — Tier 1

Verbatim Scryfall fields:
- name `Legion Lieutenant`
- mana_cost `{W}{B}`, cmc 2
- type_line `Creature — Vampire Knight`, P/T 2/2, keywords `[]`
- oracle `Other Vampires you control get +1/+1.`

| Clause | Model | Tier |
|---|---|---|
| 2/2 Vampire Knight for {W}{B} | card fields | 1 |
| "Other Vampires you control get +1/+1" | `lord_effect`, `subtypes_affected:["Vampire"]`, `power_bonus 1`, `tough_bonus 1`, `lord_excludes_self` (Inspiring Veteran shape) | 1 |

**"Other Vampires" vs "Other Vampire creatures" (Captain, Captivating, Lord of Lineage).**
- Lieutenant's wording covers any Vampire *permanent*. The other three say creatures.
- `ComputeLordBonus` is only ever asked about creatures, so the engine treats both wordings as creatures.
- The difference is **inert for this list**: no noncreature Vampire permanent exists in it. Blood tokens
  are artifacts without the Vampire subtype, and no tribal artifact or enchantment is in the list.
- State this in the bracket note.

Lord bonuses reach these automatically, because they match on `m_subtypes`:
- Keeper's and Lord of Lineage's 2/2 tokens and Mavren Fein's 1/1;
- a creature stolen with Captivating Vampire, once it has become a Vampire (section 3).

```json
{"name": "Legion Lieutenant", "mana_cost": "{W}{B}", "types": ["Creature"], "subtypes": ["Vampire", "Knight"],
 "keywords": [], "power": 2, "toughness": 2,
 "oracle_text": "Other Vampires you control get +1/+1. [Lord: lord_effect + subtypes_affected [\"Vampire\"] + lord_excludes_self (Inspiring Veteran's shape), applied inside ComputeLordBonus -- the one P/T site combat, SBA and eval read, so executor and rollout are lockstep. Matches each creature's OWN subtypes, so it reaches Vampire TOKENS (Bloodline Keeper / Lord of Lineage 2/2s, Mavren Fein 1/1s) and a creature taken with Captivating Vampire (which becomes a Vampire). 'Other Vampires' (any Vampire permanent) is evaluated for creatures only -- EXACT for this list: it has no noncreature Vampire permanent (Blood tokens are untyped artifacts).]",
 "template": "lord_effect",
 "parameters": {"subtypes_affected": ["Vampire"], "power_bonus": 1, "tough_bonus": 1, "lord_excludes_self": true}}
```

- **C++:** none.
- **Search:** none. The lord is static.
- **Viewer:** no decision.
- **Second main:** not relevant. The main-phase classifier already treats a `LordEffect` as Main1
  (`GoldFishRunner.cpp:380-393`). How the casts are split across mains is the USER's call.
- **Deferrals:** none.
- **Tests:**
  - 4 Lieutenants give each other +3/+3 and themselves nothing;
  - a Vampire token gets +1/+1;
  - a stolen spawn gets nothing until it is a Vampire, and +1/+1 after.

---------------------------------------------------------------------------------------------------------

## 2. Stromkirk Captain — Tier 2 (new keyword-lord param + a first-strike oracle; the step model is cluster C's)

Verbatim Scryfall fields:
- mana_cost `{1}{B}{R}`, cmc 3
- type_line `Creature — Vampire Soldier`, P/T 2/2, keywords `["First strike"]`
- oracle `First strike\nOther Vampire creatures you control get +1/+1 and have first strike.`
- Ruling 2011-01-22: a creature that dealt first-strike damage and then loses first strike does not deal
  damage again.

| Clause | Model | Tier |
|---|---|---|
| First strike (printed) | `keywords:["First strike"]` (Keyword::FirstStrike) | 1 (parse) |
| Other Vampire creatures get +1/+1 | `lord_effect` + Vampire + `lord_excludes_self` | 1 |
| ...and have first strike | **NEW `grants_first_strike`** (sibling of `grants_haste` / `grants_double_strike` / `grants_lifelink`), read by a **NEW shared oracle `CreatureHasFirstStrike`** | 2 |

### Why first strike is NOT inert in this list

On its own, first strike only matters through blockers, and a goldfish never blocks. **But Drana,
Liberator of Malakir (cluster C)** reads the damage steps:
- Drana has first strike. Her trigger is *"Whenever Drana deals combat damage to a player, put a +1/+1
  counter on each attacking creature you control."*
- Per CR 510.4 the trigger resolves after the first-strike damage step.
- Attackers that had neither first strike nor double strike when that step began then deal regular
  damage *with* the counter.
- Attackers that had first strike do **not** deal damage again (CR 702.7c, and the ruling above).

**The anti-synergy:**
- With a Stromkirk Captain on the battlefield, every Vampire attacker has first strike. Every creature in
  this list is a Vampire, including the stolen creature and both token kinds.
- So all of them deal damage in step 1, and Drana's counters arrive after the last damage this combat has
  left to deal. Her same-turn pump is worth 0.
- Without a Captain it is worth +1 per non-first-strike attacker.
- So "cast Captain pre-combat or post-combat on a Drana turn" is a real trade:
  - pre-combat gives +1/+1 to the others but cancels Drana's pump;
  - post-combat means Captain contributes nothing this combat.

  That trade is reachable only if Captain may be cast in main 2. **The main split is the USER's
  decision.** Surface it.

### What cluster B provides (and what it does not)

Cluster B provides the keyword-lord param and the shared oracle. The two-step damage split goes in
`ResolveCombatDamage`, and that belongs to cluster C.

1. `CardParams::grants_first_strike` (bool). Read it in `BuildParamsFromJson` (the existing grant flags
   are read near `CardDatabase.cpp` `grants_double_strike`).
2. `BoardSources::fs` list. Fill it in `GatherBoardSources` (`SpellEffects.h:4851`) when
   `pp.grants_first_strike || pp.etb_choose_keyword_count > 0`. The second term is Rick's chosen first
   strike, which today is recorded in `chosen_keyword_mask` and has no reader. Add it to `OnErase`.
3. `inline bool CreatureHasFirstStrike(const Permanent& c, const GameState& s, const std::vector<int>* fs_idx = nullptr)`.
   Put it beside `CreatureHasDoubleStrike` (`SpellEffects.h:4742`), as the one oracle every site reads.
   It returns true when any of these hold:
   - the printed `Keyword::FirstStrike`;
   - a `grants_first_strike` source with the same controller whose `subtypes_affected` matches the
     creature, or whose source animates all types. Skip the source itself when `lord_excludes_self`,
     comparing per-copy `m_number` (the Lyra / `CreatureHasLifelink` precedent). This is inert for the
     Captain, whose own keyword is printed, but it is faithful.
   - Rick's `kChosenKwFirstStrike` crossed with `keyword_grant_subtypes`.

   Double strike stays in `CreatureHasDoubleStrike`. The step model needs both oracles.
4. **Contract for cluster C:**
   - Evaluate both oracles **once, at the start of the first-strike damage step** (CR 510.4). Snapshot the
     two sets, because CR 702.7c says gaining or losing first strike between the steps does not move a
     creature between them.
   - Re-read `EffectivePower` and `ComputeLordBonus` per step, because Drana's counters land between the
     steps.
   - Apply lifelink gains per step.
   - Set `opponent_lost_life_this_turn` in step 1.
5. **Scope risk for the orchestrator / USER (not cluster B's call).** A faithful two-step split also
   changes decks that are already shipped:
   - **Angels.** Lyra Dawnbringer has first strike and lifelink, and the list runs **Archangel of Thune**
     (each life gain puts a +1/+1 counter on each creature) and **Righteous Valkyrie** (a life-threshold
     anthem). Lyra's first-step gain would grow the regular-step attackers *this* combat.
   - Lyra's note says strike order "can never change an outcome", and that is wrong for that list.
   - Soldiers (Rick's chosen first strike plus lifelink) may also move.
   - **CLAUDE.md:** "Scope a change by where it is VALID, never by what keeps the GT still". That argues
     for applying the split universally. A Drana-only gate would keep GT still, but it is the shape that
     rule forbids.
   - `DecisionProviders.cpp:5220` would have to stop calling first strike dead wherever the new step
     reader is live.

```json
{"name": "Stromkirk Captain", "mana_cost": "{1}{B}{R}", "types": ["Creature"], "subtypes": ["Vampire", "Soldier"],
 "keywords": ["First strike"], "power": 2, "toughness": 2,
 "oracle_text": "First strike\nOther Vampire creatures you control get +1/+1 and have first strike. [Lord half: lord_effect + subtypes_affected [\"Vampire\"] + lord_excludes_self, inside ComputeLordBonus (tokens and a stolen-and-Vampired creature included). FIRST STRIKE GRANT: grants_first_strike, a static subtype keyword lord in the grants_haste / grants_double_strike / grants_lifelink family, answered by the single shared oracle CreatureHasFirstStrike (self-exclusion by per-copy m_number; inert for the Captain, whose own first strike is printed). NOT goldfish-inert in this list: Drana, Liberator of Malakir's combat-damage trigger lands between the first-strike and regular damage steps (CR 510.4), so a Vampire WITHOUT first strike deals regular damage with Drana's counter while one WITH first strike (every Vampire, while a Captain is out) has already dealt its damage and does not deal it again (CR 702.7c) -- the Captain cancels Drana's same-combat pump. The two-step damage model itself is Drana's (cluster C).]",
 "template": "lord_effect",
 "parameters": {"subtypes_affected": ["Vampire"], "power_bonus": 1, "tough_bonus": 1, "lord_excludes_self": true,
                "grants_first_strike": true}}
```

- **Search:** no decision of its own. Its effect on Drana's pump reaches the search through the rollout
  once cluster C's step split reads `CreatureHasFirstStrike`. Executor and rollout share
  `ResolveCombatDamage`, so they stay lockstep.
- **Viewer:** no decision. Optional cosmetic: list the granted first strike in the state JSON's keyword
  badges, the way `main.cpp:3320` does for Rick.
- **Viewer audit manifest:** add `grants_first_strike: "static grant"` beside `grants_double_strike`
  (`scripts/audit_viewer_decisions.py:858`).
- **Second main:** see the Drana trade above. It needs Captain to be castable in main 2. It needs no new
  detection, because convoke already turns main 2 on.
- **Deferrals:** none. First strike must not be bracket-noted inert in this list.
- **Tests:**
  - `CreatureHasFirstStrike` is true for another Vampire, for a Vampire token, and for a stolen creature
    once it is a Vampire;
  - it is false for a non-Vampire spawn;
  - the Captain itself is covered by its printed keyword;
  - the self-exclusion holds;
  - the P/T bonus stacks with Lieutenant;
  - (cluster C's test) Captain plus Drana means no attacker gains damage from Drana's counter this
    combat.

---------------------------------------------------------------------------------------------------------

## 3. Captivating Vampire — lord Tier 1; steal Tier 3

Verbatim Scryfall fields:
- mana_cost `{1}{B}{B}`, cmc 3
- type_line `Creature — Vampire`, P/T 2/2, keywords `[]`
- oracle `Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target creature. It becomes a Vampire in addition to its other types.`

Rulings (2010-08-15):
1. The cost has no {T}, so you may tap Vampires, **including Captivating Vampire itself**, that have not
   been under your control since your most recent turn began. Summoning-sick bodies are legal.
2. The effect has no duration. Control and the Vampire type last until the game ends or the creature
   leaves, even if Captivating Vampire leaves.
3. You do not gain control of Auras or Equipment attached to it. That is inert here, because spawns carry
   none.

| Clause | Model | Tier |
|---|---|---|
| Other Vampire creatures +1/+1 | `lord_effect` + Vampire + `lord_excludes_self` | 1 |
| Cost: tap five untapped Vampires you control | NEW `PermAbilityMode::TapSubtypeSteal`. No {T} on the source, so it goes in `PermAbilityTaps`' exclusion list. The cost taps 5 searched bodies, and sickness is ignored (CR 302.6 restricts only {T}/{Q} abilities). | 3 |
| Gain control of target creature | the Bolas `steal_creature` body: `controller_index = activator`, `gained_control_this_turn = true` (it cannot attack until our next turn), `tapped` left as is. The target is SEARCHED. | 3 |
| It becomes a Vampire in addition to its other types | `got.card.m_subtypes.push_back("Vampire")` (layer 4, permanent). Lords then apply (layer 7c), and it counts toward every Vampire count. | 3 |

### Is the steal ever worth it, and when

- **Main 1:** the bodies are summoning-sick Vampires (free) plus attack-eligible Vampires (each costs its
  attack).
- **Main 2:** the bodies are the non-attackers plus **Mavren Fein's attack token**, which is created
  untapped during combat.
- The stolen creature can never attack this turn. So for damage, a main-1 steal that taps an
  attack-eligible body is equal to "do not attack with it, steal post-combat".
- A pre-combat steal still differs in one way: the stolen Vampire raises the Vampire count *before*
  combat. That can enable Bloodline Keeper's transform, worth +2/+2 to the attackers. It can also be Pit
  Fighter fodder (A2).
- So both mains are real and neither dominates. **Offer the steal in both mains.** No main-1 prune.
- The opponent's end step is equivalent to our main 2: non-attackers are untapped in both, and a creature
  stolen then can attack on our next turn either way. So the engine's lack of an opponent-turn priority
  window costs nothing here. Disclose it as provably equivalent.

### Params

- `steal_tap_count: 5`
- `steal_tap_subtype: "Vampire"`
- `steal_adds_subtype: "Vampire"`

Name them generically, so a future "tap N untapped X: gain control" card reuses them.

### C++

**1. Mode and helpers**
- Append `PermAbilityMode::TapSubtypeSteal` **last** in `src/core/Permanent.h:157-281`, so existing
  ActKey and signature values hold. `Action::AbilityMode` is an alias (`TurnSolver.h:271`).
- Add it to `PermAbilityTaps`' exclusion list (`Permanent.h:286-302`).
- Add a `PermAbilityLabel` row (`SpellEffects.h:21630-21661`): "tap five untapped Vampires: gain control
  of target creature".
- Add it to the mode-name list at `TurnSolver.cpp:82802`.

**2. Action payload**
- The victim goes in `Action::enchant_target`, which is the victim's `m_number`. That is the Bolas
  `steal_target` precedent (`TurnSolver.cpp:30280-30346`).
- Add a small field for the tap set:
  - Option (a): `std::shared_ptr<const std::vector<int>> tap_cost_ids`, the m_numbers of the five
    bodies, on the `cont_pins` precedent. It adds no size to actions that do not use it, and Action's
    size is pinned (`kPlanDomAssertedSize`, `TurnSolver.cpp:61464`).
  - Option (b): a class-count encoding. See the search design.
- Fold both fields into the plan signature, `SamePlan`, the dedup / census / sim keys and
  `PlanIsAxisVariant`.
- Per the core-invariant precedent, a signature keyed on names alone would collapse distinct targets or
  tap sets to the first one enumerated.

**3. Emission**
- Emit as an **out-of-band block** beside `ReconfigureUnattach` (`TurnSolver.cpp:31779-31819`). It must
  not go through the ModeSpec table, which would give a {T}-less row a K-count block.
- Gate:
  - the source is controlled;
  - at least 5 untapped controlled permanents with `CardHasSubtype(q.card, "Vampire")` exist (or
    `AnimatedAllTypes`), counting the source, and counting sick bodies;
  - at least 1 creature exists that we do not control.
- Emit one action per (target class × tap-set variant). See the search design.
- **Same-turn-cast source.** A Captivating cast earlier in the same plan is a legal source, since it may
  tap itself while sick. Emit hand-source variants carrying `needs_cast_mask`, the hand-Pod pairing
  precedent (`TurnSolver.cpp:33367-33389`). Then a subset without the cast rejects them.

**4. Liveness check**
- Add a branch to `PermAbilitySourceLive` (`SpellEffects.h:21508-21550`), checked before anything is paid
  (CR 602.5b discipline):
  - every pinned body is still controlled, untapped and a Vampire;
  - the victim is still a creature we do not control.
- If either fails, the stale activation is a clean no-op. **No substitution**: picking a replacement body
  or target would be an in-window greedy pick.

**5. Cost payment**
- Pay inside **one shared helper**, `PayTapSubtypeCost(state, controller, ids)`, called from **both**
  `ActivatePermAbility` apply sites (`TurnSolver.cpp:48436` and `AIEngine.cpp:6033`) before
  `ApplyPermAbility`.
- It taps the five bodies. It must **not** use `CanTapNow`, because sick bodies are legal.

**6. Effect**
- Add an `ApplyPermAbility` overload or argument that carries the victim id (`SpellEffects.h:21669`).
- The new case:
  - sets the controller;
  - sets `gained_control_this_turn = true`;
  - adds the Vampire subtype if absent;
  - emits a play event.
- **Owner stays the opponent.** That matters to A2: a stolen spawn that is sacrificed must go to its
  OWNER's graveyard.

**7. Attack projection**
- The pre-plan attack-damage discount (`TurnSolver.cpp:9965-10000`, the attack-tap discount over
  `sac_source_id`) must also count **the five tapped bodies** as non-attackers. Otherwise plans that
  contain a steal are over-scored.
- This is ordering and projection only. The rollout is exact, because tapped bodies fail `CanAttackFull`.

**8. Trailing-pass order**
- The steal raises the Vampire count, so it must run **before** Keeper's transform in the trailing pass
  (section 4, "Trailing order").

**9. Unchanged**
- The steal only touches spawn creatures. `HandCardContentHash` already folds `m_subtypes`
  (`TurnSolver.cpp:13280-13282`), so a Vampired spawn is distinct in every content-keyed fold.

### Search design (no greedy pick at remaining depth > 0)

**Target axis**
- Opponent creatures, identity-folded by `(name hash, P, T, tapped, counters)`. Identical spawns are
  interchangeable.
- That gives at most 2 classes in any spawn pattern; pattern 8, for example, has a 6/6 and a 1/1.
- Dominance (a provider prune, optional and PROVISIONAL): every spawn has P = T and no abilities. In
  this list a stolen creature's only uses are attacking (its power plus lords), counting as a Vampire or
  being fodder, and the last two are equal for every target. So the class with the largest P and T
  dominates.
- **That prune belongs in the provider**, as a "tested one-option prune". **Do not copy Bolas's
  in-engine "best power" pick** (`TurnSolver.cpp:30320-30346`). An engine-side pick among targets is
  exactly the shape the no-greedy rule forbids. The axis is so small that leaving it unpruned is also
  fine.

**Tap-set axis**
- Partition the untapped Vampires into classes:
  - FREE: the body cannot attack this turn (sick without haste). Key: `(colour mask, is source)`.
  - ATTACK-ELIGIBLE: key `(name hash, effective power incl. lords, counters, is_token, TapTokenSourceLive)`.
- One variant per distinct count vector summing to 5 within each class's multiplicity.
- Within a class, bodies are interchangeable for the rest of the phase, so the apply takes the
  lowest-`m_number` members. That is an identity fold, not a choice.
- Why colour is in the FREE key: convoke (Markov Baron) pays a coloured pip with a body of that colour.
  Keyed this way, the fold is exact even for a later main-2 convoke.
- Bound: C(n,5) folded by class, where n is the number of untapped Vampires (about 5-8). Typically 1-15
  variants, and only on turns where a Captivating is out, at least 5 untapped Vampires exist, and an
  opponent creature exists. That is rare, so the total branching cost is small.
- The provider may **order** the variants (fewest attack-eligible bodies first, then lowest summed power)
  so a binding budget spends on the cheap ones first. It may restrict only with a measured A/B.
- A **"never tap an attack-eligible body while a free one stays untapped"** prune is NOT a sound
  dominance (convoke colours, transform timing). If it is wanted for performance, it is a lossy provider
  prune that needs its A/B.

**How it is played**
- Rollout and executor apply the same `Action` (target id plus tap ids) through the shared helper and
  `ApplyPermAbility`. The rollout reaches it through `ApplyPlan`'s `apply_one` and the executor through
  `AIEngine`'s twin.
- Beyond the horizon (the d0 runner and the playout), the greedy policy may pick a variant by `eval`.
  That is the permitted greedy.
- `eval` is ordering only:
  `(stolen P + lord bonus) * DMG * max(0, ExpectedAttacks - 1) - sum(power of attack-eligible bodies tapped) * DMG`
  (the Bolas shape).

### Viewer (2c-ter)

| Choice | Bucket | Wiring |
|---|---|---|
| Activate the steal | **board activation, new verb `steal=<source>`** | `activate:true` and `"verb":"steal"` on the plan action (`main.cpp` ~2175-2195, the `taplife`/`landsearch` emitter, keyed on `ability_mode`). `LineSpec` key `steal` (`main.cpp:4084-4086`, `TurnSolver.h` ~2654). CheckLine matching (`TurnSolver.cpp` ~79276-79300 and ~79980-79990). GUI labels (`index.html` ~1705 / ~2622 / ~4084). A new verb is needed because the list runs 2 copies, so `cast=Captivating Vampire` is ambiguous with a hand cast. Add `"steal_tap_count": ("verb:steal", positive, None)` to `BOARD_ACTIVATIONS` (`scripts/audit_viewer_decisions.py` ~538-576). |
| Which creature (target) | **A: reuse `target`** | `g_play_loyalty_chooser` through `human_target(..., "taken under your control", ...)`, the Bolas -2 precedent (`SpellEffects.h:10538-10545`), called in the new `ApplyPermAbility` case. The legal set is every creature we do not control. Targeting our own creature is legal, but it is a no-op that wastes five taps; it is excluded on Bolas's precedent and disclosed. |
| Which five Vampires to tap | **A: reuse `target`**, asked 5 times in sequence (Magnetic Theft's "asked twice in sequence", `DECISIONS.md:78`) | `g_play_loyalty_chooser` with the prompt "tapped to pay Captivating Vampire's cost (k of 5)". The legal set is untapped Vampires we control, minus the ones already picked; sick ones are included. Called in `PayTapSubtypeCost`, the shared site, before resolution: targets are chosen before costs are paid (CR 602.2b → 601.2c, then 601.2g-h). So the target prompt comes first and the tap prompts after it. The preselected default is the plan's pinned body. |

**Human-play enumeration** (the Oko precedent, `TurnSolver.cpp:30603-30611`):
- Emit ONE representative steal action per source, gated on legality only.
- Both sub-choices are asked at apply, over the full legal sets.
- Add `DECISIONS.md` rows: a `target` reuse note for Captivating, and the `steal=` verb in the verbs
  table.

### Second main and deferrals

- **Second main:** relevant, because of Mavren's combat token and the post-combat non-attackers. It is
  already enabled through convoke (section 0, point 8).
- **Proposed deferrals (PROVISIONAL, need USER sign-off):**
  - (i) Our own creatures are excluded as targets. A legal no-op.
  - (ii) No opponent-turn activation window. Provably equivalent to main 2, argued above.
  - (iii) "Doesn't gain control of attached Auras/Equipment" is not modelled. Spawns carry none.

### Tests

- **Legality:**
  - 4 untapped Vampires → not offered;
  - 5 with the source sick → offered;
  - 5 with the source tapped → not offered;
  - an opponent with no creature → not offered.
- **Ruling:** a summoning-sick body is a legal cost body.
- **Effect:**
  - the controller changes and the owner does not;
  - `gained_control_this_turn` means it cannot attack this turn and can on the next;
  - it has the Vampire subtype;
  - Lieutenant, Captain and Captivating each give it +1/+1, and it has first strike while a Captain is
    out;
  - it counts toward Keeper's 5.
- **Folds:**
  - identical spawns fold to one target class;
  - tap-set classes fold interchangeable bodies;
  - two bodies of different colour do not fold.
- **Lockstep:** the rollout and the executor build the same board from the same action.
- **Stale:** a pinned body tapped earlier in the plan gives a no-op, with nothing paid.
- **Plan signature:** two variants that differ only in target or only in tap set do not dedup.
- **Viewer:** the `steal=` verb is offered (audit `BOARD_ACTIVATIONS`).

```json
{"name": "Captivating Vampire", "mana_cost": "{1}{B}{B}", "types": ["Creature"], "subtypes": ["Vampire"],
 "keywords": [], "power": 2, "toughness": 2,
 "oracle_text": "Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target creature. It becomes a Vampire in addition to its other types. [Lord: lord_effect + subtypes_affected [\"Vampire\"] + lord_excludes_self. STEAL: PermAbilityMode::TapSubtypeSteal (steal_tap_count 5 / steal_tap_subtype Vampire / steal_adds_subtype Vampire). No {T} in the cost, so the five may include summoning-sick Vampires and this creature itself (2010-08-15 ruling; CR 302.6 restricts only {T}/{Q} abilities). LIVE in a goldfish: the passive opponent's scheduled spawns (8 of 10 game indices; a 4/4 on T3 in slot 7, a 6/6 + 1/1 on T3 in slot 8) are the targets. Gain control is permanent (no duration), sets gained_control_this_turn so the creature cannot attack until our next turn (CR 302.6, the Bolas -2 precedent), and adds the Vampire subtype permanently -- so every Vampire lord reaches it and it counts as a Vampire for Bloodline Keeper's transform and Falkenrath Pit Fighter's sacrifice; its owner stays the opponent. The TARGET (one variant per distinct opponent creature) and the FIVE BODIES (one variant per distinct class multiset; within a class the bodies are interchangeable) are SEARCHED, offered in both mains; human play picks both on the board (`target`). PARTIAL, PROVISIONAL (USER sign-off pending): own creatures are not offered as targets (a legal no-op that taps five Vampires for nothing -- the Bolas precedent); no opponent-turn activation window (provably equivalent to the post-combat main: the untapped non-attackers are the same bodies and a creature stolen at the opponent's end step attacks on the same turn); 'does not take attached Auras/Equipment' is unmodelled (spawns carry none).]",
 "template": "lord_effect",
 "parameters": {"subtypes_affected": ["Vampire"], "power_bonus": 1, "tough_bonus": 1, "lord_excludes_self": true,
                "steal_tap_count": 5, "steal_tap_subtype": "Vampire", "steal_adds_subtype": "Vampire"}}
```

---------------------------------------------------------------------------------------------------------

## 4. Bloodline Keeper // Lord of Lineage — token Tier 1; transform Tier 3

Verbatim Scryfall fields:
- `layout: transform`, card cmc 4.0, card keywords `["Flying","Transform"]`.
- **Front:** `Bloodline Keeper`, `{2}{B}{B}`, `Creature — Vampire`, 3/3, colours B.
  Oracle: `Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate only if you control five or more Vampires.`
- **Back:** `Lord of Lineage`, mana_cost `""`, `Creature — Vampire`, 5/5, colours B (colour indicator).
  Oracle: `Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with flying.`
- The only ruling points to the SOI DFC article: a transforming permanent stays the same object. It keeps
  counters, tapped status and summoning-sickness status, and transforming is not entering. The
  back face's mana value is the front face's (4).

| Clause | Model | Tier |
|---|---|---|
| Flying (both faces, and on the tokens) | `Keyword::Flying`. Inert for combat (no blockers); populated anyway. | 1 |
| {T}: 2/2 black flying Vampire token (both faces) | `tap_token_cost "{0}"`, `tap_token_power 2`, `tap_token_toughness 2`, `tap_token_subtypes ["Vampire"]`, `tap_token_color "B"`, `tap_token_keywords ["Flying"]`. This is Speaker of the Heavens' shape: summoning sickness through `TapTokenSourceLive`, viewer verb `taptoken=`. **The colour is load-bearing**: a black token can pay Markov Baron's {B} by convoke. | 1 |
| {B}: Transform. Activate only if you control ≥5 Vampires | **NEW** `PermAbilityMode::TransformSelf`. No {T}, so it is legal while sick or tapped. Params: `transform_cost "{B}"`, `transform_min_controlled_subtype "Vampire"`, `transform_min_controlled_count 5`. Face link: `transform_back_name` (front) / `transform_front_name` (back). | 3 |
| Lord of Lineage: Other Vampire creatures +2/+2 | the back entry is `lord_effect`, Vampire, 2/2, `lord_excludes_self`. `LookupCached(p.card)` resolves the swapped face, so `ComputeLordBonus` finds it with no further wiring. | 1 (given the swap) |
| Back face MV = 4 | not modelled: the back entry has an empty cost, so MV reads 0. Inert here, because nothing in the list reads a battlefield permanent's MV (Moonrage Brute disclosure). | PROVISIONAL disclosure |
| Off the battlefield it is Bloodline Keeper (CR 712.8) | generalise `NormalizeLeftDayboundFace` and the `FireLeavesBattlefieldTriggers` gate (`SpellEffects.h:9637-9644`) to also key on `transform_front_name`. Reachable: Falkenrath Pit Fighter can sacrifice a Lord of Lineage. | 3 |

### C++ for the transform

1. **Params** in `CardParams` and `BuildParamsFromJson`, next to the daybound block
   (`CardDatabase.h:3854`, `CardDatabase.cpp:1743`):
   - `std::string transform_back_name, transform_front_name`
   - `std::optional<ManaCost> transform_cost`
   - `std::string transform_min_controlled_subtype`
   - `int transform_min_controlled_count = 0`
2. **Images:** add `transform_back_name` to `CardDatabase::MdfcBackFaceNames` (`CardDatabase.cpp:420-431`).
   Scryfall's default image for "Lord of Lineage" is the front face.
3. **Mode:** append `PermAbilityMode::TransformSelf` **last**, add it to the `PermAbilityTaps` exclusion
   list, add a label ("transform it (you control five or more Vampires)"), and add it to the name list at
   `TurnSolver.cpp:82802`.
4. **Emission:** one out-of-band block beside `ReconfigureUnattach` (`TurnSolver.cpp:31779-31819`), one
   action per untransformed source.
   - **Do not test the count at the enumeration stamp.** It must count Vampires the plan can still add:
     bodies cast this plan, Keeper/Lord tokens tapped this plan, a steal this plan.
   - So gate on the **upper bound**: current count + Vampire creature cards in hand + live tap-token
     sources + (1 if a steal is enumerable) ≥ 5.
   - Liveness re-checks the exact count at apply.
   - Admitting a transform that turns out infeasible costs a duplicate of the base plan. It never loses a
     line.
   - A Keeper **cast this plan** is a legal source, since transforming has no {T}. Emit the hand-source
     variant with `needs_cast_mask`, the hand-Pod precedent.
   - The `ActivationEquivTag` fold of two untransformed Keepers in the same state is an identity fold.
5. **Liveness:** a branch in `PermAbilitySourceLive`, checked before paying:
   - `transform_cost` is present and `transform_back_name` resolves;
   - the count of controlled permanents with `CardHasSubtype(transform_min_controlled_subtype)` or
     `AnimatedAllTypes` is at least `transform_min_controlled_count`. The source counts itself, and
     tokens and stolen creatures count.
   - CR 602.5b: the restriction is checked on activation.
6. **Cost:** {B}, paid by the existing apply sites under `CreatureAbilityPayScope`, because the source is
   a creature. Secluded Courtyard's coloured mana pays it. Unclaimed Territory's and Voldaren Estate's
   restricted coloured mana does not, which is correct per their oracle.
7. **`ApplyPermAbility` case:**
   - `SwapFace(p, *Lookup(transform_back_name))`, which is `SwapDayboundFace`'s body; rename or share it;
   - emit the "transforms into Lord of Lineage" play event;
   - **no** enter watchers, **no** ETB. In particular Vampire Socialite's "enters with an additional
     counter" and every enter cascade must not fire;
   - tapped, `entered_this_turn`, counters and `m_number` persist because the Permanent is kept.
   - Lord of Lineage has no transform ability, so the transform is one-shot. CR 701.28f, two activations
     stacked, is irrelevant: the engine resolves immediately and the back face cannot transform back.
8. **Trailing order:** `TurnSolver::OrderTrailingActivations` is shared by both trailing passes
   (`DecisionProvider.h:1277-1297`). Sort with a param-derived, engine-level class:
   **count-raising activations (`TapForTokenPay`, `TapSubtypeSteal`) → `TransformSelf` → count-lowering
   activations (Falkenrath Pit Fighter's "Sacrifice a Vampire", cluster A2)**.
   - This is a sound identity order, not a heuristic. The transform changes no other activation's
     legality, the raisers only raise, and the lowerers only lower. So every transform that is legal in
     any order is legal in this one.
   - Without it, "tap Keeper for a token → now 5 Vampires → transform" can be unfindable.
9. **Deck-trait stamps:** stamps that scan `deck.mainboard` (`GoldFishRunner::StampDeckTraits`,
   `DeckFeedsCombat`, `DeriveDependencyPulls`, `GoldFishRunner.cpp:375-420/550-580`) never see a back
   face's params. Today no stamp gates a lord or tap-token walk, so Lord of Lineage works. But any future
   stamp that gates such a walk must follow `transform_back_name`. Name this in a code comment.
10. **Back-face lord.** Lord of Lineage is never cast. Lords are found only through the battlefield walk,
    so the back entry's `lord_effect` template is all it needs.

### In-combat transform window (Mavren Fein's attack token)

- {B}: Transform is instant speed.
- The only way the Vampire count crosses 5 *during* combat is Mavren Fein's attack trigger. It makes an
  untapped 1/1 Vampire token at declare-attackers, after attackers are declared.
- Real play: Mavren's token makes 5 → activate {B} → Lord of Lineage gives this combat's attackers +2/+2.
  If the Keeper is attacking, it also attacks as a 5/5.
- A main-phase activation cannot express that line.
- A **transform in main 1 with the count already ≥ 5** gives the same result as a combat transform paid
  from the same mana. So the combat window is needed only when the count is below 5 before combat and
  reaches 5 after the attack triggers.
- Restricting the window to that case is an **identity fold**, not a prune.

**Wiring** (the Khalni in-combat crack precedent):
- Add `Plan::combat_transform_choice`, a bitmask over the untransformed Keepers (-1 = none). It rides
  `GameState::scripted_combat_transform`, the way `combat_crack_choice` rides
  `scripted_combat_land_search_crack` (`TurnSolver.h:1256-1260`, the variant fan at
  `TurnSolver.cpp:61064-61120`).
- It is consumed by **one shared helper**, `ApplyCombatTransforms(state, active, atk_idx)`, called in
  both worlds:
  - after `FireAttackCreateTokens` (Mavren's token is cluster C's trigger) and the attack pumps, and
    before anything reads power;
  - executor: next to `ApplyCombatAuraSwap` (`GameEngine.cpp:787`);
  - rollout: `TurnSolver.cpp:50066`.
- Pay {B} through the combat-time payer, as the aura swap pays {2}{U}.
- Fold the pin into the plan hash, `SamePlan`, `IsApplyEmptyPlan`, `PlanIsAxisVariant`, the census key
  and the sim/dedup keys, as `combat_crack_choice` is folded (`TurnSolver.cpp:880/1520/20156/20430/37951/60753/61619`).
  Update the `kPlanDomAssertedSize` pin.
- **Viewer:** reuse `dig` with a "(combat transform)" note, the Khalni "(combat crack)" shape
  (`DECISIONS.md:61`):
  - `examined` = the transformable Keepers;
  - -1 = do not transform;
  - default -1 (hold), unless the search pinned a transform;
  - asked only when legal and affordable.
  This needs an emitter note branch and a GUI wording branch.

### Search design (each decision is a branch, executed identically, no greedy in the window)

- **Keeper: attack or tap for a token.**
  - `TapForTokenPay` is a plan-subset candidate in main 1 and main 2 (`TurnSolver.cpp:32296-32330`). A
    Keeper tapped in main 1 does not attack.
  - The attack set itself is the standing exemption (`AttackWith`).
  - Apply: `TurnSolver.cpp:48805` (rollout) and the executor twin, both re-checking
    `TapTokenSourceLive`.
  - The attack-tap discount projection (`TurnSolver.cpp:9984-9990`) already covers `TapForTokenPay` on
    an attacker.
  - Tapping at the opponent's end step equals tapping in our main phase when Keeper does not attack:
    either way the token is under our control before our next turn begins. So no opponent-turn window is
    needed. Disclose it.
  - Works on a transformed Lord too: the apply looks up the **stamped** `card_name`'s definition, both
    faces carry identical `tap_token_*` params, and `TapTokenSourceLive` reads the live permanent.
- **Transform timing:**
  - `TransformSelf` is a plan-subset candidate in main 1 (this combat's pump), in main 2 (future turns
    only; useful when post-combat casts or the Mavren token supply the 5th Vampire with leftover mana),
    and in the combat window above;
  - the trailing order puts it after token taps and steals;
  - all three routes apply through shared code (ApplyPlan / the AIEngine twin / `ApplyCombatTransforms`);
  - `eval` (ordering only) = `2*DMG*(attack-eligible other Vampires)` + `2*DMG` if the source itself can
    attack.
- **Two Keepers:** each one's transform and token tap are separate candidates. Identical-state pairs fold
  through `ActivationEquivTag`.
- **Beyond the horizon:** the playout's greedy policy may use these by `eval`. That is permitted.

### Viewer (2c-ter)

| Choice | Bucket | Wiring |
|---|---|---|
| Tap for a token (either face) | **A** | the existing `taptoken=<source>` verb and the `BOARD_ACTIVATIONS` `tap_token_cost` row (`audit_viewer_decisions.py:576`); sickness gate through `TapTokenSourceLive`. Nothing to build. |
| Transform in main | **board activation, new verb `transform=<source>`** (the `taplife=`/`landsearch=` shape keyed on `ability_mode`) | 2 copies, so `cast=Bloodline Keeper` is ambiguous with a hand copy. Same five sites as `steal=`: `main.cpp` verb emitter, `LineSpec` parse, `TurnSolver.h` `LineSpec` field, CheckLine, `index.html` labels. Add a `BOARD_ACTIVATIONS` row `"transform_cost": ("verb:transform", truthy, "transform_cost")`. |
| Transform in combat | **A (reuse `dig`) with a wording branch** | see the combat window above |
| Art | — | the back face image through `MdfcBackFaceNames` |

Add `DECISIONS.md` rows: the `transform=` verb, the `dig` "(combat transform)" reuse, and the
`taptoken=` note naming Bloodline Keeper / Lord of Lineage.

### Second main, deferrals, risks

- **Second main:** relevant (Mavren token or a post-combat Vampire cast plus leftover {B}). Already on
  through convoke. The combat window covers the strictly better in-combat case.
- **Proposed deferrals (PROVISIONAL, USER sign-off):**
  - (i) The back face's MV reads 0 instead of 4. Unobservable, because no card in the list reads a
    battlefield MV. Also, Aether Vial only puts from hand.
  - (ii) No opponent-turn window for the token tap. Provably equivalent.

```json
{"name": "Bloodline Keeper", "mana_cost": "{2}{B}{B}", "types": ["Creature"], "subtypes": ["Vampire"],
 "keywords": ["Flying"], "power": 3, "toughness": 3,
 "oracle_text": "Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate only if you control five or more Vampires. [TRANSFORMING DFC, FRONT FACE (Bloodline Keeper // Lord of Lineage); listed under the front name as in every zone but the battlefield (CR 712.8). TOKEN: the tap_token family (Speaker of the Heavens' shape, cost {0}): {T} the source -- summoning sickness applies (TapTokenSourceLive, CR 302.6) -- for a 2/2 BLACK Vampire token with Flying, entering summoning-sick; the colour is load-bearing (a black body pays Markov Baron's {B} by convoke) and every Vampire lord reaches the token. Tapping it means it does not attack: the activation is a searched plan candidate, both mains. TRANSFORM: PermAbilityMode::TransformSelf -- no {T}, so legal while summoning-sick or tapped; {B} paid as a creature-source ability (Secluded Courtyard's coloured mana may pay it; Unclaimed Territory's and Voldaren Estate's restricted mana may not); the restriction counts Vampire PERMANENTS you control, this one, tokens and a stolen creature included, re-checked on activation (CR 602.5b). Transforming swaps the permanent to the Lord of Lineage entry in place: same object (m_number, counters, tapped and summoning-sick status kept), not an entering -- no enter watcher or ETB fires (Vampire Socialite's enters-with-a-counter does not apply). One-shot: the back face has no transform ability. Offered in main 1, main 2 and an IN-COMBAT window after declare-attackers (Plan::combat_transform_choice), which is the only route when Mavren Fein's attack token is the fifth Vampire. Trailing-pass order: token taps and steals before the transform, sacrifices after. Flying: parsed, inert for combat (no blockers).]",
 "template": "vanilla_creature",
 "parameters": {"tap_token_cost": "{0}", "tap_token_power": 2, "tap_token_toughness": 2, "tap_token_subtypes": ["Vampire"],
                "tap_token_color": "B", "tap_token_keywords": ["Flying"],
                "transform_back_name": "Lord of Lineage", "transform_cost": "{B}",
                "transform_min_controlled_subtype": "Vampire", "transform_min_controlled_count": 5}}
```

```json
{"name": "Lord of Lineage", "mana_cost": "", "types": ["Creature"], "subtypes": ["Vampire"],
 "keywords": ["Flying"], "colors": ["B"], "power": 5, "toughness": 5,
 "oracle_text": "Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with flying. [TRANSFORMING DFC, BACK FACE of Bloodline Keeper // Lord of Lineage. NEVER CAST and never in any zone but the battlefield: the entry exists so a transformed permanent resolves its own characteristics through LookupCached on its name (the Moonrage Brute precedent). Black by colour indicator (load-bearing: convoke). Lord: lord_effect + subtypes_affected [\"Vampire\"] + lord_excludes_self, +2/+2, read by ComputeLordBonus off the swapped face. Token ability identical to the front face's (tap_token family, summoning sickness through TapTokenSourceLive -- a Keeper that transformed on the turn it entered is still sick). Leaving the battlefield it becomes a Bloodline Keeper card (CR 712.8; NormalizeLeftDayboundFace generalised to transform_front_name) -- reachable through Falkenrath Pit Fighter's sacrifice. PARTIAL, PROVISIONAL: its mana value on the battlefield reads 0 (empty cost) where the rules give the front face's 4 -- no card in this list reads a battlefield permanent's mana value. Flying parsed, inert (no blockers).]",
 "template": "lord_effect",
 "parameters": {"subtypes_affected": ["Vampire"], "power_bonus": 2, "tough_bonus": 2, "lord_excludes_self": true,
                "tap_token_cost": "{0}", "tap_token_power": 2, "tap_token_toughness": 2, "tap_token_subtypes": ["Vampire"],
                "tap_token_color": "B", "tap_token_keywords": ["Flying"],
                "transform_front_name": "Bloodline Keeper"}}
```

`scryfall_divergences.json`: add `keywords` entries for **both** names. The wording follows Moonrage
Brute's: "Transforming DFC: Scryfall reports card-level keywords; Transform is the layout itself, Flying
is tagged". After adding the cards, run `audit_card_fields.py --update` and then the offline diff. The
cost audit skips the back face because its cost is empty, the Moonrage precedent.

### Tests (`test/unit/test_vampires_keeper.cpp`, Speaker-test shape)

1. **Token:**
   - a sick Keeper is not offered;
   - a non-sick one is offered;
   - it makes a 2/2 black flying Vampire, summoning sick, and the Keeper ends tapped;
   - the rollout and the executor build the same board;
   - a token gets +1/+1 from a Lieutenant.
2. **Transform legality:**
   - 4 Vampires → no-op with nothing paid;
   - 5 counting the Keeper and tokens → it transforms;
   - a sick Keeper may transform;
   - a tapped Keeper may transform.
3. **Payment:**
   - Swamp and Courtyard pay {B};
   - Unclaimed Territory's coloured mana alone does not;
   - Voldaren Estate's restricted mana alone does not.
4. **Swap fidelity:**
   - `m_number`, tapped, `entered_this_turn` and counters persist;
   - `LookupCached` returns Lord of Lineage;
   - other Vampires get +2/+2 and Lord does not pump itself;
   - no enter watcher fires;
   - no transform is offered on the back face.
5. **Lord of Lineage token:** it taps for a token on a later turn, and does not while still sick.
6. **Trailing order:** "Keeper taps for its token (4 → 5), then transforms", within one plan, is realised
   in both worlds.
7. **Leaves:** a sacrificed Lord of Lineage is a Bloodline Keeper in the graveyard.
8. **Combat window:** with 4 Vampires before combat, Mavren's token makes 5 → the pinned combat transform
   pumps the attackers this combat. An unpinned window holds.
9. **Art:** `MdfcBackFaceNames` contains "Lord of Lineage".

---------------------------------------------------------------------------------------------------------

## 5. Vampire of the Dire Moon — Tier 1

Verbatim Scryfall fields:
- `{B}`, cmc 1
- `Creature — Vampire`, 1/1, keywords `["Lifelink","Deathtouch"]`
- oracle `Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by this creature also causes you to gain that much life.)`

| Clause | Model | Tier |
|---|---|---|
| Deathtouch | `Keyword::Deathtouch` parsed. **Inert:** it deals damage only to the opponent's face (no blockers, no fight or bite in the list). | 1 (disclosure) |
| Lifelink | `Keyword::Lifelink`, read by `CreatureHasLifelink` in the shared combat core. **Modelled, and not quite inert:** our life matters through Asylum Visitor's "lose 1 life" draws and Voldaren Estate's "pay 1 life". The list has no "whenever you gain life" payoff. | 1 |

```json
{"name": "Vampire of the Dire Moon", "mana_cost": "{B}", "types": ["Creature"], "subtypes": ["Vampire"],
 "keywords": ["Deathtouch", "Lifelink"], "power": 1, "toughness": 1,
 "oracle_text": "Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by this creature also causes you to gain that much life.) [Lifelink: printed keyword, read by CreatureHasLifelink in the shared combat core (executor + rollout) -- a life-gain event per connecting swing; our life total matters to Asylum Visitor's draw-and-lose-1 and Voldaren Estate's pay-1-life, nothing in the list triggers on gaining life. PARTIAL, PROVISIONAL: Deathtouch is parsed (Keyword::Deathtouch) but has no reader -- structurally inert: it only ever deals damage to the opponent's face (the passive opponent never blocks; no fight/bite effect in the list).]",
 "template": "vanilla_creature", "parameters": {}}
```

- **C++:** none.
- **Search:** none.
- **Viewer:** none.
- **Second main:** none.
- **Deferral:** deathtouch is inert (PROVISIONAL, USER sign-off).
- **Test:** connecting with it gains 1 life (an existing combat-core path; optional).

---------------------------------------------------------------------------------------------------------

## 6. Swamp — Tier 1

Verbatim Scryfall fields: `Basic Land — Swamp`, cost `""`, oracle `({T}: Add {B}.)`. It is genuinely
missing from `cards.json`, which has Mountain, Plains, Island and Forest but no Swamp.

```json
{"name": "Swamp", "mana_cost": "", "types": ["Land"], "supertypes": ["Basic"], "subtypes": ["Swamp"], "keywords": [],
 "oracle_text": "({T}: Add {B}.)", "template": "basic_land", "parameters": {"produces": ["B"]}}
```

- C++, search, viewer and second main: none.
- **Test:** none needed beyond the coverage check. Optionally, "Swamp pays Keeper's {B} transform".

---------------------------------------------------------------------------------------------------------

## 7. Bracket notes to append (the Pirates / Soldiers precedent)

Append to **Unclaimed Territory** (`src/cards/data/cards.json`):

> [Vampires (2026-10-18): EXACT there too -- every creature card in that list is a Vampire (Scryfall type lines verified for all 14 creature cards, both faces of Bloodline Keeper included), so naming Vampire pays for every creature spell it casts, madness casts included; its coloured mana is correctly unusable for the list's creature abilities (Falkenrath Pit Fighter's {1}{R}, Bloodline Keeper's {B} transform), which this land's oracle does not cover.]

Append to **Secluded Courtyard**:

> [Vampires (2026-10-18): EXACT there too -- every creature card in that list is a Vampire (Scryfall type lines verified for all 14 creature cards, both faces of Bloodline Keeper included), so naming Vampire pays for every creature spell it casts and every creature-source ability it activates (Falkenrath Pit Fighter's {1}{R}, Bloodline Keeper's {B} transform, both paid under CreatureAbilityPayScope).]

---------------------------------------------------------------------------------------------------------

## 8. Cross-cluster notes for the integrator

- **For C (Drana):**
  - consume `CreatureHasFirstStrike` and `CreatureHasDoubleStrike`, snapshotted at the start of the
    first-strike step (CR 510.4 / 702.7c);
  - the Captain-plus-Drana anti-synergy (section 2);
  - the universal-scope question for Angels (Lyra + Archangel of Thune + Righteous Valkyrie) and Soldiers
    (Rick).
- **For C (Mavren Fein):**
  - his attack token is untapped, so it can pay Captivating's cost post-combat;
  - it is the only way the Vampire count crosses 5 in combat, which is what the Keeper combat window
    exists for;
  - a stolen spawn is a **nontoken** Vampire, so attacking with it alone triggers Mavren.
- **For C (Socialite):** "each other Vampire you control enters with an additional +1/+1 counter" must
  reach Keeper and Lord tokens, which enter through `CreateToken`. It must **not** fire on a transform.
- **For A1 (Markov Baron, convoke):** colour classes in Captivating's free-body key exist because of
  convoke. Convoke already enables main 2 (`GoldFishRunner.cpp:86`).
- **For A2 (Falkenrath Pit Fighter, "Sacrifice a Vampire"):**
  - a stolen spawn is legal fodder and goes to its **owner's** (the opponent's) graveyard;
  - a sacrificed Lord of Lineage becomes a Bloodline Keeper card;
  - Pit Fighter's activation must sort **after** `TransformSelf` in the trailing pass.
- **Main split (USER-owned):** the steal, transform and token taps are offered in both mains with no
  main-1 prune. Whether Captain is castable in main 2 on a Drana turn is the USER's call.

## 9. Proposed deferrals, all PROVISIONAL until the USER signs off

1. Vampire of the Dire Moon: deathtouch has no reader (inert, face damage only).
2. Flying everywhere (Keeper, Lord, tokens): parsed, no blocker path (the standard disclosure).
3. Captivating Vampire: our own creatures are not offered as steal targets (a legal no-op; the Bolas
   precedent).
4. Captivating Vampire and Bloodline Keeper's token tap: no opponent-turn activation window. Provably
   equivalent to our main 2 or main 1, argued above.
5. Captivating Vampire: "does not take attached Auras/Equipment" is not modelled (spawns carry none).
6. Lord of Lineage: its battlefield MV reads 0 instead of 4 (no battlefield-MV reader in the list).
7. Lieutenant's "Vampires" evaluated as "Vampire creatures" (no noncreature Vampire permanent exists).

Nothing that changes outcomes is deferred. First strike, the steal, the transform (including the
in-combat window) and the token are all designed to be implemented.

## 10. Top risks

1. **Transform reachability.**
   - The count gate must look ahead to Vampires the plan can still add (upper bound at enumeration,
     exact re-check at apply). Same-plan-cast Keepers and Captivatings need `needs_cast_mask` sources.
   - The trailing pass must run raisers, then transform, then sacrifices.
   - Miss any of these and lines like "cast the 5th Vampire, then transform" or "token, then transform"
     become unfindable at every budget. That is the completeness invariant.
2. **The first-strike step is a combat-core change with reach beyond this deck.** It is cluster C's
   change, and this cluster supplies its oracle. A faithful universal split moves Angels and possibly
   Soldiers GT. A Drana-only gate is the "scope by GT stillness" shape CLAUDE.md forbids. **That is the
   USER's decision.**
3. **Captivating's searched sub-decisions must reach every key.**
   - The target and the tap set must be folded into the plan signature / `SamePlan` / dedup / census /
     sim keys. Otherwise the dedup silently keeps the first variant enumerated, which is the
     core-invariant failure.
   - The class keys must be exact (colour for convoke, `TapTokenSourceLive` for Keeper).
   - The attack projection must discount the tapped bodies.
   - The new `Action` field must respect the pinned `kPlanDomAssertedSize`.
