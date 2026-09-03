#include "EffectHandler.h"
#include "SpellEffects.h"
#include "RolloutTouch.h"
#include <algorithm>

// Execution-trace instrumentation sink (off by default -> byte-identical). See RolloutTouch.h.
namespace rollout_touch { thread_local Sink* g_sink = nullptr; }

// ---- Helpers ----

void EffectHandler::EnterBattlefield(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    rollout_touch::Record(entry.source.m_name.str());   // execution-trace: permanent's code runs on the battlefield
    Permanent perm;
    perm.card              = def.card;
    perm.card.m_number     = entry.source.m_number;  // preserve per-copy ID from cast
    perm.controller_index  = entry.controller_index;
    perm.owner_index       = entry.controller_index;
    perm.entered_this_turn = true;
    // Sakashima's Protege: "You may have this creature enter as a copy of any permanent that
    // entered this turn." Replace the entering card with the chosen source's PRINTED card
    // (CR 706.2 copiable values -- no counters/attachments/temp pumps), keeping this cast's
    // m_number. The COPIED definition then drives enters-tapped/loyalty and, below, the enter
    // triggers -- so a copy of Breaching Dragonstorm re-fires its exile trigger. Lockstep twin:
    // apply_one's creature-enter branch makes the identical swap.
    const CardDefinition* edef = &def;
    if (def.params.enter_as_copy_of_entrant)
    {
        // The searched pick rides entry.enchant_target (reused as the copy-source m_number, the
        // aura/trick precedent; -1 = searched decline, 0 = heuristic).
        const int src_bi = ChooseCopyEntrantIndex(state, entry.controller_index,
                                                  entry.enchant_target, def);
        if (src_bi >= 0)
        {
            perm.card          = state.battlefield[src_bi].card;
            perm.card.m_number = entry.source.m_number;
            const CardDefinition* cd = CardDatabase::Instance().LookupCached(perm.card);
            if (cd) { edef = cd; }
        }
    }
    // Fire Diamond: "This artifact enters tapped." enters_tapped was previously honored only on the
    // land-drop path, so a CAST permanent carrying it entered untapped and was tappable for mana
    // the same turn. Lands are played, not cast, so they never reach here -- no double-apply.
    // Lockstep with TurnSolver's rollout non-creature enter branch.
    if (edef->params.enters_tapped) { perm.tapped = true; }
    // Planeswalker: enters with its starting loyalty (dedicated int + a display-mirror counter
    // the existing viewer badge code picks up). Mirrors the rollout's non-creature enter branch.
    if (edef->params.loyalty_start > 0)
    {
        perm.loyalty = edef->params.loyalty_start;
        perm.counters.push_back(Counter{Counter::Type::Loyalty, edef->params.loyalty_start});
    }
    state.battlefield.push_back(perm);

    // Dragonstorm kill-engine (executor side): a Dragon entering fires the shared cascade --
    // Scourge's ETB ping + Lathliss's 5/5 token. No-op for every non-Dragon permanent (early
    // subtype return) so all other decks are byte-identical. Mirrors the rollout's creature-enter
    // site in TurnSolver::ApplyPlanDirect (lockstep).
    FireEtbWatchers(state, entry.controller_index, static_cast<int>(state.battlefield.size()) - 1);
    // Goblins tribal ETB cascade (self-tokens / ETB burn / Matron tutor / Muxus reveal). No-op for
    // every non-Goblin permanent (early param return). entry.tutor_target carries a search/human
    // Goblin Matron fetch target (empty -> the provider's pick).
    FireOwnEtbTriggers(state, entry.controller_index, static_cast<int>(state.battlefield.size()) - 1,
                   entry.tutor_target, entry.chosen_x.value_or(-1));
}

void EffectHandler::MoveToGraveyard(GameState& state, const StackEntry& entry)
{
    // A demonstrate COPY was never a card: it ceases to exist on resolution (CR 707.10),
    // leaving nothing in any zone.
    if (entry.is_copy) { return; }
    // "Exile Living Wish": a self-exiling spell goes to EXILE instead. LOCKSTEP with the rollout's
    // instant/sorcery line in TurnSolver's apply_one. See CardParams::exiles_self_on_resolve for
    // why this is implemented rather than bracket-noted as inert.
    const CardDefinition* d = CardDatabase::Instance().LookupCached(entry.source);
    if (d != nullptr && d->params.exiles_self_on_resolve)
    { state.exile.push_back(entry.source); return; }
    state.players[entry.controller_index].graveyard.push_back(entry.source);
}

// ---- Public dispatch ----

bool EffectHandler::Resolve(GameState& state, const StackEntry& entry, const CardDefinition& def)
{
    // Real-stack trigger entries (cascade / Breaching Dragonstorm ETB / demonstrate) dispatch
    // to their own resolvers -- they are abilities, not the spell itself, so none of the
    // spell machinery below (legend rule, graveyard placement inside ResolveImpl) applies.
    if (entry.type == StackEntry::EntryType::Triggered
        && entry.trigger_kind != StackEntry::TriggerKind::None)
    {
        return ResolveTriggered(state, entry, def);
    }

    const bool ok = ResolveImpl(state, entry, def);
    // Legend rule (CR 704.5j) is a STATE-BASED ACTION: it is checked the moment the duplicate is on
    // the battlefield, NOT deferred. The executor used to enforce it only for Vial-deployed
    // creatures (AIEngine) and at the start of combat (GameEngine::CombatPhase), so a legendary CAST
    // from hand sat beside its twin for the whole main phase -- and anything that counts permanents
    // or triggers off them saw two. The rollout (TurnSolver::ApplyPlanDirect) always enforced it on
    // entry, so this was a rollout/executor DIVERGENCE, not just a rules gap.
    //
    // The pre-fix window ran from the duplicate ENTERING to the next begin-combat step -- those were
    // the only two executor sweep sites. For a duplicate entering in the second main that spans the
    // opponent's turn AND the whole of the next main phase. It could never ATTACK (the sweep precedes
    // DeclareAttackers), which is the one outcome the old placement did prevent; everything else --
    // static abilities, on-enter and on-cast triggers, anything counting permanents -- saw two.
    //
    // Measured on Auras seed 4227 gi223 (per-phase battlefield, pre-fix binary): a second
    // Light-Paws, Emperor's Voice cast on T3 left BOTH copies (#38, #39) on the board for the rest
    // of that main phase, and combat swept it back to one. Spirit Link therefore resolved with two
    // Light-Paws out and its Aura-tutor trigger fired TWICE, fetching an extra Aura and shuffling
    // the library an extra time; the realised draws then stopped matching the line the search had
    // proved (predicted win T4, realised T5 -- now T4). The same window let two Hinata,
    // Dawn-Crowned double-count a static cost discount, so the executor cast a Reality Spasm it
    // could not afford.
    //
    // NOTE if a legend-rule-off effect is ever added (Mirror Gallery), or a name-changing legend
    // (Sakashima): no such card exists in cards.json today, so enforcement is unconditional here.
    // Adding one means gating EVERY EnforceLegendRule site, not just this one.
    //
    // Placed AFTER the dispatch so it runs after the permanent has entered and its ETB effects have
    // resolved (dig, enter-watcher cascade), which is exactly the rollout's ordering -- and after, not
    // before, so an ETB dig's `battlefield.back()` self-pointer is never invalidated underneath it.
    // No-op for every deck with no legendary permanent.
    if (def.card.HasSupertype(Supertype::Legendary)
        // ...or the spell may have ENTERED as a legendary (Sakashima's Protege copying
        // Maelstrom Wanderer): the entering card is the copy's, not def's, so gate on the
        // param too. EnforceLegendRule is a no-op without a duplicate.
        || def.params.enter_as_copy_of_entrant)
    {
        EnforceLegendRule(state, entry.controller_index);
    }

    // Drain pending enter triggers (Breaching Dragonstorm's exile-until-nonland) recorded by
    // FireOwnEtbTriggers during this resolution, as Triggered stack entries -- LIFO, so they
    // resolve next, after the permanent has entered and the legend rule has run (the trigger
    // fires even if the permanent itself was legend-ruled away, CR 603.3). Reversed push so
    // multiple simultaneous triggers resolve in the order they fired.
    if (!g_pending_etb_free_casts.empty())
    {
        std::vector<PendingEtbFreeCast> pend;
        pend.swap(g_pending_etb_free_casts);
        for (auto it = pend.rbegin(); it != pend.rend(); ++it)
        {
            StackEntry te;
            te.type             = StackEntry::EntryType::Triggered;
            te.trigger_kind     = StackEntry::TriggerKind::EtbExileFreeCast;
            te.source           = it->source;
            te.controller_index = it->controller;
            state.stack.push_back(std::move(te));
        }
    }
    return ok;
}

bool EffectHandler::ResolveImpl(GameState& state, const StackEntry& entry, const CardDefinition& def)
{
    // Execution-trace: this card's effect is about to run -> record it as touched (no-op when off).
    rollout_touch::Record(entry.source.m_name.str());
    switch (def.tmpl)
    {
        case CardTemplate::BasicLand:
        {
            // Lands do not use the stack (CR 305.1) — should not appear here.
            // If they do, enter the battlefield gracefully.
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::VanillaCreature:
        {
            ResolveVanillaCreature(state, entry, def);
            return true;
        }

        case CardTemplate::ManaDork:
        {
            ResolveManaDork(state, entry, def);
            return true;
        }

        case CardTemplate::DirectDamage:
        {
            ResolveDirectDamage(state, entry, def);
            return true;
        }

        case CardTemplate::CounterSpell:
        {
            ResolveCounterSpell(state, entry, def);
            return true;
        }

        case CardTemplate::Removal:
        {
            ResolveRemoval(state, entry, def);
            return true;
        }

        case CardTemplate::DrawSpell:
        {
            ResolveDrawSpell(state, entry, def);
            return true;
        }

        case CardTemplate::DrawX:
        {
            ResolveDrawX(state, entry, def);
            return true;
        }

        case CardTemplate::PumpSpell:
        {
            ResolvePumpSpell(state, entry, def);
            return true;
        }

        case CardTemplate::LordEffect:
        {
            // Lord effects are static, not triggered — handled by the layer system.
            // The permanent still enters the battlefield.
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::Haste:
        {
            EnterBattlefield(state, entry, def);
            return true;
        }

        case CardTemplate::DrawUntilNonland:
        {
            ResolveDrawUntilNonland(state, entry, def);
            return true;
        }

        default:
        {
            // Unknown or Tier 3 custom card — no built-in effect.
            // Move non-permanents to graveyard; permanents enter the battlefield.
            if (def.card.IsCreature() || def.card.HasType(CardType::Enchantment)
                || def.card.HasType(CardType::Artifact) || def.card.IsLand()
                || def.card.HasType(CardType::Planeswalker))
            {
                EnterBattlefield(state, entry, def);
                // Aura (Bogles): attach to the searched creature (enchant_target), then fire
                // Light-Paws' tutor-attach. Set aura_attached_to on the JUST-entered permanent
                // (battlefield.back()) BEFORE PerformLightPawsAttach push_backs the fetched aura.
                // Lockstep with the rollout's apply_one enchantment-enter branch.
                if (def.params.is_aura && !state.battlefield.empty())
                {
                    state.battlefield.back().aura_attached_to =
                        ResolveEnchantTarget(state, entry.controller_index, entry.enchant_target,
                                             def.params.is_land_aura);
                    PerformLightPawsAttach(state, entry.controller_index,
                                           def.card.m_mana_cost.ManaValue(), "EXEC");
                }
                // ETB "each opponent gains N life" (Aria of Flame) -> reversed to damage by
                // a Tainted Remedy / Plague Drone via OpponentGainsLife.
                if (def.params.etb_opponent_lifegain > 0)
                {
                    OpponentGainsLife(state, entry.controller_index,
                                      def.params.etb_opponent_lifegain);
                }
            }
            else
            {
                // Non-permanent custom spell: tutor / destroy-all-enchantments / cascade,
                // then graveyard.
                if (def.params.tutor_to_hand || def.params.tutor_to_top)
                {
                    // Fetch the searched target carried on the stack entry (empty -> the
                    // heuristic's top pick), matching the rollout's ApplyPlanDirect.
                    PerformTutor(state, entry.controller_index, def.params, entry.tutor_target,
                                 def.card.m_name);
                }
                // Crop Rotation: search a land card, put it onto the battlefield (the sacrifice-a-
                // land additional cost is handled by the shared sacrifice_land cast path). Target =
                // the searched tutor axis (entry.tutor_target). Lockstep with ApplyPlanDirect.
                if (def.params.tutor_land_to_battlefield)
                {
                    PerformLandTutorToBattlefield(state, entry.controller_index, def.params,
                                                  entry.tutor_target);
                }
                // Dragonstorm (Storm): put min(spells_cast_this_turn, Dragons-in-library) Dragons
                // onto the battlefield, each through the shared FireEtbWatchers cascade (Scourge ping
                // + Lathliss token), then shuffle. spells_cast_this_turn was incremented at THIS
                // spell's cast (CastSpellFromHand, before it went on the stack), so it already counts
                // Dragonstorm itself -> it equals (prior spells cast this turn) + 1 = storm copies +
                // the original = the number of Dragons to put. Lockstep with TurnSolver::apply_one.
                // Turntimber Symbiosis front: look at the top 7, put <= 1 creature from among
                // them onto the battlefield (+3 counters if mv <= 3), rest to the bottom.
                // entry.tutor_target = the searched/human pick ("TURNTIMBER_NONE" = put nothing).
                if (def.params.look_top_put_creature_count > 0)
                {
                    PerformLookTopPutCreature(state, entry.controller_index, def.params,
                                              entry.tutor_target, def.card.m_name.str());
                }
                if (def.params.tutor_to_battlefield_single)
                {
                    // Natural Order: search ONE matching (type + colour) creature and put it onto
                    // the battlefield. The additional-cost sacrifice was paid at cast time
                    // (CastSpellFromHand / ApplyPlan) -- costs precede resolution (CR 601.2h).
                    // entry.tutor_target carries the searched/human pick; empty -> the scripted
                    // tutor pin or the provider's front (inside PerformTutorToBattlefield).
                    std::vector<std::string> pref;
                    if (!entry.tutor_target.empty()) { pref.push_back(entry.tutor_target); }
                    PerformTutorToBattlefield(state, entry.controller_index, def.params,
                                              /*max_puts=*/1, pref, def.card.m_name.str());
                }
                else if (def.params.tutor_to_battlefield)
                {
                    // preferred = {} (empty): put in the provider's TutorCandidates order. Both this
                    // executor and the rollout (apply_one) pass empty, so they put the identical
                    // Dragons in the identical order (lockstep). The DragonstormProvider / viewer
                    // multi-pick (later steps) supply an explicit order via the `preferred` arg.
                    PerformTutorToBattlefield(state, entry.controller_index, def.params,
                                              state.spells_cast_this_turn, /*preferred=*/{},
                                              def.card.m_name.str());
                }
                // Apex of Power: "Exile the top seven cards of your library. Until end of turn, you may
                // cast spells from among them. If this spell was cast from your hand, add ten mana of any
                // one color." Exile impulse_exile cards as STAGED cards playable THIS turn (m_impulse_no_land
                // so their lands are non-playable); then, IFF cast from hand (not off another Apex's exile),
                // float impulse_float_amount of the searched colour. Staged into Player::staged_cards so the
                // AIEngine draw-breakpoint merges them into hand and re-solves to cast them (mirrors the
                // stages_cards DrawSpell). LOCKSTEP with TurnSolver::apply_one's impulse branch.
                if (def.params.impulse_exile > 0)
                {
                    Player& imp = state.players[entry.controller_index];
                    const int expiry = def.params.impulse_expiry_this_turn
                                     ? state.turn_number : state.turn_number + 1;
                    std::vector<Card> exiled = DrawTopAsImpulseStaged(
                        state, entry.controller_index, def.params.impulse_exile, expiry);
                    for (Card& ec : exiled)
                    {
                        StagedCard sc;
                        sc.card        = std::move(ec);
                        sc.expiry_turn = expiry;
                        imp.staged_cards.push_back(std::move(sc));
                    }
                    // "add ten mana of any one color" -- cast-from-hand only (withheld for Apex-off-Apex).
                    if (entry.cast_from_hand && def.params.impulse_float_amount > 0)
                    {
                        AddChosenColorFloat(state, entry.chosen_float_color,
                                            def.params.impulse_float_amount);
                    }
                }
                if (def.params.destroy_all_enchantments)
                {
                    DestroyAllEnchantments(state);
                }
                // Zada/Mirrorwing solo-target trick (Expedite / Fists of Flame / Ancestral Anger /
                // Gold Rush / Scale the Heights / Twinflame): shared resolver -- copy fan-out,
                // escalating payloads, strive extras -- in lockstep with the rollout's apply_one.
                // enchant_target carries the searched creature target (0 = up-to-one, untargeted);
                // soulfire_own_targets is reused as Twinflame's searched strive-extras count.
                if (IsSoloTargetTrick(def.params))
                {
                    ResolveSoloTargetTrick(state, entry.controller_index, def,
                                           entry.enchant_target,
                                           entry.soulfire_own_targets.value_or(0),
                                           entry.chosen_x.value_or(0));
                }
                // Unite the Coalition (user-approved collapse): the searched split S (on
                // chosen_x) of the five mode-picks -> S x damage-per-choice to the opponent
                // face + (N - S) draws. Lockstep with the rollout's apply_one modal branch.
                if (def.params.modal_choose_n > 0)
                {
                    const int sN  = entry.chosen_x.value_or(0);
                    const int dmg = sN * def.params.modal_damage_per_choice;
                    if (dmg > 0)
                    {
                        state.players[1 - entry.controller_index].life -= dmg;
                        state.opponent_lost_life_this_turn = true;
                    }
                    const int draws = (def.params.modal_choose_n - sN)
                                      * def.params.modal_draw_per_choice;
                    Player& mp = state.players[entry.controller_index];
                    std::size_t before = mp.hand.size();
                    std::size_t mp_before = mp.hand.size();
                    for (int k = 0; k < draws && !mp.library.empty(); ++k)
                    { mp.library.DrawN(1, mp.hand); }
                    mp.cards_drawn_this_turn += static_cast<int>(mp.hand.size() - mp_before);
                    if (g_play_draw_sink)
                    {
                        for (std::size_t hi = before; hi < mp.hand.size(); ++hi)
                        { g_play_draw_sink->push_back({ state.turn_number, mp.hand[hi].m_name.str() }); }
                    }
                }
                // (Cascade is no longer resolved here: it is a CAST trigger -- CastSpellFromHand /
                // PushFreeCast push Triggered{Cascade} entries that resolve BEFORE this spell.)
                // Creative Technique: shuffle -> reveal until nonland -> exile it -> bottom the
                // rest -> free-cast (runs for the original AND its demonstrate copy).
                if (def.params.shuffle_reveal_freecast)
                {
                    ResolveShuffleRevealFreecast(state, entry, def);
                }
                // Reality Spasm / Irencrag Feat -- mana RITUAL. On resolution, add its floating
                // mana to the turn-scoped reserve so a later same-turn cast (Crackle) can spend
                // it (the Hinata combo). Modelled as floating, NOT a literal untap, so this stays
                // EXACTLY in lockstep with the planner's ritual credit and the rollout's apply_one.
                if (IsManaRitual(def))
                {
                    // Desperate Ritual SPLICE: float (splice_count+1)*{R}{R}{R} (each spliced copy adds
                    // its own {R}{R}{R}; the copies themselves stay in hand). splice_count unset -> +1
                    // copy (a plain ritual) -> byte-identical for every non-splice deck.
                    ApplyRitualFloat(state, def, entry.chosen_x.value_or(0),
                                     entry.splice_count.value_or(0) + 1);
                }
                MoveToGraveyard(state, entry);
            }
            return true;
        }
    }
}

// ---- Per-template resolvers ----

void EffectHandler::ResolveVanillaCreature(GameState& state, const StackEntry& entry,
                                            const CardDefinition& def)
{
    EnterBattlefield(state, entry, def);

    // ETB library dig (Acclaimed Contender): deterministic, identical to the rollout's
    // ApplyPlanDirect dig, so the realised hand/library matches the searched line. The
    // just-entered creature is the last on the battlefield; pass it as `self` so the
    // "control another Knight" condition excludes it.
    if (def.params.etb_dig_count > 0 && !state.battlefield.empty())
    {
        PerformEtbDig(state, entry.controller_index, def.params, &state.battlefield.back());
    }
}

void EffectHandler::ResolveManaDork(GameState& state, const StackEntry& entry,
                                     const CardDefinition& def)
{
    EnterBattlefield(state, entry, def);
    // Tap ability is handled by AIEngine::TapForCost — no ETB effect needed here.
}

void EffectHandler::ResolveDirectDamage(GameState& state, const StackEntry& entry,
                                         const CardDefinition& def)
{
    // An {X} burn deals its chosen X (carried on the stack entry; CR 202.3). A fixed-damage
    // burn uses params.damage, with the landfall boost if a land entered this turn.
    int damage;
    if (def.card.m_mana_cost.has_x)
    {
        // Crackle with Power: "deals five times X damage" -> chosen_x * x_damage_multiplier.
        int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }
        damage = entry.chosen_x.value_or(0) * mult;
    }
    else
    {
        damage = def.params.damage;
        if (def.params.landfall_damage > 0
            && state.players[entry.controller_index].lands_played_this_turn > 0)
        {
            damage = def.params.landfall_damage;
        }
        // Soulfire Eruption: bounded multi-target dig. Exile + STAGE the top N cards (opponent +
        // opp creatures + self-if-safe), the face takes the highest MV, the controller the lowest.
        // Mirrors ApplyPlanDirect (lockstep). `damage` = face damage, applied to the opp by the
        // target loop below; self-damage is applied here.
        if (def.params.damage_equals_top_mv)
        {
            SoulfireResult sr = SoulfireDig(state, entry.controller_index,
                                            entry.soulfire_own_targets.value_or(0), &def, "exec");
            damage = sr.face_damage;
            state.players[entry.controller_index].life -= sr.self_damage;
        }
    }

    // A scaled divided-damage spell (Magma Opus) commits only the searched face level to the opponent's
    // face (carried on the stack via crackle_targets; the rest was spread onto inert targets to earn the
    // Hinata discount and is not simulated). Unset for a normal cast / the model off -> full damage.
    // Lockstep with the committed cost + ApplyPlanDirect.
    if (def.params.damage_divided && entry.crackle_targets.has_value())
    {
        damage = entry.crackle_targets.value();
    }

    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Player)
        {
            state.players[t.player_index].life -= damage;
            if (t.player_index != entry.controller_index && damage > 0)
            {
                state.opponent_lost_life_this_turn = true;
            }
        }
        else if (t.type == Target::Type::Permanent)
        {
            if (t.permanent_index >= 0
                && t.permanent_index < static_cast<int>(state.battlefield.size()))
            {
                Permanent& target = state.battlefield[t.permanent_index];
                const int before = target.damage;
                target.damage += damage;

                // Delayed "when that creature dies" trigger (Searing Blood), accumulated so two copies
                // on one creature both fire when it dies. See ApplyBurnToCreature (shared lockstep).
                ApplyBurnToCreature(state, target, before, def.params.death_trigger_damage,
                                    entry.controller_index);
            }
        }
    }

    // Crackle with Power: the declared extra targets (creatures/self) take 5X and die (SBA), so a
    // killed creature leaves the target pool for later spells. The opponent FACE damage was applied
    // by the target loop above (Targeting::Any sets only the opp-face target); this adds the extras.
    // Lockstep with ApplyPlanDirect (the rollout resolves the identical set from crackle_targets).
    if (IsCrackleCountSpell(def.params))
    {
        CrackleHitExtraTargets(state, entry.controller_index, damage, entry.crackle_targets.value_or(-1));
    }

    // Rider "target opponent gains N life" (Fiery Justice) -> reversed to damage by a
    // Tainted Remedy / Plague Drone via OpponentGainsLife.
    if (def.params.opponent_lifegain > 0)
    {
        OpponentGainsLife(state, entry.controller_index, def.params.opponent_lifegain);
    }

    // Magma Opus rider: "draw two cards." Drawn to hand on resolution; mirrors apply_one (lockstep).
    if (def.params.cast_draw > 0)
    {
        Player& cp = state.players[entry.controller_index];
        cp.cards_drawn_this_turn += cp.library.DrawN(def.params.cast_draw, cp.hand);
    }

    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveCounterSpell(GameState& state, const StackEntry& entry,
                                         const CardDefinition& def)
{
    // In Phase 1 goldfishing the stack only ever contains the active player's
    // own spells, so CounterSpell has no target to counter.
    // TODO: Phase 2 — counter the top non-counter spell on the stack.
    (void)def;
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveRemoval(GameState& state, const StackEntry& entry,
                                    const CardDefinition& def)
{
    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Permanent
            && t.permanent_index >= 0
            && t.permanent_index < static_cast<int>(state.battlefield.size()))
        {
            // Pump-then-Swords: redirect a free-alt Invigorate onto this creature before
            // capturing its power, so the exile life-loss is +power_bonus larger (autonomous AI
            // only). Shared with the rollout's Removal branch (TurnSolver) for lockstep. Called
            // before taking the `target` reference: FireOnCastTriggers may reallocate the
            // battlefield vector (appending tokens), which would dangle a held reference.
            TryPumpThenSwordsRedirect(state, entry.controller_index, t.permanent_index, def);
            Permanent& target = state.battlefield[t.permanent_index];
            // Tuck removal (Unexpectedly Absent): put the target into its owner's library just
            // beneath the top X cards. A TOKEN (every opponent spawn) ceases to exist instead
            // (CR 111.7) -- faithful, not a simplification; a real card inserts at min(X, size).
            // Attachments fall off. No exile/graveyard, no lifegain rider. Mirrors the rollout.
            if (def.params.tuck_to_library)
            {
                const int  dead_num = target.card.m_number;
                const bool is_tok   = target.is_token;
                const Card tucked   = target.card;
                const int  owner    = target.owner_index;
                state.battlefield.erase(state.battlefield.begin() + t.permanent_index);
                if (!is_tok)
                {
                    Library& lib = state.players[owner].library;
                    const int x   = std::max(0, entry.chosen_x.value_or(0));
                    const int pos = std::min<int>(x, static_cast<int>(lib.size()));
                    lib.insert(lib.begin() + pos, tucked);
                }
                for (Permanent& e : state.battlefield)
                {
                    if (e.equipped_to      == dead_num) { e.equipped_to = 0; }
                    if (e.aura_attached_to == dead_num) { e.aura_attached_to = 0; }
                }
                continue;
            }
            // Rider (Swords to Plowshares): the exiled creature's controller gains life equal
            // to its power. Via OpponentGainsLife so a Tainted Remedy turns the opponent's
            // gain into damage equal to the creature's power. Captured before the erase.
            int tgt_controller = target.controller_index;
            int tgt_power      = target.EffectivePower();
            if (def.params.damage > 0)  // exile
            {
                state.exile.push_back(target.card);
            }
            else
            {
                state.players[target.controller_index].graveyard.push_back(target.card);
            }
            state.battlefield.erase(state.battlefield.begin() + t.permanent_index);

            // The exiled creature's controller gains `power` life. OpponentGainsLife(X) makes
            // (1-X) gain, so pass (1 - tgt_controller). When tgt_controller is the opponent
            // (the usual goldfish case) this routes through OUR Tainted Remedy, turning the
            // opponent's gain into `power` damage; on our own creature it just gives us life.
            if (def.params.controller_lifegain_equals_power && tgt_power > 0)
            {
                OpponentGainsLife(state, 1 - tgt_controller, tgt_power);
            }
        }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawSpell(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    int n = def.params.draw;

    // Expressive Iteration: look 3 -> 1 hand / 1 exiled-staged-this-turn / 1 bottom (its own model,
    // not the normal draw/scry path). Mirrors the rollout's apply_one DrawSpell branch (lockstep).
    if (def.params.expressive_iteration)
    {
        ResolveExpressiveIteration(state);
        MoveToGraveyard(state, entry);
        return;
    }

    // Scry-then-draw (Preordain): bottom the unwanted, reorder the rest, before drawing.
    if (def.params.cast_scry > 0) { ScryTop(state, def.params.cast_scry, def.card.m_name); }
    // Reorder-or-shuffle-then-draw (Ponder): keep all on top in best order, or shuffle away.
    if (def.params.cast_reorder > 0) { ReorderTopOrShuffle(state, def.params.cast_reorder, def.card.m_name, entry.ponder_keep.value_or(-1)); }

    if (def.params.stages_cards)
    {
        // Cards go to a staged exile zone; playable until end of the player's next turn.
        int expiry = state.turn_number + 1;
        for (int i = 0; i < n && !controller.library.empty(); ++i)
        {
            StagedCard sc;
            sc.card        = controller.library.DrawTop();
            sc.expiry_turn = expiry;
            controller.staged_cards.push_back(sc);
        }
    }
    else
    {
        // Deck-out: if the library cannot supply all n, draw what is left and the drawing player
        // loses (CR 104.3c, drawing from an empty library). Routed through the same loss flag the
        // draw step uses, so PlayOut ends the game. Reachable on this combo deck once a deep
        // Soulfire/cantrip dig has emptied the library; harmless for decks that never deck out.
        int drew = controller.library.DrawN(n, controller.hand);
        controller.cards_drawn_this_turn += drew;
        if (drew < n) { state.player_lost_on_draw = true; }
    }

    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawX(GameState& state, const StackEntry& entry,
                                   const CardDefinition& def)
{
    (void)def;
    Player& controller = state.players[entry.controller_index];
    int n = entry.chosen_x.value_or(0);
    if (n > 0)
    {
        controller.cards_drawn_this_turn += controller.library.DrawN(n, controller.hand);
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolvePumpSpell(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Permanent
            && t.permanent_index >= 0
            && t.permanent_index < static_cast<int>(state.battlefield.size()))
        {
            Permanent& target = state.battlefield[t.permanent_index];
            // "+N/+M until end of turn" (Invigorate): temp bonuses, reset each cleanup step.
            target.temp_power_bonus += def.params.power_bonus;
            target.temp_tough_bonus += def.params.tough_bonus;
        }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawUntilNonland(GameState& state, const StackEntry& entry,
                                             const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    // Reveal cards from the top until a nonland is found; put ALL revealed cards
    // (including the triggering nonland) into hand (CR oracle: "put all cards
    // revealed this way into your hand").
    // Capture the revealed -> hand cards for the replay viewer (real play only; the reveal
    // logger is paused during search/rollout, so this never fires off the real game's resolution).
    const bool capture = RevealVisible();   // log AND/or viewer (--claude-play attaches no GameLogger)
    std::vector<int>         revealed_nums;
    std::vector<std::string> revealed_names;
    while (!controller.library.empty())
    {
        Card c = controller.library.DrawTop();
        bool is_land = false;
        {
            auto cdef = CardDatabase::Instance().LookupCached(c);
            is_land = cdef ? cdef->card.IsLand() : c.IsLand();
        }
        if (capture) { revealed_nums.push_back(c.m_number); revealed_names.push_back(c.m_name); }
        controller.hand.push_back(std::move(c));
        if (!is_land) { break; }
    }
    if (capture && !revealed_nums.empty())
    {
        // All revealed cards go to hand, so "kept" = every revealed card (the viewer labels a
        // Treasure-Hunt source as "to hand").
        EmitReveal(state.turn_number, def.card.m_name, revealed_nums, revealed_names,
                                   revealed_nums, /*bottomed*/ std::vector<int>{});
    }
    MoveToGraveyard(state, entry);
}

// ---- Real-stack trigger machinery (BreachingDragonstorm onboarding, 2026-09-03) ------------
// Cascade / demonstrate are CAST triggers pushed ABOVE their spell; Breaching Dragonstorm's
// exile walk is an ENTER trigger pushed by Resolve()'s pending-queue drain. All of them resolve
// through GameEngine::ResolveStack's ordinary LIFO loop, so nested chains (Wanderer -> cascade
// -> free-cast Altisaur -> its cascade -> ...) need no special-casing: each cast pushes its own
// triggers and the stack does the rest. Lockstep twin: TurnSolver::apply_one runs the same
// sequence inline (cascade walks before the spell's own effect, copy payload before the
// original's, ETB walk right after entry), so both worlds realise identical states.

void EffectHandler::PushCastTriggers(GameState& state, const CardDefinition& def, int controller)
{
    // Cast triggers go on the stack ABOVE the just-pushed spell entry (CR 601.2i / 603.3b),
    // so they resolve first. No-op for every card without a cast-trigger param.
    if (def.params.demonstrate)
    {
        StackEntry te;
        te.type             = StackEntry::EntryType::Triggered;
        te.trigger_kind     = StackEntry::TriggerKind::Demonstrate;
        te.source           = def.card;
        te.controller_index = controller;
        state.stack.push_back(std::move(te));
    }
    if (def.params.cascade_max_mv > 0)
    {
        // One entry per instance ("Cascade, cascade" = 2 -- cascade_count). The instances are
        // identical, so their relative order is moot (CR 603.3b makes it the controller's
        // choice); each resolves FULLY (walk + free cast) before the next pops, so the second
        // cascade sees the library the first one left.
        const int n = std::max(1, def.params.cascade_count);
        for (int i = 0; i < n; ++i)
        {
            StackEntry te;
            te.type             = StackEntry::EntryType::Triggered;
            te.trigger_kind     = StackEntry::TriggerKind::Cascade;
            te.source           = def.card;
            te.controller_index = controller;
            state.stack.push_back(std::move(te));
        }
    }
}

bool EffectHandler::PushFreeCast(GameState& state, const Card& card, int controller)
{
    auto def = CardDatabase::Instance().LookupCached(card);
    if (!def) { return false; }
    // Irencrag "one more spell" budget: a forbidden cast is not made -- the caller decides the
    // card's fallback zone (cascade bottoms it, Breaching Dragonstorm hands it, Creative
    // Technique strands it in exile). Inert (-1) for every deck without a max_casts_after card.
    if (state.casts_remaining_this_turn == 0) { return false; }

    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = def->card;
    e.source.m_number  = card.m_number;   // preserve per-copy ID
    e.controller_index = controller;

    // A free cast IS a cast (CR 601.2 / 702.85a): count it and fire cast-time abilities
    // exactly as CastSpellFromHand does -- lockstep with the rollout's apply_one free-cast
    // recursion, which runs the same increments. (This also closes the old executor-vs-rollout
    // storm-count divergence, where ResolveCascade pushed a bare StackEntry that no counter or
    // cast trigger ever saw.)
    ++state.spells_cast_this_turn;
    if (state.casts_remaining_this_turn > 0) { --state.casts_remaining_this_turn; }
    if (def->params.max_casts_after >= 0)
    {
        state.casts_remaining_this_turn =
            (state.casts_remaining_this_turn < 0)
                ? def->params.max_casts_after
                : std::min(state.casts_remaining_this_turn, def->params.max_casts_after);
    }
    state.stack.push_back(std::move(e));
    FireOnCastTriggers(state, *def);
    FireProwess(state, *def);
    // ...including the free spell's OWN cast triggers, so cascade chains nest naturally.
    PushCastTriggers(state, *def, controller);
    return true;
}

bool EffectHandler::ResolveTriggered(GameState& state, const StackEntry& entry,
                                     const CardDefinition& def)
{
    switch (entry.trigger_kind)
    {
        case StackEntry::TriggerKind::Cascade:
            ResolveCascadeTrigger(state, entry, def);   return true;
        case StackEntry::TriggerKind::EtbExileFreeCast:
            ResolveEtbExileFreeCast(state, entry, def); return true;
        case StackEntry::TriggerKind::Demonstrate:
            ResolveDemonstrate(state, entry, def);      return true;
        default:                                        return true;
    }
}

void EffectHandler::ResolveCascadeTrigger(GameState& state, const StackEntry& entry,
                                          const CardDefinition& def)
{
    std::vector<int> seen_nums; std::vector<std::string> seen_names;
    Card hit;
    const bool have = WalkCascadeExile(state, entry.controller_index,
                                       def.params.cascade_max_mv, hit,
                                       &seen_nums, &seen_names);
    // Viewer/log visibility: the exile walk + the hit (historically cascade emitted nothing,
    // which left the deck's core engine invisible in the play viewer). Non-hits = "bottomed".
    std::vector<int> kept;
    if (have) { kept.push_back(hit.m_number); }
    std::vector<int> bottomed;
    for (int n : seen_nums) { if (!have || n != hit.m_number) { bottomed.push_back(n); } }
    EmitReveal(state.turn_number, def.card.m_name.str() + " (cascade)",
               seen_nums, seen_names, kept, bottomed);
    if (!have) { return; }
    // "You may cast it": provider-owned default (DecisionProvider::TakeFreeCast, Generic =
    // always take -- disclosed in Stage 6a), human chooser overrides at real resolution. A
    // decline, or a cast a restriction forbids, bottoms the hit under the walked cards.
    bool take = true;
    const CardDefinition* hd = CardDatabase::Instance().LookupCached(hit);
    if (hd) { take = ResolveProvider(state).TakeFreeCast(state, entry.controller_index, *hd); }
    if (g_play_free_cast_chooser)
    {
        std::vector<Card> cands{ hit };
        take = ((*g_play_free_cast_chooser)(state, entry.controller_index,
                                            def.card.m_name.str() + " (cascade)", cands,
                                            take ? 0 : -1) == 0);
    }
    if (!take || !PushFreeCast(state, hit, entry.controller_index))
    {
        state.players[entry.controller_index].library.push_back(hit);
    }
}

void EffectHandler::ResolveEtbExileFreeCast(GameState& state, const StackEntry& entry,
                                            const CardDefinition& def)
{
    // Breaching Dragonstorm: exile until the first nonland (NO mana-value bound on the walk);
    // the walked LANDS stay in state.exile permanently (WalkExileUntilNonland).
    std::vector<int> seen_nums; std::vector<std::string> seen_names;
    Card found;
    const bool have = WalkExileUntilNonland(state, entry.controller_index, found,
                                            &seen_nums, &seen_names);
    std::vector<int> kept;
    if (have) { kept.push_back(found.m_number); }
    EmitReveal(state.turn_number, def.card.m_name.str() + " (exile until nonland)",
               seen_nums, seen_names, kept, std::vector<int>{});
    if (!have) { return; }
    const CardDefinition* fd = CardDatabase::Instance().LookupCached(found);
    const int mv = fd ? fd->card.m_mana_cost.ManaValue() : found.m_mana_cost.ManaValue();
    // The MV gate bounds only the free cast, not the walk (oracle constant, 8 here).
    bool take = (mv <= def.params.etb_exile_free_cast_max_mv);
    if (take && fd)
    { take = ResolveProvider(state).TakeFreeCast(state, entry.controller_index, *fd); }
    if (take && g_play_free_cast_chooser)
    {
        std::vector<Card> cands{ found };
        take = ((*g_play_free_cast_chooser)(state, entry.controller_index,
                                            def.card.m_name.str(), cands, 0) == 0);
    }
    // "If you don't, put that card into your hand."
    if (!take || !PushFreeCast(state, found, entry.controller_index))
    {
        state.players[entry.controller_index].hand.push_back(found);
    }
}

void EffectHandler::ResolveDemonstrate(GameState& state, const StackEntry& entry,
                                       const CardDefinition& def)
{
    // "You may copy this spell." Provider default (copy), human chooser overrides. The
    // opponent's half ("choose an opponent to also copy it") is inert -- the goldfish opponent
    // is never dealt a library and never casts (see the card's bracket note).
    bool copy = ResolveProvider(state).DemonstrateCopy(state, entry.controller_index, def);
    if (g_play_demonstrate_chooser)
    { copy = (*g_play_demonstrate_chooser)(state, entry.controller_index, def.card, copy); }
    if (!copy) { return; }
    // The copy is NOT cast (CR 707.10): no cast triggers, no cast counters. Pushed above the
    // original (this trigger sat above it), so it resolves first (2021-04-16 ruling), then
    // ceases to exist (StackEntry::is_copy -> MoveToGraveyard skips it).
    StackEntry ce;
    ce.type             = StackEntry::EntryType::Spell;
    ce.source           = entry.source;
    ce.controller_index = entry.controller_index;
    ce.is_copy          = true;
    state.stack.push_back(std::move(ce));
}

void EffectHandler::ResolveShuffleRevealFreecast(GameState& state, const StackEntry& entry,
                                                 const CardDefinition& def)
{
    // "Shuffle your library" -- through the CRN reshuffle (ShuffleAfterSearch), NEVER an
    // ad-hoc Shuffle(): the ordinal-keyed CRN is what keeps executor and rollout on the same
    // library (and the rollout twin makes the identical call).
    ShuffleAfterSearch(state, entry.controller_index);
    std::vector<int> seen_nums; std::vector<std::string> seen_names;
    Card found;
    const bool have = WalkRevealUntilNonland(state, entry.controller_index, found,
                                             &seen_nums, &seen_names);
    std::vector<int> kept;
    if (have) { kept.push_back(found.m_number); }
    std::vector<int> bottomed;
    for (int n : seen_nums) { if (!have || n != found.m_number) { bottomed.push_back(n); } }
    EmitReveal(state.turn_number, def.card.m_name.str() + " (reveal)",
               seen_nums, seen_names, kept, bottomed);
    if (!have) { return; }
    const CardDefinition* fd = CardDatabase::Instance().LookupCached(found);
    bool take = fd ? ResolveProvider(state).TakeFreeCast(state, entry.controller_index, *fd)
                   : false;
    if (g_play_free_cast_chooser)
    {
        std::vector<Card> cands{ found };
        take = ((*g_play_free_cast_chooser)(state, entry.controller_index,
                                            def.card.m_name.str(), cands,
                                            take ? 0 : -1) == 0);
    }
    // A declined (or forbidden) hit STAYS EXILED -- the oracle's only alternative disposition.
    if (!take || !PushFreeCast(state, found, entry.controller_index))
    {
        state.exile.push_back(found);
    }
}
