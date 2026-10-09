// Unit tests for the SelesnyaLifegain cleanup-discard BUCKETS:
// SelesnyaLifegainProvider::CleanupDiscardCandidates, implementing the USER-settled design in
// docs/design/selesnyalifegain-discard-policy.md ("SelesnyaLifegain: SETTLED", 2026-10-09):
//   LANDS 3 counting board, TWO W sources while holding a Paragon, dead Sanctuary first;
//   ELVES the primary mana toward the mana target, counting their output, Priest > Archdruid >
//   Llanowar = Mystic > Alchemist; LIFEGAIN 1 (Feed > Wellwisher) while a lifegain payoff is held or in
//   play; PAYOFFS 2 + soft 3rd by castability x enablement, equal ones Bogbeast > Paragon > Craterhoof >
//   Ageless > Avatar, Paragon / Ageless enabled only with a lifegain source, one Paragon kept when
//   reachable soon and enabled, Genesis Wave only with 8+ mana within 2 turns;
//   SHED dead Sanctuary -> surplus lands -> lifegain with no payoff -> payoffs not reachable / not
//   enabled (Wave first) -> surplus Elves -> keep tail.
// Each expected order is derived BY HAND from that text plus the agent readings recorded at the
// implementation (the two-turn mana projection M1 / M2 is worked in each comment). Every board pins the
// GENERIC fallback's index 0 -- the control arm that must differ -- and one case proves the
// MTG_SELESNYA_BUCKET_DISCARD=0 hatch restores the generic ranking exactly.
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

void EnsureCardsSelesnyaDiscard()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Hand cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeSel(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsSelesnyaDiscard();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (int k = 0; k < 20; ++k)      { s.players[0].library.push_back(Ph("Forest", num++)); }
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

std::vector<int> SelArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = arm;
    std::vector<int> v = SelesnyaLifegainProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Sel(const GameState& s) { return SelArm(s, 1); }
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

// No land in play and only a Sanctuary in hand: it would return ITSELF -- dead, first. Everything else
// is out of reach (M1 = M2 = 0): payoffs unreachable (Craterhoof, Paragon, Bogbeast -- the USER's order
// reversed), then the uncastable Elves farthest first, then the kept Feed the Clan (a Paragon is held).
TEST_CASE("Selesnya discard S1: a dead Selesnya Sanctuary is shed first")
{
    const GameState s = MakeSel(
        { "Selesnya Sanctuary", "Llanowar Elves", "Priest of Titania", "Elvish Archdruid",
          "Nykthos Paragon", "Feed the Clan", "Blossoming Bogbeast", "Craterhoof Behemoth" },
        {});
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 7, 4, 6, 3, 2, 1, 5 });
    CHECK(Gen(s).front() == 7);   // generic max-MV sheds Craterhoof
    CheckPermutation(s, o);
}

// Board 2 Forests -> 1 more land for LANDS 3, but a Paragon is held and the board has no W source, so
// TWO W lands are kept (Brushland, Branchloft's W face) and both Forests are surplus.
// M1 = 2 + 1 = 3; Priest + Llanowar deploy within 3; M2 = 2 + 2 + Priest(2 Elves) + 1 = 7.
// Paragon (6, WW) is distance 2 with w2 = 2; Bogbeast (5) distance 2; equal -> Bogbeast > Paragon.
// Target = 6 -> both Elves kept. Ladder: Brushland, Priest, Bogbeast, Branchloft, Llanowar, Paragon,
// Feed (LIFEGAIN 1, a Paragon held).
TEST_CASE("Selesnya discard S2: two W sources while holding a Paragon; surplus Forests go first")
{
    const GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 5, 4, 7, 3, 8, 6, 2 });
    CHECK(Gen(s).front() == 4);   // generic sheds the Paragon
}

TEST_CASE("Selesnya discard S2b: MTG_SELESNYA_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    const std::vector<int> off = SelArm(s, 0);
    const std::vector<int> on  = SelArm(s, 1);
    CHECK(off == Gen(s));
    CHECK(off.front() == 4);
    CHECK(on.front() == 1);
    CHECK(on != off);   // the control that MUST differ
    // ...and the one-consultation cleanup is restored with it (the generic ranking is prefix-stable).
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = 0;
    CHECK(SelesnyaLifegainProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = 1;
    CHECK_FALSE(SelesnyaLifegainProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = -1;
}

// No Paragon / Ageless held -> Feed the Clan and Wellwisher are "lifegain with no payoff" (Wellwisher
// first: Feed > Wellwisher). Board 3 lands -> the hand Forest is surplus. M1 = 3, M2 = 3 + 1 + 1 = 5:
// Wave (8) is below 8 -> shed first among payoffs, Avatar (7) unreachable next; Bogbeast (5) kept.
TEST_CASE("Selesnya discard S3: lifegain with no payoff, then Genesis Wave below 8 mana first")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Feed the Clan", "Blossoming Bogbeast", "Llanowar Elves", "Elvish Mystic",
          "Forest", "Wellwisher", "Verdant Sun's Avatar" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 6, 1, 0, 7, 4, 2, 3 });
    CHECK(Gen(s).front() == 7);   // generic sheds the Avatar (MV 7)
}

// A Paragon is held but only ONE W source exists (Brushland), so Paragon is not reachable; Wave below 8
// is the FIRST shed, then Craterhoof and the Paragon (unreachable, the USER's order reversed). Feed the
// Clan is the ONLY lifegain source and enables the kept Ageless, so it is laddered right after it.
// M1 = 3 + 1 = 4, M2 = 3 + 1 + 1 + 1 = 6; target 5 -> Llanowar kept, Mystic surplus.
TEST_CASE("Selesnya discard S4: Genesis Wave below 8 mana within 2 turns is shed first")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Ageless Entity", "Feed the Clan", "Llanowar Elves", "Elvish Mystic",
          "Nykthos Paragon", "Brushland", "Craterhoof Behemoth" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 7, 5, 4, 2, 1, 3, 6 });
    CHECK(Gen(s).front() == 7);   // generic sheds Craterhoof
}

// Board 3 Forests + Priest + Llanowar (Priest taps for 2): M1 = 6; Archdruid + Mystic deploy, and M2 =
// 3 + Priest(4) + 1 + Archdruid(4) + 1 = 13 >= 8, so the Wave IS castable within 2 turns. Ranking by
// castability: Bogbeast, Ageless (distance 1; Ageless enabled by the Bogbeast / Avatar in hand), then
// Craterhoof > Wave > Avatar at distance 2 -- the 4th and 5th are surplus. Target 8 -> Archdruid alone
// reaches it (M2 = 10), Mystic is a surplus Elf.
TEST_CASE("Selesnya discard S5: castability first, Wave with 8+ mana is a real payoff, Elves scale")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Forest", "Elvish Archdruid", "Elvish Mystic", "Blossoming Bogbeast",
          "Verdant Sun's Avatar", "Craterhoof Behemoth", "Ageless Entity" },
        { "Forest", "Forest", "Forest", "Priest of Titania", "Llanowar Elves" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 5, 0, 3, 6, 7, 4, 2 });
    CHECK(Gen(s).front() == 6);   // generic sheds Craterhoof
}

// "Keep one Paragon when reachable soon and enabled": castability alone ranks Bogbeast, Ageless
// (distance 1, M1 = 5) above the Paragon (distance 2, WW from Brushland + Branchloft); the Paragon takes
// the 2nd HARD slot and Ageless becomes the soft 3rd. Target 6 -> Priest kept, Llanowar / Mystic surplus.
TEST_CASE("Selesnya discard S6: a reachable, enabled Paragon is promoted to a hard slot")
{
    const GameState s = MakeSel(
        { "Nykthos Paragon", "Ageless Entity", "Blossoming Bogbeast", "Llanowar Elves",
          "Priest of Titania", "Elvish Mystic", "Branchloft Pathway", "Feed the Clan" },
        { "Forest", "Brushland", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 3, 1, 7, 0, 2, 4, 6 });
    CHECK(Gen(s).front() == 0);   // generic sheds the Paragon
}

// No lifegain source in hand or play: Paragon and Ageless are reachable but NOT enabled -> shed in the
// payoff group (Ageless first, the USER's order reversed). Craterhoof is the one live payoff; target 8
// keeps Priest + Archdruid (M2 = 9), Llanowar and Mystic are surplus Elves.
TEST_CASE("Selesnya discard S7: Paragon / Ageless with no lifegain source are not enabled")
{
    const GameState s = MakeSel(
        { "Nykthos Paragon", "Ageless Entity", "Craterhoof Behemoth", "Llanowar Elves",
          "Priest of Titania", "Elvish Mystic", "Branchloft Pathway", "Elvish Archdruid" },
        { "Forest", "Brushland", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 5, 3, 7, 2, 4, 6 });
    CHECK(Gen(s).front() == 2);   // generic sheds Craterhoof
}

// A Wellwisher in play fills LIFEGAIN 1 (net of board) and is the enabling source: the hand Feed the
// Clan is surplus lifegain, right after the surplus Forest.
TEST_CASE("Selesnya discard S8: a Wellwisher in play fills LIFEGAIN 1")
{
    const GameState s = MakeSel(
        { "Feed the Clan", "Ageless Entity", "Llanowar Elves", "Priest of Titania", "Blossoming Bogbeast",
          "Forest", "Elvish Mystic", "Elvish Archdruid" },
        { "Forest", "Forest", "Forest", "Wellwisher" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 0, 6, 2, 7, 1, 4, 3 });
    CHECK(Gen(s).front() == 1);   // generic sheds Ageless (MV 5, first)
}

TEST_CASE("Selesnya discard S9: a staged card is never named by the policy and sorts last")
{
    GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    s.players[0].hand[4].m_is_staged = true;   // the Paragon: no W rule, no lifegain payoff held
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 3, 5, 7, 8, 6, 2, 4 });
    CHECK(std::find(o.begin(), o.end() - 1, 4) == o.end() - 1);
}

// WHY CleanupDiscardShedStable() is false: "held" is literal, so an unreachable Paragon keeps its Feed
// the Clan -- but once that Paragon is shed the Feed is lifegain with no payoff and jumps to the front.
// A batched prefix would have shed the Mystic next; the rule applied card by card sheds the Feed.
TEST_CASE("Selesnya discard S10: the order is not prefix-stable, which is why the engine re-asks")
{
    GameState s = MakeSel(
        { "Nykthos Paragon", "Feed the Clan", "Llanowar Elves", "Elvish Mystic", "Priest of Titania",
          "Blossoming Bogbeast", "Craterhoof Behemoth", "Elvish Archdruid" },
        { "Forest", "Forest", "Forest" });
    CHECK(Sel(s) == std::vector<int>{ 6, 0, 3, 2, 1, 7, 5, 4 });
    s.players[0].hand.erase(s.players[0].hand.begin() + 6);   // shed Craterhoof
    CHECK(Sel(s) == std::vector<int>{ 0, 3, 2, 1, 6, 5, 4 });  // still the prefix
    s.players[0].hand.erase(s.players[0].hand.begin() + 0);   // shed the Paragon
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 2, 1, 5, 4, 3 });          // Feed first, not the Mystic
    CHECK(o != std::vector<int>{ 2, 1, 0, 5, 4, 3 });          // the batched remainder
}
