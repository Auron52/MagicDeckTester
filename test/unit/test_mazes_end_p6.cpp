// Maze's End deck, P6 (2026-11-18; docs/design/analysis-Maze's End.md "P6"): the NON-WINNING off-main Maze's End
// activation (USER ruling 6). (1) RESPOND: with a Manabond (on a yes) or Burgeoning trigger on the stack an untapped
// Maze's End is activated and the trigger puts it back -- MazesEndProvider forces the activation, the fetch target
// stays a searched crossing ordinal. (2) BANK: after an off-main put, a searched ordinal {decline, target...}.
// Both levers are DEFAULT ON (MTG_MAZE_OFFMAIN_RESPOND / MTG_MAZE_OFFMAIN_BANK); each test also pins them off.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
void LoadP6Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card C(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

const Permanent* Find(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool InHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}

bool InLibrary(const GameState& s, int num)
{
    for (const Card& c : s.players[0].library) { if (c.m_number == num) { return true; } }
    return false;
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

// Our end step: `trigger_card` out, an untapped Maze's End (701) and three untapped Guildgates (the {3}); the library
// holds one fetchable Gate (Simic Guildgate, 800) and Forests.
GameState Board(const DecisionProvider& prov, const char* trigger_card)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::Ending;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    Put(s, trigger_card, 700);
    Put(s, "Maze's End", 701);
    Put(s, "Azorius Guildgate", 702);
    Put(s, "Boros Guildgate", 703);
    Put(s, "Dimir Guildgate", 704);
    s.players[0].library.push_back(C("Simic Guildgate", 800));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 810 + k)); }
    return s;
}

int RunManabond(GameState& t, CrossingLandProbe& pr)
{
    t_crossing_land_probe = &pr;
    PerformEndStepManabond(t);
    t_crossing_land_probe = nullptr;
    return pr.n;
}
}   // namespace

TEST_CASE("Maze's End P6 (USER ruling 6): Maze's End answers Manabond and is put back by the yes")
{
    LoadP6Cards();
    const MazesEndProvider prov;
    GameState s = Board(prov, "Manabond");
    s.players[0].hand.push_back(C("Gruul Guildgate", 710));
    {
        GameState t = s;
        CrossingLandProbe pr;
        CHECK(RunManabond(t, pr) == 0);              // all-land hand: yes forced; one fetch target: no ordinal
        REQUIRE(Find(t, 800) != nullptr);            // the fetched Gate
        REQUIRE(Find(t, 701) != nullptr);            // Maze's End redeployed by the yes
        CHECK(Find(t, 701)->tapped);                 // "This land enters tapped" (no Amulet)
        CHECK(Find(t, 710) != nullptr);
        CHECK(Find(t, 702)->tapped);                 // the {3}
        CHECK(Find(t, 703)->tapped);
        CHECK(Find(t, 704)->tapped);
        CHECK(t.players[0].hand.empty());
        CHECK_FALSE(t.won_by_effect);
    }
    {
        const Arm r(heurarm::MAZE_OFFMAIN_RESPOND, 0);
        const Arm b(heurarm::MAZE_OFFMAIN_BANK, 0);
        GameState t = s;
        PerformEndStepManabond(t);
        REQUIRE(Find(t, 701) != nullptr);
        CHECK_FALSE(Find(t, 701)->tapped);           // pre-P6: no response
        CHECK(InLibrary(t, 800));
        CHECK(Find(t, 710) != nullptr);
    }
    // The {3} is not there: no response.
    {
        GameState t = s;
        for (Permanent& p : t.battlefield) { if (p.card.m_number == 703) { p.tapped = true; } }
        PerformEndStepManabond(t);
        CHECK(InLibrary(t, 800));
        CHECK_FALSE(Find(t, 701)->tapped);
    }
    // Nothing fetchable: no response (the fail-to-find is never offered off-main).
    {
        GameState t = s;
        t.players[0].library.erase(t.players[0].library.begin());
        PerformEndStepManabond(t);
        CHECK_FALSE(Find(t, 701)->tapped);
    }
}

TEST_CASE("Maze's End P6: an EMPTY hand plus a response is the all-land hand {Maze's End}; a nonland hand keeps no")
{
    LoadP6Cards();
    const MazesEndProvider prov;
    {
        GameState t = Board(prov, "Manabond");
        CrossingLandProbe pr;
        CHECK(RunManabond(t, pr) == 0);              // yes forced, the response taken
        CHECK(Find(t, 800) != nullptr);
        CHECK(Find(t, 701) != nullptr);
        CHECK(t.players[0].hand.empty());
    }
    GameState s = Board(prov, "Manabond");
    s.players[0].hand.push_back(C("Exploration", 710));
    {
        GameState t = s;
        CrossingLandProbe pr;
        CHECK(RunManabond(t, pr) == 1);              // yes / no searched (yes would discard Exploration)
        CHECK(pr.size[0] == 2);
        CHECK(InHand(t, 710));                       // rank 0 = no (the pre-P6 default for a land-less hand)
        CHECK(InLibrary(t, 800));
    }
    {
        GameState t = s;
        t.scripted_crossing_lands = 1;               // the searched yes: respond, put Maze's End back, discard
        PerformEndStepManabond(t);
        CHECK(Find(t, 800) != nullptr);
        CHECK(Find(t, 701) != nullptr);
        CHECK(t.players[0].hand.empty());
    }
    {
        const Arm r(heurarm::MAZE_OFFMAIN_RESPOND, 0);
        GameState t = s;
        CrossingLandProbe pr;
        CHECK(RunManabond(t, pr) == 0);              // pre-P6: a land-less yes is dominated (decision 28)
        CHECK(InHand(t, 710));
    }
}

TEST_CASE("Maze's End P6: Maze's End answers Burgeoning; the put is restricted to it, the other land stays in hand")
{
    LoadP6Cards();
    const MazesEndProvider prov;
    GameState s = Board(prov, "Burgeoning");
    s.players[0].hand.push_back(C("Forest", 711));
    {
        GameState t = s;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 0);                            // activation forced, one target, the put forced
        CHECK(Find(t, 800) != nullptr);
        REQUIRE(Find(t, 701) != nullptr);
        CHECK(Find(t, 701)->tapped);
        CHECK(InHand(t, 711));
    }
    {
        const Arm r(heurarm::MAZE_OFFMAIN_RESPOND, 0);
        const Arm b(heurarm::MAZE_OFFMAIN_BANK, 0);
        GameState t = s;
        ResolveBurgeoningTriggers(t);
        CHECK(Find(t, 711) != nullptr);              // pre-P6: the Forest is put, Maze's End untouched
        CHECK_FALSE(Find(t, 701)->tapped);
        CHECK(InLibrary(t, 800));
    }
    // Unrestricted (the base provider): activate / decline is searched -- {Simic, decline} -- and so is the put.
    {
        const GenericProvider base;
        GameState t = s;
        t.m_provider = &base;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        REQUIRE(pr.n == 2);
        CHECK(pr.size[0] == 2);                      // the response ordinal
        CHECK(pr.size[1] == 3);                      // Forest, Maze's End, none
        GameState u = s;
        u.m_provider = &base;
        u.scripted_crossing_lands = 1;               // decline: the pre-P6 menu {Forest, none}
        ResolveBurgeoningTriggers(u);
        CHECK(Find(u, 711) != nullptr);
        CHECK(InLibrary(u, 800));
    }
}

TEST_CASE("Maze's End P6: after a Burgeoning put, banking a Gate is a SEARCHED ordinal (decline first)")
{
    LoadP6Cards();
    const MazesEndProvider prov;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::Ending;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    Put(s, "Burgeoning", 700);
    Put(s, "Amulet of Vigor", 705);                  // the put Maze's End untaps
    Put(s, "Azorius Guildgate", 702);
    Put(s, "Boros Guildgate", 703);
    Put(s, "Dimir Guildgate", 704);
    s.players[0].hand.push_back(C("Maze's End", 701));
    s.players[0].library.push_back(C("Baldur's Gate", 800));
    s.players[0].library.push_back(C("Gond Gate", 801));
    s.players[0].library.push_back(C("Simic Guildgate", 802));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 810 + k)); }
    {
        GameState t = s;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        REQUIRE(pr.n == 2);
        CHECK(pr.size[0] == 2);                      // Burgeoning: Maze's End, none
        CHECK(pr.size[1] == 3);                      // bank: decline, Baldur's Gate, Gond Gate (the P3 menu)
        REQUIRE(Find(t, 701) != nullptr);
        CHECK_FALSE(Find(t, 701)->tapped);           // rank 0 = decline: untouched
    }
    {
        GameState t = s;
        t.scripted_crossing_lands = 0 | (2LL << kCrossingLandPinBits);   // put Maze's End; bank for Gond Gate
        ResolveBurgeoningTriggers(t);
        CHECK(Find(t, 801) != nullptr);
        CHECK(InHand(t, 701));
        CHECK(Find(t, 702)->tapped);
    }
    {
        const Arm b(heurarm::MAZE_OFFMAIN_BANK, 0);
        GameState t = s;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 1);                            // no bank ordinal
    }
    // No put (rank 1 = none): nothing new on the board, no bank offered.
    {
        GameState t = s;
        t.scripted_crossing_lands = 1;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 1);
    }
}

TEST_CASE("Maze's End P6: after Manabond's dump, the bank; and the crossing fans the new choice")
{
    LoadP6Cards();
    const MazesEndProvider prov;
    // Manabond dumps Maze's End (Amulet untaps it); then the bank is offered.
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::Ending;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    Put(s, "Manabond", 700);
    Put(s, "Amulet of Vigor", 705);
    Put(s, "Azorius Guildgate", 702);
    Put(s, "Boros Guildgate", 703);
    Put(s, "Dimir Guildgate", 704);
    s.players[0].hand.push_back(C("Maze's End", 701));
    s.players[0].library.push_back(C("Simic Guildgate", 800));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 810 + k)); }
    {
        GameState t = s;
        CrossingLandProbe pr;
        CHECK(RunManabond(t, pr) == 1);              // the all-land yes is forced; the bank {decline, Simic}
        CHECK(pr.size[0] == 2);
        CHECK(Find(t, 701) != nullptr);
        GameState u = s;
        u.scripted_crossing_lands = 1;               // bank
        PerformEndStepManabond(u);
        CHECK(Find(u, 800) != nullptr);
        CHECK(InHand(u, 701));
    }
    // The boundary fan sees the response's yes/no where the pre-P6 predicate saw nothing to decide.
    GameState f = Board(prov, "Manabond");
    f.phase = Phase::PostCombatMain;
    f.players[0].hand.push_back(C("Exploration", 710));
    CHECK(TurnSolver::CrossingLandBoundaryPinsForTest(f).size() == 2);
    {
        const Arm r(heurarm::MAZE_OFFMAIN_RESPOND, 0);
        const Arm b(heurarm::MAZE_OFFMAIN_BANK, 0);
        CHECK(TurnSolver::CrossingLandBoundaryPinsForTest(f).size() == 1);
    }
}
