# Vampires — cluster C integration notes (combat / ETB triggers)

Track C of the Vampires onboarding: the CR 510.4 two-step combat damage model, Stromkirk Captain,
Drana, Liberator of Malakir, Mavren Fein, Dusk Apostle and Vampire Socialite. Branch `vampires-combat`
(worktree `.claude/worktrees/vampires-c`), on top of `cbbe86a4e`. Designs: `design_C_combat.md`,
`design_B_lords.md` §2 (Stromkirk Captain). The ledger (`analysis-Vampires.md`) is owned by the other
track; everything Track C has to record is here, to be folded in when the branches are rebased together.

## What landed

| Piece | Where | Notes |
|---|---|---|
| Two-step combat damage (CR 510.4) | `Combat.cpp` `ResolveCombatDamageTwoStep` | Entered when ANY attacker has first or double strike, in every deck (open question 2 below). Membership snapshotted before the first step; attackers tapped at declaration (vigilance read on the pre-damage board); power recomputed per step (double strike = a hit in each step); lifelink = one `GainLife` event per member per step; each step fires its own combat-damage triggers (Drana, Goblin Lackey, Shroofus, Neheb) in the single-step order; Leeching Sliver / Utvara fire once after the regular step's lifelink (the single-step relative order). Jitte keeps its closed form, applied once in the bearer's first step. The play-viewer line keeps one `Name (damage)` per attacker (its total), in attack order. Combats with no first/double striker run the old loop unchanged (its double-strike answer is now precomputed in the membership pass -- same value). |
| `CreatureHasFirstStrike` | `SpellEffects.h` (beside `CreatureHasDoubleStrike`) | Printed keyword; `grants_first_strike` subtype lords (Stromkirk Captain, Field Marshal, Neheb, Striking Sliver) with `lord_excludes_self` by m_number; attached `aura_grants_first_strike` Auras (Hyena Umbra, Indrik Umbra, Ethereal Armor, Daybreak Coronet); Rick's chosen first strike. `BoardSources::fs` prefilter. Not answered (each disclosed on its card): Boros Reckoner's and Slaughter-Priest's "gains first strike until end of turn" activations (not modelled as activations), Cosmic Spider-Man's grant to other Spiders (deferred, no other Spider). |
| Drana | `combat_damage_counters_each_attacker`, `FireCombatDamageTeamCounters` | Per step she connects in; `PutPlusCounters` on every creature still in `atk_idx`; one play event per trigger. Never scaled by heads. Single-step path calls it too (only reachable by a Drana-like source without first strike). |
| Projection twin | `TurnSolver.cpp` `PendingAttackDamageImpl` | Exact Drana addend: `per x (#regular-step attackers + attack-trigger tokens without a first-strike grant)`, `per` = sum of first-step Drana sources with power > 0, doubler-aware. 0 under a Captain. A double-striking Jitte bearer is excluded from the regular count (the combat core deals its closed form in step 1). Gated on `bs.cd_counters` non-empty. |
| Mavren Fein | `attack_one_or_more_*`, `FireAttackOneOrMoreTokens` | Called in `GameEngine::CombatPhase` and `TurnSolver::SimulateCombat` right after `declared_n`, before the Adeline block. One token per combat, DECLARED nontoken Vampire required, untapped / not attacking / through `CreateToken`. Never per head. The token keyword map learned `"Lifelink"` (no existing token spec carried it, so byte-identical; the unit test caught the silent drop). |
| Vampire Socialite | `etb_each_other_own_creature_counters_if_opp_lost_life` (Lieutenant block), `opp_lost_life_other_enters_counters/_subtype` (FireEtbWatchers as-enters block) | Presence gate `deck_has_subtype_enter_counters` opened by the param. Static inherits the block's `AddPlusCounters` (not doubler-aware) -- no doubler in the list. |
| Second main | `GoldFishRunner::DeckUsesSecondMain` | Socialite's two params and Mavren's token param each flip it (not parasitic on Markov Baron's convoke). |
| Viewer | `scripts/audit_viewer_decisions.py` INERT_PARAMS | All new params bucket A (no new decision type). Classifications are the integrator's; PENDING USER OK. |
| Rick provider comment | `DecisionProviders.cpp` `EtbChosenKeywords` | Comment only: first strike now has a reader; ordering unchanged (vigilance + lifelink first). |

## Deviations from the design (and why)

1. **Split scope = every first/double-strike combat**, not "a Drana-like attacker strikes first" (the
   design's gate). The orchestrator's default for open question 2; CLAUDE.md "scope a change by where it
   is VALID". Consequence: the split moves Angels and Critter (see smoke below).
2. **Attackers tap before the first step** (the design tapped at first appearance). Found by the smoke
   audit: Voice of the Blessed gained vigilance between the steps and stayed untapped. CR 508.1f.
3. **First-strike grants wired for every existing granter** (Field Marshal, Neheb, Striking Sliver, the
   four Auras, Rick), not only the Captain. With a universal split, a granted first striker that reads
   false would deal its damage after the between-step effects -- an over-count. Striking Sliver gained
   `subtypes_affected ["Sliver"]` (every other reader of that param is gated on power_bonus /
   grants_haste / attack_trigger_life_loss -- checked).
4. **Board sources re-gathered for the regular step** when a first-step hook grew the battlefield (a
   Lackey put / Shroofus token can be a lord or attachment the regular step must read). Not in the design.
5. **Projection gap disclosed, not closed**: `PendingAttackDamage` prices Drana exactly but does NOT price
   a first-step lifelink gain's watchers (Archangel of Thune, Lyra Archangel of Dawn, Voice of the
   Blessed, Ajani's Pridemate, Righteous Valkyrie / Serra Ascendant thresholds). Conservative
   under-credit (the safe direction); rollouts and the executor realise the damage. Open question 3.
6. Design §2.4's optional `AttackHasNonPowerValue` clause for a 0-power Vampire attacker is not added
   (inert: every nontoken Vampire in the list has power >= 1).

## Other attack-damage estimators (audited, unchanged)

Only `PendingAttackDamageImpl` carries the Drana addend. The rest under-credit a Drana board (never
over-credit) and are unreachable for Vampires or irrelevant to it:
`CollectAttackingManaSources` (a creature mana source's forfeited attack excludes the counter -- the
list has no creature mana source; `MTG_ATTACK_TAP_REPROJECT` re-runs the projection and inherits the
addend), `ProjectPlanWalk`'s hasty-body credit (TurnSolver ~10813/10990: a haste Bloodghast cast this
turn is credited without +1), the equip/aura delta estimators (`AttackersCombatDamage`,
`CountAttackGatherPump`, `CountAttackAttachPump`, `AuraHostKeyAttackSum`, the DecisionProviders equip-host
rankers), the dork ranker (TurnSolver ~12112) and the cast-plan lethal estimators (~28067/28654/29286).
The collapsed `x2` for double strike in those estimators is exact for damage totals wherever no
between-step effect exists.

## Card-note fixes in other decks (oracle_text only; no behaviour change except the grant params)

- **Wrong for their lists:** Lyra Dawnbringer ("strike order can never change an outcome") and Ocelot
  Pride (same claim) -- both are first-strike lifelinkers in lists with between-step lifegain watchers.
- Stale mechanism wording ("Keyword::FirstStrike has no readers", "collapsed into one event", "x2"):
  Field Marshal, Striking Sliver, Neheb (was deferral D4), Rick, Dwalin, Moonrage Brute, Balan, Knight
  Exemplar, Goblin Chainwhirler, Burning-Fist Minotaur (was D5), Cosmic Spider-Man, Thalia, Ethereal
  Armor, Daybreak Coronet, Hyena Umbra, Indrik Umbra, Boros Reckoner (D2 reworded), Slaughter-Priest
  (D9 reworded), Swiftblade Vindicator, Kor Duelist, Kinsbaile Cavalier.
- Scenario re-pinned: `test/scenarios/critter_ocelot_pride_ascend_doubling.json` opponent_life 35 -> 34
  (the Pridemate swings with Ocelot Pride's first-step counter); its real discriminator (our life 25) is
  unchanged.

## Verification

- Unit: `test/unit/test_vampires_combat.cpp` (16 cases) -- the oracle and every grant shape; Drana
  splits (8 = 2 + 3 + 3, counters on all three, descs format, projection == rollout == realised); Captain
  makes the counters add nothing (8, projection 8 not 11; Captain at home still 6); Drana sick / alone;
  double strike a hit per step (+ Drana raises the second hit); a double striker's lifelink is two events
  feeding each other through Thune; Lyra + Thune (10, was 9); Ocelot Pride + Pridemate; tap at
  declaration vs between-step vigilance; single-step path unchanged; Mavren (one token, nontoken
  requirement, non-Vampire attacker, sick Mavren, 2 heads, rollout combat); Socialite ETB intervening-if,
  recipients, the Lieutenant block unchanged; Socialite static for tokens / non-Vampires / flag false /
  Mavren's token; DeckUsesSecondMain. Full suite green except `test_pirates_provider.cpp:202`, which
  fails identically at the base commit (decks/Vampires exists, its routing pin is Track A's).
- **Smoke** (`env -C <worktree> bash test/regression.sh --smoke`, NOT accepted; final run at `2b879fee5`):
  `120 passed, 8 failed`. Scenarios 217/217 after the Ocelot Pride re-pin. **Every deck without a
  between-step effect is byte-identical, including every first/double-strike deck** (slivers, knights,
  whiteknights, auras, goblins, fivecolour, minotaur, kittyv3, soldiers, colossus, and their 2HG rows).
  The 8 that moved are exactly Angels and Critter:

  | case | exp | got | faster | slower | same score, play changed |
  |---|---|---|---|---|---|
  | critter d0 | 4.6190 | 4.5540 | 65 | 0 | 423 |
  | critter d3 | 4.3867 | 4.3200 | 10 | 0 | 66 |
  | critter d5 | 4.4000 | 4.3467 | 4 | 0 | 36 |
  | critter2hg d3 | 4.7600 | 4.6800 | 4 | 0 | 25 |
  | angels d0 | 5.2170 | 5.2170 | 0 | 0 | 36 |
  | angels d3 | 4.6280 | 4.6280 | 0 | 0 | 5 |
  | angels d5 | 4.5867 | 4.5867 | 0 | 0 | 2 |
  | angels2hg d3 | 5.1400 | 5.1300 | 1 | 0 | 2 |

  **0 slower games anywhere.** Mechanism, verified on single-game repros (`test/explain_game.py
  --old-bin <base>`, kept hand + draws identical in each): critter d3 gi11 5->4 (T3/T4: Ocelot Pride's
  first-step lifelink puts Voice of the Blessed's / Archangel of Thune's counters on the regular-step
  attackers, T4 6 -> 0 left); critter d0 gi142 6->5 (T5: the Pride's gain triggers the just-cast Thune's
  team wave before the regular step, 2 -> -1); critter d5 gi35 5->4 (two Prides = two first-step gain
  events = two Thune waves + Voice: 12 -> 28 damage); angels2hg d3 gi26 6->5 (the search now values Lyra
  Dawnbringer's first-step gain feeding Lyra, Archangel of Dawn's "+1/+1 counter on each Angel" and picks
  a different T3-T5 line); angels d3 gi229 / d0 gi11 same win turn, 2-4 more damage on the Lyra turn
  (same mechanism). The same-score play changes are the search re-pricing those combats in its rollouts.
- **Reference replay** (`test/viewer_protocol_check.py --threads 16 --strict`, the regression tier's
  gate; not part of smoke): `219 ok, 410 repaired, 1 play-drift, 0 enum-gap, 0 board-diverged`.
  The drift is `references/CritterLifegain/claude_s6_gi5.json`: the user's recorded line now wins **T4
  instead of T5** (base binary: `ok`, T5). Proven on the replay: the T4 attack is identical in both
  builds except Serra Ascendant, 1 -> 6 power -- the two Ocelot Prides' first-strike lifelink (and the
  life-gain triggers it sets off) lift our life to 30+ BETWEEN the damage steps, so Serra Ascendant's
  "30 or more life: +5/+5" is on before the regular step (CR 510.4); damage 11 -> 16, opponent 15 -> -1.
  The recorded T5 rode the old one-step model. Per the standing rule this needs the USER's sign-off; the
  reference file is untouched. Until then the regression tier's `--strict` reference gate reports it.
- `audit_card_costs.py`: 442 costed cards, 435 compared, all match -- the four new cards included; rc=2
  because 7 unrelated cards (Ancestral Anger, Oracle's Restoration, Expedite, Impolite Entrance, Scale the
  Heights, Twinflame, Bonesplitter) were rate-limited (HTTP 429), i.e. UNVERIFIED, not failed. Re-run owed.
  `audit_card_fields.py`: snapshot updated for the four new cards
  (targeted `do_update`); hard fields match; the remaining UNFETCHED entries (Legion Lieutenant, Vampire
  of the Dire Moon, Swamp from the base commit; three Temples) are not Track C's.

## Open questions for the USER (work continued on the stated default)

1. **(ledger Q2) First-strike two-step split applies to every deck** -- default taken. It moves Angels and
   Critter (faster or equal in every changed game, see smoke). Accepting their GT is the next step (not
   done here).
1b. **Reference `CritterLifegain/claude_s6_gi5` now wins T4 (recorded T5)** -- the recorded line rode the
   one-step model (Serra Ascendant's 30-life anthem now switches on between the damage steps). Needs the
   USER's sign-off; the regression tier's strict reference gate shows it as play-drift until then.
2. **INERT_PARAMS classifications** for the new params (all bucket A, listed in the audit script under
   "Vampires cluster C") -- integrator's call, needs the USER's OK per the self-guard.
3. **Projection pricing of first-step lifelink watchers** (Angels / Critter): the search's attack
   projection under-credits them (safe direction). Closing it needs a lifegain-watcher model in the
   projection. Default: leave the conservative gap, disclosed.
4. **Rick's chosen-keyword provider order** still prefers vigilance + lifelink over first strike. Exact for
   Soldiers (no between-step effect); flag only because the comment's old premise ("no reader") is gone.
5. **D-C4 (no priority window between the damage steps)**: an Aether Vial put / madness Socialite after
   Drana connects, before the regular step, is inexpressible. ~<=1% of games. PROVISIONAL deferral.
6. Drana's Flying and Socialite's Menace inert; CR 614.12 simultaneous entry (D-C3) -- PROVISIONAL.
