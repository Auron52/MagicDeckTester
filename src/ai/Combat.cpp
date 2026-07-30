// Unified combat core -- see Combat.h for what is shared and what deliberately is not.
#include "../core/EnvFlags.h"
#include "Combat.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/SpellEffects.h"

std::vector<int> DeclareAttackerIndices(const GameState& state)
{
    std::vector<int> atk_idx;
    const DecisionProvider& provider = ResolveProvider(state);
    const int active = state.active_player_index;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!provider.ShouldAttackWith(state, p)) { continue; }
        atk_idx.push_back(i);
    }
    return atk_idx;
}

CombatDamageResult ResolveCombatDamage(GameState& state, const std::vector<int>& atk_idx,
                                       int exalted_bonus, bool collect_descs)
{
    CombatDamageResult out;
    const int active  = state.active_player_index;
    const int opp_idx = 1 - active;

    std::vector<const Permanent*> attackers;
    attackers.reserve(atk_idx.size());
    std::vector<int> damaging_idx;   // attackers that dealt >0 damage (Goblin Lackey cheat)
    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        const bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state.battlefield, active, animated, &p);
        (void)lord_tb;
        const bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active));
        int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        base_pw += AuraBonusFor(p, state).first;                 // Bogles: attached auras + Kor self-buff
        const int power = base_pw * (ds ? 2 : 1);
        state.players[opp_idx].life -= power;
        out.total_damage += power;
        if (power > 0)
        {
            state.opponent_lost_life_this_turn = true;
            damaging_idx.push_back(idx);   // Goblin Lackey: dealt combat damage to a player
            // Lifelink (modeled): combat damage also gains the controller that much life. Inert vs
            // the passive opponent's clock, tracked for life-total decks.
            if (CreatureHasLifelink(p, state)) { state.players[active].life += power; }
        }
        if (collect_descs && power > 0)
        { out.attacker_descs.push_back(p.card.m_name.str() + " (" + std::to_string(power) + ")"); }
        if (!p.card.HasKeyword(Keyword::Vigilance)) { p.tapped = true; }
        attackers.push_back(&p);
    }

    // Attack triggers (e.g. Leeching Sliver: each attacking Sliver costs the opponent 1 life).
    // Life LOSS, not combat damage: it still marks the lost-life flag (which drives spectacle), and
    // is folded into the total only for the attack log.
    out.trigger_life_loss = CountAttackTriggerLifeLoss(state.battlefield, active, attackers);
    if (out.trigger_life_loss > 0)
    {
        state.players[opp_idx].life -= out.trigger_life_loss;
        out.total_damage += out.trigger_life_loss;
        state.opponent_lost_life_this_turn = true;
    }

    // Utvara Hellkite: per attacking Dragon, create a 6/6 Dragon token (untapped, summoning-sick;
    // NOT added to this combat). Each token entering fires OnDragonEnters (Scourge ping / Lathliss
    // token) via CreateToken. `attackers` still holds the pre-token attacker pointers, which
    // FireUtvaraAttackTokens reads before any CreateToken.
    FireUtvaraAttackTokens(state, active, attackers);

    // Goblin Lackey: each attacker that dealt combat damage to the player may put a matching Goblin
    // permanent from hand onto the battlefield (shared enter cascade). Fired AFTER Utvara so the
    // pre-token attacker pointers above are already consumed.
    FireCombatDamageCheatIntoPlay(state, active, damaging_idx);
    return out;
}
