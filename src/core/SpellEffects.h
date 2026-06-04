#pragma once
#include "GameState.h"
#include "../cards/CardDatabase.h"

// Fires on-cast triggers from all permanents on the battlefield (e.g. Eidolon of the
// Great Revel). Deals damage to the active player for each permanent whose trigger
// condition is met. Called at cast time from both AIEngine (real game) and
// ApplyPlanDirect (lookahead).
inline void FireOnCastTriggers(GameState& state, const CardDefinition& cast_def)
{
    int mv = cast_def.card.m_mana_cost.ManaValue();
    for (const Permanent& p : state.battlefield)
    {
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def) { continue; }
        if (def->params.on_cast_trigger_max_mv <= 0) { continue; }
        if (mv > def->params.on_cast_trigger_max_mv) { continue; }
        state.players[state.active_player_index].life -= def->params.on_cast_trigger_damage;
    }
}

// Returns the total {power_bonus, toughness_bonus} granted to `creature` by all
// lord_effect permanents that `controller_index` controls on `battlefield`.
// Matches on creature.m_subtypes (e.g. "Sliver") against params.subtypes_affected.
// Pass all_creature_types = true for animated permanents (e.g. Mutavault) which have
// all creature types and therefore match any lord.
// For lords with scales_per_matching = true (e.g. Predatory Sliver), the bonus scales
// with the number of other matching permanents on the battlefield.
inline std::pair<int,int> ComputeLordBonus(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false)
{
    int pb = 0, tb = 0;
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        std::optional<CardDefinition> ldef = CardDatabase::Instance().Lookup(lord.card.m_name);
        if (!ldef || ldef->tmpl != CardTemplate::LordEffect) { continue; }

        bool matches = false;
        if (all_creature_types && !ldef->params.subtypes_affected.empty())
        {
            matches = true;
        }
        else
        {
            for (const std::string& sub : ldef->params.subtypes_affected)
            {
                if (matches) { break; }
                for (const std::string& cs : creature.m_subtypes)
                {
                    if (cs == sub) { matches = true; break; }
                }
            }
        }
        if (!matches) { continue; }

        if (ldef->params.scales_per_matching)
        {
            // Bonus per Sliver = power_bonus * (number of other matching Slivers on board).
            // Count all matching permanents (including animated) then subtract 1 for "other".
            int matching_count = 0;
            for (const Permanent& other : battlefield)
            {
                if (other.controller_index != controller_index) { continue; }
                bool other_matches = false;
                if (other.is_animated && !ldef->params.subtypes_affected.empty())
                {
                    other_matches = true;
                }
                else
                {
                    for (const std::string& sub : ldef->params.subtypes_affected)
                    {
                        for (const std::string& cs : other.card.m_subtypes)
                        {
                            if (cs == sub) { other_matches = true; break; }
                        }
                        if (other_matches) { break; }
                    }
                }
                if (other_matches) { ++matching_count; }
            }
            int scale = std::max(0, matching_count - 1);
            pb += ldef->params.power_bonus * scale;
            tb += ldef->params.tough_bonus * scale;
        }
        else
        {
            pb += ldef->params.power_bonus;
            tb += ldef->params.tough_bonus;
        }
    }
    return {pb, tb};
}

// Returns true if any lord on the battlefield grants double strike to creature's subtype.
inline bool HasDoubleStrikeFromLords(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false)
{
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        std::optional<CardDefinition> ldef = CardDatabase::Instance().Lookup(lord.card.m_name);
        if (!ldef || !ldef->params.grants_double_strike) { continue; }
        if (all_creature_types && !ldef->params.subtypes_affected.empty()) { return true; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : creature.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Returns true if any lord on the battlefield grants haste to creature's subtype.
inline bool HasHasteFromLords(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index)
{
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        std::optional<CardDefinition> ldef = CardDatabase::Instance().Lookup(lord.card.m_name);
        if (!ldef || !ldef->params.grants_haste) { continue; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : creature.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Returns true if the permanent can attack, considering lord-granted haste as well as
// card-level haste and animation. Use this in place of Permanent::CanAttack() anywhere
// the battlefield context is available (declare attackers, combat lookahead).
inline bool CanAttackFull(
    const Permanent&               p,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index)
{
    if (!p.card.IsCreature() && !p.is_animated) { return false; }
    if (p.tapped)                               { return false; }
    if (p.card.HasKeyword(Keyword::Defender))   { return false; }
    if (!p.entered_this_turn || p.is_animated)  { return true; }
    if (p.card.HasKeyword(Keyword::Haste))      { return true; }
    return HasHasteFromLords(p.card, battlefield, controller_index);
}

// Returns the total extra damage dealt to the opponent by attack triggers (e.g. Leeching
// Sliver: 1 per attacking Sliver). Iterates the battlefield for permanents with
// attack_trigger_damage > 0, counts attackers matching their subtypes_affected.
inline int CountAttackTriggerDamage(
    const std::vector<Permanent>&         battlefield,
    int                                   controller_index,
    const std::vector<const Permanent*>&  attackers)
{
    int total = 0;
    for (const Permanent& src : battlefield)
    {
        if (src.controller_index != controller_index) { continue; }
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(src.card.m_name);
        if (!def || def->params.attack_trigger_damage <= 0) { continue; }

        int count = 0;
        for (const Permanent* atk : attackers)
        {
            for (const std::string& sub : def->params.subtypes_affected)
            {
                bool matches = atk->is_animated;  // animated = all creature types
                if (!matches)
                {
                    for (const std::string& cs : atk->card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++count; break; }
            }
        }
        total += def->params.attack_trigger_damage * count;
    }
    return total;
}

// Creates a creature token with the given stats and adds it to the active battlefield.
// The token enters with entered_this_turn = true (subject to summoning sickness unless
// given haste by a lord). Tokens have no card number and an auto-generated name.
inline void CreateToken(
    GameState&                       state,
    int                              controller_index,
    int                              power,
    int                              toughness,
    const std::vector<std::string>&  subtypes)
{
    Permanent token;
    token.card.m_name      = std::to_string(power) + "/" + std::to_string(toughness);
    if (!subtypes.empty()) { token.card.m_name += " " + subtypes[0]; }
    token.card.m_name     += " Token";
    token.card.m_id        = token.card.m_name;
    token.card.m_types     = { CardType::Creature };
    token.card.m_subtypes  = subtypes;
    token.card.m_power     = power;
    token.card.m_toughness = toughness;
    token.controller_index = controller_index;
    token.owner_index      = controller_index;
    token.entered_this_turn = true;
    state.battlefield.push_back(token);
}

// Returns true if the creature `def` gets replicate when cast this turn.
// Two sources: the card itself has `has_replicate`, or a permanent on the battlefield
// has `grants_replicate_to_subtypes` and the creature matches its subtypes_affected.
inline bool CanReplicate(
    const CardDefinition&          def,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index)
{
    if (def.params.has_replicate) { return true; }
    for (const Permanent& p : battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        std::optional<CardDefinition> ldef = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!ldef || !ldef->params.grants_replicate_to_subtypes) { continue; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : def.card.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Fires prowess triggers for all Prowess creatures the active player controls.
// Called at cast time (when a spell is pushed to the stack or applied directly in
// lookahead simulation), not at resolution — prowess reads "whenever you cast."
// Only noncreature spells trigger prowess (CR 702.107a).
inline void FireProwess(GameState& state, const CardDefinition& def)
{
    if (def.card.IsCreature()) { return; }

    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (!p.card.HasKeyword(Keyword::Prowess)) { continue; }
        ++p.temp_power_bonus;
        ++p.temp_tough_bonus;
    }
}
