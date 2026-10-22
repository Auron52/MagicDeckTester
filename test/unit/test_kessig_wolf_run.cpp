// Unit tests for KESSIG WOLF RUN (2026-10-22): "{T}: Add {C}. {X}{R}{G}, {T}: Target creature gets +X/+0
// and gains trample until end of turn." -- PermAbilityMode::TapTargetPump (CardParams::tap_target_pump_*),
// and its LandfallProvider integration (the pump folds, the Primeval Titan pick, the Karoo bounce).
//
//  1. CARD DATA: the printed cost parses with its {X}; the label is read off the params; the cost at X.
//  2. AFFORDABILITY: the land taps as part of the cost, so its own {C} never pays; the R and the G come
//     from OTHER sources; X = 0 is never offered autonomously (dominated by not activating).
//  3. SEARCHED CHOICES (base provider): one plan variant per (target identity class, X), every X.
//  4. THE APPLY: +X/+0 on the PINNED target, the land and X+2 other mana tapped, the pump reaches combat
//     damage, gone at cleanup; a target that left is a clean no-op (no substitution).
//  5. LANDFALL FOLDS: attack-eligible targets in main 1 only, one outcome class, X = all remaining mana,
//     run LAST in the trailing pass.
//  6. THE TITAN PICK (USER 2026-10-22): Wolf Run pairs admitted and searched, first with no other threat.
//  7. THE KAROO BOUNCE / FETCH COLOURS: Wolf Run is colourless, ranked last among the colourless lands.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr const char* kWolfRun = "Kessig Wolf Run";

void LoadKwr()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Dk(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshKwr()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int PutK(GameState& s, const std::string& name, int number, bool tapped = false, bool sick = false)
{
    Permanent p;
    p.card              = Dk(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void LibK(GameState& s, const std::string& name, int number)
{
    Card c = Dk(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

void HandK(GameState& s, const std::string& name, int number)
{
    Card c = Dk(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

const Permanent* ByNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool IsPump(const Action& a)
{
    return a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTargetPump;
}

// Every distinct (target number, X) the enumerated plans carry for the pump.
std::vector<std::pair<int, int>> PumpVariants(const std::vector<TurnSolver::Plan>& plans)
{
    std::vector<std::pair<int, int>> out;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (!IsPump(a)) { continue; }
            const std::pair<int, int> v{ a.sac_victim_id, a.chosen_x };
            if (std::find(out.begin(), out.end(), v) == out.end()) { out.push_back(v); }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The plan whose ONLY action is the pump at (target, x).
const TurnSolver::Plan* PumpOnlyPlan(const std::vector<TurnSolver::Plan>& plans, int target, int x)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1 || !IsPump(p.actions[0])) { continue; }
        if (p.actions[0].sac_victim_id == target && p.actions[0].chosen_x == x) { return &p; }
    }
    return nullptr;
}

int TappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

const LandfallProvider& LfProvK()
{
    static const LandfallProvider p;
    return p;
}

}   // namespace

// ---- 1. CARD DATA ----------------------------------------------------------------------------------------

TEST_CASE("Kessig Wolf Run: the printed {X}{R}{G} parses, the label reads the params, the cost at X folds X into generic")
{
    LoadKwr();
    const CardDefinition& d = Dk(kWolfRun);
    REQUIRE(d.params.tap_target_pump_cost.has_value());
    const ManaCost& c = *d.params.tap_target_pump_cost;
    CHECK(c.has_x);
    CHECK(c.x_pips == 1);
    CHECK(c.red == 1);
    CHECK(c.green == 1);
    CHECK(c.generic == 0);
    CHECK(d.params.tap_target_pump_power == 1);
    CHECK(d.params.tap_target_pump_tough == 0);
    CHECK(d.params.tap_target_pump_grants_trample);        // the trample flag: recorded (structurally inert)
    REQUIRE(d.params.produces.size() == 1);
    CHECK(d.params.produces.front() == Color::Colorless);   // {T}: Add {C}
    CHECK(d.card.IsLand());
    CHECK(PermAbilityTaps(PermAbilityMode::TapTargetPump)); // {T} is part of the cost
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapTargetPump, &d)
          == "target creature gets +X/+0 and gains trample until end of turn");
    const ManaCost at3 = TapTargetPumpCostAt(d, 3);
    CHECK(at3.generic == 3);
    CHECK(at3.red == 1);
    CHECK(at3.green == 1);
    CHECK_FALSE(at3.has_x);
    CHECK(TapTargetPumpPowerAt(d, 3) == 3);
    CHECK(TapTargetPumpToughAt(d, 3) == 0);
}

// ---- 2. AFFORDABILITY --------------------------------------------------------------------------------------

TEST_CASE("Kessig Wolf Run: its own {C} never pays; the R and the G come from other sources; X = 0 is not offered")
{
    LoadKwr();
    {   // Mountain + Forest: the other sources pay {R}{G} and nothing more -> X = 0 only -> no autonomous pump.
        GameState s = FreshKwr();
        PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11); PutK(s, "Forest", 12);
        PutK(s, "Vexing Shusher", 1);
        CHECK(PumpVariants(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
    {   // + a Forest: X = 1, exactly one variant (the Wolf Run's own {C} is NOT the third mana).
        GameState s = FreshKwr();
        PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11); PutK(s, "Forest", 12); PutK(s, "Forest", 13);
        PutK(s, "Vexing Shusher", 1);
        const std::vector<std::pair<int, int>> v = PumpVariants(TurnSolver::EnumerateMainPlans(s, true));
        REQUIRE(v.size() == 1);
        CHECK(v[0] == std::pair<int, int>{ 1, 1 });
    }
    {   // No red source at all: the {R} cannot be paid, whatever the total.
        GameState s = FreshKwr();
        PutK(s, kWolfRun, 10);
        for (int i = 0; i < 5; ++i) { PutK(s, "Forest", 11 + i); }
        PutK(s, "Vexing Shusher", 1);
        CHECK(PumpVariants(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
    {   // A TAPPED Wolf Run has no activation (its {T} is spent).
        GameState s = FreshKwr();
        PutK(s, kWolfRun, 10, /*tapped=*/true);
        PutK(s, "Mountain", 11);
        for (int i = 0; i < 4; ++i) { PutK(s, "Forest", 12 + i); }
        PutK(s, "Vexing Shusher", 1);
        CHECK(PumpVariants(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
}

// ---- 3. SEARCHED CHOICES (base provider) ----------------------------------------------------------------

TEST_CASE("Kessig Wolf Run: every X and every target identity class is its own plan variant (base provider)")
{
    LoadKwr();
    GameState s = FreshKwr();
    PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11);
    for (int i = 0; i < 4; ++i) { PutK(s, "Forest", 12 + i); }
    PutK(s, "Vexing Shusher", 1);
    PutK(s, "Vexing Shusher", 2);   // identical to #1: one identity class (LandfallTargetFoldKey)
    PutK(s, "Boros Reckoner", 3);
    // Others pay 5 (R + 4 G): X = 3, 2, 1 at each of the two classes.
    const std::vector<std::pair<int, int>> v = PumpVariants(TurnSolver::EnumerateMainPlans(s, true));
    const std::vector<std::pair<int, int>> want{ { 1, 1 }, { 1, 2 }, { 1, 3 }, { 3, 1 }, { 3, 2 }, { 3, 3 } };
    CHECK(v == want);
    // A plan holds at most ONE activation of the land (one {T}).
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int n = 0;
        for (const Action& a : p.actions) { if (IsPump(a)) { ++n; } }
        CHECK(n <= 1);
    }
}

// ---- 4. THE APPLY -------------------------------------------------------------------------------------------

TEST_CASE("Kessig Wolf Run: +X/+0 lands on the pinned target, taps the land and X+2 others, deals combat damage, gone at cleanup")
{
    LoadKwr();
    GameState s = FreshKwr();
    PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11);
    for (int i = 0; i < 4; ++i) { PutK(s, "Forest", 12 + i); }
    PutK(s, "Vexing Shusher", 1);   // 2/2
    PutK(s, "Boros Reckoner", 3);   // 3/3
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = PumpOnlyPlan(plans, 1, 3);
    REQUIRE(p != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *p, true);
    REQUIRE(ByNum(t, 1) != nullptr);
    CHECK(ByNum(t, 1)->temp_power_bonus == 3);
    CHECK(ByNum(t, 1)->temp_tough_bonus == 0);
    CHECK(ByNum(t, 3)->temp_power_bonus == 0);            // the other creature: untouched
    CHECK(ByNum(t, 10)->tapped);                          // the {T}
    CHECK(TappedLands(t) == 6);                           // the land + X(3) + R + G
    {   // The pump reaches combat damage: (2 + 3) + 3 = 8.
        GameState c = t;
        RolloutSimulateCombat(c);
        CHECK(c.players[1].life == 12);
    }
    // "Until end of turn": the shared cleanup zeroes it (CR 514.2).
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    ge.PlayOutFrom(t, t.turn_number, GameEngine::ResumeAt::Cleanup);
    REQUIRE(ByNum(t, 1) != nullptr);
    CHECK(ByNum(t, 1)->temp_power_bonus == 0);
}

TEST_CASE("Kessig Wolf Run: a target that left before the activation is a clean no-op (nothing paid, no substitution)")
{
    LoadKwr();
    GameState s = FreshKwr();
    PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11);
    for (int i = 0; i < 4; ++i) { PutK(s, "Forest", 12 + i); }
    PutK(s, "Vexing Shusher", 1);
    PutK(s, "Boros Reckoner", 3);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = PumpOnlyPlan(plans, 1, 2);
    REQUIRE(p != nullptr);
    GameState t = s;
    t.battlefield.erase(std::remove_if(t.battlefield.begin(), t.battlefield.end(),
                                       [](const Permanent& q) { return q.card.m_number == 1; }),
                        t.battlefield.end());
    TurnSolver::ApplyPlan(t, *p, true);
    CHECK(TappedLands(t) == 0);
    CHECK(ByNum(t, 3)->temp_power_bonus == 0);
}

// ---- 5. LANDFALL FOLDS -------------------------------------------------------------------------------------

TEST_CASE("Kessig Wolf Run, Landfall: one attack-eligible target, X = all remaining mana, run last; none after combat")
{
    LoadKwr();
    GameState s = FreshKwr();
    s.m_provider = &LfProvK();
    PutK(s, kWolfRun, 10); PutK(s, "Mountain", 11);
    for (int i = 0; i < 4; ++i) { PutK(s, "Forest", 12 + i); }
    PutK(s, "Avenger of Zendikar", 1);                     // 5/5, can attack
    PutK(s, "Rampaging Baloths", 2, false, /*sick=*/true); // 6/6, summoning sick: cannot attack
    PutK(s, "Terastodon", 3);                              // 9/9, can attack -- the largest
    HandK(s, "Rampant Growth", 40);
    LibK(s, "Forest", 60); LibK(s, "Forest", 61);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<std::pair<int, int>> v = PumpVariants(plans);
    REQUIRE(v.size() == 1);                                // ONE target (the largest attacker), ONE X
    CHECK(v[0].first == 3);                                // Terastodon -- never the sick Baloths
    CHECK(v[0].second == kTapTargetPumpXAll);
    // Alone: X = 5 other mana - {R}{G} = 3.
    const TurnSolver::Plan* alone = PumpOnlyPlan(plans, 3, kTapTargetPumpXAll);
    REQUIRE(alone != nullptr);
    {
        GameState t = s;
        TurnSolver::ApplyPlan(t, *alone, true);
        CHECK(ByNum(t, 3)->temp_power_bonus == 3);
        CHECK(TappedLands(t) == 6);
    }
    // Beside a Rampant Growth ({1}{G}) the pump takes what is LEFT (X = 1), whatever the plan's order.
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool rg = false, pump = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Rampant Growth") { rg = true; }
            if (IsPump(a)) { pump = true; }
        }
        if (rg && pump && p.actions.size() == 2) { both = &p; break; }
    }
    REQUIRE(both != nullptr);
    {
        GameState t = s;
        TurnSolver::ApplyPlan(t, *both, true);
        CHECK(ByNum(t, 3)->temp_power_bonus == 1);
        int forests = 0;
        for (const Permanent& q : t.battlefield) { if (q.card.m_name.str() == "Forest") { ++forests; } }
        CHECK(forests == 5);                               // Rampant Growth resolved first and found its Forest
    }
    // After combat damage the pump buys nothing: no variant in a second main.
    CHECK(PumpVariants(TurnSolver::EnumerateMainPlans(s, false)).empty());
    // The trailing order: the max-X pump is forced LAST.
    Action pump = alone->actions[0];
    Action other;
    other.kind = Action::Kind::ActivatePermAbility;
    other.ability_mode = Action::AbilityMode::SacLandSearch;
    other.card_name = Dk("Myriad Landscape").card.m_name;
    other.def = &Dk("Myriad Landscape");
    std::vector<Action> pass{ pump, other };
    TurnSolver::OrderTrailingActivations(s, pass);
    CHECK(IsPump(pass.back()));
}

// ---- 6. THE TITAN PICK --------------------------------------------------------------------------------------

namespace
{
GameState TitanBoard(bool land_in_hand)
{
    GameState s = FreshKwr();
    s.m_provider = &LfProvK();
    for (int i = 0; i < 4; ++i) { LibK(s, "Forest", 50 + i); }
    for (int i = 0; i < 3; ++i) { PutK(s, "Forest", 10 + i); }
    PutK(s, "Mountain", 20, true); PutK(s, "Mountain", 21, true);   // g3, r2
    int n = 70;
    LibK(s, "Evolving Wilds", n++); LibK(s, "Terramorphic Expanse", n++);
    LibK(s, "Myriad Landscape", n++); LibK(s, "Myriad Landscape", n++);
    LibK(s, "Gruul Turf", n++); LibK(s, "Radiant Fountain", n++); LibK(s, "Haunted Fengraf", n++);
    if (land_in_hand) { HandK(s, "Forest", 44); }
    return s;
}
using PickK = std::vector<std::string>;
}   // namespace

TEST_CASE("Landfall Titan pick + Kessig Wolf Run: the Wolf Run pairs are admitted and searched -- first with no other threat")
{
    LoadKwr();
    const LandSearchSpec ti = LandSearchSpecOf(Dk("Primeval Titan").params);
    const PickK wr_turf{ "Gruul Turf", kWolfRun };
    {   // No land in hand -> the rule makes Turf automatic: Wolf Run + Turf beside the rule's two Turf pairs.
        GameState s = TitanBoard(false);
        LibK(s, kWolfRun, 80);
        const std::vector<PickK> a = LandSearchPicks(s, 0, ti);
        REQUIRE(a.size() == 3);
        CHECK(a.front() == wr_turf);                       // no other threat: Wolf Run first
        // A threat on the battlefield (Avenger: a landfall payoff) -> after the rule's own picks.
        GameState t = s; PutK(t, "Avenger of Zendikar", 5);
        const std::vector<PickK> b = LandSearchPicks(t, 0, ti);
        REQUIRE(b.size() == 3);
        CHECK(b.back() == wr_turf);
        // ...and so does a power-4 creature CARD in hand, or a Genesis Wave in hand.
        GameState u = s; HandK(u, "Genesis Wave", 45);
        CHECK(LandSearchPicks(u, 0, ti).back() == wr_turf);
        // A Mossborn Hydra is a landfall payoff even as a 0/0 card in hand or a 1/1 on the battlefield.
        GameState h = s; HandK(h, "Mossborn Hydra", 46);
        CHECK(LandSearchPicks(h, 0, ti).back() == wr_turf);
        GameState hb = s; PutK(hb, "Mossborn Hydra", 7);
        CHECK(LandSearchPicks(hb, 0, ti).back() == wr_turf);
        // A Titan is never the "other" threat.
        GameState w = s; PutK(w, "Primeval Titan", 6);
        CHECK(LandSearchPicks(w, 0, ti).front() == wr_turf);
    }
    {   // A land in hand: the rule's Wilds / Myriad pairs, plus Wolf Run beside each of the rule's lands.
        GameState s = TitanBoard(true);
        LibK(s, kWolfRun, 80);
        const std::vector<PickK> a = LandSearchPicks(s, 0, ti);
        REQUIRE(a.size() == 5);
        const std::vector<PickK> first2{ a[0], a[1] };
        CHECK(std::find(first2.begin(), first2.end(), PickK{ "Evolving Wilds", kWolfRun }) != first2.end());
        CHECK(std::find(first2.begin(), first2.end(), PickK{ kWolfRun, "Myriad Landscape" }) != first2.end());
        for (const PickK& p : a)
        { CHECK(std::count(p.begin(), p.end(), "Forest") == 0); CHECK(std::count(p.begin(), p.end(), "Mountain") == 0); }
        // We already control a Wolf Run: the rule exactly as before (no Wolf Run pick).
        GameState c = s; PutK(c, kWolfRun, 9);
        const std::vector<PickK> b = LandSearchPicks(c, 0, ti);
        CHECK(b.size() == 3);
        for (const PickK& p : b) { CHECK(std::count(p.begin(), p.end(), std::string(kWolfRun)) == 0); }
    }
    {   // Out of basics: the fixed order's one pick (Turf + Myriad) plus Wolf Run's best pick (Turf + Wolf Run).
        GameState s = FreshKwr();
        s.m_provider = &LfProvK();
        for (int i = 0; i < 3; ++i) { PutK(s, "Forest", 10 + i); }
        HandK(s, "Forest", 44);
        int n = 70;
        LibK(s, "Evolving Wilds", n++); LibK(s, "Myriad Landscape", n++); LibK(s, "Myriad Landscape", n++);
        LibK(s, "Gruul Turf", n++); LibK(s, "Radiant Fountain", n++); LibK(s, kWolfRun, n++);
        const std::vector<PickK> a = LandSearchPicks(s, 0, ti);
        CHECK(a == std::vector<PickK>{ wr_turf, PickK{ "Gruul Turf", "Myriad Landscape" } });
        GameState t = s; PutK(t, "Rampaging Baloths", 5);
        CHECK(LandSearchPicks(t, 0, ti) == std::vector<PickK>{ PickK{ "Gruul Turf", "Myriad Landscape" }, wr_turf });
    }
    {   // The memo key separates threat / no threat and Wolf Run / none (its inputs beyond the library).
        GameState s = TitanBoard(true);
        GameState w = s; LibK(w, kWolfRun, 80);
        GameState th = w; PutK(th, "Avenger of Zendikar", 5);
        std::uint64_t k0 = 0, k1 = 0, k2 = 0;
        CHECK(LfProvK().LandSearchPickOrderKey(s, 0, k0));
        CHECK(LfProvK().LandSearchPickOrderKey(w, 0, k1));
        CHECK(LfProvK().LandSearchPickOrderKey(th, 0, k2));
        CHECK(k0 != k1); CHECK(k1 != k2);
        // Without a Wolf Run in the library the key is the old one (a threat changes nothing).
        GameState sth = s; PutK(sth, "Avenger of Zendikar", 5);
        std::uint64_t k3 = 0;
        CHECK(LfProvK().LandSearchPickOrderKey(sth, 0, k3));
        CHECK(k0 == k3);
    }
}

// ---- 7. THE KAROO BOUNCE / FETCH COLOURS ------------------------------------------------------------------

TEST_CASE("Landfall Karoo bounce: Kessig Wolf Run is colourless but ranks LAST among the colourless lands")
{
    LoadKwr();
    auto picked = [](bool fountain)
    {
        GameState s = FreshKwr();
        s.m_provider = &LfProvK();
        LibK(s, "Genesis Wave", 80); LibK(s, "Omnath, Locus of Rage", 81);
        int n = 10;
        for (int i = 0; i < 3; ++i) { PutK(s, "Forest", n++, true); }
        PutK(s, "Mountain", n++, true);
        PutK(s, kWolfRun, n++, true);
        if (fountain) { PutK(s, "Radiant Fountain", n++, true); }
        const int turf = PutK(s, "Gruul Turf", n++, false);
        std::vector<int> legal;
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (i != turf) { legal.push_back(i); } }
        const int i = LfProvK().BounceUserRulePick(s, 0, legal, legal);
        REQUIRE(i >= 0);
        return s.battlefield[static_cast<std::size_t>(i)].card.m_name.str();
    };
    CHECK(picked(true) == "Radiant Fountain");             // another colourless land goes first
    CHECK(picked(false) == std::string(kWolfRun));         // alone, it is still colourless -> first
}

TEST_CASE("Landfall fetch colours: a Kessig Wolf Run makes neither green nor red")
{
    LoadKwr();
    const LandSearchSpec rg = LandSearchSpecOf(Dk("Rampant Growth").params);
    GameState s = FreshKwr();
    s.m_provider = &LfProvK();
    for (int i = 0; i < 4; ++i) { LibK(s, "Forest", 50 + i); }
    LibK(s, "Mountain", 60); LibK(s, "Mountain", 61);
    PutK(s, "Forest", 10); PutK(s, "Forest", 11);           // g2
    PutK(s, kWolfRun, 12);                                  // r stays 0
    CHECK(LandSearchPicks(s, 0, rg)[0] == std::vector<std::string>{ "Mountain" });
    GameState g1 = FreshKwr();
    g1.m_provider = &LfProvK();
    for (int i = 0; i < 4; ++i) { LibK(g1, "Forest", 50 + i); }
    LibK(g1, "Mountain", 60);
    PutK(g1, "Forest", 10); PutK(g1, kWolfRun, 12);          // g1 (the Wolf Run is not green)
    CHECK(LandSearchPicks(g1, 0, rg)[0] == std::vector<std::string>{ "Forest" });
}
