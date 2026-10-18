# Vampires — Stage 2 research, cluster C: combat and ETB triggers

Cards: **Drana, Liberator of Malakir**, **Mavren Fein, Dusk Apostle**, **Vampire Socialite**.
Worktree read: `/workspaces/MagicDeckTester/.claude/worktrees/vampires` @ 2176032dd. Read-only research;
no repo file was edited. All `file:line` citations are against that tree.

Summary of the verdict:

| Card | Tier | One-line model |
|---|---|---|
| Drana | **3** | A new **two-step combat damage** branch inside the ONE shared combat core (`ResolveCombatDamage`, Combat.cpp), entered only when an attacker carries the new `combat_damage_counters_each_attacker` param AND strikes first. Step 1 = first/double strikers, then Drana's trigger (`PutPlusCounters` on every attacking creature), then step 2 = non-first-strikers + double strikers with power recomputed. A new `CreatureHasFirstStrike` oracle reads the printed keyword + a lord grant (Stromkirk Captain, cluster B). Matching exact addend in `PendingAttackDamageImpl`. |
| Mavren Fein | **2** | New `attack_one_or_more_*` param family + one shared helper fired at declare-attackers in BOTH combat worlds, BEFORE the Adeline block: one untapped, non-attacking 1/1 white Vampire token with lifelink per combat in which ≥1 declared NONTOKEN Vampire attacks. Never scaled by opponent heads. |
| Vampire Socialite | **2** | Intervening-if ETB as a new conditional count beside Thalia's Lieutenant's block in `FireOwnEtbTriggers`; the static "enters with an additional counter" as a new conditional term in `FireEtbWatchers`' as-enters CR 614 block (the Giada/Mimic slot). Both read `GameState::opponent_lost_life_this_turn`. New `DeckUsesSecondMain` clause. |

No new viewer decision type (all bucket A). No new GameState / Permanent / Plan field (no `Dominance.h` sizeof
tripwire moves). Byte-identical for every other deck by construction (all new paths are param-gated on
params no existing card sets).

---

## 0. Engine facts this design rests on (verified)

1. **Combat damage is ONE summed step today, and it is shared.** `ResolveCombatDamage`
   (`src/ai/Combat.cpp:112-275`) is the single combat core called by the executor
   (`GameEngine::CombatPhase`, `src/core/GameEngine.cpp:837`) and the rollout (`SimulateCombat`,
   `src/ai/TurnSolver.cpp:50118`; also `RolloutSimulateCombat` and the firebreathing probe
   `FbProbeCombat`, Combat.cpp:301). Per attacker it computes
   `power = base_pw * (ds ? 2 : 1)` (Combat.cpp:170) — **double strike is collapsed to ×2**, first
   strike has **no reader anywhere** (`grep first_strike src/` is empty; `DecisionProviders.cpp:5220`
   says "first strike has no engine reader"). The post-damage hooks (Utvara, Lackey, Shroofus, Neheb)
   run once, after the whole loop (Combat.cpp:238-273).
2. **The search's this-turn attack projection is `PendingAttackDamageImpl`**
   (`TurnSolver.cpp:9334-9470`), reached via `PendingAttackDamage` (9313) from the pre-combat
   `pending_atk` (38654, 51807), the post-plan lethal checks (40575, 54211), `ProjectPlanWalk` (9890),
   the tap re-projection (10154, 10260), 11373, and the public `ProjectedAttackDamage[ForTest]`
   (9324-9332; DecisionProviders.cpp:44728). It mirrors the combat core term by term (its own comments
   say so) and must stay lockstep. `CollectAttackingManaSources` (9037) mirrors per-attacker power for
   creature mana sources (Vampires has none).
3. **"An opponent lost life this turn" = `GameState::opponent_lost_life_this_turn`**
   (`GameState.h:288`), set by combat damage (Combat.cpp:198, 231), attack-trigger damage
   (SpellEffects.h:15424) and ~40 other opponent-life-loss sites; reset at turn start in BOTH worlds
   (`GameEngine.cpp:255`, `TurnSolver.cpp:50325`); folded into the transposition key
   (`Dominance.h:753`). Our own life payment (Voldaren Estate, Asylum Visitor) does NOT set it. In this
   deck nothing makes the opponent lose life outside combat, so in practice the flag turns true only
   at combat damage.
4. **2HG**: the opponent is one team life pool (`opp_idx = 1 - active`); per-opponent scaling is
   `gamesetup::OpponentHeads()` and is applied only where the oracle says "each opponent"/"for each
   opponent".
5. **Second main**: `GoldFishRunner::DeckUsesSecondMain` (`GoldFishRunner.cpp:56-363`) already returns
   true for this deck **if cluster A1 sets `convoke` on Markov Baron** (line 86). Rollouts also play the
   post-combat main when it is on (TurnSolver.h:2783-2788). Aether Vial puts are main-phase
   `Action::Kind::ActivateVial` candidates, so with the second main on, a post-combat Vial put is an
   ordinary searched main-2 action.
6. **Provider routing hazards found (do NOT reuse these params):**
   * `etb_each_other_own_creature_counters > 0` is in the **Soldiers** routing signature
     (`DecisionProviders.cpp:21207-21211`) → reusing it for Socialite would misroute Vampires to
     `SoldiersProvider`.
   * `attack_per_matching_creates_tokens > 0` (Utvara) is in the **Dragons** signature
     (`DecisionProviders.cpp:21376-21383`) → reusing it for Mavren would misroute to `DragonsProvider`.
   * `other_subtype_enters_counters_per_each > 0` (Giada) is in the **Angels** signature (21198).
   Hence every param below is NEW. The integrator should run `python3 scripts/provider_audit.py
   decks/Vampires` after integration.

---

## 1. Drana, Liberator of Malakir

### 1.1 Scryfall (verbatim, `build/scratch/vampires/scry/Drana__Liberator_of_Malakir_.json`)
- name: `Drana, Liberator of Malakir`
- mana_cost: `{1}{B}{B}` — cmc 3.0
- type_line: `Legendary Creature — Vampire Ally`
- power/toughness: 2 / 3
- keywords: `["Flying", "First strike"]`
- oracle_text: `Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking creature you control.`

### 1.2 Clause accounting

| Clause | Model | Tier |
|---|---|---|
| Cost `{1}{B}{B}`, Legendary Creature — Vampire Ally 2/3 | data; legend rule is engine-wide (`EnforceLegendRule`), 1 copy so it never binds | 1 |
| Flying | printed keyword stored. **Inert**: the opponent never blocks; the only flying reader (`haste_on_flying_enter`, Dragon Tempest) is not in this deck. Bracket-noted. | 1 (inert) |
| First strike | **NOT inert here.** Read by the new `CreatureHasFirstStrike` oracle; puts Drana in the first-strike damage step (CR 510.4), which is what lets her trigger grow the regular step. | 3 |
| "Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking creature you control." | new param `combat_damage_counters_each_attacker: 1`, resolved between the two damage steps (CR 510.4 + 603.2): `PutPlusCounters` on every creature still in `atk_idx` (declared + put-in-attacking tokens, Drana included). Self-scoped (her own damage only). Fires after EVERY step in which she connects (a double-striking Drana would trigger twice; the second trigger only matters for later turns). | 3 |

**2HG**: one trigger per damage event. Drana attacks one player (or the team) and deals her damage in one
event per step, so there is never a second trigger from a second head. Do **not** multiply by
`OpponentHeads()`.

### 1.3 The two-step combat model (faithful CR 510.4)

> 510.4: if at least one attacking creature has first strike or double strike as the combat damage step
> begins, only those creatures deal damage in the first step. Then a second step: the creatures that had
> NEITHER first strike nor double strike as the first step began, plus those that CURRENTLY have double
> strike.

Consequences for this deck:
* **No Stromkirk Captain:** Drana (first strike) deals 2 (+ lords) in step 1. Her trigger puts a counter on
  every attacker (her included). Step 2: every other attacker deals damage **with** the counter (+1 each).
  Drana does not deal damage again.
* **Stromkirk Captain out** ("Other Vampire creatures you control get +1/+1 and have first strike"; Captain
  itself has printed first strike): every Vampire attacker strikes first, step 2 is empty, Drana's counters
  add **0 damage this turn** (they persist and pay off next turn). Every creature in this list is a Vampire
  and every token is a Vampire, so with Captain out the split is fully degenerate.
* Membership is decided as step 1 begins (a creature that gains first strike later still deals step-2
  damage). Our model computes membership once, before step 1 — consistent.

#### Where it lives — ONE place, both worlds

All of it goes inside `ResolveCombatDamage` (`Combat.cpp:112`). Because the executor
(GameEngine.cpp:837), the rollout (TurnSolver.cpp:50118), `RolloutSimulateCombat` and the firebreathing
lethality probe (Combat.cpp:307) all call it, executor/rollout lockstep is by construction. No change to
`GameEngine::CombatPhase` or `SimulateCombat` is needed for Drana.

#### Gate (byte-identity for every other deck, zero extra lookups)

Extend `BoardSources` / `GatherBoardSources` (`SpellEffects.h:4807` / `:4851`) with two lists, gathered in
the walk that already runs at Combat.cpp:143 and TurnSolver.cpp:9347:
* `std::vector<int> fs;` — permanents whose def has `grants_first_strike` (cluster B's Stromkirk Captain
  param; see 1.5). Same superset contract as `bs.ds`.
* `std::vector<int> fs_dmg_counters;` — permanents whose def has `combat_damage_counters_each_attacker > 0`.
* `BoardSources::OnErase` must fix both new lists (it fixes every list by hand — add them to the `fix(...)`
  line, SpellEffects.h ~4846).

Split condition, evaluated once after the gather:
```
bool split = false;
if (!bs.fs_dmg_counters.empty())                       // empty for every other deck -> skip entirely
    for (int idx : atk_idx)                             // is a Drana-like ATTACKING and striking first?
        if (in(bs.fs_dmg_counters, idx) && (CreatureHasFirstStrike(p, state, &bs.fs) || ds(p)))
            { split = true; break; }
```
`split == false` → the existing loop runs **textually unchanged** (do not refactor it into a lambda that
the non-split path also uses unless the diff is provably order-preserving; the safest diff leaves the
current loop as the `else` branch). The Drana trigger still fires in the non-split case (a Drana that does
not strike first deals damage in the single step and her counters land after it — future turns only).

#### Split path (new branch)

```
// CR 510.4 two-step combat damage. Entered only when a Drana-like attacker strikes first.
std::vector<char> fs_of(n), ds_of(n);       // membership fixed as step 1 begins
for k: fs_of[k] = CreatureHasFirstStrike(p_k, state, &bs.fs); ds_of[k] = CreatureHasDoubleStrike(p_k, state, &bs.ds);
std::vector<int> dealt(n, 0);               // per-attacker total, for attacker_descs
auto deal_step = [&](bool first_step) {
    std::vector<int> step_idx, step_pw; std::vector<int> step_lifelink;
    for k in atk order:
        bool in_step = first_step ? (fs_of[k] || ds_of[k]) : (!fs_of[k] || ds_of[k]);
        if (!in_step) continue;
        // SAME per-attacker power assembly as the current loop body (lord_pb, exalted_bonus,
        // animate_power, DynamicBasePower, Aura/Equip bonus) but multiplier 1 (each step is one hit);
        // recomputed in step 2, so the counters Drana put on it are read.
        // Jitte bearer: run the existing JitteDamageMath closed form ONCE, in the bearer's first step,
        // with the original ds flag, and skip it in step 2 (disclosed approximation: Drana + Jitte is in
        // no deck).
        life -= power; total += power; if (power>0) { opponent_lost_life_this_turn = true;
            step_idx.push_back(idx); step_pw.push_back(power); free-cast bank; lifelink -> step_lifelink; }
        tap if not vigilant (first appearance only); dealt[k] += power;
    for amt in step_lifelink: GainLife(...)              // CR 510.2/702.15b: per step
    // combat-damage triggers of THIS step (CR 603.2), Drana first:
    FireCombatDamageTeamCounters(state, active, atk_idx, step_idx);   // NEW (below)
    FireCombatDamageCheatIntoPlay(state, active, step_idx);            // existing hooks, per step
    FireCombatDamageTokens(state, active, step_idx, step_pw);
    Neheb discard loop over step_idx;
};
deal_step(true); deal_step(false);
// then, exactly as today, AFTER all damage: CountAttackTriggerLifeLoss (Leeching Sliver) + FireUtvaraAttackTokens
attacker_descs: one "<name> (<dealt[k]>)" per attacker with dealt[k] > 0, in atk order
```
* `attacker_descs` MUST keep the one-entry-per-attacker `"Name (power)"` format:
  `test/viewer_protocol_check.py:284-309` parses the combat event text into the reference-replay
  attacker pin. A double striker therefore shows its two-step sum, as today.
* Firing Lackey/Shroofus/Neheb per step is the faithful CR shape and is safe because the split branch is
  live only with a Drana-like attacker (no existing deck can reach it). Every one of those hooks only
  APPENDS to the battlefield (Combat.cpp:247-249 comment), so `atk_idx`/`step_idx` stay valid.
* SBAs between steps: no blockers, so nothing dies; an opponent at ≤0 after step 1 loses at the same turn.

New helper (SpellEffects.h, beside `FireCombatDamageTokens`):
```
// Drana, Liberator of Malakir: "Whenever [this] deals combat damage to a player, put a +1/+1 counter on
// each attacking creature you control." One trigger per connecting source per damage step.
inline void FireCombatDamageTeamCounters(GameState& s, int controller,
                                         const std::vector<int>& atk_idx, const std::vector<int>& dealt_idx)
{
    int per = 0;
    for (int i : dealt_idx) { bounds-check; d = LookupCached; if (d && d->params.combat_damage_counters_each_attacker > 0) per += ...; }
    if (per <= 0) return;
    int n = 0;
    for (int i : atk_idx) {                      // "each ATTACKING creature you control" -- still on bf,
        bounds-check; Permanent& q = s.battlefield[i];   // still a creature (CR 506.4), ours
        if (q.controller_index != controller || !(q.card.IsCreature() || q.is_animated)) continue;
        PutPlusCounters(s, q, per);              // doubler-aware chokepoint (SpellEffects.h:2177)
        ++n;
    }
    if (g_play_event_sink && !g_tap_speculating && n > 0)
        EmitPlayEvent(s.turn_number, "trigger", "<source>: +1/+1 counter on each attacking creature (" + n + ")");
}
```
(`RevealLogPause` nulls `g_play_event_sink` in rollouts, GameLogger.h:1110, so the event is real-play only.)
In the non-split branch call it once after the existing loop with the full `damaging_idx`, placed first
among the post-damage hooks.

#### New first-strike oracle (SpellEffects.h, beside `CreatureHasDoubleStrike` at 4742)
```
inline bool HasFirstStrikeFromLords(const Card&, const std::vector<Permanent>&, int controller,
                                    bool all_creature_types, const std::vector<int>* fs_idx);   // mirror of
    // HasDoubleStrikeFromLords (4632) on `grants_first_strike`, honouring lord_excludes_self by address
    // when the caller passes the Permanent (Captain's "Other" is moot for Captain itself -- printed FS).
inline bool CreatureHasFirstStrike(const Permanent& c, const GameState& s, const std::vector<int>* fs_idx = nullptr)
{ return c.card.HasKeyword(Keyword::FirstStrike) || HasFirstStrikeFromLords(c.card, s.battlefield,
         c.controller_index, c.AnimatedAllTypes(), fs_idx); }
```
Readers: ONLY the split path and the projection addend below. Deliberately NOT extended to equipment/aura
first-strike grants (Daybreak Coronet, Hyena/Indrik Umbra, Ethereal Armor) or Rick's chosen keyword
(`kChosenKwFirstStrike`) — no deck holds Drana with any of them, so those readers stay out until one does.

#### Projection addend (lockstep twin) — `PendingAttackDamageImpl`, TurnSolver.cpp:9334
The main loop already has `adef`, `ds` and `base_pw` per attacker. Record per attacker
`fs_of` (`CreatureHasFirstStrike(p, state, &bs.fs)`, only when `!bs.fs_dmg_counters.empty()`) and
`dr_of` (`adef ? adef->params.combat_damage_counters_each_attacker : 0`) and `pw1_of` (= `base_pw`). After
the battle-cry / team-pump passes add:
```
// Drana: a first-striking source that connects in step 1 puts `per` counters on every attacker; each
// attacker that deals damage in STEP 2 (no first strike, or double strike) is `per` bigger there.
dmg += CountFirstStrikeStepCounterPump(state, attackers, fs_of, ds_of, dr_of, pw1_of, tok specs);
//   per     = sum over i with dr_of[i] > 0 && (fs_of[i] || ds_of[i]) && pw1_of[i] > 0 of
//             dr_of[i] << DoublerShift(state, active, /*for_tokens=*/false)      (mirrors PutPlusCounters)
//   regular = #{ i : !fs_of[i] || ds_of[i] } + #{ attack tokens entering attacking whose token Card has no
//             first strike from lords (HasFirstStrikeFromLords on the projection's `tok`) }
//   return per * regular;   // 0 when bs.fs_dmg_counters is empty -> byte-identical
```
This is exact for the deck: without Captain it credits +1 per non-first-striker; with Captain it credits 0.
Omitting it would be the safe (under-credit) direction but would blind the board-lethal short-circuit to
Drana lethals; crediting it WITHOUT the Captain first-strike reader would be the dangerous over-credit
(phantom lethal → the fd-diverge class). `CollectAttackingManaSources` needs nothing (no creature mana
sources in this deck; the exact re-projection `MTG_ATTACK_TAP_REPROJECT` re-runs PendingAttackDamage on the
tapped board, so it inherits the addend). `ProjectPlanWalk`'s hasty bodies (Bloodghast with haste) are
credited by `haste_attack_power` without the +1 — under-credit, conservative, disclosed.

Other power-summing estimators (`AttackersCombatDamage` SpellEffects.h:17472, `AuraHostKeyAttackSum`
32700, `CountAttackGatherPump`/`CountAttackAttachPump` TurnSolver.cpp:9150-9265, equip-host rankers in
DecisionProviders.cpp:1307/1320/44696, TurnSolver.cpp:12112) are Bruna/Raubahn/equipment planners: not
reachable for Vampires, left unchanged (disclose).

### 1.4 Proposed `cards.json` entry
```json
{
 "name": "Drana, Liberator of Malakir",
 "mana_cost": "{1}{B}{B}",
 "types": ["Creature"],
 "supertypes": ["Legendary"],
 "subtypes": ["Vampire", "Ally"],
 "keywords": ["Flying", "First strike"],
 "power": 2,
 "toughness": 3,
 "oracle_text": "Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking creature you control. [FIRST STRIKE + TRIGGER MODELLED (CR 510.4): when Drana attacks with first strike, the shared combat core (Combat.cpp ResolveCombatDamage, executor + rollout) runs TWO damage steps -- first/double strikers, then Drana's trigger (combat_damage_counters_each_attacker 1: a +1/+1 counter, via the doubler-aware PutPlusCounters, on every creature still attacking, Drana included), then the creatures without first strike (and double strikers) with the counter already on them. First strike is read by CreatureHasFirstStrike (printed keyword + Stromkirk Captain's grant), so under a Captain every Vampire strikes first and the counters add nothing THIS turn (they persist). TurnSolver's PendingAttackDamage carries the exact twin addend. One trigger per damage event, never scaled by opponent heads. [PARTIAL: Flying is inert -- the passive opponent never blocks and no card in this deck reads flying. PROVISIONAL pending user sign-off.]]",
 "template": "vanilla_creature",
 "parameters": {
  "combat_damage_counters_each_attacker": 1
 }
}
```
(`vanilla_creature` = the Neheb, the Worthy precedent for a creature carrying a combat-damage trigger;
`combat_damage_*` satisfies `analyze_deck.py`'s `TRIGGER_PARAM_RE`.)

### 1.5 Dependency on cluster B (Stromkirk Captain) — MUST be reconciled by the integrator
Captain's "…and have first strike" is **load-bearing** in this deck because of Drana. If cluster B
brackets it as "first strike inert (no blockers)" — the Field Marshal / Knight Exemplar / Neheb precedent —
Drana's counters would be credited to Vampires that really strike first: a systematic over-count, in the
realized game AND the projection (both read the same oracle, so no harness would flag it). Contract I
assume: Captain = `lord_effect`, `subtypes_affected ["Vampire"]`, `power_bonus 1`, `tough_bonus 1`,
`lord_excludes_self true`, plus **`grants_first_strike: true`** (new bool, read only by
`HasFirstStrikeFromLords`). If cluster B picks another name, the oracle reads that name.

Other first-strike carriers (Field Marshal, Knight Exemplar, Neheb, Striking Sliver, Goblin Chainwhirler,
Burning-Fist Minotaur, Lyra, Ocelot Pride, Thalia, Rick, Moonrage Brute, Boros Reckoner, Dwalin, Balan,
Cosmic Spider-Man, Slaughter-Priest's grant, the Umbras/Coronet/Ethereal Armor): **do not retrofit** them.
Without a Drana-like attacker the split never engages, so the collapse stays exact. Checked specifically:
Neheb's combat-damage discard can switch on "Minotaurs you control get +2/+0", but Neheb grants first strike
to every other Minotaur, so the regular step holds no Minotaur and the collapse is exact (Minotaur GT is
unaffected even in principle). Their existing bracket notes ("the engine collapses combat damage into one
event") become slightly stale wording — the integrator may reword to "the engine splits combat damage only
for a step-sensitive combat-damage trigger (Drana)"; that is a text-only change.

---

## 2. Mavren Fein, Dusk Apostle

### 2.1 Scryfall (verbatim)
- name: `Mavren Fein, Dusk Apostle`
- mana_cost: `{2}{W}` — cmc 3.0
- type_line: `Legendary Creature — Vampire Cleric`
- power/toughness: 2 / 2
- keywords: `[]`
- oracle_text: `Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with lifelink.`

### 2.2 Clause accounting

| Clause | Model | Tier |
|---|---|---|
| Cost, Legendary Creature — Vampire Cleric 2/2 | data; 1 copy, legend rule never binds | 1 |
| "Whenever one or more nontoken Vampires you control attack" | PLAYER-scoped trigger (Mavren need not attack; Mavren herself counts as a nontoken Vampire). ONE trigger per combat however many match (CR 603.2c "one or more"). Matching is over the DECLARED attackers only (tokens put onto the battlefield attacking were never declared, CR 508.4 — and they are tokens anyway), live subtypes (`CardHasSubtype` / `AnimatedAllTypes`), `!is_token`. | 2 |
| "create a 1/1 white Vampire creature token with lifelink" | `CreateToken(state, ctrl, 1, 1, {"Vampire"}, "W", {"Lifelink"})` — untapped, summoning-sick, **NOT attacking** (created after declaration). Goes through the universal enter cascade, so Vampire Socialite's static and the Vampire lords reach it. Colour `W` is load-bearing (Markov Baron's convoke reads creature colour); lifelink is modelled by `CreatureHasLifelink` on the printed token keyword (SpellEffects.h:3576), not inert-deferred. | 2 |
| 2HG | one trigger, one token (no "for each opponent"): the helper never multiplies by `OpponentHeads()`. | — |

### 2.3 Why a new param family (not Adeline's, not Utvara's)
* `attack_creates_tokens` (Adeline/Hero, CardDatabase.h:936-973) makes tokens **tapped and attacking** and
  its caller appends them to `atk_idx` (GameEngine.cpp:740-744, TurnSolver.cpp:50030-50034); it is also
  read by `PendingAttackDamageImpl` (9418-9449: credits the tokens' damage), `AttackHasNonPowerValue`
  (DecisionProviders.cpp:7038) and a rank (10115). Every one of those would be wrong for a non-attacking
  token.
* `attack_per_matching_creates_tokens` (Utvara) is PER matching attacker (Mavren is one per combat), fires
  inside `ResolveCombatDamage` **after damage** (Combat.cpp:238 — too late: a Socialite on board would then
  wrongly give the token a counter off the combat's own life loss), and it is a **Dragons routing
  signature** param (DecisionProviders.cpp:21377).
* So the `attack_tokens_per_opponent` question is answered structurally: the new helper has no
  per-opponent multiply at all (that flag defaults TRUE on Adeline's family, which is exactly why reusing it
  silently would double in 2HG).

New `CardParams` (CardDatabase.h, next to the attack-token block ~973; read in `BuildParamsFromJson` next
to CardDatabase.cpp:1034-1067; all default 0/empty/false → inert):
```
int                      attack_one_or_more_tokens       = 0;   // tokens per qualifying combat (Mavren: 1)
std::vector<std::string> attack_one_or_more_subtypes;            // attacker filter, OR (empty = any creature)
bool                     attack_one_or_more_nontoken     = false; // "nontoken" attackers only
int                      attack_one_or_more_token_power  = 0;
int                      attack_one_or_more_token_toughness = 0;
std::vector<std::string> attack_one_or_more_token_subtypes;
std::string              attack_one_or_more_token_color;          // colour letters ("W")
std::vector<std::string> attack_one_or_more_token_keywords;       // ("Lifelink")
```
Also add `attack_one_or_more_token_subtypes` to `CardDatabase::LoadFromJson`'s token-subtype interning list
(the comment in analyze_deck.py notes that list once covered "4 of the ~15 token-subtype params"; check
that every token-subtype vector is interned so `CardHasSubtype` sees "Vampire" on the token).

### 2.4 Helper and call sites
SpellEffects.h, beside `FireAttackCreateTokens` (17801):
```
// Mavren Fein: "Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire
// token with lifelink." One trigger per combat per source; tokens UNTAPPED and NOT attacking (created after
// declaration). `declared` = the DECLARED attacker indices only. Specs gathered before any CreateToken
// (CreateToken push_backs and invalidates references); CreateToken (never CreateTokenOnce) so a token
// doubler applies.
inline void FireAttackOneOrMoreTokens(GameState& s, int ctrl, const std::vector<int>& declared);
```
Call it in BOTH worlds at the same point: immediately after `const int declared_n = …` and BEFORE the
Adeline block — `GameEngine.cpp:733` / `TurnSolver.cpp:50025`. That position is (a) after
`ApplyAttackTriggerDamage` / `ApplyAttackModalTriggers` (CR 603.3b lets us order simultaneous attack
triggers; damage-first is weakly dominant because a Socialite on board then gives the token its counter,
and nothing is lost), (b) before `FireAttackCreateTokens` computes `tok_start` from `battlefield.size()`,
so the Mavren token is never appended to `atk_idx`, and (c) before every power read (irrelevant — the token
does not attack). A play event "Mavren Fein, Dusk Apostle: 1/1 white Vampire token with lifelink" under
`g_play_event_sink && !g_tap_speculating`.

Projection: nothing to add — the token does not attack this turn. Its later turns are seen by rollouts
through the shared combat core.

Optional, param-gated (inert in this list, every nontoken Vampire has power ≥ 1): in
`AttackHasNonPowerValue` (DecisionProviders.cpp:7034) treat "a nontoken creature matching a controlled
`attack_one_or_more_*` source's subtypes" as attack value, so a 0-power Vampire is never held back when its
attack would make the token.

### 2.5 Second main (2c-bis)
The token is a **resource generated during combat** that only a post-combat main can spend: Markov Baron's
convoke (an untapped white creature pays `{1}`), Bloodline Keeper's "five or more Vampires" transform, Pit
Fighter's sacrifice. Add `attack_one_or_more_tokens > 0` to `DeckUsesSecondMain` (Shroofus rule: detection
must not be parasitic on Markov Baron's convoke).

### 2.6 Proposed `cards.json` entry
```json
{
 "name": "Mavren Fein, Dusk Apostle",
 "mana_cost": "{2}{W}",
 "types": ["Creature"],
 "supertypes": ["Legendary"],
 "subtypes": ["Vampire", "Cleric"],
 "keywords": [],
 "power": 2,
 "toughness": 2,
 "oracle_text": "Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with lifelink. [Fully modelled, nothing deferred. attack_one_or_more_tokens 1 with attack_one_or_more_subtypes [Vampire] + attack_one_or_more_nontoken: a PLAYER-scoped trigger (Mavren need not attack; she counts herself) that fires ONCE per combat in which at least one DECLARED nontoken Vampire attacks (FireAttackOneOrMoreTokens, called at declare-attackers in GameEngine::CombatPhase and TurnSolver::SimulateCombat, before the Adeline block). The token is created UNTAPPED and NOT attacking, summoning-sick, through the universal enter cascade (Socialite's static and the Vampire lords reach it); white (convoke reads its colour) with printed lifelink (CreatureHasLifelink). Deliberately NOT Adeline's attack_creates_tokens (tapped-and-attacking, scales per opponent by default) and NOT Utvara's attack_per_matching_creates_tokens (per attacker, fires after damage, a Dragons routing signature). Never scaled by opponent heads: 'one or more ... attack' is one trigger in 2HG too.]",
 "template": "vanilla_creature",
 "parameters": {
  "attack_one_or_more_tokens": 1,
  "attack_one_or_more_subtypes": ["Vampire"],
  "attack_one_or_more_nontoken": true,
  "attack_one_or_more_token_power": 1,
  "attack_one_or_more_token_toughness": 1,
  "attack_one_or_more_token_subtypes": ["Vampire"],
  "attack_one_or_more_token_color": "W",
  "attack_one_or_more_token_keywords": ["Lifelink"]
 }
}
```

---

## 3. Vampire Socialite

### 3.1 Scryfall (verbatim)
- name: `Vampire Socialite`
- mana_cost: `{B}{R}` — cmc 2.0
- type_line: `Creature — Vampire Noble`
- power/toughness: 2 / 2
- keywords: `["Menace"]`
- oracle_text: `Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opponent lost life this turn, put a +1/+1 counter on each other Vampire you control.\nAs long as an opponent lost life this turn, each other Vampire you control enters with an additional +1/+1 counter on it.`

### 3.2 Clause accounting

| Clause | Model | Tier |
|---|---|---|
| Cost `{B}{R}`, Creature — Vampire Noble 2/2 | data | 1 |
| Menace | printed keyword, **inert** (opponent never blocks; no engine reader). Bracket-noted. | 1 (inert) |
| ETB, intervening-if (CR 603.4): "if an opponent lost life this turn, put a +1/+1 counter on each other Vampire you control" | new `etb_each_other_own_creature_counters_if_opp_lost_life: 1`, recipient filter `etb_counters_subtypes ["Vampire"]` (shared with Lieutenant; that vector is NOT a routing signature). Evaluated at entry. The condition is MONOTONIC within a turn (the flag is only ever set, until the turn-start reset), so "true when it triggers" ⇔ "true on resolution": the single check is exact, not an approximation. Fires on every enter route through `FireOwnEtbTriggers` (cast, Vial put, madness cast via Falkenrath Gorger, every rollout). "Other" = not itself; recipients include Vampire tokens. Counters through `PutPlusCounters` (doubler-aware). | 2 |
| Static replacement (CR 614.1c/614.12): "As long as an opponent lost life this turn, each other Vampire you control enters with an additional +1/+1 counter" | new `opp_lost_life_other_enters_counters: 1` + `opp_lost_life_other_enters_subtype: "Vampire"`, a new term in `FireEtbWatchers`' as-enters counter block (the Giada/Mimic CR 614 slot, ABOVE `FireCreatureEnterWatchers`, so the counter is on the body before any enter trigger reads it). Reaches tokens (Mavren's, Bloodline Keeper's), Vial puts, madness casts and Bloodghast's landfall return — every route goes through the universal cascade. | 2 |

### 3.3 C++ changes
1. **CardParams** (CardDatabase.h, next to Lieutenant's block 3806-3807) + `BuildParamsFromJson` (next to
   CardDatabase.cpp:1718-1720):
   ```
   int         etb_each_other_own_creature_counters_if_opp_lost_life = 0;   // Socialite ETB (intervening if)
   int         opp_lost_life_other_enters_counters = 0;                     // Socialite static (CR 614)
   std::string opp_lost_life_other_enters_subtype;                          // "Vampire"
   ```
   NOT `etb_each_other_own_creature_counters` (Soldiers routing signature, DecisionProviders.cpp:21209) and
   NOT `other_subtype_enters_counters_*` (Giada; Angels signature).
2. **ETB** — `FireOwnEtbTriggers`, Thalia's Lieutenant block (SpellEffects.h:8868-8895). Change its count to
   ```
   const int n = p.etb_each_other_own_creature_counters
               + ((p.etb_each_other_own_creature_counters_if_opp_lost_life > 0
                   && state.opponent_lost_life_this_turn)
                  ? p.etb_each_other_own_creature_counters_if_opp_lost_life : 0);
   if (n > 0) { … the existing loop with n … }
   ```
   Lieutenant: second term 0 → byte-identical. The existing loop's recipient test (creature or animated,
   `etb_counters_subtypes`, `qi != entered_index`) is exactly Socialite's "each other Vampire you control".
3. **Static** — `FireEtbWatchers` as-enters block (SpellEffects.h:7465-7505), inside the per-watcher loop:
   ```
   if (wd->params.opp_lost_life_other_enters_counters > 0 && state.opponent_lost_life_this_turn
       && &w != &state.battlefield[entered_index]                         // "other"
       && (state.battlefield[entered_index].AnimatedAllTypes()
           || CardHasSubtype(state.battlefield[entered_index].card, wd->params.opp_lost_life_other_enters_subtype)))
   { add += wd->params.opp_lost_life_other_enters_counters; }
   ```
   placed before the Giada `continue`. It is folded into the existing `add` → one `AddPlusCounters`,
   exactly as Mimic/Giada (that block's pre-existing choice of `AddPlusCounters` over the doubler-aware
   `PutPlusCounters` is inherited; no doubler in this deck — note only).
4. **Presence gate** — `GoldFishRunner.cpp:1319-1323`: `if (d->params.opp_lost_life_other_enters_counters > 0)
   subtype_enter_counters = true;` so `deck_has_subtype_enter_counters` opens the scan for this deck only.
5. **Second main** — `DeckUsesSecondMain` (GoldFishRunner.cpp, after the spectacle clause at :72):
   ```
   // VAMPIRES (2026-10-18): Vampire Socialite's ETB and static both key on "an opponent lost life this
   // turn", which in this goldfish only COMBAT DAMAGE sets -- the spectacle shape. Casting / Vialing /
   // madness-casting Socialite (and any Vampire entering under it) after damage is a play combat
   // enables. Mavren Fein's token is a combat-created body (convoke / Keeper's five-Vampire count /
   // Pit Fighter's sacrifice). Each param alone flips it (the Shroofus rule).
   if (def->params.etb_each_other_own_creature_counters_if_opp_lost_life > 0
       || def->params.opp_lost_life_other_enters_counters > 0
       || def->params.attack_one_or_more_tokens > 0) { return true; }
   ```
   No lever: Markov Baron's `convoke` already opens main 2 for this deck, so an `=0` arm on these terms
   would be a null A/B (the Bogbeast lesson in the same function).
6. **Projection**: `ProjectPlanWalk` (TurnSolver.cpp:9810) does not run ETB triggers, so a PRE-combat
   Socialite ETB would be under-credited (conservative; the same is true of Lieutenant and Mimic today).
   Moot for this list: nothing makes the opponent lose life before combat. No change proposed.

### 3.4 Proposed `cards.json` entry
```json
{
 "name": "Vampire Socialite",
 "mana_cost": "{B}{R}",
 "types": ["Creature"],
 "supertypes": [],
 "subtypes": ["Vampire", "Noble"],
 "keywords": ["Menace"],
 "power": 2,
 "toughness": 2,
 "oracle_text": "Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opponent lost life this turn, put a +1/+1 counter on each other Vampire you control.\nAs long as an opponent lost life this turn, each other Vampire you control enters with an additional +1/+1 counter on it. [CLAUSE 2 (intervening-if ETB, CR 603.4): etb_each_other_own_creature_counters_if_opp_lost_life 1 with etb_counters_subtypes [Vampire], resolved in FireOwnEtbTriggers beside Thalia's Lieutenant's block -- every enter route (cast, Aether Vial, madness, rollout). The condition reads GameState::opponent_lost_life_this_turn, which only goes false at the turn-start reset, so checking it once at entry is EXACT for both the trigger and the resolution check. CLAUSE 3 (CR 614 replacement): opp_lost_life_other_enters_counters 1 + opp_lost_life_other_enters_subtype Vampire, in FireEtbWatchers' as-enters counter block (the Giada / Metallic Mimic slot, above the enter-trigger watchers), so tokens (Mavren Fein, Bloodline Keeper), Vial puts, madness casts and Bloodghast's return all get it. Our own life payments (Voldaren Estate, Asylum Visitor) do not set the flag. Deliberately NOT Lieutenant's etb_each_other_own_creature_counters (a Soldiers routing signature) nor Giada's other_subtype_enters_counters_* (an Angels signature). Both halves make the post-combat main a real decision, so DeckUsesSecondMain detects them. [PARTIAL: Menace is inert -- the passive opponent never blocks. PROVISIONAL pending user sign-off.] [PARTIAL: CR 614.12 -- a Vampire entering SIMULTANEOUSLY with Socialite does not get the static's counter; the engine enters permanents one at a time, and no card in this deck puts two Vampires onto the battlefield in one event. PROVISIONAL pending user sign-off.]]",
 "template": "custom",
 "parameters": {
  "etb_each_other_own_creature_counters_if_opp_lost_life": 1,
  "etb_counters_subtypes": ["Vampire"],
  "opp_lost_life_other_enters_counters": 1,
  "opp_lost_life_other_enters_subtype": "Vampire"
 }
}
```
(`custom` = the Thalia's Lieutenant precedent for an ETB-trigger creature.)

---

## 4. Search design (no greedy pick at remaining depth > 0)

All three cards' effects are **mandatory, untargeted and exhaustive** — no "may", no target, no mode,
no count. So they create no branch the search must own; the engine applies them in the shared core in
both worlds. The decisions they touch:

| Decision | How the search sees it (rollout AND executor) |
|---|---|
| Cast Socialite in main 1 vs main 2 | A main_phase plan in both mains: `DeckUsesSecondMain` is true, rollouts play main 2 too (TurnSolver.h:2783), and the interior second main is searched unconditionally (docs/design/searched-second-main-unconditional.md). No classifier is active for this deck. Pre-combat can still be right (Socialite as a convoke body for Markov Baron, or the fifth Vampire for Bloodline Keeper's transform), so both mains must stay enumerated. |
| Vial-put / madness-cast Socialite (or any Vampire under it) after combat | `ActivateVial` and the madness route are main-phase actions, offered in the post-combat main. Searched. |
| Cast Stromkirk Captain pre- vs post-combat with Drana out | Same second-main search. Captain pre-combat: +1 lord damage on every other Vampire, but Drana's counters add 0 this turn. Post-combat: the counters add +1 per attacker. The search compares the two realised combats through the shared core. |
| Attack declarations | Unchanged standing attack exemption (`AttackWith`). |
| Trigger ORDER (CR 603.3b) | Fixed by stated dominance/inertness, not a heuristic pick. (a) Drana's counters run first among step-1 combat-damage triggers. They commute with every existing hook: no deck holds Drana with Lackey, Shroofus or Neheb. (b) Mavren's token is created after the damage-dealing attack triggers. Weakly dominant: the token can only gain Socialite's counter. (c) Socialite's ETB vs other enter watchers. Counter addition commutes (the Lieutenant precedent, SpellEffects.h:8861-8866). |

Nothing new is greedy inside the window. The only engine-side choice is the step membership, and that is
rules-determined.

## 5. Viewer decisions (2c-ter)

All **bucket A**. No new type, so none of the four wiring sites is touched.
* Socialite main 1/main 2 → `main_phase` plans (both mains; human play already plays main 2,
  AIEngine.cpp:2694 `use_external`).
* Post-combat Vial put → `main_phase` plan in main 2 (+ the existing `vial_charge` at upkeep).
* Drana's counters, Mavren's token and Socialite's counters → automatic. They get play-viewer HISTORY events
  only (`EmitPlayEvent`, guarded `g_play_event_sink && !g_tap_speculating`). The Drana event lands before the
  "⚔ attacked:" summary line. The summary keeps one "Name (power)" per attacker so the reference-replay pin
  parser (`test/viewer_protocol_check.py:300-307`) is unaffected.
* `scripts/audit_viewer_decisions.py` hard-fails on unclassified params, so each new param needs an
  `INERT_PARAMS` entry (`:623`). Reasons: `combat_damage_counters_each_attacker` = "automatic combat-damage
  trigger (Drana), mandatory, untargeted, exhaustive". `attack_one_or_more_*` = "automatic attack trigger
  (Mavren) + token spec constants". `etb_each_other_own_creature_counters_if_opp_lost_life` = "Socialite
  intervening-if ETB, mandatory, untargeted, exhaustive". `opp_lost_life_other_enters_*` = "mandatory
  CR 614 as-enters counter replacement". Cluster B's `grants_first_strike` = "static keyword grant". These
  classifications need the user's OK; collect it without blocking.

## 6. Proposed deferrals (PROVISIONAL, need user sign-off; none is a Tier 1–3 clause of these cards)

* **D-C1: Drana's Flying is inert.** The opponent never blocks, and no card in this list reads flying.
* **D-C2: Socialite's Menace is inert**, for the same reason.
* **D-C3: CR 614.12 simultaneous entry.** A Vampire entering at the same time as Socialite should not get
  the static's counter. The engine enters permanents one at a time. Inert here: no card puts two Vampires
  onto the battlefield in one event, and the two Bloodghast landfall returns are separate triggers.
* **D-C4: No priority window between the first-strike and regular damage steps.** This is a timing gap of
  the engine and of Aether Vial / madness, not a clause of these three cards. The faithful line is: Drana
  connects in step 1, the opponent has now lost life, then Aether Vial (at 2) puts in Vampire Socialite (or
  Stromkirk Condemned discards a madness Socialite under Falkenrath Gorger). Socialite's ETB then pumps every
  other Vampire before the regular step. It needs Drana (1 copy) + Socialite (1 copy) + the enabler + no
  Captain attacking, so it is about ≤1% of games. It is buildable at Tier 3 as a searched in-combat window,
  on the Khalni combat-crack pattern (`FireCombatLandSearchCracks` + a `Plan` pin + the `dig` decision).
  Recommendation: defer, and reopen if the claude-play sweep or references show it. The user's call.
  Post-damage windows (end of combat, the opponent's end step) are equivalent to main 2 in a goldfish and
  are covered.
* Disclosures, not deferrals:
  * Hasty bodies cast this turn (Bloodghast) are credited by `ProjectPlanWalk` without Drana's +1.
    This is a conservative under-credit.
  * A Jitte bearer in the split path keeps the collapsed closed form. Drana + Jitte is in no deck.
  * Equipment/aura/Rick first-strike grants have no `CreatureHasFirstStrike` reader yet.

## 7. Unit-test plan (doctest, `test/unit/test_vampires_combat.cpp`; shape of `test_battle_cry.cpp`)

1. **Drana splits:** Drana + two 2/2 Vampires (no FS, not sick), no lords. Realized
   `ResolveCombatDamage` damage = 2 + 3 + 3 = 8. Each of the three has one +1/+1 counter.
   `TurnSolver::ProjectedAttackDamageForTest` == 8 (lockstep). `RolloutSimulateCombat` gives the same
   opponent life.
2. **Captain makes it degenerate:** Drana + Captain + one 2/2 Vampire. All strike first. Damage = Drana
   2+1, Captain 2, Vampire 2+1 = 8. Counters on all three. Projection == 8 (addend 0). The next combat
   reads the counters.
3. **Drana sick or absent:** no counters, damage unchanged. Drana attacking alone gets her own counter and
   deals 2.
4. **Byte-identity guard:** a double-strike board (Swiftblade Vindicator or a Thrumming Hivepool Sliver)
   with no Drana gives exactly the pre-change totals and per-attacker descs. Also run the full existing
   unit suite plus smoke digests.
5. **Mavren:**
   * One nontoken Vampire attacks: exactly 1 token, 1/1 W Vampire with Lifelink, untapped, `is_token`,
     `entered_this_turn`, NOT in `atk_idx`, no damage.
   * Three nontoken Vampires attack: still 1 token.
   * Only a Vampire TOKEN attacks: 0.
   * Sick Mavren while another Vampire attacks: 1.
   * `opponent_heads = 2`: 1.
   * Both combat worlds create it before the Adeline block (an Adeline on board: her Human attacks, the
     Mavren token does not).
6. **Socialite ETB:**
   * Flag false: no counters.
   * Flag true: +1 on each other Vampire (tokens included), not on itself, not on a non-Vampire creature,
     not on the opponent's creatures.
   * A Thalia's Lieutenant board is unchanged (regression of the shared block).
7. **Socialite static:**
   * Flag true: a `CreateToken` Vampire enters 2/2, a non-Vampire token enters 1/1, Socialite itself gets
     none from its own static.
   * Flag false: none.
   * Mavren token while Socialite is out, flag false at declare-attackers: 1/1 (no counter). Flag set
     earlier: 2/2.
8. `GoldFishRunner::DeckUsesSecondMain` is true for a decklist holding only Socialite (no Baron), and only
   Mavren. It is false for a vanilla list.
9. Stage 5 (not unit): the fd-diverge / nonconv harnesses on a Drana-heavy sample (executor vs rollout).
   Also the 5h viewer surface check (no new type expected).

## 8. Risks

1. **Cluster B models Stromkirk Captain's first strike as inert** (the standing Field Marshal / Knight
   Exemplar precedent). Drana's counters would then be over-credited in BOTH the realized combat and the
   projection. Lockstep means no harness flags it, and the deck simply reads better than it is. The
   integrator must wire `grants_first_strike` into `CreatureHasFirstStrike` before any measurement.
2. **GT movement for other decks from the `ResolveCombatDamage` edit.** The combat core runs for every deck
   in both worlds. Reordering any side effect in the existing loop would shift digests suite-wide: Jitte
   counters, `pending_lifelink`, Maelstrom's free-cast bank, the tap, the lost-life flag, the descs. Keep
   the current loop as the untouched `else` branch, and gate the split on `!bs.fs_dmg_counters.empty()`.
   The other edits are byte-identical by param-gating: GatherBoardSources (two new lists + `OnErase`), the
   projection addend, the Lieutenant/Giada/Mimic blocks, and `DeckUsesSecondMain`.
   * Decks that must stay byte-identical include every first/double-strike holder: WhiteKnights, Knights,
     Soldiers, Minotaur (Neheb, verified exact), Goblins (Chainwhirler + Lackey), Colossus Hammer
     (Swiftblade / Lizard Blades), Bogles/Auras (umbras), KittyEquipment.
   * Also every combat-damage-trigger deck: Goblins (Lackey), Fungus (Shroofus), Minotaur (Neheb),
     Maelstrom Archangel.
   * And the Giada (Angels), Mimic (Pirates) and Lieutenant (Soldiers) enter-counter decks.
   * Gate: unit suite + smoke byte-identity before integration lands.
3. **Misroute / timing traps if a shortcut is taken.**
   * Reusing `etb_each_other_own_creature_counters` routes Vampires to SoldiersProvider.
   * Reusing `attack_per_matching_creates_tokens` routes it to DragonsProvider, and also creates Mavren's
     token after damage, where Socialite would wrongly pump it.
   * Reusing `attack_creates_tokens` makes the token attack and enters it in the projection.
   * Run `scripts/provider_audit.py` after integration.

   Secondary risks:
   * **Main-2 cost.** The second main is a real search cost (1.6x–4.25x on other decks). It is already
     forced for this deck by Baron's convoke, so the new clauses add nothing. The deck must still clear the
     suite 3x cost gate.
   * **`SkipsUnproductiveSecondMain`.** If a Vampires provider ever opts in, the predicate
     (TurnSolver.cpp:5759-5765) must also treat "combat flipped `opponent_lost_life_this_turn` while a
     lost-life-gated card is castable or activatable" as productive. Otherwise it deletes the Socialite and
     Pit Fighter main-2 lines.

## 9. USER-owned items surfaced (not decided here)
* Any Vampires **cast order / main-phase classification** is the user's. As input:
  * Socialite reads BOTH by the USER's rule ("does it help the attack?"). It is MAIN2 for its own ETB, but
    it can feed the attack pre-combat as convoke fuel or as Keeper's fifth Vampire.
  * Mavren reads MAIN1/BOTH: cast pre-combat, she makes a token THIS combat.
  * Captain with Drana is a genuine either-main decision.
* The INERT_PARAMS classifications and the D-C1..D-C4 deferrals need user sign-off.
