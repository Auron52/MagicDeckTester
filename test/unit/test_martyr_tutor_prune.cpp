// Unit tests for the Martyr Lifegain 1-drop tutor prune: MartyrLifegainProvider::TutorCandidates and
// TutorHandPutList (MTG_MARTYR_TUTOR_PRUNE, USER 2026-11-11: "add a heuristic prune for the 1-drop search to get
// only Martyr (if we don't have one and need the lifegain), Serra Ascendant or Speaker of the Heavens. Usually it
// will be Serra Ascendant I would imagine, so that should rank first.").
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{
using Names = std::vector<std::string>;

void EnsureCardsMt()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhMt(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeMt(const Names& library, const Names& board, const Names& hand, int life)
{
    EnsureCardsMt();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = life;
    int num = 1;
    for (const std::string& n : library) { s.players[0].library.push_back(PhMt(n, num++)); }
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhMt(n, num++)); }
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
    return s;
}

const CardParams& ParamsOf(const char* name)
{
    EnsureCardsMt();
    return CardDatabase::Instance().Lookup(name)->params;
}

// arm: -1 = env default (ON), 0 / 1 pinned.
Names Captain(const GameState& s, int arm = -1)
{
    heurarm::t_arm[heurarm::MARTYR_TUTOR_PRUNE] = static_cast<std::int8_t>(arm);
    Names v = MartyrLifegainProvider().TutorCandidates(s, 0, ParamsOf("Ranger-Captain of Eos"));
    heurarm::t_arm[heurarm::MARTYR_TUTOR_PRUNE] = -1;
    return v;
}

Names Pair(const GameState& s, int arm = -1)
{
    heurarm::t_arm[heurarm::MARTYR_TUTOR_PRUNE] = static_cast<std::int8_t>(arm);
    Names v = MartyrLifegainProvider().TutorHandPutList(s, 0, ParamsOf("Ranger of Eos"), 2);
    heurarm::t_arm[heurarm::MARTYR_TUTOR_PRUNE] = -1;
    return v;
}

const Names kLib = {"Mother of Runes", "Speaker of the Heavens", "Thraben Inspector", "Martyr of Sands",
                    "Kami of False Hope", "Serra Ascendant", "Plains", "Serra Ascendant", "Speaker of the Heavens"};
}   // namespace

TEST_CASE("martyr tutor prune: Serra, Martyr (needed), Speaker -- ranked, the rest pruned")
{
    const GameState s = MakeMt(kLib, {"Plains"}, {}, 20);
    CHECK(Captain(s) == Names{"Serra Ascendant", "Martyr of Sands", "Speaker of the Heavens"});
}

TEST_CASE("martyr tutor prune: a Martyr in hand or on the battlefield drops Martyr")
{
    CHECK(Captain(MakeMt(kLib, {}, {"Martyr of Sands"}, 20)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
    CHECK(Captain(MakeMt(kLib, {"Martyr of Sands"}, {}, 20)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
}

TEST_CASE("martyr tutor prune: at or above the top threshold (30 in Standard) Martyr is not needed")
{
    gamesetup::t_setup.starting_life = 20;
    CHECK(Captain(MakeMt(kLib, {}, {}, 29)) == Names{"Serra Ascendant", "Martyr of Sands", "Speaker of the Heavens"});
    CHECK(Captain(MakeMt(kLib, {}, {}, 30)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
    // 2HG: Speaker's start + 7 = 37 is the top threshold.
    gamesetup::t_setup.starting_life = 30;
    CHECK(Captain(MakeMt(kLib, {}, {}, 36)) == Names{"Serra Ascendant", "Martyr of Sands", "Speaker of the Heavens"});
    CHECK(Captain(MakeMt(kLib, {}, {}, 37)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
    gamesetup::t_setup.starting_life = -1;
}

TEST_CASE("martyr tutor prune: nothing admissible falls back to the generic list; =0 is the generic list")
{
    const Names rest = {"Mother of Runes", "Thraben Inspector", "Martyr of Sands", "Plains"};
    const GameState s = MakeMt(rest, {}, {"Martyr of Sands"}, 20);   // only a not-needed Martyr is left
    CHECK(Captain(s) == GenericProvider().TutorCandidates(s, 0, ParamsOf("Ranger-Captain of Eos")));
    const GameState full = MakeMt(kLib, {}, {}, 20);
    CHECK(Captain(full, 0) == GenericProvider().TutorCandidates(full, 0, ParamsOf("Ranger-Captain of Eos")));
}

TEST_CASE("martyr tutor prune: Ranger of Eos's pair takes distinct admitted names first, then fills")
{
    CHECK(Pair(MakeMt(kLib, {}, {}, 20)) == Names{"Serra Ascendant", "Martyr of Sands"});
    CHECK(Pair(MakeMt(kLib, {}, {}, 30)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
    // Only Serra copies left among the three: both copies.
    CHECK(Pair(MakeMt({"Mother of Runes", "Serra Ascendant", "Serra Ascendant"}, {}, {}, 30))
          == Names{"Serra Ascendant", "Serra Ascendant"});
    // One admitted card: fill the second slot in library order.
    CHECK(Pair(MakeMt({"Mother of Runes", "Serra Ascendant", "Thraben Inspector"}, {}, {}, 30))
          == Names{"Serra Ascendant", "Mother of Runes"});
    // =0: the default library-order pair.
    CHECK(Pair(MakeMt(kLib, {}, {}, 20), 0) == Names{"Mother of Runes", "Speaker of the Heavens"});
}

TEST_CASE("martyr tutor prune: MTG_MARTYR_TUTOR_FARM admits Curious Farm Animals while the gain is needed")
{
    const Names lib = {"Curious Farm Animals", "Mother of Runes", "Serra Ascendant", "Speaker of the Heavens"};
    CHECK(Captain(MakeMt(lib, {}, {}, 20)) == Names{"Serra Ascendant", "Speaker of the Heavens"});   // default OFF
    heurarm::t_arm[heurarm::MARTYR_TUTOR_FARM] = 1;
    CHECK(Captain(MakeMt(lib, {}, {}, 20)) == Names{"Serra Ascendant", "Curious Farm Animals", "Speaker of the Heavens"});
    CHECK(Captain(MakeMt(lib, {}, {}, 30)) == Names{"Serra Ascendant", "Speaker of the Heavens"});
    heurarm::t_arm[heurarm::MARTYR_TUTOR_FARM] = -1;
}
