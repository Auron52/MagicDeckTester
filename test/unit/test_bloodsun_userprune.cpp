// Blood Sun step 6c: the USER's tutor-target and land-play RESTRICTIONS (ledger B3 / B4, USER 2026-11-20;
// BloodSunProvider, lever MTG_BLOODSUN_USER_PRUNE = heurarm BLOODSUN_USER_PRUNE, DEFAULT ON). Every restriction is
// checked with the lever ON and again with it OFF (=0), where the option returns -- except Revelation at exactly 5
// mana, which fold F-R4 (X >= 1) already holds back in both arms.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/RevelationPackages.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
void EnsureCardsUp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& DefUp(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card CardUp(const std::string& name, int number)
{
    Card c = DefUp(name).card;
    c.m_number = number;
    return c;
}
void PutUp(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card = CardUp(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}
const BloodSunProvider& ProvUp()
{
    static const BloodSunProvider bp;
    return bp;
}
GameState BaseUp()
{
    EnsureCardsUp();
    GameState s;
    s.m_provider          = &ProvUp();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = true;
    s.own_death_live      = true;
    return s;
}
void Lib(GameState& s, const std::vector<std::string>& names)
{
    for (std::size_t k = 0; k < names.size(); ++k) { s.players[0].library.push_back(CardUp(names[k], 500 + static_cast<int>(k))); }
}
// Pins the lever (and the Exploration arm) for one scope; restores "use the env default" after.
struct PruneArm
{
    PruneArm(bool on, int anylife = -1)
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]          = on ? 1 : 0;
        heurarm::t_arm[heurarm::BLOODSUN_EXPLORATION_ANYLIFE] = static_cast<std::int8_t>(anylife);
    }
    ~PruneArm()
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]          = -1;
        heurarm::t_arm[heurarm::BLOODSUN_EXPLORATION_ANYLIFE] = -1;
    }
};
// Every autonomous Revelation cast the node's CollectActions offers (wave 0), as X -> count.
std::map<int, int> RevelationXs(const GameState& s)
{
    std::map<int, int> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    { if (a.card_name.str() == "Diabolic Revelation") { ++out[a.chosen_x]; } }
    return out;
}
// Every DISTINCT Revelation package (names, wave tag stripped) of the exhaustive menu -- every wave.
std::set<std::vector<std::string>> AllPackages(const GameState& s)
{
    std::set<std::vector<std::string>> out;
    bool truncated = true;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, &truncated))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Diabolic Revelation" || !revpkg::IsPackage(a.tutor_target.str())) { continue; }
            std::vector<std::string> names = revpkg::DecodePackage(a.tutor_target.str());
            std::sort(names.begin(), names.end());
            out.insert(names);
        }
    }
    CHECK_FALSE(truncated);
    return out;
}
std::vector<std::string> GrimTargets(const GameState& s)
{ return ProvUp().TutorCandidates(s, 0, DefUp("Grim Tutor").params); }
// The transmute's offered targets (one Channel action per name).
std::set<std::string> TransmuteTargets(const GameState& s)
{
    std::set<std::string> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind == Action::Kind::Channel && a.card_name.str() == "Dimir Machinations")
        { out.insert(a.tutor_target.str()); }
    }
    return out;
}
bool Has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
// Does any plan of the main-phase menu play this land?
bool MenuPlays(const GameState& s, const std::string& land)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { if (p.land_to_play == land) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("bloodsun prune (a): Revelation never at X 0; at X 1 only with another Revelation in hand")
{
    const std::vector<std::string> lib = { "Time Warp", "Emrakul, the Aeons Torn", "Lotus Vale", "Scorched Ruins" };
    // Exactly 5 mana: X = 0 only -> never cast, in both arms (fold F-R4 is not the lever's).
    for (bool on : { true, false })
    {
        PruneArm arm(on);
        GameState s = BaseUp();
        for (int k = 0; k < 5; ++k) { PutUp(s, "Swamp", 10 + k); }
        s.players[0].hand.push_back(CardUp("Diabolic Revelation", 1));
        Lib(s, lib);
        CHECK(RevelationXs(s).empty());
    }
    // 6 mana, ONE Revelation: X = 1 is the only X -> not cast under the lever; offered without it.
    GameState six = BaseUp();
    for (int k = 0; k < 6; ++k) { PutUp(six, "Swamp", 10 + k); }
    six.players[0].hand.push_back(CardUp("Diabolic Revelation", 1));
    Lib(six, lib);
    {
        PruneArm arm(true);
        CHECK(RevelationXs(six).empty());
    }
    {
        PruneArm arm(false);
        CHECK(RevelationXs(six)[1] > 0);
    }
    // 6 mana, TWO Revelations: X = 1 is cast (the spare is the "extra").
    GameState two = six;
    two.players[0].hand.push_back(CardUp("Diabolic Revelation", 2));
    {
        PruneArm arm(true);
        CHECK(RevelationXs(two)[1] > 0);
    }
    // 7 mana, one Revelation: X = 2 is offered, X = 1 still not.
    GameState seven = six;
    PutUp(seven, "Swamp", 30);
    {
        PruneArm arm(true);
        const std::map<int, int> xs = RevelationXs(seven);
        CHECK(xs.count(1) == 0);
        CHECK(xs.count(2) == 1);
    }
}

TEST_CASE("bloodsun prune (b): a Revelation package never holds a non-target; the restricted space is complete")
{
    GameState s = BaseUp();
    for (int k = 0; k < 7; ++k) { PutUp(s, "Swamp", 10 + k); }   // 7 mana: X <= 2
    s.players[0].hand.push_back(CardUp("Diabolic Revelation", 1));
    Lib(s, { "Burgeoning", "Ulamog, the Ceaseless Hunger", "Blooming Marsh", "Grim Tutor", "Dimir Machinations",
             "Golgari Rot Farm", "Time Warp", "Time Warp", "Lotus Vale", "Emrakul, the Aeons Torn", "Fastbond",
             "Fastbond", "Blood Sun", "Blood Sun" });
    const std::set<std::string> banned = { "Burgeoning", "Ulamog, the Ceaseless Hunger", "Blooming Marsh", "Grim Tutor",
                                           "Dimir Machinations", "Golgari Rot Farm" };
    std::set<std::vector<std::string>> on, off;
    {
        PruneArm arm(true);
        on = AllPackages(s);
    }
    {
        PruneArm arm(false);
        off = AllPackages(s);
    }
    bool banned_on = false, banned_off = false, two_fastbond = false, two_sun = false;
    for (const auto& pk : on)
    {
        for (const std::string& n : pk) { banned_on = banned_on || banned.count(n) != 0; }
        two_fastbond = two_fastbond || std::count(pk.begin(), pk.end(), "Fastbond") > 1;
        two_sun      = two_sun || std::count(pk.begin(), pk.end(), "Blood Sun") > 1;
    }
    for (const auto& pk : off) { for (const std::string& n : pk) { banned_off = banned_off || banned.count(n) != 0; } }
    CHECK_FALSE(banned_on);
    CHECK_FALSE(two_fastbond);   // Fastbond: one, never a second
    CHECK_FALSE(two_sun);        // Blood Sun: one while we have none
    CHECK(banned_off);           // the unrestricted arm offers them
    // THE COUNT. Admitted classes: Time Warp (cap 2), Lotus Vale (1), Emrakul (1), Fastbond (cap 1 of 2), Blood Sun
    // (cap 1 of 2); X = 2 only (X = 1 needs a second Revelation): the multisets of size 2 within those caps.
    CHECK(static_cast<long long>(on.size()) == revpkg::CountMultisets({ 2, 1, 1, 1, 1 }, 2));
    for (const auto& pk : on) { CHECK(pk.size() == 2); }
    // A Blood Sun in hand takes Blood Sun out of the space; one on the battlefield too.
    GameState h = s;
    h.players[0].hand.push_back(CardUp("Blood Sun", 3));
    {
        PruneArm arm(true);
        const std::set<std::vector<std::string>> hp = AllPackages(h);
        CHECK(static_cast<long long>(hp.size()) == revpkg::CountMultisets({ 2, 1, 1, 1 }, 2));
        for (const auto& pk : hp) { CHECK(std::count(pk.begin(), pk.end(), "Blood Sun") == 0); }
    }
}

TEST_CASE("bloodsun prune (b): under Blood Sun Lotus Field == Lotus Vale even with an Ulamog reachable")
{
    GameState s = BaseUp();
    PutUp(s, "Blood Sun", 60);
    s.players[0].library.push_back(CardUp("Lotus Vale", 1));
    s.players[0].library.push_back(CardUp("Lotus Field", 2));
    s.players[0].library.push_back(CardUp("Ulamog, the Ceaseless Hunger", 3));
    auto lotus_classes = [](const GameState& g, bool forced) -> int
    {
        int n = 0;
        for (const revpkg::Class& k : revpkg::BuildClasses(g, 0, forced))
        { for (const auto& m : k.members) { if (m.first == "Lotus Vale" || m.first == "Lotus Field") { ++n; break; } } }
        return n;
    };
    {
        PruneArm arm(true);
        CHECK(ProvUp().RevelationTwinFoldForced(s));
        CHECK(lotus_classes(s, ProvUp().RevelationTwinFoldForced(s)) == 1);
    }
    {
        PruneArm arm(false);
        CHECK_FALSE(ProvUp().RevelationTwinFoldForced(s));
        CHECK(lotus_classes(s, ProvUp().RevelationTwinFoldForced(s)) == 2);   // the step-6 conservative scan
    }
}

TEST_CASE("bloodsun prune (b): Machinations' transmute -- Grim Tutor (and Blood Sun while we have none) only")
{
    GameState s = BaseUp();
    for (int k = 0; k < 3; ++k) { PutUp(s, "Swamp", 10 + k); }
    s.players[0].hand.push_back(CardUp("Dimir Machinations", 1));
    Lib(s, { "Blood Sun", "Grim Tutor", "Dimir Machinations", "Time Warp" });
    {
        PruneArm arm(true);
        CHECK(TransmuteTargets(s) == std::set<std::string>{ "Blood Sun", "Grim Tutor" });
        GameState h = s;
        h.players[0].hand.push_back(CardUp("Blood Sun", 2));
        CHECK(TransmuteTargets(h) == std::set<std::string>{ "Grim Tutor" });
        GameState b = s;
        PutUp(b, "Blood Sun", 61);
        CHECK(TransmuteTargets(b) == std::set<std::string>{ "Grim Tutor" });
    }
    {
        PruneArm arm(false);
        CHECK(TransmuteTargets(s) == std::set<std::string>{ "Blood Sun", "Grim Tutor", "Dimir Machinations" });
    }
}

TEST_CASE("bloodsun prune (b): Grim Tutor's targets, Exploration by life and Fastbond, the red-source exception")
{
    GameState s = BaseUp();
    for (int k = 0; k < 3; ++k) { PutUp(s, "Swamp", 10 + k); }
    Lib(s, { "Exploration", "Fastbond", "Grim Tutor", "Burgeoning", "Time Warp", "Emrakul, the Aeons Torn",
             "Diabolic Revelation", "Scorched Ruins", "Lotus Field", "City of Brass", "Rakdos Carnarium",
             "Ulamog, the Ceaseless Hunger", "Blood Sun" });
    {
        PruneArm arm(true);
        std::vector<std::string> t = GrimTargets(s);
        CHECK_FALSE(Has(t, "Exploration"));   // life 20
        CHECK(Has(t, "Fastbond"));
        CHECK(Has(t, "Blood Sun"));
        for (const char* n : { "Time Warp", "Emrakul, the Aeons Torn", "Diabolic Revelation", "Scorched Ruins",
                               "Lotus Field" }) { CHECK(Has(t, n)); }
        for (const char* n : { "Grim Tutor", "Burgeoning", "Ulamog, the Ceaseless Hunger", "City of Brass",
                               "Rakdos Carnarium" }) { CHECK_FALSE(Has(t, n)); }
        // ORDER: Fastbond first among the acceleration, Exploration after it; Time Warp before Emrakul.
        GameState low = s;
        low.players[0].life = 8;
        t = GrimTargets(low);
        REQUIRE(Has(t, "Exploration"));
        CHECK(std::find(t.begin(), t.end(), "Fastbond") < std::find(t.begin(), t.end(), "Exploration"));
        CHECK(std::find(t.begin(), t.end(), "Time Warp") < std::find(t.begin(), t.end(), "Emrakul, the Aeons Torn"));
        // A Fastbond in HAND: no second Fastbond; Exploration still (no Fastbond on the battlefield).
        GameState fh = low;
        fh.players[0].hand.push_back(CardUp("Fastbond", 2));
        t = GrimTargets(fh);
        CHECK_FALSE(Has(t, "Fastbond"));
        CHECK(Has(t, "Exploration"));
        // A Fastbond on the BATTLEFIELD: never Exploration, at any life and in the any-life arm.
        GameState fb = low;
        PutUp(fb, "Fastbond", 62);
        CHECK_FALSE(Has(GrimTargets(fb), "Exploration"));
        CHECK_FALSE(Has(GrimTargets(fb), "Fastbond"));
        // Red-source exception: Blood Sun in hand, none out, no red source anywhere of ours -> City and Carnarium.
        GameState r = s;
        r.players[0].hand.push_back(CardUp("Blood Sun", 3));
        t = GrimTargets(r);
        CHECK(Has(t, "City of Brass"));
        CHECK(Has(t, "Rakdos Carnarium"));
        CHECK_FALSE(Has(t, "Blood Sun"));
        GameState rg = r;
        rg.players[0].hand.push_back(CardUp("Copperline Gorge", 4));   // a red source in hand
        CHECK_FALSE(Has(GrimTargets(rg), "City of Brass"));
        GameState rb = r;
        PutUp(rb, "Graven Cairns", 63);                                 // a red source on the battlefield
        CHECK_FALSE(Has(GrimTargets(rb), "Rakdos Carnarium"));
    }
    {
        PruneArm arm(true, /*anylife=*/1);
        CHECK(Has(GrimTargets(s), "Exploration"));   // the A/B arm: any life
        GameState fb = s;
        PutUp(fb, "Fastbond", 62);
        CHECK_FALSE(Has(GrimTargets(fb), "Exploration"));   // ...still never with a Fastbond out
    }
    {
        PruneArm arm(false);
        const std::vector<std::string> t = GrimTargets(s);
        for (const char* n : { "Exploration", "Grim Tutor", "Burgeoning", "Ulamog, the Ceaseless Hunger", "City of Brass" })
        { CHECK(Has(t, n)); }
    }
    // A Grim Tutor with no admitted target is not cast under the lever; it is without.
    GameState none = BaseUp();
    for (int k = 0; k < 3; ++k) { PutUp(none, "Swamp", 10 + k); }
    none.players[0].hand.push_back(CardUp("Grim Tutor", 1));
    Lib(none, { "Burgeoning", "Blooming Marsh" });
    auto grim_cast = [](const GameState& g) -> bool
    {
        for (const Action& a : TurnSolver::CollectActionsForTest(g, true))
        { if (a.card_name.str() == "Grim Tutor") { return true; } }
        return false;
    };
    {
        PruneArm arm(true);
        CHECK_FALSE(grim_cast(none));
    }
    {
        PruneArm arm(false);
        CHECK(grim_cast(none));
    }
}

TEST_CASE("bloodsun prune (c): pre-Blood Sun land plays -- Ruins never, a Lotus only to cast the Blood Sun in hand")
{
    // Two untapped Swamps, no red: Blood Sun in hand cannot be cast without the Vale.
    GameState s = BaseUp();
    PutUp(s, "Swamp", 10);
    PutUp(s, "Swamp", 11);
    s.players[0].hand.push_back(CardUp("Blood Sun", 1));
    s.players[0].hand.push_back(CardUp("Scorched Ruins", 2));
    s.players[0].hand.push_back(CardUp("Lotus Vale", 3));
    Lib(s, { "Swamp", "Swamp", "Swamp" });
    {
        PruneArm arm(true);
        CHECK_FALSE(MenuPlays(s, "Scorched Ruins"));          // Ruins held pre-Blood Sun
        CHECK(MenuPlays(s, "Lotus Vale"));                    // the Vale, to cast Blood Sun
        bool vale_sun = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (p.land_to_play != "Lotus Vale") { continue; }
            for (const Action& a : p.actions) { vale_sun = vale_sun || a.card_name.str() == "Blood Sun"; }
        }
        CHECK(vale_sun);
        // Blood Sun on the battlefield: every land is free -> Ruins offered.
        GameState u = s;
        PutUp(u, "Blood Sun", 64);
        CHECK(MenuPlays(u, "Scorched Ruins"));
        // Other mana CAN cast Blood Sun (a Mountain): the Vale waits.
        GameState m = s;
        PutUp(m, "Mountain", 12);
        CHECK_FALSE(MenuPlays(m, "Lotus Vale"));
        // No Blood Sun in hand: no Lotus.
        GameState n = s;
        n.players[0].hand.erase(n.players[0].hand.begin());
        CHECK_FALSE(MenuPlays(n, "Lotus Vale"));
        // One other land only: never a Lotus.
        GameState one = s;
        one.battlefield.pop_back();
        CHECK_FALSE(MenuPlays(one, "Lotus Vale"));
        // Field: not with a Vale in hand; with none it is offered (same condition); never with 1 other land.
        GameState f = s;
        f.players[0].hand.push_back(CardUp("Lotus Field", 4));
        CHECK_FALSE(MenuPlays(f, "Lotus Field"));
        GameState fo = f;
        fo.players[0].hand.erase(std::find_if(fo.players[0].hand.begin(), fo.players[0].hand.end(),
                                              [](const Card& c) { return c.m_name.str() == "Lotus Vale"; }));
        CHECK(MenuPlays(fo, "Lotus Field"));
        GameState f1 = fo;
        f1.battlefield.pop_back();
        CHECK_FALSE(MenuPlays(f1, "Lotus Field"));
        // The hook directly, a Burgeoning put on the opponent's turn (step 6d item 3, USER: Vale and Field "equivalent
        // there"): ONE rule for both, judged for our next turn -- two Swamps cannot cast Blood Sun then, so either Lotus
        // is offered, the Field even with a Vale in hand (that prune is the land PLAY's only). Ruins never.
        GameState opp = f;
        opp.active_player_index = 1;
        CHECK(ProvUp().LandPlayOffered(opp, 0, DefUp("Lotus Vale")));
        CHECK(ProvUp().LandPlayOffered(opp, 0, DefUp("Lotus Field")));
        CHECK_FALSE(ProvUp().LandPlayOffered(opp, 0, DefUp("Scorched Ruins")));
        // ...not with one other land, nor when a Mountain on the battlefield would cast it next turn, nor when a
        // non-sacrificing land in hand (a Mountain), put instead, would.
        GameState opp1 = opp;
        opp1.battlefield.pop_back();
        CHECK_FALSE(ProvUp().LandPlayOffered(opp1, 0, DefUp("Lotus Vale")));
        CHECK_FALSE(ProvUp().LandPlayOffered(opp1, 0, DefUp("Lotus Field")));
        GameState oppm = opp;
        PutUp(oppm, "Mountain", 12, /*tapped=*/true);
        CHECK_FALSE(ProvUp().LandPlayOffered(oppm, 0, DefUp("Lotus Vale")));
        GameState opph = opp;
        opph.players[0].hand.push_back(CardUp("Mountain", 5));
        CHECK_FALSE(ProvUp().LandPlayOffered(opph, 0, DefUp("Lotus Vale")));
        CHECK_FALSE(ProvUp().LandPlayOffered(opph, 0, DefUp("Lotus Field")));
        // Our own play, the same backup rule (USER: "drop a non-sacrificing land ... if one is available"): a Mountain
        // in hand casts Blood Sun with the two Swamps, so neither Lotus is the drop; a Swamp in hand would not.
        GameState hm = s;
        hm.players[0].hand.push_back(CardUp("Mountain", 5));
        CHECK_FALSE(MenuPlays(hm, "Lotus Vale"));
        CHECK(MenuPlays(hm, "Mountain"));
        GameState hs = s;
        hs.players[0].hand.push_back(CardUp("Swamp", 5));
        CHECK(MenuPlays(hs, "Lotus Vale"));
    }
    {
        PruneArm arm(false);
        CHECK(MenuPlays(s, "Scorched Ruins"));
        GameState m = s;
        PutUp(m, "Mountain", 12);
        CHECK(MenuPlays(m, "Lotus Vale"));
        GameState f = s;
        f.players[0].hand.push_back(CardUp("Lotus Field", 4));
        CHECK(MenuPlays(f, "Lotus Field"));
    }
}

// Step 6d item 2: the BEYOND-HORIZON greedy drop honours (c) for this provider only (GreedyLandHonoursOffer). Hand:
// Scorched Ruins and Lotus Vale, no Blood Sun anywhere: the menu refuses both, and so does the greedy ranker (no drop);
// with the lever off, or under any other provider (the generic one here), the greedy drops a land as before.
TEST_CASE("bloodsun prune (c), playout: the greedy land choice skips a land the menu refuses (opt-in provider only)")
{
    GameState s = BaseUp();
    PutUp(s, "Swamp", 10);
    PutUp(s, "Swamp", 11);
    s.players[0].hand.push_back(CardUp("Scorched Ruins", 2));
    s.players[0].hand.push_back(CardUp("Lotus Vale", 3));
    Lib(s, { "Swamp", "Swamp", "Swamp" });
    {
        PruneArm arm(true);
        CHECK_FALSE(MenuPlays(s, "Scorched Ruins"));
        CHECK_FALSE(MenuPlays(s, "Lotus Vale"));
        CHECK(GreedyLandChoiceIndex(s) == -1);
        CHECK(GreedyLandChoice(s).hand_index == -1);
        // A Swamp in hand is still the greedy's drop.
        GameState t = s;
        t.players[0].hand.push_back(CardUp("Swamp", 4));
        CHECK(GreedyLandChoiceIndex(t) == 2);
        // Another provider never opts in: its greedy is unchanged.
        static const GenericProvider gp;
        GameState g = s;
        g.m_provider = &gp;
        CHECK(GreedyLandChoiceIndex(g) >= 0);
    }
    {
        PruneArm arm(false);
        CHECK(GreedyLandChoiceIndex(s) >= 0);
    }
}

// Step 6d item 3 (USER, VERBATIM): "On the Red-source exception we only need one red source." Blood Sun in hand, no red
// source anywhere, a library holding both exception names: each is a target (cap 1), but no package holds both.
TEST_CASE("bloodsun prune (b): the red-source exception admits ONE red source per package")
{
    GameState s = BaseUp();
    for (int k = 0; k < 7; ++k) { PutUp(s, "Swamp", 10 + k); }
    s.players[0].hand.push_back(CardUp("Diabolic Revelation", 1));
    s.players[0].hand.push_back(CardUp("Blood Sun", 2));
    // 7 mana and ONE Revelation: X = 2 exactly (X = 1 needs a spare Revelation), so a package holds two cards. Time Warp
    // is the admitted partner: without one a two-card package would need BOTH red sources, which the cap refuses.
    Lib(s, { "Rakdos Carnarium", "Rakdos Carnarium", "City of Brass", "City of Brass", "Swamp", "Time Warp" });
    {
        PruneArm arm(true);
        const std::vector<std::string> g = GrimTargets(s);
        CHECK(Has(g, "Rakdos Carnarium"));
        CHECK(Has(g, "City of Brass"));
        int with_red = 0;
        for (const std::vector<std::string>& pk : AllPackages(s))
        {
            const long red = std::count(pk.begin(), pk.end(), "Rakdos Carnarium") + std::count(pk.begin(), pk.end(), "City of Brass");
            CHECK(red <= 1);
            if (red == 1) { ++with_red; }
        }
        CHECK(with_red == 2);   // {Carnarium, Time Warp} and {City of Brass, Time Warp}
    }
    {
        PruneArm arm(false);
        bool both = false;
        for (const std::vector<std::string>& pk : AllPackages(s))
        {
            both = both || (std::count(pk.begin(), pk.end(), "Rakdos Carnarium") > 0
                             && std::count(pk.begin(), pk.end(), "City of Brass") > 0);
        }
        CHECK(both);   // the unrestricted space holds them together
    }
}

// ---- Step 6e (ledger B5 / B8 / B10, USER 2026-11-21).

TEST_CASE("bloodsun prune (B8): at most one Revelation per package, never a Revelation-only package")
{
    GameState s = BaseUp();
    for (int k = 0; k < 8; ++k) { PutUp(s, "Swamp", 10 + k); }   // 8 mana: X <= 3
    s.players[0].hand.push_back(CardUp("Diabolic Revelation", 1));
    Lib(s, { "Diabolic Revelation", "Diabolic Revelation", "Diabolic Revelation", "Time Warp", "Emrakul, the Aeons Torn" });
    auto revs = [](const std::vector<std::string>& pk) -> long
    { return std::count(pk.begin(), pk.end(), "Diabolic Revelation"); };
    {
        PruneArm arm(true);
        const std::set<std::vector<std::string>> on = AllPackages(s);
        for (const auto& pk : on)
        {
            CHECK(revs(pk) <= 1);
            CHECK(revs(pk) < static_cast<long>(pk.size()));
        }
        // Rev (cap 1), Time Warp (1), Emrakul (1); X in {2, 3} (one Revelation in hand): every multiset, none refused
        // besides the Revelation-only ones (there are none of size >= 2 under the one-copy cap).
        CHECK(static_cast<long long>(on.size())
              == revpkg::CountMultisets({ 1, 1, 1 }, 2) + revpkg::CountMultisets({ 1, 1, 1 }, 3));
        CHECK(on.count({ "Diabolic Revelation", "Time Warp" }) == 1);   // a replacement Revelation + a useful card
        // A spare Revelation in hand opens X = 1: {Time Warp} and {Emrakul}, never {Revelation} (keep the mana).
        GameState two = s;
        two.players[0].hand.push_back(CardUp("Diabolic Revelation", 2));
        const std::set<std::vector<std::string>> tp = AllPackages(two);
        CHECK(tp.count({ "Time Warp" }) == 1);
        CHECK(tp.count({ "Emrakul, the Aeons Torn" }) == 1);
        CHECK(tp.count({ "Diabolic Revelation" }) == 0);
        for (const auto& pk : tp) { CHECK(revs(pk) <= 1); }
    }
    {
        PruneArm arm(false);
        const std::set<std::vector<std::string>> off = AllPackages(s);
        CHECK(off.count({ "Diabolic Revelation", "Diabolic Revelation" }) == 1);
        CHECK(off.count({ "Diabolic Revelation" }) == 1);
    }
}

namespace
{
// The autonomous Machinations hand casts of the menu's CollectActions: (self-target count, opponent-target count).
std::pair<int, int> MachinationsCasts(const GameState& s)
{
    std::pair<int, int> n{ 0, 0 };
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Dimir Machinations") { continue; }
        if (a.player_target != 0) { ++n.second; } else { ++n.first; }
    }
    return n;
}
struct MachCastArm
{
    explicit MachCastArm(bool on) { heurarm::t_arm[heurarm::BLOODSUN_PRUNE_MACHINATIONS_CAST] = on ? 1 : 0; }
    ~MachCastArm() { heurarm::t_arm[heurarm::BLOODSUN_PRUNE_MACHINATIONS_CAST] = -1; }
};
}   // namespace

TEST_CASE("bloodsun prune (B5): Machinations never cast at the opponent; the self cast behind its sub-lever; transmute kept")
{
    GameState s = BaseUp();
    for (int k = 0; k < 3; ++k) { PutUp(s, "Swamp", 10 + k); }
    s.opponent_library_dealt = true;
    for (int k = 0; k < 6; ++k) { s.players[1].library.push_back(CardUp("Swamp", 100010 + k)); }
    s.players[0].hand.push_back(CardUp("Dimir Machinations", 1));
    Lib(s, { "Grim Tutor", "Blood Sun", "Swamp", "Swamp", "Swamp" });
    {
        PruneArm arm(true);
        MachCastArm sub(true);   // the default: no cast at all
        const std::pair<int, int> n = MachinationsCasts(s);
        CHECK(n.first == 0);
        CHECK(n.second == 0);
        CHECK(TransmuteTargets(s) == std::set<std::string>{ "Blood Sun", "Grim Tutor" });   // transmute unaffected
    }
    {
        PruneArm arm(true);
        MachCastArm sub(false);  // the sub-lever's =0 arm: the self cast returns, the opponent target stays pruned
        const std::pair<int, int> n = MachinationsCasts(s);
        CHECK(n.first > 0);
        CHECK(n.second == 0);
        CHECK(TransmuteTargets(s) == std::set<std::string>{ "Blood Sun", "Grim Tutor" });
    }
    {
        PruneArm arm(false);
        MachCastArm sub(true);   // inert while the prune is off: both targets offered
        const std::pair<int, int> n = MachinationsCasts(s);
        CHECK(n.first > 0);
        CHECK(n.second > 0);
    }
}

TEST_CASE("bloodsun prune (B10): a Blood Sun fetched earlier this turn leaves Grim Tutor the transmute's only target")
{
    GameState s = BaseUp();
    for (int k = 0; k < 6; ++k) { PutUp(s, "Swamp", 10 + k); }
    s.players[0].hand.push_back(CardUp("Dimir Machinations", 1));
    Lib(s, { "Blood Sun", "Blood Sun", "Grim Tutor", "Time Warp", "Fastbond" });
    PruneArm arm(true);
    CHECK(TransmuteTargets(s) == std::set<std::string>{ "Blood Sun", "Grim Tutor" });
    CHECK(Has(GrimTargets(s), "Blood Sun"));
    // This turn's first tutor (a Grim Tutor's resolution) takes a Blood Sun: the state every later decision of the turn
    // -- the breakpoint continuation's derived list, a second tutor's resolution -- is read from.
    GameState after = s;
    PerformTutor(after, 0, DefUp("Grim Tutor").params, "Blood Sun", "Grim Tutor");
    REQUIRE(std::count_if(after.players[0].hand.begin(), after.players[0].hand.end(),
                          [](const Card& c) { return c.m_name.str() == "Blood Sun"; }) == 1);
    CHECK(TransmuteTargets(after) == std::set<std::string>{ "Grim Tutor" });
    const std::vector<std::string> g = GrimTargets(after);
    CHECK_FALSE(Has(g, "Blood Sun"));
    CHECK(Has(g, "Time Warp"));
    CHECK(Has(g, "Fastbond"));
}
