# Control mode: blocking in the gauntlet replays

Status: DESIGN (2026-11-27), not built. It is the implementation design for section 5.6 of
`docs/design/control-scoring-mode.md` (blocking as a control effect) and the USER answers to Q10 (the gauntlet
deck's attacks) and Q11 (goldfish never blocks). Read that section first. This doc does not restate the
objective, the mapping or the partial scores; a blocker enters them as one more scheduled effect.

**Summary.**
* **One new combat step, armed per game, nowhere else.** A declare-blockers step is added to the shared combat
  core (`src/ai/Combat.cpp`) and to both of its callers (the executor's `GameEngine::CombatPhase` and the
  rollout's `SimulateOneCombat`). It runs only in a game whose injection schedule puts a creature on OUR side
  (the passive opponent, player 1, in the gauntlet replay). `GameState::blocking_armed` is false in every
  other game (goldfish, 2HG, the viewer, claude-play, every tier), and every new branch tests that bool first.
  So goldfish ground truth cannot move (Q11).
* **The rules are the post-Foundations rules.** Damage assignment ORDER no longer exists (Magic Foundations,
  November 2024): a blocked attacker divides its damage among its blockers freely; only trample still needs
  lethal damage on every blocker first. `.claude/skills/mtg-rules.md` still describes the old order rule and
  has one wrong blocking statement (section 1.1). Fixing the skill is owed; this doc did not touch it.
* **The card pool was written for a world without blocks.** 195 cards in the 67 deck folders carry a card note
  whose model rests on "the opponent never blocks / never attacks". 112 of them are evasion or combat keywords
  that are parsed but have NO reader (flying, trample and menace have none at all today). Day one needs the
  evasion and damage keywords of the gauntlet's attackers, and a premise audit of the 23 provider prunes and
  collapses that assume opposing creatures never block (section 2).
* **The defender (us):** a clairvoyant SCORED choice, measured by forking the replay one combat deep and
  keeping the assignment that delays the gauntlet deck most. Beside it, a deterministic non-clairvoyant RULE
  built from the USER's rulings (soak, chump on the lethal turn, body value, reflectors, Nemesis). The rule is
  also how the gauntlet's search predicts our blocks, and how later combats inside a fork are played.
* **The attacker (the gauntlet deck), per Q10:** the sound provider prunes (a creature that cannot die
  attacks; interchangeable attackers fold into counts), a searched count `k = 0..n` per at-risk class, and the
  USER's one-sided-loss rule as a heuristic prune that ships OFF until a held-out check (its default is the
  USER's call).
* **A new mid-combat trick root** after blockers are declared, which always offers "do nothing". In an armed
  game the leftover-mana firebreathing sink moves into it, because against blockers it is no longer dominant.
* **The playout blocks too**, using the same rule. A gauntlet deck runs LEAFLESS from the round our first
  creature arrives, because its value leaf was fitted to blocker-free play. Stage 1 measures what that costs.
* **13 small steps** (section 7), each with its unit or scenario tests and the byte-identity check.
* **Open questions:** section 8, each with the default this design takes.

## 0. Where things stand (code read 2026-11-27, branch `feature/control-mode`)

* `Step::DeclareBlockers` exists (`src/core/GameState.h:133`) and nothing uses it.
* `ResolveCombatDamage` (`src/ai/Combat.cpp:498`) is THE combat core, shared by the executor
  (`GameEngine.cpp:1610`) and the rollout (`TurnSolver.cpp`, `SimulateOneCombat`, reached from
  `SimulateCombat` at `TurnSolver.cpp:86582`). Every attacker's power goes to `players[1 - active].life`. The
  CR 510.4 two-step split (`ResolveCombatDamageTwoStep`, `Combat.cpp:274`) exists for first and double strike.
  Its comment states the premise that breaks here: "SBA between the steps: nothing blocks, so nothing dies".
* Attack choice: `DecisionProvider::AttackWith` (`DecisionProviders.cpp:9017`) gates
  `GenericProvider::ShouldAttackWith`, which returns true ("goldfish default: attack with everything that can
  attack (no blockers)", `:8656`). There is precedent for a searched attack HOLD: the Stuffy Doll step-9c fan
  (`TurnSolver.cpp:108018`) makes every subset of the provider's `MayDeclineAttack` creatures a plan variant,
  carried to combat as hold codes on `GameState::scripted_combat_reflect` and honoured by `AttackWith` in both
  worlds. The attack-count axis of section 4 has the same shape.
* Executor order inside combat (`GameEngine::CombatPhase`, `:1331`): begin combat, `DeclareAttackers`
  (`:1373`), then every declare-attackers trigger and pump (quest counters, Inferno Titan, Bruna's gather,
  transforms, firebreathing `m_ai.Firebreathe` at `:1527`, exalted, Raubahn, Skyhunter, Flinger), then
  `ResolveCombatDamage`, then SBA and end of combat. The blockers step goes AFTER the last declare-attackers
  trigger and BEFORE firebreathing (section 5), with the damage step after it.
* The opponent's creatures today: `GoldFishRunner::PopulateOpponentSpawns` (`GoldFishRunner.cpp:1032`), ten
  vanilla P/T patterns materialised at `GameEngine.cpp:709` on player 1's side, plus Forbidden Orchard Spirits,
  Hunted Phantasm's Goblins and Varchild's Survivors, gifted to player 1. None blocks.
* The damage-reflection pipeline (`src/core/DamageReflection.h`, `dmgrx::DealDamageBatch`) already makes
  "a creature is dealt damage" one event, with reflection triggers, LKI, SBA and termination. It is armed only
  for the Stuffy Doll deck (`reflect_armed`). Its disclosed limit is the one that matters here: damage-SOURCE
  keywords (lifelink, deathtouch) are not modelled.
* The prototype injection hook (commit `73694c1d0`, `src/core/ControlInject.h`, `GameEngine.cpp:416`,
  `BatchRunner.cpp:2617`) applies a schedule to the goldfishing deck. It is thread-local and is consulted only
  in the real game (`InRolloutForCtrlInject`). The blocker effect needs a GameState field instead, because the
  blocker is on the board and every search copy must see it (section 1.2).
* A game can be continued from a mid-game state: `GameEngine::PlayOutFrom(state, max_turns, ResumeAt)`
  (`GameEngine.h:51`), used by the bottoming trials (`AIEngine.cpp` ~2215-2260, which stash and restore the
  committed line and pins around a nested engine). The clairvoyant fork of section 3.2 reuses that route.

## 1. A rules-correct declare-blockers step

### 1.1 The rules it implements, and where the rules skill is out of date

Each rule below cites `.claude/skills/mtg-rules.md` where the skill covers it. CR numbers outside the skill come
from the Comprehensive Rules as amended by Magic Foundations. Where the skill and the current rules differ, the
current rules win, and the implementer must check every CR number against the current CR text before coding
(the CR download could not be fetched from this container; the Foundations change itself is confirmed by the
Wizards bulletin and the judges' release notes, see Sources).

**Out of date in the skill (owed fix, not made here):**
1. Skill, Combat > Blocking: "One blocker can block only one attacker (unless it has reach/flying
   interactions)". Wrong. Reach and flying are evasion rules; only an explicit "can block an additional
   creature" effect lets a creature block more than one. No card in the pool has one (`grep "block an
   additional|block any number"` over `cards.json`: 0 hits), so one blocker blocks exactly one attacker.
2. Skill, Build Guidance > Combat damage with trample + deathtouch: "distribute attacker.power among blockers
   (attacker chooses order, must assign lethal to each before moving on)". Also Code Review > Combat: "order of
   blocker assignment matters for trample damage distribution". Both describe damage assignment order, which
   Foundations removed. Today: a creature blocked by several creatures divides its combat damage among them as
   its controller chooses, with no lethal-first requirement (CR 510.1c). The assignment is made in the combat
   damage step, with no priority between assignment and damage. Trample keeps its requirement: lethal damage to
   EVERY blocker (1 each with deathtouch) before any goes to the player (CR 702.19b). The same free division
   applies to a creature blocking several attackers (CR 510.1d), which nothing in the pool can do.

**The step, rule by rule:**

| # | Rule | Source | Day one? |
|---|---|---|---|
| R1 | Blockers are declared as a group, as a turn-based action, by the defending player. | skill Code Review > Combat ("Blockers are declared as a group"); CR 509.1 | yes |
| R2 | Only an untapped creature the defending player controls can block. Summoning sickness does NOT restrict blocking (it restricts attacking and {T} only). | skill Combat > Blocking ("Any untapped creature may block"), Core > summoning sickness pattern; CR 509.1a, 302.6 | yes |
| R3 | Each blocker blocks exactly one attacker (no pool card grants more). Any number of blockers may block one attacker. | skill ("Multiple creatures can block one attacker"); CR 509.1a | yes |
| R4 | Restrictions: flying (blocked only by flying or reach), menace (needs two or more blockers), "can't be blocked", "can't be blocked by creatures with power 2 or less", "can't be blocked by more than one creature", landwalk, fear, protection (DEBT: can't be Blocked by that quality), "can't block". The declaration is legal only if every restriction holds; menace is checked on the whole declaration. | skill keyword tables (Flying, Reach, Menace, Protection DEBT, Fear, Landwalk); CR 509.1b | yes |
| R5 | Requirements ("all creatures able to block it do so": Taunting Elf, Indrik Umbra). The declaration must obey as many requirements as possible without breaking a restriction. | CR 509.1c (skill silent) | yes (Elves, Auras gauntlet decks) |
| R6 | An attacker with one or more blockers becomes BLOCKED, and stays blocked if every blocker leaves combat. A blocked attacker without trample whose blockers are all gone deals no combat damage. A trampler with no blockers left assigns all its damage to the player. | CR 509.1h, 506.4, 510.1c, 702.19e (skill silent) | yes |
| R7 | "Whenever ... blocks" and "becomes blocked" triggers go on the stack after the declaration, active player's first (APNAP). In the pool: Rampage 1 (Varchild's War-Riders), Elder Gargaroth's "attacks or blocks", Bruna's "attacks or blocks". | skill Triggered Abilities (Rampage, Bushido rows), Core > Stack; CR 509.3-509.4 | Rampage yes; the "blocks" halves only when the blocker is ours |
| R8 | Each attacking and blocking creature assigns combat damage equal to its power. An unblocked attacker assigns to the player. A blocking creature assigns to the attacker it blocks. A blocked attacker divides its damage among its blockers freely (no order). | skill Combat > Combat Damage; CR 510.1a-d (Foundations wording) | yes |
| R9 | Trample: lethal to every blocker first, then the excess to the player. "Lethal" accounts for damage already marked this turn (a first-strike step) and is 1 against a blocker when the attacker has deathtouch. | skill trample+deathtouch pattern and Code Review check; CR 702.19b-c | yes |
| R10 | All combat damage in one step is dealt simultaneously as ONE event. | skill Combat > Combat Damage ("Assigned simultaneously"); CR 510.2 | yes |
| R11 | First and double strike: two damage steps; a creature dealt lethal damage in the first step is destroyed by SBA before the regular step and deals no regular damage. Players get priority after each step. | skill Code Review > Combat ("two separate damage steps required"); CR 510.4, 704.3 | yes; the between-step SBA is NEW (today "nothing dies") |
| R12 | Deathtouch: any nonzero damage from it is lethal, and a creature so damaged is destroyed (SBA). | skill keyword table; CR 702.2b-c, 704.5h | yes |
| R13 | Lifelink applies to ALL damage the source deals, creatures included. One life-gain event per source per damage step. | skill keyword table ("damage dealt = life gained"); CR 702.15b, 119.10 | yes (today it reads only damage to the player) |
| R14 | Lethal damage destroys a creature unless indestructible; destruction can be replaced (umbra armor, regeneration). | skill SBA section, Indestructible and Umbra rows; CR 704.5g, 702.89 | indestructible and umbra armor yes; regeneration day two |
| R15 | Prevention: Iroas ("prevent all damage that would be dealt to attacking creatures you control"), Kami of False Hope (all combat damage this turn), Questing Beast ("can't be prevented"), protection's damage half. | skill Replacement Effects; CR 615, 702.16 | yes |
| R16 | "Deals combat damage to a player" triggers fire only for damage actually dealt to the player (unblocked or trampled through), never for damage to a blocker. "Deals combat damage to a creature" (Stinkweed Imp) fires for damage to a blocker. | CR 510.3 / 603 (skill Triggered Abilities) | yes |
| R17 | Damage marked on a creature wears off in the cleanup step of EVERY turn, both players' creatures. | skill Pitfalls ("cleanup step resets damage"); CR 514.2 | yes (verify the reset covers player 1's permanents) |
| R18 | Reflectors: "whenever this creature is dealt damage" triggers once per damage event for the total (the Reckoner/Nemesis/Sliver rulings already implemented in `dmgrx`). Combat damage dealt TO a reflector by our blocker reflects to us at the blocker's full power. | `DamageReflection.h` header (CR 120.3) | yes (Stuffy Doll deck) |
| R19 | Tapped-by-an-effect blockers untap in their controller's untap step; the passive opponent's compressed turn must untap player 1's creatures. | CR 502.3 | yes (Hinata's Icy Blast taps opposing creatures) |

Banding, bushido, flanking, afflict, provoke and "can block additional creatures" have no card in the pool; the
step rejects them by an assert rather than modelling them.

### 1.2 Data model

All of it lives in `GameState`, because the trick root (section 5) is a search root in the MIDDLE of a combat and
its copies must carry the declaration.

```
// GameState (src/core/GameState.h)
bool blocking_armed = false;   // stamped per game; in the existing bool padding (sizeof(GameState) unchanged)
struct BlockPair { int blocker_number; int attacker_number; };   // Card::m_number of each (stable ids)
std::vector<BlockPair> combat_blocks;     // this combat's declaration; cleared at end of combat and both turn starts
std::uint64_t combat_blocked_mask = 0;    // bit per atk_idx slot: "became blocked" (R6), survives blocker removal
std::vector<int> combat_damage_split;     // the attacker's searched division (section 1.4); empty = canonical
```

* Ids are `m_number`, never battlefield indices: a first-strike death or a reflection trigger can remove a
  permanent between steps and shift indices (the same reason `RefindCombatSources` exists).
* `combat_blocks` is empty in every unarmed game, so a copy costs one empty vector (24 bytes, no allocation).
  The implementer measures `sizeof(GameState)` and copy throughput before and after (the repo tracks both).
* Our creatures in the replay are ordinary `Permanent`s on player 1's battlefield with a real `CardDefinition`
  (keywords, colours, abilities from `cards.json`) or a spec-built token (`P/T`, keyword list, colours). They
  get `m_number` from `next_opp_spawn_number` and are NOT marked as spawns.
* Every search-state key that folds the opponent's board (`dominance::Build`, `BuildSimKey`, the TT key) must
  include, in an armed game, each opposing creature's tapped state, marked damage, P/T modifiers and
  `combat_blocks`. Today those keys can treat spawns as interchangeable vanilla bodies; the audit is part of
  step S7.

### 1.3 Where the step goes

Executor (`GameEngine::CombatPhase`), after every declare-attackers trigger has resolved and before the
damage:

```
DeclareAttackers ... (unchanged) ... FireAttackSacFling
if (state.blocking_armed && !atk_idx.empty()) {
    state.step = Step::DeclareBlockers;
    DeclareBlockersReal(state, atk_idx);          // section 3: scored fork, or the rule
    FireBlockTriggers(state, atk_idx);            // R7, then ResolveStack
    CombatTrickRoot(state, atk_idx);              // section 5: searched, "do nothing" included
}
m_ai.Firebreathe(state, atk_idx);                 // unarmed games only; armed: inside the trick root
... exalted ... ResolveCombatDamage(state, atk_idx, ..., blocks)
```

Rollout (`SimulateOneCombat`), at the same point, lockstep: `if (state.blocking_armed)
PredictBlocks(state, atk_idx)` (the rule, section 3.3), then the playout's trick policy (section 6), then the
same `ResolveCombatDamage`. A shared function `DeclareBlockersShared(GameState&, const std::vector<int>&
atk_idx, BlockSource)` holds legality, the triggers and the record, so the two worlds cannot drift.

`ResolveCombatDamage` gains a blocks-aware path. With `combat_blocks` empty it runs today's code unchanged (the
single-step loop and `ResolveCombatDamageTwoStep`, byte for byte). With blocks it runs a new
`ResolveCombatDamageBlocked` that:
1. snapshots first/double-strike membership for attackers AND blockers (R11);
2. per step, builds ONE damage event: each attacker's assignment (unblocked to the player; blocked by the split
   of section 1.4; trample per R9), each blocker's damage to its attacker; prevention applied (R15);
3. applies it through `dmgrx::DealDamageBatch` extended with source keywords (deathtouch marks the recipient;
   lifelink queues one gain per source, R13), so reflection, LKI and the SBA pass are the existing ones. The
   pipeline is armed for this event regardless of `reflect_armed` (it is only reached when
   `blocking_armed`);
4. fires the combat-damage triggers off PLAYER damage only (R16), using the existing helpers
   (`FireCombatDamageTeamCounters`, `...CheatIntoPlay`, `...Tokens`, `...Dig`, `...Proliferate`, Neheb,
   draw grants, Prophets, Guilty Conscience) over the connectors that hit the player;
5. runs SBA between the steps (R11) and re-finds attackers by `m_number`.

Gating, so goldfish, 2HG and the viewer stay byte-identical:
* `blocking_armed` is set ONLY by the batch runner, from a job whose injection schedule contains a creature
  effect, for player 1. Nothing else writes it. The viewer (`tools/play/server.js`, `--claude-play`) never
  passes the field, so it never arms.
* A job that carries both a creature effect and `opponent_heads > 1` is REFUSED at parse time (Q7: 2HG is out
  of scope), so no 2HG path ever runs armed.
* Reference replays (`--force-attackers`, `viewer_protocol_check.py`) never arm.
* Every new branch begins with `if (!state.blocking_armed)` returning to today's code. A unit test counts
  entries into every new function over a smoke run with no blocker job: all zero.

### 1.4 The attacker's damage division

When an attacker is blocked by two or more creatures (menace, a double block), its controller chooses the
division (R8). That is the gauntlet deck's decision, so inside its search window it is searched, not picked:
* **Sound fold:** only divisions that are lethal-exact on a subset S of the blockers (ties broken by
  battlefield order), with the remainder on one blocker, are distinct. Assigning more than lethal to one
  blocker while another gets less than lethal is dominated by moving the excess (it changes nothing else in the
  pool: no "damage dealt" payoff reads the amount on a creature except reflectors, where MORE damage reflected
  hurts the attacker's own side only through Reckoner-style "any target"; that case is kept unfolded).
* The variants are the subsets S (at most 2^b, b <= 3 in practice). Beyond the horizon the playout kills the
  largest subset it can, ordered by the blockers' power.
* With one blocker there is no choice. Trample keeps the R9 minimum on each blocker and sends the rest to the
  player (more to the player is dominant when no reflector blocks).

## 2. Card-pool inventory

Method: every card named in the 67 `decks/*/` lists (`.txt` and `.cod` main zones), looked up in
`src/cards/data/cards.json`, scanned for combat keywords and triggers in `keywords`, `oracle_text` and
`parameters`. The raw regex scan finds 356 combat-relevant card names (it is noisy: card notes mention
keywords they deny). The sharper count is the second one:

**195 cards whose card note rests on the opponent never blocking or attacking** (phrases "never blocks", "no
blocker path", "inert vs the passive", ...). Grouped by what blocking breaks:

| group | cards | what changes in an armed game |
|---|---|---|
| A. evasion / combat keyword parsed but unread | 112 | must be READ by the new legality and damage code |
| B. our-side survival / "we never block" | 22 | only matters if such a creature is OUR scheduled blocker |
| C. a collapse whose proof is "opposing creatures never block or get buffs" | 7 | the collapse is no longer exact |
| D. a provider prune that refuses to target opposing creatures ("never offered", "no effect") | 16 | the prune hides the gauntlet's answer to our blocker: a completeness defect |
| E. other notes (planeswalkers never attacked, lifegain irrelevant, ...) | 38 | case by case; most stay inert |

### 2.1 What the step must handle on day one

Day one = what a gauntlet deck in the 66 goldfish decks does against one scheduled creature of ours.

**Evasion of the attacker (R4).** Today `Keyword::Flying`, `Trample` and `Menace` have no combat reader, and
most GRANTS of them are not modelled at all (no `grants_flying` / `grants_trample` / `grants_menace` lord
param exists). New single oracles in `SpellEffects.h`, in the shape of `CreatureHasFirstStrike` (printed,
lord, aura, equipment, temporary, chosen):
* `CreatureHasFlying`: 62 printed in the pool. Grants to model: Galerider Sliver and Cloudshredder Sliver
  (Slivers), Gryff's Boon and Arcanum Wings (Auras, Bruna), Serra Ascendant's 30-life flying and Voice of the
  Blessed's (Critter / Heliod), Figure of Destiny's top level, Colossus Hammer's "loses flying",
  Lingering Souls / Hornet Queen / Utvara tokens. (The raw scan also flags Accorder's and Cathar's Shield;
  each implementer check reads the card's own text, since the scan is noisy.)
* `CreatureHasReach`: matters for OUR blockers only (3 printed in the pool; Elder Gargaroth, Annoyed Altisaur,
  Twinshot Sniper, Spider Umbra, Spidersilk Net).
* `CreatureHasTrample`: 39 printed plus grants (Rancor, Berserk, Temur Battle Rage, Fists of Flame, Ancestral
  Anger, Armadillo Cloak, Shadowspear, Loxodon Warhammer, Craterhoof, Prodigious Growth, Mythic Proportions,
  Raggadragga's grant, `tap_target_pump_grants_trample`, `cast_trigger_team_pump_trample`). Each card's note
  today says "trample inert: not modelled".
* `CreatureHasMenace`: Vampire Socialite, Two-Headed Hellkite, Migloz (`vigilance_menace_cost`), Iroas
  (creatures you control have menace), Lord of the Accursed and Sethron's activated grants.
* `CantBeBlockedBy(attacker, blocker, defender)`: "can't be blocked" (Hunted Phantasm, `tap_target_pump_grants_
  unblockable`: The Black Gate, Rogue's Passage, Merfolk Sovereign), "power 2 or less" (Questing Beast,
  Brushfire Elemental), "more than one creature" (Alpha Authority), landwalk (Lord of Atlantis, Master of the
  Pearl Trident, Stonybrook Banneret, Rishadan Dockhand: islandwalk; Goblin King: mountainwalk; Street
  Wraith: swampwalk), "can't be blocked except by" a type (Serpent of Yawning Depths), fear (Dimir House Guard, Shriekmaw), protection (Progenitus,
  Spirit Mantle: from creatures; Auriok Champion: black and red; Mother of Runes' activated grant).
* Requirements (R5): Taunting Elf (Elves), Indrik Umbra (Auras).

**Damage keywords (R9-R15).** Deathtouch: Questing Beast, Hornet Queen, Vampire of the Dire Moon, Severance
Priest, Pharika's tokens, Death Baron's grant (Zombies), Ice-Fang Coatl (snow-conditional), Contested Cliffs'
fight. First strike: 28 cards, already read by `CreatureHasFirstStrike` (keep), plus the two disclosed gaps
that now matter: Boros Reckoner's and Slaughter-Priest's "{R/W}: gains first strike" (trick root). Double
strike: 22, already read. Lifelink: 37 (extend to creature damage). Indestructible: 43 hits, of which the
combat-relevant are Stuffy Doll, the Gods, Knight Exemplar's grant, Ulamog, Steelbane Hydra, Marit Lage.
Umbra armor: Hyena, Lion, Spider, Crab Umbra. Prevention: Iroas, Kami of False Hope, Questing Beast's
can't-be-prevented, protection.

**Combat triggers.** Rampage (Varchild's War-Riders, Creature Giving). Stinkweed Imp's "deals combat damage to
a creature, destroy it" (Gods). The 13 "deals combat damage to a player" cards (Drana, Garruk's Harbinger,
Goblin Lackey, Neheb, Shroofus, Wanderwine Prophets, Maelstrom Archangel, Bloated Contaminator, Spawning
Kraken, Koma, Millicent, Hystrodon, Questing Beast) must fire only off player damage (R16). The 38 attack
triggers are unchanged (they fire at declaration).

**Reflectors (R18).** Stuffy Doll (indestructible, reflects to the chosen player = us), Mogg Maniac (target
opponent), Boros Reckoner (any target), Screaming Nemesis (any other target, plus the rest-of-game
"can't gain life" lockout, `reflect_player_cant_gain_life`), Spiteful Sliver's granted reflection
(`grants_reflect_to_subtype`). Apex Altisaur's fight loop also reads "is dealt damage". All already in the
`dmgrx` pipeline; combat damage from our blocker must enter it as a creature-damage event.

**Instant-speed tricks for the trick root (section 5).** Pumps: Temur Battle Rage, Berserk, Might of Old Krosa,
Vines of Vastwood, Fists of Flame, Ancestral Anger, Fortifying Draught, Gold Rush, Legion Leadership,
Invigorate, Boros Charm, Feed the Clan, Heroes' Reunion (life gain), firebreathing and team pumps
(`team_pump_cost` 7 cards, `tap_target_pump_cost` 9). Removal of our blocker: Lightning Bolt, Searing Blood,
Searing Blaze, Skred, Swords to Plowshares, Path to Exile, Fatal Push, Drown in the Loch, Icy Blast (tap),
Magnetic Theft. Protection and regeneration: Mother of Runes, Vines of Vastwood, Golgari Grave-Troll, Krosan
Warchief, Ezuri, Deathbellow Raider (regeneration is day two). Prevention: Kami of False Hope.

**Group D, the 16 prunes to gate on `!blocking_armed` (completeness).** Basilisk Gate, Brutal Cathar, Crab
Umbra, General Kudro of Drannith, Haytham Kenway, Icy Blast, Krosan Warchief, Magnetic Theft, Oko, Path to
Exile, Pyrohemia, Seedcradle Witch, Sigarda's Aid, Skyclave Apparition, Staff of Domination, Whelming Wave.
Each says, in its own words, that removing or tapping an opposing creature "changes no outcome" because spawns
never block. In an armed game the gauntlet deck's removal on our blocker is a real line, and a prune that hides
it over-credits the blocker. The premise is now false, so the prune is gated off in armed games (the repo rule
for a prune with a stated premise: when the premise fails, the prune does not apply). Skred, Searing Blood and
`FindBurnKillTarget`-style target pickers (group E) get the same audit.

**Group C, the 7 collapses.** Massacre Wurm ("-2/-2 until end of turn collapsed to destroy each creature with
toughness <= 2": a surviving 4/4 blocker is a 2/2 for that turn's combat, and each of our creatures dying costs
us 2 life, which matters for the Creature Giving body-value ruling), Inferno Titan, Contested Cliffs,
Falkenrath Pit Fighter, Haunted Fengraf, Myriad Landscape, Steelbane Hydra. Each gets the exact form in armed
games; the collapse stays in unarmed games.

**Group B (only if the scheduled blocker is such a card):** Bloodghast, Carrion Feeder, Gravecrawler (can't
block); Kami of False Hope, umbra armor, indestructible grants on our side. The harness validates a named
blocker against this list and refuses a creature with "can't block".

**Day two (disclosed until built):** regeneration (Golgari Grave-Troll, Krosan Warchief, Ezuri, Deathbellow
Raider); the "blocks" halves of Bruna and Elder Gargaroth when the blocker is ours; the after-first-strike
priority window; Mother of Runes' protection as a trick (static protection is day one).

## 3. The defender's choice (our creatures in the gauntlet replay)

### 3.1 Legal assignments

`EnumerateBlocks(state, atk_idx)`: each untapped creature of ours (R2) gets "no block" or one attacker it may
legally block (R4); the declaration is filtered for menace and "no more than one" on the whole set, and for
requirements (R5: among the legal declarations, keep only those that satisfy the most requirements). Sound
folds, both identity folds:
* **F1, interchangeable blockers:** our creatures with the same name, counters, damage, pumps and attachments
  are one class; a declaration is a multiset of (class, attacker) pairs.
* **F2, interchangeable attackers:** the same identity test on attackers (`BoardIdentity.h`, which already
  folds identical permanents), so blocking either of two identical 1/1 tokens is one option.

Size: in a control-mode replay we schedule one to three creatures. With b blockers and c attacker classes the
count is at most (c+1)^b: 5 for b = 1 and c = 4, 125 for b = 3.

### 3.2 The scored (clairvoyant) variant: fork the replay one combat deep

The USER's Q3/Q8 rulings make the scored target "the one that delays the gauntlet deck most, found by trying
each candidate in the replay". For blocks:

1. At each declare-blockers step of the REAL replay game, enumerate the declarations (3.1).
2. For each declaration, copy the state, pin the declaration (`scripted_blocks`), and play the game out with
   `PlayOutFrom(copy, max_turns, ResumeAt::Combat)` on a nested engine, the bottoming-trial route (stash the
   committed line and pins, isolate the logger). The attack is pinned too (the declared attacker set, as
   `--force-attackers` does), so the fork re-runs exactly this combat with a different block. Later combats
   inside the fork use the RULE (3.3), never another fork, so the cost is one level deep.
3. Score = the fork's kill round (unwon = horizon + 1). Keep the maximum. Ties go to the declaration the rule
   would make, then to enumeration order (deterministic).
4. **Sound cut:** stop enumerating once a fork reaches the horizon (horizon + 1 cannot be beaten).
5. **The real game BECOMES the winning fork** (its final state and kill round are the game's result). This
   avoids relying on a re-search reproducing the fork, which a per-game transposition table warmed by the other
   forks need not do.

Cost: (number of blocked combats) x (declarations) x (rest of the game). With one creature against a cheap
gauntlet deck (0.1-0.7 s a game, `test/suite_cost.json`) and two to four blocked combats, about 10-20 partial
games per replay. Mapping keys are measured offline only (control-scoring-mode.md section 7.5), so this is paid
once per key, never per scored control game.

Why one combat deep, not the full tree: the exact clairvoyant optimum is a tree over every combat (declarations
to the power of the number of combats). One deep is a policy improvement over the rule (never worse than the
rule's own play, by construction) and is exact when only one combat matters. Section 8, Q-B2.

### 3.3 The non-clairvoyant rule (`PredictBlocks`)

A deterministic function of the PUBLIC state: the attackers (with the section 2 oracles), our untapped
creatures, our life, the gauntlet deck's untapped mana and hand size, plus three offline per-gauntlet-deck tables.
It is used in four places: inside the gauntlet's search (predicting us), in the rollout, as the reported
non-clairvoyant variant's actual blocks, and for the later combats inside a fork.

Inputs measured offline (stage 1 vocabulary, beside the Q8 trouble values):
* `trouble[g][card]`: delay of deck g's kill from removing that card (Q8). Prices "what is a priority to kill".
* `body[g]`: delay of deck g's kill from removing ONE of our creatures. Negative for decks that use our
  creatures (USER 2026-11-27: Hinata's targets, Stuffy Doll's Blasphemous Act discount and Chain Reaction
  scaling, Creature Giving's Massacre Wurm drain).
* `trick[g][round]`: how often deck g holds an instant-speed combat trick with the mana up at that round.
Until the tables exist, all three are 0 (body value 0 = "hold the chump", the USER's default for most decks).

The rule, in order. Each blocker takes at most one attacker; menace needs two blockers.

1. **Lethal turn** ("Particularly on the turn where you would die otherwise"): if the damage that gets through
   with no blocks is at least our life, choose the declaration that minimises the damage through (an exact
   assignment over the small set; damage stopped per blocker = the attacker's full power if it lacks trample,
   else `min(power, lethal-to-blocker)`, which is 1 against deathtouch). Ties: prefer the declaration that also
   kills the most trouble. This is the chump and soak case.
2. **Killing blocks that survive:** a blocker that kills an attacker and survives it (first strike, toughness,
   deathtouch on our side) blocks the attacker with the highest `trouble`.
3. **Trades:** a blocker that kills its attacker and dies blocks when `trouble[attacker] >= body[g]` ("block when
   the trade is even or better", priced by measurement). With negative `body[g]`, trade freely.
4. **Soaks:** a blocker that kills nothing and survives (a wall) blocks every turn, on the largest remaining
   attacker WITHOUT trample (it stops all of that damage); a trampler only loses the blocker's toughness (1 with
   deathtouch). USER: "if you happen to have a smaller creature that can't kill anything you would want to soak
   up as much (preferably non-trample) damage as possible."
5. **Chumps** (dies, kills nothing): only on the lethal turn (step 1), or when `body[g] < 0` (spend the body).
6. **Reflectors** (Stuffy Doll, Mogg Maniac, Boros Reckoner, Screaming Nemesis, a Spiteful Sliver): the reflected
   damage is our blocker's FULL power and counts as damage to us.
   * soak: block when the attacker's damage stopped exceeds the power we hit it with ("you would rather take 1
     than 3");
   * kill: the smallest-power blocker that is just lethal, when `trouble[reflector]` is worth the reflected hit;
     an indestructible reflector (Stuffy Doll) can only be soaked;
   * Screaming Nemesis: dealing it damage also costs the rest-of-game life-gain lockout, priced as the mapping's
     credited delay of the life gain still in our schedule. A 0-power blocker deals no damage and triggers
     nothing, so it is the preferred soaker.
7. **Trick risk:** a block whose value needs our creature to survive is discounted by `trick[g][round]` when
   the gauntlet deck has the mana up and cards in hand.
8. Remaining ties: battlefield order. Deterministic, so the executor and the rollout agree.

This rule is the OPPONENT's policy as the gauntlet's search sees it (control-scoring-mode.md Q10: "This is the
opponent's behaviour as seen from the attacker's search, not a greedy pick for the searching side"), the same
status as the passive opponent's land-drop rule. It engages no `GreedyPermit`.

### 3.4 Reported variants

Every blocker key reports three numbers per gauntlet deck: the scored fork (3.2), the rule (3.3), and the
attack-all upper bound (the gauntlet attacks with everything, we block by the rule: Q10 (a)). A large gap
between scored and rule is the clairvoyance premium; a large gap between attack-all and the searched attack is
the over-credit Q10 exists to remove.

## 4. The attacker's choice (the gauntlet deck), per Q10

The USER (Q10): "We would want some heuristics and some search." A provider decides the obvious attacks; the
search branches on the rest.

### 4.1 Sound provider prunes

* **P1, a creature that cannot die in this combat attacks.** It cannot die when no untapped creature of ours can
  legally block it (section 2 oracles), or when no legal block (including a double block, summing powers,
  counting first strike, deathtouch, indestructible, Iroas's prevention and the blockers' own trick-free
  P/T) can deal it lethal damage. "A blocker that kills nothing threatens nothing" (USER): a 0-power wall never
  makes an attacker stay home. The existing exceptions still apply (`AttackWith`'s tap-ability and mana holds,
  must-attack, the Stuffy step-9c hold).
  * Why it is sound in the replay: our creatures never attack the gauntlet deck (our damage lives in our own
    game, W), so a creature held back has no defensive value, and an attacker that cannot die loses nothing.
  * **Counterexample class, kept searched:** a gauntlet deck that VALUES our creatures (Stuffy Doll's
    Blasphemous Act discount and Chain Reaction, Hinata's targets) can lose by attacking if our rule chumps and
    our creature dies. For a deck whose list has `cost_less_per_creature_on_battlefield`,
    `damage_all_creatures_count_creatures` or `hinata_cost_reducer`, P1 does not apply and the attack is
    searched. Section 8, Q-B6.
  * If the schedule has an effect of ours that acts during the gauntlet's combat (a fog, an instant removal),
    P1's "cannot die" must include it, or P1 is suspended for that combat.
* **P2, interchangeable attackers fold into counts.** F2's identity classes. "Send 2 of the 4 1/1s" is one
  option, not six.

### 4.2 The searched part: counts per at-risk class

The at-risk attackers (eligible, would attack by the provider, not P1) are grouped into identity classes
`c_1..c_m` with sizes `n_1..n_m`. The plan variants are every count vector `(k_1..k_m)`, `0 <= k_i <= n_i`,
including all zeros ("attack with none of them" is always an option). That is `prod (n_i + 1)` variants; with
most boards one or two classes. Carried exactly like the step-9c hold: a combat suffix of hold codes on the
plan (`AttackHoldCode(m_number)` for each held copy, the highest battlefield index of a class held first so the
fold is canonical), honoured by `AttackWith` in the executor and the rollout alike. The fan is built in
`TurnSolver::AppendSubdecisionAxes` beside the step-9c fan, for pre-combat top-level lists, and only when
`blocking_armed` and at least one at-risk attacker exists. Beyond the horizon nothing fans: the playout uses
the section 6 policy.

What the search weighs (USER: "it is the question of what you lose vs the opponent in terms of resources or
health"): inside the window it is exact, because the gauntlet deck's objective is its kill round and combat now
moves our life, its attackers and our blockers. The hold-back "until your board is larger or you have evasion
or creature combat is more favourable" comes out of the search, since depth counts rounds and waiting a turn for
a lethal swing is found whenever it kills sooner.

### 4.3 The USER's one-sided-loss rule (heuristic prune, default OFF until measured)

USER (Q10): "If you can attack and lose your attacker, but the blocker goes down it might be worth it.
Attacking in when only you lose it worth it only when you can get through enough damage to either kill the
opponent or put them on the ropes for next turn." and "The question is whether they outnumber the blockers that
would kill them and by how much."

As a prune on the count variants, evaluated against `PredictBlocks` for the WHOLE attack:
* Let `b` = our untapped blockers that can kill an attacker of the class. At most `b` of the `k` sent die.
* A trade (our block kills the attacker AND the blocker dies) stays searched.
* A count `k` whose losses are one-sided (attackers die, blockers survive) is pruned UNLESS the damage of the
  `k - b` that get through, plus the P1 group's damage, is lethal now, or leaves us dead to next turn's attack
  of the same board ("on the ropes": our life after this combat <= that board's unblocked power next turn,
  using the rule's predicted blocks).
* Mixed boards: the rule applies per attacker against the whole attack ("if you have one creature that is safe
  and another that would just die uselessly you would only attack with the safe creature").

This is a HEURISTIC prune (unlike P1 and P2). It ships behind `MTG_CTRL_ATK_ONESIDED_PRUNE` (`EnvOn`, default
OFF) and is checked against the unpruned search at a large budget on a held-out sample of armed replays; a
replay it makes worse is a counterexample, and the fix is to the rule's ranking. Its default is the USER's call
on that measurement (CLAUDE.md: a heuristic prune's default is user-owned). Until then the count axis is
searched in full.

### 4.4 How the attacker's lookahead predicts our blocks

Everywhere the gauntlet's search evaluates a combat (the plan's apply plus `SimulateCombat`, every
`PendingAttackDamage` projection, the dominance folds that say "a combat line that kills cannot be improved"), it
calls `PredictBlocks` on that state. The 40 `PendingAttackDamage` sites in `TurnSolver.cpp` and the 16
`CombatPowerOf` sites are audited in step S7: each either becomes blocker-aware (subtract the damage the rule
stops) or is disabled when `blocking_armed`. A projection that keeps assuming every point reaches us would
re-create "attacking is free" inside the search. The replay itself then uses the scored fork (3.2); the
mismatch between what the attacker predicts and what we do is stated, not hidden.

## 5. The mid-combat trick root

After blockers are declared (CR 509: the active player then gets priority), the gauntlet deck may cast an
instant or activate an ability: pump an attacker, remove or tap our blocker, grant protection, regenerate,
prevent. Today no goldfish deck has this window, because nothing ever blocks. The USER: "This is a new
mid-combat search root for the attacker, and by the hard rule it must offer 'do nothing' there like every other
root."

* **Root kind `CombatAfterBlocks`**, solved by `TurnSolver` like any root. Its options are every instant-speed
  action legal now (`CollectActions` filtered to instants, flash, and non-sorcery-speed activations, with the
  current `combat_blocks` visible), each followed by the rest of the combat (damage with the blocks), main 2
  and the normal lookahead. **"Do nothing" (proceed to damage) is always the first option, and no prune, fold or
  provider hook may remove it** (CLAUDE.md, "DO NOTHING MORE IS ALWAYS AN OPTION").
* Multiple tricks: the root re-solves after each resolved action (the breakpoint shape), so "Bolt the blocker,
  then pump" is two steps, and "do nothing more" is offered after each.
* **Firebreathing moves here in armed games.** `m_ai.Firebreathe` spends all leftover mana on attacker pumps
  before damage, on the premise that unspent combat mana is lost and more damage is always better. Against
  blockers that premise is false (pumping the wrong attacker, or pumping before seeing blocks). In an armed
  game the activation count per firebreathing source becomes a searched option of this root (`k = 0..max`, the
  `MTG_FB_HOLD_SEARCHED` precedent), and the pre-damage sink is skipped. Unarmed games keep it unchanged.
* **Before blocks, too.** A pump in the declare-attackers step can deter a block, which is a legal line a large
  budget must be able to find (completeness). A second root, `CombatBeforeBlocks`, with the same "do nothing"
  rule, follows the post-block root in the build order (step S10b).
* **Our side's scheduled combat effects** (a fog, a removal on an attacker, a flash blocker, an exile with
  Parallax Wave) apply at a fixed point: after the attacker's post-block root has passed (we act last, the
  strongest real timing for a defender). Section 8, Q-B4.
* **Inside the window, future combats** carry the trick choice as a plan axis (the same combat suffix as the
  attack counts); beyond the horizon the playout casts a held pump only when it changes who dies in that combat
  or makes it lethal (the existing `GreedyHeldTrick` notion), which is the permitted playout greed.
* **Our blocks allow for tricks** in the scored fork automatically (the fork plays the gauntlet's root, so a
  block that loses to a pump measures worse) and in the rule through `trick[g][round]` (3.3 step 7).

## 6. The playout's combat with blockers, and the leafless fallback

* **The playout must block.** The rollout beyond the horizon (`SimulateOneCombat`, the d0 runner, the bottoming
  trials) runs `PredictBlocks` and the blocks-aware `ResolveCombatDamage` whenever `blocking_armed`, or it values
  every attack as free again (Q10: "The playout must block").
* **The playout's attack policy** (greedy is permitted beyond the horizon): P1 creatures attack; for each at-risk
  class, send the count the USER's rule (4.3) prefers: trades yes; one-sided losses only when lethal or on the
  ropes; otherwise hold. This is the rule used as a policy, not as a prune, so no held-out check is needed for it
  beyond the stage-1 measurements.
* **The playout's tricks:** as section 5's last bullet.
* **Leafless from the blocker's arrival.** Gauntlet value leaves (`<deck>.value.json`, 55 decks) and keep
  tables were fitted to blocker-free play and misprice exactly the attack-versus-loss exchange (cf.
  "turn-to-win does not price resources"). From the round our first creature enters the replay (not before:
  the prefix is the baseline game, which keeps prefix sharing valid), the gauntlet runs with
  `m_value_model = nullptr` and `m_evaluator = nullptr`, i.e. rollout beyond the horizon. The keep table is
  unaffected: the mulligan is over before the blocker exists, and the search is not told in advance.
* **Cost, measured in stage 1:** ms per replay armed vs unarmed, per gauntlet deck, at the mapping's settings.
  The value-leaf skill records that a missing model can cost 1.35-84.8x through the H-cell ladder, so this is
  the number most likely to bind. Fallbacks, in the USER's hands on those numbers: (a) refit blocker-aware leaves
  on armed replays (a generation per deck, serial, on a frozen commit); (b) keep the leaf with a blocker
  correction term (lossy; a quality A/B would be owed). Default: leafless, and report the cost.

## 7. Implementation plan

Each step lands alone, with its tests, and passes the byte-identity check: **with no blocker effect in any job,
smoke play-changed = 0 (every digest equal to `test/regression_gt.txt`), `test/viewer_protocol_check.py` clean,
2HG smoke keys unchanged, unit tests green.** Steps that touch the shared combat core (S4, S7) also run the
regression tier with `MTG_PGO=0` on both arms. Validate under the push queue rules; subagents do not push.

| step | content | tests |
|---|---|---|
| S1 | `blocking_armed`, `combat_blocks`, `combat_blocked_mask`, `combat_damage_split` in `GameState`; the creature effect in the injection harness (below); parse-time refusal with `opponent_heads > 1`; untap of player 1's creatures in the compressed opponent turn (R19); damage cleared at every cleanup for both sides (R17). | unit: an armed job materialises the creature on player 1 at round t, with its definition; an unarmed smoke run enters no new function (entry counters all 0); `sizeof(GameState)` unchanged or recorded. |
| S2 | Combat keyword oracles: `CreatureHasFlying`, `...Reach`, `...Trample`, `...Menace`, `CantBeBlockedBy`, protection, landwalk, fear, with every grant shape in section 2.1. Readers reached only when armed. | unit: one test per grant shape (lord, aura, equipment, temporary, conditional: Serra Ascendant at 30 life, Ice-Fang snow count). |
| S3 | `EnumerateBlocks` (R1-R5): restrictions, menace on the whole declaration, Alpha Authority, requirements maximised (Taunting Elf), F1/F2 folds. | unit: flying vs reach; a single blocker on menace illegal; Questing Beast vs power 2; landwalk on/off by the defender's lands; protection from creatures; Taunting Elf forces every able blocker; fold counts. |
| S4 | `ResolveCombatDamageBlocked` (R6, R8-R16, R18): the damage event through `dmgrx::DealDamageBatch` with deathtouch and lifelink sources, first/double strike with SBA between steps, trample minimums with marked damage, indestructible, umbra armor, Iroas, Kami, player-damage-only triggers, reflectors to us. | scenarios (`test/scenarios/*.json`): 3/3 blocks 2/2; first-striker kills before regular damage; trample+deathtouch assigns 1; double block division; Stuffy Doll blocked by a 3/3 reflects 3 to us; Nemesis lockout set; a blocked Neheb does not discard; blocked-then-blocker-removed attacker deals nothing, a trampler deals all. |
| S5 | R7 triggers: Rampage; Stinkweed Imp; the "blocks" halves when ours (day two). | scenario: War-Riders double-blocked gets +1/+1. |
| S6 | `PredictBlocks` (3.3), tables defaulting to 0. | unit, one per USER ruling: soak the biggest non-trampler; chump only on the lethal turn; trade when trouble >= body; trade freely with negative body; wall soaks every turn; reflector soak vs kill; 0-power soak on Nemesis; indestructible reflector never "killed"; determinism. |
| S7 | The executor's step (1.3), the rollout lockstep, the `PendingAttackDamage` / `CombatPowerOf` / combat-kill dominance audit (4.4), the search-key audit (1.2). | lockstep unit: executor combat == rollout combat on scripted armed boards; a dominance fold never merges two boards that differ only in our blocker's damage or tapped state. |
| S8 | Attacker side: P1, P2, the count axis (4.2), `MTG_CTRL_ATK_ONESIDED_PRUNE` (default OFF). | unit: a flier with no reach blocker is in P1; a 1/1 into a 0/4 wall is in P1 ("threatens nothing"); 4 identical 1/1s give 5 variants; "attack with none" present; the body-valuing exception keeps Stuffy's attack searched. Counterexample harness: pruned vs unpruned at `--budget-ms 0` on a held-out armed sample. |
| S9 | Group D prunes and group C collapses gated on `!blocking_armed`; Massacre Wurm's real -2/-2 in armed games. | unit per card: Skred / Path / Icy Blast target our blocker in an armed game; unarmed games byte-identical. |
| S10 | `CombatAfterBlocks` root (5), with firebreathing inside it when armed. S10b: `CombatBeforeBlocks`. | unit: the root's option list always contains "do nothing" first, including after a resolved trick; scenario: Giant Growth-style pump wins the combat; Bolt the blocker then attack-through; firebreathing count 0 chosen when the blocker would kill anyway. |
| S11 | The scored fork (3.2): nested engine, pinned attack and block, the horizon cut, "the real game becomes the winning fork"; the three reported variants (3.4). | unit: the fork's result equals the game's reported kill round; identical results at 1 and N threads; the rule variant equals a fork-free run. |
| S12 | Leafless from the blocker's arrival (6). | unit: before round t the replay is byte-identical to the baseline game; stage-1 cost table (ms per replay, armed vs unarmed, per gauntlet deck). |
| S13 | Offline tables `body[g]`, `trick[g][round]` (and Q8's `trouble[g][card]`), built on the fixed baseline seeds (control-scoring-mode.md 7.5). | the table records its gauntlet (deck list, decklist hashes, play digests) and is regenerated when any changes. |

**Plugging into the injection harness.** The grammar is being extended by another agent and documented in
`docs/design/control-inject-harness.md` (not yet written); this design needs only the semantics:
* **creature, from round t:** put a creature on OUR side (player 1) at the start of the gauntlet's round t, as
  an opponent's sorcery-speed cast on its preceding turn (the prototype's timing), or before the gauntlet's
  combat of round t for a flash creature. Identified by a card name from `cards.json` (keywords, colours,
  abilities from the definition) or by a spec `P/T[+keywords][+colours]` for probes (colourless token).
  Optional leave round (a creature we sacrifice or that our own sweeper kills in our game).
* **becomes a creature, from round t:** Enchanted Evening's case: under Opalescence our enchantments are
  creatures (Parallax Wave a 4/4, Opalescence's partners at their mana value), so they block. Same semantics,
  naming an existing scheduled permanent.
* Arming: any such effect sets `blocking_armed` for that game (S1). The game's prefix before round t is the
  baseline game.
* Mapping keys gain creature effects (power, toughness, combat keywords, entry round, leave round). Blockers
  interact (two walls are not twice one wall), so they belong to the interaction levels 2-3 of
  control-scoring-mode.md section 7.5, not the pure sum. Stage 1 adds blocker singles (for example a 0/4 wall, a
  2/2, a 4/4 reach, on rounds 1-4) to the delay table, reporting the scored, rule and attack-all numbers.

**What stays wrong (stated in every report):** open loop. A blocker that dies in the replay is alive in our own
game, so our W can count on it later (control-scoring-mode.md 5.6). The gauntlet does not adapt beyond its own
search, and its keep table never knew a blocker was coming.

## 8. Open questions (each with the default this design takes; nothing here blocks the build)

* **Q-B1. Which of our creatures block in the replay?** Only scheduled creatures, or also the passive
  opponent's spawns and the creatures the gauntlet hands us (Forbidden Orchard Spirits, Hunted Phantasm's
  Goblins, Varchild's Survivors)? Those gifted bodies are really ours in the replay, but the spawns are a
  goldfish fixture, and letting either block puts delay into every key that is not caused by our effect.
  **Default: only scheduled creatures block.** The gifted bodies stay ours for every other purpose (Massacre
  Wurm's drain, Blasphemous Act's discount), which is what the body-value ruling needs.
* **Q-B2. How deep is the scored clairvoyance?** One combat deep (later combats by the rule) or the full tree
  (exponential in the number of combats)? **Default: one combat deep**, with a two-deep variant measured on a
  sample in stage 1 to see whether depth changes the delays.
* **Q-B3. Landwalk against which lands?** In the replay the passive side holds a random pool deck's lands, not
  the control deck's. **Default: the schedule carries the control deck's basic land types (from its decklist),
  and landwalk and other "defending player controls a land" checks read those.** It changes nothing in an
  unarmed game.
* **Q-B4. When do our scheduled combat effects act** (a fog, removal of an attacker, a Parallax Wave exile, a
  flash blocker)? **Default: after the gauntlet's post-block trick root has passed**, so we act last.
* **Q-B5. The one-sided-loss prune's default** (4.3, user-owned). **Default: OFF** until the held-out large-budget
  check; then the USER decides on the numbers.
* **Q-B6. The can't-die prune for decks that value our creatures** (Stuffy Doll, Hinata, by card parameter).
  **Default: P1 is off for those decks and the attack is searched**, because our creature dying can cost them a
  Blasphemous Act discount or a Hinata target.
* **Q-B7. Large declarations.** With three or more scheduled creatures the fork count is up to (c+1)^b per
  combat. **Default: exact enumeration for every b**, with any replay over 256 forks in one combat reported, and
  a decision on a cap only if that happens.

**Status items, not questions:** the rules skill's combat section needs the Foundations update (section 1.1); the
16 group-D prunes and 7 group-C collapses are gated by the standing rule for a failed premise (step S9); the
firebreathing move and the pre-block root follow from the no-greedy and completeness rules (section 5).

## Sources

* `.claude/skills/mtg-rules.md` (Combat CR 506-511; keyword tables; SBA; Code Review > Combat; the
  trample+deathtouch pattern) and `.claude/skills/mtg-ai.md` (Phase 2 block heuristic; the provider invariant).
* Damage assignment order removed by Magic Foundations: [Foundations Update Bulletin (Wizards)](https://magic.wizards.com/en/news/announcements/foundations-update-bulletin);
  [Magic Judges forum, Foundations release notes](https://apps.magicjudges.org/forum/topic/55908/);
  [MTG Salvation discussion](https://www.mtgsalvation.com/forums/magic-fundamentals/the-rumor-mill/834014-major-rules-change-no-more-damage-assignment-order);
  [senseos.gg combat rules guide](https://senseos.gg/guides/magic-the-gathering/combat-rules/);
  [TheGamer summary](https://www.thegamer.com/magic-the-gathering-mtg-combat-rules-change-foundations/).
* Code: `src/ai/Combat.{h,cpp}`, `src/core/GameEngine.{h,cpp}`, `src/core/GameState.h`, `src/core/DamageReflection.h`,
  `src/core/ControlInject.h`, `src/ai/DecisionProvider.h`, `src/ai/DecisionProviders.cpp`, `src/ai/TurnSolver.cpp`,
  `src/runner/GoldFishRunner.cpp`, `src/cards/data/cards.json` (read 2026-11-27 on `feature/control-mode`,
  tip `73694c1d0`).
