#include "TurnSolver.h"
#include "../core/ManaPool.h"

// ---- Local helpers -------------------------------------------------------

static ManaPool BuildPool(const GameState& state)
{
    ManaPool pool;
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller != &ap || p.tapped) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap());
        if (!is_land && !is_dork) { continue; }
        for (Color c : def->params.produces) { pool.Add(c); }
    }
    return pool;
}

static int CountLands(const GameState& state)
{
    int n = 0;
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller == &ap && p.card.IsLand()) { ++n; }
    }
    return n;
}

static int PendingAttackDamage(const GameState& state)
{
    int dmg = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller == &state.ActivePlayer() && p.CanAttack())
        {
            dmg += p.EffectivePower();
        }
    }
    return dmg;
}

static bool HasLegalCreatureTarget(const GameState& state)
{
    const Player& opp = state.Opponent();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller == &opp && p.card.IsCreature()) { return true; }
    }
    return false;
}

static ManaCost EffectiveCost(const CardDefinition& def, const GameState& state)
{
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        return def.params.spectacle_cost.value();
    }
    return def.card.m_mana_cost;
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
        int power = def.card.m_power.value_or(0);
        if (power <= 0) { return 0; }

        // Haste creatures attack this turn; others start next turn.
        bool haste   = def.card.HasKeyword(Keyword::Haste);
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

    return DMG;  // fallback for other spell types
}

// ---- TurnSolver::Solve ---------------------------------------------------

TurnSolver::Plan TurnSolver::Solve(const GameState& state, bool is_pre_combat)
{
    const Player& ap = state.ActivePlayer();
    ManaPool pool    = BuildPool(state);
    int total_lands  = CountLands(state);
    int pending_atk  = PendingAttackDamage(state);
    bool has_creature_target = HasLegalCreatureTarget(state);

    // Build candidate list: non-land cards that are legally castable this phase.
    struct Candidate
    {
        int          hand_index;
        CardDefinition def;   // copied from database; optional is a temporary
        ManaCost     cost;
        int          eval;
        bool         sacrifice_land;
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

        int direct = (def.tmpl == CardTemplate::DirectDamage) ? def.params.damage : 0;
        cands.push_back({i, def, EffectiveCost(def, state),
                         EvalCard(def, state), def.params.sacrifice_land, direct});
    }

    int m = static_cast<int>(cands.size());
    Plan best;

    // Enumerate all 2^m subsets.
    for (int mask = 0; mask < (1 << m); ++mask)
    {
        ManaCost combined;
        int sacrifice_count = 0;
        int direct_dmg      = 0;
        int total_eval      = 0;

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

            if (c.sacrifice_land)  { ++sacrifice_count; }
            direct_dmg += c.direct_damage;
            total_eval += c.eval;
        }

        if (!pool.CanPay(combined))       { continue; }
        if (sacrifice_count > total_lands) { continue; }

        bool wins = (pending_atk + direct_dmg) >= state.Opponent().life;

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
