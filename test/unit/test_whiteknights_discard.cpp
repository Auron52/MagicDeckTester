// Unit tests for the USER-settled WhiteKnights cleanup-discard rule (2026-10-09):
// WhiteKnightsProvider::CleanupDiscardCandidates, implementing docs/design/whiteknights-discard-policy.md
// -- "Knights curve rule + the extras": Swords / Unexpectedly Absent shed first, Sol Ring and Remote
// Farm count as mana (2 each), duplicate legends (2nd Adeline / Basri, in play or in hand) shed early,
// Vial surplus after turn 1. MTG_WK_BUCKET_DISCARD=0 -> the generic ranking, independent of the Knights
// flag. Expected orders are derived BY HAND from the design text; every case also checks the GENERIC
// fallback's index 0 (the control arm that must differ).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsWk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeWk(const std::vector<std::string>& hand, const std::vector<std::string>& board, int turn = 4)
{
    EnsureCardsWk();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.vial_target_mv      = 2;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> Wk(const GameState& s, int wk = 1, int knights = -1)
{
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD]      = static_cast<std::int8_t>(wk);
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = static_cast<std::int8_t>(knights);
    std::vector<int> v = WhiteKnightsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD]      = -1;
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> KnightsRule(const GameState& s)
{
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = 1;
    std::vector<int> v = KnightsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kAdeline  = "Adeline, Resplendent Cathar";
const char* kBasri    = "Basri, Tomorrow's Champion";
const char* kVen      = "Venerable Knight";
const char* kWorthy   = "Worthy Knight";
const char* kExemplar = "Knight Exemplar";
const char* kBenalish = "Benalish Marshal";
const char* kAccorder = "Accorder Paladin";
const char* kSilver   = "Silverblade Paladin";
const char* kHero     = "Hero of Bladehold";
const char* kSwords   = "Swords to Plowshares";
const char* kUA       = "Unexpectedly Absent";
const char* kFarm     = "Remote Farm";
const char* kSolRing  = "Sol Ring";

}   // namespace

TEST_CASE("WhiteKnights discard: Swords and Unexpectedly Absent are shed first")
{
    // The removal is inert against the goldfish -> first, in hand order (1, 4). The Knights rule alone
    // has no such class (it would shed the surplus Plains first), so this IS the extra.
    const GameState s = MakeWk({ kAdeline, kSwords, kVen, kWorthy, kUA, "Plains", kBenalish, kAccorder },
                               { "Plains", "Plains", "Plains" });
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 1);
    CHECK(o[1] == 4);
    CHECK(o[2] == 5);   // then the surplus Plains (the land target 3 is met by the board)
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);          // generic sheds Adeline (MV 3, earliest)
    CHECK(KnightsRule(s).front() == 5);  // the Knights rule without the extras
    // Independent of the Knights flag, in both directions.
    CHECK(Wk(s, 1, 0) == o);
    CHECK(Wk(s, 0, 1) == Gen(s));
}

TEST_CASE("WhiteKnights discard: a 2nd Adeline in hand and a Basri with a copy in play are dead")
{
    const GameState s = MakeWk({ kAdeline, kBasri, kAdeline, kVen, kWorthy, kBenalish, "Plains", kAccorder },
                               { "Plains", "Plains", "Plains", kBasri });
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 1);   // Basri: its namesake is on our battlefield
    CHECK(o[1] == 2);   // the second hand Adeline
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("WhiteKnights discard: Remote Farm counts as 2 mana -- one Farm fills the 2 the hand owes")
{
    // Board 1 Plains; target 3 -> the hand owes 2 mana. The Farm makes {W}{W}, so it alone fills the
    // quota (the bigger source is kept: fewer cards for the same mana) and BOTH hand Plains are
    // surplus, worst first (2, 1). The Farm is the last card.
    const GameState s = MakeWk({ kFarm, "Plains", "Plains", kVen, kWorthy, kBenalish, kExemplar, kAccorder },
                               { "Plains" }, 2);
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 2);
    CHECK(o[1] == 1);
    CHECK(o[7] == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 5);
}

TEST_CASE("WhiteKnights discard: Sol Ring counts as mana -- it fills the slot and the Plains is surplus")
{
    // Board 2 Plains; target 3 -> the hand owes 1. Sol Ring (2 mana, no land drop) fills it, so the
    // hand Plains (1) is surplus -> index 0, and the Ring is kept to the end. The Knights rule (no
    // extras) does not count a rock as mana: it keeps the Plains and sheds the Ring as unclassified.
    const GameState s = MakeWk({ kSolRing, "Plains", kVen, kWorthy, kBenalish, kExemplar, kAccorder, kSilver },
                               { "Plains", "Plains" }, 3);
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 1);
    CHECK(o[7] == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 4);
    CHECK(KnightsRule(s).front() == 0);
}

TEST_CASE("WhiteKnights discard: a held 4-drop (Hero of Bladehold) keeps a 4th land")
{
    // Board 3 Plains. Hero (MV 4) held -> target 4 (agent reading of "4 while a Haytham is held": the
    // 4-drop is why) -> the hand owes 1: the first Plains is kept (last card), the second (2) surplus.
    const GameState s = MakeWk({ kHero, "Plains", "Plains", kVen, kWorthy, kBenalish, kExemplar, kAccorder },
                               { "Plains", "Plains", "Plains" });
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 2);
    CHECK(o[7] == 1);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);

    // Without the 4-drop both Plains are surplus.
    const GameState t = MakeWk({ kVen, "Plains", "Plains", kVen, kWorthy, kBenalish, kExemplar, kAccorder },
                               { "Plains", "Plains", "Plains" });
    const std::vector<int> u = Wk(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 2);
    CHECK(u[1] == 1);
}

TEST_CASE("WhiteKnights discard: the Knights curve fill -- the 3-drops beyond the curve go first")
{
    // Board 3 Plains, no hand land: 3 W a turn. The removal leads (5, 6). Most creatures castable in
    // the window: Venerable + Accorder (1+2) on one turn and a 3-drop on each of the other two = 4
    // (a fifth would cost 12 > 9). Ties -> more lords: the two 3-drop slots go to Benalish and
    // Exemplar, so Silverblade (4) and Adeline (7) are outside the fill -> shed lowest value first:
    // Silverblade (soulbond, agent value) below Adeline (USER: the top body).
    const GameState s = MakeWk({ kBenalish, kVen, kExemplar, kAccorder, kSilver, kSwords, kUA, kAdeline },
                               { "Plains", "Plains", "Plains" });
    const std::vector<int> o = Wk(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 5);
    CHECK(o[1] == 6);
    CHECK(o[2] == 4);
    CHECK(o[3] == 7);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("WhiteKnights discard: MTG_WK_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeWk({ kAdeline, kSwords, kVen, kWorthy, kUA, "Plains", kBenalish, kAccorder },
                               { "Plains", "Plains", "Plains" });
    CHECK(Wk(s, 0) == Gen(s));
    CHECK(Wk(s, 1) != Gen(s));
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD] = 0;
    CHECK(WhiteKnightsProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD] = 1;
    CHECK_FALSE(WhiteKnightsProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD] = -1;
}
