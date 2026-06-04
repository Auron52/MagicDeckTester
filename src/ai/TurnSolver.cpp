#include "TurnSolver.h"
#include "../core/ManaPool.h"
#include "../core/EffectHandler.h"
#include "../core/SpellEffects.h"
#include <algorithm>
#include <chrono>
#include <functional>

// ---- Local helpers -------------------------------------------------------

// Add a land's mana production to a pool.
// Single-color lands add to their specific color; multi-color lands add to wild
// (one tap = one mana of the player's choice, not one of each color).
static void AddLandToPool(ManaPool& pool, const CardDefinition& def)
{
    if (def.params.produces.size() == 1)
    {
        pool.Add(def.params.produces[0]);
    }
    else if (!def.params.produces.empty())
    {
        ++pool.wild;
    }
}

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
        AddLandToPool(pool, *def);
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
        AddLandToPool(pool, *def);
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

    // Aether Vial and similar: each turn after being played, the Vial deploys one
    // creature from hand for free. Value declines with a turn penalty so that:
    //   T1-T3: Vial beats a non-haste 1-drop (good opening plays)
    //   T4+:   direct board presence beats Vial (speed matters more late-game;
    //           lords and haste Slivers compound existing board, not future ones)
    // A floor of 3/4 * DMG keeps Vial as a valid filler if nothing else is castable.
    if (def.params.upkeep_adds_charge)
    {
        return std::max(3 * DMG / 4,
                        ExpectedAttacks(state) * DMG - state.turn_number * 30);
    }

    return DMG;  // fallback for other spell types
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
    bool has_creature_target = HasLegalCreatureTarget(state);

    // Build candidate list: non-land cards that are legally castable this phase.
    struct Candidate
    {
        int          hand_index;
        CardDefinition def;
        ManaCost     cost;
        int          eval;
        bool         sacrifice_land;
        bool         is_noncreature;
        int          card_mv;        // base MV of the card (used for trigger checks)
        int          direct_damage;
    };

    std::vector<Candidate> cands;
    int n = static_cast<int>(ap.hand.size());
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
        cands.push_back({i, def, EffectiveCost(def, state), EvalCard(def, state),
                         def.params.sacrifice_land,
                         !def.card.IsCreature(),
                         def.card.m_mana_cost.ManaValue(), direct});
    }

    int m = static_cast<int>(cands.size());
    Plan best;

    // Enumerate all non-empty subsets (mask=0 is "do nothing" and is the default).
    for (int mask = 1; mask < (1 << m); ++mask)
    {
        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count   = 0;
        int noncreature_count = 0;
        int direct_dmg        = 0;
        int total_eval        = 0;
        int self_damage       = 0;

        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            const Candidate& c = cands[j];

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
            direct_dmg += c.direct_damage;
            total_eval += c.eval;

            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        if (!pool.CanPay(combined))                          { continue; }
        if (!pool_noncreature.CanPay(noncreature_combined))  { continue; }
        if (sacrifice_count > total_lands)                   { continue; }

        // Eidolon-style on-cast triggers go on top of the spell being cast (CR 603), so
        // they resolve BEFORE the spell. A plan that kills us via self-damage cannot win —
        // we die to the trigger before the spell deals its damage.
        if (self_damage >= ap.life) { continue; }

        int projected_atk = pending_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;

        // A winning plan always beats a non-winning plan.
        // Among plans with the same win status, prefer higher total eval.
        bool better = (!best.wins_this_turn && wins)
                   || (wins == best.wins_this_turn && total_eval > best.value);

        if (better)
        {
            best.spells.clear();
            best.sacrifice.clear();
            for (int j = 0; j < m; ++j)
            {
                if (!(mask & (1 << j))) { continue; }
                const Candidate& c = cands[j];
                const std::string& name = ap.hand[c.hand_index].m_name;
                if (c.sacrifice_land) { best.sacrifice.push_back(name); }
                else                  { best.spells.push_back(name); }
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
    int active = state.active_player_index;

    std::function<bool(Color)> tap_color = [&](Color needed) -> bool
    {
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }
            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }
            if (def->params.creature_mana_only && !for_creature) { continue; }
            for (Color c : def->params.produces)
            {
                if (c == needed) { p.tapped = true; return true; }
            }
        }
        return false;
    };

    for (int i = 0; i < cost.white;     ++i) { if (!tap_color(Color::White))     { return false; } }
    for (int i = 0; i < cost.blue;      ++i) { if (!tap_color(Color::Blue))      { return false; } }
    for (int i = 0; i < cost.black;     ++i) { if (!tap_color(Color::Black))     { return false; } }
    for (int i = 0; i < cost.red;       ++i) { if (!tap_color(Color::Red))       { return false; } }
    for (int i = 0; i < cost.green;     ++i) { if (!tap_color(Color::Green))     { return false; } }
    for (int i = 0; i < cost.colorless; ++i) { if (!tap_color(Color::Colorless)) { return false; } }

    for (int i = 0; i < cost.generic; ++i)
    {
        bool found = false;
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }
            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }
            if (def->params.creature_mana_only && !for_creature) { continue; }
            p.tapped = true;
            found = true;
            break;
        }
        if (!found) { return false; }
    }
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

    std::function<void(const std::string&, bool)> apply_one;
    apply_one = [&](const std::string& name, bool is_sacrifice)
    {
        std::optional<CardDefinition> opt = CardDatabase::Instance().Lookup(name);
        if (!opt) { return; }
        const CardDefinition& def = *opt;

        std::vector<Card>::iterator it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == ap.hand.end()) { return; }

        bool is_creature = def.card.IsCreature();
        ManaCost ec = EffectiveCost(def, state);
        if (!TapForCostDirect(state, ec, is_creature)) { return; }
        ap.hand.erase(it);

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
            if (CanReplicate(def, state.battlefield, state.active_player_index))
            {
                ManaPool remaining = BuildPool(state);
                while (remaining.CanPay(ec))
                {
                    if (!TapForCostDirect(state, ec, true)) { break; }
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
            for (const std::string& extra_name : extra.spells)    { apply_one(extra_name, false); }
            for (const std::string& extra_name : extra.sacrifice) { apply_one(extra_name, true); }
        }

        // On-cast triggers fire when the spell is cast (CR 603.3), before it resolves.
        FireOnCastTriggers(state, def);
        FireProwess(state, def);

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

    for (const std::string& name : plan.spells)    { apply_one(name, false); }
    for (const std::string& name : plan.sacrifice) { apply_one(name, true); }
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

// Activate Aether Vials: put the best creature from hand with MV == charge_counters
// onto the battlefield for free. Mirrors the real-game logic in AIEngine::ActivateVials.
static void SimulateVialActivation(GameState& state)
{
    int active = state.active_player_index;
    Player& ap = state.ActivePlayer();
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int vi = 0; vi < bf_size; ++vi)
    {
        if (state.battlefield[vi].controller_index != active
            || state.battlefield[vi].tapped) { continue; }
        std::optional<CardDefinition> vdef =
            CardDatabase::Instance().Lookup(state.battlefield[vi].card.m_name);
        if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }

        int target_mv = state.battlefield[vi].charge_counters;
        std::vector<Card>::iterator best = ap.hand.end();
        int best_pw = -1;
        for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
        {
            std::optional<CardDefinition> cdef = CardDatabase::Instance().Lookup(it->m_name);
            if (!cdef || !cdef->card.IsCreature()) { continue; }
            if (cdef->card.m_mana_cost.ManaValue() != target_mv) { continue; }
            int pw = cdef->card.m_power.value_or(0);
            if (best == ap.hand.end() || pw > best_pw) { best = it; best_pw = pw; }
        }
        if (best == ap.hand.end()) { continue; }

        std::optional<CardDefinition> cdef = CardDatabase::Instance().Lookup(best->m_name);
        if (!cdef) { continue; }

        Permanent perm;
        perm.card              = cdef->card;
        perm.controller_index  = active;
        perm.owner_index       = active;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);  // may reallocate — never use old refs after this
        ap.hand.erase(best);
        state.battlefield[vi].tapped = true;
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

    // Discard down to hand size 7 (always discard the highest MV card)
    while (ap.hand.size() > 7)
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
        std::map<int, int> mv_count;
        for (const Card& c : ap_upkeep.hand)
        {
            std::optional<CardDefinition> cdef = CardDatabase::Instance().Lookup(c.m_name);
            if (!cdef || !cdef->card.IsCreature()) { continue; }
            int mv = cdef->card.m_mana_cost.ManaValue();
            if (mv > 0) { ++mv_count[mv]; }
        }
        int optimal = p.charge_counters;
        if (!mv_count.empty())
        {
            int best_mv = mv_count.begin()->first;
            int best_cnt = 0;
            for (const auto& kv : mv_count)
            {
                if (kv.second > best_cnt || (kv.second == best_cnt && kv.first < best_mv))
                { best_cnt = kv.second; best_mv = kv.first; }
            }
            optimal = best_mv;
        }
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

            Permanent perm;
            perm.card              = def->card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);

            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
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
    bool          has_target      = HasLegalCreatureTarget(state);

    struct Candidate
    {
        int         hand_index;
        ManaCost    cost;
        int         eval;
        bool        sacrifice_land;
        bool        is_noncreature;
        int         card_mv;
        bool        is_draw;
        bool        has_spectacle;
        int         direct_damage;
    };

    std::vector<Candidate> cands;
    int n = static_cast<int>(ap.hand.size());
    for (int i = 0; i < n; ++i)
    {
        std::optional<CardDefinition> opt = CardDatabase::Instance().Lookup(ap.hand[i].m_name);
        if (!opt || opt->card.IsLand()) { continue; }
        const CardDefinition& def = *opt;

        bool timing_ok = def.card.IsInstant()
                      || def.card.HasKeyword(Keyword::Flash)
                      || state.stack.empty();
        if (!timing_ok) { continue; }

        Targeting tgt = def.params.targeting;
        if ((tgt == Targeting::Creature || tgt == Targeting::Multi) && !has_target) { continue; }
        if (def.card.m_mana_cost.has_x) { continue; }

        Candidate c;
        c.hand_index     = i;
        c.cost           = EffectiveCost(def, state);
        c.eval           = EvalCard(def, state);
        c.sacrifice_land = def.params.sacrifice_land;
        c.is_noncreature = !def.card.IsCreature();
        c.card_mv        = def.card.m_mana_cost.ManaValue();
        c.is_draw        = (def.tmpl == CardTemplate::DrawSpell || def.tmpl == CardTemplate::DrawX);
        c.has_spectacle  = def.params.spectacle_cost.has_value();
        if (def.tmpl == CardTemplate::DirectDamage
            && def.params.targeting != Targeting::Creature)
        {
            c.direct_damage = def.params.damage;
            if (def.params.landfall_damage > 0 && ap.lands_played_this_turn > 0)
            {
                c.direct_damage = def.params.landfall_damage;
            }
        }
        else
        {
            c.direct_damage = 0;
        }
        cands.push_back(c);
    }

    int m = static_cast<int>(cands.size());
    std::vector<TurnSolver::Plan> plans;

    // --- Base set: all non-empty feasible subsets ---
    for (int mask = 1; mask < (1 << m); ++mask)
    {
        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count   = 0;
        int noncreature_count = 0;
        int direct_dmg        = 0;
        int total_eval        = 0;
        int self_damage       = 0;

        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            const Candidate& c = cands[j];
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
            if (c.sacrifice_land)  { ++sacrifice_count; }
            if (c.is_noncreature)  { ++noncreature_count; }
            direct_dmg += c.direct_damage;
            total_eval += c.eval;
            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        if (!pool.CanPay(combined))                          { continue; }
        if (!pool_noncreature.CanPay(noncreature_combined))  { continue; }
        if (sacrifice_count > total_lands)                   { continue; }

        if (self_damage >= ap.life) { continue; }

        int projected_atk = pending_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
        TurnSolver::Plan plan;
        plan.value          = total_eval;
        plan.wins_this_turn = wins;
        for (int j = 0; j < m; ++j)
        {
            if (!(mask & (1 << j))) { continue; }
            const Candidate& c = cands[j];
            const std::string& name = ap.hand[c.hand_index].m_name;
            if (c.sacrifice_land) { plan.sacrifice.push_back(name); }
            else                  { plan.spells.push_back(name); }
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
        const Candidate& c = cands[i];
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
        const Candidate& draw = cands[i];
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
            plan.spells.push_back(ap.hand[trigger->idx].m_name);
            plan.value += trigger->eval;
            direct_dmg += trigger->damage;
        }
        plan.spells.push_back(ap.hand[draw.hand_index].m_name);
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

    return plans;
}

// Simulate from the current state (at the START of a pre-combat main phase,
// land already played) to game end. Uses SolveWithLookahead(depth) for
// pre-combat decisions and Solve for post-combat decisions.
// Returns the win turn, or max_turns+1 if the game was not won in time.
static int SimulateToEnd(GameState state, int depth, int max_turns,
                         std::chrono::steady_clock::time_point deadline)
{
    while (state.turn_number <= max_turns)
    {
        if (std::chrono::steady_clock::now() >= deadline) { return max_turns + 1; }

        // Pre-combat main: activate Vials, pick and apply plan, then animate lands + tokens
        SimulateVialActivation(state);
        TurnSolver::Plan pre_plan = TurnSolver::SolveWithLookahead(state, true, depth, max_turns, deadline);
        ApplyPlanDirect(state, pre_plan, true);
        SimulateAnimateLands(state);
        SimulateTapTokens(state);

        // Combat
        SimulateCombat(state);
        if (state.Opponent().life <= 0) { return state.turn_number; }

        // Post-combat main (no lookahead — single-turn heuristic)
        TurnSolver::Plan post_plan = TurnSolver::Solve(state, false);
        ApplyPlanDirect(state, post_plan, false);

        if (state.Opponent().life <= 0) { return state.turn_number; }

        // End of turn + start of next
        if (!SimulateEndAndStartNextTurn(state)) { return max_turns + 1; }
        SimulateLandPlay(state);
    }
    return max_turns + 1;
}

// ---- Public API ----

TurnSolver::Plan TurnSolver::SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                                  int depth, int max_turns,
                                                  std::chrono::steady_clock::time_point deadline)
{
    if (depth <= 0) { return Solve(state, is_pre_combat); }

    std::vector<Plan> candidates = EnumeratePlans(state, is_pre_combat);

    // Candidates are sorted highest-value first, so the first winning plan
    // (if any) is also the highest-value winning plan.
    for (const Plan& p : candidates)
    {
        if (p.wins_this_turn) { return p; }
    }

    if (candidates.empty()) { return Plan{}; }

    // Initialise with the highest-value single-turn plan (candidates.front()
    // after sorting) so that a timeout at any point returns a reasonable result.
    Plan best_plan    = candidates.front();
    int  best_win_turn = max_turns + 1;

    for (const Plan& plan : candidates)
    {
        if (std::chrono::steady_clock::now() >= deadline) { break; }

        GameState copy = state;
        SimulateVialActivation(copy);
        ApplyPlanDirect(copy, plan, true);
        SimulateAnimateLands(copy);
        SimulateTapTokens(copy);

        // Combat this turn
        SimulateCombat(copy);
        if (copy.Opponent().life <= 0) { return plan; }

        // Post-combat main (single-turn)
        Plan post = Solve(copy, false);
        ApplyPlanDirect(copy, post, false);
        if (copy.Opponent().life <= 0) { return plan; }

        // End of this turn + start of next
        if (!SimulateEndAndStartNextTurn(copy)) { continue; }
        SimulateLandPlay(copy);

        // Simulate remaining turns with (depth-1) lookahead.
        // Pass max() so the sub-simulation runs to completion — the deadline
        // only controls how many top-level plans this function evaluates, not
        // how deeply each evaluated plan is simulated. Without this, a long
        // timeout causes early plans to consume most of the budget, leaving
        // later plans with truncated simulations that make them look worse
        // than they are, which can produce a worse overall decision.
        int win_turn = SimulateToEnd(copy, depth - 1, max_turns,
                                     std::chrono::steady_clock::time_point::max());
        bool better = win_turn < best_win_turn
                   || (win_turn == best_win_turn && plan.value > best_plan.value);
        if (better)
        {
            best_win_turn = win_turn;
            best_plan     = plan;
        }
    }

    return best_plan;
}
