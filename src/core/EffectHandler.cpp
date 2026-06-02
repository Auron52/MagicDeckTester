#include "EffectHandler.h"
#include <algorithm>

// ---- Helpers ----

void EffectHandler::EnterBattlefield(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    Permanent perm;
    perm.card               = def.card;
    perm.controller         = &state.players[entry.controller_index];
    perm.owner              = &state.players[entry.controller_index];
    perm.entered_this_turn  = true;
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

        default:
        {
            // Unknown or Tier 3 custom card — no built-in effect.
            // Move non-permanents to graveyard; permanents enter the battlefield.
            if (def.card.IsCreature() || def.card.HasType(CardType::Enchantment)
                || def.card.HasType(CardType::Artifact) || def.card.IsLand())
            {
                EnterBattlefield(state, entry, def);
            }
            else
            {
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
    int damage = def.params.damage;

    for (const Target& t : entry.targets)
    {
        if (t.type == Target::Type::Player)
        {
            state.players[t.player_index].life -= damage;
        }
        else if (t.type == Target::Type::Permanent)
        {
            if (t.permanent_index >= 0
                && t.permanent_index < static_cast<int>(state.battlefield.size()))
            {
                state.battlefield[t.permanent_index].damage += damage;
            }
        }
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
            if (def.params.damage > 0)  // exile
            {
                state.exile.push_back(target.card);
            }
            else
            {
                target.controller->graveyard.push_back(target.card);
            }
            state.battlefield.erase(state.battlefield.begin() + t.permanent_index);
        }
    }
    MoveToGraveyard(state, entry);
}

void EffectHandler::ResolveDrawSpell(GameState& state, const StackEntry& entry,
                                      const CardDefinition& def)
{
    Player& controller = state.players[entry.controller_index];
    int n = def.params.draw;
    controller.library.DrawN(n, controller.hand);
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
            // Apply +N/+M as a +1/+1 counter equivalent for now.
            // TODO: implement "until end of turn" tracking in Phase 1.2+.
            Counter bonus;
            bonus.type  = Counter::Type::PlusOnePlusOne;
            bonus.count = std::min(def.params.power_bonus, def.params.tough_bonus);
            if (bonus.count > 0)
            {
                target.counters.push_back(bonus);
            }
        }
    }
    MoveToGraveyard(state, entry);
}
