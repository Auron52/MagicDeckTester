#include "TurnSolver.h"
#include "TranspositionTable.h"
#include "Profiler.h"
#include "../core/ManaPool.h"
#include "../core/EffectHandler.h"
#include "../core/SpellEffects.h"
#include "../core/Trace.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <utility>

// When true, SolveWithLookahead prints per-pass per-candidate win turn estimates
// for top-level T1 pre-combat decisions.  Set via TurnSolver::SetTraceSolve().
static bool s_trace_solve = false;

// Diagnostic (env-gated, inert by default): MTG_TRACE_PLAYOUT_SEED/_TURN replay the
// committed plan from one real decision and trace the rollout's believed winning
// line (per-turn opponent life). The companion of the non-convergence detector
// (AIEngine MTG_FLAG_NONCONV): when the detector flags a turn whose earlier-proven
// win the later search can't reproduce, this prints exactly what line the rollout
// thought would win, so the rollout-vs-reality divergence can be pinpointed.
// g_trace_arm makes only the OUTERMOST SimulateToEndImpl print (nested rollout
// sub-searches disarm it).
static const char*     s_tp_seed_env = std::getenv("MTG_TRACE_PLAYOUT_SEED");
static const long long s_tp_seed     = s_tp_seed_env ? std::atoll(s_tp_seed_env) : -1;
static const char*     s_tp_turn_env = std::getenv("MTG_TRACE_PLAYOUT_TURN");
static const int       s_tp_turn     = s_tp_turn_env ? std::atoi(s_tp_turn_env) : -1;
static thread_local bool g_trace_arm = false;

// Fidelity of the full-depth search's BEYOND-HORIZON leaf estimate (FSLineWin's
// `depth<=0` tail). This is the policy that ranks plans whose payoff lies past the
// searched horizon -- e.g. a combo deck (Treasure Hunt) whose lethal turn is several
// turns out. A greedy depth-0 rollout (the original value) develops such lines too
// weakly: it mis-ranked TH's setup turn and predicted a ~turn-19 win where baseline
// (whose own leaf rolls out WITH lookahead, sub_depth up to depth-1) reached turn 5.
// A 1-ply lookahead leaf fixes this -- it matches baseline's rollout fidelity closely
// enough to rank combo setup correctly, and across slivers/burn/TH at d3 and d5 it
// makes the committed-line search BEAT baseline. We cap it at 1 rather than the full
// depth-1: a deeper leaf is run at EVERY node of the B^depth tree (not just the root
// like baseline) and so blows the per-decision budget -- a depth-2 leaf measured ~11x
// slower for only a marginal further gain. Env-overridable as the active tuning lever
// for the leaf estimator (it is the slot a learned eval will eventually replace -- see
// project-search-distillation). See project-full-depth-search (TH leaf-depth finding).
static const char* s_fd_leaf_depth_env = std::getenv("MTG_FD_LEAF_DEPTH");
static const int   s_fd_leaf_depth     = s_fd_leaf_depth_env ? std::atoi(s_fd_leaf_depth_env) : 1;

// Move-ordering for the full-depth branch-and-bound (FSLineWin / FSLineTail). Each B&B loop
// returns at the FIRST verified in-horizon win; trying the plans that statically look lethal
// (then higher-value) first makes that win surface after fewer simulated plans, so WINNING
// nodes search less. Result-preserving by construction: in an iterative-deepening pass every
// in-horizon win sits at the same horizon-edge turn, so reordering changes only WHICH tied
// line commits, not the win turn -- and the rollout and executor share FSLineWin, so they
// stay in lockstep. A stable sort keeps the original order within ties to minimise
// committed-line churn (a different tied line could realise differently only under
// commit-the-line non-convergence; verified against GT). Default ON; MTG_NO_MOVE_ORDER opts
// out for the with/without A/B. Cheap: one O(n log n) sort per interior node.
static const bool s_move_order = std::getenv("MTG_NO_MOVE_ORDER") == nullptr;

static void MoveOrderPlans(std::vector<TurnSolver::Plan>& plans)
{
    if (!s_move_order || plans.size() < 2) { return; }
    std::stable_sort(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn; }
            return a.value > b.value;
        });
}

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
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap()) || def->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, state, *def);
    }
    // Turn-scoped reserve (ritual float + retained over-production) is spendable on later
    // same-phase casts, so it counts toward affordability. Empty for non-floating decks ->
    // byte-identical; off (MTG_NO_FLOAT_LEFTOVER) -> not added (legacy board-only pool).
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
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
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.creature_mana_only) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap()) || def->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, state, *def);
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }  // see BuildPool
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

// Color-producibility gate for a chosen action subset. ManaPool::CanPay stores every
// multi-color land as one "wild" mana that satisfies ANY single colored pip — an
// over-approximation for a land that produces only a SUBSET of colors (e.g. Tournament
// Grounds = W/R/B cannot pay {U}). That let the enumerator OFFER hard-casts the executor
// then cannot pay (a silent no-op; found by the Knights claude-play sweep). This gate
// rejects a subset only when it requires a colored pip of a color that NO untapped source
// can produce at all. That is the maximally-conservative necessary condition: it can never
// reject a plan the real payment would allow whenever the controller has at least one
// source of each needed color (true for burn/slivers/TH on their own seeds), so those
// decks stay byte-identical; it prunes exactly the restricted-color phantom (a {U} cost
// with zero blue sources). It deliberately does NOT model count/contention/filter-yield
// (those mis-estimate amounts and would false-reject payable RR/filter-chain plans), so a
// rarer "needs 2 of a color, only 1 source" phantom is left to the rollout's real payment,
// which already no-ops it. Cheap and deterministic.
// Colors at least one untapped source can produce (W,U,B,R,G). State-only -- it does not depend
// on which subset is being tested -- so callers compute it ONCE before the subset-enumeration
// loop and pass the result to SubsetPayable, instead of re-scanning the battlefield per subset.
static void ComputeAvailableColors(const GameState& state, bool have[5])
{
    have[0] = have[1] = have[2] = have[3] = have[4] = false;
    int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        bool is_src = (def->tmpl == CardTemplate::BasicLand)
                   || (def->tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def->params.mana_rock;
        if (!is_src) { continue; }
        for (Color c : EffectiveProduces(state, active, *def))   // RP -> union of other lands
        {
            switch (c)
            {
                case Color::White: have[0] = true; break;
                case Color::Blue:  have[1] = true; break;
                case Color::Black: have[2] = true; break;
                case Color::Red:   have[3] = true; break;
                case Color::Green: have[4] = true; break;
                default: break;
            }
        }
    }
    // Floating mana (turn-scoped reserve) also satisfies colored pips: a floated {U} pays a {U}
    // pip even when no untapped land produces blue. BuildPool already credits floating into the
    // count pool, so without this the per-color gate would false-reject an otherwise-payable cast
    // (e.g. a second Treasure Hunt {1}{U} off a floating {U} plus a colorless land). A wild
    // floating mana (multi-color ritual float) can pay any single pip. Empty floating ->
    // byte-identical for every non-floating deck/seed.
    if (FloatLeftoverManaEnabled())
    {
        const ManaPool& f = state.floating_mana;
        if (f.white > 0) { have[0] = true; }
        if (f.blue  > 0) { have[1] = true; }
        if (f.black > 0) { have[2] = true; }
        if (f.red   > 0) { have[3] = true; }
        if (f.green > 0) { have[4] = true; }
        if (f.wild  > 0) { have[0] = have[1] = have[2] = have[3] = have[4] = true; }
    }
}

static bool SubsetPayable(const bool have[5], const std::vector<Action>& cands,
                          const std::vector<int>& sel)
{
    // Colors required by the chosen casts (Vial deploys cost no mana).
    bool need[5] = {false,false,false,false,false};  // W,U,B,R,G ({C}/generic via CanPay)
    bool any = false;
    for (int j : sel)
    {
        const Action& a = cands[j];
        if (a.kind == Action::Kind::ActivateVial) { continue; }
        if (a.cost.white > 0) { need[0] = true; any = true; }
        if (a.cost.blue  > 0) { need[1] = true; any = true; }
        if (a.cost.black > 0) { need[2] = true; any = true; }
        if (a.cost.red   > 0) { need[3] = true; any = true; }
        if (a.cost.green > 0) { need[4] = true; any = true; }
    }
    if (!any) { return true; }

    for (int i = 0; i < 5; ++i) { if (need[i] && !have[i]) { return false; } }
    return true;
}

static int PendingAttackDamage(const GameState& state)
{
    int dmg = 0;
    int active = state.active_player_index;
    std::vector<const Permanent*> attackers;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!ResolveProvider(state).ShouldAttackWith(state, p)) { continue; }
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, active, animated, &p);
        bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active));
        int base_pw = p.EffectivePower() + lord_pb;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        dmg += base_pw * (ds ? 2 : 1);
        attackers.push_back(&p);
    }
    dmg += CountAttackTriggerLifeLoss(state.battlefield, active, attackers);

    // Exalted (Ignoble Hierarch): a creature attacking ALONE gets +1/+1 per Exalted ability.
    if (static_cast<int>(attackers.size()) == 1)
    { dmg += CountExalted(state.battlefield, active); }

    // Estimate attack-trigger tokens (Adeline) for this turn only: const path cannot create
    // them, so add their immediate damage (token base power + anthem bonus) if attacking.
    if (!attackers.empty())
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d || d->params.attack_creates_tokens <= 0) { continue; }
            Card tok;
            tok.AddType(CardType::Creature);
            tok.m_subtypes = d->params.attack_token_subtypes;
            tok.m_power    = d->params.attack_token_power;
            auto [tpb, ttb] = ComputeLordBonus(tok, state.battlefield, active, false, nullptr);
            dmg += d->params.attack_creates_tokens
                 * (d->params.attack_token_power + tpb);
        }
    }
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
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
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
            && ResolveProvider(state).ShouldAttackWith(state, p)
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
    // Hinata's per-target cost reduction (fixed-cost spells; {X} spells apply it at the X-cost
    // sites where the whole generic, incl. X, is known -- see the X-enumeration / apply_one).
    if (!def.card.m_mana_cost.has_x)
    {
        cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
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
        // Adeline (power = creatures you control): estimate as the current creature count
        // plus 1 for herself entering. Her printed power is 0, so without this she scores 0.
        int dyn = def.params.power_equals_creature_count
                  ? CreatureCount(state, state.active_player_index) + 1 : 0;
        int power = (def.card.m_power.value_or(0) + dyn + lord_pb) * (ds ? 2 : 1);
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

    // Archetype-specific card value (Treasure Hunt / Land's Edge combo): provider-owned
    // (Hook 15), so the clairvoyant + combo assumptions live in the per-deck file. A deck
    // without such a model returns false here and falls through to the generic estimates.
    {
        int archetype_value = 0;
        if (ResolveProvider(state).ArchetypeCardValue(state, def, DMG, archetype_value))
        {
            return archetype_value;
        }
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
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
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
        auto opt = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!opt || opt->card.IsLand()) { continue; }
        const CardDefinition& def = *opt;
        // Goldfish-inert cards (counterspells / "tap target creature" / "bounce target
        // permanent" against a passive opponent that never casts, attacks or blocks): they
        // have no useful target, so the deck genuinely cannot productively cast them. Never
        // offered as an action -> they sit in hand as faithful dead draws. Gated off for
        // every existing deck.
        if (def.params.goldfish_inert) { continue; }
        // Sorceries/non-flash spells require an empty stack and a main phase.
        bool timing_ok = def.card.IsInstant()
                      || def.card.HasKeyword(Keyword::Flash)
                      || state.stack.empty();
        if (!timing_ok) { continue; }

        // Flood-engine gate: a Treasure Hunt (DrawUntilNonland) or a cascade/retrace card
        // that can cascade INTO Treasure Hunt (Throes of Chaos) is only offered when its
        // draw has a payoff this turn (Land's Edge online / castable, or a no-max-hand-size
        // land to keep it) -- otherwise the drawn lands are wasted to cleanup. Asked at this
        // single enumeration choke point so the search AND the bottoming rollouts both honor
        // it. Generic returns true (no gate), so non-Treasure-Hunt decks are byte-identical.
        if ((def.tmpl == CardTemplate::DrawUntilNonland
             || def.params.cascade_max_mv > 0 || def.params.retrace)
            && !ResolveProvider(state).ShouldCastDrawEngine(state, state.active_player_index, def))
        {
            continue;
        }

        // Skip spells that need a creature target when none exists. An own-creature pump
        // (Invigorate) needs one of OUR attackers; other creature-targeting spells need an
        // opponent creature.
        Targeting t = def.params.targeting;
        if (t == Targeting::Creature && def.params.target_own_creature)
        {
            if (FindBestOwnAttacker(state, state.active_player_index) < 0) { continue; }
        }
        else if ((t == Targeting::Creature || t == Targeting::Multi) && !has_creature_target)
        {
            continue;
        }

        // {X} spells: enumerate candidate X values (provider XCandidates narrows the range,
        // the search picks among the variants -- they share hand_index, so they are mutually
        // exclusive in the plan). Only X-damage (DirectDamage) is modeled today; other X
        // templates (DrawX) stay skipped until their effect is scaled in BOTH cast paths.
        if (def.card.m_mana_cost.has_x)
        {
            // Reality Spasm (untap RITUAL): emit ONE action that floats mana for a same-turn
            // payoff (Crackle). Only productive with Hinata in play (her discount makes the {X}
            // free), so chosen_x = #mana sources -> untap/refloat them ALL. Cost is the fixed
            // {U}{U} (the X discounts away via the same formula as Crackle). It deals no damage;
            // its value is the floating it adds (credited in Solve::consider).
            if (def.params.untap_x_mana_sources)
            {
                if (!ResolveProvider(state).ShouldEmitUntapRitual(state)) { continue; }
                int x = ManaSourceCount(state);
                if (x <= 0) { continue; }
                int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
                ManaCost xcost = EffectiveCost(def, state);
                xcost.generic += x * pips;
                xcost.generic = std::max(0, xcost.generic - HinataGenericDiscount(def, state, x));
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = xcost;
                a.chosen_x       = x;
                a.eval           = 0;                  // ritual deals nothing; value is enabling Crackle
                a.direct_damage  = 0;
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                a.ritual_float   = RitualFloatAmount(state, def, x);   // refloat mana, stamped once
                actions.push_back(std::move(a));
                continue;
            }
            if (def.tmpl != CardTemplate::DirectDamage) { continue; }
            ManaCost base  = EffectiveCost(def, state);   // fixed part; ManaValue() ignores X
            ManaPool xpool = BuildPool(state);
            // Hinata combo: a ritual in hand (Reality Spasm) funds a bigger X this turn. Credit
            // its NET mana so this payoff's max X reaches the combo's lethal value. Over-generates
            // candidates affordable ONLY with the ritual; Solve::consider rejects any subset that
            // does not actually include the ritual (only the ritual+payoff subset passes CanPay).
            // 0 with no Hinata / no ritual in hand -> byte-identical for every other deck.
            xpool.wild += HinataRitualNetBonus(state);
            // Each point of X is paid x_pips times (Crackle {X}{X}{X} = 3), so the max
            // affordable X divides the leftover mana by x_pips.
            int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
            int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }
            // Hinata's discount frees up mana for a larger X. Two cases:
            int max_x;
            int P = xpool.Total(), bmv = base.ManaValue();
            if (def.params.discount_targets_scale_x && HinataInPlay(state))
            {
                // Crackle: discount = min(X, avail) -> cost(X) = pips*X + bmv - min(X, avail),
                // monotonic increasing. Solve for the largest affordable X (piecewise at X=avail).
                int avail = HinataAvailableTargets(def, state);
                int cost_at_avail = (pips - 1) * avail + bmv;
                if (P >= cost_at_avail) { max_x = (P - bmv + avail) / pips; }
                else                    { max_x = (P - bmv) / (pips > 1 ? pips - 1 : 1); }
            }
            else
            {
                // Fixed (non-scaling) discount: a constant addend to the affordable budget.
                int disc = HinataGenericDiscount(def, state, 0);
                max_x = (P - bmv + disc) / pips;
            }
            if (max_x < 0) { max_x = 0; }
            for (int x : ResolveProvider(state).XCandidates(state, def, max_x))
            {
                if (x <= 0) { continue; }
                ManaCost xcost = base;
                xcost.generic += x * pips;                // X is paid (x_pips times) as generic mana
                // Hinata reduces the whole generic (incl. X) by the spell's target count;
                // for an X-target spell (Reality Spasm) that scales with the chosen X.
                xcost.generic = std::max(0, xcost.generic - HinataGenericDiscount(def, state, x));
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = xcost;
                a.chosen_x       = x;
                a.sacrifice_land = def.params.sacrifice_land;
                a.eval           = x * mult * 100;         // EvalCard's DMG unit (dmg-equivalents)
                // X burn reaches the face only when not creature-only targeted (the creature-
                // target guard above already dropped Creature/Multi with no opponent creature).
                // Damage per target = chosen X * x_damage_multiplier (Crackle = 5X).
                a.direct_damage  = (def.params.targeting != Targeting::Creature) ? x * mult : 0;
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();  // X = 0 outside the stack
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Alternative cost (Invigorate / Skyshroud Cutter / Reverent Silence): "If you control
        // a Forest, rather than pay this spell's mana cost, you may have an opponent gain N
        // life" (-> N damage with a Remedy active). These free payloads split into two kinds:
        //   * SAFE (Invigorate / Skyshroud): firing one is strictly good under a Remedy (free
        //     face damage, no downside), so it is NOT a search choice -- it is AUTO-FIRED
        //     deterministically after the casts (FireSafeAltPayloads, see ApplyPlanDirect /
        //     AIEngine). Keeping them out of the action set avoids a free-subset enumeration
        //     blow-up (free actions are never mana-pruned, so flooded hands exploded the plan
        //     count). They are therefore not emitted here at all.
        //   * RISKY (Reverent Silence: destroy_all_enchantments wipes our OWN Aria/Remedy):
        //     this stays a genuine SEARCH decision, offered as a free action only with a Remedy
        //     already active, so the search weighs the board wipe against keeping the combo.
        if (def.params.alt_lifegain_cost > 0
            && ControlsSubtype(state, state.active_player_index, def.params.alt_cost_requires_subtype))
        {
            // Default (pruned): only the RISKY alt is a search choice; the SAFE ones are auto-fired
            // below (byte-identical). UNPRUNED opens the safe alt to the search too -- firing a free
            // payload is NOT always correct (e.g. Invigorate with no attacker just feeds the
            // opponent life), so the search-primary A/B (and human play) must be able to weigh
            // not-firing it. When opened here, the auto-fire pass is suppressed (gated on the same
            // DecisionUnpruned), so the choice is made exactly once, by the search/human.
            if (ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, def)
                || DecisionUnpruned())
            {
                constexpr int DMG = 100;
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = ManaCost{};             // free (alt cost is the opponent lifegain)
                a.alt_cost       = true;
                a.alt_lifegain   = def.params.alt_lifegain_cost;
                a.eval           = def.params.alt_lifegain_cost * DMG;
                a.direct_damage  = def.params.alt_lifegain_cost;
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                actions.push_back(std::move(a));
            }
            continue;   // safe alts: auto-fired (not enumerated); risky alt handled above
        }

        // Tutor: the heuristic (TutorCandidates) returns a NARROWED candidate set. One
        // candidate = a clear heuristic decision; several = the heuristic pruned the options
        // but cannot distinguish them, so emit one cast variant per candidate (all sharing this
        // hand_index, hence mutually exclusive in the plan enumerator) and let the search pick.
        // Narrowing to the few cards that matter keeps the search's branching factor small --
        // the general "heuristic narrows, search decides the rest" pattern.
        if (def.params.tutor_to_hand || def.params.tutor_to_top)
        {
            std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, state.active_player_index, def.params);
            if (cands.empty()) { cands.push_back(std::string{}); }  // whiff: castable, fetches nothing
            for (const std::string& tgt : cands)
            {
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = EffectiveCost(def, state);
                a.eval           = EvalCard(def, state);
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                a.tutor_target   = tgt;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Soulfire Eruption: the own-creature target COUNT is a searched parameter. Emit one cast
        // variant per own_targets value 0..K (K = #own creatures). More targets = a deeper dig
        // (face = max MV over more exiled cards) + a bigger Hinata discount, paid for with mana-
        // value damage to those creatures (see SoulfireDig). All share hand_index (mutually
        // exclusive), so the search picks the count by playing each out. own_targets = 0 = 6a.
        if (def.tmpl == CardTemplate::DirectDamage && def.params.damage_equals_top_mv)
        {
            const int active   = state.active_player_index;
            // Own-targeting only earns its keep with Hinata in play: each own target is {1} less
            // (the discount that can ENABLE an otherwise-unaffordable Soulfire) plus a deeper dig.
            // Without her, an own target gives no discount -- only marginal extra dig at the cost of
            // creature damage on a 9-mana spell -- so we don't branch on it (keeps the K+1 variants
            // off every non-combo turn). With her, the search still picks the count 0..K.
            const int K        = ResolveProvider(state).BranchSoulfireOwnTargets(state)
                                     ? SoulfireOwnCreatureCount(state, active) : 0;
            ManaCost base_cost = EffectiveCost(def, state);   // base Hinata discount already applied
            const int eval     = EvalCard(def, state);
            const bool to_face = def.params.targeting != Targeting::Creature;
            for (int ot = 0; ot <= K; ++ot)
            {
                ManaCost c = base_cost;
                c.generic = std::max(0, c.generic - SoulfireOwnTargetDiscount(def, state, active, ot));
                Action a;
                a.kind                 = Action::Kind::CastFromHand;
                a.card_name            = ap.hand[i].m_name;
                a.hand_index           = i;
                a.cost                 = c;
                a.eval                 = eval;
                a.direct_damage        = to_face ? SoulfireFaceDamage(state, active, ot) : 0;
                a.is_noncreature       = !def.card.IsCreature();
                a.card_mv              = def.card.m_mana_cost.ManaValue();
                a.soulfire_own_targets = ot;
                actions.push_back(std::move(a));
            }
            continue;
        }

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
        a.max_casts_after       = def.params.max_casts_after;   // Irencrag "one more spell" restriction
        if (IsManaRitual(def)) { a.ritual_float = RitualFloatAmount(state, def, a.chosen_x); }  // Irencrag burst
        // Same-turn mana-rock ramp: a non-creature mana rock (Sol Ring) taps the turn it is cast.
        // Stamp the mana it produces (by real colour) so the enumerator can fund the rest of the
        // subset off it. Creatures (mana dorks) are excluded -- they are summoning-sick this turn.
        if (RockRampEnumEnabled() && def.params.mana_rock && !def.card.IsCreature())
        { AddSourceToPool(a.rock_mana, state, def); }
        // Ponder (cast_reorder): keep-vs-shuffle. This was the #1 branching source (MTG_BRANCH_STATS:
        // ~47% of all enumeration) because searching BOTH futures emits two variants that multiply
        // every plan where Ponder is castable. By default we now DECIDE it with the heuristic
        // (ponder_keep = -1 -> ReorderTopOrShuffle shuffles iff none of the top N pass ScryKeepOnTop,
        // which for Hinata is the situational-rank threshold) -- one variant, no branch. MTG_UNPRUNED
        // restores the searched 2-way branch for the standing A/B. The provider still supplies the
        // kept-card ORDER (by situational rank) either way.
        static const bool s_ponder_search = std::getenv("MTG_PONDER_SEARCH") != nullptr;
        if (def.params.cast_reorder > 0 && (s_ponder_search || DecisionUnpruned()))
        {
            Action keep_a = a;            keep_a.ponder_keep    = 1;
            a.ponder_keep = 0;            // `a` becomes the shuffle variant
            actions.push_back(std::move(keep_a));
            actions.push_back(std::move(a));
        }
        else
        {
            // Heuristic decides keep-vs-shuffle at RESOLUTION (ponder_keep = -1, the Action default):
            // ReorderTopOrShuffle shuffles iff none of the top N cards passes ScryKeepOnTop (the
            // situational-rank threshold for Hinata), else keeps them on top ordered by rank. One
            // variant, no branch. Deciding at resolution (vs at enumeration) measured strictly better
            // -- the top of library reflects this turn's earlier cantrips by then -- and at the SAME
            // fd-diverge as the searched 2-variant baseline (no lockstep regression).
            actions.push_back(std::move(a));   // a.ponder_keep stays -1
        }
    }

    // --- Aether Vial activations: one per (Vial, creature) pair ---
    {
        constexpr int DMG = 100;
        int bf_size = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_size; ++vi)
        {
            const Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            const CardDefinition* vdef =
                CardDatabase::Instance().LookupCached(vp.card);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }

            int target_mv = vp.charge_counters;
            for (int i = 0; i < n; ++i)
            {
                const CardDefinition* copt =
                    CardDatabase::Instance().LookupCached(ap.hand[i]);
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
            auto cdef = CardDatabase::Instance().LookupCached(c);
            if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
        }
        if (lands_in_hand > 0)
        {
            std::unordered_set<std::string> seen_gy;
            for (const Card& gc : ap.graveyard)
            {
                auto gdef = CardDatabase::Instance().LookupCached(gc);
                if (!gdef || !gdef->params.retrace) { continue; }
                bool timing_ok = gdef->card.IsInstant()
                              || gdef->card.HasKeyword(Keyword::Flash)
                              || state.stack.empty();
                if (!timing_ok) { continue; }
                if (gdef->card.m_mana_cost.has_x) { continue; }
                if (!seen_gy.insert(gc.m_name).second) { continue; }
                // Flood-engine gate (same as hand casts): a retrace card here is Throes of
                // Chaos, which cascades into Treasure Hunt -- only recast it from the
                // graveyard when the resulting draw has a payoff this turn. In human play the
                // player owns that call, so the gate is bypassed and the retrace line is always
                // offered (MTG_HUMAN_PLAY); autonomous search keeps the gate (byte-identical).
                static const bool s_human_play_retrace = std::getenv("MTG_HUMAN_PLAY") != nullptr;
                if (!s_human_play_retrace &&
                    !ResolveProvider(state).ShouldCastDrawEngine(state, state.active_player_index, *gdef))
                {
                    continue;
                }

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

    // Resolve each action's card definition ONCE so the per-node subset evaluators read the
    // cached pointer instead of re-hashing card_name. Equivalent to Lookup(card_name) at each
    // use site (every kind's card_name is a real DB name: hand-cast/vial creature/dig source).
    for (Action& a : actions) { a.def = CardDatabase::Instance().Lookup(a.card_name); }

    return actions;
}

// ---- Breadth cap shared by Solve and EnumeratePlans -------------------------------------------
// The plan enumerators (Solve's odometer and EnumeratePlans) cost product_g(1+|group_g|) * 2^ind.
// A deep Soulfire/cantrip dig leaves ~20 distinct nonland casts in hand, exploding it -- and these
// run PER rollout node, so a few such turns dominate the whole search (MTG_PROFILE: 2 of 24 games
// held ~90% of wall time at low node counts -> the cost was enumeration, not the rollout). Keep only
// the top-K groups by the provider's SituationalCardRank; the lowest-ranked groups (dig duplicates /
// dead cards the rank already deprioritizes) drop out of THIS turn's enumeration. Lossy, so gated:
// inert for any hand with <= cap groups (the whole suite is byte-identical), and disabled by
// MTG_NO_GROUP_CAP / MTG_UNPRUNED for the standing A/B (ON vs OFF give byte-identical search-node
// counts on Hinata -- the pruned groups never produced the optimal plan). The cap VALUE is now
// provider-owned policy (DecisionProvider::EnumGroupCap, audit A1); MTG_SOLVE_GROUP_CAP still tunes K.
bool GroupCapDisabled()
{
    static const bool v = std::getenv("MTG_NO_GROUP_CAP") != nullptr;
    return v;
}

static void CapGroupsBySituationalRank(const GameState& state, const std::vector<Action>& cands,
                                       std::vector<std::vector<int>>& groups,
                                       std::vector<int>& group_hand_index)
{
    if (GroupCapDisabled() || DecisionUnpruned()) { return; }
    // The provider supplies the breadth policy; the env knob is an engine-side A/B override.
    int cap = ResolveProvider(state).EnumGroupCap();
    if (const char* e = std::getenv("MTG_SOLVE_GROUP_CAP")) { int x = std::atoi(e); cap = x < 1 ? 1 : x; }
    if (static_cast<int>(groups.size()) <= cap)    { return; }

    const DecisionProvider& prov = ResolveProvider(state);
    std::vector<std::pair<int, int>> ranked;   // (situational rank, original group index)
    ranked.reserve(groups.size());
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
    {
        int best_r = -1;
        for (int idx : groups[g])
        {
            const CardDefinition* d = cands[idx].def;
            if (!d) { continue; }
            int r = prov.SituationalCardRank(state, d->card);
            if (r > best_r) { best_r = r; }
        }
        ranked.push_back({ best_r, g });
    }
    std::stable_sort(ranked.begin(), ranked.end(),
        [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first > b.first; });
    std::vector<char> keep(groups.size(), 0);
    for (int i = 0; i < cap; ++i) { keep[ranked[i].second] = 1; }
    std::vector<std::vector<int>> kept_groups;
    std::vector<int>              kept_hand_index;
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
    {
        if (!keep[g]) { continue; }
        kept_groups.push_back(std::move(groups[g]));
        kept_hand_index.push_back(group_hand_index[g]);
    }
    groups.swap(kept_groups);
    group_hand_index.swap(kept_hand_index);
}

// ---- TurnSolver::Solve ---------------------------------------------------

TurnSolver::Plan TurnSolver::Solve(const GameState& state, bool is_pre_combat)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    const Player& ap = state.ActivePlayer();
    ManaPool pool             = BuildPool(state);
    ManaPool pool_noncreature = BuildNonCreaturePool(state);
    int total_lands  = CountLands(state);
    int pending_atk  = PendingAttackDamage(state);
    int prowess_attackers   = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    std::vector<Action> cands = CollectActions(state, is_pre_combat);
    int n = static_cast<int>(ap.hand.size());

    // Hinata combo: does any candidate float ritual mana (Reality Spasm / Irencrag)? Each Action's
    // gross float was stamped at enumeration (a.ritual_float), so this is a cheap scan -- no
    // per-node card lookup. `consider` sums cands[j].ritual_float over the chosen subset and credits
    // the pool's CanPay. False for every non-ritual deck -> byte-identical.
    bool any_ritual = false;
    for (const Action& ra : cands) { if (ra.ritual_float > 0) { any_ritual = true; break; } }
    // Same-turn mana-rock ramp: any non-creature rock (Sol Ring) stamped with its production?
    // Cheap scan -> the credit below is inert for every deck without such a rock.
    bool any_rock = false;
    for (const Action& ra : cands) { if (ra.rock_mana.Total() > 0) { any_rock = true; break; } }

    // Lands in hand -- a generic feasibility input (a plan cannot discard more lands than it
    // holds for retrace / Land's Edge additional costs; see the discard_lands_used check below).
    int lands_in_hand = 0;
    for (int i = 0; i < n; ++i)
    {
        auto def = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (def && def->card.IsLand()) { ++lands_in_hand; }
    }

    // Deck-specific reach toward THIS turn's lethal beyond combat + direct damage (the Treasure
    // Hunt / Land's Edge ammunition model) is provider-owned (Hook 14). HasExtraLethalModel()
    // gates the whole thing: a deck without such a model skips building the per-plan cast list
    // entirely, staying byte-identical to the old "all addends 0" path.
    const DecisionProvider& provider = ResolveProvider(state);
    const bool has_extra_lethal = provider.HasExtraLethalModel();
    std::vector<const CardDefinition*> casting;   // reused per subset (only when has_extra_lethal)

    int m = static_cast<int>(cands.size());
    Plan best;
    int  best_mask = 0;     // action mask of `best` (0 = the do-nothing default); ties keep min mask

    bool have_colors[5];    // untapped-source colors -- state-only, computed once for all subsets
    ComputeAvailableColors(state, have_colors);

    // Evaluate one selected combination of candidate indices and, if it is the new optimum,
    // record it. The optimum is ordered by (wins, value, SMALLEST action mask): a winning plan
    // beats a non-winner; higher eval beats lower; among equals the numerically smallest mask
    // wins. That last tie-break is exactly the plan the ascending-mask powerset below settles on
    // (first-found under a strict '>' test) -- making it explicit lets the odometer enumeration
    // return the byte-identical plan despite visiting subsets in a different order.
    auto consider = [&](std::vector<int> sel)
    {
        std::sort(sel.begin(), sel.end());          // ascending -> matches the powerset's bit order
        int mask = 0;
        for (int j : sel) { mask |= (1 << j); }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count    = 0;
        int noncreature_count  = 0;
        int direct_dmg         = 0;
        int total_eval         = 0;
        int self_damage        = 0;
        int vial_haste_atk     = 0;
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)

        for (int j : sel)
        {
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
                ++noncreature_count;
            }

            if (c.sacrifice_land)    { ++sacrifice_count; }
            direct_dmg        += c.direct_damage;
            total_eval        += c.eval;
            vial_haste_atk    += c.vial_attack_power;

            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        // Hinata combo: a ritual cast in THIS subset floats mana for the rest of the subset.
        // Credit its gross float to the affordable pool (the ritual's own cost is already in
        // `combined`, so pool+gross-cost == pool+net -> exact, conservative). Zero unless a
        // ritual is selected -> byte-identical for every non-ritual deck.
        // Same-turn ramp credit. Ritual float (Reality Spasm / Irencrag) credited as wild; a mana
        // rock cast in THIS subset (Sol Ring -> {C}{C}) credited by its REAL produced colours, but
        // only once the board can already pay for the rocks themselves (a rock never funds its own
        // cost). The casts' own costs are already in `combined`, so this stays net/conservative.
        // Both inert -> byte-identical for decks without rituals or rocks.
        ManaPool eff = pool, eff_nc = pool_noncreature;
        bool credited = false;
        if (any_ritual)
        {
            int ritual_credit = 0;
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            if (ritual_credit > 0) { eff.wild += ritual_credit; eff_nc.wild += ritual_credit; credited = true; }
        }
        if (any_rock)
        {
            ManaPool rock_prod; ManaCost rock_costs; bool sel_rock = false;
            for (int j : sel)
            {
                if (cands[j].rock_mana.Total() <= 0) { continue; }
                rock_prod.AddPool(cands[j].rock_mana);
                const ManaCost& rc = cands[j].cost;
                rock_costs.white += rc.white; rock_costs.blue += rc.blue; rock_costs.black += rc.black;
                rock_costs.red   += rc.red;   rock_costs.green += rc.green;
                rock_costs.colorless += rc.colorless; rock_costs.generic += rc.generic;
                sel_rock = true;
            }
            if (sel_rock && pool.CanPay(rock_costs)) { eff.AddPool(rock_prod); eff_nc.AddPool(rock_prod); credited = true; }
        }
        if (credited)
        {
            if (!eff.CanPay(combined))                       { return; }
            if (!eff_nc.CanPay(noncreature_combined))        { return; }
        }
        else
        {
            if (!pool.CanPay(combined))                          { return; }
            if (!pool_noncreature.CanPay(noncreature_combined))  { return; }
        }
        if (sacrifice_count > total_lands)                   { return; }
        if (discard_lands_used > lands_in_hand)              { return; }
        // Accurate per-color payability (rejects wild-pool phantoms, e.g. a {U} hard-cast off a
        // W/R/B-only land). Strict tightening; inert for decks whose lands produce their colors.
        if (!SubsetPayable(have_colors, cands, sel))         { return; }

        // Irencrag Feat "you can cast only one more spell this turn": reject any subset that casts
        // more than max_casts_after spells AFTER the restricting ritual (ordered by CastOrderRank).
        // The provider ranks Irencrag as the last ritual (18, just before the Crackle payoff at 20),
        // so the realised cast order ...Reality Spasm(15) -> Irencrag(18) -> Crackle(20) is legal and
        // the executor/rollout (which cast in CastOrderRank order) match this judgement -> lockstep.
        // Loop runs lookups only when a restrictor is actually selected (rare); flag-check otherwise.
        for (int j : sel)
        {
            if (cands[j].max_casts_after < 0) { continue; }
            const CardDefinition* rd = cands[j].def;
            const int r_rank = rd ? ResolveProvider(state).CastOrderRank(state, *rd) : 20;
            int after = 0;
            for (int k : sel)
            {
                if (k == j) { continue; }
                const CardDefinition* kd = cands[k].def;
                const int k_rank = kd ? ResolveProvider(state).CastOrderRank(state, *kd) : 20;
                if (k_rank > r_rank) { ++after; }
            }
            if (after > cands[j].max_casts_after) { return; }   // illegal: too many spells after it
        }

        // Eidolon-style on-cast triggers go on top of the spell being cast (CR 603), so they
        // resolve BEFORE the spell. A plan that kills us via self-damage cannot win.
        if (self_damage >= ap.life)                          { return; }

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
        // Deck-specific extra reach toward lethal (Land's Edge ammo + clairvoyant Treasure Hunt),
        // provider-owned. Built only when the deck has such a model (byte-identical otherwise).
        int extra_lethal = 0;
        if (has_extra_lethal)
        {
            casting.clear();
            for (int j : sel) { casting.push_back(cands[j].def); }
            extra_lethal = provider.ExtraLethalDamage(state, casting);
        }
        bool wins = (projected_atk + direct_dmg + extra_lethal) >= state.Opponent().life;

        bool better;
        if (best.wins_this_turn != wins)   { better = wins; }                  // winning dominates
        else if (total_eval != best.value) { better = total_eval > best.value; } // then higher eval
        else                               { better = mask < best_mask; }        // tie -> smallest mask
        if (!better) { return; }

        best.actions.clear();
        for (int j : sel) { best.actions.push_back(cands[j]); }
        best.value          = total_eval;
        best.wins_this_turn = wins;
        best_mask           = mask;
    };

    std::vector<int> sel;   // reused across subset iterations (clear keeps capacity, avoids per-call alloc)

    // ---- Combo-line short-circuit (the breadth cut; see hinata-combo-heuristic-spec) ----------
    // On a ritual-funded combo turn (Hinata: Reality Spasm / Irencrag float mana for a big Crackle)
    // the hand is bloated with the cards a deep Soulfire/cantrip dig staged, so the powerset/odometer
    // below explodes. But the lethal line is structurally fixed: cast the rituals, then the X-damage
    // finisher at the largest affordable X. So before enumerating, evaluate JUST that line -- the
    // finisher (max-X variant) plus every available ritual (more rituals == more mana == strictly
    // more affordable, and the max_casts_after order RS->Irencrag->finisher is legal) -- via the same
    // consider() the powerset uses. consider() enforces ALL the feasibility it always does: total +
    // per-color mana (the {U}{U}-for-Reality-Spasm and {R}{R}-for-Crackle pruning is exactly its
    // CanPay + SubsetPayable), the Irencrag one-more-spell restriction, self-damage, and the EXACT
    // win projection (the finisher's 5X is direct_damage). We skip the powerset ONLY when that line
    // WINS -- a turn-winning plan dominates every other plan this turn, so we lose nothing; when it
    // does not win we fall through to the full enumeration (best is merely pre-seeded, like move
    // ordering), so a line the full search would find is never missed. Gated on any_ritual, so every
    // non-ritual deck is byte-identical; MTG_UNPRUNED also disables it, leaving the full search as the
    // standing A/B that proves the cut wins the same games. MTG_NO_COMBO_LINE is a dedicated isolation
    // toggle (disables ONLY this cut, keeping every other heuristic) for a clean perf A/B.
    static const bool s_no_combo_line = std::getenv("MTG_NO_COMBO_LINE") != nullptr;
    if (any_ritual && !s_no_combo_line && !DecisionUnpruned())
    {
        int finisher = -1, finisher_dmg = -1;
        std::vector<int> rituals;
        for (int j = 0; j < m; ++j)
        {
            const Action& c = cands[j];
            if (c.kind != Action::Kind::CastFromHand) { continue; }
            const CardDefinition* d = c.def;
            if (!d) { continue; }
            // The lethal payoff: an {X} direct-damage finisher (Crackle with Power, 5X) cast at the
            // largest X CollectActions emitted (it already credited the rituals' net mana into X).
            if (d->params.x_damage_multiplier > 1 && c.chosen_x > 0 && c.direct_damage > 0)
            {
                if (c.direct_damage > finisher_dmg) { finisher_dmg = c.direct_damage; finisher = j; }
            }
            else if (c.ritual_float > 0)   // a mana ritual (Reality Spasm / Irencrag Feat)
            {
                rituals.push_back(j);
            }
        }
        if (finisher >= 0)
        {
            std::vector<int> combo = rituals;   // all rituals -> max mana to fund the finisher's X
            combo.push_back(finisher);
            consider(combo);
            if (best.wins_this_turn) { return best; }   // lethal combo found -> skip the powerset
            // Not lethal/affordable: fall through; `best` is pre-seeded (harmless move-ordering).
        }
    }

    // The default enumeration replaces the 2^m action powerset with the PRODUCT of per-hand-card
    // choices {skip, cast, deploy-via-Vial} (same-charge Vial deploys collapse to one
    // representative, bounded by an aggregate per-charge capacity) crossed with the 2^independent
    // powerset of non-hand actions (graveyard retrace). This visits exactly the powerset's feasible
    // subsets -- the same invariant EnumeratePlans relies on -- in O(prod(1+choices)*2^independent)
    // instead of O(2^m), which is the wide-board (slivers/knights) hot path. MTG_LEGACY_SOLVE keeps
    // the reference powerset for A/B (the two must produce byte-identical game results).
    static const bool s_legacy_solve = std::getenv("MTG_LEGACY_SOLVE") != nullptr;
    if (s_legacy_solve)
    {
        // Reference path: full 2^m powerset with precomputed mutual-exclusion conflict masks. Two
        // actions conflict if they use the same hand card (cast vs. its Vial deploy) or tap the
        // same Vial. conflict[j] is the bitmask of actions that cannot co-occur with j.
        std::vector<int> conflict(m, 0);
        for (int j = 0; j < m; ++j)
        {
            for (int k = j + 1; k < m; ++k)
            {
                bool conf =
                    (cands[j].hand_index >= 0 && cands[j].hand_index == cands[k].hand_index)
                    || (cands[j].kind == Action::Kind::ActivateVial
                        && cands[k].kind == Action::Kind::ActivateVial
                        && cands[j].vial_bf_index == cands[k].vial_bf_index);
                if (conf) { conflict[j] |= (1 << k); conflict[k] |= (1 << j); }
            }
        }
        for (int mask = 1; mask < (1 << m); ++mask)
        {
            bool valid = true;
            for (int j = 0; j < m; ++j)
            {
                if (!(mask & (1 << j))) { continue; }
                if (mask & conflict[j]) { valid = false; break; }
            }
            if (!valid) { continue; }
            sel.clear();
            for (int j = 0; j < m; ++j) { if (mask & (1 << j)) { sel.push_back(j); } }
            consider(sel);
        }
        return best;
    }

    // --- Default: odometer over per-hand-card choices x powerset of independent actions ---

    // Per-charge Vial capacity = number of distinct untapped Vials at each charge (derived from
    // the Vial actions' vial_bf_index, matching apply_vial which taps a fresh matching Vial per
    // deploy). Mirrors EnumeratePlans.
    std::vector<std::pair<int, int>> vial_capacity;   // (charge, count)
    auto capacity_for = [&](int charge) -> int
    {
        for (const std::pair<int, int>& vc : vial_capacity)
        {
            if (vc.first == charge) { return vc.second; }
        }
        return 0;
    };
    {
        std::vector<std::pair<int, int>> seen;   // (charge, vial_bf_index) already counted
        for (const Action& a : cands)
        {
            if (a.kind != Action::Kind::ActivateVial) { continue; }
            std::pair<int, int> key{ a.card_mv, a.vial_bf_index };
            bool already = false;
            for (const std::pair<int, int>& s : seen) { if (s == key) { already = true; break; } }
            if (already) { continue; }
            seen.push_back(key);
            bool found = false;
            for (std::pair<int, int>& vc : vial_capacity)
            {
                if (vc.first == a.card_mv) { ++vc.second; found = true; break; }
            }
            if (!found) { vial_capacity.push_back({ a.card_mv, 1 }); }
        }
    }

    // Group action indices: one mutually-exclusive option list per hand card (its cast + a single
    // representative Vial deploy), plus a flat list of independent non-hand actions (retrace).
    std::vector<std::vector<int>> groups;            // per hand card: option cand indices
    std::vector<int>              group_hand_index;  // parallel: the card's hand_index
    std::vector<int>              independent;
    for (int j = 0; j < m; ++j)
    {
        if (cands[j].hand_index < 0) { independent.push_back(j); continue; }

        int gi = -1;
        for (int g = 0; g < static_cast<int>(groups.size()); ++g)
        {
            if (group_hand_index[g] == cands[j].hand_index) { gi = g; break; }
        }
        if (gi < 0)
        {
            groups.push_back({});
            group_hand_index.push_back(cands[j].hand_index);
            gi = static_cast<int>(groups.size()) - 1;
        }
        // Collapse all same-charge Vial deploys of one card to a single representative.
        if (cands[j].kind == Action::Kind::ActivateVial)
        {
            bool has_vial = false;
            for (int existing : groups[gi])
            {
                if (cands[existing].kind == Action::Kind::ActivateVial) { has_vial = true; break; }
            }
            if (has_vial) { continue; }
        }
        groups[gi].push_back(j);
    }

    // Breadth cap on a bloated combo-dig hand (the lethal combo line is already returned by the
    // short-circuit above, so a non-lethal turn -- the only kind that reaches here -- never drops a
    // win). Shared with EnumeratePlans; see CapGroupsBySituationalRank.
    CapGroupsBySituationalRank(state, cands, groups, group_hand_index);

    int num_groups = static_cast<int>(groups.size());
    int num_ind    = static_cast<int>(independent.size());

    // Reject combinations whose Vial deploys exceed the per-charge capacity.
    auto vial_ok = [&](const std::vector<int>& s) -> bool
    {
        for (int j : s)
        {
            if (cands[j].kind != Action::Kind::ActivateVial) { continue; }
            int charge = cands[j].card_mv;
            int used   = 0;
            for (int k : s)
            {
                if (cands[k].kind == Action::Kind::ActivateVial && cands[k].card_mv == charge) { ++used; }
            }
            if (used > capacity_for(charge)) { return false; }
        }
        return true;
    };

    std::vector<int> choice(num_groups, 0);
    bool done = false;
    while (!done)
    {
        for (int imask = 0; imask < (1 << num_ind); ++imask)
        {
            sel.clear();
            for (int g = 0; g < num_groups; ++g)
            {
                if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); }
            }
            for (int b = 0; b < num_ind; ++b)
            {
                if (imask & (1 << b)) { sel.push_back(independent[b]); }
            }
            if (!sel.empty() && vial_ok(sel)) { consider(sel); }
        }

        int g = 0;
        for (; g < num_groups; ++g)
        {
            ++choice[g];
            if (choice[g] <= static_cast<int>(groups[g].size())) { break; }
            choice[g] = 0;
        }
        if (g == num_groups) { done = true; }
    }

    return best;
}

// ============================================================
// Multi-turn lookahead
// ============================================================

// Tap mana sources in-place to pay a cost. Returns false if mana is unavailable.
// for_creature: if false, skip creature-only mana sources (e.g. Ancient Ziggurat)
//               since non-creature spells cannot be paid with that mana.
static bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature = true)
{
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed

    // Spend any turn-scoped RESERVE mana (a ritual's floating output) before tapping. No-op when
    // empty -> byte-identical for non-ritual decks. Mirrors AIEngine::TapForCost so the rollout
    // and the real executor realise a ritual's floating mana identically (lockstep).
    const ManaPool reserve_pre = state.floating_mana;
    ManaCost cost = cost_in;
    SpendFloatingTowardCost(state.floating_mana, cost);

    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def.params.mana_rock;
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        return true;
    };

    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        p.tapped = true;
        DecrementDepletionOnTap(p);
        if (def.params.tap_self_damage > 0) { state.players[active].life -= def.params.tap_self_damage; }
        // Grove of the Burnwillows: each coloured tap makes the opponent gain 1 (-> 1 damage
        // with Tainted Remedy out). Mirrored in AIEngine::TapForCost and TapForCostBacktrack.
        if (def.params.tap_opponent_lifegain > 0)
        { OpponentGainsLife(state, active, def.params.tap_opponent_lifegain); }
        floating.Add(col, ManaProducedPerTap(def));
    };

    // allow_ramp: may a ramp filter (Ferrous Lake) be used? false when feeding a ramp
    // filter's {1} so ramp filters never feed each other. Mirrors AIEngine::TapForCost.
    std::function<bool(Color,bool,bool)> produce = [&](Color needed, bool any, bool allow_ramp) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || def->params.is_filter || def->params.ramp_filter || !usable(p, *def)) { continue; }
            const std::vector<Color>& prod = EffectiveProduces(state, active, *def);  // RP-aware
            Color col;
            if (any)
            {
                if (prod.empty()) { continue; }
                col = prod[0];
            }
            else
            {
                bool match = false;
                for (Color c : prod) { if (c == needed) { match = true; break; } }
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
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
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
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
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
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                        if (!sd || sd->params.is_filter || sd->params.ramp_filter || !usable(s, *sd)) { continue; }
                        bool m = false;
                        for (Color c : EffectiveProduces(state, active, *sd)) { if (c == ic) { m = true; break; } }  // RP feeder
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

        // 4) Ramp filter (e.g. Ferrous Lake: {1},{T}: Add {U}{R}). Pay {1} generic from any
        //    other untapped source (incl. a filter's {C}), then yield one of each produces
        //    colour. No free mode; allow_ramp=false in the feed call prevents ramp chains.
        if (allow_ramp)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.ramp_filter || !usable(p, *def)) { continue; }
                if (!any)
                {
                    bool match = false;
                    for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                    if (!match) { continue; }
                }
                else if (def->params.produces.empty()) { continue; }
                if (floating.Total() == 0 && !produce(Color::Colorless, true, false)) { continue; }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                for (Color c : def->params.produces) { floating.Add(c, 1); }
                return true;
            }
        }
        return false;
    };

    auto pay = [&](Color needed, bool any) -> bool
    {
        if (!produce(needed, any, true)) { return false; }
        if (any) { Color took; return ConsumeFloatingAny(floating, took); }
        return ConsumeFloating(floating, needed);
    };

    // Greedy-first, then a backtracking fallback for filter chains the greedy strands
    // (e.g. Throes of Chaos via a Cascade Bluffs + Ferrous Lake chain). Snapshot so the
    // greedy's success path is byte-identical (no GT churn) and only previously-FAILING
    // casts gain the chain solution. See TapForCostBacktrack.
    const std::vector<Permanent> bf_pre = state.battlefield;
    const int life_pre = state.players[active].life;
    // Retain over-produced mana (forced filter/depletion over-tap) into the turn-scoped
    // reserve so a later same-(main-)phase cast can spend it (CR 500.4). state.floating_mana
    // already holds the un-spent reserve after SpendFloatingTowardCost; add the leftover on top.
    // Off (MTG_NO_FLOAT_LEFTOVER) -> no-op. Mirrored byte-for-byte in AIEngine::TapForCost.
    auto commit_leftover = [&](const ManaPool& lo)
    { if (FloatLeftoverManaEnabled()) { state.floating_mana.AddPool(lo); } };
    auto greedy = [&]() -> bool
    {
        for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) return false; }
        for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) return false; }
        for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) return false; }
        for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) return false; }
        for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) return false; }
        for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) return false; }
        for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) return false; }
        return true;
    };
    if (greedy()) { commit_leftover(floating); return true; }
    // Greedy failed: try the backtracking solver from a clean board.
    const std::vector<Permanent> bf_greedy_fail = state.battlefield;
    const int life_greedy_fail = state.players[active].life;
    state.battlefield        = bf_pre;
    state.players[active].life = life_pre;
    ManaPool bt_leftover;
    if (TapForCostBacktrack(state, cost, for_creature, ManaPool{}, nullptr, nullptr, &bt_leftover))
    { commit_leftover(bt_leftover); return true; }
    // Total failure: restore the greedy's exact end-state to match prior behaviour.
    state.battlefield        = bf_greedy_fail;
    state.players[active].life = life_greedy_fail;
    state.floating_mana      = reserve_pre;   // payment failed -> return the reserve untouched
    return false;
}

// Apply a plan to the game state sequentially (bypassing the stack).
// Mana is tapped in-place as each spell is cast rather than accumulated and
// tapped at the end, so the remaining pool is always correct at each step.
// Draw spells act as breakpoints: after drawing, Solve re-runs on the updated
// state so newly revealed cards can be cast with remaining mana this turn.
// Land-play helpers (defined below, near the land enumeration). PlayLandByName plays
// a specific named land; SimulateLandPlay is the greedy fallback used when a plan did
// not search the land (depth-0 static plans).
static bool PlayLandByName(GameState& state, const std::string& name,
                           const std::string& fetch_target = "");
static std::string SimulateLandPlay(GameState& state);

// Provider cast-order rank for a hand cast by name (lower = cast earlier). Thin lookup
// wrapper around DecisionProvider::CastOrderRank; mirrored byte-for-byte in AIEngine so the
// rollout's canonical cast order and the real executor's stay in lockstep. Unknown card
// (should not happen for a planned cast) falls to the noncreature rank.
static int CastRankOf(const GameState& state, const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    return d ? ResolveProvider(state).CastOrderRank(state, *d) : 20;
}

// A cast whose resolution triggers a mid-turn re-solve breakpoint (draw / staging / cascade
// / retrace): the rest of the turn re-solves from the post-draw state, so the optimal cast
// ORDER around it is situation-dependent (mana left, what is revealed) -- a static rank
// can't capture it. The CastOrderRank reordering is therefore SKIPPED for any set that
// contains such a card; that set keeps its canonical plan/breakpoint order (the search owns
// the ambiguous ordering). Mirrored in AIEngine.
static bool OrderingOpaque(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawUntilNonland
        || d->params.stages_cards
        || d->params.cascade_max_mv > 0
        || d->params.retrace
        || d->params.expressive_iteration
        || d->params.draw > 0;
}

static void ApplyPlanDirect(GameState& state, const TurnSolver::Plan& plan, bool is_pre_combat,
                            std::vector<Action>* out_breakpoint = nullptr)
{
    PROF_INC(applyplan_calls);
    Player& ap  = state.ActivePlayer();
    int opp_idx = 1 - state.active_player_index;

    // Commit-the-line recording (out_breakpoint != null, set only when building the
    // committed line): capture the casts each draw-breakpoint re-solve makes so the
    // AIEngine replay can reproduce them verbatim. sink_stack.back() is the container
    // for casts of the breakpoint currently being applied; an empty stack means we are
    // at the main-plan level (those casts are NOT recorded -- the committed plan.actions
    // already holds them -- but a main draw engine's OWN re-solve records into the
    // top-level out_breakpoint). See Action::breakpoint_casts.
    std::vector<std::vector<Action>*> sink_stack;

    // Deferred plain-cantrip (Ponder/Preordain) re-solve. A plain DrawSpell cast at the
    // MAIN-plan level used to re-solve INLINE — casting freshly-affordable spells right
    // after its draw, BEFORE the main plan's remaining casts. The executor instead casts
    // every main-plan spell first and replays the breakpoint only afterwards (its
    // is_draw_engine excludes plain cantrips), so the inline re-solve could spend mana a
    // later main cast still needed (Ponder+Ponder+Preordain off two blue sources) and
    // diverge from the realised game. We defer the re-solve to AFTER all main casts so it
    // uses only leftover mana — byte-for-byte the executor's post-loop replay. EI / staging
    // / Treasure Hunt / cascade (the executor's real draw engines) keep their inline
    // re-solve, and a plain cantrip cast INSIDE a re-solve (sink_stack non-empty) also stays
    // inline so its nested breakpoint records correctly. MTG_NO_DEFER_CANTRIP opts out
    // (the old inline behaviour) for the A/B. Inert for decks without plain cantrips.
    static const bool s_defer_cantrip = std::getenv("MTG_NO_DEFER_CANTRIP") == nullptr;
    bool deferred_cantrip_resolve = false;

    // Karoo bounce-land play-at-end timing. A Karoo (Izzet Boilerworks: etb_bounce_land,
    // enters tapped) returns one of our lands to hand on ETB. Played land-FIRST it bounces a
    // still-UNTAPPED land we then need, losing that land's mana this turn. A Karoo enters
    // tapped, so it provides no mana this turn regardless -- we therefore DEFER its play until
    // AFTER the main casts (below, before the deferred-cantrip re-solve). By then the lands we
    // needed are tapped, and BounceKarooLand returns a spent land for zero tempo loss. The land
    // drop is RESERVED for the Karoo: while deferred, a draw/cantrip breakpoint must not play a
    // revealed land as the drop (guarded in play_breakpoint_land / play_drawn_flood_keep_land).
    // Lockstep: AIEngine::TakeTurn defers its fold_land the same way. MTG_NO_KAROO_DEFER opts
    // out (old land-first behaviour) for the A/B. Inert for decks without a Karoo.
    static const bool s_karoo_defer = std::getenv("MTG_NO_KAROO_DEFER") == nullptr;
    bool        karoo_deferred = false;
    std::string karoo_land_name;
    std::string karoo_fetch;

    // Human-play mode (tools/play GUI): execute EXACTLY the committed plan -- suppress every
    // auto-heuristic that would play cards the human didn't choose (draw-breakpoint re-solve,
    // auto-dig, auto Land's Edge). After a draw the AIEngine chooser re-fires so the human
    // re-decides with the revealed cards. Set ONLY under --claude-play, so normal search /
    // goldfish runs are byte-identical (the flag is never set there). Function-local static so
    // it reads the env AFTER main's setenv (file-scope statics init too early).
    static const bool s_human_play = std::getenv("MTG_HUMAN_PLAY") != nullptr;

    // Land drop first, so the land's mana is available to the spells that follow.
    // A searched plan (land_decided) plays exactly its chosen land ("" == a deliberate
    // defer); an unsearched plan (depth-0 static Solve) falls back to greedy land play.
    if (is_pre_combat)
    {
        if (plan.land_decided)
        {
            if (!plan.land_to_play.empty())
            {
                const CardDefinition* ld = CardDatabase::Instance().Lookup(plan.land_to_play);
                if (s_karoo_defer && ld && ld->params.etb_bounce_land)
                {
                    // Reserve the drop; play it after the main casts (see karoo_deferred above).
                    karoo_deferred  = true;
                    karoo_land_name = plan.land_to_play;
                    karoo_fetch     = plan.fetch_target;
                }
                else
                {
                    PlayLandByName(state, plan.land_to_play, plan.fetch_target);
                }
            }
        }
        else
        {
            SimulateLandPlay(state);
        }
    }

    // Deploy creatures via Aether Vial before casting spells so lord effects are live.
    auto apply_vial = [&](const std::string& name)
    {
        const CardDefinition* copt = CardDatabase::Instance().Lookup(name);
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
            const CardDefinition* vdef = CardDatabase::Instance().LookupCached(vp.card);
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
            // ETB dig / legend rule also apply to Vial-deployed creatures (Vial is not a
            // cast, so no on-cast trigger, but the ETB still happens).
            if (copt->params.etb_dig_count > 0)
            {
                PerformEtbDig(state, state.active_player_index, copt->params,
                              &state.battlefield.back());
            }
            if (copt->card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }
            return;
        }
    };
    // Forward-declared so apply_one's draw breakpoints can re-apply a freshly solved
    // sub-plan (newly drawn castables) through the same canonical-order dispatch.
    std::function<void(const std::vector<Action>&, bool)> apply_plan_actions;

    // Play a revealed land as the turn's land drop inside a staged-draw breakpoint
    // (pre-combat only), mirroring the real engine's draw-engine second pass
    // (AIEngine::TryPlayLand). A Light Up the Stage land revealed by the draw frees
    // mana for the freshly revealed spells; without this the search under-developed
    // vs the real game (the gi=561 class: real cast a creature a turn earlier off the
    // revealed land). Records the played land (Kind::PlayLand) into `sink` so
    // commit-the-line replay reproduces it. Used only by the DrawSpell (stages_cards)
    // branch -- for Treasure Hunt's DrawUntilNonland the revealed lands are Land's
    // Edge ammo, not a land drop. Always mutates state; records only while building a
    // committed line (out_breakpoint && sink non-null).
    auto play_breakpoint_land = [&](std::vector<Action>* sink)
    {
        // Default engine behavior (mirrors s_fd_opp_spawns); MTG_LEGACY_SEARCH opts
        // back into the held-out baseline (byte-frozen old ground truth) for A/Bs.
        static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        std::string played = SimulateLandPlay(state);
        if (!played.empty() && out_breakpoint != nullptr && sink != nullptr)
        {
            Action la;
            la.kind      = Action::Kind::PlayLand;
            la.card_name = played;
            sink->push_back(la);
        }
    };

    // Part B (defer-the-land-until-you-see-the-draw): after a Treasure Hunt (DrawUntilNonland)
    // resolves, play the DEFERRED land drop now that the draw is known. If the hand is flooding
    // and no no-max-hand-size land is already in play, play a DRAWN Reliquary Tower so the whole
    // draw is KEPT as Land's Edge ammo (gi=65). Otherwise play the best normal land (chosen
    // against the post-draw hand) so the drop is developed and the land not discarded (gi=881: a
    // drawn Temple of Epiphany was discarded only because the deferred drop was never played).
    // Only when the land drop is still open (the plan deferred); records the play for
    // commit-the-line replay. Legacy keeps the frozen behavior (no land here).
    auto play_drawn_flood_keep_land = [&](std::vector<Action>* sink)
    {
        static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        Player& lp = state.ActivePlayer();
        if (lp.lands_played_this_turn >= lp.LandDropsAvailable()) { return; }   // drop already used

        // The keep-ammo land CHOICE is deck logic -> ask the provider (Hook 13); the engine
        // keeps the open-drop precondition above and the land-play mechanism below.
        std::string reliquary =
            ResolveProvider(state).PostDrawKeepLandName(state, state.active_player_index);
        if (!reliquary.empty())
        {
            if (PlayLandByName(state, reliquary, std::string{}) && out_breakpoint != nullptr && sink != nullptr)
            {
                Action la;
                la.kind      = Action::Kind::PlayLand;
                la.card_name = reliquary;
                sink->push_back(la);
            }
            return;
        }
        // No flood-keep land to play -> play the best normal land (the deferred drop), recorded.
        play_breakpoint_land(sink);
    };


    // One-shot flag: when set, the NEXT apply_one cast skips its mana cost (a free
    // cascade cast). Consumed at the top of apply_one so it applies to exactly one cast.
    bool cascade_free = false;
    std::function<void(const std::string&, bool, bool, int, bool, int, const std::string&, int, int, int)> apply_one;
    apply_one = [&](const std::string& name, bool is_sacrifice, bool from_graveyard, int discard_lands,
                    bool alt_cost, int alt_lifegain, const std::string& tutor_target, int chosen_x,
                    int own_targets, int ponder_keep)
    {
        // Find the card in its zone first, then resolve its definition via the card's cached
        // pointer -- avoids a by-name Lookup (string hash) on every cast (apply_one is per-cast,
        // ~200k/game). Byte-identical: it->m_name == name so LookupCached(*it) == Lookup(name),
        // and the two early-returns (not in zone / unknown def) yield the same outcome in either
        // order.
        std::vector<Card>& zone = from_graveyard ? ap.graveyard : ap.hand;
        std::vector<Card>::iterator it = std::find_if(zone.begin(), zone.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == zone.end()) { return; }
        const CardDefinition* opt = CardDatabase::Instance().LookupCached(*it);
        if (!opt) { return; }
        const CardDefinition& def = *opt;

        // Cast-time guard for a risky alt payload (Reverent Silence): the search may commit it
        // from a node whose enabler diverges away in the realized line (commit-the-line
        // non-convergence, gi=212). Re-check the gate on the CURRENT board: if no enabler
        // survives the wipe and it isn't lethal, SKIP it -- keep the card and our own
        // enchantments rather than self-brick. Inert for decks without a destroy-all alt.
        if (alt_cost && def.params.destroy_all_enchantments
            && !ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, def))
        {
            return;
        }

        bool is_creature = def.card.IsCreature();
        // Cascade casts its target for FREE (CR 702.84). Consume the one-shot flag here,
        // before any nested casts, so exactly THIS cast skips its mana cost.
        bool free_cast = cascade_free;
        cascade_free = false;
        ManaCost ec = EffectiveCost(def, state);
        // Soulfire Eruption: extra Hinata discount from the searched own-creature targets (mirrors
        // the enumeration cost and the executor's CastSpellFromHand -> lockstep).
        ec.generic = std::max(0, ec.generic
                       - SoulfireOwnTargetDiscount(def, state, state.active_player_index, own_targets));
        // {X} spells: pay the chosen X, once per {X} pip (Crackle {X}{X}{X} -> 3X generic).
        if (def.card.m_mana_cost.has_x && chosen_x > 0)
        {
            int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
            ec.generic += chosen_x * pips;
            ec.generic = std::max(0, ec.generic - HinataGenericDiscount(def, state, chosen_x));
        }
        if (!free_cast && !alt_cost && !TapForCostDirect(state, ec, is_creature)) { return; }
        zone.erase(it);

        // Alternative cost paid as "an opponent gains alt_lifegain life" (Invigorate / Skyshroud
        // Cutter / Reverent Silence) -> reversed to damage by a Tainted Remedy / Plague Drone.
        // Paid at cast (before on-cast triggers), then the spell resolves its normal effect.
        if (alt_cost) { OpponentGainsLife(state, state.active_player_index, alt_lifegain); }

        // Commit-the-line recording: if inside a breakpoint re-solve (sink_stack
        // non-empty), record THIS cast into the current sink so AIEngine can replay it
        // verbatim; its own draw-breakpoint casts nest under my_bp_sink. A main-plan
        // cast (empty stack) is not recorded -- the committed plan.actions already holds
        // it -- but its re-solve still records into the top-level out_breakpoint.
        std::vector<Action>* my_bp_sink = out_breakpoint;
        if (out_breakpoint && !sink_stack.empty())
        {
            Action rec;
            rec.kind = from_graveyard ? Action::Kind::CastFromGraveyard
                                      : Action::Kind::CastFromHand;
            rec.card_name      = name;
            rec.sacrifice_land = is_sacrifice;
            rec.discard_lands  = discard_lands;
            rec.alt_cost       = alt_cost;
            rec.alt_lifegain   = alt_lifegain;
            rec.tutor_target   = tutor_target;
            rec.chosen_x       = chosen_x;
            rec.soulfire_own_targets = own_targets;
            rec.ponder_keep    = ponder_keep;
            sink_stack.back()->push_back(rec);
            my_bp_sink = &sink_stack.back()->back().breakpoint_casts;
        }

        // Retrace additional cost: discard `discard_lands` land cards from hand.
        // (The mana cost was paid above; CollectActions ensured enough lands exist.)
        if (from_graveyard && discard_lands > 0)
        {
            int discarded = 0;
            for (std::vector<Card>::iterator hit = ap.hand.begin();
                 hit != ap.hand.end() && discarded < discard_lands; )
            {
                const CardDefinition* hdef = CardDatabase::Instance().LookupCached(*hit);
                bool is_land = hdef ? hdef->card.IsLand() : hit->IsLand();
                if (is_land) { ap.graveyard.push_back(*hit); hit = ap.hand.erase(hit); ++discarded; }
                else         { ++hit; }
            }
        }

        // On-cast triggers (Eidolon of the Great Revel) and Prowess fire when the spell
        // is CAST -- before it resolves AND before this spell's own permanent (if a
        // creature/enchantment) enters the battlefield. Fire them HERE, ahead of the
        // resolution branch below, to mirror the real engine: CastSpellFromHand pushes
        // the spell onto the stack and only then fires on-cast triggers, so the spell is
        // NOT yet on the battlefield and a permanent with its own on-cast trigger does
        // not trigger on its own cast (Eidolon casting Eidolon deals 0, not 2). Firing
        // after the branch (the old position) placed the creature first, so Eidolon
        // self-triggered -- over-counting rollout self-damage by 2 per Eidolon cast.
        FireOnCastTriggers(state, def);
        FireProwess(state, def);

        if (def.tmpl == CardTemplate::DirectDamage)
        {
            // Mirror EffectHandler::ResolveDirectDamage so the rollout's life total
            // matches the real game. Previously only Any/Player targeting dealt face
            // damage, so Searing Blaze (Multi) and Searing Blood (Creature) were inert
            // here while the win-check and the real engine both counted their damage —
            // a phantom-early-win source.
            Targeting t = def.params.targeting;
            // An {X} burn deals chosen X * x_damage_multiplier (Crackle = 5X; mirrors
            // EffectHandler::ResolveDirectDamage); a fixed-damage burn uses params.damage.
            int x_mult = def.params.x_damage_multiplier; if (x_mult < 1) { x_mult = 1; }
            int dmg = def.card.m_mana_cost.has_x ? (chosen_x * x_mult) : def.params.damage;
            if (!def.card.m_mana_cost.has_x
                && def.params.landfall_damage > 0 && ap.lands_played_this_turn > 0)
            {
                dmg = def.params.landfall_damage;
            }
            // Soulfire Eruption: bounded multi-target dig (exile + stage top N; face = max MV,
            // self = min MV). Mirrors EffectHandler so the rollout matches the executor (lockstep).
            if (def.params.damage_equals_top_mv)
            {
                SoulfireResult sr = SoulfireDig(state, state.active_player_index, own_targets);
                dmg = sr.face_damage;
                state.players[state.active_player_index].life -= sr.self_damage;
            }

            if (t == Targeting::Any || t == Targeting::Player)
            {
                // Human-play board-click targeting (claude-play): deal `dmg` to each chosen target
                // (face or a creature) instead of always the opponent face. The chooser is nulled by
                // RevealLogPause for the search/rollout, so this is byte-identical there (face only).
                // Uniform per-target damage: a fixed burn, or Crackle's 5X to up to X targets. Soulfire
                // (damage_equals_top_mv) is handled above and never reaches here as a retargetable set.
                if (g_play_target_chooser && dmg > 0 && !def.params.damage_equals_top_mv)
                {
                    // Divided damage (Fiery Justice): up to `dmg` targets, the chooser returns a
                    // per-target allocation summing to dmg. Uniform burn: 1 target (or X for Crackle),
                    // each taking the full dmg. The chooser branches on def.params.damage_divided.
                    bool divided = def.params.damage_divided;
                    int max_targets = divided ? dmg
                                    : (def.card.m_mana_cost.has_x && def.params.x_damage_multiplier > 0)
                                    ? std::max(1, chosen_x) : 1;
                    // Default: all damage to the opponent face (amount = dmg for divided, ignored else).
                    std::vector<ChosenTarget> heur = { { 0, opp_idx, divided ? dmg : 0 } };
                    // `dmg` is the ACTUAL per-target damage (fixed burn = base damage; Crackle = X*mult)
                    // or, for a divided spell, the TOTAL to allocate -- passed so the dialog shows the
                    // true number instead of recomputing (x_damage_multiplier defaults to 1, which made
                    // fixed burn mis-display as "1 damage").
                    std::vector<ChosenTarget> picked =
                        (*g_play_target_chooser)(state, def, state.active_player_index, max_targets, dmg, heur);
                    if (picked.empty()) { picked = heur; }
                    for (const ChosenTarget& c : picked)
                    {
                        int amt = divided ? c.amount : dmg;   // divided: per-target share; else flat dmg
                        if (amt <= 0) { continue; }
                        if (c.kind == 0)
                        {
                            state.players[c.index].life -= amt;
                            if (c.index == opp_idx) { state.opponent_lost_life_this_turn = true; }
                        }
                        else if (c.index >= 0 && c.index < static_cast<int>(state.battlefield.size()))
                        {
                            state.battlefield[c.index].damage += amt;   // SBA sweep below removes the dead
                        }
                    }
                    // State-based: destroy creatures with lethal damage (highest index first so the
                    // erase doesn't shift a not-yet-processed index).
                    for (int bi = static_cast<int>(state.battlefield.size()) - 1; bi >= 0; --bi)
                    {
                        Permanent& p = state.battlefield[bi];
                        if (p.card.IsCreature() && p.damage > 0 && p.damage >= p.EffectiveToughness())
                        {
                            state.players[p.owner_index].graveyard.push_back(p.card);
                            state.battlefield.erase(state.battlefield.begin() + bi);
                        }
                    }
                }
                else
                {
                    state.players[opp_idx].life -= dmg;
                    if (dmg > 0) { state.opponent_lost_life_this_turn = true; }
                }
            }
            else if (t == Targeting::Creature || t == Targeting::Multi)
            {
                // Both require an opponent creature; with none the spell has no legal
                // target and is not cast (mirrors CastSpellFromHand's early return).
                int ci = -1;
                for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
                {
                    const Permanent& bp = state.battlefield[bi];
                    if (bp.controller_index != state.active_player_index && bp.card.IsCreature())
                    { ci = bi; break; }
                }
                if (ci >= 0)
                {
                    if (t == Targeting::Multi)  // Searing Blaze also hits the player
                    {
                        state.players[opp_idx].life -= dmg;
                        if (dmg > 0) { state.opponent_lost_life_this_turn = true; }
                    }
                    Permanent& tgt = state.battlefield[ci];
                    tgt.damage += dmg;
                    bool lethal = tgt.damage >= tgt.EffectiveToughness();
                    // Death trigger (Searing Blood): if the creature now has lethal damage.
                    if (def.params.death_trigger_damage > 0 && lethal)
                    {
                        state.players[opp_idx].life -= def.params.death_trigger_damage;
                        state.opponent_lost_life_this_turn = true;
                    }
                    // State-based action: a creature with lethal damage is destroyed.
                    // Without this the rollout keeps the dead creature on the battlefield
                    // as a phantom target, so a later creature-targeting burn (Searing
                    // Blaze/Blood) "re-kills" it and invents face damage -> phantom early
                    // win. Mirrors the real engine's SBA after damage is dealt.
                    if (lethal) { state.battlefield.erase(state.battlefield.begin() + ci); }
                }
            }
            // Rider "target opponent gains N life" (Fiery Justice) -> reversed to damage by a
            // Tainted Remedy / Plague Drone. Mirrors EffectHandler::ResolveDirectDamage.
            if (def.params.opponent_lifegain > 0)
            {
                OpponentGainsLife(state, state.active_player_index, def.params.opponent_lifegain);
            }
            // Magma Opus rider: "draw two cards." Mirrors EffectHandler (lockstep).
            if (def.params.cast_draw > 0) { ap.library.DrawN(def.params.cast_draw, ap.hand); }
        }
        else if (is_creature)
        {
            Permanent perm;
            perm.card              = def.card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);

            // ETB library dig (Acclaimed Contender): performed inline so the clairvoyant
            // rollout sees the dug card in hand for later turns. The real game does the
            // SAME deterministic dig at resolution (EffectHandler), reaching identical
            // hand/library state -- no breakpoint/replay needed (the dug card is cast on a
            // later turn, not re-solved this turn). Use the just-pushed permanent as self.
            if (def.params.etb_dig_count > 0)
            {
                PerformEtbDig(state, state.active_player_index, def.params,
                              &state.battlefield.back());
            }

            // Legend rule: a duplicate legendary just cast is put into the graveyard, so a
            // second copy of a legendary lord confers no benefit (the search then avoids
            // casting it). No-op for non-legendary creatures.
            if (def.card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }

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
            // Expressive Iteration: look 3 -> 1 hand / 1 exiled-staged-this-turn / 1 bottom (its own
            // model). Mirrors EffectHandler::ResolveDrawSpell (lockstep). The breakpoint re-solve
            // below then plays the staged (this-turn-only) card.
            const bool is_ei = def.params.expressive_iteration;
            if (is_ei) { ResolveExpressiveIteration(state); }
            // Scry-then-draw (Preordain) / reorder-or-shuffle-then-draw (Ponder): mirror
            // ResolveDrawSpell exactly so the rollout's realised draw matches the executor.
            if (!is_ei && def.params.cast_scry > 0)    { ScryTop(state, def.params.cast_scry); }
            if (!is_ei && def.params.cast_reorder > 0) { ReorderTopOrShuffle(state, def.params.cast_reorder, def.card.m_name, ponder_keep); }
            int n = is_ei ? 0 : std::min(def.params.draw, static_cast<int>(ap.library.size()));
            if (!is_ei && def.params.stages_cards)
            {
                // Mirror ResolveDrawSpell: the cards are exiled and playable only until
                // the end of the controller's next turn (CR 406). Carry that expiry on
                // the card so the rollout expires them (see SimulateToEndImpl) instead
                // of keeping them forever — the latter let the rollout win with cards
                // the real game had already lost (a phantom-early-win source).
                int expiry = state.turn_number + 1;
                for (int d = 0; d < n; ++d)
                {
                    Card c = ap.library.DrawTop();
                    c.m_is_staged     = true;
                    c.m_staged_expiry = expiry;
                    ap.hand.push_back(std::move(c));
                }
            }
            else
            {
                ap.library.DrawN(n, ap.hand);
            }

            // Draw breakpoint: play a revealed land (the real engine's second pass
            // does), then re-solve with updated hand and remaining mana so newly
            // revealed cards can be cast with mana still available this turn.
            // A plain cantrip (Ponder/Preordain — NOT EI/staging) at the MAIN-plan level
            // (sink_stack empty) defers its re-solve until after every main cast, matching
            // the executor's post-loop replay (see deferred_cantrip_resolve). Everything
            // else (EI/staging, or a cantrip already inside a re-solve) re-solves inline.
            const bool plain_cantrip = !is_ei && !def.params.stages_cards;
            if (s_human_play)
            {
                // Human play: the cantrip drew; STOP here. The chooser re-fires so the human
                // re-decides with the drawn card (no auto re-solve, no auto land play).
            }
            else if (s_defer_cantrip && plain_cantrip && sink_stack.empty())
            {
                deferred_cantrip_resolve = true;
            }
            else
            {
                if (out_breakpoint && my_bp_sink) { sink_stack.push_back(my_bp_sink); }
                play_breakpoint_land(my_bp_sink);
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
                if (out_breakpoint && my_bp_sink) { sink_stack.pop_back(); }
            }
        }
        else if (def.tmpl == CardTemplate::DrawUntilNonland)
        {
            // Draw cards from the top until a nonland is found (inclusive) into hand.
            while (!ap.library.empty())
            {
                Card c = ap.library.DrawTop();
                auto cdef = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                ap.hand.push_back(std::move(c));
                if (!is_land) { break; }
            }
            // Draw breakpoint: play a DEFERRED land now that the draw is seen, then re-solve
            // so new castables are played with remaining mana. The flood-keep land play (part
            // B) plays a drawn Reliquary Tower as the open land drop so a flooded draw is KEPT
            // for Land's Edge rather than discarded at cleanup; other revealed lands remain
            // Land's Edge ammo (no land played). See play_drawn_flood_keep_land.
            if (!s_human_play)
            {
                if (out_breakpoint && my_bp_sink) { sink_stack.push_back(my_bp_sink); }
                play_drawn_flood_keep_land(my_bp_sink);
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
                if (out_breakpoint && my_bp_sink) { sink_stack.pop_back(); }
            }
            // Human play: Treasure Hunt's reveal is now in hand; the chooser re-fires so the
            // human plays a land / Land's Edge / another Treasure Hunt with the revealed cards.
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
                auto cdef = CardDatabase::Instance().LookupCached(c);
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
                auto cdef2 = CardDatabase::Instance().LookupCached(exiled[cascade_idx]);
                if (cdef2)
                {
                    ap.hand.push_back(cdef2->card);
                    cascade_free = true;   // cascade cast pays no mana
                    apply_one(cname, false, false, 0, false, 0, std::string{}, 0, 0, -1);
                }
            }
        }
        else if (def.tmpl == CardTemplate::Removal)
        {
            // Removal (Swords to Plowshares): exile/destroy the first opponent creature and
            // apply the controller-lifegain rider (-> damage with Tainted Remedy). Mirrors
            // EffectHandler::ResolveRemoval. CollectActions only offers this with a legal
            // opponent creature target present.
            int ci = -1;
            for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
            {
                const Permanent& bp = state.battlefield[bi];
                if (bp.controller_index != state.active_player_index && bp.card.IsCreature())
                { ci = bi; break; }
            }
            if (ci >= 0)
            {
                int tgt_controller = state.battlefield[ci].controller_index;
                int tgt_power      = state.battlefield[ci].EffectivePower();
                if (def.params.damage > 0) { state.exile.push_back(state.battlefield[ci].card); }
                else { state.players[tgt_controller].graveyard.push_back(state.battlefield[ci].card); }
                state.battlefield.erase(state.battlefield.begin() + ci);
                if (def.params.controller_lifegain_equals_power && tgt_power > 0)
                {
                    OpponentGainsLife(state, 1 - tgt_controller, tgt_power);
                }
            }
        }
        else if (IsManaRitual(def))
        {
            // Reality Spasm / Irencrag Feat -- mana RITUAL. Float its mana into the turn-scoped
            // reserve for a same-turn payoff (Crackle). Mirrors EffectHandler so the rollout and
            // the real executor realise the identical floating mana (lockstep); the planner
            // credits this same amount, so the predicted combo and the executed one never diverge.
            ApplyRitualFloat(state, def, chosen_x);
        }
        else if (def.params.tutor_to_hand || def.params.tutor_to_top)
        {
            // Tutor (Idyllic / Enlightened): fetch the SEARCHED target (tutor_target); empty
            // falls back to the heuristic's top pick. Identical to the real game (EffectHandler)
            // so the clairvoyant rollout sees the same fetched card.
            PerformTutor(state, state.active_player_index, def.params, tutor_target, def.card.m_name);
        }
        else if (def.params.destroy_all_enchantments)
        {
            DestroyAllEnchantments(state);
        }
        else if (def.tmpl == CardTemplate::PumpSpell)
        {
            // "+N/+M until end of turn" (Invigorate) on the controller's best attacker, so the
            // rollout's combat reflects the pump. Mirrors EffectHandler::ResolvePumpSpell.
            int ti = def.params.target_own_creature
                     ? FindBestOwnAttacker(state, state.active_player_index)
                     : -1;
            if (ti >= 0)
            {
                state.battlefield[ti].temp_power_bonus += def.params.power_bonus;
                state.battlefield[ti].temp_tough_bonus += def.params.tough_bonus;
            }
        }
        else if (!def.card.IsInstant() && !def.card.IsSorcery())
        {
            // Non-creature permanent (e.g. Aether Vial, Tainted Remedy, Aria of Flame): place
            // on battlefield, then apply any ETB "each opponent gains N life" (Aria of Flame)
            // -> reversed to damage by a Tainted Remedy / Plague Drone.
            Permanent perm;
            perm.card              = def.card;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            if (def.params.etb_opponent_lifegain > 0)
            {
                OpponentGainsLife(state, state.active_player_index, def.params.etb_opponent_lifegain);
            }
        }

        // (On-cast triggers + Prowess already fired above, at cast time, before the
        // resolution branch -- see the note there.)

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

    // Canonical execution order, applied within each kind:
    //   ActivateVial -> hand casts (non-sacrifice, in provider CastOrderRank order)
    //   -> hand casts (sacrifice-land) -> graveyard casts (Retrace).
    // The non-sacrifice hand casts are stable-sorted by DecisionProvider::CastOrderRank
    // (enabler-first, prowess creatures before noncreature spells, on-cast self-damage
    // sources last); see the canonical branch below. Byte-identical for a deck whose ranks
    // don't reorder its casts.
    apply_plan_actions = [&](const std::vector<Action>& acts, bool explicit_order)
    {
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::ActivateVial) { apply_vial(a.card_name); }
        }
        if (explicit_order)
        {
            // Cast-ordering search: play the non-sacrifice hand casts in the EXACT vector
            // order the search chose (no enabler-first bucketing), so interleavings the
            // canonical order batches wrong are reachable. See Plan::searched_order.
            for (const Action& a : acts)
            {
                if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
                {
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep);
                }
            }
        }
        else
        {
            // Reorder the non-sacrifice hand casts by CastOrderRank, EXCEPT when the set has
            // a re-solve breakpoint card (draw/staging/cascade): its ordering is search-owned
            // (Light Up the Stage can't be ordered optimally without search), so keep the
            // canonical enabler-first + plan order there. The rank encodes the full-search-
            // grounded ordering rules (prowess creatures early, on-cast self-damage sources
            // last, ...) and grows as analysis surfaces more; d0 imperfection is acceptable.
            // Definitive validation is the with/without-heuristic per-game A/B.
            bool opaque = false;
            for (const Action& a : acts)
            {
                if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land
                    && OrderingOpaque(a.card_name)) { opaque = true; break; }
            }
            auto is_enabler = [&](const Action& a)
            {
                return a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !a.alt_cost
                    && ResolveProvider(state).CastEnablerFirst(state, a.card_name);
            };
            if (opaque)
            {
                for (const Action& a : acts)
                { if (is_enabler(a)) { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep); } }
                for (const Action& a : acts)
                {
                    if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !is_enabler(a))
                    { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep); }
                }
            }
            else
            {
                // Clean set: stable-sort the non-sacrifice hand casts by CastOrderRank
                // (enabler-first, prowess creatures before noncreature spells, on-cast
                // self-damage sources last). Stable => plan order breaks ties. Mirrored in
                // AIEngine::TakeTurn so rollout and executor stay in lockstep.
                std::vector<int> order;
                for (int i = 0; i < static_cast<int>(acts.size()); ++i)
                {
                    if (acts[i].kind == Action::Kind::CastFromHand && !acts[i].sacrifice_land)
                    { order.push_back(i); }
                }
                std::stable_sort(order.begin(), order.end(), [&](int x, int y)
                { return CastRankOf(state, acts[x].card_name) < CastRankOf(state, acts[y].card_name); });
                for (int i : order)
                {
                    const Action& a = acts[i];
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep);
                }
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            {
                apply_one(a.card_name, true, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep);
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromGraveyard)
            {
                apply_one(a.card_name, false, true, a.discard_lands, false, 0, std::string{}, a.chosen_x, a.soulfire_own_targets, a.ponder_keep);
            }
        }

        // Discard-to-Land's-Edge activations the human/search committed as an explicit plan
        // action (Kind::DiscardToLandsEdge). In autonomous play Land's Edge is auto-fired by the
        // post-cast heuristic loop below (suppressed under s_human_play); here we apply the
        // EXACT count the committed plan carries -- discard `discard_lands` lands from hand and
        // deal `rate` per land, where rate is the best discard_land_damage among controlled
        // permanents. Fired after all casts (it is a post-stack main-phase activation), in plan
        // order; a count > lands-in-hand simply fires every land present.
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::DiscardToLandsEdge || a.discard_lands <= 0) { continue; }
            int le_rate = 0;
            for (const Permanent& p : state.battlefield)
            {
                if (p.controller_index != state.active_player_index) { continue; }
                auto le_def = CardDatabase::Instance().LookupCached(p.card);
                if (le_def && le_def->params.discard_land_damage > 0)
                { le_rate = std::max(le_rate, le_def->params.discard_land_damage); }
            }
            if (le_rate <= 0) { continue; }   // no Land's Edge in play -> nothing to fire
            Player& le_ap = state.ActivePlayer();
            std::vector<Card> keep;
            int fired = 0;
            for (Card& c : le_ap.hand)
            {
                auto cdef    = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                if (is_land && fired < a.discard_lands)
                {
                    le_ap.graveyard.push_back(c);
                    state.players[opp_idx].life -= le_rate;
                    state.opponent_lost_life_this_turn = true;
                    ++fired;
                }
                else { keep.push_back(std::move(c)); }
            }
            le_ap.hand = std::move(keep);
        }

        // Auto-fire safe alt payloads (Invigorate / Skyshroud) once everything else has
        // resolved and a Remedy is live -> each is free face damage. Deterministic (not a
        // search choice), so no enumeration blow-up; re-scan after each because firing one
        // mutates the hand (and can add a verse trigger). No-op for decks without alt cards.
        // Hard termination guard: each pass must REMOVE the chosen card from hand; if a cast
        // does not (a fizzled/uncastable alt), stop -- never spin on the same card.
        // SUPPRESSED under UNPRUNED: there the safe alt is enumerated as a real cast choice
        // (CollectActions), so the search/human decides whether to fire it -- auto-firing it
        // here too would double-cast it AND override that decision.
        if (!DecisionUnpruned())
        for (;;)
        {
            Player& ap2 = state.ActivePlayer();
            int target = -1; int amt = 0;
            for (int i = 0; i < static_cast<int>(ap2.hand.size()); ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(ap2.hand[i]);
                if (d && ResolveProvider(state).CanAutoFireAltPayload(state, state.active_player_index, *d))
                { target = i; amt = d->params.alt_lifegain_cost; break; }
            }
            if (target < 0) { break; }
            std::string nm = ap2.hand[target].m_name;
            size_t before = ap2.hand.size();
            apply_one(nm, false, false, 0, true, amt, std::string{}, 0, 0, -1);
            if (state.ActivePlayer().hand.size() >= before) { break; }   // didn't consume -> stop
        }
    };

    apply_plan_actions(plan.actions, plan.searched_order);

    // Play the deferred Karoo bounce land now -- after the main casts have tapped the lands we
    // needed, so BounceKarooLand returns a SPENT land at no tempo cost (see karoo_deferred
    // above). Done before the deferred-cantrip re-solve so the drop is taken (lands_played==1)
    // and that re-solve never plays a freshly-revealed land as the drop. AIEngine mirrors this
    // (its Karoo play sits between the main cast loop and the breakpoint replay).
    if (karoo_deferred)
    {
        karoo_deferred = false;
        PlayLandByName(state, karoo_land_name, karoo_fetch);
    }

    // Deferred plain-cantrip re-solve: run ONCE, after every main-plan cast, using only the
    // mana those casts left — the executor's post-loop breakpoint replay does exactly this,
    // so recording it into out_breakpoint (the committed breakpoint_actions) keeps the two in
    // lockstep. A cantrip cast within this re-solve has sink_stack non-empty and so re-solves
    // inline, recording into its own nested breakpoint (replayed recursively by the executor).
    if (deferred_cantrip_resolve)
    {
        deferred_cantrip_resolve = false;
        if (out_breakpoint) { sink_stack.push_back(out_breakpoint); }
        play_breakpoint_land(out_breakpoint);
        TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
        apply_plan_actions(extra.actions, false);
        if (out_breakpoint) { sink_stack.pop_back(); }
    }

    // Dig when stuck (cycling / sacrifice-to-draw lands, e.g. Lonely Sandbar, Forgotten
    // Cave, Fiery Islet): spend a surplus land to draw toward action -- chiefly Treasure
    // Hunt, whose reveal refills Land's Edge ammo. Done in the rollout so the clairvoyant
    // search MODELS the dig (a Treasure Hunt dug within the horizon pulls the win earlier);
    // each dig is recorded into out_breakpoint (Kind::DigDraw, post-draw casts nested in
    // breakpoint_casts) so the executor replays the dug line verbatim rather than
    // re-solving (which drifts on land/mana state and would miss the win). Only decks with
    // a dig source enter the loop, so burn/slivers stay byte-identical. ShouldConsiderDig
    // encodes when NOT to dig (a draw engine already in hand, a retrace engine in the
    // graveyard, fewer than two lands, or Land's Edge already lethal from the hand).
    if (!s_human_play && is_pre_combat && ResolveProvider(state).HasAnyDigSource(state))
    {
        int dig_guard = 0;
        while (dig_guard++ < 16 && ResolveProvider(state).ShouldConsiderDig(state) && !ap.library.empty())
        {
            ManaPool pool = BuildPool(state);
            bool is_sac = false;
            std::string src = ResolveProvider(state).SelectDigSource(state, pool, is_sac);
            if (src.empty()) { break; }
            const CardDefinition* sd = CardDatabase::Instance().Lookup(src);
            if (!sd) { break; }

            if (is_sac)
            {
                // {cost},{T},Sacrifice: tap the source first (the {T}) so it isn't its own
                // mana source, pay the remaining mana, then sacrifice it.
                int idx = -1;
                for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
                {
                    const Permanent& p = state.battlefield[i];
                    if (p.controller_index == state.active_player_index
                        && !p.tapped && p.card.m_name == src) { idx = i; break; }
                }
                if (idx < 0) { break; }
                state.battlefield[idx].tapped = true;
                if (!TapForCostDirect(state, sd->params.sacrifice_draw_cost.value(), false))
                {
                    state.battlefield[idx].tapped = false;
                    break;
                }
                ap.graveyard.push_back(state.battlefield[idx].card);
                state.battlefield.erase(state.battlefield.begin() + idx);
            }
            else
            {
                if (!TapForCostDirect(state, sd->params.cycling_cost.value(), false)) { break; }
                std::vector<Card>::iterator it = std::find_if(ap.hand.begin(), ap.hand.end(),
                    [&src](const Card& c) { return c.m_name == src; });
                if (it == ap.hand.end()) { break; }
                ap.graveyard.push_back(*it);
                ap.hand.erase(it);
            }

            // Draw one. Record EVERY dig (even a land) so the executor replays the exact
            // cycle/sacrifice sequence and stays in library/hand sync. Deck-out safe: stop digging
            // if the library is empty (can't draw from an empty library).
            if (ap.library.empty()) { break; }
            Card drawn = ap.library.DrawTop();
            const CardDefinition* ddef = CardDatabase::Instance().LookupCached(drawn);
            bool drew_land = ddef ? ddef->card.IsLand() : drawn.IsLand();
            ap.hand.push_back(std::move(drawn));

            std::vector<Action>* my_bp_sink = out_breakpoint;
            if (out_breakpoint)
            {
                Action rec;
                rec.kind          = Action::Kind::DigDraw;
                rec.card_name     = src;
                rec.dig_sacrifice = is_sac;
                out_breakpoint->push_back(rec);
                my_bp_sink = &out_breakpoint->back().breakpoint_casts;
            }

            // Dig THROUGH lands toward the first nonland (Treasure Hunt is a nonland). On a
            // land we keep digging; on a nonland we re-solve so the found action is cast
            // THIS turn (exactly like the DrawUntilNonland breakpoint), then stop -- once
            // we have action we are no longer stuck.
            if (!drew_land)
            {
                if (out_breakpoint) { sink_stack.push_back(my_bp_sink); }
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
                if (out_breakpoint) { sink_stack.pop_back(); }
                break;
            }
        }
    }

    // Activate Land's Edge after all spells resolve (mirrors GameEngine::MainPhase's
    // post-stack ActivateLandsEdge call). Human play: SUPPRESSED -- the human decides whether to
    // discard lands to Land's Edge (else the engine would auto-burn the lands they just drew).
    if (!s_human_play)
    {
        int rate = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            auto le_def = CardDatabase::Instance().LookupCached(p.card);
            if (le_def && le_def->params.discard_land_damage > 0)
            {
                rate = std::max(rate, le_def->params.discard_land_damage);
            }
        }
        if (rate > 0)
        {
            // Default uses the real engine's conditional heuristic so the search does
            // not over-count the Land's Edge burst -- the gi=947 class: the search fired
            // every drawn land for opp -24 at T5 where the real engine holds them (fire
            // only for lethal / cleanup excess), slipping the win to T10. The legacy
            // baseline (MTG_LEGACY_SEARCH) fires ALL lands -- its rollouts are frozen as
            // the held-out ground truth.
            static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
            int fire_count = s_fd ? ResolveProvider(state).LandsEdgeFireCount(state, rate)
                                  : std::numeric_limits<int>::max();

            std::vector<Card> keep;
            int fired = 0;
            for (Card& c : ap.hand)
            {
                auto cdef    = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                if (is_land && fired < fire_count)
                {
                    ap.graveyard.push_back(c);
                    state.players[opp_idx].life -= rate;
                    state.opponent_lost_life_this_turn = true;
                    ++fired;
                }
                else { keep.push_back(std::move(c)); }
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
    // Mana empties when leaving the pre-combat main phase (CR 500.4): drop any reserve
    // floated this main phase so it cannot fund combat or the post-combat main. Mirrors
    // GameEngine::CombatPhase. Off (MTG_NO_FLOAT_LEFTOVER) -> no-op (pool only ever held
    // ritual float, which was already spent this main phase -> byte-identical regardless).
    if (FloatLeftoverManaEnabled()) { state.floating_mana = ManaPool{}; }
    int opp_idx = 1 - state.active_player_index;
    int active  = state.active_player_index;

    // Legend rule before declaring attackers (mirror GameEngine::CombatPhase): a duplicate
    // legendary lord cannot double-count its buff. No-op without legendaries.
    EnforceLegendRule(state, active);

    // Eligible attacker indices BEFORE any token creation (push_back keeps indices stable).
    std::vector<int> atk_idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!ResolveProvider(state).ShouldAttackWith(state, p)) { continue; }
        atk_idx.push_back(i);
    }

    // Attack-trigger tokens (Adeline), tapped and attacking this combat, then persist.
    if (!atk_idx.empty())
    {
        int tok_start = FireAttackCreateTokens(state, active);
        for (int i = tok_start; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            atk_idx.push_back(i);
        }
    }

    // Exalted (Ignoble Hierarch): +1/+1 per Exalted ability to a creature attacking ALONE.
    int exalted_bonus = (static_cast<int>(atk_idx.size()) == 1)
                        ? CountExalted(state.battlefield, active) : 0;

    std::vector<const Permanent*> attackers;
    attackers.reserve(atk_idx.size());
    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, active, animated, &p);
        bool ds = animated
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
        int power = base_pw * (ds ? 2 : 1);
        state.players[opp_idx].life -= power;
        if (power > 0) { state.opponent_lost_life_this_turn = true; }
        if (!p.card.HasKeyword(Keyword::Vigilance)) { p.tapped = true; }
        attackers.push_back(&p);
    }
    int trigger_life_loss = CountAttackTriggerLifeLoss(state.battlefield, active, attackers);
    if (trigger_life_loss > 0)
    {
        state.players[opp_idx].life -= trigger_life_loss;
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
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
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
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
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
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }

    // Discard down to hand size 7. Default mirrors AIEngine::ChooseDiscard: when a
    // Land's Edge outlet exists (the lands are ammunition, the spells are the combo)
    // shed a LAND first; otherwise shed the highest-MV card. The legacy baseline
    // (MTG_LEGACY_SEARCH) keeps the highest-MV-only rule (frozen as the held-out ground
    // truth). Without this the search kept every drawn land as Land's Edge ammo while
    // the real game discards lands here, over-counting Land's Edge damage (gi=947).
    static const bool s_fd_discard = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
    while (!unlimited_hand && ap.hand.size() > 7)
    {
        // Default (commit-the-line): use the SHARED selector so the rollout sheds exactly the
        // card the real engine's ChooseDiscard would -- required-piece protection + land-outlet
        // ammo, reading the deck's pieces from state.m_required_pieces. Without this the rollout
        // shed high-MV spells and hoarded lands, predicting a phantom Land's Edge flood (gi=220).
        // Legacy (MTG_LEGACY_SEARCH): the frozen highest-MV-only rule (held-out ground truth).
        std::vector<Card>::iterator victim;
        if (s_fd_discard)
        {
            int idx = SelectCleanupDiscardIndex(state, state.m_required_pieces);
            victim = ap.hand.begin() + idx;
        }
        else
        {
            victim = std::max_element(ap.hand.begin(), ap.hand.end(),
                [](const Card& a, const Card& b)
                {
                    return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
                });
        }
        ap.graveyard.push_back(*victim);
        ap.hand.erase(victim);
    }

    // Reset per-turn damage marks, "until end of turn" power/toughness boosts, and
    // animation effects (CR 514.2) — mirrors GameEngine::CleanupStep. Resetting the
    // temp_*_bonus fields is essential: without it prowess (and any until-end-of-turn
    // buff) accumulates across rollout turns, so the clairvoyant rollout over-counts a
    // prowess creature's combat damage on later turns and predicts a phantom early win
    // the real game (which clears the bonus each cleanup) never reaches. That mismatch
    // was the root cause of non-convergence on the burn deck (Monastery Swiftspear).
    for (Permanent& p : state.battlefield)
    {
        p.damage           = 0;
        p.temp_power_bonus = 0;
        p.temp_tough_bonus = 0;
        p.is_animated      = false;
    }

    // Start of next turn
    ++state.turn_number;
    state.opponent_lost_life_this_turn = false;
    state.floating_mana            = ManaPool{};   // reserve (ritual) mana empties each turn (CR 500.4)
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
    // Materialise any passive opponent creatures scheduled for this turn, mirroring
    // GameEngine::StartTurnStep. Without this the rollout never sees the opponent's
    // board, so creature-targeted burn (Searing Blaze / Searing Blood) is wrongly
    // treated as having no legal target: the search undervalues those cards and misses
    // lines that use them. Token shape/flags match GameEngine exactly so the rollout's
    // board == the real game's.
    //
    // ON for the default (commit-the-line) engine; OFF under MTG_LEGACY_SEARCH.
    // Measured rationale, not a punt: BASELINE SolveWithLookahead re-decides every turn
    // against the REAL board, so it already handles opponent creatures where it matters
    // (the actual play) and gains NOTHING from modelling them in its rollout -- enabling
    // it for baseline left burn/slivers' fingerprints unchanged and only perturbed 3
    // games via rollout/bottoming noise (burn gi=278 5->6; th d3 s2002 gi=72 4->5, gi=97
    // 5->6), all slightly worse, 0 better. Only commit-the-line, which REPLAYS the
    // search's line and cannot re-decide, actually needs the rollout's board accurate.
    static const bool s_fd_opp_spawns = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
    if (s_fd_opp_spawns && state.opponent_spawns)
    {
        int opp_index = 1 - state.active_player_index;
        for (const OpponentSpawn& spawn : *state.opponent_spawns)
        {
            if (spawn.turn != state.turn_number) { continue; }
            Card token;
            token.m_name      = std::to_string(spawn.power) + "/"
                              + std::to_string(spawn.toughness) + " Creature";
            token.RehashName();
            token.AddType(CardType::Creature);
            token.m_power     = spawn.power;
            token.m_toughness = spawn.toughness;
            Permanent perm;
            perm.card             = token;
            perm.controller_index = opp_index;
            perm.owner_index      = opp_index;
            state.battlefield.push_back(perm);
        }
    }

    // Forbidden Orchard: one opponent 1/1 Spirit per Orchard the active player controls this turn
    // (assume each is tapped for mana). Mirrors GameEngine (executor) at the same turn-start point
    // -> lockstep. Gated by s_fd_opp_spawns so MTG_LEGACY_SEARCH stays byte-identical to the old model.
    if (s_fd_opp_spawns) { SpawnForbiddenOrchardTokensTurnStart(state); }

    Player& ap_upkeep = state.ActivePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.upkeep_adds_charge) { continue; }
        // Hand-aware charge policy, shared with the real engine (AIEngine::DecideVialCharge)
        // so the rollout models the same charge the executor will make.
        if (ResolveProvider(state).WantVialCharge(state, p)) { ++p.charge_counters; }
    }

    // Upkeep token creation (e.g. Thrumming Hivepool). Iterate over initial size only.
    int upkeep_bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < upkeep_bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != state.active_player_index) { continue; }
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
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

// Play a specific named land from hand onto the battlefield, resolving its
// enters-tapped / depletion / scry effects. Returns false if the land drop is
// unavailable or no such card is in hand. Shared by the greedy fallback
// (SimulateLandPlay) and the searched land fold (ApplyPlanDirect) so both produce
// byte-identical placement for the same card.
static bool PlayLandByName(GameState& state, const std::string& name,
                           const std::string& fetch_target)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        auto def = CardDatabase::Instance().LookupCached(*it);
        if (!def || !def->card.IsLand()) { continue; }

        // Fetchland: the land drop sacrifices the fetchland to search out a real land.
        // fetch_target names the searched choice (Pass 2); empty -> PerformFetch falls back
        // to FetchCandidates' top heuristic pick (Pass 1 / single-candidate).
        if (!def->params.fetch_land_types.empty())
        {
            Card fetchland = *it;
            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            ap.graveyard.push_back(fetchland);
            PerformFetch(state, state.active_player_index, def->params, fetch_target);
            return true;
        }

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
        if (def->params.etb_scry > 0)    { ScryTop(state, def->params.etb_scry); }
        if (def->params.etb_surveil > 0) { SurveilTop(state, def->params.etb_surveil); }
        if (def->params.etb_bounce_land) { BounceKarooLand(state, state.active_player_index, static_cast<int>(state.battlefield.size()) - 1); }
        // Forbidden Orchard played this turn -> tapped this turn -> opponent Spirit now (the
        // turn-start spawn only covers copies already in play). Mirrors AIEngine::TryPlaySpecificLand;
        // gated like the turn-start spawn so MTG_LEGACY_SEARCH keeps the old model.
        if (IsForbiddenOrchard(def))
        {
            static const bool s_orchard_onplay = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
            if (s_orchard_onplay) { SpawnOpponentSpirit(state); }
        }
        return true;
    }
    return false;
}

// Greedy land play: one land drop per turn, preferring multi-color lands over
// colorless-only lands (e.g. Mutavault) so colored spells stay castable.
// Two-pass: multi-color first, then any land. Used as the fallback when a plan did
// not search its land (depth-0 static Solve plans and the rollout horizon leaf).
static std::string SimulateLandPlay(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return std::string(); }

    // Does the active player control another land (one a Karoo could bounce)?
    const int active = state.active_player_index;
    bool has_other_land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == active && p.card.IsLand()) { has_other_land = true; break; }
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (const Card& c : ap.hand)
        {
            auto def = CardDatabase::Instance().LookupCached(c);
            if (!def || !def->card.IsLand()) { continue; }

            // A Karoo bounce land with no other land in play must return ITSELF (the bounce is
            // mandatory) -- net no land in play and a wasted drop. Never the greedy choice; play a
            // real land first (matches the searched land drop, which rejects the self-bounce).
            if (def->params.etb_bounce_land && !has_other_land) { continue; }

            bool is_multi = def->params.produces.size() > 1;
            if (pass == 0 && !is_multi) { continue; }

            std::string name = c.m_name;
            PlayLandByName(state, name);
            return name;
        }
    }
    return std::string();
}

// Cast-ordering search gate (C): when on, EnumeratePlans expands each action SET into the
// DISTINCT orderings of its non-sacrifice hand casts (deduped by end-of-phase state),
// instead of only the canonical enabler-first order -- so interleavings the canonical
// heuristic batches wrong (enabler / destroy-all-payload rebuilds) are reachable. Off by
// default => byte-identical (canonical order). On via MTG_SEARCH_ORDER or the global
// MTG_UNPRUNED. Expensive (applies each tried ordering on a copy); run with a high budget.
static bool OrderingSearchEnabled()
{
    static const bool v = (std::getenv("MTG_SEARCH_ORDER") != nullptr) || DecisionUnpruned();
    return v;
}

// Defined after EnumeratePlans; used here only as an end-of-phase STATE signature for
// ordering dedup (two orderings with the same key drive an identical rollout, so keeping
// one is lossless -- the same omissions that make it a valid rollout memo key make it a
// valid ordering-equivalence key).
static TranspositionTable::Key BuildSimKey(const GameState& state, int depth, int max_turns,
                                           bool second_main);

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
// --- Branching diagnostics (MTG_BRANCH_STATS, off by default = zero cost) ---------------------
// Answers "which situations cause the most branching?": per EnumeratePlans call it attributes the
// raw odometer size (product of per-group option counts x 2^independent) and the final plan count
// to the card driving the biggest option-group, aggregates by that driver, and dumps a ranked
// table at exit. Run single-threaded for clean numbers (MTG_BRANCH_STATS=1 ... --threads 1).
namespace branchstats
{
    inline bool Enabled() { static const bool v = std::getenv("MTG_BRANCH_STATS") != nullptr; return v; }
    struct Bucket { uint64_t calls = 0; double odo = 0, final_plans = 0, raw_plans = 0; uint64_t max_odo = 0; };
    inline std::mutex                          g_mtx;
    inline std::map<std::string, Bucket>       g_by_driver;   // keyed by biggest-group card name
    inline std::map<std::string, Bucket>       g_by_situ;     // keyed by coarse situation label
    inline Bucket                              g_total;

    inline void Record(const std::string& driver, const std::string& situ,
                       double odo, uint64_t raw, uint64_t final_plans)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto upd = [&](Bucket& b) {
            ++b.calls; b.odo += odo; b.raw_plans += raw; b.final_plans += final_plans;
            if (static_cast<uint64_t>(odo) > b.max_odo) { b.max_odo = static_cast<uint64_t>(odo); }
        };
        upd(g_by_driver[driver]); upd(g_by_situ[situ]); upd(g_total);
    }

    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            auto dump = [](const char* title, std::map<std::string, Bucket>& m) {
                std::vector<std::pair<std::string, Bucket>> v(m.begin(), m.end());
                std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.second.odo > b.second.odo; });
                std::fprintf(stderr, "\n=== BRANCH STATS: %s (sorted by total odometer) ===\n", title);
                std::fprintf(stderr, "%-34s %10s %14s %12s %12s %10s\n",
                             "key", "calls", "sum_odo", "sum_final", "avg_odo", "max_odo");
                for (size_t i = 0; i < v.size() && i < 20; ++i) {
                    const Bucket& b = v[i].second;
                    std::fprintf(stderr, "%-34s %10llu %14.0f %12.0f %12.1f %10llu\n",
                        v[i].first.c_str(), (unsigned long long)b.calls, b.odo, b.final_plans,
                        b.calls ? b.odo / b.calls : 0.0, (unsigned long long)b.max_odo);
                }
            };
            std::fprintf(stderr, "\n=== BRANCH STATS: total EnumeratePlans calls=%llu sum_odo=%.0f sum_final=%.0f sum_raw=%.0f ===\n",
                (unsigned long long)g_total.calls, g_total.odo, g_total.final_plans, g_total.raw_plans);
            dump("by driver card (biggest option-group)", g_by_driver);
            dump("by situation", g_by_situ);
        }
    };
    inline Dumper g_dumper;
}

static std::vector<TurnSolver::Plan> EnumeratePlans(const GameState& state, bool is_pre_combat)
{
    // Enumeration SCORES candidate plans by applying them on copies (ApplyPlanDirect resolves their
    // scry/dig/cantrips), which is hypothetical, not real resolution. Pause the human-play choosers
    // and reveal logging for the whole scoring pass so claude-play does NOT ask the player to resolve
    // every candidate's cantrip (that produced a per-turn storm of phantom scry/reorder decisions);
    // the chooser fires only during the REAL TurnSolver::ApplyPlan, which runs outside enumeration.
    RevealLogPause _rlp_enum;
    PROF_INC(enumerate_calls);
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
        auto cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }

    // Hinata combo: cheap scan of the stamped per-Action ritual float (mirrors Solve), so the deep
    // search also enumerates the ritual->payoff combo. False for non-ritual decks -> byte-identical.
    bool any_ritual = false;
    for (const Action& ra : cands) { if (ra.ritual_float > 0) { any_ritual = true; break; } }
    // Same-turn mana-rock ramp scan (mirrors Solve). Inert without a non-creature rock.
    bool any_rock = false;
    for (const Action& ra : cands) { if (ra.rock_mana.Total() > 0) { any_rock = true; break; } }

    int m = static_cast<int>(cands.size());
    std::vector<TurnSolver::Plan> plans;

    // --- Base set: enumerate feasible action combinations ---
    //
    // A naive powerset over all action sources (for mask in [1, 2^m)) blows up
    // combinatorially when a hand holds many same-mana-value creatures while several
    // untapped Aether Vials share a charge count: each creature then yields one
    // CastFromHand plus one ActivateVial per same-charge Vial, so m (=|cands|) reaches
    // the dozens and the subset loop spins for minutes building plans the name-based
    // dedup below would only discard. Two facts make almost all of those subsets
    // redundant: (1) the actions that share a hand_index (a card's cast vs. its Vial
    // deploys) are mutually exclusive, and (2) deploying a creature through Vial A vs.
    // Vial B at the same charge is byte-identical after resolution — apply_vial taps
    // the first untapped matching Vial regardless of vial_bf_index — so a second
    // same-charge Vial adds CAPACITY, not a distinct plan.
    //
    // So instead of the action powerset we enumerate the PRODUCT of per-hand-card
    // choices {skip, cast, deploy-via-Vial} (all same-charge Vial deploys of one card
    // collapse to a single representative option), crossed with the independent
    // include/exclude choices for non-hand actions (graveyard retrace). Vial usage is
    // bounded by an aggregate per-charge capacity. This generates exactly the same set
    // of plan signatures as powerset-then-dedup, but in
    // O(prod(1+choices) * 2^independent) instead of O(2^m).

    // Per-charge Vial capacity = number of distinct untapped Vials available at each
    // charge (derived from the Vial actions' vial_bf_index, matching apply_vial which
    // taps a fresh matching Vial per deploy).
    std::vector<std::pair<int, int>> vial_capacity;   // (charge, count)
    auto capacity_for = [&](int charge) -> int
    {
        for (const std::pair<int, int>& vc : vial_capacity)
        {
            if (vc.first == charge) { return vc.second; }
        }
        return 0;
    };
    {
        std::vector<std::pair<int, int>> seen;   // (charge, vial_bf_index) already counted
        for (const Action& a : cands)
        {
            if (a.kind != Action::Kind::ActivateVial) { continue; }
            std::pair<int, int> key{ a.card_mv, a.vial_bf_index };
            bool already = false;
            for (const std::pair<int, int>& s : seen)
            {
                if (s == key) { already = true; break; }
            }
            if (already) { continue; }
            seen.push_back(key);
            bool found = false;
            for (std::pair<int, int>& vc : vial_capacity)
            {
                if (vc.first == a.card_mv) { ++vc.second; found = true; break; }
            }
            if (!found) { vial_capacity.push_back({ a.card_mv, 1 }); }
        }
    }

    // Group action indices: one mutually-exclusive option list per hand card
    // (its cast + a single representative Vial deploy), plus a flat list of
    // independent non-hand actions (graveyard retrace, hand_index < 0).
    std::vector<std::vector<int>> groups;            // per hand card: option cand indices
    std::vector<int>              group_hand_index;  // parallel: the card's hand_index
    std::vector<int>              independent;
    for (int j = 0; j < m; ++j)
    {
        if (cands[j].hand_index < 0) { independent.push_back(j); continue; }

        int gi = -1;
        for (int g = 0; g < static_cast<int>(groups.size()); ++g)
        {
            if (group_hand_index[g] == cands[j].hand_index) { gi = g; break; }
        }
        if (gi < 0)
        {
            groups.push_back({});
            group_hand_index.push_back(cands[j].hand_index);
            gi = static_cast<int>(groups.size()) - 1;
        }
        // Collapse all same-charge Vial deploys of one card to a single representative.
        if (cands[j].kind == Action::Kind::ActivateVial)
        {
            bool has_vial = false;
            for (int existing : groups[gi])
            {
                if (cands[existing].kind == Action::Kind::ActivateVial) { has_vial = true; break; }
            }
            if (has_vial) { continue; }
        }
        groups[gi].push_back(j);
    }

    // Breadth cap on a bloated combo-dig hand (shared with Solve). This enumerator feeds both the
    // multi-turn search and the per-turn leaf rollout (SolveWithLookahead), so capping it is what
    // attacks the rollout-bound no-win games. See CapGroupsBySituationalRank.
    CapGroupsBySituationalRank(state, cands, groups, group_hand_index);

    int num_groups = static_cast<int>(groups.size());
    int num_ind    = static_cast<int>(independent.size());

    bool have_colors[5];   // untapped-source colors -- state-only, computed once for all subsets
    ComputeAvailableColors(state, have_colors);

    // Evaluate one selected combination (a list of candidate indices) and, if
    // feasible, append the resulting plan. Mirrors the former per-mask body.
    auto eval_and_push = [&](const std::vector<int>& sel)
    {
        // Reject combinations whose Vial deploys exceed the per-charge capacity.
        for (int j : sel)
        {
            if (cands[j].kind != Action::Kind::ActivateVial) { continue; }
            int charge = cands[j].card_mv;
            int used   = 0;
            for (int k : sel)
            {
                if (cands[k].kind == Action::Kind::ActivateVial && cands[k].card_mv == charge)
                {
                    ++used;
                }
            }
            if (used > capacity_for(charge)) { return; }
        }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count    = 0;
        int noncreature_count  = 0;
        int direct_dmg         = 0;
        int total_eval         = 0;
        int self_damage        = 0;
        int vial_haste_atk     = 0;
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)

        for (int j : sel)
        {
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

        // Same-turn ramp credit. Ritual float (Reality Spasm / Irencrag) credited as wild; a mana
        // rock cast in THIS subset (Sol Ring -> {C}{C}) credited by its REAL produced colours, but
        // only once the board can already pay for the rocks themselves (a rock never funds its own
        // cost). The casts' own costs are already in `combined`, so this stays net/conservative.
        // Both inert -> byte-identical for decks without rituals or rocks.
        ManaPool eff = pool, eff_nc = pool_noncreature;
        bool credited = false;
        if (any_ritual)
        {
            int ritual_credit = 0;
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            if (ritual_credit > 0) { eff.wild += ritual_credit; eff_nc.wild += ritual_credit; credited = true; }
        }
        if (any_rock)
        {
            ManaPool rock_prod; ManaCost rock_costs; bool sel_rock = false;
            for (int j : sel)
            {
                if (cands[j].rock_mana.Total() <= 0) { continue; }
                rock_prod.AddPool(cands[j].rock_mana);
                const ManaCost& rc = cands[j].cost;
                rock_costs.white += rc.white; rock_costs.blue += rc.blue; rock_costs.black += rc.black;
                rock_costs.red   += rc.red;   rock_costs.green += rc.green;
                rock_costs.colorless += rc.colorless; rock_costs.generic += rc.generic;
                sel_rock = true;
            }
            if (sel_rock && pool.CanPay(rock_costs)) { eff.AddPool(rock_prod); eff_nc.AddPool(rock_prod); credited = true; }
        }
        if (credited)
        {
            if (!eff.CanPay(combined))                       { return; }
            if (!eff_nc.CanPay(noncreature_combined))        { return; }
        }
        else
        {
            if (!pool.CanPay(combined))                          { return; }
            if (!pool_noncreature.CanPay(noncreature_combined))  { return; }
        }
        if (sacrifice_count > total_lands)                   { return; }
        if (discard_lands_used > lands_in_hand)              { return; }
        // Accurate per-color payability (rejects wild-pool phantoms; see SubsetPayable).
        if (!SubsetPayable(have_colors, cands, sel))         { return; }

        // Irencrag "one more spell this turn": reject subsets casting > max_casts_after spells after
        // the restricting ritual (mirrors Solve::consider; keeps the commit-the-line enumerator legal).
        for (int j : sel)
        {
            if (cands[j].max_casts_after < 0) { continue; }
            const CardDefinition* rd = cands[j].def;
            const int r_rank = rd ? ResolveProvider(state).CastOrderRank(state, *rd) : 20;
            int after = 0;
            for (int k : sel)
            {
                if (k == j) { continue; }
                const CardDefinition* kd = cands[k].def;
                const int k_rank = kd ? ResolveProvider(state).CastOrderRank(state, *kd) : 20;
                if (k_rank > r_rank) { ++after; }
            }
            if (after > cands[j].max_casts_after) { return; }
        }

        if (self_damage >= ap.life) { return; }

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
        TurnSolver::Plan plan;
        plan.value          = total_eval;
        plan.wins_this_turn = wins;
        for (int j : sel) { plan.actions.push_back(cands[j]); }
        plans.push_back(std::move(plan));
    };

    // Odometer over per-card choices (0 = skip the card, v >= 1 selects
    // groups[g][v-1]), crossed with the 2^num_ind powerset of independent actions.
    // The empty combination (skip everything) is not a plan and is dropped.
    std::vector<int> choice(num_groups, 0);
    std::vector<int> sel;   // reused across subset iterations (clear keeps capacity, avoids per-combo alloc)
    bool done = false;
    while (!done)
    {
        for (int imask = 0; imask < (1 << num_ind); ++imask)
        {
            sel.clear();
            for (int g = 0; g < num_groups; ++g)
            {
                if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); }
            }
            for (int b = 0; b < num_ind; ++b)
            {
                if (imask & (1 << b)) { sel.push_back(independent[b]); }
            }
            if (!sel.empty()) { eval_and_push(sel); }
        }

        int g = 0;
        for (; g < num_groups; ++g)
        {
            ++choice[g];
            if (choice[g] <= static_cast<int>(groups[g].size())) { break; }
            choice[g] = 0;
        }
        if (g == num_groups) { done = true; }
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
    std::stable_sort(triggers.begin(), triggers.end(),
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
        const CardDefinition* draw_def =
            CardDatabase::Instance().LookupCached(ap.hand[draw.hand_index]);
        if (!draw_def || !ResolveProvider(state).ShouldStageSpectacleDraw(state, state.active_player_index, *draw_def)) { continue; }
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
    std::stable_sort(plans.begin(), plans.end(),
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
    // Human play keeps plans that differ ONLY in a sub-decision (tutor target / X / Ponder keep /
    // Soulfire count / land / fetch) as DISTINCT plans, so the player can choose among them.
    //
    // The autonomous dedup collapses them to one cast-name representative (the FIRST enumerated,
    // i.e. the tutor heuristic's best-first pick). NB this is NOT a correctness property -- it is an
    // efficiency shortcut that DELEGATES the sub-decision to the heuristic and never search-branches
    // over the alternatives. It is a real (heuristic-masked) limitation in the search too; it only
    // "works" there insofar as TutorCandidates' ordering is trusted. For human play that shortcut is
    // simply wrong -- the human IS the decision-maker -- so we keep every variant. Gated on
    // MTG_HUMAN_PLAY: the autonomous search and the MTG_UNPRUNED A/B stay byte-identical (the
    // shortcut, warts and all, is unchanged there -- widening the search is a separate question).
    static const bool s_human_play_sig = std::getenv("MTG_HUMAN_PLAY") != nullptr;
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
                case Action::Kind::PlayLand: break;  // never appears in plan.actions
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
        if (s_human_play_sig)
        {
            // Per-action sub-decisions, order-independent; plus the land/fetch the plan commits.
            std::vector<std::string> sub;
            for (const Action& act : p.actions)
            {
                if (!act.tutor_target.empty())   { sub.push_back("t" + act.card_name + ">" + act.tutor_target); }
                if (act.chosen_x > 0)            { sub.push_back("x" + act.card_name + "=" + std::to_string(act.chosen_x)); }
                if (act.ponder_keep >= 0)        { sub.push_back("p" + act.card_name + "=" + std::to_string(act.ponder_keep)); }
                if (act.soulfire_own_targets > 0){ sub.push_back("f" + act.card_name + "=" + std::to_string(act.soulfire_own_targets)); }
            }
            std::sort(sub.begin(), sub.end());
            for (const std::string& x : sub) { sig += '#'; sig += x; }
            if (p.land_decided)            { sig += "|land="  + p.land_to_play; }
            if (!p.fetch_target.empty())   { sig += "|fetch=" + p.fetch_target; }
        }
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

    // Branching diagnostics (off by default): attribute this call's odometer size + plan count to
    // the card driving the biggest option-group and to a coarse situation label.
    if (branchstats::Enabled())
    {
        double odo = 1.0;
        for (const std::vector<int>& gp : groups) { odo *= (1.0 + static_cast<double>(gp.size())); }
        odo *= static_cast<double>(1u << std::min(num_ind, 24));
        int max_opts = 0; std::string driver = "(casts<=1)";
        for (const std::vector<int>& gp : groups)
        {
            if (static_cast<int>(gp.size()) > max_opts)
            { max_opts = static_cast<int>(gp.size()); driver = cands[gp[0]].card_name; }
        }
        const int bf = static_cast<int>(state.battlefield.size());
        char situ[96];
        std::snprintf(situ, sizeof situ, "groups=%s board=%s hinata=%d",
            (num_groups <= 4 ? "0-4" : num_groups <= 8 ? "5-8" : num_groups <= 12 ? "9-12" : "13+"),
            (bf <= 6 ? "0-6" : bf <= 10 ? "7-10" : bf <= 15 ? "11-15" : "16+"),
            HinataInPlay(state) ? 1 : 0);
        branchstats::Record(driver, situ, odo,
                            static_cast<uint64_t>(plans.size()), static_cast<uint64_t>(deduped.size()));
    }

    // Cast-ordering search (C): expand each action set into the DISTINCT orderings of its
    // non-sacrifice hand casts, deduped by end-of-phase state. Off by default => return
    // the canonical-order sets unchanged (byte-identical). See OrderingSearchEnabled.
    if (!OrderingSearchEnabled()) { return deduped; }

    std::vector<TurnSolver::Plan> ordered;
    ordered.reserve(deduped.size());
    for (TurnSolver::Plan& p : deduped)
    {
        // Reorderable = non-sacrifice hand casts (where enabler/payload interactions live);
        // everything else (Vial / sacrifice-land / retrace) keeps its canonical bucket.
        std::vector<Action> reorder, fixed;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { reorder.push_back(a); }
            else { fixed.push_back(a); }
        }
        if (reorder.size() < 2) { ordered.push_back(std::move(p)); continue; }

        // Bound the work: permutations grow as k! (distinct end-states are far fewer, but we
        // still APPLY each tried ordering). Beyond the cap keep the canonical order only.
        long long perms = 1; bool too_many = false;
        for (size_t i = 2; i <= reorder.size(); ++i)
        { perms *= static_cast<long long>(i); if (perms > 120) { too_many = true; break; } }
        if (too_many) { ordered.push_back(std::move(p)); continue; }

        // Permute reorderable casts by NAME (next_permutation over a name-sorted index list
        // yields each distinct multiset ordering once -- identical copies don't multiply).
        std::vector<int> idx(reorder.size());
        for (size_t i = 0; i < idx.size(); ++i) { idx[i] = static_cast<int>(i); }
        auto by_name = [&](int x, int y) { return reorder[x].card_name < reorder[y].card_name; };
        std::sort(idx.begin(), idx.end(), by_name);

        std::unordered_set<TranspositionTable::Key, TranspositionTable::KeyHash> seen_states;
        do
        {
            TurnSolver::Plan cand = p;
            cand.actions.clear();
            for (int j : idx)            { cand.actions.push_back(reorder[j]); }
            for (const Action& a : fixed){ cand.actions.push_back(a); }
            cand.searched_order = true;

            // Apply this ordering on a copy; dedup by the resulting end-of-phase state.
            GameState copy = state;
            ApplyPlanDirect(copy, cand, is_pre_combat);
            if (seen_states.insert(BuildSimKey(copy, 0, 0, false)).second)
            {
                // Combat is order-independent, so inherit the base plan's combat-based
                // win; a reordering can only ADD direct damage (e.g. the rebuild), so also
                // mark a win if this ordering kills outright. Keeps winning orderings
                // sorted first (not cut under budget).
                cand.wins_this_turn = p.wins_this_turn || (copy.Opponent().life <= 0);
                ordered.push_back(std::move(cand));
            }
        } while (std::next_permutation(idx.begin(), idx.end(), by_name));
    }

    return ordered;
}

// Land-folded candidate enumeration: the land drop is searched alongside the spells.
//
// When a pre-combat land drop is available, for each DISTINCT playable land in hand
// (deduped by static effect signature so 4-ofs and mechanically-identical lands
// collapse to one representative) plus a DEFER option (play no land), we play that
// land on a copy, enumerate the spell subsets on the resulting board, and tag every
// plan with its land_to_play. A "play this land, cast nothing" baseline is always
// included so a turn may legally develop only its land. SolveWithLookahead runs this
// at every searched turn — in the real game AND in the rollout — so the land choice is
// modelled identically end to end (no greedy-rollout / searched-reality divergence,
// which otherwise makes searched land choices play out worse than the greedy heuristic).
// Human-play only: make Land's Edge a PICKABLE line action. When the active player controls a
// Land's Edge and holds lands, fan out a DiscardToLandsEdge(N) variant of every base plan -- and a
// STANDALONE "pass + Land's Edge" line if there are no base plans (the post-Treasure-Hunt
// breakpoint: land drop used, no mana left, hand full of drawn lands). Autonomous search auto-fires
// Land's Edge in ApplyPlanDirect (suppressed under s_human_play), so without this the human could
// cast a Land's-Edge deck but never fire it. N is bounded at lethal (over-fire only pings a dead
// opponent). Deterministic -> plan indices stay stable across CheckLine validation and the
// --choices stateless replay. Gated on MTG_HUMAN_PLAY: a no-op (byte-identical) for every
// autonomous goldfish/search run. Applied on BOTH EnumeratePlansWithLand return paths.
static void AppendHumanPlayLandsEdgePlans(const GameState& state, std::vector<TurnSolver::Plan>& all)
{
    static const bool s_human_play_enum = std::getenv("MTG_HUMAN_PLAY") != nullptr;
    if (!s_human_play_enum) { return; }

    const Player& ap = state.ActivePlayer();
    int le_rate = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.discard_land_damage > 0)
        { le_rate = std::max(le_rate, d->params.discard_land_damage); }
    }
    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d ? d->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }
    if (le_rate <= 0 || lands_in_hand <= 0) { return; }

    const int opp_life = state.players[1 - state.active_player_index].life;
    const int lethal_lands = (opp_life > 0) ? (opp_life + le_rate - 1) / le_rate : 0;
    // No base plan to fan from (out of other plays) -> seed a "pass" base so Land's Edge is still
    // offered as a standalone line; otherwise the chooser sees zero plans and the turn advances.
    if (all.empty()) { all.push_back(TurnSolver::Plan{}); }
    const size_t base_count = all.size();
    for (size_t i = 0; i < base_count; ++i)
    {
        const bool plays_land = !all[i].land_to_play.empty();   // a played/fetched land leaves hand
        const int  avail = lands_in_hand - (plays_land ? 1 : 0);
        const int  maxN  = std::min(avail, std::max(0, lethal_lands));
        for (int n = 1; n <= maxN; ++n)
        {
            TurnSolver::Plan v = all[i];
            Action le;
            le.kind          = Action::Kind::DiscardToLandsEdge;
            le.card_name     = "Land's Edge";
            le.discard_lands = n;
            v.actions.push_back(std::move(le));
            v.value          = all[i].value + n * le_rate;
            v.wins_this_turn = all[i].wins_this_turn || (opp_life - n * le_rate <= 0);
            all.push_back(std::move(v));
        }
    }
}

static std::vector<TurnSolver::Plan> EnumeratePlansWithLand(const GameState& state,
                                                            bool is_pre_combat)
{
    RevealLogPause _rlp_enum;   // candidate scoring is hypothetical -- see EnumeratePlans
    const Player& ap = state.ActivePlayer();
    bool drop_available = is_pre_combat
                       && ap.lands_played_this_turn < ap.LandDropsAvailable();

    if (!drop_available)
    {
        // Nothing to decide; mark land as resolved so ApplyPlanDirect does not fall
        // back to greedy land play for these searched plans. Human play: still offer Land's Edge
        // as a standalone line here (this is the post-Treasure-Hunt breakpoint, drop already used).
        std::vector<TurnSolver::Plan> plans = EnumeratePlans(state, is_pre_combat);
        AppendHumanPlayLandsEdgePlans(state, plans);
        for (TurnSolver::Plan& p : plans) { p.land_decided = true; }
        return plans;
    }

    // Static effect signature: two lands with the same signature are interchangeable
    // for the search (identical mana, ETB, and abilities), so only one need be tried.
    auto land_sig = [](const CardParams& pp) -> std::string
    {
        std::string s;
        std::vector<int> prod;
        for (Color c : pp.produces) { prod.push_back(static_cast<int>(c)); }
        std::sort(prod.begin(), prod.end());
        for (int c : prod) { s += std::to_string(c); s += ','; }
        s += "n" + std::to_string(pp.produces_amount);
        s += pp.enters_tapped ? "T" : "U";
        s += "l" + std::to_string(pp.etb_pay_life_to_untap);
        for (const std::string& sub : pp.etb_untap_reveal_subtypes) { s += "r" + sub; }
        s += "s" + std::to_string(pp.etb_scry);
        s += "d" + std::to_string(pp.enters_tapped_with_depletion);
        s += pp.no_max_hand_size      ? "H" : "-";
        s += pp.is_filter             ? "F" : "-";
        s += pp.cycling_cost          ? "C" : "-";
        s += pp.sacrifice_draw_cost   ? "D" : "-";
        // creature-only mana (Ancient Ziggurat) is NOT interchangeable with an unrestricted
        // any-colour land: deduping the two loses the unrestricted land's non-creature lines
        // (and could force the strictly-worse Ziggurat as the sole representative). Distinguish.
        s += pp.creature_mana_only    ? "M" : "-";
        // Fetchlands with different target colours are NOT interchangeable; distinguish
        // them. Empty for ordinary lands -> sig unchanged (other decks byte-identical).
        for (const std::string& ft : pp.fetch_land_types) { s += "f" + ft; }
        return s;
    };

    // Human play (the play GUI) enumerates one plan per distinct land NAME rather than per static
    // signature: the player chose a SPECIFIC land and expects that exact card played (not a
    // signature-equivalent representative), and a different-but-equivalent land must never read as
    // a reject. Gated on MTG_HUMAN_PLAY -> byte-identical for every autonomous goldfish/search run,
    // which keeps deduping by signature for enumeration economy.
    static const bool s_human_play_lands = std::getenv("MTG_HUMAN_PLAY") != nullptr;
    std::vector<std::string>        land_names;   // representatives, in hand order
    std::unordered_set<std::string> seen_key;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || !def->card.IsLand()) { continue; }
        const std::string key = s_human_play_lands ? c.m_name : land_sig(def->params);
        if (seen_key.insert(key).second) { land_names.push_back(c.m_name); }
    }

    // Greedy land the heuristic (AIEngine::TryPlayLand) would play from this hand.
    // Used ONLY as the last-resort ordering tiebreak below: when the search is
    // genuinely indifferent between land lines (equal win-turn AND equal first-turn
    // value), we default to the proven heuristic rather than to hand order. The
    // clairvoyant rollout often rates two land choices identically at the horizon,
    // and letting an arbitrary order decide picks a land that plays out marginally
    // worse than greedy in the realized game (the small fold-vs-greedy regressions).
    // Mirrors TryPlayLand's TH pre-pass + four-pass (untapped/tapped x multi/any).
    auto greedy_land_name = [&]() -> std::string
    {
        bool has_draw_until_nonland = false;
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && d->tmpl == CardTemplate::DrawUntilNonland) { has_draw_until_nonland = true; break; }
        }
        if (has_draw_until_nonland)
        {
            for (const Card& c : ap.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (d && d->card.IsLand() && d->params.no_max_hand_size) { return c.m_name; }
            }
        }
        for (int pass = 0; pass < 4; ++pass)
        {
            bool want_untapped = (pass < 2);
            bool want_multi    = (pass == 0 || pass == 2);
            for (const Card& c : ap.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (!d || !d->card.IsLand()) { continue; }
                bool is_tapped = d->params.enters_tapped;
                bool is_multi  = d->params.produces.size() > 1;
                if (want_untapped == is_tapped) { continue; }
                if (want_multi && !is_multi)    { continue; }
                return c.m_name;
            }
        }
        return std::string();
    }();

    std::vector<TurnSolver::Plan> all;

    auto add_for_land = [&](const std::string& land_name, const std::string& fetch_target)
    {
        PROF_INC(gamestate_copies);
        GameState copy = state;
        if (!land_name.empty() && !PlayLandByName(copy, land_name, fetch_target)) { return; }

        // "Play this land, cast nothing" baseline (neutral value 0).
        TurnSolver::Plan idle;
        idle.value        = 0;
        idle.land_decided = true;
        idle.land_to_play = land_name;
        idle.fetch_target = fetch_target;
        all.push_back(std::move(idle));

        std::vector<TurnSolver::Plan> plans = EnumeratePlans(copy, is_pre_combat);
        for (TurnSolver::Plan& p : plans)
        {
            p.land_decided = true;
            p.land_to_play = land_name;
            p.fetch_target = fetch_target;
            all.push_back(std::move(p));
        }
    };

    // Pass 2 of the real-fetch model: a fetchland whose FetchCandidates returns MORE THAN
    // ONE legal target is a genuine search choice (which colours to commit to). Emit one
    // land-variant per candidate so the rollout picks the best, capped at the heuristic's
    // top few (it orders best-first; lower-ranked targets are strictly worse on colour and
    // a basic always ranks last, so the cap drops only clearly-inferior fetches). A single
    // candidate (or none) plays the heuristic top pick with no extra branching (Pass 1).
    // The cap is provider-owned policy (DecisionProvider::FetchSearchCap, audit A2).
    const int kMaxFetchSearchTargets = ResolveProvider(state).FetchSearchCap();
    for (const std::string& ln : land_names)
    {
        const CardDefinition* ld = CardDatabase::Instance().Lookup(ln);
        if (ld && !ld->params.fetch_land_types.empty())
        {
            std::vector<std::string> cands =
                ResolveProvider(state).FetchCandidates(state, state.active_player_index, ld->params);
            if (cands.size() > 1)
            {
                // Unpruned audit: search EVERY fetch candidate (no cap), so a costly
                // fetch-target heuristic can be detected. See DecisionUnpruned.
                int cap = DecisionUnpruned() ? static_cast<int>(cands.size())
                                             : kMaxFetchSearchTargets;
                int n = std::min(static_cast<int>(cands.size()), cap);
                for (int i = 0; i < n; ++i) { add_for_land(ln, cands[i]); }
                continue;
            }
        }
        add_for_land(ln, "");   // ordinary land, or fetchland with <=1 candidate (heuristic)
    }
    add_for_land("", "");   // defer: play no land this turn

    // Land's Edge activation as a PICKABLE plan action (human play only) -- see the helper. Applied
    // here for the land-drop-available path; the !drop_available early-return applies it too.
    AppendHumanPlayLandsEdgePlans(state, all);

    // A tapped land that is a fine early play (a dual/tri/scry/surveil/depletion/Karoo tap
    // land played on a turn you don't need its mana) vs one you'd rather NOT play as a normal
    // tapped land. EXCLUDED: cycling lands -- kept in hand for their from-hand cycling utility
    // (draw a card when flooding), not played as plain tap lands. KEPT (strong early when you
    // don't need the mana): scry/surveil duals (Thundering Falls, Temple of Epiphany), plain
    // tapped duals, depletion bursts (Saprazzan Skerry), and Karoo bounce lands (Izzet
    // Boilerworks) -- a Karoo SHOULD take a tapland turn; its only issue is play TIMING (it must
    // be played last so it bounces an already-tapped land), fixed separately.
    //
    // COLOUR-COVERAGE gate (field-aware): a tapped land is a fine early play UNLESS playing it
    // wastes the drop on a colour we don't need while a colour we DO need goes uncovered. The
    // discriminator is UNCOVERED need = (colours a non-land hand card requires) minus (colours the
    // lands we already control on the battlefield can make). Two refinements over a naive
    // colour-need check, both proven on logs/tiebreak_changed:
    //   * Field-aware: if the field already produces every colour our hand demands, colour is MOOT
    //     -- fall back to plain tapped-first tempo (== colour-blind). (gi=68: T1 Saprazzan Skerry
    //     already gives the U that Treasure Hunt needs, so at T2 we must NOT prefer the on-colour
    //     surveil land over a vanilla tapland just for redundant blue.)
    //   * Demand from the HAND, coverage from the FIELD only (not other hand lands): an empty field
    //     leaves the need uncovered, so an off-colour tapland is still correctly demoted. (gi=271:
    //     T1 field empty, Treasure Hunt needs U, Sandstone Needle makes only R -> demote; play a
    //     U source instead.) This is the "somewhere in between colour-blind and strict-colour-need"
    //     rule. MTG_COLOR_BLIND_TIEBREAK restores the old colour-blind rule for A/B.
    static const bool s_color_blind_tiebreak = std::getenv("MTG_COLOR_BLIND_TIEBREAK") != nullptr;
    bool needed[6] = { false, false, false, false, false, false };
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        const ManaCost& mc = d->card.m_mana_cost;   // authoritative cost (hand Card's may be unset)
        if (mc.white > 0) { needed[static_cast<int>(Color::White)] = true; }
        if (mc.blue  > 0) { needed[static_cast<int>(Color::Blue)]  = true; }
        if (mc.black > 0) { needed[static_cast<int>(Color::Black)] = true; }
        if (mc.red   > 0) { needed[static_cast<int>(Color::Red)]   = true; }
        if (mc.green > 0) { needed[static_cast<int>(Color::Green)] = true; }
    }
    // Colours the lands we already control can make (tapped or not -- a tapped land still covers
    // its colour on later turns, which is what "do we already have this colour" asks).
    bool have[6] = { false, false, false, false, false, false };
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->card.IsLand()) { continue; }
        for (Color col : EffectiveProduces(state, state.active_player_index, *d))
        {
            have[static_cast<int>(col)] = true;
        }
    }
    bool any_uncovered = false;
    bool uncovered[6];
    for (int i = 0; i < 6; ++i)
    {
        uncovered[i] = needed[i] && !have[i];
        if (i != static_cast<int>(Color::Colorless) && uncovered[i]) { any_uncovered = true; }
    }
    auto land_good_early_tapped = [&](const std::string& name) -> bool
    {
        if (name.empty()) { return false; }
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (!d || !d->params.enters_tapped) { return false; }
        if (d->params.cycling_cost.has_value()) { return false; }   // hold to cycle for a card
        if (s_color_blind_tiebreak) { return true; }                // legacy colour-blind A/B
        if (!any_uncovered) { return true; }                        // colour moot -> tempo governs
        for (Color col : d->params.produces)
        {
            if (col != Color::Colorless && uncovered[static_cast<int>(col)]) { return true; }
        }
        return false;   // off-colour: wastes the drop while a needed colour stays uncovered
    };
    static const bool s_develop_tiebreak = std::getenv("MTG_NO_DEVELOP_TIEBREAK") == nullptr;

    // Winning plans first, then by value — matches EnumeratePlans' ordering so the
    // win-this-turn shortcut in SolveWithLookahead still returns the best winning plan.
    // Final tiebreak (only fires on equal wins AND equal value, i.e. true search
    // indifference): order the greedy land's plans first. SolveWithLookahead replaces
    // its incumbent only on a STRICTLY better rollout win-turn or STRICTLY higher
    // value, so among equal-value/equal-win-turn candidates it keeps the first one —
    // this ordering makes that first one the greedy land, defaulting indifferent ties
    // to the proven heuristic without overriding any strictly-better searched line.
    std::stable_sort(all.begin(), all.end(),
        [&](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn > b.wins_this_turn; }
            if (a.value != b.value) { return a.value > b.value; }
            if (s_develop_tiebreak)
            {
                const bool a_has = !a.land_to_play.empty();
                const bool b_has = !b.land_to_play.empty();
                if (a_has != b_has) { return a_has > b_has; }                    // (1) develop
                if (a_has && b_has)
                {
                    const bool a_tap = land_good_early_tapped(a.land_to_play);
                    const bool b_tap = land_good_early_tapped(b.land_to_play);
                    if (a_tap != b_tap) { return a_tap > b_tap; }                // (2) tapped-first
                }
            }
            bool a_greedy = (a.land_to_play == greedy_land_name);
            bool b_greedy = (b.land_to_play == greedy_land_name);
            return a_greedy > b_greedy;
        });

    TRACE("plans", "T%d EnumeratePlansWithLand -> %zu plans (lands=%zu, hand=%zu)",
          state.turn_number, all.size(), land_names.size(), ap.hand.size());
    return all;
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

    // With search-shuffle ON the library order is NO LONGER a deterministic function of
    // its size (a fetch/tutor reshuffles it), so the cheap (size + front) library digest
    // below would let two differently-ordered libraries share a memo entry -- a stale TT/
    // FSLineCache hit yielding a wrong rollout. Fold search_count (it seeds the NEXT
    // shuffle, so it distinguishes states with identical current order but different
    // futures) and the FULL ordered library (below) to make the key exact. OFF (default):
    // not folded => byte-identical keys, and the clairvoyant "size => content" assumption
    // still holds because nothing shuffles mid-search.
    const bool shuffle_keys = SearchShuffleEnabled();
    if (shuffle_keys) { Fold(k, 0x5ADF); Fold(k, state.search_count); }

    for (int pi = 0; pi < 2; ++pi)
    {
        const Player& p = state.players[pi];
        Fold(k, 0x9100 + static_cast<uint64_t>(pi)); // section tag: player pi
        Fold(k, static_cast<uint64_t>(p.life));
        Fold(k, static_cast<uint64_t>(p.lands_played_this_turn));
        Fold(k, static_cast<uint64_t>(p.bonus_land_drops_this_turn));
        Fold(k, static_cast<uint64_t>(p.library.size()));
        if (!p.library.empty()) { Fold(k, p.library.front().m_name_hash); }
        // Full ordered library when shuffle can reorder it (see above). Skipped when OFF.
        if (shuffle_keys) { for (const Card& c : p.library) { Fold(k, c.m_name_hash); } }

        Fold(k, 0x4A00 + static_cast<uint64_t>(pi)); // sub-section: hand (ordered)
        Fold(k, static_cast<uint64_t>(p.hand.size()));
        for (const Card& c : p.hand)
        {
            Fold(k, c.m_name_hash);
            // A staged card's expiry changes when it can still be played, so two hands
            // with identical names but different staged expiries are different rollout
            // states. Folded ONLY when staged, so non-staging decks keep their exact
            // prior key (byte-identical results).
            if (c.m_is_staged) { Fold(k, static_cast<uint64_t>(c.m_staged_expiry)); }
        }

        Fold(k, 0x57A6 + static_cast<uint64_t>(pi)); // sub-section: staged cards
        Fold(k, static_cast<uint64_t>(p.staged_cards.size()));
        for (const StagedCard& sc : p.staged_cards)
        {
            Fold(k, sc.card.m_name_hash);
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
                gy_acc += c.m_name_hash;  // cached std::hash(m_name)
                const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
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
        Fold(k, perm.card.m_name_hash);
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
    const bool trace_pl = g_trace_arm;   // only the outermost diagnostic playout prints
    g_trace_arm = false;
    while (state.turn_number <= max_turns)
    {
        // Branch-and-bound: a line that hasn't won by cutoff_turn cannot beat the
        // incumbent best win turn, so abandon it (win turn only grows from here).
        // Abort only AFTER cutoff_turn so a win exactly on cutoff_turn still
        // registers for the value tiebreak.
        if (state.turn_number > cutoff_turn) { return max_turns + 1; }

        // Count one work unit per simulated turn-step. The rollout normally never self
        // truncates on the budget — it only consumes; the top-level decision decides when
        // to stop adding more rollouts. EXCEPTION: the mid-pass OVERRUN ceiling (armed by
        // FullSearchLine). A pathological no-early-win decision spawns a huge number of these
        // deep leaf rollouts; once the pass has blown kOverrunBeta x the whole budget, bail
        // out here too (return no-win) so the leaf can't run unbounded. Overrun() is false
        // unless armed (m_overrun_limit==0 on the baseline path / normal decisions), so this
        // is byte-identical for every non-pathological rollout.
        if (budget) { budget->Consume(1); }
        if (budget && budget->Overrun()) { return max_turns + 1; }

        // Expire staged (Light Up the Stage) cards whose play window has passed,
        // mirroring AIEngine::TakeTurn's expiry check (CR 406). Without this the
        // rollout would keep casting cards the real game has already lost. The hand
        // only ever holds staged cards for staging decks, so this is a no-op (and
        // byte-identical) for every other deck.
        {
            Player& rp = state.ActivePlayer();
            rp.hand.erase(std::remove_if(rp.hand.begin(), rp.hand.end(),
                [&](const Card& c)
                {
                    return c.m_is_staged && c.m_staged_expiry < state.turn_number;
                }), rp.hand.end());
        }

        if (trace_pl)
        {
            std::cerr << "  [pl] >>> turn=" << state.turn_number << " hand_before=[";
            for (const Card& c : state.ActivePlayer().hand)
            { std::cerr << c.m_name << (c.m_is_staged ? "*" : "") << "; "; }
            std::cerr << "] lib_top=";
            const Player& tp = state.ActivePlayer();
            std::cerr << (tp.library.empty() ? std::string("(none)") : tp.library.front().m_name.str()) << "\n";
        }

        // Pre-combat main: pick and apply plan (includes Vial activations), then animate + tokens
        TurnSolver::Plan pre_plan = TurnSolver::SolveWithLookahead(
            state, true, depth, max_turns, budget, false, second_main, tt);
        int life_before_pl = state.Opponent().life;
        ApplyPlanDirect(state, pre_plan, true);
        SimulateAnimateLands(state);
        SimulateTapTokens(state);

        // Combat
        SimulateCombat(state);
        if (trace_pl)
        {
            std::cerr << "  [pl] turn=" << state.turn_number
                      << " opp " << life_before_pl << "->" << state.Opponent().life
                      << "  " << PlanDesc(pre_plan) << "  hand_after=[";
            for (const Card& c : state.ActivePlayer().hand)
            { std::cerr << c.m_name << (c.m_is_staged ? "*" : "") << "; "; }
            std::cerr << "]\n";
        }
        if (state.Opponent().life <= 0)
        {
            if (trace_pl) { std::cerr << "  [pl] WIN at turn " << state.turn_number << "\n"; }
            return state.turn_number;
        }

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

        // End of turn + start of next. The next turn's land drop is searched as part
        // of that turn's plan (folded into SolveWithLookahead / played by
        // ApplyPlanDirect), so no greedy land play happens here.
        if (!SimulateEndAndStartNextTurn(state)) { return max_turns + 1; }
    }
    return max_turns + 1;
}

// Memoizing wrapper around SimulateToEndImpl. On a cache hit the rollout is
// skipped entirely (and no budget is consumed — avoided recompute is the point).
// Only REAL win turns (<= max_turns) are cached: they are exact and cutoff-
// independent, whereas a max_turns+1 result may be a branch-and-bound abort
// rather than a genuine no-win, so it is never stored. See TranspositionTable.h.
// Takes the rollout's starting state by rvalue reference: the sole caller passes a
// throwaway scratch copy it no longer needs, so binding (and, on a miss, mutating)
// it in place avoids a full GameState deep clone of the whole state on every call —
// previously the by-value parameter cloned ~60 cards even when the very next line
// returned a transposition-table hit. The TT key is read before any mutation, so
// the result is byte-identical to the by-value version.
static int SimulateToEnd(GameState&& state, int depth, int max_turns,
                         SearchBudget* budget, int cutoff_turn,
                         bool second_main, TranspositionTable* tt)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    TranspositionTable::Key key;
    if (tt != nullptr)
    {
        key = BuildSimKey(state, depth, max_turns, second_main);
        PROF_INC(tt_lookups);
        const int* cached = tt->Lookup(key);
        if (cached != nullptr) { PROF_INC(tt_hits); return *cached; }
    }

    int result = SimulateToEndImpl(state, depth, max_turns, budget, cutoff_turn, second_main, tt);

    if (tt != nullptr && result <= max_turns) { tt->Store(key, result); }
    return result;
}

// ---- Full-depth search (experimental, MTG_FULL_DEPTH) -------------------------
//
// "depth N" here means: fully search N COMPLETE turns (pre-combat main + combat +
// optional second main), branching over EVERY plan at each phase, then estimate
// the tail with a greedy rollout. Objective = earliest win turn, with
// branch-and-bound: a this-turn win is the hard floor; any branch that cannot
// beat the running best is pruned (`cutoff`). Contrast SolveWithLookahead, which
// reduces future-turn fidelity (shallower rollout + greedy second main).

// Interior-node memo for the full-depth search: maps a pre-combat-main state
// (+ remaining search depth, folded into the key) to the optimal SearchLine from
// it. Different opening sequences that transpose to the same later board reuse one
// another's result instead of re-searching the whole subtree -- the big cost the
// leaf SimulateToEnd table does NOT touch. Like that table it is per-FullSearchLine
// scope (one fixed root library, so library size uniquely identifies remaining
// content -- the cached line's draw-dependent breakpoint_actions stay valid) and
// caches ONLY genuine wins (win_turn <= max_turns): a winning line is cutoff-
// independent (pruning never removes a strictly-earlier win, and selection replaces
// only on strict improvement), whereas a no-win may be a branch-and-bound abort.
using FSLineCache = std::unordered_map<TranspositionTable::Key, TurnSolver::SearchLine,
                                       TranspositionTable::KeyHash>;

static TurnSolver::SearchLine FSLineWin(const GameState& state, int depth, int max_turns,
                                        int cutoff, bool second_main, TranspositionTable* tt,
                                        FSLineCache* lc, SearchBudget* budget);

// Expire staged (Light Up the Stage) cards whose play window has passed, mirroring
// SimulateToEndImpl's top-of-turn expiry and AIEngine::TakeTurn's merge skip (CR
// 406). Without this the full-depth search keeps casting cards the real game has
// already lost, over-valuing deferral and picking a slower line. No-op for decks
// that never stage cards (hand never holds m_is_staged cards).
static void ExpireStagedCards(GameState& state)
{
    Player& rp = state.ActivePlayer();
    rp.hand.erase(std::remove_if(rp.hand.begin(), rp.hand.end(),
        [&](const Card& c) { return c.m_is_staged && c.m_staged_expiry < state.turn_number; }),
        rp.hand.end());
}

// `state` is positioned just AFTER this turn's combat, opponent still alive.
// Returns the best line (min win turn) for the optional second main this turn plus
// `depth` further complete turns. The returned line is prefixed with the chosen
// second-main phase (when second_main) and continues with the recursed turns.
// `cutoff` is the incumbent best (a line that cannot win by it is abandoned).
static TurnSolver::SearchLine FSLineTail(const GameState& state, int depth, int max_turns,
                                         int cutoff, bool second_main, TranspositionTable* tt,
                                         FSLineCache* lc, SearchBudget* budget)
{
    // Mid-pass overrun guard (see FSLineWin): abort the runaway pass.
    if (budget && budget->Overrun()) { return { max_turns + 1, {} }; }
    if (second_main)
    {
        std::vector<TurnSolver::Plan> post = EnumeratePlans(state, false);
        // Always allow casting nothing in the second main and just advancing the
        // turn. EnumeratePlans returns an empty vector when no post-combat play is
        // castable (e.g. the pre-combat main tapped out), so without this the loop
        // below would never run and the whole line would be (wrongly) scored as a
        // no-win — making any tap-out play look strictly worse than idling. The
        // baseline rollout always advances past an empty second main; mirror that.
        post.push_back(TurnSolver::Plan{});
        MoveOrderPlans(post);   // lethal-looking / higher-value second mains first -> earlier cutoff
        // NOTE: we do NOT shortcut on the projected `wins_this_turn` flag here. That
        // projection (pending_atk + direct_dmg >= opp life) can over-count what the
        // actual ApplyPlanDirect + SimulateCombat deals, and trusting it would commit
        // a phantom win turn the replayed line never realises. The loop below decides
        // lethality by actually simulating, so the committed line's win turn always
        // matches replaying it. (Baseline SolveWithLookahead can trust the projection
        // because it re-decides every turn; commit-the-line locks the line in.)
        TurnSolver::SearchLine best;
        best.win_turn = max_turns + 1;
        for (const TurnSolver::Plan& q : post)
        {
            if (budget) { budget->Consume(1); }   // one interior node (plan applied)
            GameState s2 = state;
            std::vector<Action> bp;
            ApplyPlanDirect(s2, q, false, &bp);
            // Self-lethal second main (Eidolon on-cast self-damage) -> we die to the
            // triggers before the spell resolves; not a viable line. See FSLineWin.
            if (s2.ActivePlayer().life <= 0) { continue; }
            if (s2.Opponent().life <= 0)
            {
                TurnSolver::Plan q_rec = q;
                q_rec.breakpoint_actions = std::move(bp);
                return { state.turn_number, { { false, std::move(q_rec) } } };
            }
            if (!SimulateEndAndStartNextTurn(s2)) { continue; }
            ExpireStagedCards(s2);
            TurnSolver::SearchLine sub =
                FSLineWin(s2, depth, max_turns, std::min(cutoff, best.win_turn), second_main, tt, lc, budget);
            if (sub.win_turn < best.win_turn)
            {
                best.win_turn = sub.win_turn;
                best.phases.clear();
                TurnSolver::Plan q_rec = q;
                q_rec.breakpoint_actions = std::move(bp);
                best.phases.push_back({ false, std::move(q_rec) });
                best.phases.insert(best.phases.end(), sub.phases.begin(), sub.phases.end());

                // Stop at the first VERIFIED win (within horizon) -- the pass minimum.
                // Same reasoning as FSLineWin; the second-main FSLineWin runs at turn+1
                // with `depth` more turns, so its horizon edge is state.turn_number+depth.
                if (sub.win_turn <= state.turn_number + depth)
                {
                    return best;
                }
            }
        }
        return best;
    }

    GameState s = state;
    if (!SimulateEndAndStartNextTurn(s)) { return { max_turns + 1, {} }; }
    ExpireStagedCards(s);
    return FSLineWin(s, depth, max_turns, cutoff, second_main, tt, lc, budget);
}

// `state` is positioned at the START of the active player's pre-combat main (land
// not yet played; EnumeratePlansWithLand folds the land choice). Returns the best
// line (min win turn) fully searching `depth` complete turns from here, prefixed
// with the chosen pre-combat phase.
static TurnSolver::SearchLine FSLineWin(const GameState& state, int depth, int max_turns,
                                        int cutoff, bool second_main, TranspositionTable* tt,
                                        FSLineCache* lc, SearchBudget* budget)
{
    if (state.turn_number > max_turns) { return { max_turns + 1, {} }; }
    if (state.turn_number > cutoff)    { return { max_turns + 1, {} }; }  // can't beat incumbent
    // Mid-pass overrun: this pass has blown far past its budget estimate; bail out with a
    // no-win so FullSearchLine rolls back to the last completed pass (see SetOverrunLimit).
    if (budget && budget->Overrun())   { return { max_turns + 1, {} }; }
    if (depth <= 0)
    {
        // Tail estimate beyond the horizon: roll out to game end at s_fd_leaf_depth
        // fidelity (default 1 = a 1-ply lookahead; see s_fd_leaf_depth), no committed
        // plays (the caller re-searches once it exhausts the committed line). The
        // rollout only CONSUMES the budget (enforce_budget is false inside), so it
        // never truncates -- the start gate alone reads the budget, between passes.
        GameState leaf = state;
        int w = SimulateToEnd(std::move(leaf), s_fd_leaf_depth, max_turns, budget, cutoff, second_main, tt);
        return { w, {} };
    }

    // Interior-node memo: a transposed re-entry at this (state, depth) returns the
    // already-computed optimal line. depth is folded into the key, so different
    // remaining depths never collide. See FSLineCache.
    TranspositionTable::Key key;
    if (lc != nullptr)
    {
        key = BuildSimKey(state, depth, max_turns, second_main);
        FSLineCache::const_iterator it = lc->find(key);
        if (it != lc->end()) { return it->second; }
    }

    // No projected-`wins_this_turn` shortcut: lethality is decided by actually
    // simulating each plan below, so the committed line's win turn always matches
    // replaying it (the projection can over-count vs ApplyPlanDirect+SimulateCombat).
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);
    MoveOrderPlans(pre);   // lethal-looking / higher-value plans first -> earlier B&B cutoff

    TurnSolver::SearchLine best;
    best.win_turn = max_turns + 1;
    for (const TurnSolver::Plan& p : pre)
    {
        if (budget) { budget->Consume(1); }   // one interior node (plan applied)
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);
        // A plan that kills the active player via its own on-cast triggers (Eidolon of
        // the Great Revel) or self-damage cannot win: those triggers go on top of the
        // spell and resolve BEFORE it (CR 603.3), so we die to them before our spell or
        // combat deals any damage. Skip the line entirely -- mirrors the baseline plan
        // guard (`self_damage >= ap.life`) so commit-the-line never commits a suicide
        // (burn gi=492: two Eidolons + an extra Goblin Guide = 8 self-damage at 6 life).
        if (s.ActivePlayer().life <= 0) { continue; }
        SimulateAnimateLands(s);
        SimulateTapTokens(s);
        SimulateCombat(s);
        if (s.Opponent().life <= 0)  // win this turn -> floor
        {
            TurnSolver::Plan p_rec = p;
            p_rec.breakpoint_actions = std::move(bp);
            TurnSolver::SearchLine win = { state.turn_number, { { true, std::move(p_rec) } } };
            // A this-turn win is the earliest possible from here, so it is the final
            // optimal line for this node -- cache it (cutoff-independent).
            if (lc != nullptr) { lc->emplace(key, win); }
            return win;
        }

        TurnSolver::SearchLine tail =
            FSLineTail(s, depth - 1, max_turns, std::min(cutoff, best.win_turn), second_main, tt, lc, budget);
        if (tail.win_turn < best.win_turn)
        {
            best.win_turn = tail.win_turn;
            best.phases.clear();
            TurnSolver::Plan p_rec = p;
            p_rec.breakpoint_actions = std::move(bp);
            best.phases.push_back({ true, std::move(p_rec) });
            best.phases.insert(best.phases.end(), tail.phases.begin(), tail.phases.end());

            // Stop at the first VERIFIED win (within this node's horizon, found by real
            // simulation -- not the greedy leaf). Under the iterative-deepening caller
            // (FullSearchLine) a pass runs only after every shallower pass found no win,
            // and every node in a pass shares the same horizon edge, so any in-horizon
            // win is at that edge = the global minimum. Hence the FIRST one found is
            // optimal; no later plan can beat it (only tie), so commit and return. This
            // is the sound, general form of the old `best<=turn+1` sibling break, which
            // assumed lethal plans sort first and so skipped a later this-turn kill (the
            // slivers regression). Greedy-tail estimates (beyond the horizon) are NOT
            // mutually tied, so they fall through to keep min-tracking. NOTE: this
            // couples FSLineWin's correctness to that calling convention -- it is not a
            // standalone earliest-win finder.
            if (tail.win_turn <= state.turn_number + depth - 1)
            {
                if (lc != nullptr) { lc->emplace(key, best); }
                return best;
            }
        }
    }

    // Cache only a genuine win; a no-win (best.win_turn > max_turns) may be a
    // cutoff abort rather than a true dead end, so it is never stored (mirrors
    // SimulateToEnd / the leaf table).
    if (lc != nullptr && best.win_turn <= max_turns) { lc->emplace(key, best); }
    return best;
}

// Estimate-and-skip tuning (deterministic budget), shared by FullSearchLine's
// iterative-deepening start gate and SolveWithLookahead. See
// project-deterministic-budget.
namespace
{
    // Start gate: begin pass k only if its estimated cost <= alpha * remaining
    // budget; a little over (>1.0) is allowed since the overrun guard backs it up.
    constexpr double kStartGateAlpha = 1.10;
    // Bootstrap growth ratio used for pass 1's estimate, before two completed
    // passes exist to measure a real C_{k-1}/C_{k-2} branching ratio.
    constexpr double kDefaultGrowth = 6.0;
    // Overrun guard: once a pass is running past budget, abort + roll back only
    // when it has spent more than the ceiling below. "Almost done" passes always finish.
    // The ceiling is max(beta*budget, FLOOR): for small per-decision budgets beta*budget
    // is tiny (th d3: 2*9000=18k units) and collides with a LEGITIMATE deep pass that
    // genuinely needs ~2x budget, aborting it and changing the result (this broke th d3
    // s3003 game 278's win turn). The absolute FLOOR keeps the guard above any normal
    // completing pass (well under ~1e5 units for the suite decks) while staying far below
    // a true no-win runaway (millions of units), so normal games of every deck remain
    // byte-identical and only a genuine runaway aborts. Calibrated with the pathological
    // antilife deck OUT of the suite; revisit if NODES_PER_VIRTUAL_MS is rebased.
    // See search-perf-investigation memory.
    constexpr double    kOverrunBeta  = 2.0;
    constexpr long long kOverrunFloor = 1000000;
}

TurnSolver::SearchLine TurnSolver::FullSearchLine(const GameState& state, int depth,
                                                  int max_turns, bool second_main,
                                                  TranspositionTable* tt, SearchBudget* budget,
                                                  int* out_committed_depth)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    // Memoize the greedy tail rollouts across the whole branch-and-bound tree. The
    // deep search revisits identical leaf states many times; without a table each
    // is a fresh full rollout. When the caller hands no table (the common non-
    // bottoming path), own a per-call one. Mirrors SolveWithLookahead's local_table.
    // Byte-identical to nullptr (SimulateToEnd is a pure function of its key); the
    // table only skips recompute.
    TranspositionTable local_table;
    if (tt == nullptr) { tt = &local_table; }

    // Interior-node line memo. Always per-call (never shared like the bottoming int
    // table): it caches draw-dependent lines, valid only under THIS call's single
    // fixed root library (library size => remaining content). See FSLineCache.
    FSLineCache line_cache;

    // Iterative deepening with a deterministic START GATE (mirrors SolveWithLookahead).
    // Search 1, 2, ... `depth` complete turns and commit the deepest pass that fits the
    // budget. Both memo tables are SHARED across passes and keyed by (state, remaining-
    // depth), so pass L+1 reuses every node pass L already solved at the same remaining
    // depth and only expands its new top layer -- the re-search is almost entirely cache
    // hits. The start gate skips a pass whose estimated cost won't fit the remaining
    // budget (committing the prior, deepest-fitted pass); a pass that DOES start always
    // runs to completion (no mid-pass abort/overrun-guard yet). With no budget (nullptr)
    // or a generous one every pass runs, so the committed line is pass `depth`'s --
    // byte-identical to the former single FSLineWin(depth) call.
    // depth <= 0 keeps the former single call (its FSLineWin greedy-leaf fallback);
    // depth >= 1 deepens 1..depth. Either way the LAST committed pass is FSLineWin(depth).
    SearchLine line;
    line.win_turn = max_turns + 1;
    int committed_depth = depth;             // depth actually searched for `line`
    SearchLine prev_line = line;             // last pass that COMPLETED (overrun rollback target)
    int        prev_committed = committed_depth;

    long long c_prev = 0, c_prev2 = 0;       // work units of passes k-1, k-2
    bool      have_prev = false, have_prev2 = false;

    for (int pass_depth = (depth >= 1 ? 1 : depth); pass_depth <= depth; ++pass_depth)
    {
        // Start gate: skip (and commit the prior pass) when the next pass clearly
        // won't fit. Keyed on the running work-unit count, never the clock, so a
        // deeper search makes the same skip decision and can never come out worse.
        if (budget != nullptr && have_prev)
        {
            double ratio    = (have_prev2 && c_prev2 > 0)
                            ? static_cast<double>(c_prev) / static_cast<double>(c_prev2)
                            : kDefaultGrowth;
            double estimate = static_cast<double>(c_prev) * ratio;
            if (estimate > kStartGateAlpha * static_cast<double>(budget->Remaining()))
            {
                break;
            }
        }

        long long used_before = budget ? budget->Used() : 0;
        // Arm the OVERRUN guard: this pass may exceed its estimate, but if its real cost
        // blows past kOverrunBeta x the whole decision budget it is pathological -- abort
        // and keep the last completed pass. Normal passes finish far under this ceiling, so
        // the guard never fires for them (parity preserved). Only for a limited budget.
        if (budget != nullptr && !budget->Unlimited())
        {
            long long beta_ceiling = static_cast<long long>(
                kOverrunBeta * static_cast<double>(budget->Limit()));
            budget->SetOverrunLimit(used_before + std::max(beta_ceiling, kOverrunFloor));
        }
        SearchLine attempt = FSLineWin(state, pass_depth, max_turns, max_turns + 1, second_main, tt,
                                       &line_cache, budget);
        bool aborted = (budget != nullptr && budget->Overrun());
        if (budget != nullptr) { budget->SetOverrunLimit(0); }   // disarm

        if (aborted)
        {
            // Runaway pass: discard its partial result, commit the last completed pass.
            TRACE("search", "T%d pass=%d OVERRUN abort (used=%lld limit=%lld) -> commit depth=%d",
                  state.turn_number, pass_depth,
                  budget ? budget->Used() : 0, budget ? budget->Limit() : 0, prev_committed);
            line = prev_line; committed_depth = prev_committed;
            break;
        }

        line = attempt;
        committed_depth = pass_depth;
        prev_line = line; prev_committed = committed_depth;   // this pass completed
        long long cost = (budget ? budget->Used() : 0) - used_before;
        TRACE("search", "T%d pass=%d done win=%d cost=%lld used=%lld",
              state.turn_number, pass_depth, line.win_turn, cost, budget ? budget->Used() : 0);

        c_prev2 = c_prev;   have_prev2 = have_prev;
        c_prev  = cost;     have_prev  = true;

        // Stop at the first VERIFIED win (within this pass's searched horizon). A
        // deeper pass only extends the horizon to LATER turns, so it can never find
        // an earlier win -- this line is already optimal. Lossless: the shallowest
        // verified win equals the depth-`depth` search's min win (same first-value-
        // order line). A win turn BEYOND the horizon is a greedy-tail estimate, so we
        // keep deepening to verify or beat it (until the start gate / `depth` stops).
        if (line.win_turn <= state.turn_number + pass_depth - 1) { break; }
    }
    if (out_committed_depth != nullptr) { *out_committed_depth = committed_depth; }

    static const bool fd_trace = std::getenv("MTG_FD_TRACE") != nullptr;
    if (fd_trace)
    {
        std::cerr << "[fd] T" << state.turn_number << " LINE win=" << line.win_turn;
        for (const PhasePlan& pp : line.phases)
        {
            std::cerr << " | " << (pp.is_pre_combat ? "pre:" : "2nd:") << PlanDesc(pp.plan);
            if (pp.plan.land_decided)
            {
                std::cerr << "{land=" << (pp.plan.land_to_play.empty() ? "<none>" : pp.plan.land_to_play);
                if (!pp.plan.fetch_target.empty()) { std::cerr << " fetch=" << pp.plan.fetch_target; }
                std::cerr << "}";
            }
            else { std::cerr << "{land=undecided}"; }
        }
        std::cerr << "\n";

        // Replay the committed line on a copy and print the search's PREDICTED opp
        // life after each phase, mirroring FSLineWin/FSLineTail's own simulation. Diff
        // this against the realised [traj] opp_life to pinpoint where ApplyPlanDirect/
        // SimulateCombat over-counts vs real execution.
        GameState copy  = state;
        bool      first = true;
        for (const PhasePlan& pp : line.phases)
        {
            if (pp.is_pre_combat && !first)
            {
                if (!SimulateEndAndStartNextTurn(copy)) { break; }
                ExpireStagedCards(copy);
            }
            if (pp.is_pre_combat)
            {
                // Diagnostic: dump the library top BEFORE the plan resolves so the
                // rollout's draw source can be diffed against the executor's [traj] libtop.
                {
                    const auto& lib = copy.ActivePlayer().library;
                    int pre_hand_lands = 0, pre_nomax = 0;
                    for (const Card& hc : copy.ActivePlayer().hand)
                    { auto hd = CardDatabase::Instance().LookupCached(hc);
                      if (hd ? hd->card.IsLand() : hc.IsLand()) ++pre_hand_lands; }
                    for (const Permanent& perm : copy.battlefield)
                    { if (perm.controller_index != copy.active_player_index) continue;
                      auto pd = CardDatabase::Instance().LookupCached(perm.card);
                      if (pd && pd->params.no_max_hand_size && pd->card.IsLand()) ++pre_nomax; }
                    std::cerr << "[fd-pred]   turn=" << copy.turn_number
                              << " POST-CLEANUP hand_lands=" << pre_hand_lands
                              << " handsize=" << copy.ActivePlayer().hand.size()
                              << " nomax=" << pre_nomax << " libtop=";
                    for (std::size_t li = 0; li < lib.size() && li < 6; ++li)
                    { std::cerr << lib[li].m_name << "; "; }
                    std::cerr << " (libsize=" << lib.size() << ")\n";
                }
                ApplyPlanDirect(copy, pp.plan, true);
                SimulateAnimateLands(copy);
                SimulateTapTokens(copy);
                SimulateCombat(copy);
            }
            else
            {
                ApplyPlanDirect(copy, pp.plan, false);
            }
            int my_creatures = 0;
            for (const Permanent& perm : copy.battlefield)
            {
                if (perm.controller_index == copy.active_player_index && perm.card.IsCreature())
                { ++my_creatures; }
            }
            int hand_lands = 0, bf_le = 0, bf_nomax = 0;
            for (const Card& hc : copy.ActivePlayer().hand)
            { auto hd = CardDatabase::Instance().LookupCached(hc); if (hd ? hd->card.IsLand() : hc.IsLand()) ++hand_lands; }
            for (const Permanent& perm : copy.battlefield)
            { if (perm.controller_index != copy.active_player_index) continue;
              auto pd = CardDatabase::Instance().LookupCached(perm.card);
              if (pd && pd->params.discard_land_damage>0) ++bf_le;
              if (pd && pd->params.no_max_hand_size && pd->card.IsLand()) ++bf_nomax; }
            std::cerr << "[fd-pred]   turn=" << copy.turn_number
                      << (pp.is_pre_combat ? " pre " : " 2nd ")
                      << "opp_life=" << copy.Opponent().life
                      << " hand_lands=" << hand_lands << " LE=" << bf_le << " nomax=" << bf_nomax
                      << " my_creatures=" << my_creatures
                      << "  " << PlanDesc(pp.plan) << "\n";
            first = false;
        }
    }
    return line;
}

// ---- Rule-miner: enumerate-all-earliest-wins (offline diagnostic, see header) ----------
TurnSolver::EarliestWinReport TurnSolver::EnumerateEarliestWins(const GameState& state,
                                                                int max_turns, bool second_main)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    EarliestWinReport report;
    report.turn     = state.turn_number;
    report.earliest = max_turns + 1;

    // Same candidate set the search ranks (cast ORDERINGS included iff MTG_SEARCH_ORDER /
    // MTG_UNPRUNED is set -- EnumeratePlansWithLand expands them there).
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);

    // Deep enough to reach any win up to max_turns from this turn; NO cross-candidate B&B
    // (cutoff = max_turns+1) so each candidate gets its TRUE earliest win, not a pruned bound.
    int depth = max_turns - state.turn_number + 1;
    if (depth < 1) { depth = 1; }

    // Shared tail memo across candidates (downstream states transpose). Budget is never armed
    // for overrun here, so FSLineTail runs to completion -- this is an offline tool.
    TranspositionTable tt;
    FSLineCache        lc;
    SearchBudget       budget = SearchBudget::FromVirtualMs(1000000);

    for (const TurnSolver::Plan& p : pre)
    {
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);

        int wt;
        if (s.ActivePlayer().life <= 0)                 // self-lethal line -> never a win
        {
            wt = max_turns + 1;
        }
        else
        {
            SimulateAnimateLands(s);
            SimulateTapTokens(s);
            SimulateCombat(s);
            if (s.Opponent().life <= 0)
            {
                wt = state.turn_number;                 // wins THIS turn
            }
            else
            {
                TurnSolver::SearchLine tail = FSLineTail(s, depth - 1, max_turns,
                                                         max_turns + 1, second_main,
                                                         &tt, &lc, &budget);
                wt = tail.win_turn;
            }
        }

        EarliestWinCandidate c;
        c.land           = p.land_to_play;
        c.fetch          = p.fetch_target;
        c.searched_order = p.searched_order;
        c.win_turn       = wt;

        // Effective cast order, mirroring apply_plan_actions: a searched plan casts in vector
        // order; otherwise the canonical clean-set order (stable-sort by CastRankOf). The
        // enabler-first / opaque-set nuance is approximated (a searched_order flag marks the
        // exact-order plans). Sacrifice-land casts are reported separately (they execute last).
        std::vector<int> hand_casts;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[i];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.sacrifice_land) { c.sac_casts.push_back(a.card_name); }
            else                  { hand_casts.push_back(i); }
        }
        if (!p.searched_order)
        {
            std::stable_sort(hand_casts.begin(), hand_casts.end(), [&](int x, int y)
            { return CastRankOf(state, p.actions[x].card_name)
                   < CastRankOf(state, p.actions[y].card_name); });
        }
        for (int i : hand_casts) { c.cast_order.push_back(p.actions[i].card_name); }

        if (wt < report.earliest) { report.earliest = wt; }
        report.candidates.push_back(std::move(c));
    }
    return report;
}

// ---- Public API ----

TurnSolver::Plan TurnSolver::SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                                  int depth, int max_turns,
                                                  SearchBudget* budget, bool enforce_budget,
                                                  bool second_main, TranspositionTable* tt,
                                                  int* out_committed_win,
                                                  int* out_committed_sub_depth)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    // Report the committed pass's (win turn, sub_depth) to the caller's optional
    // out-params. Used by the non-convergence detector; every return path calls it.
    auto report = [&](int win, int sub_depth)
    {
        if (out_committed_win)       { *out_committed_win = win; }
        if (out_committed_sub_depth) { *out_committed_sub_depth = sub_depth; }
    };

    if (depth <= 0)
    {
        if (budget) { budget->Consume(1); }
        report(max_turns + 1, 0);   // greedy fallback is not an exhaustively-verified win
        return Solve(state, is_pre_combat);
    }

    // The enforcing top-level call owns the per-decision transposition table and
    // threads it through the whole recursion; rollout sub-searches reuse the table
    // they were handed. local_table is only referenced when we create it here.
    TranspositionTable  local_table;
    if (tt == nullptr && enforce_budget) { tt = &local_table; }

    // Candidate set is land-folded: when a land drop is available this enumerates
    // every (land choice x spell subset) plus a defer option, each plan tagged with
    // its land_to_play. The same fold runs in the rollout (SimulateToEndImpl calls
    // this per turn), so the land choice is searched consistently end to end.
    std::vector<Plan> candidates = EnumeratePlansWithLand(state, is_pre_combat);

    // Candidates are sorted highest-value first, so the first winning plan
    // (if any) is also the highest-value winning plan.
    for (const Plan& p : candidates)
    {
        if (p.wins_this_turn) { report(state.turn_number, depth - 1); return p; }
    }

    if (candidates.empty()) { report(max_turns + 1, 0); return Plan{}; }

    // Track the committed pass's win turn / sub_depth for non-convergence reporting.
    int committed_win       = max_turns + 1;
    int committed_sub_depth = 0;

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

            PROF_INC(gamestate_copies);
            GameState copy = state;
            if (is_pre_combat)
            {
                ApplyPlanDirect(copy, plan, true);
                SimulateAnimateLands(copy);
                SimulateTapTokens(copy);

                // Combat this turn
                SimulateCombat(copy);
                if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }

                // Post-combat (second) main if this deck wants one (greedy here).
                if (second_main)
                {
                    Plan post = Solve(copy, false);
                    ApplyPlanDirect(copy, post, false);
                    if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }
                }
            }
            else
            {
                // Top-level post-combat (second) main decision: combat already
                // happened this turn, so apply the candidate as a post-combat play
                // and DON'T re-simulate combat (that would be a phantom second one).
                ApplyPlanDirect(copy, plan, false);
                if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }
            }

            // End of this turn + start of next. The next turn's land drop is searched
            // inside SimulateToEnd's per-turn SolveWithLookahead, so no greedy land
            // play happens here.
            if (!SimulateEndAndStartNextTurn(copy)) { ++candidates_done; continue; }

            // Simulate remaining turns at this pass's sub_depth. The rollout runs to
            // completion (enforce_budget=false inside), only consuming budget; the
            // within-pass running best (pass_best_win) is the branch-and-bound cutoff.
            int win_turn = SimulateToEnd(std::move(copy), sub_depth, max_turns, budget,
                                         pass_best_win, second_main, tt);
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
            best_plan           = pass_best;
            committed_win       = pass_best_win;
            committed_sub_depth = sub_depth;
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

    // Diagnostic (MTG_TRACE_PLAYOUT_*): replay the committed plan and trace the
    // rollout's believed winning line. Inert unless the env seed/turn match.
    if (enforce_budget && s_tp_seed >= 0
        && static_cast<long long>(state.game_seed) == s_tp_seed
        && state.turn_number == s_tp_turn)
    {
        std::cerr << "[playout] seed=" << state.game_seed << " turn=" << state.turn_number
                  << " committed_win=" << committed_win
                  << " sub_depth=" << committed_sub_depth
                  << "  best_plan=" << PlanDesc(best_plan) << "\n";
        GameState dbg = state;
        int life0 = dbg.Opponent().life;
        ApplyPlanDirect(dbg, best_plan, is_pre_combat);
        SimulateAnimateLands(dbg);
        SimulateTapTokens(dbg);
        SimulateCombat(dbg);
        std::cerr << "  [pl] turn=" << dbg.turn_number << " opp " << life0 << "->"
                  << dbg.Opponent().life << " (committed turn)\n";
        if (dbg.Opponent().life > 0 && SimulateEndAndStartNextTurn(dbg))
        {
            g_trace_arm = true;
            int w = SimulateToEndImpl(dbg, committed_sub_depth, max_turns,
                                      nullptr, max_turns + 1, second_main, nullptr);
            g_trace_arm = false;
            std::cerr << "  [pl] rollout returned win=" << w << "\n";
        }
    }

    report(committed_win, committed_sub_depth);
    return best_plan;
}

// --- External-controller hooks (Claude-play / human-play prototype) -------------
// Thin public wrappers around the file-static enumeration + application the solver
// uses internally, so an external decision provider gets the exact same legal plans
// and identical execution. See TurnSolver.h.
std::vector<TurnSolver::Plan> TurnSolver::EnumerateMainPlans(const GameState& state,
                                                             bool is_pre_combat)
{
    return EnumeratePlansWithLand(state, is_pre_combat);
}

// One-line "land=...; cast: a, b" summary of a plan (for the human-play accept verdict).
static std::string LineSummaryOfPlan(const TurnSolver::Plan& p)
{
    std::string s;
    if (p.land_decided && !p.land_to_play.empty()) { s += "land=" + p.land_to_play + "; "; }
    int le_count = 0;
    std::vector<std::string> cast_names;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::DiscardToLandsEdge) { le_count += a.discard_lands; }
        else { cast_names.push_back(a.card_name); }
    }
    s += "cast: ";
    if (cast_names.empty()) { s += "(nothing)"; }
    else
    {
        for (size_t i = 0; i < cast_names.size(); ++i) { if (i) s += ", "; s += cast_names[i]; }
    }
    if (le_count > 0) { s += "; Land's Edge x" + std::to_string(le_count); }
    return s;
}

// Deduct a KNOWN-PAYABLE cost from an accounting pool (caller checks CanPay first).
// Mirrors ManaPool::CanPay's allocation: colour pips from their own colour then wild;
// generic from leftover specific mana (any colour / {C}) then wild.
static void DeductPayable(ManaPool& p, const ManaCost& cost)
{
    auto pay = [&](int need, int& specific)
    {
        int u = std::min(need, specific); specific -= u; need -= u;
        int w = std::min(need, p.wild);   p.wild   -= w; need -= w;
    };
    pay(cost.white, p.white); pay(cost.blue, p.blue); pay(cost.black, p.black);
    pay(cost.red,   p.red);   pay(cost.green, p.green); pay(cost.colorless, p.colorless);
    int g = cost.generic;
    for (int* src : { &p.colorless, &p.white, &p.blue, &p.black, &p.red, &p.green })
    { int u = std::min(g, *src); *src -= u; g -= u; }
    int w = std::min(g, p.wild); p.wild -= w; g -= w;
}

TurnSolver::LineCheck TurnSolver::CheckLine(const GameState& state, bool is_pre_combat,
                                            const LineSpec& spec)
{
    using V = LineCheck::Verdict;
    LineCheck out;

    // --- 0) Pass / cast-nothing maps to the engine's "idx < 0" pass ------------
    // A line that only activates Land's Edge / deploys via Vial / retraces (no land, no hand
    // casts) is NOT a pass -- those are real actions committed via their own verbs.
    if (spec.pass || (!spec.has_land && spec.casts.empty() && spec.lands_edge == 0 &&
                      spec.vial_deploys.empty() && spec.retrace_casts.empty()))
    {
        out.verdict = V::Accept; out.plan_index = -1;
        out.matched_summary = "pass / cast nothing";
        return out;
    }

    const std::string wantLand = spec.has_land ? spec.land : std::string();
    std::vector<std::string> sortedCasts = spec.casts;
    std::sort(sortedCasts.begin(), sortedCasts.end());
    std::vector<std::string> sortedVial = spec.vial_deploys;
    std::sort(sortedVial.begin(), sortedVial.end());
    std::vector<std::string> sortedRetrace = spec.retrace_casts;
    std::sort(sortedRetrace.begin(), sortedRetrace.end());

    // --- 1) Does the line match a plan (or several variants) the model would play? ----
    // A "match" is same land + same multiset of cast card names. Several enumerated plans can
    // match while differing in a per-spell sub-decision (tutor target / X / Ponder keep /
    // Soulfire count) -- in human-play mode (MTG_UNPRUNED) the search enumerates them all, so we
    // surface the distinct ones for the human to pick among. Pure cast-ORDER duplicates (same
    // sub-decisions, different order) collapse to one representative by their param signature.
    std::vector<Plan> plans = EnumerateMainPlans(state, is_pre_combat);

    // Collect every plan matching land + cast-name MULTISET, recording each plan's cast order and
    // a sub-decision signature/label (tutor target / X / Ponder keep / Soulfire count / fetch
    // target). The land + the plan index come along so we can both honour the player's ORDER and
    // surface genuine sub-decision choices.
    struct Cand { int idx; std::vector<std::string> order; std::string sig, label;
                  std::vector<std::string> cards; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < plans.size(); ++i)
    {
        const Plan& p = plans[i];
        const std::string planLand = p.land_decided ? p.land_to_play : std::string();
        if (planLand != wantLand) { continue; }
        // Actions split by how the human commits them, each matched against its own verb:
        //   DiscardToLandsEdge -> a COUNT vs spec.lands_edge (card_name is "Land's Edge")
        //   ActivateVial       -> creature names vs spec.vial_deploys (free Vial deploy)
        //   CastFromGraveyard  -> spell names vs spec.retrace_casts (retrace)
        //   everything else    -> the plain hand-cast multiset vs spec.casts (order honoured)
        int planLE = 0;
        std::vector<std::string> orderNames, vialNames, retraceNames;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::DiscardToLandsEdge) { planLE += a.discard_lands; continue; }
            if (a.kind == Action::Kind::ActivateVial)       { vialNames.push_back(a.card_name); continue; }
            if (a.kind == Action::Kind::CastFromGraveyard)  { retraceNames.push_back(a.card_name); continue; }
            orderNames.push_back(a.card_name);
        }
        if (planLE != spec.lands_edge) { continue; }
        std::vector<std::string> sortedNames = orderNames;
        std::sort(sortedNames.begin(), sortedNames.end());
        if (sortedNames != sortedCasts) { continue; }
        std::vector<std::string> sortedVialNames = vialNames;
        std::sort(sortedVialNames.begin(), sortedVialNames.end());
        if (sortedVialNames != sortedVial) { continue; }
        std::vector<std::string> sortedRetraceNames = retraceNames;
        std::sort(sortedRetraceNames.begin(), sortedRetraceNames.end());
        if (sortedRetraceNames != sortedRetrace) { continue; }

        // Per-decision tokens, order-INDEPENDENT (sorted) so plans differing only in cast order
        // share a signature; a real sub-decision difference (target / X / keep / fetch) splits it.
        std::vector<std::string> toks, artCards;
        for (const Action& a : p.actions)
        {
            if (!a.tutor_target.empty())   { toks.push_back(a.card_name + " \xE2\x86\x92 " + a.tutor_target); artCards.push_back(a.tutor_target); }
            if (a.chosen_x > 0)            { toks.push_back(a.card_name + " X=" + std::to_string(a.chosen_x)); artCards.push_back(a.card_name); }
            if (a.ponder_keep >= 0)        { toks.push_back(a.card_name + (a.ponder_keep ? ": keep top" : ": shuffle")); artCards.push_back(a.card_name); }
            if (a.soulfire_own_targets > 0){ toks.push_back(a.card_name + " +" + std::to_string(a.soulfire_own_targets) + " own"); artCards.push_back(a.card_name); }
        }
        // Fetchland target is a plan-level sub-decision (cracking a fetch chooses what to get).
        if (!p.fetch_target.empty()) { toks.push_back(p.land_to_play + " fetches " + p.fetch_target); artCards.push_back(p.fetch_target); }
        std::sort(toks.begin(), toks.end());
        std::string sig, label;
        for (size_t t = 0; t < toks.size(); ++t) { sig += "|" + toks[t]; label += (t?"; ":"") + toks[t]; }
        if (label.empty()) { label = LineSummaryOfPlan(p); }
        cands.push_back({ static_cast<int>(i), orderNames, sig, label, artCards });
    }

    // Honour the player's ORDER: prefer candidates whose cast sequence equals the queued order, so
    // the executed plan (a searched_order variant) plays in that order. If none matches exactly
    // (the only enumerated plan is an end-state-equivalent ordering), fall back to all candidates
    // -- the result is identical, only the displayed order may differ.
    std::vector<const Cand*> pool;
    for (const Cand& c : cands) { if (c.order == spec.casts) { pool.push_back(&c); } }
    if (pool.empty()) { for (const Cand& c : cands) { pool.push_back(&c); } }

    std::vector<std::string> seenSig;
    for (const Cand* c : pool)
    {
        if (std::find(seenSig.begin(), seenSig.end(), c->sig) != seenSig.end()) { continue; }
        seenSig.push_back(c->sig);
        out.variants.push_back({ c->idx, c->label, c->cards });
    }
    if (out.variants.size() == 1)
    {
        out.verdict = V::Accept; out.plan_index = out.variants[0].plan_index;
        out.matched_summary = LineSummaryOfPlan(plans[out.variants[0].plan_index]);
        return out;
    }
    if (out.variants.size() > 1)
    {
        out.verdict = V::Choose;
        out.reason = "this line resolves several ways -- pick the sub-decisions";
        return out;
    }

    // --- 2) Not enumerated: is it rules-legal (modelling same-turn ramp)? ------
    GameState s = state;   // work on a copy; CheckLine must not mutate the real game

    if (spec.has_land)
    {
        if (!PlayLandByName(s, spec.land))
        {
            out.verdict = V::Illegal;
            out.failed_action = "land=" + spec.land;
            out.reason = "can't play land '" + spec.land +
                         "' (land drop unavailable, or it is not a land in hand)";
            return out;
        }
    }

    // Resolve each named cast to a card definition; bail to Unsupported for the action
    // kinds this v1 check cannot honestly validate (X spells, alt-cost, tutors).
    struct PendingCast { std::string name; const CardDefinition* def; ManaCost cost; bool rock; };
    std::vector<PendingCast> pending;
    for (const std::string& name : spec.casts)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        if (!def)
        {
            out.verdict = V::Illegal; out.failed_action = "cast=" + name;
            out.reason = "unknown card '" + name + "'";
            return out;
        }
        const ManaCost& mc = def->card.m_mana_cost;
        if (mc.has_x || def->params.alt_lifegain_cost > 0 ||
            def->params.tutor_to_hand || def->params.tutor_to_top)
        {
            out.verdict = V::Unsupported; out.failed_action = "cast=" + name;
            out.reason = "'" + name + "' uses an action kind (X / alt-cost / tutor) the v1 "
                         "line check cannot validate yet";
            return out;
        }
        pending.push_back({ name, def, mc, def->params.mana_rock && !def->card.IsCreature() });
    }

    // Each named card must actually be in hand (multiset-correct for duplicates).
    {
        std::vector<std::string> handNames;
        for (const Card& c : s.ActivePlayer().hand) { handNames.push_back(c.m_name); }
        for (const PendingCast& pc : pending)
        {
            auto it = std::find(handNames.begin(), handNames.end(), pc.name);
            if (it == handNames.end())
            {
                out.verdict = V::Illegal; out.failed_action = "cast=" + pc.name;
                out.reason = "'" + pc.name + "' is not in hand (already cast, or never there)";
                return out;
            }
            handNames.erase(it);
        }
    }

    // Greedy affordability fixpoint: repeatedly cast any affordable not-yet-cast spell,
    // mana producers FIRST so a freshly-cast rock's mana is online for the rest of the
    // line (the same-turn ramp the enumerator's BuildPool does not credit). Order-
    // independent, so it doesn't penalise the human's click order.
    ManaPool avail = BuildPool(s);
    std::vector<bool> done(pending.size(), false);
    size_t remaining = pending.size();
    bool progress = true;
    while (remaining > 0 && progress)
    {
        progress = false;
        for (int phase = 0; phase < 2 && !progress; ++phase)
        {
            const bool want_rock = (phase == 0);
            for (size_t k = 0; k < pending.size(); ++k)
            {
                if (done[k] || pending[k].rock != want_rock) { continue; }
                if (!avail.CanPay(pending[k].cost)) { continue; }
                DeductPayable(avail, pending[k].cost);
                if (pending[k].rock) { AddSourceToPool(avail, s, *pending[k].def); }
                done[k] = true; --remaining; progress = true; break;
            }
        }
    }

    if (remaining == 0)
    {
        out.verdict = V::LegalNotEnumerated;
        out.reason  = "rules-legal (an affordability simulation can execute it), but the "
                      "search never enumerated this line";
        return out;
    }
    for (size_t k = 0; k < pending.size(); ++k)
    {
        if (!done[k])
        {
            out.verdict = V::Illegal; out.failed_action = "cast=" + pending[k].name;
            out.reason  = "can't pay " + pending[k].cost.ToString() + " for '" +
                          pending[k].name + "' with the mana available this phase";
            break;
        }
    }
    return out;
}

void TurnSolver::ApplyPlan(GameState& state, const Plan& plan, bool is_pre_combat)
{
    ApplyPlanDirect(state, plan, is_pre_combat);
}
