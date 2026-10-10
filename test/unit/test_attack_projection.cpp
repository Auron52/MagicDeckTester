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
