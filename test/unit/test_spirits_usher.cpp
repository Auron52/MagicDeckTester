// Spirits onboarding, integrator D (2026-11-13): Usher of the Fallen -- the engine's first BOAST.
//   "Boast -- {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked
//    this turn and only once each turn.)"
//   * pay_token_cost {1}{W} (PermAbilityMode::PayToken, Slimefoot's shape) + activate_only_if_attacked_this_turn +
//     activate_once_each_turn, both checked by PermAbilitySourceLive (enumeration, activation keys, both apply
//     worlds, SpendRepeatActivations).
//   * the per-attacker count: NoteAttacksDeclared arms it for a deck stamped deck_has_attack_count, ONLY on an
//     attacker carrying the param.
//   * the post-combat window: SpiritsProvider's restricted second main (BoastSecondMainLive / IsBoastActivation).
//   * payment: Secluded Courtyard's colour pays the {W} of a creature's ability, Unclaimed Territory's does not.
// test/scenarios/spirits_usher_*.json drive the search end to end.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
const char* const kUsher = "Usher of the Fallen";

const CardDefinition& UsDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card UsZone(const std::string& name, int number)
{
    UsDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int UsPut(GameState& s, const std::string& name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card = UsDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const DecisionProvider& SpiritsProvU()
{
    static const DecisionProvider& prov = [] () -> const DecisionProvider& {
        UsDef("Plains");
        return DetectDecisionProvider(DeckLoader::LoadFromFile(
            ResolveHeuristicDefaultsPath("decks/Spirits/Spirits.cod")));
    }();
    return prov;
}

// A Spirits board in the POST-COMBAT main (or main 1), stamped as the Spirits deck is.
GameState UsBoard(const std::vector<std::string>& lands, bool post_combat)
{
    GameState s;
    s.m_provider          = &SpiritsProvU();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = post_combat ? Phase::PostCombatMain : Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_attack_count = true;     // StampDeckTraits' answer for a list holding Usher
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(UsZone("Plains", 950 + k)); }
    int n = 100;
    for (const std::string& l : lands) { UsPut(s, l, ++n); }
    return s;
}

int UsHumanTokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == 0 && p.is_token && CardHasSubtype(p.card, "Human")
            && CardHasSubtype(p.card, "Warrior")) { ++n; }
    }
    return n;
}

int UsBoasts(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::PayToken)
        { n += a.chosen_x; }
    }
    return n;
}

int UsMaxBoasts(const std::vector<TurnSolver::Plan>& plans)
{
    int m = 0;
    for (const TurnSolver::Plan& p : plans) { m = std::max(m, UsBoasts(p)); }
    return m;
}

const TurnSolver::Plan* UsPlanWith(const std::vector<TurnSolver::Plan>& plans, int boasts)
{
    for (const TurnSolver::Plan& p : plans) { if (UsBoasts(p) == boasts) { return &p; } }
    return nullptr;
}

void UsResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState UsRunExecutor(const GameState& s, const TurnSolver::Plan& plan, bool pre_combat)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, pre_combat);
    GameState exec = s;
    eng.TakeTurn(exec, pre_combat, UsResolveAll);
    return exec;
}

int UsIndex(const GameState& s, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_name.str() == name) { return i; } }
    return -1;
}

bool UsLive(const GameState& s, int idx)
{
    return PermAbilitySourceLive(s, 0, s.battlefield[static_cast<std::size_t>(idx)].card.m_number,
                                 PermAbilityMode::PayToken);
}
}   // namespace

TEST_CASE("Usher of the Fallen: the card -- {W} 2/1 Spirit Warrior; boast params")
{
    const CardDefinition& d = UsDef(kUsher);
    CHECK(d.card.m_mana_cost.ToString() == "{W}");
    CHECK(d.card.m_mana_cost.ManaValue() == 1);
    CHECK(d.card.IsCreature());
    CHECK(CardHasSubtype(d.card, "Spirit"));
    CHECK(CardHasSubtype(d.card, "Warrior"));
    CHECK(d.card.m_power.value_or(0) == 2);
    CHECK(d.card.m_toughness.value_or(0) == 1);
    REQUIRE(d.params.pay_token_cost.has_value());
    CHECK(d.params.pay_token_cost->ToString() == "{1}{W}");
    CHECK(d.params.pay_token_power == 1);
    CHECK(d.params.pay_token_toughness == 1);
    CHECK(d.params.pay_token_color == "W");
    CHECK(d.params.activate_only_if_attacked_this_turn);
    CHECK(d.params.activate_once_each_turn);
    // Slimefoot's {4} carries neither restriction (byte-identical for FungusB).
    CHECK_FALSE(UsDef("Slimefoot, the Stowaway").params.activate_only_if_attacked_this_turn);
    CHECK_FALSE(UsDef("Slimefoot, the Stowaway").params.activate_once_each_turn);
}

TEST_CASE("Usher boast gate: legal only after it attacked, and only once each turn")
{
    GameState s = UsBoard({ "Plains", "Plains" }, true);
    const int u = UsPut(s, kUsher, 1);
    CHECK_FALSE(UsLive(s, u));                       // did not attack
    s.battlefield[u].SetAttacksThisTurn(1);
    CHECK(UsLive(s, u));
    ApplyPermAbility(s, 0, 1, PermAbilityMode::PayToken);
    CHECK(UsHumanTokens(s) == 1);
    CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(UsIndex(s, kUsher))].once_per_turn_used));
    CHECK_FALSE(UsLive(s, UsIndex(s, kUsher)));   // once each turn
    // The token is a 1/1 white Human Warrior, NOT a Spirit.
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK_FALSE(CardHasSubtype(p.card, "Spirit"));
        CHECK(p.card.m_power.value_or(0) == 1);
        CHECK(p.card.m_toughness.value_or(0) == 1);
    }
}

TEST_CASE("Usher attack count: NoteAttacksDeclared counts ONLY a boast attacker, and only for a stamped deck")
{
    GameState s = UsBoard({}, false);
    const int u = UsPut(s, kUsher, 1);
    const int g = UsPut(s, "Supreme Phantom", 2);
    NoteAttacksDeclared(s, 0, { u, g });
    CHECK(s.battlefield[u].AttacksThisTurn() == 1);
    CHECK(s.battlefield[g].AttacksThisTurn() == 0);   // nothing reads it: never set (no key split)
    GameState t = UsBoard({}, false);
    t.deck_has_attack_count = false;
    const int u2 = UsPut(t, kUsher, 1);
    NoteAttacksDeclared(t, 0, { u2 });
    CHECK(t.battlefield[u2].AttacksThisTurn() == 0);
}

TEST_CASE("Usher boast is NEVER enumerated in main 1, even with {1}{W} up and the count somehow set")
{
    GameState s = UsBoard({ "Plains", "Plains", "Plains" }, false);
    UsPut(s, kUsher, 1);
    CHECK(UsMaxBoasts(TurnSolver::EnumerateMainPlans(s, true)) == 0);
}

TEST_CASE("Usher post-combat: an attacked Usher gets at most ONE boast; a sick one none; two attacked -> K in {0,1,2}")
{
    {
        GameState s = UsBoard({ "Plains", "Plains", "Plains", "Plains", "Plains" }, true);
        const int u = UsPut(s, kUsher, 1);
        s.battlefield[u].SetAttacksThisTurn(1);
        UsPut(s, kUsher, 2, /*sick=*/true);             // cast this turn: did not attack
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, false);
        CHECK(UsMaxBoasts(plans) == 1);
        // The PASS (K=0) is not an enumerated plan: every post-combat host appends it itself (FSLineTail's
        // do-nothing second main, SolveWithLookahead's post-combat Plan{}), so the count stays searched.
    }
    {
        GameState s = UsBoard({ "Plains", "Plains", "Plains", "Plains", "Plains" }, true);
        const int a = UsPut(s, kUsher, 1);
        const int b = UsPut(s, kUsher, 2);
        s.battlefield[a].SetAttacksThisTurn(1);
        s.battlefield[b].SetAttacksThisTurn(1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, false);
        CHECK(UsPlanWith(plans, 1) != nullptr);          // K=0 is the host-appended pass (above)
        CHECK(UsPlanWith(plans, 2) != nullptr);
        CHECK(UsMaxBoasts(plans) == 2);
    }
}

TEST_CASE("Usher both worlds: two boasts make two Human Warriors in ApplyPlan AND the executor; a K=2 block on ONE Usher fires once")
{
    GameState s = UsBoard({ "Plains", "Plains", "Plains", "Plains", "Plains" }, true);
    const int a = UsPut(s, kUsher, 1);
    const int b = UsPut(s, kUsher, 2);
    s.battlefield[a].SetAttacksThisTurn(1);
    s.battlefield[b].SetAttacksThisTurn(1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, false);
    const TurnSolver::Plan* two = UsPlanWith(plans, 2);
    REQUIRE(two != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *two, false);
    const GameState exec = UsRunExecutor(s, *two, false);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(UsHumanTokens(*g) == 2);
    }
    // A hand-built K=2 block on one Usher (never enumerated; a stale or foreign plan): the second
    // activation is refused by the shared gate in both worlds, and only {1}{W} is spent.
    TurnSolver::Plan k2;
    Action act;
    act.kind          = Action::Kind::ActivatePermAbility;
    act.card_name     = kUsher;
    act.def           = &UsDef(kUsher);
    act.hand_index    = -1;
    act.sac_source_id = 1;
    act.ability_mode  = Action::AbilityMode::PayToken;
    act.chosen_x      = 2;
    act.cost          = *UsDef(kUsher).params.pay_token_cost;
    act.is_noncreature = true;
    k2.actions.push_back(act);
    GameState roll2 = s;
    TurnSolver::ApplyPlan(roll2, k2, false);
    const GameState exec2 = UsRunExecutor(s, k2, false);
    for (const GameState* g : { static_cast<const GameState*>(&roll2), &exec2 })
    {
        INFO("world: " << std::string(g == &roll2 ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(UsHumanTokens(*g) == 1);
        int tapped = 0;
        for (const Permanent& p : g->battlefield) { if (p.card.IsLand() && p.tapped) { ++tapped; } }
        CHECK(tapped == 2);
    }
}

TEST_CASE("Usher's restricted second main: opens only for a live, payable boast; admits only boasts")
{
    GameState s = UsBoard({ "Plains" }, true);
    const int u = UsPut(s, kUsher, 1);
    s.players[0].hand.push_back(UsZone("Supreme Phantom", 30));
    CHECK_FALSE(BoastSecondMainLive(s));                 // not attacked
    s.battlefield[u].SetAttacksThisTurn(1);
    CHECK_FALSE(BoastSecondMainLive(s));                 // one land: {1}{W} unpayable
    UsPut(s, "Plains", 120);
    CHECK(BoastSecondMainLive(s));
    CHECK(ProviderSecondMainLive(s));                    // the deck plays no full second main
    CHECK(ResolveProvider(s).MayOpenRestrictedSecondMain());
    // The menu: the boast only -- the Phantom in hand was castable, and weakly better, in main 1.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, false);
    CHECK(UsMaxBoasts(plans) == 1);
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions) { CHECK(IsBoastActivation(a)); }
    }
    ApplyPermAbility(s, 0, 1, PermAbilityMode::PayToken);
    CHECK_FALSE(BoastSecondMainLive(s));                 // boasted: the phase closes
    GameState full = s;
    full.uses_second_main = true;
    CHECK_FALSE(ProviderSecondMainLive(full));           // never narrows a full second main
}

TEST_CASE("Usher payment: Secluded Courtyard's colour pays the boast's {W}; Unclaimed Territory's does not")
{
    for (const bool courtyard : { true, false })
    {
        INFO("land: " << (courtyard ? "Secluded Courtyard + Unclaimed Territory" : "Unclaimed Territory x2"));
        GameState s = UsBoard({ courtyard ? "Secluded Courtyard" : "Unclaimed Territory", "Unclaimed Territory" }, true);
        const int u = UsPut(s, kUsher, 1);
        s.battlefield[u].SetAttacksThisTurn(1);
        TurnSolver::Plan one;
        Action act;
        act.kind          = Action::Kind::ActivatePermAbility;
        act.card_name     = kUsher;
        act.def           = &UsDef(kUsher);
        act.hand_index    = -1;
        act.sac_source_id = 1;
        act.ability_mode  = Action::AbilityMode::PayToken;
        act.chosen_x      = 1;
        act.cost          = *UsDef(kUsher).params.pay_token_cost;
        act.is_noncreature = true;
        one.actions.push_back(act);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, one, false);
        const GameState exec = UsRunExecutor(s, one, false);
        for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
            CHECK(UsHumanTokens(*g) == (courtyard ? 1 : 0));
        }
    }
}
