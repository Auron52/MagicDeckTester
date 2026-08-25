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
    // Reference-replay attacker pin (--force-attackers; nulled by RevealLogPause -> real
    // declaration only). The recorded game's attack set overrides the willingness heuristic
    // (AttackWith) but never legality (CanAttackFull): declare exactly the eligible recorded
    // names, consuming the multiset so duplicate names pin the right number of copies. Player 0
    // only -- the human's deck is always player 0 under --claude-play, and the recording says
    // nothing about opponent combats.
    if (g_play_attackers_chooser && active == 0)
    {
        if (const std::vector<std::string>* pin = (*g_play_attackers_chooser)(state.turn_number))
        {
            std::vector<std::string> want = *pin;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != active) { continue; }
                if (!CanAttackFull(p, state.battlefield, active)) { continue; }
                auto it = std::find(want.begin(), want.end(), p.card.m_name.str());
                if (it == want.end()) { continue; }
                want.erase(it);
                atk_idx.push_back(i);
            }
            return atk_idx;
        }
    }
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!provider.AttackWith(state, p)) { continue; }
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

    // Pre-filter the active player's lord permanents ONCE (usually none), so the per-attacker
    // ComputeLordBonus / HasDoubleStrikeFromLords below iterate that tiny list instead of each
    // re-scanning the whole battlefield. Byte-identical: same permanents, same per-creature logic.
    std::vector<int> lord_idx, ds_idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& q = state.battlefield[i];
        if (q.controller_index != active) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (!qd) { continue; }
        if (IsLordPermanent(*qd)) { lord_idx.push_back(i); }   // template lords + dual-role (Archdruid)
        if (qd->params.grants_double_strike)      { ds_idx.push_back(i); }
    }

    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        const bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, animated, &p, &lord_idx);
        (void)lord_tb;
        const bool ds = (animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true, &ds_idx)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active, false, &ds_idx)))
            || HasDoubleStrikeFromEquipment(p, state);   // Kor Duelist / Balan (KittyEquipment)
        int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        base_pw += AuraBonusFor(p, state).first;                 // Bogles: attached auras + Kor self-buff
        base_pw += EquipBonusFor(p, state).first;                // KittyEquipment: attached equipment
        // Umezawa's Jitte: spend charge counters on +2/+2 (greedy default / provider / human
        // side-channel), earn 2 per damage event -- the shared closed form (JitteDamageMath) the
        // two TurnSolver projections also use, so search and execution stay lockstep.
        int power = base_pw * (ds ? 2 : 1);
        const int jitte_bf = FindAttachedChargeEquip(state, p);
        if (jitte_bf >= 0)
        {
            Permanent& je = state.battlefield[jitte_bf];
            const CardDefinition* jd = CardDatabase::Instance().LookupCached(je.card);
            int req = ResolveProvider(state).JitteSpendCount(state, je.charge_counters);
            if (g_play_jitte_chooser)   // nulled by RevealLogPause -> real combat only
            {
                int r = (*g_play_jitte_chooser)(state, active, atk_idx, je.charge_counters);
                if (r >= 0) { req = r; }
            }
            const auto [jdmg, jafter] = JitteDamageMath(
                base_pw, ds, je.charge_counters, jd->params.charge_pump_power, req);
            power             = jdmg;
            je.charge_counters = jafter;
        }
        state.players[opp_idx].life -= power;
        out.total_damage += power;
        if (power > 0)
        {
            state.opponent_lost_life_this_turn = true;
            damaging_idx.push_back(idx);   // Goblin Lackey: dealt combat damage to a player
            // Maelstrom Archangel: connecting banks one free cast for the post-combat main
            // (user-approved banking model; see GameState::free_casts_available). Shared combat
            // core -> executor and rollout bank identically.
            if (adef && adef->params.combat_damage_free_cast) { ++state.free_casts_available; }
            // Lifelink (modeled): combat damage also gains the controller that much life. Inert vs
            // the passive opponent's clock, tracked for life-total decks.
            if (CreatureHasLifelink(p, state))
            { state.players[active].life += power; state.players[active].life_gained_this_turn += power; }
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

    // Neheb, the Worthy: "Whenever Neheb deals combat damage to a player, each player discards a
    // card." Fired here, after damage, off the same damaging_idx list Goblin Lackey uses -- so it
    // is once per connecting copy, in the shared combat core (executor + rollout lockstep). Only
    // OUR half is modelled: the passive opponent never casts and no decision reads their hand
    // (disclosed deferral D10). The shed card is the provider's cleanup-discard pick, and the
    // discard is what can switch Neheb's own hand-size anthem ON -- but only for a LATER turn's
    // combat, since this fires after damage has already been dealt.
    for (int idx : damaging_idx)
    {
        if (idx < 0 || idx >= static_cast<int>(state.battlefield.size())) { continue; }
        const CardDefinition* nd = CardDatabase::Instance().LookupCached(state.battlefield[idx].card);
        if (!nd || nd->params.combat_damage_each_discards <= 0) { continue; }
        Player& ap = state.players[active];
        for (int k = 0; k < nd->params.combat_damage_each_discards && !ap.hand.empty(); ++k)
        {
            const int hi = ChooseNonCleanupDiscardIndex(state, active);   // surfaced as `discard`
            if (hi < 0) { break; }
            ap.graveyard.push_back(ap.hand[static_cast<std::size_t>(hi)]);
            ap.hand.erase(ap.hand.begin() + hi);
        }
    }
    return out;
}
