// MTG_BOTTOM_PREF_ORDER (AIEngine::BottomCards, 2026-10-20): the clairvoyant bottom trials run in
// HeuristicBottomPick's preference order, and every trial after the first stops once it can no
// longer win STRICTLY before the best so far. The claim is that the BOTTOMED CARDS are unchanged.
//
//   1. BottomPreferenceOrder: the peeled order is a priority choice -- HeuristicBottomPick on ANY
//      allowed set returns that set's first card in the order (checked here independently of the
//      function's own verification, on real opening hands).
//   2. End to end: RolloutKeepWinTurn (bottom N, then play out) bottoms the SAME physical cards and
//      reports the same win turn with the lever on and off, at one and two mulligans, over a
//      spread of real opening hands -- the subset table (N = 2) and the per-candidate loop (N = 1).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/MulliganProfileIO.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HandEntry.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsBpo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct ForcePrefOrder
{
    explicit ForcePrefOrder(int v) { AIEngine::TestForceBottomPrefOrder(v); }
    ~ForcePrefOrder() { AIEngine::TestForceBottomPrefOrder(-1); }
};

GameState OpeningState(const Decklist& deck, std::uint64_t seed)
{
    GameState s = GoldFishRunner::SetupGame(deck, seed);
    DrawIntoHand(s, s.active_player_index, 7, HandEntryReason::Opening);
    return s;
}

const char* kAaDeck  = "decks/AccomplishedAlchemist/AccomplishedAlchemist.cod";
const char* kAaProf  = "decks/AccomplishedAlchemist/AccomplishedAlchemist.profile.json";
}   // namespace

TEST_CASE("BottomPreferenceOrder: HeuristicBottomPick is the order's first allowed card on every set")
{
    EnsureCardsBpo();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath(kAaDeck));
    AIEngine eng(LoadDeckProfile(ResolveHeuristicDefaultsPath(kAaProf)), 1, 1);
    int verified = 0;
    for (std::uint64_t seed = 70001; seed < 70041; ++seed)
    {
        const GameState s = OpeningState(deck, seed);
        const std::vector<Card>& hand = s.players[0].hand;
        REQUIRE(hand.size() == 7);
        std::vector<int> order;
        if (!eng.BottomPreferenceOrder(hand, order)) { continue; }
        ++verified;
        REQUIRE(order.size() == hand.size());
        for (std::uint32_t set = 1; set < (1u << hand.size()); ++set)
        {
            std::vector<char> allowed(hand.size(), 0);
            for (std::size_t j = 0; j < hand.size(); ++j) { allowed[j] = static_cast<char>((set >> j) & 1u); }
            int want = -1;
            for (int o : order) { if (allowed[static_cast<std::size_t>(o)]) { want = o; break; } }
            CHECK(eng.HeuristicBottomPick(hand, allowed) == want);
        }
    }
    // The deck's hands are all priority choices (the land / spell / fallback branches are each a
    // lexicographic key with an index tie-break) -- if this ever drops, the lever silently stands
    // down to the plain order, which is safe but worth knowing.
    CHECK(verified == 40);
    std::vector<int> o;
    CHECK_FALSE(eng.BottomPreferenceOrder({}, o));
}

TEST_CASE("MTG_BOTTOM_PREF_ORDER: the bottomed cards and the win turn are unchanged (1 and 2 mulligans)")
{
    EnsureCardsBpo();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath(kAaDeck));
    const MulliganProfile prof = LoadDeckProfile(ResolveHeuristicDefaultsPath(kAaProf));
    int compared = 0;
    for (std::uint64_t seed = 70001; seed < 70007; ++seed)
    {
        for (int mull = 1; mull <= 2; ++mull)
        {
            const GameState s = OpeningState(deck, seed);
            std::vector<int> bottomed[2];
            int win[2] = {0, 0};
            for (int on = 0; on < 2; ++on)
            {
                ForcePrefOrder force(on);
                AIEngine eng(prof, 1, 1);
                win[on]      = eng.RolloutKeepWinTurn(s, mull, 8);
                bottomed[on] = eng.LastBottomedNumbers();
            }
            INFO("seed " << seed << " mulligans " << mull);
            CHECK(bottomed[0] == bottomed[1]);
            CHECK(win[0] == win[1]);
            CHECK(static_cast<int>(bottomed[1].size()) == mull);
            ++compared;
        }
    }
    CHECK(compared == 12);
}
