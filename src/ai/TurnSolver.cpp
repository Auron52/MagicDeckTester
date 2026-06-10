#include "TurnSolver.h"
#include "TranspositionTable.h"
#include "../core/ManaPool.h"
#include "../core/EffectHandler.h"
#include "../core/SpellEffects.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_set>

// When true, SolveWithLookahead prints per-pass per-candidate win turn estimates
// for top-level T1 pre-combat decisions.  Set via TurnSolver::SetTraceSolve().
static bool s_trace_solve = false;

void TurnSolver::SetTraceSolve(bool enable) { s_trace_solve = enable; }
bool TurnSolver::GetTraceSolve() { return s_trace_solve; }

static std::string PlanDesc(const TurnSolver::Plan& p)
{
    std::ostringstream os;
    auto list_kind = [&](Action::Kind kind, const char* label)
    {
        bool first = true;
        for (const Action& a : p.actions)
        {
            if (a.kind != kind) { continue; }
            if (first) { if (os.tellp() > 0) os << ' '; os << label << '['; first = false; }
            else       { os << ','; }
            os << a.card_name;
            if (kind == Action::Kind::DiscardToLandsEdge) { os << "x" << a.discard_lands; }
        }
        if (!first) { os << ']'; }
    };
    list_kind(Action::Kind::ActivateVial,       "vial");
    list_kind(Action::Kind::CastFromHand,       "spells");
    list_kind(Action::Kind::CastFromGraveyard,  "retrace");
    list_kind(Action::Kind::DiscardToLandsEdge, "le");
    if (p.empty()) os << "<pass>";
    return os.str();
}

// ---- Local helpers -------------------------------------------------------

// Build the active player's accounting mana pool from untapped sources. Depletion
// lands contribute 2, multi-color lands 1 wild, filter lands (Cascade Bluffs) 1 wild
// when fed else 1 {C} — see AddSourceToPool, shared with AIEngine::BuildAvailableMana.
static ManaPool BuildPool(const GameState& state)
{
    ManaPool pool;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap());
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, state, *def);
    }
    return pool;
}

// Pool excluding creature-only mana sources (e.g. Ancient Ziggurat).
// Used to verify that non-creature spells are payable without those sources.
static ManaPool BuildNonCreaturePool(const GameState& state)
{
    ManaPool pool;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def || def->params.creature_mana_only) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap());
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, state, *def);
    }
    return pool;
}

static int CountLands(const GameState& state)
{
    int n = 0;
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index && p.card.IsLand()) { ++n; }
    }
    return n;
}

static int PendingAttackDamage(const GameState& state)
{
    int dmg = 0;
    std::vector<const Permanent*> attackers;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (!CanAttackFull(p, state.battlefield, state.active_player_index)) { continue; }
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, state.active_player_index, animated);
        bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, state.active_player_index, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, state.active_player_index));
        int base_pw = p.EffectivePower() + lord_pb;
        if (animated)
        {
            std::optional<CardDefinition> adef = CardDatabase::Instance().Lookup(p.card.m_name);
            if (adef) { base_pw += adef->params.animate_power; }
        }
        dmg += base_pw * (ds ? 2 : 1);
        attackers.push_back(&p);
    }
    dmg += CountAttackTriggerDamage(state.battlefield, state.active_player_index, attackers);
    return dmg;
}

// Pre-computed per-permanent on-cast trigger: damage dealt to the caster when they
// cast a spell with MV <= max_mv (e.g. Eidolon of the Great Revel).
struct TriggerSource
{
    int max_mv;
    int damage;
};

static std::vector<TriggerSource> CollectTriggerSources(const GameState& state)
{
    std::vector<TriggerSource> sources;
    for (const Permanent& p : state.battlefield)
    {
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def || def->params.on_cast_trigger_max_mv <= 0) { continue; }
        sources.push_back({def->params.on_cast_trigger_max_mv,
                           def->params.on_cast_trigger_damage});
    }
    return sources;
}

// Returns the number of Prowess creatures the active player controls that can attack.
// Each noncreature spell cast this turn adds 1 to each of their powers, so the
// prowess bonus to combat damage = noncreature_spell_count * CountProwessAttackers().
static int CountProwessAttackers(const GameState& state)
{
    int count = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index
            && CanAttackFull(p, state.battlefield, state.active_player_index)
            && p.card.HasKeyword(Keyword::Prowess))
        {
            ++count;
        }
    }
    return count;
}

static bool HasLegalCreatureTarget(const GameState& state)
{
    const Player& opp = state.Opponent();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index && p.card.IsCreature()) { return true; }
    }
    return false;
}

static ManaCost EffectiveCost(const CardDefinition& def, const GameState& state)
{
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        return def.params.spectacle_cost.value();
    }
    ManaCost cost = def.card.m_mana_cost;
    if (def.params.affinity_for_subtype && !def.params.subtypes_affected.empty())
    {
        int reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            for (const std::string& sub : def.params.subtypes_affected)
            {
                bool matches = p.is_animated;
                if (!matches)
                {
                    for (const std::string& cs : p.card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++reduction; break; }
            }
        }
        cost.generic = std::max(0, cost.generic - reduction);
    }
    return cost;
}

// Estimate how many times a creature placed NOW will attack before the game ends.
// Uses the current turn number against an assumed 6-turn average game length for
// aggressive decks, so creatures placed early score higher than late-game drops.
// Callers subtract 1 for non-haste creatures (they miss the current attack step).
static int ExpectedAttacks(const GameState& state)
{
    // +1: include the current turn's attack step (haste creatures attack now).
    int remaining = 6 - state.turn_number + 1;
    return std::max(1, std::min(remaining, 5));
}

// Evaluate a card's contribution to winning, accounting for tempo.
// Uses a fixed unit of 100 per damage-equivalent so integer arithmetic
// stays precise even with multipliers.
static int EvalCard(const CardDefinition& def, const GameState& state)
{
    constexpr int DMG = 100;  // points per damage-equivalent

    if (def.tmpl == CardTemplate::DirectDamage)
    {
        // Penalise spells with additional costs so equivalent-damage alternatives
        // are preferred.  The penalty must be less than DMG so a sacrifice-land
        // spell is still selected when it extends the total subset (e.g. Shard
        // Volley + Bolt beats Bolt alone).
        int penalty = def.params.sacrifice_land ? (DMG / 2) : 0;
        return def.params.damage * DMG - penalty;
    }

    if (def.card.IsCreature())
    {
        auto [lord_pb, lord_tb] = ComputeLordBonus(def.card, state.battlefield, state.active_player_index);
        bool ds = def.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(def.card, state.battlefield, state.active_player_index);
        int power = (def.card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
        if (power <= 0) { return 0; }

        // Haste (from the card or from a lord already on board) attacks this turn;
        // others start next turn.
        bool haste = def.card.HasKeyword(Keyword::Haste)
                  || HasHasteFromLords(def.card, state.battlefield, state.active_player_index);
        int  attacks = ExpectedAttacks(state);
        if (!haste && attacks > 0) { --attacks; }
        return power * attacks * DMG;
    }

    if (def.tmpl == CardTemplate::DrawSpell)
    {
        // 1 damage-equivalent per card: draw spells trail burn and creatures,
        // but are still worth casting when other options are exhausted.
        return def.params.draw * DMG;
    }

    if (def.tmpl == CardTemplate::DrawX)
    {
        return DMG;  // minimal X=1 estimate
    }

    if (def.tmpl == CardTemplate::DrawUntilNonland)
    {
        // Estimate how many lands TH will draw (clairvoyant scan of the library top).
        int estimated_lands = 0;
        for (const Card& c : state.ActivePlayer().library)
        {
            auto cdef = CardDatabase::Instance().Lookup(c.m_name);
            bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
            if (!is_land) { break; }
            ++estimated_lands;
        }
        // Check for enabling permanents on the battlefield.
        bool has_no_max_hand = false;
        bool has_lands_edge  = false;
        int  lands_edge_rate = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            auto pdef = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!pdef) { continue; }
            if (pdef->params.no_max_hand_size) { has_no_max_hand = true; }
            if (pdef->params.discard_land_damage > 0)
            {
                has_lands_edge  = true;
                lands_edge_rate = pdef->params.discard_land_damage;
            }
        }
        // With Land's Edge active, each drawn land converts to direct damage.
        if (has_lands_edge)
        {
            return (estimated_lands + 1) * lands_edge_rate * DMG;
        }
        // With Reliquary Tower (no max hand size) but no Land's Edge, the drawn lands
        // accumulate for a future LE activation.  Card-draw value only; LE combo scored
        // by the plan evaluator's two-pass logic when LE is also in the plan.
        if (has_no_max_hand)
        {
            return (estimated_lands + 1) * DMG;
        }
        // No enabler in play: drawn lands accumulate in hand until a future LE/RT turn.
        // Value the draw normally; the plan evaluator's two-pass logic handles TH+LE
        // combos in the same plan, and Fix1 (SimulateEndAndStartNextTurn) ensures the
        // lookahead correctly models the no-discard case when RT is later in play.
        return (estimated_lands + 1) * DMG;
    }

    // Land's Edge: each land already in hand plus any held is worth discard_land_damage damage.
    if (def.params.discard_land_damage > 0)
    {
        int lands_in_hand = 0;
        for (const Card& c : state.ActivePlayer().hand)
        {
            auto cdef = CardDatabase::Instance().Lookup(c.m_name);
            if (cdef && cdef->card.IsLand()) { ++lands_in_hand; }
        }
        return lands_in_hand * def.params.discard_land_damage * DMG;
    }

    // Cascade spells: value = free spell drawn (assume ~3 damage-equivalents on average).
    if (def.params.cascade_max_mv > 0)
    {
        return 3 * DMG;
    }

    // Aether Vial and similar: deploys a creature from hand for free each turn once charged.
    // Score as the best matching creature in hand. Lords boost all existing attackers
    // immediately on deployment (continuous effect, not blocked by summoning sickness);
    // haste creatures (via card or lord) attack the same turn deployed.
    // Apply a 1-attack penalty vs direct cast: Vial must reach target charge first.
    if (def.params.upkeep_adds_charge)
    {
        int best = 0;
        int target_mv = state.vial_target_mv;
        for (const Card& c : state.ActivePlayer().hand)
        {
            std::optional<CardDefinition> cdef = CardDatabase::Instance().Lookup(c.m_name);
            if (!cdef || !cdef->card.IsCreature()) { continue; }
            if (target_mv > 0 && cdef->card.m_mana_cost.ManaValue() != target_mv) { continue; }
            auto [lord_pb, lord_tb] = ComputeLordBonus(cdef->card, state.battlefield,
                                                        state.active_player_index);
            bool ds = cdef->card.HasKeyword(Keyword::DoubleStrike)
                   || HasDoubleStrikeFromLords(cdef->card, state.battlefield,
                                               state.active_player_index);
            int power = (cdef->card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
            bool haste = cdef->card.HasKeyword(Keyword::Haste)
                      || HasHasteFromLords(cdef->card, state.battlefield,
                                           state.active_player_index);
            int attacks = std::max(1, ExpectedAttacks(state) - (haste ? 1 : 2));
            best = std::max(best, power * attacks * DMG);
        }
        if (best > 0) { return best; }
        return std::max(3 * DMG / 4, ExpectedAttacks(state) * DMG - state.turn_number * 30);
    }

    return DMG;  // fallback for other spell types
}

// ---- CollectActions ------------------------------------------------------
//
// The single enumeration of action SOURCES, shared by Solve and EnumeratePlans.
// Returns every candidate play available this main phase as an Action:
//   - CastFromHand        : each legally-castable non-land spell in hand
//   - ActivateVial        : each (Aether Vial, matching-MV creature in hand) pair
//   - CastFromGraveyard   : each retrace card in the graveyard with a land to discard
// Land's Edge discards are generated as plan-level count variants by the callers,
// since they depend on the rest of the chosen subset (lands left after retrace).
// The per-Action valuation scalars are read by each caller's subset evaluator.
static std::vector<Action> CollectActions(const GameState& state, bool /*is_pre_combat*/)
{
    const Player& ap = state.ActivePlayer();
    bool has_creature_target = HasLegalCreatureTarget(state);
    int  n = static_cast<int>(ap.hand.size());

    std::vector<Action> actions;

    // --- Hand casts ---
    for (int i = 0; i < n; ++i)
    {
        auto opt = CardDatabase::Instance().Lookup(ap.hand[i].m_name);
        if (!opt || opt->card.IsLand()) { continue; }
        const CardDefinition& def = *opt;
        // Sorceries/non-flash spells require an empty stack and a main phase.
        bool timing_ok = def.card.IsInstant()
                      || def.card.HasKeyword(Keyword::Flash)
                      || state.stack.empty();
        if (!timing_ok) { continue; }

        // Skip spells that need a creature target when none exists.
        Targeting t = def.params.targeting;
        if ((t == Targeting::Creature || t == Targeting::Multi) && !has_creature_target)
        {
            continue;
        }

        // Skip X spells (X=0 is useless; proper X selection is future work).
        if (def.card.m_mana_cost.has_x) { continue; }

        // Only count damage that actually reaches the opponent's life total.
        // Creature-only targeting (e.g. Searing Blood) deals damage to a permanent,
        // not to the player, so it doesn't contribute to face-lethal calculations.
        int direct = 0;
        if (def.tmpl == CardTemplate::DirectDamage
            && def.params.targeting != Targeting::Creature)
        {
            direct = def.params.damage;
            if (def.params.landfall_damage > 0 && ap.lands_played_this_turn > 0)
            {
                direct = def.params.landfall_damage;
            }
        }

        Action a;
        a.kind                  = Action::Kind::CastFromHand;
        a.card_name             = ap.hand[i].m_name;
        a.hand_index            = i;
        a.cost                  = EffectiveCost(def, state);
        a.sacrifice_land        = def.params.sacrifice_land;
        a.eval                  = EvalCard(def, state);
        a.direct_damage         = direct;
        a.is_noncreature        = !def.card.IsCreature();
        a.card_mv               = def.card.m_mana_cost.ManaValue();
        a.is_draw               = (def.tmpl == CardTemplate::DrawSpell
                                   || def.tmpl == CardTemplate::DrawX);
        a.has_spectacle         = def.params.spectacle_cost.has_value();
        a.is_draw_until_nonland = (def.tmpl == CardTemplate::DrawUntilNonland);
        a.discard_land_damage   = def.params.discard_land_damage;
        actions.push_back(std::move(a));
    }

    // --- Aether Vial activations: one per (Vial, creature) pair ---
    {
        constexpr int DMG = 100;
        int bf_size = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_size; ++vi)
        {
            const Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            std::optional<CardDefinition> vdef =
                CardDatabase::Instance().Lookup(vp.card.m_name);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }

            int target_mv = vp.charge_counters;
            for (int i = 0; i < n; ++i)
            {
                std::optional<CardDefinition> copt =
                    CardDatabase::Instance().Lookup(ap.hand[i].m_name);
                if (!copt || !copt->card.IsCreature()) { continue; }
                if (copt->card.m_mana_cost.ManaValue() != target_mv) { continue; }

                auto [lord_pb, lord_tb] = ComputeLordBonus(
                    copt->card, state.battlefield, state.active_player_index);
                bool ds = copt->card.HasKeyword(Keyword::DoubleStrike)
                       || HasDoubleStrikeFromLords(copt->card, state.battlefield,
                                                   state.active_player_index);
                int power = (copt->card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
                bool haste = copt->card.HasKeyword(Keyword::Haste)
                          || HasHasteFromLords(copt->card, state.battlefield,
                                               state.active_player_index);
                int attacks = ExpectedAttacks(state);
                if (!haste && attacks > 0) { --attacks; }

                Action a;
                a.kind              = Action::Kind::ActivateVial;
                a.card_name         = ap.hand[i].m_name;
                a.hand_index        = i;
                a.eval              = power * attacks * DMG;
                a.is_noncreature    = false;
                a.card_mv           = target_mv;
                a.vial_bf_index     = vi;
                a.vial_attack_power = haste ? power : 0;
                actions.push_back(std::move(a));
            }
        }
    }

    // --- Retrace: cast a retrace card from the graveyard (Throes of Chaos) ---
    // Additional cost: discard a land card from hand. The card is not exiled, so it
    // returns to the graveyard and can be retraced again on a later turn. One Action
    // per distinct card name (apply finds the first matching copy in the graveyard).
    {
        int lands_in_hand = 0;
        for (const Card& c : ap.hand)
        {
            auto cdef = CardDatabase::Instance().Lookup(c.m_name);
            if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
        }
        if (lands_in_hand > 0)
        {
            std::unordered_set<std::string> seen_gy;
            for (const Card& gc : ap.graveyard)
            {
                auto gdef = CardDatabase::Instance().Lookup(gc.m_name);
                if (!gdef || !gdef->params.retrace) { continue; }
                bool timing_ok = gdef->card.IsInstant()
                              || gdef->card.HasKeyword(Keyword::Flash)
                              || state.stack.empty();
                if (!timing_ok) { continue; }
                if (gdef->card.m_mana_cost.has_x) { continue; }
                if (!seen_gy.insert(gc.m_name).second) { continue; }

                Action a;
                a.kind                  = Action::Kind::CastFromGraveyard;
                a.card_name             = gc.m_name;
                a.hand_index            = -1;            // sourced from graveyard, not hand
                a.cost                  = EffectiveCost(*gdef, state);
                a.discard_lands         = 1;            // discard one land as the retrace cost
                a.eval                  = EvalCard(*gdef, state);
                a.is_noncreature        = !gdef->card.IsCreature();
                a.card_mv               = gdef->card.m_mana_cost.ManaValue();
                a.is_draw_until_nonland = (gdef->tmpl == CardTemplate::DrawUntilNonland);
                a.discard_land_damage   = gdef->params.discard_land_damage;
                actions.push_back(std::move(a));
            }
        }
    }

    return actions;
}

// ---- TurnSolver::Solve ---------------------------------------------------

TurnSolver::Plan TurnSolver::Solve(const GameState& state, bool is_pre_combat)
{
    const Player& ap = state.ActivePlayer();
    ManaPool pool             = BuildPool(state);
    ManaPool pool_noncreature = BuildNonCreaturePool(state);
    int total_lands  = CountLands(state);
    int pending_atk  = PendingAttackDamage(state);
    int prowess_attackers   = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    std::vector<Action> cands = CollectActions(state, is_pre_combat);
    int n = static_cast<int>(ap.hand.size());

    // Compute damage available from Land's Edge permanents already on the battlefield.
    // (discard_land_damage > 0 signals the card; rate = damage per land discarded)
    int lands_edge_rate = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (def && def->params.discard_land_damage > 0)
        {
            lands_edge_rate = std::max(lands_edge_rate, def->params.discard_land_damage);
        }
    }
    int lands_in_hand = 0;
    for (int i = 0; i < n; ++i)
    {
        auto def = CardDatabase::Instance().Lookup(ap.hand[i].m_name);
        if (def && def->card.IsLand()) { ++lands_in_hand; }
    }
    int base_lands_edge_dmg = lands_in_hand * lands_edge_rate;

    // Clairvoyant estimate of how many consecutive lands sit on top of the library
    // (what a Treasure Hunt cast this turn would draw into hand, minus the nonland).
    int th_lands_estimate = 0;
    for (const Card& c : ap.library)
    {
        auto def = CardDatabase::Instance().Lookup(c.m_name);
        bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (!is_land) { break; }
        ++th_lands_estimate;
    }

    int m = static_cast<int>(cands.size());
    Plan best;

    // Enumerate all non-empty subsets (mask=0 is "do nothing" and is the default).
    for (int mask = 1; mask < (1 << m); ++mask)
    {
        // Reject subsets that use the same hand card twice (e.g. cast + Vial same creature)
        // or that tap the same Vial twice.
        bool valid = true;
        for (int j = 0; j < m && valid; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            for (int k = j + 1; k < m; ++k)
            {
                if (!(mask & (1 << k))) { continue; }
                if (cands[j].hand_index >= 0
                    && cands[j].hand_index == cands[k].hand_index) { valid = false; break; }
                if (cands[j].kind == Action::Kind::ActivateVial
                    && cands[k].kind == Action::Kind::ActivateVial
                    && cands[j].vial_bf_index == cands[k].vial_bf_index)
                { valid = false; break; }
            }
        }
        if (!valid) { continue; }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count   = 0;
        int noncreature_count = 0;
        int direct_dmg        = 0;
        int total_eval        = 0;
        int self_damage       = 0;
        int vial_haste_atk    = 0;
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)
        // Land's Edge damage added by casting Land's Edge or Treasure Hunt this plan.
        int plan_le_dmg       = 0;

        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            const Action& c = cands[j];
            discard_lands_used += c.discard_lands;

            combined.white     += c.cost.white;
            combined.blue      += c.cost.blue;
            combined.black     += c.cost.black;
            combined.red       += c.cost.red;
            combined.green     += c.cost.green;
            combined.colorless += c.cost.colorless;
            combined.generic   += c.cost.generic;

            if (c.is_noncreature)
            {
                noncreature_combined.white     += c.cost.white;
                noncreature_combined.blue      += c.cost.blue;
                noncreature_combined.black     += c.cost.black;
                noncreature_combined.red       += c.cost.red;
                noncreature_combined.green     += c.cost.green;
                noncreature_combined.colorless += c.cost.colorless;
                noncreature_combined.generic   += c.cost.generic;
            }

            if (c.sacrifice_land)    { ++sacrifice_count; }
            if (c.is_noncreature)    { ++noncreature_count; }
            direct_dmg        += c.direct_damage;
            total_eval        += c.eval;
            vial_haste_atk    += c.vial_attack_power;

            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }

            // Land's Edge being cast: if no LE is on board yet, this plan enables it.
            // Add the damage potential from discarding the current hand's lands.
            if (lands_edge_rate == 0 && c.discard_land_damage > 0)
            {
                plan_le_dmg += lands_in_hand * c.discard_land_damage;
            }
            // Treasure Hunt in a plan where Land's Edge is active (board or this plan):
            // th_lands_estimate new lands become available for LE discards.
            if (c.is_draw_until_nonland)
            {
                int active_rate = (lands_edge_rate > 0)
                    ? lands_edge_rate
                    : 0;  // LE may also be in this plan; picked up below second pass
                if (active_rate > 0)
                {
                    plan_le_dmg += th_lands_estimate * active_rate;
                }
            }
        }

        // Second pass: if TH is in the plan and LE is being cast this plan (not already on board),
        // add the TH bonus lands to plan_le_dmg now that we know LE is present.
        if (lands_edge_rate == 0)
        {
            bool has_le  = false;
            bool has_th  = false;
            for (int j = 0; j < m; ++j)
            {
                if (!(mask & (1 << j))) { continue; }
                if (cands[j].discard_land_damage > 0) { has_le = true; }
                if (cands[j].is_draw_until_nonland) { has_th = true; }
            }
            if (has_le && has_th) { plan_le_dmg += th_lands_estimate * 2; }
        }

        if (!pool.CanPay(combined))                          { continue; }
        if (!pool_noncreature.CanPay(noncreature_combined))  { continue; }
        if (sacrifice_count > total_lands)                   { continue; }
        if (discard_lands_used > lands_in_hand)              { continue; }

        // Eidolon-style on-cast triggers go on top of the spell being cast (CR 603), so
        // they resolve BEFORE the spell. A plan that kills us via self-damage cannot win —
        // we die to the trigger before the spell deals its damage.
        if (self_damage >= ap.life) { continue; }

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg + base_lands_edge_dmg + plan_le_dmg)
                    >= state.Opponent().life;

        // A winning plan always beats a non-winning plan.
        // Among plans with the same win status, prefer higher total eval.
        bool better = (!best.wins_this_turn && wins)
                   || (wins == best.wins_this_turn && total_eval > best.value);

        if (better)
        {
            best.actions.clear();
            for (int j = 0; j < m; ++j)
            {
                if (!(mask & (1 << j))) { continue; }
                best.actions.push_back(cands[j]);
            }
            best.value          = total_eval;
            best.wins_this_turn = wins;
        }
    }

    return best;
}

// ============================================================
// Multi-turn lookahead
// ============================================================

// Tap mana sources in-place to pay a cost. Returns false if mana is unavailable.
// for_creature: if false, skip creature-only mana sources (e.g. Ancient Ziggurat)
//               since non-creature spells cannot be paid with that mana.
static bool TapForCostDirect(GameState& state, const ManaCost& cost, bool for_creature = true)
{
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed

    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap());
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        return true;
    };

    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        p.tapped = true;
        DecrementDepletionOnTap(p);
        if (def.params.tap_self_damage > 0) { state.players[active].life -= def.params.tap_self_damage; }
        floating.Add(col, ManaProducedPerTap(def));
    };

    std::function<bool(Color,bool)> produce = [&](Color needed, bool any) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def || def->params.is_filter || !usable(p, *def)) { continue; }
            Color col;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                col = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                col = needed;
            }
            tap_source(p, *def, col);
            return true;
        }

        // 2) Filter land colourless mode ({T}: Add {C}) — for a generic or {C} pip.
        if (any || needed == Color::Colorless)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
                if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
                p.tapped = true;
                floating.Add(Color::Colorless, 1);
                return true;
            }
        }

        // 3) Filter mode for a coloured pip: feed one of the filter's colours, yield 2.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
            Color out;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                out = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                out = needed;
            }
            bool have_input = false;
            for (Color c : def->params.produces)
            {
                ManaPool probe = floating;
                if (ConsumeFloating(probe, c)) { have_input = true; break; }
            }
            if (!have_input)
            {
                bool fed = false;
                for (Color ic : def->params.produces)
                {
                    for (Permanent& s : state.battlefield)
                    {
                        if (s.controller_index != active || s.tapped) { continue; }
                        std::optional<CardDefinition> sd = CardDatabase::Instance().Lookup(s.card.m_name);
                        if (!sd || sd->params.is_filter || !usable(s, *sd)) { continue; }
                        bool m = false;
                        for (Color c : sd->params.produces) { if (c == ic) { m = true; break; } }
                        if (!m) { continue; }
                        tap_source(s, *sd, ic);
                        fed = true; break;
                    }
                    if (fed) { break; }
                }
                if (!fed) { continue; }
            }
            for (Color c : def->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            p.tapped = true;
            floating.Add(out, 2);
            return true;
        }
        return false;
    };

    auto pay = [&](Color needed, bool any) -> bool
    {
        if (!produce(needed, any)) { return false; }
        if (any) { Color took; return ConsumeFloatingAny(floating, took); }
        return ConsumeFloating(floating, needed);
    };

    for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) { return false; } }
    for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) { return false; } }
    for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) { return false; } }
    for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) { return false; } }
    for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) { return false; } }
    for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) { return false; } }
    for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) { return false; } }
    return true;
}

// Apply a plan to the game state sequentially (bypassing the stack).
// Mana is tapped in-place as each spell is cast rather than accumulated and
// tapped at the end, so the remaining pool is always correct at each step.
// Draw spells act as breakpoints: after drawing, Solve re-runs on the updated
// state so newly revealed cards can be cast with remaining mana this turn.
static void ApplyPlanDirect(GameState& state, const TurnSolver::Plan& plan, bool is_pre_combat)
{
    Player& ap  = state.ActivePlayer();
    int opp_idx = 1 - state.active_player_index;

    // Deploy creatures via Aether Vial before casting spells so lord effects are live.
    auto apply_vial = [&](const std::string& name)
    {
        std::optional<CardDefinition> copt = CardDatabase::Instance().Lookup(name);
        if (!copt || !copt->card.IsCreature()) { return; }
        int mv = copt->card.m_mana_cost.ManaValue();
        auto hand_it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (hand_it == ap.hand.end()) { return; }
        int bf_sz = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_sz; ++vi)
        {
            Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            std::optional<CardDefinition> vdef = CardDatabase::Instance().Lookup(vp.card.m_name);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }
            if (vp.charge_counters != mv) { continue; }
            Permanent perm;
            perm.card              = copt->card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            ap.hand.erase(hand_it);
            state.battlefield[vi].tapped = true;  // access by index — push_back may reallocate
            return;
        }
    };
    // Forward-declared so apply_one's draw breakpoints can re-apply a freshly solved
    // sub-plan (newly drawn castables) through the same canonical-order dispatch.
    std::function<void(const std::vector<Action>&)> apply_plan_actions;

    std::function<void(const std::string&, bool, bool, int)> apply_one;
    apply_one = [&](const std::string& name, bool is_sacrifice, bool from_graveyard, int discard_lands)
    {
        std::optional<CardDefinition> opt = CardDatabase::Instance().Lookup(name);
        if (!opt) { return; }
        const CardDefinition& def = *opt;

        std::vector<Card>& zone = from_graveyard ? ap.graveyard : ap.hand;
        std::vector<Card>::iterator it = std::find_if(zone.begin(), zone.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == zone.end()) { return; }

        bool is_creature = def.card.IsCreature();
        ManaCost ec = EffectiveCost(def, state);
        if (!TapForCostDirect(state, ec, is_creature)) { return; }
        zone.erase(it);

        // Retrace additional cost: discard `discard_lands` land cards from hand.
        // (The mana cost was paid above; CollectActions ensured enough lands exist.)
        if (from_graveyard && discard_lands > 0)
        {
            int discarded = 0;
            for (std::vector<Card>::iterator hit = ap.hand.begin();
                 hit != ap.hand.end() && discarded < discard_lands; )
            {
                std::optional<CardDefinition> hdef = CardDatabase::Instance().Lookup(hit->m_name);
                bool is_land = hdef ? hdef->card.IsLand() : hit->IsLand();
                if (is_land) { ap.graveyard.push_back(*hit); hit = ap.hand.erase(hit); ++discarded; }
                else         { ++hit; }
            }
        }

        if (def.tmpl == CardTemplate::DirectDamage)
        {
            Targeting t = def.params.targeting;
            if (t == Targeting::Any || t == Targeting::Player)
            {
                state.players[opp_idx].life -= def.params.damage;
                if (def.params.damage > 0) { state.opponent_lost_life_this_turn = true; }
            }
        }
        else if (is_creature)
        {
            Permanent perm;
            perm.card              = def.card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);

            // Replicate: if Hatchery Sliver (or the card itself) grants replicate,
            // pay the mana cost additional times to create token copies.
            // Replicate cost = printed mana cost (CR 702.56a), not the effective cast cost.
            if (CanReplicate(def, state.battlefield, state.active_player_index))
            {
                ManaCost rep_cost = def.card.m_mana_cost;
                ManaPool remaining = BuildPool(state);
                while (remaining.CanPay(rep_cost))
                {
                    if (!TapForCostDirect(state, rep_cost, true)) { break; }
                    Permanent token = perm;
                    token.card.m_number = 0;
                    state.battlefield.push_back(token);
                    remaining = BuildPool(state);
                }
            }
        }
        else if (def.tmpl == CardTemplate::DrawSpell)
        {
            int n = std::min(def.params.draw, static_cast<int>(ap.library.size()));
            ap.library.DrawN(n, ap.hand);

            // Draw breakpoint: re-solve with updated hand and remaining mana so
            // newly revealed cards can be cast with mana still available this turn.
            TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
            apply_plan_actions(extra.actions);
        }
        else if (def.tmpl == CardTemplate::DrawUntilNonland)
        {
            // Draw cards from the top until a nonland is found (inclusive) into hand.
            while (!ap.library.empty())
            {
                Card c = ap.library.DrawTop();
                auto cdef = CardDatabase::Instance().Lookup(c.m_name);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                ap.hand.push_back(std::move(c));
                if (!is_land) { break; }
            }
            // Draw breakpoint: re-solve so any new castables are played with remaining mana.
            TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
            apply_plan_actions(extra.actions);
        }
        else if (def.params.cascade_max_mv > 0)
        {
            // Cascade: exile from top until a nonland with MV < cascade_max_mv is found.
            int limit = def.params.cascade_max_mv;
            std::vector<Card> exiled;
            int cascade_idx = -1;
            while (!ap.library.empty())
            {
                Card c = ap.library.DrawTop();
                auto cdef = CardDatabase::Instance().Lookup(c.m_name);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                int  mv      = cdef ? cdef->card.m_mana_cost.ManaValue()
                                    : c.m_mana_cost.ManaValue();
                exiled.push_back(std::move(c));
                if (!is_land && mv < limit)
                {
                    cascade_idx = static_cast<int>(exiled.size()) - 1;
                    break;
                }
            }
            // All non-target cards return to library bottom in exile order.
            for (int ei = 0; ei < static_cast<int>(exiled.size()); ++ei)
            {
                if (ei == cascade_idx) { continue; }
                ap.library.push_back(std::move(exiled[ei]));
            }
            // Cast the cascade target for free: place it in hand so apply_one finds it.
            if (cascade_idx >= 0)
            {
                const std::string& cname = exiled[cascade_idx].m_name;
                auto cdef2 = CardDatabase::Instance().Lookup(cname);
                if (cdef2)
                {
                    ap.hand.push_back(cdef2->card);
                    apply_one(cname, false, false, 0);
                }
            }
        }
        else if (!def.card.IsInstant() && !def.card.IsSorcery())
        {
            // Non-creature permanent (e.g. Aether Vial, Thrumming Hivepool): place on battlefield.
            Permanent perm;
            perm.card              = def.card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
        }

        // On-cast triggers fire when the spell is cast (CR 603.3), before it resolves.
        FireOnCastTriggers(state, def);
        FireProwess(state, def);

        // A resolved instant or sorcery goes to the graveyard (mirrors the real game's
        // MoveToGraveyard). This makes a retrace card recur and keeps the inline
        // graveyard faithful; nothing reads the graveyard for decks without retrace.
        if (def.card.IsInstant() || def.card.IsSorcery())
        {
            ap.graveyard.push_back(def.card);
        }

        if (is_sacrifice)
        {
            int idx = -1;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
                if (idx < 0)  { idx = i; }
                if (p.tapped) { idx = i; break; }
            }
            if (idx >= 0)
            {
                ap.graveyard.push_back(state.battlefield[idx].card);
                state.battlefield.erase(state.battlefield.begin() + idx);
            }
        }
    };

    // Canonical execution order, applied within each kind in plan order:
    //   ActivateVial -> hand casts (non-sacrifice) -> hand casts (sacrifice-land)
    //   -> graveyard casts (Retrace).  (DiscardToLandsEdge is added in a later phase.)
    apply_plan_actions = [&](const std::vector<Action>& acts)
    {
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::ActivateVial) { apply_vial(a.card_name); }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
            {
                apply_one(a.card_name, false, false, 0);
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            {
                apply_one(a.card_name, true, false, 0);
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromGraveyard)
            {
                apply_one(a.card_name, false, true, a.discard_lands);
            }
        }
    };

    apply_plan_actions(plan.actions);

    // Activate Land's Edge: discard all lands in hand after all spells resolve.
    // This mirrors GameEngine::MainPhase's post-stack ActivateLandsEdge call.
    {
        int rate = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            auto le_def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (le_def && le_def->params.discard_land_damage > 0)
            {
                rate = std::max(rate, le_def->params.discard_land_damage);
            }
        }
        if (rate > 0)
        {
            std::vector<Card> keep;
            for (Card& c : ap.hand)
            {
                auto cdef    = CardDatabase::Instance().Lookup(c.m_name);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                if (!is_land) { keep.push_back(std::move(c)); continue; }
                ap.graveyard.push_back(c);
                state.players[opp_idx].life -= rate;
                if (rate > 0) { state.opponent_lost_life_this_turn = true; }
            }
            ap.hand = std::move(keep);
        }
    }

    // Sacrifice depletion lands (e.g. Saprazzan Skerry) exhausted by this turn's taps.
    SacrificeDepletedLands(state);
}

// Deal combat damage: all eligible attackers hit the opponent.
static void SimulateCombat(GameState& state)
{
    int opp_idx = 1 - state.active_player_index;
    std::vector<const Permanent*> attackers;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (!CanAttackFull(p, state.battlefield, state.active_player_index)) { continue; }
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, state.active_player_index, animated);
        bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, state.active_player_index, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, state.active_player_index));
        int base_pw = p.EffectivePower() + lord_pb;
        if (animated)
        {
            std::optional<CardDefinition> adef = CardDatabase::Instance().Lookup(p.card.m_name);
            if (adef) { base_pw += adef->params.animate_power; }
        }
        int power = base_pw * (ds ? 2 : 1);
        state.players[opp_idx].life -= power;
        if (power > 0) { state.opponent_lost_life_this_turn = true; }
        if (!p.card.HasKeyword(Keyword::Vigilance)) { p.tapped = true; }
        attackers.push_back(&p);
    }
    int trigger_dmg = CountAttackTriggerDamage(
        state.battlefield, state.active_player_index, attackers);
    if (trigger_dmg > 0)
    {
        state.players[opp_idx].life -= trigger_dmg;
        state.opponent_lost_life_this_turn = true;
    }
}


// Activate tap-and-pay token abilities (e.g. Sliver Hive) with any spare mana.
static void SimulateTapTokens(GameState& state)
{
    int active = state.active_player_index;
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != active
            || state.battlefield[i].tapped) { continue; }
        std::optional<CardDefinition> def =
            CardDatabase::Instance().Lookup(state.battlefield[i].card.m_name);
        if (!def || !def->params.tap_token_cost.has_value()) { continue; }

        if (!def->params.tap_token_requires_subtypes.empty())
        {
            bool found = false;
            for (int j = 0; j < bf_size; ++j)
            {
                if (state.battlefield[j].controller_index != active) { continue; }
                for (const std::string& req : def->params.tap_token_requires_subtypes)
                    for (const std::string& cs : state.battlefield[j].card.m_subtypes)
                        if (cs == req) { found = true; break; }
                if (found) { break; }
            }
            if (!found) { continue; }
        }

        const ManaCost& add_cost = def->params.tap_token_cost.value();
        state.battlefield[i].tapped = true;  // {T} cost; tap before building pool
        ManaPool remaining = BuildPool(state);
        if (!remaining.CanPay(add_cost)) { state.battlefield[i].tapped = false; continue; }
        TapForCostDirect(state, add_cost, true);

        // CreateToken appends to battlefield — never use stale refs after this point.
        CreateToken(state, active,
                    def->params.tap_token_power,
                    def->params.tap_token_toughness,
                    def->params.tap_token_subtypes);
    }
}

// Animate all animatable lands (e.g. Mutavault) if the active player has spare mana.
// Called after spells are cast, before combat, so the animated land can attack.
static void SimulateAnimateLands(GameState& state)
{
    int active = state.active_player_index;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped || p.is_animated) { continue; }
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def || !def->params.can_animate || !def->params.animate_cost.has_value()) { continue; }
        if (TapForCostDirect(state, def->params.animate_cost.value(), false))
        {
            p.is_animated = true;
        }
    }
}

// End-of-turn cleanup + start of next turn (untap, draw).
// Returns false if the player lost on draw (empty library).
static bool SimulateEndAndStartNextTurn(GameState& state)
{
    Player& ap = state.ActivePlayer();

    // Check for "no maximum hand size" permanent (e.g. Reliquary Tower) — if present,
    // skip the discard-to-7 step so the lookahead correctly models turns after RT is played.
    bool unlimited_hand = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }

    // Discard down to hand size 7 (always discard the highest MV card)
    while (!unlimited_hand && ap.hand.size() > 7)
    {
        auto worst = std::max_element(ap.hand.begin(), ap.hand.end(),
            [](const Card& a, const Card& b)
            {
                return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
            });
        ap.graveyard.push_back(*worst);
        ap.hand.erase(worst);
    }

    // Reset per-turn damage marks and animation effects
    for (Permanent& p : state.battlefield) { p.damage = 0; p.is_animated = false; }

    // Start of next turn
    ++state.turn_number;
    state.opponent_lost_life_this_turn = false;
    ap.lands_played_this_turn     = 0;
    ap.bonus_land_drops_this_turn = 0;

    // Untap and advance Aether Vial counters (upkeep trigger).
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index)
        {
            p.tapped            = false;
            p.entered_this_turn = false;
        }
    }
    Player& ap_upkeep = state.ActivePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def || !def->params.upkeep_adds_charge) { continue; }
        int optimal = (state.vial_target_mv > 0) ? state.vial_target_mv : p.charge_counters;
        if (p.charge_counters < optimal) { ++p.charge_counters; }
    }

    // Upkeep token creation (e.g. Thrumming Hivepool). Iterate over initial size only.
    int upkeep_bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < upkeep_bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != state.active_player_index) { continue; }
        std::optional<CardDefinition> def =
            CardDatabase::Instance().Lookup(state.battlefield[i].card.m_name);
        if (!def || def->params.upkeep_creates_tokens <= 0) { continue; }
        for (int t = 0; t < def->params.upkeep_creates_tokens; ++t)
        {
            CreateToken(state, state.active_player_index,
                        def->params.upkeep_token_power,
                        def->params.upkeep_token_toughness,
                        def->params.upkeep_token_subtypes);
        }
    }

    // Draw
    if (ap.library.empty()) { return false; }
    ap.hand.push_back(ap.library.DrawTop());
    return true;
}

// Play a land from hand (one land drop per turn).
// Prefers multi-color lands over colorless-only lands (e.g. Mutavault) so that
// colored spells remain castable. Two-pass: multi-color first, then any land.
static void SimulateLandPlay(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return; }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
        {
            auto def = CardDatabase::Instance().Lookup(it->m_name);
            if (!def || !def->card.IsLand()) { continue; }

            bool is_multi = def->params.produces.size() > 1;
            if (pass == 0 && !is_multi) { continue; }

            // Resolve "as this land enters" choices while the card is still in hand.
            bool tapped = LandEntersTapped(state, *def);
            Permanent perm;
            perm.card              = def->card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            perm.tapped            = tapped;
            if (def->params.enters_tapped_with_depletion > 0)
            {
                Counter dep;
                dep.type  = Counter::Type::Depletion;
                dep.count = def->params.enters_tapped_with_depletion;
                perm.counters.push_back(dep);
            }
            state.battlefield.push_back(perm);

            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            if (def->params.etb_scry > 0) { ScryTop(state, def->params.etb_scry); }
            return;
        }
    }
}

// Returns candidate plans for the current turn for use by SolveWithLookahead.
//
// Base set (unchanged from original): all 2^m feasible hand subsets, so the
// lookahead can compare any combination of spells including lower-value plans
// that may win faster than the greedy-optimal one.
//
// Added: "draw-early" Plan B variants for draw spells that have a Spectacle
// alternate cost (e.g. Light Up the Stage).  In these plans a cheap damage spell
// fires first to unlock Spectacle, then the draw spell resolves while mana is
// still available.  ApplyPlanDirect's post-draw re-solve then casts newly
// revealed cards with the remaining mana — a line that static evaluation cannot
// see.  The lookahead simulation compares these against the base plans and picks
// whichever leads to the earliest win.
static std::vector<TurnSolver::Plan> EnumeratePlans(const GameState& state, bool is_pre_combat)
{
    const Player& ap              = state.ActivePlayer();
    ManaPool      pool            = BuildPool(state);
    ManaPool      pool_noncreature = BuildNonCreaturePool(state);
    int           total_lands     = CountLands(state);
    int           pending_atk     = PendingAttackDamage(state);
    int           prowess_attackers    = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    // Shared enumeration of all action sources (hand casts + Vial + retrace; LE in a
    // later phase). The subset machinery below reads the per-Action valuation scalars.
    std::vector<Action> cands = CollectActions(state, is_pre_combat);
    int n = static_cast<int>(ap.hand.size());

    // Lands in hand: the shared budget for additional discard costs (retrace, LE).
    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        auto cdef = CardDatabase::Instance().Lookup(c.m_name);
        if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }

    int m = static_cast<int>(cands.size());
    std::vector<TurnSolver::Plan> plans;

    // --- Base set: all non-empty feasible subsets ---
    for (int mask = 1; mask < (1 << m); ++mask)
    {
        // Reject subsets that use the same hand card twice or tap the same Vial twice.
        bool valid = true;
        for (int j = 0; j < m && valid; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            for (int k = j + 1; k < m; ++k)
            {
                if (!(mask & (1 << k))) { continue; }
                if (cands[j].hand_index >= 0
                    && cands[j].hand_index == cands[k].hand_index) { valid = false; break; }
                if (cands[j].kind == Action::Kind::ActivateVial
                    && cands[k].kind == Action::Kind::ActivateVial
                    && cands[j].vial_bf_index == cands[k].vial_bf_index)
                { valid = false; break; }
            }
        }
        if (!valid) { continue; }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count   = 0;
        int noncreature_count = 0;
        int direct_dmg        = 0;
        int total_eval        = 0;
        int self_damage       = 0;
        int vial_haste_atk    = 0;
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)

        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            const Action& c = cands[j];
            discard_lands_used += c.discard_lands;
            combined.white     += c.cost.white;
            combined.blue      += c.cost.blue;
            combined.black     += c.cost.black;
            combined.red       += c.cost.red;
            combined.green     += c.cost.green;
            combined.colorless += c.cost.colorless;
            combined.generic   += c.cost.generic;
            if (c.is_noncreature)
            {
                noncreature_combined.white     += c.cost.white;
                noncreature_combined.blue      += c.cost.blue;
                noncreature_combined.black     += c.cost.black;
                noncreature_combined.red       += c.cost.red;
                noncreature_combined.green     += c.cost.green;
                noncreature_combined.colorless += c.cost.colorless;
                noncreature_combined.generic   += c.cost.generic;
            }
            if (c.sacrifice_land)   { ++sacrifice_count; }
            if (c.is_noncreature)   { ++noncreature_count; }
            direct_dmg     += c.direct_damage;
            total_eval     += c.eval;
            vial_haste_atk += c.vial_attack_power;
            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        if (!pool.CanPay(combined))                          { continue; }
        if (!pool_noncreature.CanPay(noncreature_combined))  { continue; }
        if (sacrifice_count > total_lands)                   { continue; }
        if (discard_lands_used > lands_in_hand)              { continue; }

        if (self_damage >= ap.life) { continue; }

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
        TurnSolver::Plan plan;
        plan.value          = total_eval;
        plan.wins_this_turn = wins;
        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            plan.actions.push_back(cands[j]);
        }
        plans.push_back(std::move(plan));
    }

    // --- Plan B: draw-early variants for Spectacle draw spells ---
    // Helper: field-by-field ManaCost addition.
    auto add_cost = [](ManaCost a, const ManaCost& b) -> ManaCost
    {
        a.white     += b.white;  a.blue  += b.blue;  a.black += b.black;
        a.red       += b.red;    a.green += b.green;
        a.colorless += b.colorless; a.generic += b.generic;
        return a;
    };

    struct TriggerCand { int idx; ManaCost cost; int damage; int eval; };
    std::vector<TriggerCand> triggers;
    for (int i = 0; i < m; ++i)
    {
        const Action& c = cands[i];
        if (c.direct_damage > 0 && !c.has_spectacle && !c.sacrifice_land)
        {
            triggers.push_back({i, c.cost, c.direct_damage, c.eval});
        }
    }
    std::sort(triggers.begin(), triggers.end(),
        [](const TriggerCand& a, const TriggerCand& b)
        {
            return a.cost.ManaValue() < b.cost.ManaValue();
        });

    for (int i = 0; i < m; ++i)
    {
        const Action& draw = cands[i];
        if (!draw.is_draw || !draw.has_spectacle) { continue; }

        ManaCost spectacle_cost = draw.cost; // already set to Spectacle cost if active
        // If Spectacle not yet active, we need a trigger first; use the Spectacle cost directly.
        std::optional<CardDefinition> draw_def =
            CardDatabase::Instance().Lookup(ap.hand[draw.hand_index].m_name);
        if (!draw_def || !draw_def->params.spectacle_cost.has_value()) { continue; }
        ManaCost spectacle_only = draw_def->params.spectacle_cost.value();

        bool spectacle_active = state.opponent_lost_life_this_turn;
        const TriggerCand* trigger = nullptr;

        if (!spectacle_active)
        {
            for (const TriggerCand& tc : triggers)
            {
                if (pool.CanPay(add_cost(tc.cost, spectacle_only)))
                {
                    trigger = &tc;
                    break;
                }
            }
            if (!trigger) { continue; }
        }
        else
        {
            if (!pool.CanPay(spectacle_only)) { continue; }
        }

        TurnSolver::Plan plan;
        int direct_dmg = 0;
        if (trigger)
        {
            // NOTE: legacy quirk preserved for byte-identical results — the trigger's
            // name is taken from ap.hand[trigger->idx], where trigger->idx is a
            // *candidate* index (lands are skipped when building candidates), not a
            // hand index. When the hand holds lands these diverge and the named card
            // may not be the intended trigger. Faithfully reproduced here; fixing it
            // is a separate behaviour change requiring a burn ground-truth regen.
            Action ta = cands[trigger->idx];
            ta.card_name = ap.hand[trigger->idx].m_name;
            plan.actions.push_back(ta);  // cheap damage spell unlocks Spectacle
            plan.value += trigger->eval;
            direct_dmg += trigger->damage;
        }
        plan.actions.push_back(draw);  // draw spell at its Spectacle cost
        plan.value += draw.eval;
        plan.wins_this_turn = (pending_atk + direct_dmg) >= state.Opponent().life;
        plans.push_back(std::move(plan));
    }

    // Sort so the highest-value plans are simulated first.  When a timeout fires
    // the search returns the best plan found so far, so we want the most promising
    // candidates evaluated before cheaper ones.
    std::sort(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn > b.wins_this_turn; }
            return a.value > b.value;
        });

    // Dedup by EFFECT signature. The powerset enumeration treats each copy of a
    // 4-of as a distinct candidate, so subsets that differ only in WHICH copy they
    // pick (e.g. Bolt #2 vs Bolt #5) produce byte-identical plans — a 2-16x phantom
    // blowup. ApplyPlanDirect resolves spells/sacrifices/Vial activations purely by
    // NAME (it finds the first matching card), so two plans with the same multiset
    // of names are indistinguishable downstream. Collapsing them is exactly lossless
    // and removes the duplicate inline-first-turn + transposition lookups they'd
    // otherwise each incur. Done after the sort so the surviving copy is the
    // highest-ranked (identical plans share rank, so order is unaffected either way).
    auto plan_signature = [](const TurnSolver::Plan& p) -> std::string
    {
        std::vector<std::string> v, s, a, g, l;
        for (const Action& act : p.actions)
        {
            switch (act.kind)
            {
                case Action::Kind::ActivateVial:      v.push_back(act.card_name); break;
                case Action::Kind::CastFromHand:
                    (act.sacrifice_land ? a : s).push_back(act.card_name);        break;
                case Action::Kind::CastFromGraveyard: g.push_back(act.card_name); break;
                case Action::Kind::DiscardToLandsEdge:
                    l.push_back(act.card_name + "#" + std::to_string(act.discard_lands)); break;
            }
        }
        std::sort(v.begin(), v.end());
        std::sort(s.begin(), s.end());
        std::sort(a.begin(), a.end());
        std::sort(g.begin(), g.end());
        std::sort(l.begin(), l.end());
        std::string sig;
        for (const std::string& n : v) { sig += 'V'; sig += n; }
        for (const std::string& n : s) { sig += 'S'; sig += n; }
        for (const std::string& n : a) { sig += 'A'; sig += n; }
        for (const std::string& n : g) { sig += 'G'; sig += n; }
        for (const std::string& n : l) { sig += 'L'; sig += n; }
        return sig;
    };
    std::unordered_set<std::string> seen;
    seen.reserve(plans.size() * 2);
    std::vector<TurnSolver::Plan> deduped;
    deduped.reserve(plans.size());
    for (TurnSolver::Plan& p : plans)
    {
        if (seen.insert(plan_signature(p)).second) { deduped.push_back(std::move(p)); }
    }

    return deduped;
}

// ---- Transposition key over the future-determining state ------------------
//
// Folds every game-state field the rollout reads into a 128-bit key, plus the
// rollout depth. Fields the rollout never reads (graveyard, exile, poison, the
// always-empty rollout stack contents) are omitted so genuinely-equivalent
// states share a key. Order-sensitive sequences (hand, battlefield) are folded
// in order because plan tie-breaks and mana/sacrifice selection read that order.
// The library is keyed by remaining size + top card (see TranspositionTable.h
// for why size alone is exact within one decision).
namespace
{
    inline void Fold(TranspositionTable::Key& k, uint64_t v)
    {
        k.h1 ^= v + 0x9e3779b97f4a7c15ULL + (k.h1 << 6) + (k.h1 >> 2);
        k.h2 ^= (v * 0xff51afd7ed558ccdULL) + 0xc4ceb9fe1a85ec53ULL
              + (k.h2 << 5) + (k.h2 >> 3);
    }

    inline void FoldName(TranspositionTable::Key& k, const std::string& s)
    {
        Fold(k, static_cast<uint64_t>(std::hash<std::string>{}(s)));
    }
}

static TranspositionTable::Key BuildSimKey(const GameState& state, int depth, int max_turns,
                                           bool second_main)
{
    TranspositionTable::Key k;

    Fold(k, 0x5117); // section tag: scalars
    Fold(k, static_cast<uint64_t>(depth));
    Fold(k, static_cast<uint64_t>(max_turns));
    Fold(k, second_main ? 1u : 0u);
    Fold(k, static_cast<uint64_t>(state.turn_number));
    Fold(k, static_cast<uint64_t>(state.active_player_index));
    Fold(k, state.on_the_play ? 1u : 0u);
    Fold(k, state.opponent_lost_life_this_turn ? 1u : 0u);
    Fold(k, static_cast<uint64_t>(state.vial_target_mv));
    Fold(k, static_cast<uint64_t>(state.stack.size()));

    for (int pi = 0; pi < 2; ++pi)
    {
        const Player& p = state.players[pi];
        Fold(k, 0x9100 + static_cast<uint64_t>(pi)); // section tag: player pi
        Fold(k, static_cast<uint64_t>(p.life));
        Fold(k, static_cast<uint64_t>(p.lands_played_this_turn));
        Fold(k, static_cast<uint64_t>(p.bonus_land_drops_this_turn));
        Fold(k, static_cast<uint64_t>(p.library.size()));
        if (!p.library.empty()) { FoldName(k, p.library.front().m_name); }

        Fold(k, 0x4A00 + static_cast<uint64_t>(pi)); // sub-section: hand (ordered)
        Fold(k, static_cast<uint64_t>(p.hand.size()));
        for (const Card& c : p.hand) { FoldName(k, c.m_name); }

        Fold(k, 0x57A6 + static_cast<uint64_t>(pi)); // sub-section: staged cards
        Fold(k, static_cast<uint64_t>(p.staged_cards.size()));
        for (const StagedCard& sc : p.staged_cards)
        {
            FoldName(k, sc.card.m_name);
            Fold(k, static_cast<uint64_t>(sc.expiry_turn));
        }

        // Graveyard: the rollout reads it only via Retrace, so fold it ONLY when it
        // holds a retrace-castable card. Decks without retrace never fold it and keep
        // the exact key — and identical TT/budget behaviour — they had before. Folded
        // order-insensitively (commutative sum of name hashes): two states differing
        // only in cast/discard order share a key, so this never adds spurious misses.
        {
            uint64_t gy_acc       = 0;
            bool     gy_retraceable = false;
            for (const Card& c : p.graveyard)
            {
                gy_acc += static_cast<uint64_t>(std::hash<std::string>{}(c.m_name));
                std::optional<CardDefinition> cdef = CardDatabase::Instance().Lookup(c.m_name);
                if (cdef && cdef->params.retrace) { gy_retraceable = true; }
            }
            if (gy_retraceable)
            {
                Fold(k, 0x6748 + static_cast<uint64_t>(pi)); // sub-section: graveyard (retrace live)
                Fold(k, static_cast<uint64_t>(p.graveyard.size()));
                Fold(k, gy_acc);
            }
        }
    }

    Fold(k, 0xB1F1); // section tag: battlefield (ordered)
    Fold(k, static_cast<uint64_t>(state.battlefield.size()));
    for (const Permanent& perm : state.battlefield)
    {
        FoldName(k, perm.card.m_name);
        Fold(k, static_cast<uint64_t>(perm.controller_index));
        Fold(k, perm.tapped ? 1u : 0u);
        Fold(k, perm.entered_this_turn ? 1u : 0u);
        Fold(k, perm.is_animated ? 1u : 0u);
        Fold(k, static_cast<uint64_t>(perm.charge_counters));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_power_bonus)));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_tough_bonus)));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.damage)));
        for (const Counter& ctr : perm.counters)
        {
            Fold(k, static_cast<uint64_t>(ctr.type));
            Fold(k, static_cast<uint64_t>(static_cast<int64_t>(ctr.count)));
        }
    }

    return k;
}

// Simulate from the current state (at the START of a pre-combat main phase,
// land already played) to game end. Uses SolveWithLookahead(depth) for
// pre-combat decisions. A post-combat (second) main is played only when
// second_main is set (greedy, via Solve) — see AIEngine::TakeTurn for why it is
// otherwise skipped. Returns the win turn, or max_turns+1 if not won in time.
static int SimulateToEndImpl(GameState& state, int depth, int max_turns,
                             SearchBudget* budget, int cutoff_turn,
                             bool second_main, TranspositionTable* tt)
{
    while (state.turn_number <= max_turns)
    {
        // Branch-and-bound: a line that hasn't won by cutoff_turn cannot beat the
        // incumbent best win turn, so abandon it (win turn only grows from here).
        // Abort only AFTER cutoff_turn so a win exactly on cutoff_turn still
        // registers for the value tiebreak.
        if (state.turn_number > cutoff_turn) { return max_turns + 1; }

        // Count one work unit per simulated turn-step. The rollout never self
        // truncates on the budget — it only consumes; the top-level decision
        // (enforce_budget) is what decides when to stop adding more rollouts.
        if (budget) { budget->Consume(1); }

        // Pre-combat main: pick and apply plan (includes Vial activations), then animate + tokens
        TurnSolver::Plan pre_plan = TurnSolver::SolveWithLookahead(
            state, true, depth, max_turns, budget, false, second_main, tt);
        ApplyPlanDirect(state, pre_plan, true);
        SimulateAnimateLands(state);
        SimulateTapTokens(state);

        // Combat
        SimulateCombat(state);
        if (state.Opponent().life <= 0) { return state.turn_number; }

        // Post-combat (second) main, only for second-main-relevant decks (e.g.
        // spectacle finishers unlocked by combat damage). Played greedily here in
        // the rollout; the real game searches it. Skipped entirely otherwise — in a
        // goldfish combat creates no new resources, so everything was castable in
        // the first main, and modelling a second main the real game skips would let
        // the search optimise against plays that never happen. See AIEngine::TakeTurn.
        if (second_main)
        {
            TurnSolver::Plan post_plan = TurnSolver::Solve(state, false);
            ApplyPlanDirect(state, post_plan, false);
            if (state.Opponent().life <= 0) { return state.turn_number; }
        }

        // End of turn + start of next
        if (!SimulateEndAndStartNextTurn(state)) { return max_turns + 1; }
        SimulateLandPlay(state);
    }
    return max_turns + 1;
}

// Memoizing wrapper around SimulateToEndImpl. On a cache hit the rollout is
// skipped entirely (and no budget is consumed — avoided recompute is the point).
// Only REAL win turns (<= max_turns) are cached: they are exact and cutoff-
// independent, whereas a max_turns+1 result may be a branch-and-bound abort
// rather than a genuine no-win, so it is never stored. See TranspositionTable.h.
static int SimulateToEnd(GameState state, int depth, int max_turns,
                         SearchBudget* budget, int cutoff_turn,
                         bool second_main, TranspositionTable* tt)
{
    TranspositionTable::Key key;
    if (tt != nullptr)
    {
        key = BuildSimKey(state, depth, max_turns, second_main);
        const int* cached = tt->Lookup(key);
        if (cached != nullptr) { return *cached; }
    }

    int result = SimulateToEndImpl(state, depth, max_turns, budget, cutoff_turn, second_main, tt);

    if (tt != nullptr && result <= max_turns) { tt->Store(key, result); }
    return result;
}

// ---- Public API ----

// Estimate-and-skip tuning (deterministic budget). See project-deterministic-budget.
namespace
{
    // Start gate: begin pass k only if its estimated cost <= alpha * remaining
    // budget; a little over (>1.0) is allowed since the overrun guard backs it up.
    constexpr double kStartGateAlpha = 1.10;
    // Bootstrap growth ratio used for pass 1's estimate, before two completed
    // passes exist to measure a real C_{k-1}/C_{k-2} branching ratio.
    constexpr double kDefaultGrowth = 6.0;
    // Overrun guard: once a pass is running past budget, abort + roll back only
    // when it has spent more than beta * the budget it started with AND finishing
    // is still expensive (see below). "Almost done" passes always finish.
    constexpr double kOverrunBeta = 2.0;
}

TurnSolver::Plan TurnSolver::SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                                  int depth, int max_turns,
                                                  SearchBudget* budget, bool enforce_budget,
                                                  bool second_main, TranspositionTable* tt)
{
    if (depth <= 0)
    {
        if (budget) { budget->Consume(1); }
        return Solve(state, is_pre_combat);
    }

    // The enforcing top-level call owns the per-decision transposition table and
    // threads it through the whole recursion; rollout sub-searches reuse the table
    // they were handed. local_table is only referenced when we create it here.
    TranspositionTable  local_table;
    if (tt == nullptr && enforce_budget) { tt = &local_table; }

    std::vector<Plan> candidates = EnumeratePlans(state, is_pre_combat);

    // Candidates are sorted highest-value first, so the first winning plan
    // (if any) is also the highest-value winning plan.
    for (const Plan& p : candidates)
    {
        if (p.wins_this_turn) { return p; }
    }

    if (candidates.empty()) { return Plan{}; }

    // Iterative deepening: evaluate EVERY candidate at increasing rollout depth
    // (sub_depth = 0, 1, ... depth-1), and only commit a pass's result once that
    // pass has fully completed. When the budget runs out we fall back to the best
    // plan from the last fully-completed depth, so the decision always reflects a
    // COMPLETE comparison of all candidates (at some depth) rather than a partial
    // ranking. This guarantees a low-ranked but winning line is never starved:
    // even the cheap depth-0 pass plays each rollout out and discovers its win.
    //
    // Fidelity-consistent ranking: each pass ranks candidates at its OWN sub_depth
    // with a FRESH incumbent (we do NOT seed the decision from the previous,
    // shallower pass), and we commit the deepest fully-completed pass's own best.
    // A deeper rollout is the more reliable estimate, so its ranking wins outright —
    // carrying a shallower pass's (often optimistic) win turn as the incumbent would
    // let a stale shallow estimate out-rank a deeper-confirmed equal win. The
    // branch-and-bound cutoff is the WITHIN-pass running best, so SimulateToEnd is
    // only ever compared at one fidelity per pass and the value tiebreak resolves
    // genuinely-equal win turns. (Cost: we lose the cross-pass cutoff's head start,
    // re-discovering the bound each pass — accepted for consistency.)
    //
    // Budget control (only when enforce_budget; the rollout sub-search runs every
    // pass to completion and merely consumes). Two deterministic gates, both keyed
    // on the running work-unit count — never the clock — so a deeper search makes
    // the same start/skip/abort decisions and can never come out worse:
    //   1. START GATE: before pass k, estimate its cost from the measured growth
    //      of the previous passes and skip the whole pass (committing pass k-1) if
    //      it clearly won't fit. No point starting a pass that would be cut off
    //      mid-sweep and discarded anyway.
    //   2. OVERRUN GUARD: once a pass has started, run it PAST the budget to
    //      completion — sunk work near completion shouldn't be thrown away. Abort
    //      and roll back to pass k-1 only when the pass is BOTH well over budget
    //      AND still expensive to finish. Roll-back is free: each candidate runs on
    //      a GameState copy, so a pass only touches pass-local state.
    Plan best_plan = candidates.front();

    const bool   gate         = enforce_budget && budget != nullptr;
    long long    c_prev       = 0;       // cost of pass k-1 (work units)
    long long    c_prev2      = 0;       // cost of pass k-2
    bool         have_prev    = false;
    bool         have_prev2   = false;

    for (int sub_depth = 0; sub_depth <= depth - 1; ++sub_depth)
    {
        long long remaining_at_start = budget ? budget->Remaining() : LLONG_MAX;

        // --- Start gate: skip a pass we estimate won't fit (commit pass k-1) ---
        if (gate && sub_depth > 0)
        {
            double ratio    = (have_prev2 && c_prev2 > 0)
                            ? static_cast<double>(c_prev) / static_cast<double>(c_prev2)
                            : kDefaultGrowth;
            double estimate = static_cast<double>(c_prev) * ratio;
            if (estimate > kStartGateAlpha * static_cast<double>(remaining_at_start))
            {
                break;
            }
        }

        long long used_before     = budget ? budget->Used() : 0;
        Plan      pass_best        = candidates.front();
        int       pass_best_win    = max_turns + 1;
        bool      pass_has_best    = false;
        bool      pass_aborted     = false;
        long long candidates_done  = 0;

        // Trace T1 top-level decisions (enforce_budget=true) AND T2 rollout decisions
        // where depth==3 (fired from the sub_depth=3 pass inside SimulateToEnd).
        const bool trace_t1 = s_trace_solve && enforce_budget
                              && state.turn_number == 1 && is_pre_combat;
        const bool trace_t2 = s_trace_solve && !enforce_budget
                              && state.turn_number == 2 && depth == 3 && is_pre_combat;
        const bool trace_this = trace_t1 || trace_t2;
        if (trace_this)
        {
            std::cerr << "[trace] T" << state.turn_number
                      << (enforce_budget ? " top-level" : " rollout")
                      << " sub_depth=" << sub_depth
                      << "  candidates=" << candidates.size() << "\n";
        }

        for (const Plan& plan : candidates)
        {
            // --- Overrun guard: finish if almost done, else abort + roll back ---
            if (gate && budget->Exhausted() && candidates_done > 0)
            {
                long long pass_used       = budget->Used() - used_before;
                double    avg_per_cand    = static_cast<double>(pass_used)
                                          / static_cast<double>(candidates_done);
                long long remaining_cands = static_cast<long long>(candidates.size())
                                          - candidates_done;
                double    projected       = avg_per_cand
                                          * static_cast<double>(remaining_cands);
                bool      way_over         = static_cast<double>(pass_used)
                                          > kOverrunBeta * static_cast<double>(remaining_at_start);
                bool      finish_expensive = projected
                                          > static_cast<double>(remaining_at_start);
                if (way_over && finish_expensive)
                {
                    pass_aborted = true;
                    break;
                }
            }

            // One work unit for this candidate's inline first turn (combat + post
            // main); the remaining turns are counted inside SimulateToEnd.
            if (budget) { budget->Consume(1); }

            GameState copy = state;
            if (is_pre_combat)
            {
                ApplyPlanDirect(copy, plan, true);
                SimulateAnimateLands(copy);
                SimulateTapTokens(copy);

                // Combat this turn
                SimulateCombat(copy);
                if (copy.Opponent().life <= 0) { return plan; }

                // Post-combat (second) main if this deck wants one (greedy here).
                if (second_main)
                {
                    Plan post = Solve(copy, false);
                    ApplyPlanDirect(copy, post, false);
                    if (copy.Opponent().life <= 0) { return plan; }
                }
            }
            else
            {
                // Top-level post-combat (second) main decision: combat already
                // happened this turn, so apply the candidate as a post-combat play
                // and DON'T re-simulate combat (that would be a phantom second one).
                ApplyPlanDirect(copy, plan, false);
                if (copy.Opponent().life <= 0) { return plan; }
            }

            // End of this turn + start of next
            if (!SimulateEndAndStartNextTurn(copy)) { ++candidates_done; continue; }
            SimulateLandPlay(copy);

            // Simulate remaining turns at this pass's sub_depth. The rollout runs to
            // completion (enforce_budget=false inside), only consuming budget; the
            // within-pass running best (pass_best_win) is the branch-and-bound cutoff.
            int win_turn = SimulateToEnd(copy, sub_depth, max_turns, budget, pass_best_win,
                                         second_main, tt);
            if (trace_this)
            {
                std::cerr << "  " << PlanDesc(plan)
                          << "  val=" << plan.value
                          << "  win=" << win_turn << "\n";
            }
            bool better = !pass_has_best
                       || win_turn < pass_best_win
                       || (win_turn == pass_best_win && plan.value > pass_best.value);
            if (better)
            {
                pass_best_win = win_turn;
                pass_best     = plan;
                pass_has_best = true;
            }
            ++candidates_done;
        }

        if (pass_aborted)
        {
            if (trace_this)
            {
                std::cerr << "  [T" << state.turn_number << " ABORTED — keeping pass "
                          << (sub_depth-1) << " result]\n";
            }
            break;
        }

        if (pass_has_best)
        {
            best_plan = pass_best;
            if (trace_this)
            {
                std::cerr << "  -> T" << state.turn_number << " COMMITTED sub_depth=" << sub_depth
                          << ": " << PlanDesc(pass_best)
                          << "  win=" << pass_best_win << "\n";
            }
        }

        // Record this completed pass's cost for the next pass's estimate.
        if (budget)
        {
            c_prev2    = c_prev;
            have_prev2 = have_prev;
            c_prev     = budget->Used() - used_before;
            have_prev  = true;
        }
    }

    return best_plan;
}
