// Spirits: the USER's cleanup-discard buckets (2026-11-14, SpiritsProvider::CleanupDiscardCandidates / SpiritsShed)
// and the USER's dredge nuance under Spirit of the Labyrinth's draw cap (2026-11-14).
//
// USER (discard): "We should hold at least 3-4 threats of varying costs and at most 3 lands and an Aether Vial for
// deployment. Most of the time we are probably okay with 3 lands total, though 4 or 5 are okay to hit if we can
// keep 4 threats in hand. I agree that Lingering Souls should be dropped early on the threat side."
// USER (dredge): "if you dredge all previous cards subsequent draws are not blocked yet because dredging does not
// count as a draw. That means you can continue dredging." / "you have to actually draw at some point for the block
// to kick in."
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HandEntry.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& SdDef(const std::string& name)
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

using Names = std::vector<std::string>;

GameState SdState(const Names& board, const Names& hand, const Names& library = {})
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    int n = 100;
    for (const std::string& nm : board)
    {
        Permanent p;
        p.card             = SdDef(nm).card;
        p.card.m_number    = n++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& nm : hand)
    {
        Card c     = SdDef(nm).card;
        c.m_number = n++;
        s.players[0].hand.push_back(c);
    }
    for (const std::string& nm : library)
    {
        Card c     = SdDef(nm).card;
        c.m_number = n++;
        s.players[0].library.push_back(c);
    }
    return s;
}

// The first card the rule sheds.
std::string FirstShed(const GameState& s)
{
    static const SpiritsProvider prov;
    const std::vector<int> r = prov.CleanupDiscardCandidates(s, nullptr);
    REQUIRE(!r.empty());
    return s.players[0].hand[static_cast<std::size_t>(r.front())].m_name.str();
}
}   // namespace

TEST_CASE("Spirits discard: Lingering Souls with black access is the first threat shed (it stays a threat)")
{
    // Board 3 lands incl. Godless Shrine (black); hand 4 threats + Souls + 2 lands + a Vial = 8 cards. Lands total
    // 5 with 4 threats kept is allowed, the Vial is the one deployment Vial, so Souls goes first.
    GameState s = SdState({ "Plains", "Hallowed Fountain", "Godless Shrine" },
                          { "Supreme Phantom", "Lingering Souls", "Drogskol Captain", "Island",
                            "Millicent, Restless Revenant", "Geist of Saint Traft", "Plains", "Aether Vial" });
    CHECK(FirstShed(s) == "Lingering Souls");
}

TEST_CASE("Spirits discard: past 5 lands in total the worst land goes first")
{
    // No black anywhere: Souls is an ordinary threat. Board 3 + hand 3 = 6 lands; the kept order fills W3 (Plains),
    // U2 (Island), so the surplus is the second Plains.
    GameState s = SdState({ "Plains", "Island", "Plains" },
                          { "Lingering Souls", "Plains", "Supreme Phantom", "Island", "Drogskol Captain",
                            "Millicent, Restless Revenant", "Plains", "Aether Vial" });
    CHECK(FirstShed(s) == "Plains");
}

TEST_CASE("Spirits discard: without 4 threats the lands past 3 in total are surplus")
{
    // Board 2 lands; hand 4 lands + Vial + 3 threats. Only 3 threats -> lands are capped at 3 in total, so a land is
    // shed before the Vial or any threat.
    GameState s = SdState({ "Plains", "Island" },
                          { "Supreme Phantom", "Plains", "Island", "Drogskol Captain", "Hallowed Fountain",
                            "Aether Vial", "Millicent, Restless Revenant", "Azorius Chancery" });
    const std::string first = FirstShed(s);
    CHECK(SdDef(first).card.IsLand());
    CHECK(first != "Hallowed Fountain");   // the dual is the land kept
}

TEST_CASE("Spirits discard: a Vial already on the battlefield makes the one in hand surplus")
{
    GameState s = SdState({ "Plains", "Island", "Hallowed Fountain", "Aether Vial" },
                          { "Supreme Phantom", "Drogskol Captain", "Patrician Geist", "Millicent, Restless Revenant",
                            "Geist of Saint Traft", "Aether Vial", "Plains", "Mausoleum Wanderer" });
    CHECK(FirstShed(s) == "Aether Vial");
}

TEST_CASE("Spirit of the Labyrinth + dredge: dredging is not drawing -- dredge, dredge, draw, then the cap")
{
    GameState s = SdState({ "Spirit of the Labyrinth" }, {});
    s.deck_has_draw_cap = true;
    s.deck_has_dredge   = true;
    Player& p = s.players[0];
    int n = 500;
    for (const char* nm : { "Stinkweed Imp", "Stinkweed Imp" })
    {
        Card c     = SdDef(nm).card;
        c.m_number = n++;
        p.graveyard.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c     = SdDef("Plains").card;
        c.m_number = n++;
        p.library.push_back(c);
    }
    struct Picks final : DredgeChooser
    {
        std::vector<int> picks;
        int Choose(const GameState&, int, const std::vector<int>&) override
        {
            const int pick = picks.empty() ? 0 : picks.front();
            if (!picks.empty()) { picks.erase(picks.begin()); }
            return pick;
        }
    } picks;
    picks.picks = { 1, 1, 0 };   // dredge, dredge, then the ordinary draw
    DredgeChooser* saved = g_dredge_chooser;
    g_dredge_chooser = &picks;
    // The per-draw idiom of the single-draw sites (watchers, ETB draws, modal draws): count ACTUAL draws only.
    int drawn = 0;
    for (int k = 0; k < 4; ++k) { const int d = DrawIntoHand(s, 0, 1, HandEntryReason::Draw); drawn += d; p.cards_drawn_this_turn += d; }
    g_dredge_chooser = saved;
    CHECK(drawn == 1);                       // the third attempt drew; the fourth was ignored by the cap
    CHECK(p.cards_drawn_this_turn == 1);
    CHECK(p.hand.size() == 3);               // two Imps back + one Plains
    CHECK(p.library.size() == 20 - 10 - 1);  // 2 x mill 5, 1 draw, nothing for the ignored draw
}
