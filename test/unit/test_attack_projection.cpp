// The this-turn ATTACK PROJECTION (TurnSolver's consider() sites: Solve's d0/playout walk and the
// search's EnumeratePlans), pinned on small constructed boards. Each defect below was exposed by
// MTG_ATTACK_TAP_NEED_EXACT, which removed a false lethal that had been hiding it; each fix has its
// own default-ON lever, and every test checks the projection with the lever ON and with it OFF.
//
// THE MEASUREMENT. `wins_this_turn` is `projected >= opponent life`, so the largest opponent life at
// which a plan still reads as a win IS its projected damage. Threshold() scans the opponent's life
// and returns that number, which pins the projection exactly rather than on one side of a line.
// (Neither site scores a bare attack-only plan -- the enumerator does not emit one and Solve's walk
// skips the empty selection -- so each board's pre-plan attack is given in its comment, and the
// tests pin the CAST plans, whose projection is pre-plan attack - what the plan's taps cost + what
// its casts add.) The d0 Solve site is pinned through the plan it picks at the threshold and one
// above it: the cast line reads as a win exactly up to its projected damage.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <functional>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

Permanent& Put(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card HandCard(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

// A bare pre-combat main: no land drop left, a library of `filler`, `hand` in hand. `salt` keeps the
// per-thread Solve / enumeration memos from serving one arm's answer to the other (they key on the
// state, and an arm is not part of the state).
GameState Bare(int opp_life, int salt, const std::vector<std::string>& hand,
               const std::string& filler = "Forest")
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20 + salt;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    int num = 100;
    for (const std::string& n : hand) { s.players[0].hand.push_back(HandCard(n, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(HandCard(filler, 900 + k)); }
    return s;
}

bool Casts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

bool CastsOnly(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    int casts = 0;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::PlayLand) { continue; }
        if (a.kind != Action::Kind::CastFromHand) { return false; }
        ++casts;
    }
    if (casts != static_cast<int>(names.size())) { return false; }
    for (const std::string& n : names) { if (!Casts(p, n)) { return false; } }
    return true;
}

// The projected damage of the plan(s) matching `pred` in the search's enumerator: the largest
// opponent life at which one of them reads as a win (0 = none ever does; -1 = never enumerated).
int Threshold(const std::function<GameState(int)>& board,
              const std::function<bool(const TurnSolver::Plan&)>& pred, int max_life = 60)
{
    int best = 0;
    bool seen = false;
    for (int life = 1; life <= max_life; ++life)
    {
        const GameState s = board(life);
        bool win = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            if (!pred(p)) { continue; }
            seen = true;
            if (p.wins_this_turn) { win = true; }
        }
        if (win) { best = life; }
    }
    return seen ? best : -1;
}

TurnSolver::Plan GreedySolve(const GameState& s)
{
    return TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
}

struct ArmScope
{
    explicit ArmScope(std::initializer_list<std::pair<heurarm::Slot, int>> v)
    { for (const auto& [slot, on] : v) { heurarm::t_arm[slot] = static_cast<std::int8_t>(on); } }
    ~ArmScope() { heurarm::Clear(); }
};

}   // namespace

// ---- F1: a domain dork's yield is the pool's (MTG_ATTACK_DOMAIN_DORK_YIELD) -----------------------
// Faeburrow Elder with all five colours among our permanents (Two-Headed Hellkite is WUBRG) is a 5/5
// that taps for FIVE. A Forest and Faeburrow are the only mana; Lightning Greaves costs {2}, so the
// payment must tap Faeburrow. The pool already knew Faeburrow makes 5; the attack-tap need read its
// yield as the card's static 1, so 4 of its 5 counted as non-attacker supply and the need was
// 2 - (6 - 1) < 0: nothing tapped, Greaves projected the whole 10-point swing.
namespace
{
GameState FaeBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Lightning Greaves" });
    Put(s, "Faeburrow Elder", 10);
    Put(s, "Two-Headed Hellkite", 11);
    Put(s, "Forest", 12);
    return s;
}
}

TEST_CASE("F1 domain dork yield: tapping Faeburrow for Greaves costs its 5-power attack")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    {
        const GameState probe = FaeBoard(20, 0);
        REQUIRE(DomainColors(probe, 0).size() == 5);
        REQUIRE(AvailableManaPool(probe, nullptr).Total() == 6);   // Forest 1 + Faeburrow 5
    }
    // Pre-plan attack 10: Faeburrow 5 + Hellkite 5.
    const int greaves = Threshold([](int l) { return FaeBoard(l, 0); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(greaves == 5);             // Faeburrow pays the second mana and stays home
    // The d0 / playout Solve projects it the same way.
    const TurnSolver::Plan g5 = GreedySolve(FaeBoard(5, 0));
    CHECK((g5.wins_this_turn && Casts(g5, "Lightning Greaves")));
    const TurnSolver::Plan g6 = GreedySolve(FaeBoard(6, 0));
    CHECK_FALSE((g6.wins_this_turn && Casts(g6, "Lightning Greaves")));
}

TEST_CASE("F1 domain dork yield: MTG_ATTACK_DOMAIN_DORK_YIELD=0 restores the static yield")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_DOMAIN_DORK_YIELD, 0 } });
    const int greaves = Threshold([](int l) { return FaeBoard(l, 1); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(greaves == 10);            // the phantom: Faeburrow both pays and swings
    const TurnSolver::Plan g = GreedySolve(FaeBoard(10, 1));
    CHECK((g.wins_this_turn && Casts(g, "Lightning Greaves")));
}

// ---- F6: an Astrolabe's conversion is not supply (MTG_ATTACK_TAP_ASTROLABE_CREDIT) ---------------
// A Snow-Covered Island fed into Arcum's Astrolabe is still ONE mana. With the Island and an
// attacking Boreal Druid the only real sources, Coldsteel Heart ({2}) taps the Druid. The pool books
// the fed Astrolabe as one more (phantom) unit, so the raw total said 3 and the need came out 0.
namespace
{
GameState SnowBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Coldsteel Heart" }, "Snow-Covered Island");
    Put(s, "Snow-Covered Island", 20);
    Put(s, "Arcum's Astrolabe", 21);
    Put(s, "Boreal Druid", 22);
    Put(s, "Frost Augur", 23);
    return s;
}
}

TEST_CASE("F6 Astrolabe credit: a {2} that only the Druid can finish costs the Druid's attack")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    {
        const ManaPool p = AvailableManaPool(SnowBoard(20, 0), nullptr);
        REQUIRE(p.Total() == 3);
        REQUIRE(p.wild_phantom == 1);
    }
    // Pre-plan attack 2: Druid 1 + Augur 1.
    const int heart = Threshold([](int l) { return SnowBoard(l, 0); },
                                [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Coldsteel Heart" }); });
    CHECK(heart == 1);               // the Druid pays, Augur swings alone
    const TurnSolver::Plan g1 = GreedySolve(SnowBoard(1, 0));
    CHECK((g1.wins_this_turn && Casts(g1, "Coldsteel Heart")));
    const TurnSolver::Plan g2 = GreedySolve(SnowBoard(2, 0));
    CHECK_FALSE((g2.wins_this_turn && Casts(g2, "Coldsteel Heart")));
}

TEST_CASE("F6 Astrolabe credit: MTG_ATTACK_TAP_ASTROLABE_CREDIT=0 restores the raw total")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_ASTROLABE_CREDIT, 0 } });
    const int heart = Threshold([](int l) { return SnowBoard(l, 1); },
                                [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Coldsteel Heart" }); });
    CHECK(heart == 2);
    const TurnSolver::Plan g = GreedySolve(SnowBoard(2, 1));
    CHECK((g.wins_this_turn && Casts(g, "Coldsteel Heart")));
}

// ---- F2: Mana Cannons' cast triggers (MTG_ATTACK_CANNONS_TRIGGER) ---------------------------------
// No attackers at all, so any projected damage is the trigger's. Oko is two colours ({1}{G}{U}).
namespace
{
GameState CannonsBoard(int opp, int salt, const std::vector<std::string>& hand, bool cannons_out)
{
    GameState s = Bare(opp, salt, hand);
    if (cannons_out) { Put(s, "Mana Cannons", 30); }
    Put(s, "Forest", 31);
    Put(s, "Island", 32);
    Put(s, "Mountain", 33);
    Put(s, "Forest", 34);
    Put(s, "Island", 35);
    Put(s, "Mountain", 36);
    return s;
}
}

TEST_CASE("F2 Cannons: a multicoloured cast under a Cannons deals its colour count")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const int oko = Threshold([](int l) { return CannonsBoard(l, 0, { "Oko, Thief of Crowns" }, true); },
                              [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Oko, Thief of Crowns" }); });
    CHECK(oko == 2);
    // A Cannons cast EARLIER in the same plan counts (plan order on the generic provider: hand order).
    const int both = Threshold([](int l) { return CannonsBoard(l, 0, { "Mana Cannons", "Oko, Thief of Crowns" }, false); },
                               [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Mana Cannons", "Oko, Thief of Crowns" }); });
    CHECK(both == 2);
    // ...and a Cannons cast AFTER the multicoloured spell does not.
    const int after = Threshold([](int l) { return CannonsBoard(l, 0, { "Oko, Thief of Crowns", "Mana Cannons" }, false); },
                                [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Mana Cannons", "Oko, Thief of Crowns" }); });
    CHECK(after == 0);
    // Under the FiveColour USER order Cannons is cast first (rank 4), whatever the hand order.
    static const FiveColourProvider fc;
    const int fc_order = Threshold([](int l)
                                   {
                                       GameState s = CannonsBoard(l, 0, { "Oko, Thief of Crowns", "Mana Cannons" }, false);
                                       s.m_provider = &fc;
                                       return s;
                                   },
                                   [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Mana Cannons", "Oko, Thief of Crowns" }); });
    CHECK(fc_order == 2);
}

TEST_CASE("F2 Cannons: MTG_ATTACK_CANNONS_TRIGGER=0 projects no trigger damage")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_CANNONS_TRIGGER, 0 } });
    const int oko = Threshold([](int l) { return CannonsBoard(l, 1, { "Oko, Thief of Crowns" }, true); },
                              [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Oko, Thief of Crowns" }); });
    CHECK(oko == 0);
}

// ---- F3: a hard-cast Craterhoof pumps the team (MTG_ATTACK_TEAM_PUMP_ETB) -------------------------
// Two 2-power attackers and eight Forests. Craterhoof enters as the third creature: X = 3, so the
// Knights hit for 5 each and the hasty Hoof for 5 + 3 = 8 -- 18, against the 4 + 5 = 9 of its
// printed power alone.
namespace
{
GameState HoofBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Craterhoof Behemoth" });
    Put(s, "Venerable Knight", 40);
    Put(s, "Venerable Knight", 41);
    for (int k = 0; k < 8; ++k) { Put(s, "Forest", 50 + k); }
    return s;
}
}

TEST_CASE("F3 team pump ETB: Craterhoof's +X reaches every attacker, itself included")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 4: two Knights.
    CHECK(Threshold([](int l) { return HoofBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Craterhoof Behemoth" }); }) == 18);
    const TurnSolver::Plan g = GreedySolve(HoofBoard(18, 0));
    CHECK((g.wins_this_turn && Casts(g, "Craterhoof Behemoth")));
    const TurnSolver::Plan g19 = GreedySolve(HoofBoard(19, 0));
    CHECK_FALSE(g19.wins_this_turn);
}

TEST_CASE("F3 team pump ETB: MTG_ATTACK_TEAM_PUMP_ETB=0 credits the printed power only")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TEAM_PUMP_ETB, 0 } });
    CHECK(Threshold([](int l) { return HoofBoard(l, 1); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Craterhoof Behemoth" }); }) == 9);
}

// ---- F4a: a spent dork loses its team-pump share too (MTG_ATTACK_TAP_REPROJECT) ------------------
// Blossoming Bogbeast + Llanowar Elves attack: Bogbeast gains 2, then +2/+2 to each attacker -> 3 + 1
// + 2 x 2 = 8. Elvish Mystic's {G} can only come from the Llanowar, which then cannot attack and gets
// no pump: Bogbeast alone, 3 + 2 = 5. Subtracting the Llanowar's base power said 7.
namespace
{
GameState BogDorkBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Elvish Mystic" });
    Put(s, "Blossoming Bogbeast", 60);
    Put(s, "Llanowar Elves", 61);
    return s;
}
}

TEST_CASE("F4a re-projection: a tapped dork takes its Bogbeast pump share with it")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 8.
    CHECK(Threshold([](int l) { return BogDorkBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Elvish Mystic" }); }) == 5);
}

TEST_CASE("F4a re-projection: MTG_ATTACK_TAP_REPROJECT=0 subtracts base power only")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_REPROJECT, 0 } });
    CHECK(Threshold([](int l) { return BogDorkBoard(l, 1); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Elvish Mystic" }); }) == 7);
}

// ---- F4b: the plan's own life gain feeds the per-life pumps (MTG_ATTACK_PLAN_LIFEGAIN) -----------
// Bogbeast, Wellwisher and two Llanowar Elves. Tapping Wellwisher gains 1 per Elf (3); then the
// attack: Bogbeast gains 2 more, X = 5, three attackers -> 3 + 1 + 1 + 5 x 3 = 20. Without the plan's
// gain X read only Bogbeast's own 2: 5 + 2 x 3 = 11.
namespace
{
GameState WellBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, {});
    Put(s, "Blossoming Bogbeast", 70);
    Put(s, "Wellwisher", 71);
    Put(s, "Llanowar Elves", 72);
    Put(s, "Llanowar Elves", 73);
    return s;
}
// Nykthos Paragon (4/6) + Wellwisher + Llanowar: the tap gains 2 (two Elves) and the Paragon puts two
// +1/+1 counters on each creature -> Paragon 6 + Llanowar 3 = 9, against 4 + 1 = 5 before.
GameState ParagonBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, {});
    Put(s, "Nykthos Paragon", 80);
    Put(s, "Wellwisher", 81);
    Put(s, "Llanowar Elves", 82);
    return s;
}
bool TapsForLife(const TurnSolver::Plan& p)
{
    bool tap = false;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::PlayLand) { continue; }
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapLifegain)
        { tap = true; continue; }
        return false;
    }
    return tap;
}
}

TEST_CASE("F4b plan lifegain: Wellwisher's gain grows Bogbeast's pump and Paragon's counters")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 14: 3 + 1 + 1 + 1, then Bogbeast's +2 on four attackers.
    CHECK(Threshold([](int l) { return WellBoard(l, 0); }, TapsForLife) == 20);
    // Pre-plan attack 6: 4 + 1 + 1.
    CHECK(Threshold([](int l) { return ParagonBoard(l, 0); }, TapsForLife) == 9);
}

TEST_CASE("F4b plan lifegain: MTG_ATTACK_PLAN_LIFEGAIN=0 reads the pre-plan life gained only")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_PLAN_LIFEGAIN, 0 } });
    CHECK(Threshold([](int l) { return WellBoard(l, 1); }, TapsForLife) == 11);
    CHECK(Threshold([](int l) { return ParagonBoard(l, 1); }, TapsForLife) == 5);
}

// ---- F5.1: Bruna opts into the board-lethal short-circuit (MTG_BRUNA_LETHAL_SHORTCIRCUIT) --------
TEST_CASE("F5.1 Bruna lethal short-circuit: on by default, =0 restores the generic no-cut")
{
    const BrunaProvider bruna;
    heurarm::Clear();
    CHECK(bruna.UseLethalShortCircuit());
    ArmScope arm({ { heurarm::BRUNA_LETHAL_SHORTCIRCUIT, 0 } });
    CHECK_FALSE(bruna.UseLethalShortCircuit());
}

// ---- F5.4: no Karoo-for-Karoo land drop (MTG_LAND_BOUNCE_GUARD) ----------------------------------
namespace
{
GameState KarooBoard()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].lands_played_this_turn = 0;
    Put(s, "Azorius Chancery", 120);
    s.players[0].hand.push_back(HandCard("Azorius Chancery", 121));
    s.players[0].hand.push_back(HandCard("Remote Farm", 122));
    return s;
}
}

TEST_CASE("F5.4 bounce guard: a Chancery that would return the other Chancery yields to Remote Farm")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    CHECK(GreedyLandChoiceIndex(KarooBoard()) == 1);
    {
        ArmScope arm({ { heurarm::LAND_BOUNCE_GUARD, 0 } });
        CHECK(GreedyLandChoiceIndex(KarooBoard()) == 0);   // the legacy pick: the multi-colour Karoo
    }
    // A non-Karoo land on the battlefield is what the bounce returns, so the Karoo is a real ramp.
    GameState s = KarooBoard();
    Put(s, "Plains", 123);
    CHECK(GreedyLandChoiceIndex(s) == 0);
}
