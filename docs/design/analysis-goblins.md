# Analysis ledger — Goblins

Per-deck durable state for the `analyze-deck` workflow (survives compaction / handoff).
Deck: `decks/Goblins/Goblins.cod`. Branch: `phase-1-2-deck-analyzer`.

## Deck list (25 distinct)
Muxus Goblin Grandee ×3, Rundvelt Hordemaster ×3, Twinshot Sniper ×1, Siege-Gang Commander ×4,
Goblin Lackey ×4, Goblin Piledriver ×4, Goblin Matron ×2, Goblin King ×2, Goblin Chieftain ×2,
Lightning Bolt ×2, Mogg War Marshal ×1, Goblin Warchief ×1, Mountain ×21, Aether Vial ×2,
Skirk Prospector ×2, Goblin Chainwhirler ×1, Stingscourger ×1, Krenko Mob Boss ×1,
Three Tree City ×1, Cavern of Souls ×1, Pashalik Mons ×1.
Sideboard (not analyzed): Experimental Frenzy, Lightning Bolt.

## Stage 1 — Coverage (2026-07-28)
Already covered: Lightning Bolt (full), Mountain (full), Aether Vial (full), Cavern of Souls (full).
**17 missing**, all Goblin cards + 1 land:
Muxus, Rundvelt Hordemaster, Twinshot Sniper, Siege-Gang Commander, Goblin Lackey, Goblin Piledriver,
Goblin Matron, Goblin King, Goblin Chieftain, Mogg War Marshal, Goblin Warchief, Skirk Prospector,
Goblin Chainwhirler, Stingscourger, Krenko Mob Boss, Three Tree City, Pashalik Mons.

## New engine infrastructure this deck needs (design in progress)
- ETB create-N-tokens (fixed): Siege-Gang (3× 1/1), Mogg War Marshal (1×). *(no existing ETB-flat-token param)*
- Death trigger: goblin dies → deal 1 dmg (Pashalik Mons); → make token (Rundvelt Hordemaster).
- Sacrifice-a-Goblin outlets: → add {R} (Skirk Prospector); → deal 2 any target (Siege-Gang, Pashalik).
- Tap → make X tokens (X = #Goblins): Krenko.
- Subtype tutor to hand: Goblin Matron (search Goblin card → hand). *(existing tutor is by card TYPE)*
- Cost reducer by subtype + haste grant: Goblin Warchief (Goblins cost {1} less, have haste).
- Combat pump per other attacking matching creature: Goblin Piledriver (+2/+0 each other attacking Goblin).
- Cheat-into-play on combat damage: Goblin Lackey (put a Goblin permanent from hand into play).
- Reveal top N, put matching (MV≤5) onto battlefield: Muxus.
- ETB ping each opponent creature + player: Goblin Chainwhirler (1 dmg).
- Channel (discard from hand → deal 2): Twinshot Sniper.
- Echo: Mogg War Marshal, Stingscourger.
- Lord + haste: Goblin Chieftain (existing grants_haste + lord_effect).
- Lord + mountainwalk: Goblin King (mountainwalk evasion inert vs passive opp; +1/+1 modelled).
- ETB bounce opponent creature: Stingscourger (mostly inert vs passive opp).

## Stage 2 research — collected drafts (6/8 families in)

### Tier 1 (cards.json only, existing params)
- **Goblin King** {1}{R}{R} 2/2 — lord_effect Goblin +1/+1, lord_excludes_self. Mountainwalk INERT (evasion vs non-blocking opp).
- **Goblin Chieftain** {1}{R}{R} 2/2 Haste — lord_effect Goblin +1/+1 + grants_haste + lord_excludes_self.

### Tier 2 (one small new param each)
- **Goblin Warchief** {1}{R}{R} 2/2 (Goblin Warrior) — lord_effect + grants_haste + NEW `reduces_spell_subtype:"Goblin"` (subtype twin of reduces_spell_color; mirror at all reduces_spell_color sites). Goblins cost {1} less, have haste.
- **Goblin Piledriver** {1}{R} 1/2 (Goblin Warrior, Protection) — NEW `attack_pump_power_per_other_matching:2` over subtypes_affected=["Goblin"] (mirror attack_trigger_life_loss scan, self-excluded, +power at declare-attackers both worlds). Protection-from-blue INERT.
- **Krenko, Mob Boss** {2}{R}{R} 3/3 Legendary (Goblin Warrior) — NEW tap-activated (no mana) `tap_creates_tokens_per_controlled_subtype:"Goblin"` + tap_created_token_power/tough/subtypes. X = #Goblins at resolution (incl. self+tokens). New Action::Kind. Summoning-sick gating via CanTap.
- **Skirk Prospector** {R} 1/1 — NEW `sac_subtype_for_mana_amount:1`+`_color:"R"`+`_subtype:"Goblin"`. No-tap, repeatable, sac any Goblin (incl self) → add {R}. New Action::Kind (contrast Lotus `sac_for_mana_amount`=tap+sac-self).

### Tier 3 (new engine machinery)
- **Mogg War Marshal** {1}{R} 1/1 (Goblin Warrior, Echo) — NEW `etb_self_creates_tokens:1` (+ reuse etb_created_token_*), NEW `death_creates_tokens:1` (+death_token_*), NEW `echo_cost:"{1}{R}"` (upkeep pay-or-sac decision). Not paying echo → death token (net same body, saves mana).
- **Siege-Gang Commander** {3}{R}{R} 2/2 — NEW `etb_self_creates_tokens:3`, NEW sac-outlet `sac_damage_cost:"{1}{R}"`+`sac_subtype_damage:2`+`sac_damage_requires_subtype:"Goblin"`, targeting Any. Repeatable sac-a-Goblin → 2 dmg (face in goldfish) = burn engine. New Action::Kind.
- **Goblin Matron** {2}{R} 1/1 — reuses tutor_to_hand + tutor_types:["Goblin"] (subtype match via CardMatchesTypeName fallback) + tutor_shuffle_after, BUT needs NEW **ETB-tutor dispatch** (PerformTutor currently spell-only; wire at creature-ETB in executor + rollout). Tutor target = search/viewer decision.
- **Goblin Lackey** {R} 1/1 — oracle is "deals damage to a player" (modern; no "combat"). NEW `combat_damage_puts_subtype_from_hand:["Goblin"]` — combat-damage trigger → put a Goblin permanent from hand onto battlefield (shared enter cascade). **Needs DeckUsesSecondMain += this flag** (2c-bis resource-in-combat). Bucket-B viewer chooser (which Goblin / decline).
- **Muxus, Goblin Grandee** {4}{R}{R} **4/4 Legendary (Goblin Noble), NO Menace** — NEW `etb_reveal_count:6` + `etb_reveal_put_subtypes:["Goblin"]` + `etb_reveal_put_creatures_only:true` + `etb_reveal_put_max_mv:5` (reveal top 6, put Goblin creatures MV≤5 onto bf via shared cascade, rest to bottom), NEW `attack_self_pump_per_other_subtype:"Goblin"`+power/tough:1 (attack +1/+1 per other Goblin). Not second-main-relevant. No viewer choice (puts ALL matching).

### Tier 3 (cont.) — damage/channel/death families
- **Goblin Chainwhirler** {R}{R}{R} 3/3 First strike (Goblin Warrior) — NEW `etb_damage_each_opponent:1` (ETB 1 to opp face + each opp creature/pw; face is race-relevant, AoE only matters vs spawn tokens). First strike INERT.
- **Twinshot Sniper** {3}{R} **2/3 Artifact Creature** (Goblin Archer), Reach+Channel — NEW `etb_damage_any:2` (ETB 2 to face) + NEW `channel_cost:"{1}{R}"`/`channel_damage:2` (from-HAND discard-activated burn = new hand-action). Reach INERT.
- **Stingscourger** {1}{R} 2/2 (Goblin Warrior), Echo {3}{R} — ETB bounce opp creature = GOLDFISH-INERT; Echo {3}{R} = pay-or-sac upkeep (model or defer, USER DECISION).
- **Pashalik Mons** {2}{R} 2/2 Legendary (Goblin Warrior) — NEW death trigger `dies_trigger_subtype:"Goblin"`+`_includes_self:true`+`dies_trigger_damage:1` (per Goblin death incl. own → 1 to face); NEW sac-outlet `{3}{R}` sac-a-Goblin → create TWO 1/1 Goblins (NO damage rider).
- **Rundvelt Hordemaster** {1}{R} 1/1 (Goblin Warrior) — lord_effect Goblin +1/+1 (lord_excludes_self) + NEW death-triggered impulse-exile (`dies_trigger_impulse_exile`: exile top on Goblin death; if Goblin creature, castable until end of NEXT turn). Lord is immediate/faithful; impulse-exile is the complex edge.
- **Three Tree City** Legendary Land — `{T}: Add {C}` faithful (produces ["C"]); clause 3 `{2},{T}: add N colored = creatures of chosen type` = board-scaled ramp/fixing, NOT modelled by default (under-rates as colorless-only). USER DECISION. ETB type-choice simplified to any-creature (Cavern precedent).

## CRITICAL STRUCTURAL INSIGHT (death-trigger agent)
Against the **passive goldfish opponent, our Goblins never die in combat** — deaths occur ONLY via the sacrifice outlets (Skirk Prospector, Siege-Gang, Pashalik, and Mogg-War-Marshal-lets-echo-lapse). So the death-trigger engine (Pashalik ping, Rundvelt impulse, Mogg/Rundvelt death tokens) is productive ONLY if the sac outlets are modelled. Build death-triggers + creature-sac-outlets as ONE coordinated subsystem, not per-card.

## Proposed deferrals — NEED USER APPROVAL (per skill 2a)
- Inert keyword/ability collapses vs passive opponent (standard): Goblin King mountainwalk, Piledriver protection-from-blue, Chainwhirler first strike, Twinshot reach, Stingscourger ETB bounce. Token "red" color unmodelled (nothing keys on it).
- Echo (Mogg War Marshal, Stingscourger): model as upkeep pay-or-sac decision, or defer as vanilla body (over-rating).
- Three Tree City clause 3 (board-scaled colored mana): implement or defer (under-rate as {C}-only).
- Rundvelt clause 2 impulse-exile, Pashalik sac-outlet: build now or defer edges.

## KEY INTEGRATION RISKS (found during infra survey)
1. **Provider misrouting:** `SelectDecisionProvider` sets `anti=true` if `p.tutor_to_hand` → Goblin Matron would route the whole deck to **AntiLifegainProvider**. MUST guard: detect Goblins (by a goblin-specific param) and route to Generic (or a new GoblinProvider) BEFORE the anti check, OR gate the anti tutor-signal. Same-shape block at DecisionProviders.cpp ~2278 and GoldFishRunner DeckUsesSecondMain.
2. **etb_self_creates_tokens** shared by Mogg War Marshal + Siege-Gang; reuse existing etb_created_token_power/tough/subtypes (Lathliss) — a card sets exactly one ETB-token gate, no conflict.
3. **New Action::Kind** values needed: SacGoblinForMana (Skirk), SacGoblinForDamage (Siege-Gang, Pashalik), TapForTokens (Krenko). Model on SacForMana precedent (TurnSolver.h Kind enum). Each needs: enumeration in CollectActions, cost/effect in apply_one (rollout) + executor, plan_signature inclusion.
4. **Death-trigger machinery** does not exist — CheckStateBasedActions (GameEngine.cpp:602) moves dead creatures to graveyard with no trigger hook. Need a "goblin died" event fired from BOTH the executor SBA and the rollout death path, driving death_creates_tokens (Rundvelt, Mogg) + death-damage (Pashalik). This is the biggest new subsystem.
5. **Echo** (Mogg War Marshal, Stingscourger) = new upkeep pay-or-sac decision.
6. **Second main** for Goblin Lackey via DeckUsesSecondMain extension.

## Implementation progress (serial integration)
- [x] CardParams fields added (CardDatabase.h) — full Goblins block. **Compiles clean (build exit 0).**
- [x] BuildParamsFromJson reads added (CardDatabase.cpp). Compiles clean.
- [x] reduces_spell_subtype (Warchief) — DONE, builds clean. Wired: TurnSolver EffectiveCost subtype block; AIEngine EffectiveCost copy; SameTurnReducerGenericCredit (with self-exclusion — Warchief is a Goblin); CheckLine in-order walk (sub_reducers); GenericProvider::CastOrderRank rank 8 (before creatures). All gated on non-empty reduces_spell_subtype.
- [x] ETB effects — DONE, builds clean. New `OnGoblinEnters()` + `PerformMuxusReveal()` in SpellEffects.h; called from EffectHandler::EnterBattlefield (executor, passes entry.tutor_target) + TurnSolver apply_one creature-enter (rollout, passes tutor_target). Handles etb_self_creates_tokens, etb_damage_any + etb_damage_each_opponent (face via life-loss; opp creatures pinged+pruned inline), Matron ETB tutor (PerformTutor), Muxus reveal-6-put-creatures-MV≤5 (via DrawN, rest to bottom, each put fires its own OnGoblinEnters cascade). NOTE refinement: Matron search-branching over tutor target relies on existing tutor_to_hand CollectActions enumeration — verify composes in Stage 5.
- [x] Death-watcher engine — DONE, builds clean. `OnCreatureDies(state, controller, dead_card)` in SpellEffects.h: scans other in-play watchers (subtype match) + the dead creature's own watcher (includes_self); applies dies_trigger_damage (face), dies_trigger_creates_tokens, dies_trigger_impulse_exile (stage top if type+subtype match, expiry turn+1). Wired into GameEngine::CheckStateBasedActions (collects deaths, fires AFTER erase loop to avoid iterator invalidation from token creation). PRIMARY death path = the sac outlets (next) which will call OnCreatureDies directly in both worlds. Documented approximation: simultaneous multi-death (unreachable in goldfish).
- [~] Sac-outlet subsystem — PARTIAL (this is the hard serial piece). DONE: Action::Kind values (TapForTokens, SacCreatureOutlet, Channel) + sac_victim_id field in TurnSolver.h; shared apply helpers in SpellEffects.h (CountControlledSubtype, ApplyTapForTokens, ApplySacCreatureOutlet [sacs victim→payload+OnCreatureDies], ApplyChannel); Krenko enumeration in CollectActions; Krenko rollout apply (trailing pass after apply_plan_actions, so X counts developed board); plan_signature cases for all three kinds.
  REMAINING (before Stage 5 / regression baseline):
  1. [x] **Executor Krenko apply** — DONE, builds clean. Trailing TapForTokens pass in AIEngine::TakeTurn at the mirrored post-cast point (before deferred Karoo, ~line 2448), lockstep with rollout ApplyPlanDirect ~5407. Krenko now fires identically in both worlds.
  2. [x] **Costed outlets** (Siege-Gang damage / Pashalik tokens) + **Channel** — DONE. Enumerated with real mana cost; apply pays via TapForCostDirect (rollout) / BuildAvailableMana+TapForCost (executor) in a trailing pass; stranded=no-op both worlds (no phantom fd-diverge). **HANG FIX**: bounded to ONE heuristic victim per outlet (token first / weakest / source last) — one-action-per-victim exploded the O(2^n) subset search. Commits cd47f29, 33715bd.
  3. [x] **Skirk mana outlet** (sac Goblin → {R}) — DONE. Emitted as a SacForMana action (reuses subset credit / BatchPrepay decline / pre-cast float / plan signature); ApplySacForMana gained a victim_id param (source stays, victim sacrificed). Lotus victim_id=0 byte-identical. Commit ad59f2f. fd-diverge + nonconv clean.

## ENGINE COMPLETE (functionally). Smoke: d3 seed2002 = 4.80; d5 seed4004 = 4.80; fd-diverge=0, nonconv=0 on samples.
Remaining refinements (documented, non-blocking): multi-sac-for-lethal as a searched COUNT (Siege-Gang saccing the whole swarm) — currently one sac/activation; viewer bucket-B wiring (Lackey put-from-hand chooser); field-audit snapshot (was 429-throttled).
- [ ] Echo upkeep pay-or-sac (needs Permanent flag entered-since-last-upkeep + upkeep step hook).
- [ ] Combat pumps: attack_pump_power_per_other_matching (Piledriver), attack_self_pump_per_other_subtype (Muxus) at declare-attackers both worlds.
- [ ] Goblin Lackey combat-damage-cheat + DeckUsesSecondMain extension + viewer bucket-B chooser.
- [ ] Channel (Twinshot from-hand action).
- [ ] Three Tree City scaled mana ({2} feeder → N colored = creatures).
- [ ] Provider routing guard (Matron tutor_to_hand must NOT route deck to g_antilife).
- [ ] Viewer wiring (Lackey put-from-hand; sac-outlet target; Krenko/echo choices).
- [ ] cards.json entries (17) + audits + coverage + profile + Stage 5.

## Parallel workflow (wf_e288902a-c63) — INTEGRATED 2026-07-29
All 5 worktree agents built clean; diffs applied via `git apply --3way` onto the Krenko commit (no conflicts); combined tree builds clean; coverage `missing:[]`; deck runs (3 games d3 → ~4.3 avg win turn). Patches saved under logs/goblins_patches/.
- [x] Echo (Permanent::echo_resolved + upkeep pay-or-sac in AIEngine::TakeTurn + rollout SimulateEndAndStartNextTurn; Mogg declines→death token, Stingscourger pays-or-sacs).
- [x] Combat pumps (Piledriver attack_pump_power_per_other_matching; Muxus attack_self_pump) + Goblin Lackey combat-damage cheat + DeckUsesSecondMain extension.
- [x] Three Tree City scaled mana ({2},{T} → N colored = creatures of type).
- [x] Provider routing guard (Goblins → GenericProvider before anti; Matron tutor no longer misroutes).
- [x] cards.json — all 17 entries (costs Scryfall-verified; param keys match parser).

## Status
- [x] Stage 1 coverage
- [x] Stage 2 research (fan-out) — 8 agents, authoritative Scryfall drafts collected
- [x] Stage 3 coverage clean (missing:[])
- [~] Stage 2 integration — 8 subsystems in; REMAINING: costed sac outlets (Siege-Gang/Pashalik/Channel) + Skirk mana + executor Krenko apply; viewer bucket-B wiring
- [ ] Stage 2d/2d-bis audits (cost audit running)
- [ ] Stage 4 baseline profile
- [ ] Stage 5 verify
- [ ] Stage 2d / 2d-bis audits
- [ ] Stage 3 coverage clean
- [ ] Stage 4 baseline profile
- [ ] Stage 5 verify
- [ ] Stage 6 report

## User decisions (2026-07-28)
1. **Full faithful build** — implement every clause including Rundvelt impulse-exile, Pashalik sac-outlet, Three Tree City scaled mana. No feature deferrals.
2. **Model echo faithfully** — upkeep pay-{cost}-or-sacrifice decision (Mogg War Marshal, Stingscourger).
3. **Implement Three Tree City clause 3** — board-count-scaled colored mana ({2} feeder → N of chosen color = creatures of chosen type).
4. **Inert collapses approved** — mountainwalk (Goblin King), protection-from-blue (Piledriver), first strike (Chainwhirler), reach (Twinshot), Stingscourger ETB bounce; token "red" color unmodelled. All disclosed as cosmetic vs the passive opponent; bodies + all race-relevant effects fully modelled.

## Approved deferrals
None (full faithful build). Inert-collapse disclosures above are cosmetic, not feature deferrals.
