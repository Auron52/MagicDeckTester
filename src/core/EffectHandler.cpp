#include "EffectHandler.h"
#include "SpellEffects.h"
#include <algorithm>

// ---- Helpers ----

void EffectHandler::EnterBattlefield(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    Permanent perm;
    perm.card              = def.card;
    perm.card.m_number     = entry.source.m_number;  // preserve per-copy ID from cast
    perm.controller_index  = entry.controller_index;
    perm.owner_index       = entry.controller_index;
    perm.entered_this_turn = true;
    state.battlefield.push_back(perm);
}

void EffectHandler::MoveToGraveyard(GameState& state, const StackEntry& entry)
{
    state.players[entry.controller_index].graveyard.push_back(entry.source);
}

// ---- Public dispatch ----

bool EffectHandler::Resolve(GameState& state, const StackEntry& entry, const CardDefinition& def)
{
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
                || def.card.HasType(CardType::Artifact) || def.card.IsLand())
            {
                EnterBattlefield(state, entry, def);
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
                if (def.params.destroy_all_enchantments)
                {
                    DestroyAllEnchantments(state);
                }
                if (def.params.cascade_max_mv > 0)
                {
                    ResolveCascade(state, entry, def);
                }
                // Reality Spasm / Irencrag Feat -- mana RITUAL. On resolution, add its floating
                // mana to the turn-scoped reserve so a later same-turn cast (Crackle) can spend
                // it (the Hinata combo). Modelled as floating, NOT a literal untap, so this stays
                // EXACTLY in lockstep with the planner's ritual credit and the rollout's apply_one.
                if (IsManaRitual(def))
                {
                    ApplyRitualFloat(state, def, entry.chosen_x.value_or(0));
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
                                            entry.soulfire_own_targets.value_or(0));
            damage = sr.face_damage;
            state.players[entry.controller_index].life -= sr.self_damage;
        }
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
                target.damage += damage;

                // Death trigger: if the creature now has lethal damage, fire immediately.
                if (def.params.death_trigger_damage > 0
                    && target.damage >= target.EffectiveToughness())
                {
                    int ctrl = target.controller_index;
                    state.players[ctrl].life -= def.params.death_trigger_damage;
                    if (ctrl != entry.controller_index && def.params.death_trigger_damage > 0)
                    {
                        state.opponent_lost_life_this_turn = true;
                    }
                }
            }
        }
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
        cp.library.DrawN(def.params.cast_draw, cp.hand);
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
            Permanent& target = state.battlefield[t.permanent_index];
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
        controller.library.DrawN(n, controller.hand);
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
                                             const CardDefinition& /*def*/)
{
    Player& controller = state.players[entry.controller_index];
    // Reveal cards from the top until a nonland is found; put ALL revealed cards
    // (including the triggering nonland) into hand (CR oracle: "put all cards
    // revealed this way into your hand").
    while (!controller.library.empty())
    {
        Card c = controller.library.DrawTop();
        bool is_land = false;
        {
            auto cdef = CardDatabase::Instance().LookupCached(c);
            is_land = cdef ? cdef->card.IsLand() : c.IsLand();
        }
        controller.hand.push_back(std::move(c));
        if (!is_land) { break; }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveCascade(GameState& state, const StackEntry& entry,
                                    const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    int cascade_limit  = def.params.cascade_max_mv;

    // Exile cards from top until a nonland with MV < cascade_limit is found.
    std::vector<Card> exiled;
    int cascade_idx = -1;
    while (!controller.library.empty())
    {
        Card c = controller.library.DrawTop();
        auto cdef = CardDatabase::Instance().LookupCached(c);
        bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
        int  mv      = cdef ? cdef->card.m_mana_cost.ManaValue()
                            : c.m_mana_cost.ManaValue();
        exiled.push_back(std::move(c));
        if (!is_land && mv < cascade_limit)
        {
            cascade_idx = static_cast<int>(exiled.size()) - 1;
            break;
        }
    }

    // Push the cascade target onto the stack so it resolves before the original spell's
    // graveyard placement (the stack is LIFO; next iteration of ResolveStack pops it).
    if (cascade_idx >= 0)
    {
        const Card& cascade_card = exiled[cascade_idx];
        auto cdef = CardDatabase::Instance().LookupCached(cascade_card);
        if (cdef)
        {
            StackEntry ce;
            ce.type             = StackEntry::EntryType::Spell;
            ce.source           = cdef->card;
            ce.source.m_number  = cascade_card.m_number;
            ce.controller_index = entry.controller_index;
            state.stack.push_back(std::move(ce));
        }
    }

    // Remaining exiled cards go to the bottom of the library in the order exiled.
    for (int i = 0; i < static_cast<int>(exiled.size()); ++i)
    {
        if (i == cascade_idx) { continue; }
        controller.library.push_back(std::move(exiled[i]));
    }
}
