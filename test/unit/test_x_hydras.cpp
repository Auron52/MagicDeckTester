// Unit tests for the Hydras I2 group (2026-11-11): the CR 601.2f X-cost reduction, the same-plan X potential
// (defect 6), Primordial / Voracious / Neverwinter / Steelbane Hydra and Gargos, Vicious Watcher, plus the
// HydrasProvider X / fight / destroy-target rules.
//
//  1. Card data: the five entries carry the right params; Scryfall's keyword ACTIONS (Fight, Double) are no tags.
//  2. CR 601.2f: Gargos's {4} reaches the X part (XSpellUnabsorbedReduction / XCastGenericDiscount), and the
//     enumerator widens max X by it and prices every X with it.
//  3. Lockstep: the rollout's apply of "Primordial X = 4 under Gargos" pays exactly {G}{G}.
//  4. Defect 6: "Vernal Bloom, then the largest X" is enumerated and applies.
//  5. Primordial's upkeep doubling in both worlds; Neverwinter's dice are a prefix stream shared by both worlds.
//  6. Voracious: double in both worlds, the fight pin, and the fight variants (Generic emits, Hydras prunes).
//  7. Steelbane: the activation's targets, its resolution, and the HydrasProvider prune.
//  8. HydrasProvider: X = 0 prune for 0/0 bodies, Neverwinter's maximum-X rule.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void LoadXh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefXh(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardXh(const std::string& name, int number)
{
    Card c = DefXh(name).card;
    c.m_number = number;
    return c;
}

GameState FreshXh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.game_seed           = 4242;
    return s;
}

int PutXh(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = CardXh(name, number);
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const Permanent* ByNumXh(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountersXh(const GameState& s, int number)
{
    const Permanent* p = ByNumXh(s, number);
    return p ? PlusCountersOn(*p) : -1;
}

void ForestsXh(GameState& s, int n, int first = 100)
{
    for (int i = 0; i < n; ++i) { PutXh(s, "Forest", first + i); }
}

std::set<int> CastXs(const std::vector<Action>& acts, const std::string& name)
{
    std::set<int> xs;
    for (const Action& a : acts)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { xs.insert(a.chosen_x); } }
    return xs;
}

const Action* CastAt(const std::vector<Action>& acts, const std::string& name, int x)
{
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name && a.chosen_x == x
            && a.tutor_target.empty())
        { return &a; }
    }
    return nullptr;
}

int UntappedForests(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Forest" && !p.tapped) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("X Hydras: card data -- params set, keyword actions not tagged")
{
    LoadXh();
    const CardParams& pr = DefXh("Primordial Hydra").params;
    CHECK(pr.enters_with_x_plus_counters);
    CHECK(pr.upkeep_double_self_counters);
    const CardParams& vo = DefXh("Voracious Hydra").params;
    CHECK(vo.etb_modal_double_self_counters);
    CHECK(vo.etb_modal_fight_opp_creature);
    CHECK(DefXh("Voracious Hydra").card.m_toughness.value_or(-1) == 1);
    const CardDefinition& nw = DefXh("Neverwinter Hydra");
    CHECK(nw.params.enters_with_x_dice_sides == 6);
    CHECK(nw.card.m_mana_cost.x_pips == 2);
    const CardParams& st = DefXh("Steelbane Hydra").params;
    REQUIRE(st.remove_counter_destroy_art_ench_cost.has_value());
    CHECK(st.remove_counter_destroy_art_ench_cost->ManaValue() == 3);
    const CardDefinition& g = DefXh("Gargos, Vicious Watcher");
    CHECK(g.params.reduces_spell_subtype == "Hydra");
    CHECK(g.params.reduces_spell_subtype_amount == 4);
    CHECK(g.card.m_mana_cost.ManaValue() == 6);
}

TEST_CASE("CR 601.2f: Gargos's {4} reaches the X of an X Hydra, and only Hydra spells")
{
    LoadXh();
    GameState s = FreshXh();
    s.deck_has_cost_reducer = true;
    CHECK(XSpellUnabsorbedReduction(DefXh("Primordial Hydra"), s) == 0);   // no Gargos yet
    PutXh(s, "Gargos, Vicious Watcher", 1);
    const CardDefinition& pr = DefXh("Primordial Hydra");
    CHECK(EffectiveSpellCost(pr, s).generic == 0);
    CHECK(EffectiveSpellCost(pr, s).green == 2);
    CHECK(XSpellUnabsorbedReduction(pr, s) == 4);
    CHECK(XCastGenericDiscount(pr, s, 4) == 4);
    CHECK(XSpellUnabsorbedReduction(DefXh("Neverwinter Hydra"), s) == 4);
    // Gargos discounts a second Gargos to {G}{G}{G} (a fixed-cost Hydra: the printed generic absorbs it).
    CHECK(EffectiveSpellCost(DefXh("Gargos, Vicious Watcher"), s).generic == 0);
    CHECK(XSpellUnabsorbedReduction(DefXh("Gargos, Vicious Watcher"), s) == 0);
    // A non-Hydra keeps its generic.
    CHECK(EffectiveSpellCost(DefXh("Vernal Bloom"), s).generic == DefXh("Vernal Bloom").card.m_mana_cost.generic);
}

TEST_CASE("CR 601.2f: with Gargos and 4 Forests the enumerator offers Primordial X up to 6, priced after X")
{
    LoadXh();
    GameState s = FreshXh();
    s.deck_has_cost_reducer = true;
    PutXh(s, "Gargos, Vicious Watcher", 1);
    ForestsXh(s, 4);
    s.players[0].hand.push_back(CardXh("Primordial Hydra", 2));
    s.players[0].hand.push_back(CardXh("Neverwinter Hydra", 3));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const std::set<int> xs = CastXs(acts, "Primordial Hydra");
    REQUIRE(!xs.empty());
    CHECK(*xs.rbegin() == 6);
    const Action* x4 = CastAt(acts, "Primordial Hydra", 4);
    REQUIRE(x4 != nullptr);
    CHECK(x4->cost.generic == 0);
    const Action* x6 = CastAt(acts, "Primordial Hydra", 6);
    REQUIRE(x6 != nullptr);
    CHECK(x6->cost.generic == 2);
    // Neverwinter ({X}{X}): 4 mana + the {4} -> X = 3 ({6} - 4 = {2}, plus {G}{G}).
    CHECK(*CastXs(acts, "Neverwinter Hydra").rbegin() == 3);
}

TEST_CASE("CR 601.2f lockstep: the rollout's Primordial X = 4 under Gargos pays exactly {G}{G}")
{
    LoadXh();
    GameState s = FreshXh();
    s.deck_has_cost_reducer = true;
    PutXh(s, "Gargos, Vicious Watcher", 1);
    ForestsXh(s, 2);
    s.players[0].hand.push_back(CardXh("Primordial Hydra", 2));
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* hy = nullptr;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Primordial Hydra" && a.chosen_x == 4) { hy = &a; } }
        if (hy == nullptr) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        CHECK(CountersXh(t, 2) == 4);
        CHECK(UntappedForests(t) == 0);
        seen = true;
        break;
    }
    CHECK(seen);
}

TEST_CASE("Defect 6: Vernal Bloom then the largest X is enumerated and payable (same-plan X potential)")
{
    LoadXh();
    GameState s = FreshXh();
    ForestsXh(s, 10);
    s.players[0].hand.push_back(CardXh("Vernal Bloom", 1));
    s.players[0].hand.push_back(CardXh("Primordial Hydra", 2));
    // Pre-plan board: 10 mana -> X <= 8. Bloom first: 4 Forests pay it, 6 tap for 12 -> X = 10.
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    CHECK(CastXs(acts, "Primordial Hydra").count(10) == 1);
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool bloom = false; int x = -1;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Vernal Bloom") { bloom = true; }
            if (a.card_name.str() == "Primordial Hydra") { x = a.chosen_x; }
        }
        if (!bloom || x != 10) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        CHECK(CountersXh(t, 2) == 10);
        seen = true;
        break;
    }
    CHECK(seen);
}

TEST_CASE("Primordial Hydra: the upkeep doubles its counters identically in both worlds")
{
    LoadXh();
    GameState before = FreshXh();
    const int idx = PutXh(before, "Primordial Hydra", 1);
    PutPlusCounters(before, before.battlefield[static_cast<std::size_t>(idx)], 3);
    PutXh(before, "Forest", 2);
    for (int i = 0; i < 4; ++i) { before.players[0].library.push_back(CardXh("Forest", 10 + i)); }
    before.players[1].library.push_back(CardXh("Forest", 30));
    GameState rollout = before;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(rollout));
    GameState executor = before;
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    REQUIRE(engine.AdvanceToFirstMainForTest(executor));
    CHECK(CountersXh(rollout, 1) == 6);
    CHECK(CountersXh(executor, 1) == 6);
}

TEST_CASE("Neverwinter Hydra: X d6 from one prefix stream, the same in the executor and the rollout")
{
    LoadXh();
    // Prefix property: each extra die adds 1..6.
    {
        GameState a = FreshXh();
        GameState b = FreshXh();
        const int t3 = RollDiceTotal(a, 0, 3, 6, "test");
        const int t4 = RollDiceTotal(b, 0, 4, 6, "test");
        CHECK(t4 - t3 >= 1);
        CHECK(t4 - t3 <= 6);
        CHECK(a.players[0].dice_rolled == 3);
    }
    // Executor resolution vs the rollout's apply of the same X.
    int exec_ctrs = -1, exec_rolled = -1;
    {
        GameState s = FreshXh();
        StackEntry e;
        e.type             = StackEntry::EntryType::Spell;
        e.source           = CardXh("Neverwinter Hydra", 1);
        e.controller_index = 0;
        e.chosen_x         = 2;
        EffectHandler::Resolve(s, e, DefXh("Neverwinter Hydra"));
        exec_ctrs = CountersXh(s, 1);
        exec_rolled = s.players[0].dice_rolled;
        CHECK(exec_rolled == 2);
        CHECK(exec_ctrs >= 2);
        CHECK(exec_ctrs <= 12);
    }
    {
        GameState s = FreshXh();
        ForestsXh(s, 6);
        s.players[0].hand.push_back(CardXh("Neverwinter Hydra", 1));
        bool seen = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            int x = -1;
            for (const Action& a : p.actions) { if (a.card_name.str() == "Neverwinter Hydra") { x = a.chosen_x; } }
            if (x != 2) { continue; }
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, true);
            CHECK(CountersXh(t, 1) == exec_ctrs);
            CHECK(t.players[0].dice_rolled == exec_rolled);
            seen = true;
            break;
        }
        CHECK(seen);
    }
}

TEST_CASE("Voracious Hydra: double in both worlds; a fight pin fights; Generic offers the fight, Hydras prunes it")
{
    LoadXh();
    // Executor, unpinned: X = 3 doubles to 6.
    {
        GameState s = FreshXh();
        StackEntry e;
        e.type             = StackEntry::EntryType::Spell;
        e.source           = CardXh("Voracious Hydra", 1);
        e.controller_index = 0;
        e.chosen_x         = 3;
        EffectHandler::Resolve(s, e, DefXh("Voracious Hydra"));
        CHECK(CountersXh(s, 1) == 6);
    }
    // Executor, fight pin vs an opponent 2/2: the 3/4 kills it and survives with 2 damage.
    {
        GameState s = FreshXh();
        const int oi = PutXh(s, "Forest", 50, /*controller=*/1);
        s.battlefield[static_cast<std::size_t>(oi)].card = CardXh("Voracious Hydra", 50);   // a creature body
        PutPlusCounters(s, s.battlefield[static_cast<std::size_t>(oi)], 1);                // 0/1 + 1 = 1/2
        StackEntry e;
        e.type             = StackEntry::EntryType::Spell;
        e.source           = CardXh("Voracious Hydra", 1);
        e.controller_index = 0;
        e.chosen_x         = 3;
        e.tutor_target     = VoraciousPinFight(50);
        EffectHandler::Resolve(s, e, DefXh("Voracious Hydra"));
        CHECK(CountersXh(s, 1) == 3);           // fought: not doubled
        CHECK(ByNumXh(s, 50) == nullptr);       // 1/2 took 3: dead
        REQUIRE(ByNumXh(s, 1) != nullptr);
        CHECK(ByNumXh(s, 1)->damage == 1);
    }
    // Rollout: a plan with X = 3 (base action, no pin) doubles to 6.
    {
        GameState s = FreshXh();
        ForestsXh(s, 5);
        s.players[0].hand.push_back(CardXh("Voracious Hydra", 1));
        bool seen = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            const Action* va = nullptr;
            for (const Action& a : p.actions) { if (a.card_name.str() == "Voracious Hydra") { va = &a; } }
            if (va == nullptr || va->chosen_x != 3 || !va->tutor_target.empty()) { continue; }
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, true);
            CHECK(CountersXh(t, 1) == 6);
            seen = true;
            break;
        }
        CHECK(seen);
    }
    // Generic: one fight variant per legal opponent creature; HydrasProvider admits none.
    {
        GameState s = FreshXh();
        ForestsXh(s, 3);
        const int oi = PutXh(s, "Forest", 50, 1);
        s.battlefield[static_cast<std::size_t>(oi)].card = CardXh("Voracious Hydra", 50);
        s.players[0].hand.push_back(CardXh("Voracious Hydra", 1));
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        int fights = 0;
        for (const Action& a : acts) { if (VoraciousPinFightTarget(a.tutor_target.str()) == 50) { ++fights; } }
        CHECK(fights >= 1);
        const HydrasProvider hp;
        const std::vector<int> legal = VoraciousFightTargets(s, 0);
        REQUIRE(legal.size() == 1);
        CHECK(hp.EtbFightTargets(s, 0, DefXh("Voracious Hydra"), legal).empty());
    }
}

TEST_CASE("Steelbane Hydra: the destroy activation, its cost, and the HydrasProvider own-target prune")
{
    LoadXh();
    GameState s = FreshXh();
    const int si = PutXh(s, "Steelbane Hydra", 1);
    PutPlusCounters(s, s.battlefield[static_cast<std::size_t>(si)], 2);
    ForestsXh(s, 3);
    PutXh(s, "Gauntlet of Power", 2);                    // ours
    PutXh(s, "Lightning Greaves", 3, /*controller=*/1);   // the opponent's
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    std::set<int> targets;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::CounterDestroyArtEnch)
        { targets.insert(a.sac_victim_id); }
    }
    CHECK(targets == std::set<int>{ 2, 3 });   // Generic: every legal target
    const HydrasProvider hp;
    const std::vector<int> legal = CounterDestroyLegalTargets(s, 0);
    const std::vector<int> adm = hp.CounterDestroyArtEnchTargets(s, 0, s.battlefield[static_cast<std::size_t>(si)], legal);
    REQUIRE(adm.size() == 1);
    CHECK(s.battlefield[static_cast<std::size_t>(adm[0])].card.m_number == 3);
    // Resolution: pay a counter, destroy the opponent's Greaves.
    {
        GameState t = s;
        ResolveCounterDestroyArtEnch(t, 0, 1, 3);
        CHECK(CountersXh(t, 1) == 1);
        CHECK(ByNumXh(t, 3) == nullptr);
        CHECK(ByNumXh(t, 2) != nullptr);
    }
    // A 1-counter Steelbane dies paying the cost; the destroy still resolves.
    {
        GameState t = s;
        Permanent* sp = nullptr;
        for (Permanent& p : t.battlefield) { if (p.card.m_number == 1) { sp = &p; } }
        REQUIRE(sp != nullptr);
        RemovePlusCounters(*sp, 1);
        ResolveCounterDestroyArtEnch(t, 0, 1, 3);
        CHECK(ByNumXh(t, 1) == nullptr);
        CHECK(ByNumXh(t, 3) == nullptr);
    }
}

TEST_CASE("HydrasProvider: X = 0 dropped for a doomed 0/0, kept for Voracious and beside a Gauntlet; Neverwinter max X")
{
    LoadXh();
    const HydrasProvider hp;
    GameState s = FreshXh();
    std::vector<int> xs = hp.XCandidates(s, DefXh("Primordial Hydra"), 3);
    CHECK(std::find(xs.begin(), xs.end(), 0) == xs.end());
    CHECK(std::find(xs.begin(), xs.end(), 3) != xs.end());
    xs = hp.XCandidates(s, DefXh("Voracious Hydra"), 3);
    CHECK(std::find(xs.begin(), xs.end(), 0) != xs.end());
    s.players[0].hand.push_back(CardXh("Gauntlet of Power", 9));
    xs = hp.XCandidates(s, DefXh("Primordial Hydra"), 3);
    CHECK(std::find(xs.begin(), xs.end(), 0) != xs.end());

    // Neverwinter at X = 1 with 2 mana unspent of a 6-mana pool is below its maximum; at X = 2 it is not.
    GameState g = FreshXh();
    Action nw;
    nw.kind = Action::Kind::CastFromHand;
    nw.card_name = DefXh("Neverwinter Hydra").card.m_name;
    nw.def = &DefXh("Neverwinter Hydra");
    nw.cost = DefXh("Neverwinter Hydra").card.m_mana_cost;
    nw.cost.generic = 2;
    nw.chosen_x = 1;
    std::vector<Action> cands{ nw };
    CHECK(hp.XCastBelowMax(g, cands, { 0 }, 0, 6));
    cands[0].cost.generic = 4;
    cands[0].chosen_x = 2;
    CHECK(!hp.XCastBelowMax(g, cands, { 0 }, 0, 6));
    // Primordial: no rule unless MTG_HYDRAS_MAX_X is set (default OFF).
    Action pr = nw;
    pr.card_name = DefXh("Primordial Hydra").card.m_name;
    pr.def = &DefXh("Primordial Hydra");
    pr.cost = DefXh("Primordial Hydra").card.m_mana_cost;
    pr.cost.generic = 1;
    pr.chosen_x = 1;
    CHECK(!hp.XCastBelowMax(g, std::vector<Action>{ pr }, { 0 }, 0, 6));
}
