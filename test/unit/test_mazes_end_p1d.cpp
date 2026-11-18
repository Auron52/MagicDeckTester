// Maze's End deck, P1d (2026-11-18; docs/design/analysis-Maze's End.md "Performance"): the USER-directed
// provider prunes in MazesEndProvider -- B1 Guild Summit's count menu, P1 one chosen colour, P2 no red in the
// spendable premise, P3 land-search targets. Each is a flag DEFAULT ON (heurarm slots MAZE_SUMMIT_COUNT,
// MAZE_ONE_COLOUR, MAZE_SPEND_NEVERCAST, MAZE_LAND_TARGETS, plus the P3 amendment MAZE_TALON_TARGET); each test
// also pins the flag off and checks the unpruned menu comes back.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadP1dCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
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

GameState Base()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// The list's spenders (Guild Summit U, Sylvan Scrying G, Gates Ablaze R) so the spendable premise holds.
void StockSpend(GameState& s, int base)
{
    s.players[0].library.push_back(C("Guild Summit", base));
    s.players[0].library.push_back(C("Sylvan Scrying", base + 1));
    s.players[0].library.push_back(C("Gates Ablaze", base + 2));
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

std::vector<int> Sorted(std::vector<int> v) { std::sort(v.begin(), v.end()); return v; }
}   // namespace

TEST_CASE("Maze's End P1d (USER B1): Guild Summit's count menu is {rest-of-line, 0, all}")
{
    LoadP1dCards();
    const MazesEndProvider prov;
    const CardDefinition& summit = D("Guild Summit");
    GameState s = Base();
    s.m_provider = &prov;
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 900 + k)); }
    Put(s, "Azorius Guildgate", 700);
    Put(s, "Boros Guildgate", 701);
    Put(s, "Dimir Guildgate", 702);
    Put(s, "Golgari Guildgate", 703, /*tapped=*/true);
    // Three untapped Gates: pruned to {0 = rest-of-line (unpinned), 1 = N 0, 4 = all}.
    CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1, 4 });
    {
        const Arm off(heurarm::MAZE_SUMMIT_COUNT, 0);
        CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 1, 2, 3, 4 });
        CHECK(prov.EtbTapSubtypeDrawDefault(s, 0, summit, 3) == 3);   // the old unpinned default: all
    }
    // A plan-free context: an empty line hold that carries no activations (another test may leave the thread's
    // g_line_hold_has_acts set; every real apply binds its own scope).
    const LineUnpaidCostScope clean(ManaCost{});
    // The rest-of-line count. Nothing owed, no Maze's End: all three.
    CHECK(prov.EtbTapSubtypeDrawDefault(s, 0, summit, 3) == 3);
    // The line still owes {1}{G} (two mana) and nothing else pays it: tap one, keep two.
    {
        ManaCost owed; owed.generic = 1; owed.green = 1;
        const LineUnpaidCostScope luc(owed);
        CHECK(prov.EtbTapSubtypeDrawDefault(s, 0, summit, 3) == 1);
        // ...an untapped Forest (a non-Gate land) pays one of it.
        GameState t = s;
        Put(t, "Forest", 710);
        CHECK(prov.EtbTapSubtypeDrawDefault(t, 0, summit, 3) == 2);
    }
    // An untapped Maze's End of ours reserves its {3} (the hold carries no activations): tap none of three.
    GameState m = s;
    Put(m, "Maze's End", 720);
    CHECK(prov.EtbTapSubtypeDrawDefault(m, 0, summit, 3) == 0);
    Put(m, "Forest", 721);
    CHECK(prov.EtbTapSubtypeDrawDefault(m, 0, summit, 3) == 1);
    // A tapped Maze's End reserves nothing.
    GameState mt = s;
    Put(mt, "Maze's End", 722, /*tapped=*/true);
    CHECK(prov.EtbTapSubtypeDrawDefault(mt, 0, summit, 3) == 3);
    // The resolver: an unpinned entry (etb_kx 0) taps the rest-of-line count and draws it.
    GameState r = m;   // Maze's End + Forest: one Gate
    Put(r, "Guild Summit", 730);
    const int idx = static_cast<int>(r.battlefield.size()) - 1;
    FireOwnEtbTriggers(r, 0, idx, std::string(), /*etb_kx=*/0);
    CHECK(r.players[0].hand.size() == 1);
    // One untapped Gate: the base {0, all} menu is already within the prune.
    GameState one = Base();
    Put(one, "Azorius Guildgate", 740);
    CHECK(prov.EtbChoiceCandidates(one, 0, summit) == std::vector<int>{ 1, 2 });
}

TEST_CASE("Maze's End P1d (USER P1/P2): a chosen-colour land is offered ONE colour, G before U, never R")
{
    LoadP1dCards();
    const MazesEndProvider prov;
    const CardDefinition& cliff = D("Cliffgate");   // printed R; chooses another colour
    GameState s = Base();
    s.m_provider = &prov;
    StockSpend(s, 100);
    // No green, no blue source: G.
    CHECK(prov.LandEntryPinCandidates(s, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    // A green source, no blue: U.
    GameState g = s;
    Put(g, "Forest", 110);
    CHECK(prov.LandEntryPinCandidates(g, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Blue) });
    // Both: G.
    GameState gu = g;
    Put(gu, "Island", 111);
    CHECK(prov.LandEntryPinCandidates(gu, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    // Blue only: G (no green source yet).
    GameState u = s;
    Put(u, "Island", 112);
    CHECK(prov.LandEntryPinCandidates(u, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    // A filter (Planar Nexus: {1},{T}: any colour) is not a source; a reflector (Gond Gate) is not either.
    GameState f = s;
    Put(f, "Planar Nexus", 113);
    Put(f, "Gond Gate", 114);
    CHECK(prov.LandEntryPinCandidates(f, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    // A Gate whose PRINTED colour is G (Manor Gate) cannot choose G: the admitted U.
    CHECK(prov.LandEntryPinCandidates(g, 0, D("Manor Gate")) == std::vector<int>{ static_cast<int>(Color::Blue) });
    // P1 off: every admitted colour, G/U only (P2: Gates Ablaze is never cast, so red spends nothing).
    {
        const Arm off(heurarm::MAZE_ONE_COLOUR, 0);
        CHECK(Sorted(prov.LandEntryPinCandidates(g, 0, cliff))
              == Sorted({ static_cast<int>(Color::Blue), static_cast<int>(Color::Green) }));
        const Arm off2(heurarm::MAZE_SPEND_NEVERCAST, 0);
        CHECK(Sorted(prov.LandEntryPinCandidates(g, 0, cliff))
              == Sorted({ static_cast<int>(Color::Blue), static_cast<int>(Color::Green) }));   // Cliffgate's own R excluded
        CHECK(Sorted(prov.LandEntryPinCandidates(g, 0, D("Sea Gate")))
              == Sorted({ static_cast<int>(Color::Red), static_cast<int>(Color::Green) }));
    }
    // Gateway Plaza's pay-or-sacrifice is not a colour: untouched by P1 (P5's pay-only pinned off here).
    {
        const Arm plaza_off(heurarm::MAZE_PLAZA_PAY, 0);
        CHECK(Sorted(prov.LandEntryPinCandidates(g, 0, D("Gateway Plaza"))) == std::vector<int>{ 0, 1 });
    }
}

TEST_CASE("Maze's End P1d (USER P3): land-search targets follow the USER's menu")
{
    LoadP1dCards();
    const MazesEndProvider prov;
    const CardParams& scry = D("Sylvan Scrying").params;
    auto lib = [](GameState& s, std::initializer_list<const char*> names, int base)
    {
        int n = base;
        for (const char* nm : names) { s.players[0].library.push_back(C(nm, n++)); }
    };
    // No Maze's End held: Maze's End alone.
    GameState s = Base();
    s.m_provider = &prov;
    lib(s, { "Simic Guildgate", "Maze's End", "Baldur's Gate", "Gond Gate", "Izzet Guildgate", "Sea Gate", "Sea Gate" }, 200);
    CHECK(prov.TutorCandidates(s, 0, scry) == std::vector<std::string>{ "Maze's End" });
    // One Maze's End in HAND, no Amulet: Maze's End, Baldur's (in the library), Gond (none held).
    GameState h = s;
    h.players[0].hand.push_back(C("Maze's End", 250));
    CHECK(prov.TutorCandidates(h, 0, scry) == std::vector<std::string>{ "Maze's End", "Baldur's Gate", "Gond Gate" });
    // ...plus an Amulet of Vigor in hand: Maze's End drops out.
    GameState ha = h;
    ha.players[0].hand.push_back(C("Amulet of Vigor", 251));
    CHECK(prov.TutorCandidates(ha, 0, scry) == std::vector<std::string>{ "Baldur's Gate", "Gond Gate" });
    // Two Maze's Ends (battlefield + hand): out; Gond Gate in hand: out; Baldur's still in the library.
    GameState two = h;
    Put(two, "Maze's End", 252);
    two.players[0].hand.push_back(C("Gond Gate", 253));
    CHECK(prov.TutorCandidates(two, 0, scry) == std::vector<std::string>{ "Baldur's Gate" });
    // Baldur's Gate gone (on our battlefield) and Gond had: ONE new-name Gate, a singleton before Sea Gate (2 left).
    GameState nn = Base();
    nn.m_provider = &prov;
    lib(nn, { "Simic Guildgate", "Maze's End", "Gond Gate", "Sea Gate", "Sea Gate" }, 300);
    Put(nn, "Maze's End", 310);
    Put(nn, "Maze's End", 311);
    Put(nn, "Baldur's Gate", 312);
    Put(nn, "Gond Gate", 313);
    CHECK(prov.TutorCandidates(nn, 0, scry) == std::vector<std::string>{ "Simic Guildgate" });
    // ...a new name we control already is not one: Simic on the battlefield -> Sea Gate.
    GameState nn2 = nn;
    Put(nn2, "Simic Guildgate", 314);
    CHECK(prov.TutorCandidates(nn2, 0, scry) == std::vector<std::string>{ "Sea Gate" });
    // Flag off: every distinct name.
    {
        const Arm off(heurarm::MAZE_LAND_TARGETS, 0);
        CHECK(prov.TutorCandidates(nn, 0, scry).size() == 4);
    }
    // Maze's End's own fetch: the same menu without Maze's End, the fail-to-find kept (returns false). P5's
    // fail-to-find prune (MTG_MAZE_FIND_NOTHING) is pinned off for the rest of this test.
    const Arm find_off(heurarm::MAZE_FIND_NOTHING, 0);
    const CardDefinition& maze = D("Maze's End");
    GameState f = Base();
    f.m_provider = &prov;
    Put(f, "Maze's End", 400);
    Put(f, "Azorius Guildgate", 401);
    std::vector<std::string> names = { "Simic Guildgate", "Baldur's Gate", "Gond Gate", "Izzet Guildgate" };
    lib(f, { "Simic Guildgate", "Baldur's Gate", "Gond Gate", "Izzet Guildgate" }, 410);
    CHECK_FALSE(prov.TapTutorPutCandidates(f, maze, names));
    CHECK(names == std::vector<std::string>{ "Baldur's Gate", "Gond Gate" });
    // Gond Gate in hand and Baldur's held: one new name.
    GameState f2 = Base();
    f2.m_provider = &prov;
    Put(f2, "Maze's End", 400);
    Put(f2, "Azorius Guildgate", 401);
    lib(f2, { "Simic Guildgate", "Gond Gate", "Izzet Guildgate" }, 430);
    f2.players[0].hand.push_back(C("Gond Gate", 420));
    Put(f2, "Baldur's Gate", 421);
    names = { "Simic Guildgate", "Gond Gate", "Izzet Guildgate" };
    CHECK_FALSE(prov.TapTutorPutCandidates(f2, maze, names));
    CHECK(names.size() == 1);
    CHECK((names.front() == "Simic Guildgate" || names.front() == "Izzet Guildgate"));
}

TEST_CASE("Maze's End P1d (P3 amendment, MTG_MAZE_TALON_TARGET default ON): a land tutor also offers Talon Gates")
{
    LoadP1dCards();
    const MazesEndProvider prov;
    const CardParams& scry = D("Sylvan Scrying").params;
    GameState s = Base();
    s.m_provider = &prov;
    int n = 500;
    for (const char* nm : { "Simic Guildgate", "Maze's End", "Baldur's Gate", "Gond Gate", "Talon Gates of Madara" })
    { s.players[0].library.push_back(C(nm, n++)); }
    s.players[0].hand.push_back(C("Maze's End", 550));
    // Default ON: the USER's P3 menu (Maze's End, Baldur's, Gond) plus Talon Gates -- its {4} put from hand is not
    // a land play, so it adds a Gate name without competing for the drop.
    CHECK(prov.TutorCandidates(s, 0, scry)
          == std::vector<std::string>{ "Maze's End", "Baldur's Gate", "Gond Gate", "Talon Gates of Madara" });
    {
        const Arm off(heurarm::MAZE_TALON_TARGET, 0);
        CHECK(prov.TutorCandidates(s, 0, scry) == std::vector<std::string>{ "Maze's End", "Baldur's Gate", "Gond Gate" });
    }
    // Held in hand: not offered (a second copy is not a new name).
    GameState h = s;
    h.players[0].hand.push_back(C("Talon Gates of Madara", 551));
    CHECK(prov.TutorCandidates(h, 0, scry) == std::vector<std::string>{ "Maze's End", "Baldur's Gate", "Gond Gate" });
    // Controlled: not offered.
    GameState b = s;
    Put(b, "Talon Gates of Madara", 552);
    CHECK(prov.TutorCandidates(b, 0, scry) == std::vector<std::string>{ "Maze's End", "Baldur's Gate", "Gond Gate" });
    // With no Maze's End held the USER's "{Maze's End} alone" rule still wins.
    GameState none = Base();
    none.m_provider = &prov;
    n = 600;
    for (const char* nm : { "Maze's End", "Baldur's Gate", "Talon Gates of Madara" }) { none.players[0].library.push_back(C(nm, n++)); }
    CHECK(prov.TutorCandidates(none, 0, scry) == std::vector<std::string>{ "Maze's End" });
}
