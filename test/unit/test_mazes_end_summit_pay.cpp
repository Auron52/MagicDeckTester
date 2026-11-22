// Maze's End, Guild Summit's PAY-FIRST tap set (USER 2026-11-22, MTG_MAZE_SUMMIT_PAY_FIRST; heurarm slot
// MAZE_SUMMIT_PAY_FIRST): "We can just pay our mana costs and then tap the rst." At resolution the rest of the line is
// paid for real (colour-aware, every non-Gate source first), and every Gate that payment does not use is tapped. The
// searched menu is {all unneeded, none}.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/PlanContext.h"
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
void LoadSummitPayCards()
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
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(C("Forest", 900 + k)); }
    return s;
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

bool TappedByNumber(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p.tapped; } }
    FAIL("no permanent " << num);
    return false;
}

// Resolve a Guild Summit entering `s` (unpinned unless `kx`), returning the cards drawn.
int ResolveSummit(GameState& s, int kx = 0)
{
    Put(s, "Guild Summit", 799);
    const int idx = static_cast<int>(s.battlefield.size()) - 1;
    const std::size_t before = s.players[0].hand.size();
    FireOwnEtbTriggers(s, 0, idx, std::string(), kx);
    return static_cast<int>(s.players[0].hand.size() - before);
}
}   // namespace

TEST_CASE("Maze's End Summit pay-first: the menu is {all unneeded, none}")
{
    LoadSummitPayCards();
    const MazesEndProvider prov;
    const CardDefinition& summit = D("Guild Summit");
    GameState s = Base();
    s.m_provider = &prov;
    Put(s, "Azorius Guildgate", 700);
    Put(s, "Boros Guildgate", 701);
    Put(s, "Dimir Guildgate", 702);
    {
        const Arm on(heurarm::MAZE_SUMMIT_PAY_FIRST, 1);
        CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1 });
        GameState one = Base();
        Put(one, "Azorius Guildgate", 740);
        CHECK(prov.EtbChoiceCandidates(one, 0, summit) == std::vector<int>{ 0, 1 });
        GameState none = Base();
        CHECK(prov.EtbChoiceCandidates(none, 0, summit).size() <= 1);
    }
    {
        const Arm off(heurarm::MAZE_SUMMIT_PAY_FIRST, 0);
        CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1, 4 });
        std::vector<int> elig{ 0, 1, 2 };
        CHECK(prov.EtbTapSubtypePayFirst(s, 0, summit, elig) == -1);
    }
}

TEST_CASE("Maze's End Summit pay-first: the Gate making the colour the line needs stays untapped")
{
    LoadSummitPayCards();
    const MazesEndProvider prov;
    const Arm on(heurarm::MAZE_SUMMIT_PAY_FIRST, 1);
    GameState s = Base();
    s.m_provider = &prov;
    // Golgari is the ONLY green source, and it sits first on the battlefield.
    Put(s, "Golgari Guildgate", 700);
    Put(s, "Azorius Guildgate", 701);
    Put(s, "Boros Guildgate", 702);
    Put(s, "Orzhov Guildgate", 703);
    ManaCost owed; owed.green = 1;   // the rest of the line owes {G}
    const LineUnpaidCostScope luc(owed);
    {
        GameState r = s;
        CHECK(ResolveSummit(r) == 3);
        CHECK_FALSE(TappedByNumber(r, 700));
        CHECK(TappedByNumber(r, 701));
        CHECK(TappedByNumber(r, 702));
        CHECK(TappedByNumber(r, 703));
    }
    // A Forest pays the {G}: every Gate is unneeded.
    {
        GameState r = s;
        Put(r, "Forest", 710);
        CHECK(ResolveSummit(r) == 4);
        CHECK_FALSE(TappedByNumber(r, 710));
    }
    // The pinned "none" taps nothing.
    {
        GameState r = s;
        CHECK(ResolveSummit(r, /*kx=*/1) == 0);
    }
}

TEST_CASE("Maze's End Summit pay-first: Baldur's Gate pays ahead of single-mana Gates; Maze's End's {3} is reserved")
{
    LoadSummitPayCards();
    const MazesEndProvider prov;
    const Arm on(heurarm::MAZE_SUMMIT_PAY_FIRST, 1);
    GameState s = Base();
    s.m_provider = &prov;
    Put(s, "Azorius Guildgate", 700);
    Put(s, "Boros Guildgate", 701);
    Put(s, "Dimir Guildgate", 702);
    Put(s, "Golgari Guildgate", 703);
    Put(s, "Gruul Guildgate", 704);
    Put(s, "Baldur's Gate", 705);   // X = the five other Gates
    {
        // {4} owed: Baldur's Gate fed by two Gates makes five, so three Gates go (the mana-value count was two).
        ManaCost owed; owed.generic = 4;
        const LineUnpaidCostScope luc(owed);
        GameState r = s;
        CHECK(ResolveSummit(r) == 3);
        CHECK_FALSE(TappedByNumber(r, 705));
    }
    {
        // {1} owed: one Gate's {C} covers it -- Baldur's Gate is the one kept.
        ManaCost owed; owed.generic = 1;
        const LineUnpaidCostScope luc(owed);
        GameState r = s;
        CHECK(ResolveSummit(r) == 5);
        CHECK_FALSE(TappedByNumber(r, 705));
    }
    {
        // No line, an untapped Maze's End: its {3} is reserved (its own {T} cannot pay) and paid by Baldur's Gate
        // plus two feeders; the other three Gates draw.
        const LineUnpaidCostScope clean(ManaCost{});
        GameState r = s;
        Put(r, "Maze's End", 720);
        CHECK(ResolveSummit(r) == 3);
        CHECK_FALSE(TappedByNumber(r, 705));
        CHECK_FALSE(TappedByNumber(r, 720));
    }
}

TEST_CASE("Maze's End Summit pay-first NAMED: the {3} reserve only when the applying plan activates Maze's End")
{
    LoadSummitPayCards();
    const MazesEndProvider prov;
    const Arm on(heurarm::MAZE_SUMMIT_PAY_FIRST, 1);
    const Arm named(heurarm::MAZE_SUMMIT_PF_NAMED, 1);
    GameState s = Base();
    s.m_provider = &prov;
    Put(s, "Azorius Guildgate", 700);
    Put(s, "Boros Guildgate", 701);
    Put(s, "Dimir Guildgate", 702);
    Put(s, "Golgari Guildgate", 703);
    Put(s, "Maze's End", 720);
    const LineUnpaidCostScope clean(ManaCost{});
    {
        // No plan apply in scope: the speculative reserve stays (three Gates pay {3}; one draws).
        GameState r = s;
        CHECK(ResolveSummit(r) == 1);
    }
    {
        // A plan that does not activate Maze's End: nothing is owed, every Gate draws.
        PlanTraits t;
        const PlanTraitsScope sc(&t);
        GameState r = s;
        CHECK(ResolveSummit(r) == 4);
        CHECK_FALSE(TappedByNumber(r, 720));
    }
    {
        // A plan that activates Maze's End ({3}, {T}): its {3} is owed and its {T} is not a mana source.
        PlanTraits t;
        t.act_mana_total = 3;
        t.act_src_nums[0] = 720;
        t.act_src_count = 1;
        const PlanTraitsScope sc(&t);
        GameState r = s;
        CHECK(ResolveSummit(r) == 1);
        CHECK_FALSE(TappedByNumber(r, 720));
    }
}

TEST_CASE("Maze's End Summit pay-first ALL3: the all pin stays while an untapped Maze's End is ours")
{
    LoadSummitPayCards();
    const MazesEndProvider prov;
    const CardDefinition& summit = D("Guild Summit");
    const Arm on(heurarm::MAZE_SUMMIT_PAY_FIRST, 1);
    const Arm all3(heurarm::MAZE_SUMMIT_PF_ALL3, 1);
    GameState s = Base();
    s.m_provider = &prov;
    Put(s, "Azorius Guildgate", 700);
    Put(s, "Boros Guildgate", 701);
    Put(s, "Dimir Guildgate", 702);
    CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1 });
    Put(s, "Maze's End", 720);
    CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1, 4 });
    s.battlefield.back().tapped = true;
    CHECK(prov.EtbChoiceCandidates(s, 0, summit) == std::vector<int>{ 0, 1 });
}
