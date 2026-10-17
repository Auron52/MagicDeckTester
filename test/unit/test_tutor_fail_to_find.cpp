// A human "-1" at a REQUIRED library search is a fail-to-find (CR 701.19b): nothing is fetched, but
// the printed "then shuffle" still happens. Only a "you MAY search" card (tutor_optional) skips the
// search -- and with it the shuffle. Found by the 2026-10-17 Colossus Hammer claude-play recheck
// (seed 2100011): declining Steelshaper's Gift / Open the Armory kept the known library top, and the
// next draw was the very card the search had just looked past.

#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void LoadCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    const char* lib[] = { "Plains", "Colossus Hammer", "Mountain", "Lizard Blades", "Plains", "Mountain" };
    int n = 60;
    for (const char* name : lib)
    {
        Card c = D(name).card;
        c.m_number = n++;
        s.players[0].library.push_back(c);
    }
    return s;
}

std::vector<int> LibNumbers(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].library) { v.push_back(c.m_number); }
    return v;
}

// Installs a tutor chooser that always answers `answer` for the scope's lifetime.
struct ChooserScope
{
    TutorChooser  fn;
    TutorChooser* saved;
    int           calls = 0;
    explicit ChooserScope(int answer)
        : saved(g_play_tutor_chooser)
    {
        fn = [this, answer](const GameState&, int, const std::string&, const std::vector<std::string>&, int,
                            const std::vector<int>&) -> int { ++calls; return answer; };
        g_play_tutor_chooser = &fn;
    }
    ~ChooserScope() { g_play_tutor_chooser = saved; }
};

}   // namespace

TEST_CASE("Tutor fail-to-find: a required search the human declines still shuffles (CR 701.19b)")
{
    LoadCards();
    for (const char* tutor : { "Steelshaper's Gift", "Open the Armory" })
    {
        CAPTURE(tutor);
        const CardParams& pp = D(tutor).params;
        REQUIRE_FALSE(pp.tutor_optional);

        // Empty-target route (the chooser decides the pick).
        {
            GameState s = Board();
            const std::vector<int> before = LibNumbers(s);
            const int sc0 = s.search_count;
            ChooserScope cs(-1);
            PerformTutor(s, 0, pp, std::string{}, tutor, /*human_repick=*/false);
            CHECK(cs.calls == 1);
            CHECK(s.players[0].hand.empty());                    // nothing fetched
            CHECK(s.players[0].library.size() == before.size());
            CHECK(s.search_count == sc0 + 1);                    // ...but the library was shuffled
        }
        // Cast route (a plan-baked target, re-asked at resolution).
        {
            GameState s = Board();
            const int sc0 = s.search_count;
            ChooserScope cs(-1);
            PerformTutor(s, 0, pp, "Colossus Hammer", tutor, /*human_repick=*/true);
            CHECK(cs.calls == 1);
            CHECK(s.players[0].hand.empty());
            CHECK(s.search_count == sc0 + 1);
        }
        // Finding a card shuffles exactly once too (unchanged).
        {
            GameState s = Board();
            const int sc0 = s.search_count;
            ChooserScope cs(0);
            PerformTutor(s, 0, pp, std::string{}, tutor, /*human_repick=*/false);
            CHECK(s.players[0].hand.size() == 1);
            CHECK(s.search_count == sc0 + 1);
        }
    }
}

TEST_CASE("Tutor fail-to-find: a 'you MAY search' decline skips the search and the shuffle")
{
    LoadCards();
    const CardParams& pp = D("Giant Harbinger").params;
    REQUIRE(pp.tutor_optional);
    GameState s = Board();
    Card giant = D("Hamletback Goliath").card;   // a Giant, so the search has a candidate and the chooser fires
    giant.m_number = 90;
    s.players[0].library.push_back(giant);
    const std::vector<int> before = LibNumbers(s);
    const int sc0 = s.search_count;
    ChooserScope cs(-1);
    PerformTutor(s, 0, pp, std::string{}, "Giant Harbinger", /*human_repick=*/false);
    CHECK(cs.calls == 1);
    CHECK(s.players[0].hand.empty());
    CHECK(s.search_count == sc0);            // no search happened
    CHECK(LibNumbers(s) == before);          // library order untouched
}
