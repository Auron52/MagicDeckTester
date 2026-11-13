// Spirits onboarding, integrator A (2026-11-13): SpiritsProvider routing and its lossless fetch surface,
// the two Spirit lords (Drogskol Captain, Supreme Phantom), Geist of Saint Traft's end-of-combat Angel,
// and Spirit of the Labyrinth's fail-closed draw-cap guard.
//
// The combat cases replay the declare-attackers -> damage -> end-of-combat sequence BOTH worlds run
// (GameEngine::CombatPhase and TurnSolver's SimulateOneCombatAfterTitan) through the SAME shared helpers
// they call (FireAttackCreateTokens, ResolveCombatDamage, ExileEndOfCombatTokens), so a pass here is a
// pass in both worlds; test/scenarios/spirits_*.json drive the real executor and search end to end.
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& SpDef(const std::string& name)
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

int SpPut(GameState& s, const std::string& name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card             = SpDef(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card SpCard(const std::string& name, int number)
{
    Card c     = SpDef(name).card;
    c.m_number = number;
    return c;
}

GameState SpFresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

int Angels(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && CardHasSubtype(p.card, "Angel")) { ++n; } }
    return n;
}

// The combat sequence both worlds run for this card: declare (tokens join the attacker list), combat
// damage, then the end-of-combat exile. Returns the damage dealt.
int SwingAndEndCombat(GameState& s, std::vector<int> atk, std::vector<int>* eoc_out = nullptr)
{
    std::vector<int> eoc;
    if (!atk.empty())
    {
        const int tok_start = FireAttackCreateTokens(s, 0, &atk, &eoc);
        for (int i = tok_start; i < static_cast<int>(s.battlefield.size()); ++i) { atk.push_back(i); }
    }
    const int before = s.players[1].life;
    ResolveCombatDamage(s, atk, 0, /*collect_descs=*/false);
    if (eoc_out) { *eoc_out = eoc; }
    if (!eoc.empty()) { ExileEndOfCombatTokens(s, eoc); }
    return before - s.players[1].life;
}

Decklist SpiritsDeck()
{
    SpDef("Plains");
    return DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Spirits/Spirits.cod"));
}

Decklist Without(const Decklist& full, const std::vector<std::string>& cut)
{
    Decklist d = full;
    d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(), [&](const Card& c)
                      { return std::find(cut.begin(), cut.end(), c.m_name.str()) != cut.end(); }),
                      d.mainboard.end());
    return d;
}
}   // namespace

// ---- routing ------------------------------------------------------------------------------------

TEST_CASE("Spirits routing: the list resolves to SpiritsProvider, not anti (Flooded Strand) or Vial")
{
    const Decklist d = SpiritsDeck();
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "Spirits");
}

TEST_CASE("Spirits routing: the signature survives cutting any ONE of its cards, and all three lords at once")
{
    const Decklist full = SpiritsDeck();
    for (const std::vector<std::string>& cut :
         std::vector<std::vector<std::string>>{{"Drogskol Captain"}, {"Supreme Phantom"},
                                               {"Geist of Saint Traft"}, {"Patrician Geist"},
                                               {"Mausoleum Wanderer"}, {"Millicent, Restless Revenant"},
                                               {"Drogskol Captain", "Supreme Phantom", "Patrician Geist"}})
    {
        CHECK_MESSAGE(std::string(DetectDecisionProvider(Without(full, cut)).Name()) == "Spirits",
                      "cutting ", cut.front());
    }
    // ...but a list with NONE of the signature cards is not Spirits (it would ride the generic chain:
    // Flooded Strand's fetch_land_types -> anti). The signature is what routes, not the deck name.
    const Decklist none = Without(full, {"Drogskol Captain", "Supreme Phantom", "Geist of Saint Traft",
                                         "Patrician Geist", "Mausoleum Wanderer",
                                         "Millicent, Restless Revenant"});
    CHECK(std::string(DetectDecisionProvider(none).Name()) != "Spirits");
}

// ---- the lossless fetch surface -----------------------------------------------------------------

TEST_CASE("SpiritsProvider: Flooded Strand offers every distinct legal target, hold kept")
{
    GameState s = SpFresh();
    // Library: two Plains, an Island, Hallowed Fountain, Godless Shrine, Watery Grave, a creature.
    int n = 100;
    for (const char* name : {"Plains", "Plains", "Island", "Hallowed Fountain", "Godless Shrine",
                             "Watery Grave", "Supreme Phantom"})
    { s.players[0].library.push_back(SpCard(name, n++)); }
    const SpiritsProvider prov;
    const CardDefinition& strand = SpDef("Flooded Strand");
    std::vector<std::string> c = prov.FetchCandidates(s, 0, strand.params);
    std::sort(c.begin(), c.end());
    CHECK(c == std::vector<std::string>{"Godless Shrine", "Hallowed Fountain", "Island", "Plains",
                                        "Watery Grave"});
    // The searched set is the USER's fetch rule (test_spirits_fetch_rule.cpp): with no land out, Hallowed
    // Fountain plus the best black maker -- never the DecisionProvider top-1 default.
    CHECK(prov.FetchSearchCandidates(s, 0, strand.params, prov.FetchCandidates(s, 0, strand.params))
          == std::vector<std::string>{"Hallowed Fountain", "Godless Shrine"});
    // The battlefield hold stays offered (root default), so the Strand is never collapsed on play.
    CHECK(prov.ConsiderFetchHold(s, strand));
    s.m_provider = &prov;
    CHECK_FALSE(FetchResolvesOnPlay(s, strand));
    // ...and BESIDE the hold, the crack-on-play variants are offered (a pinned target collapses the
    // fetch at the land drop so its land pays for this plan's casts) -- never at 3 life or less.
    CHECK(FetchCrackOnPlayOffered(s, strand));
    CHECK(FetchCrackOnPlayPinned(s, strand, "Island"));
    CHECK_FALSE(FetchCrackOnPlayPinned(s, strand, ""));   // the hold variant carries no target
    s.players[0].life = 3;
    CHECK_FALSE(FetchCrackOnPlayOffered(s, strand));
    CHECK_FALSE(FetchCrackOnPlayPinned(s, strand, "Island"));
}

// ---- the lords ----------------------------------------------------------------------------------

TEST_CASE("Spirit lords: Captain and Phantom pump OTHER Spirits (each other too), not themselves or the Angel")
{
    GameState s = SpFresh();
    const int cap   = SpPut(s, "Drogskol Captain", 1);
    const int ph    = SpPut(s, "Supreme Phantom", 2);
    const int shk   = SpPut(s, "Shacklegeist", 3);
    const int lab   = SpPut(s, "Spirit of the Labyrinth", 4);
    const int geist = SpPut(s, "Geist of Saint Traft", 5);
    auto bonus = [&](int i)
    {
        const Permanent& p = s.battlefield[static_cast<std::size_t>(i)];
        return ComputeLordBonus(p.card, s, p.controller_index, p.AnimatedAllTypes(), &p);
    };
    CHECK(bonus(cap)   == std::pair<int, int>{1, 1});   // Phantom only
    CHECK(bonus(ph)    == std::pair<int, int>{1, 1});   // Captain only
    CHECK(bonus(shk)   == std::pair<int, int>{2, 2});
    CHECK(bonus(lab)   == std::pair<int, int>{2, 2});
    CHECK(bonus(geist) == std::pair<int, int>{2, 2});
    // An opponent's Spirit is not "you control".
    const int opp = SpPut(s, "Shacklegeist", 6, /*controller=*/1);
    CHECK(bonus(opp) == std::pair<int, int>{0, 0});
}

// ---- Geist of Saint Traft -----------------------------------------------------------------------

TEST_CASE("Geist of Saint Traft: an attacking Geist makes a tapped, attacking 4/4 white flying Angel that deals 4")
{
    GameState s = SpFresh();
    const int geist = SpPut(s, "Geist of Saint Traft", 1);
    std::vector<int> atk{geist};
    std::vector<int> eoc;
    const int tok_start = FireAttackCreateTokens(s, 0, &atk, &eoc);
    REQUIRE(static_cast<int>(s.battlefield.size()) == tok_start + 1);
    const Permanent& angel = s.battlefield.back();
    CHECK(angel.is_token);
    CHECK(angel.tapped);
    CHECK(CardHasSubtype(angel.card, "Angel"));
    CHECK_FALSE(CardHasSubtype(angel.card, "Spirit"));
    CHECK(angel.card.m_power == 4);
    CHECK(angel.card.m_toughness == 4);
    CHECK(angel.card.HasKeyword(Keyword::Flying));
    CHECK(angel.card.HasColor(Color::White));
    REQUIRE(eoc.size() == 1);
    CHECK(eoc.front() == angel.card.m_number);
}

TEST_CASE("Geist of Saint Traft: 2 + 4 damage, and the Angel is GONE after combat (not at the end step)")
{
    GameState s = SpFresh();
    const int geist = SpPut(s, "Geist of Saint Traft", 1);
    std::vector<int> eoc;
    CHECK(SwingAndEndCombat(s, {geist}, &eoc) == 6);
    CHECK(eoc.size() == 1);
    CHECK(Angels(s) == 0);
    CHECK(s.battlefield.size() == 1);                       // Geist alone
    CHECK(s.players[0].graveyard.empty());                  // exiled, not died
    CHECK(s.exile.empty());                                 // a token in exile ceases to exist
}

TEST_CASE("Geist of Saint Traft: the Angel is not lorded (not a Spirit); Geist is")
{
    GameState s = SpFresh();
    SpPut(s, "Supreme Phantom", 2);
    SpPut(s, "Drogskol Captain", 3);
    const int geist = SpPut(s, "Geist of Saint Traft", 1);
    // Geist 2 + 2 (two lords) = 4, Angel 4 + 0 = 4.
    CHECK(SwingAndEndCombat(s, {geist}) == 8);
    CHECK(Angels(s) == 0);
}

TEST_CASE("Geist of Saint Traft: a Geist that does not attack (summoning sick / held back) makes no Angel")
{
    GameState s = SpFresh();
    SpPut(s, "Geist of Saint Traft", 1, 0, /*sick=*/true);
    const int phantom = SpPut(s, "Supreme Phantom", 2);
    std::vector<int> eoc;
    CHECK(SwingAndEndCombat(s, {phantom}, &eoc) == 1);
    CHECK(eoc.empty());
    CHECK(Angels(s) == 0);
}

TEST_CASE("Geist of Saint Traft: a flat count of ONE in 2HG (no 'for each opponent')")
{
    GameState s = SpFresh();
    const int saved = gamesetup::t_setup.opponent_heads;
    gamesetup::t_setup.opponent_heads = 2;
    const int geist = SpPut(s, "Geist of Saint Traft", 1);
    std::vector<int> atk{geist};
    std::vector<int> eoc;
    FireAttackCreateTokens(s, 0, &atk, &eoc);
    gamesetup::t_setup.opponent_heads = saved;
    CHECK(Angels(s) == 1);
    CHECK(eoc.size() == 1);
}

TEST_CASE("Hero of Bladehold's tokens are NOT recorded for the end-of-combat exile (they persist)")
{
    GameState s = SpFresh();
    const int hero = SpPut(s, "Hero of Bladehold", 1);
    std::vector<int> eoc;
    SwingAndEndCombat(s, {hero}, &eoc);
    CHECK(eoc.empty());
    int soldiers = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && CardHasSubtype(p.card, "Soldier")) { ++soldiers; } }
    CHECK(soldiers == 2);
}

// (Spirit of the Labyrinth's draw cap: test_spirit_labyrinth.cpp.)
