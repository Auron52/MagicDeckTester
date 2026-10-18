// Unit tests for the Vampires cluster A2 (2026-10-18; docs/design/vampires/design_A2_outlets.md):
// Falkenrath Pit Fighter, Stromkirk Condemned, Voldaren Estate (+ the Blood token), Bloodghast.
//
// WHY THESE ARE UNIT TESTS. Almost every defect this cluster can have is SILENT: a discard pin missing
// from the plan signature collapses every discard to the first variant (a greedy pick inside the
// search window); a sacrifice outlet paid without the creature-ability scope cannot use Secluded
// Courtyard; a Blood priced at {5} when the plan's own Vampires made it {2} is never enumerated; a
// Bloodghast that misses its graveyard landfall just stays dead. None crashes and none diverges
// between the worlds -- they share the resolvers -- so only a direct test sees them.
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

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent unless a test opens it
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false, int ctrl = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = ctrl;
    p.owner_index       = ctrl;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountIn(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

int CountOnBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// GameEngine::MainPhase's shape: every cast resolves before the next.
void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

std::vector<Action> ActionsOf(const std::vector<TurnSolver::Plan>& plans, Action::Kind k)
{
    std::vector<Action> out;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (a.kind == k) { out.push_back(a); } } }
    return out;
}

}   // namespace

// ---- 1. card data ---------------------------------------------------------------------------------

TEST_CASE("Vampires A2: the five definitions load with their params")
{
    EnsureCardsLoaded();
    const CardParams& pf = Def("Falkenrath Pit Fighter").params;
    CHECK(pf.sac_creature_outlet);
    CHECK(pf.sac_outlet_discard_cost == 1);
    CHECK(pf.sac_outlet_draw == 2);
    CHECK(pf.activate_only_if_opp_lost_life);
    CHECK(pf.sac_creature_requires_subtype == "Vampire");
    CHECK(IsDiscardSacOutlet(pf));
    const CardParams& sc = Def("Stromkirk Condemned").params;
    CHECK(sc.discard_team_pump_power == 1);
    CHECK(sc.discard_team_pump_toughness == 1);
    CHECK(sc.activate_once_each_turn);
    CHECK_FALSE(sc.team_pump_cost.has_value());   // never the combat firebreathing converter's
    const CardParams& ve = Def("Voldaren Estate").params;
    REQUIRE(ve.tap_blood_cost.has_value());
    CHECK(ve.tap_blood_cost->generic == 5);
    CHECK(ve.tap_blood_cost_less_per_subtype == "Vampire");
    CHECK(ve.colored_creature_only);
    CHECK(ve.tap_self_damage == 1);
    REQUIRE(Def("Blood Token").params.blood_draw_cost.has_value());
    CHECK(Def("Blood Token").params.blood_draw_cost->generic == 1);
    const CardDefinition& bg = Def("Bloodghast");
    CHECK(bg.params.landfall_return_self_from_gy);
    CHECK(bg.params.haste_if_opp_life_at_most == 10);
    CHECK(bg.gy_landfall_watcher);
    CHECK_FALSE(bg.landfall_watcher);   // a Bloodghast on the battlefield watches nothing
}

// ---- 2. Voldaren Estate mana + the sac-outlet pay scope ----------------------------------------------

TEST_CASE("Voldaren Estate: coloured mana pays a creature SPELL (1 life), {C} is painless, abilities get {C} only")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Voldaren Estate", 1);
        ManaCost r; r.red = 1;
        CHECK(TapForCostDirect(s, r, /*for_creature=*/true));
        CHECK(s.players[0].life == gamesetup::StartingLife() - 1);
    }
    {
        GameState s = Fresh();
        Put(s, "Voldaren Estate", 1);
        ManaCost g; g.generic = 1;
        CHECK(TapForCostDirect(s, g, /*for_creature=*/false));
        CHECK(s.players[0].life == gamesetup::StartingLife());   // the {C} mode is free
    }
    {
        // Falkenrath Pit Fighter's {R} is an ABILITY: the Vampire-spell-only colour may not pay it.
        GameState s = Fresh();
        Put(s, "Voldaren Estate", 1);
        ManaCost r; r.red = 1;
        CreatureAbilityPayScope scope;
        CHECK_FALSE(TapForCostDirect(s, r, /*for_creature=*/false));
    }
    {
        // ...while Secluded Courtyard's coloured mana MAY pay a creature source's ability (design 0.4).
        GameState s = Fresh();
        Put(s, "Secluded Courtyard", 1);
        ManaCost r; r.red = 1;
        CreatureAbilityPayScope scope;
        CHECK(TapForCostDirect(s, r, /*for_creature=*/false));
    }
}

// ---- 3. Blood: cost, creation, crack ---------------------------------------------------------------

TEST_CASE("Voldaren Estate: the Blood cost is {5} minus our Vampires AT ACTIVATION, floor {0}")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Voldaren Estate", 1);
    Put(s, "Vampire of the Dire Moon", 2);
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Stromkirk Condemned", 4);
    CHECK(TapBloodCostNow(s, 0, Def("Voldaren Estate")).generic == 2);
    Put(s, "Vampire of the Dire Moon", 5);
    Put(s, "Vampire of the Dire Moon", 6);
    Put(s, "Vampire of the Dire Moon", 7);
    CHECK(TapBloodCostNow(s, 0, Def("Voldaren Estate")).generic == 0);
    Put(s, "Vampire of the Dire Moon", 8, false, /*ctrl=*/1);   // an opponent's Vampire is not "you control"
    CHECK(TapBloodCostNow(s, 0, Def("Voldaren Estate")).generic == 0);
}

TEST_CASE("Blood token: created by the Estate, crackable at once, discards the PINNED card, draws one")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Voldaren Estate", 1);
    Hand(s, "Swamp", 10);
    Hand(s, "Swamp", 11);
    Hand(s, "Bloodghast", 12);
    ApplyPermAbility(s, 0, 1, PermAbilityMode::TapBlood);
    REQUIRE(CountOnBf(s, "Blood Token") == 1);
    int blood = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Blood Token") { blood = p.card.m_number; } }
    // A pin naming a card that is not in hand: the activation is not live (decided before any cost).
    const std::string missing = "Stromkirk Condemned";
    CHECK_FALSE(PermAbilitySourceLive(s, 0, blood, PermAbilityMode::BloodDraw, &missing));
    const std::string pin = "Bloodghast";
    REQUIRE(PermAbilitySourceLive(s, 0, blood, PermAbilityMode::BloodDraw, &pin));
    const std::size_t hand_before = s.players[0].hand.size();
    {
        const ActivationDiscardPinScope scope(&pin);
        ApplyPermAbility(s, 0, blood, PermAbilityMode::BloodDraw);
    }
    CHECK(CountOnBf(s, "Blood Token") == 0);                       // sacrificed as a cost
    CHECK(CountIn(s.players[0].graveyard, "Bloodghast") == 1);    // the pinned card, not a Swamp
    CHECK(CountIn(s.players[0].hand, "Swamp") == 3);               // 2 kept + 1 drawn
    CHECK(s.players[0].hand.size() == hand_before);                // discard one, draw one
    CHECK(g_activation_discard_pin == nullptr);                    // the scope does not leak
}

TEST_CASE("Blood token: no card in hand -> not live (the discard is part of the cost)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Voldaren Estate", 1);
    ApplyPermAbility(s, 0, 1, PermAbilityMode::TapBlood);
    int blood = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Blood Token") { blood = p.card.m_number; } }
    REQUIRE(blood != 0);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, blood, PermAbilityMode::BloodDraw));
}

TEST_CASE("Voldaren Estate (search): 'cast the 5th Vampire, THEN make Blood for {0}' is enumerated and realised")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Voldaren Estate", 1);
    Put(s, "Swamp", 2);
    Put(s, "Vampire of the Dire Moon", 3);
    Put(s, "Vampire of the Dire Moon", 4);
    Put(s, "Legion Lieutenant", 5);
    Put(s, "Stromkirk Condemned", 6);
    Hand(s, "Vampire of the Dire Moon", 10);
    // Four Vampires: the Blood costs {1} before the cast and {0} after it. Two lands: the Swamp pays the
    // Dire Moon and the Estate must keep its {T} for the Blood -- only the k=1 cost variant prices this
    // line within the board's mana, and only the {T} hold keeps the payment off the Estate.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool cast = false, blood = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Vampire of the Dire Moon") { cast = true; }
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapBlood)
            { blood = true; }
        }
        if (cast && blood) { line = &p; break; }
    }
    REQUIRE(line != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(roll, "Blood Token") == 1);
    CHECK(CountOnBf(roll, "Vampire of the Dire Moon") == 3);
    // Exactly one priced variant survives per subset: no plan pairs the cast with the over-priced k=0 one.
    for (const TurnSolver::Plan& p : plans)
    {
        bool cast = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Vampire of the Dire Moon") { cast = true; } }
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::ActivatePermAbility || a.ability_mode != Action::AbilityMode::TapBlood) { continue; }
            CHECK(a.cost.ManaValue() == (cast ? 0 : 1));
        }
    }
}

// ---- 3b. Defect M (2026-10-18): the Blood is priced off the arrivals AHEAD of it in the executed order --
// The claude-play confirmation sweep (seeds 92003 gi5 T5, 92004 gi6 T6): the cost variants counted only the
// plan's Vampire CASTS, so a Bloodline Keeper token tap -- which the trailing pass runs ahead of the Blood
// (rank 0 vs rank 1) -- never lowered the booked cost, and every line affordable only at the true cost was
// missing from the search and the human menu alike.

namespace
{
bool IsBlood(const Action& a)
{ return a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapBlood; }

// The Blood cost a plan's own actions book in the SHIPPED trailing order: {5} - (Vampires now + its
// Vampire casts / Vial puts + its Vampire token taps), floor {0}. The cast section runs before the trailing
// pass and Keeper's token tap (rank 0) ahead of Blood (rank 1); a madness cast (the pump's rank 2, the
// crack's 3), a steal (count band 1) and Pit Fighter (band 3) all run AFTER it.
int ExpectedBloodCost(const GameState& s, const TurnSolver::Plan& p)
{
    int v = CountControlledSubtypeCreatures(s, 0, "Vampire");
    for (const Action& a : p.actions)
    {
        const CardDefinition* d = a.def != nullptr ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if ((a.kind == Action::Kind::CastFromHand || a.kind == Action::Kind::ActivateVial)
            && d != nullptr && d->card.IsCreature() && CardHasSubtype(d->card, "Vampire")) { ++v; }
        if (a.kind == Action::Kind::TapForTokenPay) { ++v; }
    }
    return std::max(0, 5 - v);
}

int VampireTokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& q : s.battlefield)
    { if (q.is_token && q.card.IsCreature() && CardHasSubtype(q.card, "Vampire")) { ++n; } }
    return n;
}

// The rollout's ApplyPlan and the executor (the committed-line pop) must realise `line` alike. The Blood
// made mid-pass opens breakpoint site 9 (a crackable Blood); pin the EMPTY arm, which both worlds honour,
// so the comparison is exactly the plan's own actions (the lockstep test's discipline, section 8).
void CheckBothWorlds(const GameState& s, const TurnSolver::Plan& line0, bool pre,
                     const std::function<void(const GameState&, const char*)>& check)
{
    TurnSolver::Plan line = line0;
    if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, pre);
    check(roll, "rollout");
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    if (!pre) { eng.SetSearchPostCombat(true); }
    MtgTestSeam::CommitPhasePlan(eng, line, pre);
    GameState exec = s;
    eng.TakeTurn(exec, pre, ResolveAll);
    check(exec, "executor");
}
}   // namespace

TEST_CASE("Defect M: a Keeper token tap AHEAD of the Blood lowers its cost -- 'Captain, token, Blood for {0}' is offered and realised")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 11);       // unsick: its {T} token is live
    Put(s, "Legion Lieutenant", 24);
    Put(s, "Falkenrath Gorger", 16);      // three Vampires: the Blood costs {2}
    Put(s, "Voldaren Estate", 58);
    Put(s, "Blackcleave Cliffs", 7);
    Put(s, "Swamp", 44);
    Put(s, "Swamp", 45);
    Hand(s, "Stromkirk Captain", 37);     // {1}{B}{R} takes all three lands; the Estate keeps its {T}
    // Captain + the token = five Vampires: the Blood is {0} -- the ONLY price at which the line fits the
    // board. Before the fix the variants stopped at k = 1 (one Vampire card in hand), booked it at {1},
    // and the line was never enumerated.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    int blood_plans = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        bool cap = false, tok = false, blood = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Stromkirk Captain") { cap = true; }
            if (a.kind == Action::Kind::TapForTokenPay) { tok = true; }
            if (IsBlood(a))
            {
                blood = true;
                ++blood_plans;
                // Exactly one priced variant per subset, and it is the one the executed order books.
                CHECK(a.cost.ManaValue() == ExpectedBloodCost(s, p));
            }
        }
        if (cap && tok && blood && line == nullptr) { line = &p; }
    }
    CHECK(blood_plans > 0);
    REQUIRE_MESSAGE(line != nullptr, "'Captain, Keeper token, Blood for {0}' was not offered (defect M)");
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        CHECK(CountOnBf(st, "Blood Token") == 1);
        CHECK(CountOnBf(st, "Stromkirk Captain") == 1);
        CHECK(VampireTokens(st) == 1);
        REQUIRE(FindNum(st, 58) != nullptr);
        CHECK(FindNum(st, 58)->tapped);   // the Estate's {T}
        CHECK(FindNum(st, 11)->tapped);   // Keeper's {T}
    });
}

TEST_CASE("Defect M: 'Keeper token, Blood, Blood, transform' -- the token prices BOTH Estates at {0} (seed 92004 T6 shape)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 11);
    Put(s, "Vampire of the Dire Moon", 3);
    Put(s, "Legion Lieutenant", 24);
    Put(s, "Falkenrath Gorger", 16);      // four Vampires: each Blood costs {1}
    Put(s, "Swamp", 44);
    Put(s, "Secluded Courtyard", 33);
    Put(s, "Voldaren Estate", 58);
    Put(s, "Voldaren Estate", 59);
    // Two lands besides the Estates. {1}{1}{B} never fit; with the token first the Bloods are {0}{0} and
    // the transform's {B} (five Vampires, its count band after the Bloods) is the whole bill.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        int bloods = 0;
        bool tok = false, xf = false;
        for (const Action& a : p.actions)
        {
            if (IsBlood(a)) { ++bloods; CHECK(a.cost.ManaValue() == ExpectedBloodCost(s, p)); }
            if (a.kind == Action::Kind::TapForTokenPay) { tok = true; }
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TransformSelf)
            { xf = true; }
        }
        if (bloods == 2 && tok && xf && line == nullptr) { line = &p; }
    }
    REQUIRE_MESSAGE(line != nullptr, "'token, Blood, Blood, transform' was not offered (defect M)");
    for (const Action& a : line->actions) { if (IsBlood(a)) { CHECK(a.cost.ManaValue() == 0); } }
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        CHECK(CountOnBf(st, "Blood Token") == 2);
        CHECK(CountOnBf(st, "Lord of Lineage") == 1);
        CHECK(CountOnBf(st, "Bloodline Keeper") == 0);
        CHECK(VampireTokens(st) == 1);
        CHECK(FindNum(st, 58)->tapped);
        CHECK(FindNum(st, 59)->tapped);
    });
}

TEST_CASE("Defect M (item 2): the land drop's Bloodghast is already in the Blood's base count -- a land-first plan is enumerated post-drop")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_landfall = true;
    s.players[0].lands_played_this_turn = 0;
    Put(s, "Vampire of the Dire Moon", 3);
    Put(s, "Legion Lieutenant", 24);      // two Vampires on the battlefield
    Put(s, "Voldaren Estate", 58);
    Put(s, "Blackcleave Cliffs", 7);
    { Card bg = Def("Bloodghast").card; bg.m_number = 5; s.players[0].graveyard.push_back(bg); }
    Hand(s, "Swamp", 44);
    Hand(s, "Vampire of the Dire Moon", 10);
    // Swamp first -> Bloodghast returns (three) -> the Dire Moon (four) -> Blood {1}: {B} + {1} is exactly
    // the Cliffs and the Swamp. Without the returned Bloodghast the Blood would be {2} and the line short.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play != "Swamp" || p.land_slot != TurnSolver::kLandSlotFirst) { continue; }
        bool dm = false, blood = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Vampire of the Dire Moon") { dm = true; } }
        for (const Action& a : p.actions)
        { if (IsBlood(a)) { blood = true; CHECK(a.cost.ManaValue() == (dm ? 1 : 2)); } }   // Bloodghast counted
        if (dm && blood && line == nullptr) { line = &p; }
    }
    REQUIRE(line != nullptr);
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        CHECK(CountOnBf(st, "Bloodghast") == 1);
        CHECK(CountOnBf(st, "Blood Token") == 1);
        CHECK(CountOnBf(st, "Vampire of the Dire Moon") == 2);
    });
}

// ---- 4. Falkenrath Pit Fighter ----------------------------------------------------------------------

TEST_CASE("Pit Fighter: not live before an opponent lost life; live after; pinned discard + baked victim + draw two")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pf  = Put(s, "Falkenrath Pit Fighter", 1);
    const int bg  = Put(s, "Bloodghast", 2);
    Hand(s, "Swamp", 10);
    Hand(s, "Stromkirk Condemned", 11);
    CHECK_FALSE(SacOutletLive(s, 0, pf, bg, std::string(), "Swamp"));
    s.opponent_lost_life_this_turn = true;
    CHECK(SacOutletLive(s, 0, pf, bg, std::string(), "Swamp"));
    CHECK_FALSE(SacOutletLive(s, 0, pf, bg, std::string(), "Bloodghast"));   // pin not in hand
    CHECK_FALSE(SacOutletLive(s, 0, pf, 77, std::string(), "Swamp"));        // victim gone
    REQUIRE(ApplyDiscardSacOutlet(s, 0, pf, bg, std::string(), "Swamp"));
    CHECK(FindNum(s, bg) == nullptr);
    CHECK(FindNum(s, pf) != nullptr);
    CHECK(CountIn(s.players[0].graveyard, "Bloodghast") == 1);
    CHECK(CountIn(s.players[0].graveyard, "Swamp") == 1);
    CHECK(CountIn(s.players[0].hand, "Stromkirk Condemned") == 1);
    CHECK(s.players[0].hand.size() == 3);   // 2 - 1 discarded + 2 drawn
}

TEST_CASE("Pit Fighter: it may sacrifice ITSELF (no 'another'), and the draw still happens")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pf = Put(s, "Falkenrath Pit Fighter", 1);
    Hand(s, "Swamp", 10);
    s.opponent_lost_life_this_turn = true;
    REQUIRE(ApplyDiscardSacOutlet(s, 0, pf, pf, std::string(), "Swamp"));
    CHECK(CountOnBf(s, "Falkenrath Pit Fighter") == 0);
    CHECK(s.players[0].hand.size() == 2);
}

TEST_CASE("Pit Fighter (enumeration): victims fold by identity class, discards by name, both searched")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Falkenrath Pit Fighter", 1);
    Put(s, "Vampire of the Dire Moon", 2);
    Put(s, "Vampire of the Dire Moon", 3);              // identical to #2: ONE victim class
    const int tapped = Put(s, "Vampire of the Dire Moon", 4);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == tapped) { p.tapped = true; } }   // a different class
    Put(s, "Swamp", 50);
    Put(s, "Blackcleave Cliffs", 51);
    Put(s, "Secluded Courtyard", 52);
    Hand(s, "Swamp", 10);
    Hand(s, "Swamp", 11);                               // two Swamps: ONE discard class
    Hand(s, "Stromkirk Condemned", 12);                 // {B}{B}: castable off Swamp + Courtyard
    s.opponent_lost_life_this_turn = true;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    std::set<std::pair<int, std::string>> board_pairs;
    std::set<std::pair<std::string, std::string>> plan_pairs;
    for (const Action& a : ActionsOf(plans, Action::Kind::SacCreatureOutlet))
    {
        CHECK_FALSE(a.discard_name.empty());           // autonomous: always pinned
        if (a.victim_name.empty()) { board_pairs.insert({a.sac_victim_id, a.discard_name.str()}); }
        else { plan_pairs.insert({a.victim_name.str(), a.discard_name.str()}); }
    }
    std::set<int> victims;
    std::set<std::string> discards;
    for (const auto& pr : board_pairs) { victims.insert(pr.first); discards.insert(pr.second); }
    CHECK(victims.count(1) == 1);                                       // the source itself
    CHECK((victims.count(2) + victims.count(3)) == 1);                  // the two twins folded
    CHECK(victims.count(4) == 1);                                       // the tapped one is distinct
    CHECK(discards.count("Swamp") == 1);
    CHECK(discards.count("Stromkirk Condemned") == 1);
    // A same-plan victim: the hand Stromkirk Condemned cast by the plan, then sacrificed. Its discard
    // can never be the Condemned itself (one copy -- SubsetVampOutletInvalid rule 1). (With {1}{R} +
    // {B}{B} the line needs four mana: three lands here, so it is emitted as an ACTION but no plan can
    // afford it -- check the plan set instead with a fourth land below.)
    (void)plan_pairs;
    Put(s, "Swamp", 53);
    const std::vector<TurnSolver::Plan> plans4 = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    std::set<std::pair<std::string, std::string>> plan_pairs4;
    for (const Action& a : ActionsOf(plans4, Action::Kind::SacCreatureOutlet))
    { if (!a.victim_name.empty()) { plan_pairs4.insert({a.victim_name.str(), a.discard_name.str()}); } }
    CHECK(plan_pairs4.count({"Stromkirk Condemned", "Swamp"}) == 1);
    CHECK(plan_pairs4.count({"Stromkirk Condemned", "Stromkirk Condemned"}) == 0);
    // ...and every surviving same-plan-victim plan casts that victim.
    for (const TurnSolver::Plan& p : plans4)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::SacCreatureOutlet || a.victim_name.empty()) { continue; }
            bool cast = false;
            for (const Action& c : p.actions)
            { if (c.kind == Action::Kind::CastFromHand && c.card_name == a.victim_name) { cast = true; } }
            CHECK(cast);
        }
    }
    // Every surviving plan that activates it does so ONCE (one family per plan).
    for (const TurnSolver::Plan& p : plans)
    {
        int n = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::SacCreatureOutlet) { ++n; } }
        CHECK(n <= 1);
    }
}

TEST_CASE("Pit Fighter (enumeration): nothing in the PRE-combat main (no life lost yet)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Falkenrath Pit Fighter", 1);
    Put(s, "Swamp", 50);
    Put(s, "Blackcleave Cliffs", 51);
    Hand(s, "Swamp", 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(ActionsOf(plans, Action::Kind::SacCreatureOutlet).empty());
}

// ---- 5. Stromkirk Condemned ---------------------------------------------------------------------------

TEST_CASE("Condemned: +1/+1 to the Vampires present at resolution, once per turn per copy")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int sc  = Put(s, "Stromkirk Condemned", 1, /*sick=*/true);   // legal while summoning sick
    const int vd  = Put(s, "Vampire of the Dire Moon", 2);
    const int opp = Put(s, "Vampire of the Dire Moon", 3, false, /*ctrl=*/1);
    Hand(s, "Swamp", 10);
    Hand(s, "Swamp", 11);
    REQUIRE(DiscardTeamPumpLive(s, 0, sc, "Swamp"));
    CHECK(ApplyActivatePump(s, 0, sc, 4, 1, "Swamp") == 1);
    CHECK(FindNum(s, sc)->temp_power_bonus == 1);
    CHECK(FindNum(s, sc)->temp_tough_bonus == 1);
    CHECK(FindNum(s, vd)->temp_power_bonus == 1);
    CHECK(FindNum(s, opp)->temp_power_bonus == 0);                 // "you control"
    CHECK(FindNum(s, sc)->once_per_turn_used);
    CHECK(CountIn(s.players[0].graveyard, "Swamp") == 1);
    // Once each turn: the second activation is not live, and the apply refuses it.
    CHECK_FALSE(DiscardTeamPumpLive(s, 0, sc, "Swamp"));
    CHECK(ApplyActivatePump(s, 0, sc, 4, 1, "Swamp") == 0);
    CHECK(FindNum(s, vd)->temp_power_bonus == 1);
    // A Vampire that arrives LATER gets nothing (the affected set is fixed at resolution).
    const int late = Put(s, "Vampire of the Dire Moon", 5);
    CHECK(FindNum(s, late)->temp_power_bonus == 0);
}

TEST_CASE("Condemned: a used copy and an unused one are NOT interchangeable (fold keys)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Stromkirk Condemned", 2);
    std::uint64_t k1 = 0, k2 = 0;
    REQUIRE(LandfallTargetFoldKey(s, s.battlefield[0], k1));
    REQUIRE(LandfallTargetFoldKey(s, s.battlefield[1], k2));
    CHECK(k1 == k2);
    s.battlefield[0].once_per_turn_used = true;
    CHECK_FALSE(LandfallTargetFoldKey(s, s.battlefield[0], k1));   // used: never folded
    CHECK(RemovalSelfTargetFoldKey(s, s.battlefield[0]) != RemovalSelfTargetFoldKey(s, s.battlefield[1]));
}

TEST_CASE("Condemned (enumeration): one variant per discard NAME, two identical copies keep one assignment per multiset")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Stromkirk Condemned", 2);
    Put(s, "Vampire of the Dire Moon", 3);
    Hand(s, "Swamp", 10);
    Hand(s, "Legion Lieutenant", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    std::set<std::string> pins;
    for (const Action& a : ActionsOf(plans, Action::Kind::ActivatePump))
    { CHECK(a.gy_exile_mode == 4); pins.insert(a.discard_name.str()); }
    CHECK(pins.count("Swamp") == 1);
    CHECK(pins.count("Legion Lieutenant") == 1);
    // Plans activating BOTH copies: {Swamp, Lieutenant} appears once (not once per assignment), and a
    // double pin of one name with ONE copy in hand never appears (rule 1).
    int both_mixed = 0, both_swamp = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        std::vector<std::string> d;
        bool casts_lt = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePump) { d.push_back(a.discard_name.str()); }
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Legion Lieutenant") { casts_lt = true; }
        }
        if (d.size() != 2 || casts_lt) { continue; }
        std::sort(d.begin(), d.end());
        if (d[0] != d[1]) { ++both_mixed; } else if (d[0] == "Swamp") { ++both_swamp; }
    }
    CHECK(both_mixed == 1);
    CHECK(both_swamp == 0);
}

// ---- 6. Bloodghast ---------------------------------------------------------------------------------

TEST_CASE("Bloodghast: returns on a land entering ONLY from the graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_landfall = true;
    Card bg = Def("Bloodghast").card; bg.m_number = 5;
    s.players[0].graveyard.push_back(bg);
    const int land = Put(s, "Swamp", 60);
    OnLandEntered(s, 0, land);
    CHECK(CountIn(s.players[0].graveyard, "Bloodghast") == 0);
    REQUIRE(FindNum(s, 5) != nullptr);
    CHECK(FindNum(s, 5)->entered_this_turn);
    CHECK_FALSE(FindNum(s, 5)->tapped);
    // On the battlefield it watches nothing: a second land changes nothing.
    const int land2 = Put(s, "Swamp", 61);
    OnLandEntered(s, 0, land2);
    CHECK(CountOnBf(s, "Bloodghast") == 1);
}

TEST_CASE("Bloodghast (human): the `revive` decision can decline -- it stays in the graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_landfall = true;
    Card bg = Def("Bloodghast").card; bg.m_number = 5;
    s.players[0].graveyard.push_back(bg);
    int asked = 0;
    ReviveChooser decline = [&asked](const GameState&, int, const std::string& src,
                                     const std::vector<Card>& cands, int, const std::vector<int>& heur)
    {
        ++asked;
        CHECK(cands.size() == 1);
        CHECK(heur.size() == 1);           // preselected: YES
        CHECK(src.find("Bloodghast") != std::string::npos);
        return std::vector<int>{};
    };
    ReviveChooser* saved = g_play_revive_chooser;
    g_play_revive_chooser = &decline;
    const int land = Put(s, "Swamp", 60);
    OnLandEntered(s, 0, land);
    g_play_revive_chooser = saved;
    CHECK(asked == 1);
    CHECK(CountIn(s.players[0].graveyard, "Bloodghast") == 1);
}

TEST_CASE("Bloodghast: haste while the opponent has 10 or less life (entry and beginning of combat)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.players[1].life = 11;
    s.deck_has_landfall = true;
    Card bg = Def("Bloodghast").card; bg.m_number = 5;
    s.players[0].graveyard.push_back(bg);
    OnLandEntered(s, 0, Put(s, "Swamp", 60));
    REQUIRE(FindNum(s, 5) != nullptr);
    CHECK_FALSE(CanAttackFull(*FindNum(s, 5), s.battlefield, 0));   // 11 life: sick
    s.players[1].life = 10;
    RefreshOppLifeHaste(s);                                          // the beginning-of-combat refresh
    CHECK(CanAttackFull(*FindNum(s, 5), s.battlefield, 0));
    s.players[1].life = 11;
    RefreshOppLifeHaste(s);
    CHECK_FALSE(CanAttackFull(*FindNum(s, 5), s.battlefield, 0));   // the bit is CLEARED too
}

// ---- 7. search: the land after the activations, the held main-2 land ----------------------------------

TEST_CASE("Search: a plan that discards Bloodghast is offered 'land after the activations'")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_landfall = true;
    s.players[0].lands_played_this_turn = 0;
    Put(s, "Stromkirk Condemned", 1);
    Hand(s, "Bloodghast", 10);
    Hand(s, "Swamp", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool found = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_slot != TurnSolver::kLandSlotAfterCasts || p.land_to_play != "Swamp") { continue; }
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivatePump && a.discard_name.str() == "Bloodghast") { found = true; } }
    }
    CHECK(found);
    // ...and applying it returns Bloodghast (the trailing pass runs before the deferred land).
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_slot != TurnSolver::kLandSlotAfterCasts || p.land_to_play != "Swamp") { continue; }
        bool pins_bg = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivatePump && a.discard_name.str() == "Bloodghast") { pins_bg = true; } }
        if (!pins_bg) { continue; }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(CountOnBf(roll, "Bloodghast") == 1);
        CHECK(CountIn(roll.players[0].graveyard, "Bloodghast") == 0);
        break;
    }
}

TEST_CASE("Search: a HELD land is offered in main 2 only while a Bloodghast is (or can be put) in the graveyard")
{
    EnsureCardsLoaded();
    auto main2_swamp_plans = [](bool bg_in_gy)
    {
        GameState s = Fresh();
        s.deck_has_landfall = true;
        s.players[0].lands_played_this_turn = 0;
        Put(s, "Vampire of the Dire Moon", 2);
        Hand(s, "Swamp", 11);
        s.StampMain1Hand();                       // the Swamp was in hand as main 1 began
        if (bg_in_gy) { Card bg = Def("Bloodghast").card; bg.m_number = 5; s.players[0].graveyard.push_back(bg); }
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        { if (p.land_to_play == "Swamp") { ++n; } }
        return n;
    };
    CHECK(main2_swamp_plans(false) == 0);   // the USER rule: no held land in main 2
    CHECK(main2_swamp_plans(true) > 0);     // the narrow exemption (MTG_VAMP_GY_LANDFALL_HELD_LAND)
}

// ---- 8. lockstep: rollout ApplyPlan vs the executor ---------------------------------------------------

TEST_CASE("Lockstep: every main-2 Pit Fighter / Blood / Condemned plan realises the same state in both worlds")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Falkenrath Pit Fighter", 1);
    Put(s, "Stromkirk Condemned", 2);
    Put(s, "Bloodghast", 3);
    Put(s, "Voldaren Estate", 50);
    Put(s, "Blackcleave Cliffs", 51);
    Put(s, "Swamp", 52);
    Put(s, "Secluded Courtyard", 53);
    Hand(s, "Swamp", 10);
    Hand(s, "Vampire of the Dire Moon", 11);
    Hand(s, "Legion Lieutenant", 12);
    s.opponent_lost_life_this_turn = true;
    s.players[0].lands_played_this_turn = 0;
    s.deck_has_landfall = true;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    int checked = 0;
    for (const TurnSolver::Plan& line0 : plans)
    {
        // The draw of Pit Fighter / the Blood crack opens a hand-entry breakpoint. In real play the
        // executor REPLAYS the continuation the search recorded; a raw enumerated plan carries none,
        // so the executor would re-solve it with its own search while the rollout takes its default --
        // a harness artifact, not a lockstep property. Pin the EMPTY arm at every breakpoint, which
        // both worlds honour, so the comparison is exactly the plan's own actions.
        TurnSolver::Plan line = line0;
        if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
        bool interesting = false;
        for (const Action& a : line.actions)
        {
            if (a.kind == Action::Kind::SacCreatureOutlet || a.kind == Action::Kind::ActivatePump
                || a.kind == Action::Kind::ActivatePermAbility) { interesting = true; }
        }
        if (!interesting) { continue; }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/false);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        eng.SetSearchPostCombat(true);   // the deck uses the second main (DeckUsesSecondMain)
        MtgTestSeam::CommitPhasePlan(eng, line, false);
        GameState exec = s;
        eng.TakeTurn(exec, false, ResolveAll);
        std::string desc;
        for (const Action& a : line.actions)
        { desc += a.card_name.str() + "/k" + std::to_string(static_cast<int>(a.kind)) + "/m"
                + std::to_string(static_cast<int>(a.ability_mode)) + "/d" + a.discard_name.str() + " "; }
        std::string rb, eb;
        for (const Permanent& p : roll.battlefield) { rb += p.card.m_name.str() + (p.tapped ? "(T) " : " "); }
        for (const Permanent& p : exec.battlefield) { eb += p.card.m_name.str() + (p.tapped ? "(T) " : " "); }
        INFO("plan " << checked << " [" << desc << "] land=" << line.land_to_play << " slot="
             << static_cast<int>(line.land_slot) << "\n roll: " << rb << "\n exec: " << eb);
        CHECK(roll.battlefield.size() == exec.battlefield.size());
        for (const Permanent& p : roll.battlefield)
        {
            const Permanent* q = FindNum(exec, p.card.m_number);
            if (p.card.m_number >= 1000) { continue; }   // tokens: numbered in creation order
            REQUIRE(q != nullptr);
            CHECK(q->tapped == p.tapped);
            CHECK(q->temp_power_bonus == p.temp_power_bonus);
            CHECK(q->once_per_turn_used == p.once_per_turn_used);
        }
        CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
        CHECK(roll.players[0].graveyard.size() == exec.players[0].graveyard.size());
        CHECK(roll.players[0].life == exec.players[0].life);
        ++checked;
        if (checked >= 40) { break; }
    }
    CHECK(checked > 0);
    CHECK(g_activation_discard_unpinned.load() == 0);
}

// ---- 9. routing ------------------------------------------------------------------------------------

TEST_CASE("Vampires routes to its own provider, which derives from Vial")
{
    EnsureCardsLoaded();
    const DecisionProvider& p = SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    CHECK(std::string(p.Name()) == "Vampires");
    CHECK(dynamic_cast<const VialProvider*>(&p) != nullptr);
}
