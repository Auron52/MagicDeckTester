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

// These cases read the PROJECTION inside Solve's subset walk, which MTG_GREEDY_LETHAL_STOP bypasses on a board
// already lethal in combat (it returns the verified attack-only plan). So the helper runs with the stop OFF
// unless a case sets the slot itself; the stop has its own case at the end of this file.
TurnSolver::Plan GreedySolve(const GameState& s)
{
    const std::int8_t prev = heurarm::t_arm[heurarm::GREEDY_LETHAL_STOP];
    if (prev < 0) { heurarm::t_arm[heurarm::GREEDY_LETHAL_STOP] = 0; }
    TurnSolver::Plan p = TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    heurarm::t_arm[heurarm::GREEDY_LETHAL_STOP] = prev;
    return p;
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

// ---- The attack-only plan competes in Solve (MTG_SOLVE_ATTACK_ONLY_WIN) --------------------------
// Solve's walk skips the empty selection, so a lethal board whose every castable plan taps an
// attacker used to cast the best-eval spell and lose the lethal. The Faeburrow board at 6: the board
// swings for 10, Greaves (which taps Faeburrow) for 5.
TEST_CASE("Solve attack-only win: a lethal board attacks instead of casting into a lost lethal")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const TurnSolver::Plan g = GreedySolve(FaeBoard(6, 2));
    CHECK(g.wins_this_turn);
    CHECK_FALSE(Casts(g, "Lightning Greaves"));
    // Where a cast plan wins too, it is still the one taken (the seed loses every tie).
    const TurnSolver::Plan g5 = GreedySolve(FaeBoard(5, 2));
    CHECK((g5.wins_this_turn && Casts(g5, "Lightning Greaves")));
    // Not lethal at all: nothing to seed.
    const TurnSolver::Plan g11 = GreedySolve(FaeBoard(11, 2));
    CHECK_FALSE(g11.wins_this_turn);
}

// ---- MTG_GREEDY_LETHAL_STOP: the greedy policy stops casting on a verified combat-lethal board ---------------
// FaeBoard at 5: the board swings for 10 and Greaves (which taps Faeburrow) still wins on 5 -- the walk keeps the
// cast (the seed loses every tie). With the stop on, the greedy policy takes the VERIFIED attack-only plan instead:
// a cast that taps an attacker can only undo a kill the projection prices off lands (AccomplishedAlchemist d0
// s1001 gi976: the pumped Alchemist tapped for a post-route Scholar).
TEST_CASE("Greedy lethal stop: a combat-lethal board attacks without casting")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::GREEDY_LETHAL_STOP, 1 } });
    const TurnSolver::Plan g5 = GreedySolve(FaeBoard(5, 4));
    CHECK(g5.wins_this_turn);
    CHECK_FALSE(Casts(g5, "Lightning Greaves"));
    const TurnSolver::Plan g10 = GreedySolve(FaeBoard(10, 4));
    CHECK(g10.wins_this_turn);
    CHECK_FALSE(Casts(g10, "Lightning Greaves"));
    // Not lethal: the stop does not fire, the walk runs as before.
    const TurnSolver::Plan g11 = GreedySolve(FaeBoard(11, 4));
    CHECK_FALSE(g11.wins_this_turn);
}

TEST_CASE("Solve attack-only win: MTG_SOLVE_ATTACK_ONLY_WIN=0 restores the unscored empty plan")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::SOLVE_ATTACK_ONLY_WIN, 0 } });
    const TurnSolver::Plan g = GreedySolve(FaeBoard(6, 3));
    CHECK_FALSE(g.wins_this_turn);
    CHECK(Casts(g, "Lightning Greaves"));
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

// ---- F4c: the walk counts the plan's casts toward STORM (Weather the Storm) ------------------------
// Bogbeast + Llanowar attack; two spells were already cast this turn, so Weather the Storm resolves as
// the THIRD spell: 3 instances x 3 = 9 life, then the attack's +2 -> X = 11 on two attackers:
// 3 + 1 + 11 x 2 = 26. A walk that did not count its own cast saw 2 instances (6 + 2 -> 3 + 1 + 16 = 20).
namespace
{
GameState StormBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Weather the Storm" });
    s.spells_cast_this_turn = 2;
    Put(s, "Blossoming Bogbeast", 75);
    Put(s, "Llanowar Elves", 76);
    Put(s, "Forest", 77);
    Put(s, "Forest", 78);
    return s;
}
}

TEST_CASE("F4c plan lifegain: Weather the Storm's storm copies are projected from the plan's own count")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    CHECK(Threshold([](int l) { return StormBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Weather the Storm" }); }) == 26);
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

// ---- F5.2: creature-only mana cannot pay the noncreature share (MTG_ATTACK_TAP_CREATURE_ONLY) ----
// Somberwald Sage's three are creature-only. Lightning Greaves ({2}) can only be paid by the two
// attacking Avacyn's Pilgrims, so both stay home: 4 -> 2. The all-spells need was met by tapping the
// 0-power Sage first (least power), whose mana cannot pay a noncreature spell -- so it charged nothing.
namespace
{
GameState SageBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Lightning Greaves" });
    Put(s, "Somberwald Sage", 90);
    Put(s, "Avacyn's Pilgrim", 91);
    Put(s, "Avacyn's Pilgrim", 92);
    Put(s, "Venerable Knight", 93);
    return s;
}
}

TEST_CASE("F5.2 creature-only mana: an Aura-class cost taps the dorks that can pay it")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 4: Knight 2 + two Pilgrims (the Sage swings for 0).
    CHECK(Threshold([](int l) { return SageBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); }) == 2);
}

TEST_CASE("F5.2 creature-only mana: MTG_ATTACK_TAP_CREATURE_ONLY=0 restores the all-spells need")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_CREATURE_ONLY, 0 } });
    CHECK(Threshold([](int l) { return SageBoard(l, 1); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); }) == 4);
}

// ---- F5.3: an Aura that taps its host on entry takes the host out (MTG_ATTACK_AURA_HOST_TAP) -----
namespace
{
GameState ColossBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Colossification" });
    Put(s, "Venerable Knight", 100);
    for (int k = 0; k < 7; ++k) { Put(s, "Forest", 110 + k); }
    return s;
}
}

TEST_CASE("F5.3 aura host tap: Colossification on the only attacker leaves nothing to swing")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 2: the Knight.
    CHECK(Threshold([](int l) { return ColossBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Colossification" }); }) == 0);
}

TEST_CASE("F5.3 aura host tap: MTG_ATTACK_AURA_HOST_TAP=0 keeps the tapped host's attack")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_AURA_HOST_TAP, 0 } });
    CHECK(Threshold([](int l) { return ColossBoard(l, 1); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Colossification" }); }) == 2);
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

// ---- Point 8: Garth's conjure taps Garth (MTG_ATTACK_TAP_ABILITY_KINDS) ----------------------------
// Garth One-Eye is the only attacker; conjuring Black Lotus ({0}) taps him, so the plan swings for 0.
namespace
{
GameState GarthBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, {});
    Put(s, "Garth One-Eye", 130);
    return s;
}
bool GarthOnly(const TurnSolver::Plan& p)
{
    bool garth = false;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::PlayLand) { continue; }
        if (a.kind != Action::Kind::GarthActivate) { return false; }
        garth = true;
    }
    return garth;
}
}

TEST_CASE("Point 8 ability kinds: a Garth conjure takes Garth out of the attack")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 5: Garth.
    CHECK(Threshold([](int l) { return GarthBoard(l, 0); }, GarthOnly) == 0);
}

TEST_CASE("Point 8 ability kinds: MTG_ATTACK_TAP_ABILITY_KINDS=0 keeps the tapped Garth's 5")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_ABILITY_KINDS, 0 } });
    CHECK(Threshold([](int l) { return GarthBoard(l, 1); }, GarthOnly) == 5);
}

// ---- Point 9: the executor's payer order (MTG_ATTACK_TAP_PAYER_ORDER) -----------------------------
// Three Forests and four 2-power Elves (Archdruid pumps the other three to 2/2; it taps for 4).
// Craterhoof's 8 leaves 5 to the Elves: on an attack turn the payment taps the scaler first, so
// Archdruid + one Llanowar pay and Llanowar + Fyndhorn swing -- with Hoof's X = 5: 7 + 7 + Hoof 10 =
// 24. Lowest-power-first with board-order ties tapped all four Elves: Hoof alone, 10.
namespace
{
GameState ElfHoofBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Craterhoof Behemoth" });
    Put(s, "Forest", 140);
    Put(s, "Forest", 141);
    Put(s, "Forest", 142);
    Put(s, "Llanowar Elves", 143);
    Put(s, "Llanowar Elves", 144);
    Put(s, "Fyndhorn Elves", 145);
    Put(s, "Elvish Archdruid", 146);
    return s;
}
}

TEST_CASE("Point 9 payer order: Archdruid pays for Craterhoof, the flat Elves swing")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Pre-plan attack 8: three pumped Elves at 2 + Archdruid 2.
    CHECK(Threshold([](int l) { return ElfHoofBoard(l, 0); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Craterhoof Behemoth" }); }) == 24);
}

TEST_CASE("Point 9 payer order: MTG_ATTACK_TAP_PAYER_ORDER=0 spends lowest power, board order")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_PAYER_ORDER, 0 } });
    CHECK(Threshold([](int l) { return ElfHoofBoard(l, 1); },
                    [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Craterhoof Behemoth" }); }) == 10);
}

// ---- Point 10: a re-projection keeps the pre-plan attack intent (MTG_ATTACK_REPROJECT_INTENT) -----
// FiveColour d0 gi1924's T5 board (seed 5101925), after the Misty Rainforest -> Steam Vents drop:
// four lands, Deathrite Shaman (fuelled by the Misty in the graveyard), Faeburrow Elder (5/5, taps
// for five), Birds of Paradise and a hasty Two-Headed Hellkite. Pre-plan attack 11: Hellkite 5 +
// Faeburrow 5 + Deathrite 1 -- Deathrite swings only because the eligible total is lethal
// (FiveColourProvider::ShouldAttackWith's lethal exemption). {Mana Cannons, Ancient Cornucopia,
// Maelstrom Archangel} costs 11, so Faeburrow and Birds pay and stay home; Archangel is five colours
// under the Cannons: 5 + Hellkite 5 + Deathrite 1 = 11. The re-projection tapped Faeburrow + Birds
// and re-asked the hold on that board -- eligible total 6 against an opponent still at 11, castable
// cards still in hand -- so it held Deathrite and read 10.
namespace
{
GameState DeathriteBoard(int opp, int salt)
{
    GameState s = Bare(opp, salt, { "Ancient Cornucopia", "Mana Cannons", "Maelstrom Archangel",
                                     "Nicol Bolas, Planeswalker", "Godless Shrine" });
    static const FiveColourProvider fc;
    s.m_provider = &fc;
    s.players[0].life = 9 + salt;
    Put(s, "Overgrown Tomb", 150);
    Put(s, "Deathrite Shaman", 151);
    Put(s, "Blood Crypt", 152);
    Put(s, "Faeburrow Elder", 153);
    Put(s, "Breeding Pool", 154);
    Put(s, "Two-Headed Hellkite", 155);
    Put(s, "Birds of Paradise", 156);
    Put(s, "Steam Vents", 157);
    s.players[0].graveyard.push_back(HandCard("Misty Rainforest", 158));
    return s;
}
bool CannonsCornucopiaArchangel(const TurnSolver::Plan& p)
{
    return CastsOnly(p, { "Mana Cannons", "Ancient Cornucopia", "Maelstrom Archangel" });
}
bool ArchangelAlone(const TurnSolver::Plan& p) { return CastsOnly(p, { "Maelstrom Archangel" }); }
}

TEST_CASE("Point 10 re-projection intent: Deathrite still swings once the plan taps Faeburrow")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    {   // The hold itself: the pre-plan board swings Deathrite; the half-updated one holds it.
        static const FiveColourProvider fc;
        GameState s = DeathriteBoard(11, 0);
        auto drs = [&]() -> const Permanent*
        {
            for (const Permanent& p : s.battlefield) { if (p.card.m_number == 151) { return &p; } }
            return nullptr;
        };
        REQUIRE(drs() != nullptr);
        CHECK(fc.AttackWith(s, *drs()));
        for (Permanent& p : s.battlefield)
        { if (p.card.m_number == 153 || p.card.m_number == 156) { p.tapped = true; } }
        CHECK_FALSE(fc.AttackWith(s, *drs()));
    }
    CHECK(Threshold([](int l) { return DeathriteBoard(l, 0); }, CannonsCornucopiaArchangel) == 11);
    CHECK(Threshold([](int l) { return DeathriteBoard(l, 0); }, ArchangelAlone) == 11);
}

TEST_CASE("Point 10 re-projection intent: MTG_ATTACK_REPROJECT_INTENT=0 re-asks the hold on the tapped board")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_REPROJECT_INTENT, 0 } });
    CHECK(Threshold([](int l) { return DeathriteBoard(l, 1); }, CannonsCornucopiaArchangel) == 10);
    CHECK(Threshold([](int l) { return DeathriteBoard(l, 1); }, ArchangelAlone) == 11);
}

// ---- F7: the attack's free cast (MTG_ATTACK_FREECAST_DAMAGE) ---------------------------------------
// FiveColour d0 gi1106's T4 board (seed 6211107): three untapped lands (the Triome drop entered
// tapped), Birds of Paradise, Faeburrow Elder (5/5, taps for five under the Archangel's WUBRG) and a
// Maelstrom Archangel cast last turn. Pre-plan attack 10: Archangel 5 + vigilant Faeburrow 5. Unite the
// Coalition costs 7 of the 9 available, so casting it pre-combat taps Faeburrow: 10 direct + Archangel
// 5. The Archangel connects and frees the SECOND Unite: 10 more. Without the term {Unite} read 15, no
// plan read lethal at 20, and d0 cast Archangel + Faeburrow instead (won T5, the T4 kill missed).
namespace
{
GameState FreeCastBoard(int opp, int salt, const std::vector<std::string>& hand)
{
    GameState s = Bare(opp, salt, hand);
    static const FiveColourProvider fc;
    s.m_provider = &fc;
    Put(s, "Stomping Ground", 160);
    Put(s, "Godless Shrine", 161);
    Put(s, "Island", 162);
    Put(s, "Zagoth Triome", 163, /*tapped=*/true);
    Put(s, "Birds of Paradise", 164);
    Put(s, "Faeburrow Elder", 165);
    Put(s, "Maelstrom Archangel", 166);
    return s;
}
bool UniteAlone(const TurnSolver::Plan& p) { return CastsOnly(p, { "Unite the Coalition" }); }
}

TEST_CASE("F7 free cast: a connecting Archangel frees the second Unite's 10")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    {
        const GameState probe = FreeCastBoard(20, 0, { "Unite the Coalition", "Unite the Coalition" });
        REQUIRE(DomainColors(probe, 0).size() == 5);
        REQUIRE(AvailableManaPool(probe, nullptr).Total() == 9);   // 3 lands + Birds 1 + Faeburrow 5
    }
    // {Unite}: 10 direct + Archangel 5 (Faeburrow pays) + the free Unite 10.
    CHECK(Threshold([](int l) { return FreeCastBoard(l, 0, { "Unite the Coalition", "Unite the Coalition" }); },
                    UniteAlone) == 25);
    const TurnSolver::Plan g20 = GreedySolve(FreeCastBoard(20, 0, { "Unite the Coalition", "Unite the Coalition" }));
    CHECK(g20.wins_this_turn);
    // With ONE Unite the cast leaves nothing to free (15), but the attack-only plan swings 10 and frees
    // that Unite: 20 -- Faeburrow attacks and the free cast spends no mana at all.
    CHECK(Threshold([](int l) { return FreeCastBoard(l, 0, { "Unite the Coalition" }); }, UniteAlone) == 15);
    const TurnSolver::Plan one20 = GreedySolve(FreeCastBoard(20, 0, { "Unite the Coalition" }));
    CHECK(one20.wins_this_turn);
    CHECK_FALSE(Casts(one20, "Unite the Coalition"));
    const TurnSolver::Plan one21 = GreedySolve(FreeCastBoard(21, 0, { "Unite the Coalition" }));
    CHECK_FALSE(one21.wins_this_turn);
    // A tapped Archangel cannot attack, so it frees nothing.
    GameState tapped = FreeCastBoard(20, 0, { "Unite the Coalition", "Unite the Coalition" });
    for (Permanent& p : tapped.battlefield) { if (p.card.m_number == 166) { p.tapped = true; } }
    CHECK_FALSE(GreedySolve(tapped).wins_this_turn);
}

TEST_CASE("F7 free cast: MTG_ATTACK_FREECAST_DAMAGE=0 projects no free cast")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_FREECAST_DAMAGE, 0 } });
    CHECK(Threshold([](int l) { return FreeCastBoard(l, 1, { "Unite the Coalition", "Unite the Coalition" }); },
                    UniteAlone) == 15);
    CHECK_FALSE(GreedySolve(FreeCastBoard(20, 1, { "Unite the Coalition", "Unite the Coalition" })).wins_this_turn);
    CHECK_FALSE(GreedySolve(FreeCastBoard(20, 1, { "Unite the Coalition" })).wins_this_turn);
}
