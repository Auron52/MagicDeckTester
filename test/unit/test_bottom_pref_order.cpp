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
#include <algorithm>

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

const char* kAaDeck  = "decks/Accomplished Alchemist/Accomplished Alchemist.cod";
const char* kAaProf  = "decks/Accomplished Alchemist/Accomplished Alchemist.profile.json";
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

TEST_CASE("Merged mulligan buckets bottom the weaker physical card")
{
    EnsureCardsBpo();
    const Decklist deck = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Worldfire/Worldfire.cod"));
    const MulliganProfile base = LoadDeckProfile(
        ResolveHeuristicDefaultsPath("decks/Worldfire/Worldfire.profile.json"));
    const std::vector<std::string> tutor_order{ "Mystical Tutor", "Personal Tutor" };
    REQUIRE(base.bucket_bottom_keep_order.size() == 1);
    CHECK(base.bucket_bottom_keep_order[0] == tutor_order);

    auto bottom_one = [&](const std::vector<std::string>& names,
                          const std::vector<int>& target, bool reverse_tutor_scores)
    {
        GameState s = GoldFishRunner::SetupGame(deck, 71123);
        auto& ap = s.ActivePlayer();
        ap.hand.clear();
        for (const std::string& name : names)
        {
            auto it = std::find_if(ap.library.begin(), ap.library.end(),
                                   [&](const Card& c) { return c.m_name == name; });
            REQUIRE(it != ap.library.end());
            ap.hand.push_back(*it);
            ap.library.erase(it);
        }
        ExhaustiveKeepPolicy ek;
        ek.buckets = { {"Mystical Tutor", "Personal Tutor"}, {"Ponder", "Preordain"},
                       {"Island"}, {"Mountain"}, {"Worldfire"} };
        ek.max_mull = 1;
        ek.bottoming_enabled = true;
        ek.Index();
        std::vector<int> comp;
        REQUIRE(ek.Composition(names, comp));
        ek.keep[comp] = { 1, 1, 1, 1 };
        ek.bottom_keep[comp] = { {}, {}, target, target };
        MulliganProfile prof = base;
        if (reverse_tutor_scores)
        {
            // The explicit rule must win even when a later score update reverses the ranking.
            prof.card_scores["Mystical Tutor"] = { 0.0 };
            prof.card_scores["Personal Tutor"] = { 1.0 };
        }
        prof.exhaustive_keep = std::make_shared<const ExhaustiveKeepPolicy>(std::move(ek));
        AIEngine eng(prof, 0, 1);
        eng.RolloutKeepWinTurn(s, 1, 1);
        REQUIRE(eng.LastBottomedNumbers().size() == 1);
        for (const Card& c : ap.hand)
            if (c.m_number == eng.LastBottomedNumbers()[0]) { return c.m_name.str(); }
        return std::string();
    };

    CHECK(bottom_one({"Mystical Tutor", "Personal Tutor", "Ponder", "Preordain",
                      "Island", "Mountain", "Worldfire"}, { 1, 2, 1, 1, 1 }, true)
          == "Personal Tutor");
    CHECK(bottom_one({"Mystical Tutor", "Personal Tutor", "Personal Tutor", "Ponder",
                      "Island", "Mountain", "Worldfire"}, { 2, 1, 1, 1, 1 }, true)
          == "Personal Tutor");
    CHECK(bottom_one({"Ponder", "Ponder", "Preordain", "Mystical Tutor",
                      "Island", "Mountain", "Worldfire"}, { 1, 2, 1, 1, 1 }, false)
          == "Ponder");
}
