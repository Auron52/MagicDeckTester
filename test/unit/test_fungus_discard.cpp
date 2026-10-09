// Unit tests for the Fungus cleanup-discard BUCKETS: FungusProvider::CleanupDiscardCandidates,
// implementing docs/design/fungus-cleanup-discard.md section 5 (the USER's 2026-09-24 buckets) and
// section 6 (the gaps the USER settled 2026-10-09). Every expected order below is derived BY HAND from
// the design text (worked in each case's comment), not read off the implementation:
//   MANA to 5 total counting the board (<= 3 lands, <= 1 accelerator, the 5 includes it);
//   ENABLERS 1 Doubling Season + 1 Beastmaster; THREATS a core of one Sporecrown / Sporesower /
//   Fungus 1-drop (Utopia Mycon preferred) / Mycoloth; the Season + its combo partners kept TOGETHER
//   above Beastmaster and the rest of the core unless 5 mana is out of reach within 2 turns; then
//   Beastmaster gives way first, then Sporesower, Sporecrown, the 1-drop; mana within the 5 last;
//   beyond the core one of each name first, Essence Warden last; a dead Simic Growth Chamber first.
// Every board also pins the GENERIC fallback's index 0 -- the control arm that MUST differ -- and one
// case proves the MTG_FUNGUS_BUCKET_DISCARD=0 hatch restores exactly the generic ranking.
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

void EnsureCardsFungusDiscard()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build the hand
// that way so a policy reading the card's own (empty) type / cost masks would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeFungus(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsFungusDiscard();
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

std::vector<int> FuArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::FUNGUS_BUCKET_DISCARD] = arm;
    std::vector<int> v = FungusProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::FUNGUS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Fu(const GameState& s)  { return FuArm(s, 1); }
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (non-staged ones named by the policy, staged ones last via tier C).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

// PREFIX STABILITY (the provider keeps DecisionProvider::CleanupDiscardShedStable's default `true`):
// shedding the front card and re-ranking the smaller hand must give the rest of the original order.
void CheckPrefixStable(GameState s)
{
    std::vector<int> order = Fu(s);
    while (s.players[0].hand.size() > 1)
    {
        const int v = order.front();
        s.players[0].hand.erase(s.players[0].hand.begin() + v);
        std::vector<int> expect;
        for (std::size_t k = 1; k < order.size(); ++k)
        { const int i = order[k]; expect.push_back(i > v ? i - 1 : i); }
        order = Fu(s);
        CHECK(order == expect);
    }
}

const char* kSlimefoot = "Slimefoot, the Stowaway";

}   // namespace

// No land in play and none but Karoos in hand: each Chamber would return ITSELF -- both dead, first.
// MANA: board 0 -> need 5, no live land -> nothing kept, reach2 = 0 < 5 -> the tier is NOT castable.
// Ladder: Mycon(6) > Sporecrown(5) > Sporesower(7) > [tier: Season(3) > Mycoloth(2)] > Thallid(4).
TEST_CASE("Fungus discard F1: a dead Simic Growth Chamber is shed first (the Karoo rule)")
{
    const GameState s = MakeFungus(
        { "Simic Growth Chamber", "Simic Growth Chamber", "Mycoloth", "Doubling Season", "Thallid",
          "Sporecrown Thallid", "Utopia Mycon", "Sporesower Thallid" },
        {});
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 0, 1, 4, 2, 3, 7, 5, 6 });
    CHECK(Gen(s).front() == 2);   // generic max-MV sheds Mycoloth
    CheckPermutation(s, o);
}

// Board 2 Forests -> need 3: keep 3 of the 4 hand Forests; need - lands = 0, so Wild Growth is
// SURPLUS ("the max 5 includes the accelerator mana"). reach2 = 2 + 1 + 1 = 4 < 5 -> the tier is
// not castable within 2 turns and gives way BEFORE Sporecrown.
TEST_CASE("Fungus discard F2: mana to 5 counting the board -- the 4th land and the accelerator are surplus")
{
    const GameState s = MakeFungus(
        { "Forest", "Forest", "Forest", "Forest", "Wild Growth", "Doubling Season", "Mycoloth",
          "Sporecrown Thallid" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 3, 4, 6, 5, 7, 2, 1, 0 });
    CHECK(Gen(s).front() == 5);   // generic sheds Doubling Season
    CheckPrefixStable(s);
}

// Board 4 mana (Forest, Forest, Chamber = 2) -> need 1: keep one Forest; reach2 = 4 + 1 = 5 -> the
// tier is castable and sits ABOVE the core. Within target the order of giving way is: beyond the core
// (Warden last-kept = first shed, then the Thallid), Beastmaster, Sporesower, Sporecrown, the 1-drop,
// the tier (Mycoloth, then the Season), the Forest.
TEST_CASE("Fungus discard F3: Season + Mycoloth kept together above Beastmaster and the core")
{
    const GameState s = MakeFungus(
        { "Forest", "Doubling Season", "Mycoloth", "Beastmaster Ascension", "Sporesower Thallid",
          "Sporecrown Thallid", "Utopia Mycon", "Thallid", "Essence Warden" },
        { "Forest", "Forest", "Simic Growth Chamber" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 8, 7, 3, 4, 5, 6, 2, 1, 0 });
    CHECK(Gen(s).front() == 1);   // generic sheds Doubling Season
    CheckPrefixStable(s);
}

TEST_CASE("Fungus discard F3b: MTG_FUNGUS_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeFungus(
        { "Forest", "Doubling Season", "Mycoloth", "Beastmaster Ascension", "Sporesower Thallid",
          "Sporecrown Thallid", "Utopia Mycon", "Thallid", "Essence Warden" },
        { "Forest", "Forest", "Simic Growth Chamber" });
    const std::vector<int> off = FuArm(s, 0);
    const std::vector<int> on  = FuArm(s, 1);
    CHECK(off == Gen(s));
    CHECK(off.front() == 1);
    CHECK(on.front() == 8);
    CHECK(on != off);   // the control that MUST differ
}

// One Season and one Beastmaster AT MOST, net of board: the hand Season is surplus (one is in play),
// the 2nd Beastmaster is surplus. Surplus enablers shed Beastmaster first, then Season (agent reading).
// Board 3 mana -> need 2 (both hand Forests); reach2 = 5 -> castable; the tier is Mycoloth alone (it
// still combos with the Season in play). No Mycon anywhere -> Thallid is the core 1-drop.
TEST_CASE("Fungus discard F4: surplus enablers (2nd Beastmaster, a Season with one in play)")
{
    const GameState s = MakeFungus(
        { "Forest", "Forest", "Doubling Season", "Beastmaster Ascension", "Beastmaster Ascension",
          "Mycoloth", "Sporecrown Thallid", "Thallid" },
        { "Forest", "Forest", "Forest", "Doubling Season" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 4, 2, 3, 6, 7, 5, 1, 0 });
    CHECK(Gen(s).front() == 2);   // generic sheds the Season
    CheckPrefixStable(s);
}

// Board 5 Forests -> need 0: the hand Forest is surplus. Core 1-drop = Utopia Mycon over Thallid
// ("preferably Utopia Mycon"). Beyond the core: one of each name first (Thallid, Tukatongue,
// Psychotrope, Shell-Dweller), then the 2nd Thallid, Essence Warden last. Read backwards: Warden,
// the duplicate Thallid, Shell-Dweller, Psychotrope, Tukatongue, Thallid, Sporecrown, Mycon.
TEST_CASE("Fungus discard F5: Mycon preferred as the 1-drop; one of each name first, Warden last")
{
    const GameState s = MakeFungus(
        { "Forest", "Thallid", "Utopia Mycon", "Tukatongue Thallid", "Thallid", "Psychotrope Thallid",
          "Thallid Shell-Dweller", "Essence Warden", "Sporecrown Thallid" },
        { "Forest", "Forest", "Forest", "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 0, 7, 4, 6, 5, 3, 1, 8, 2 });
    CHECK(Gen(s).front() == 5);   // generic sheds Psychotrope (MV 3)
    CheckPrefixStable(s);
}

// Net of board: a Mycon and a Sporecrown already in play fill the core 1-drop and lord slots, so the
// hand copies are beyond the core -- where the 2nd Sporecrown is kept first (lord), Warden last.
TEST_CASE("Fungus discard F6: core slots filled from the board; the hand lord leads the rest")
{
    const GameState s = MakeFungus(
        { "Utopia Mycon", "Sporecrown Thallid", "Thallid", "Thallid Shell-Dweller", "Essence Warden",
          "Tukatongue Thallid", "Psychotrope Thallid", "Forest" },
        { "Forest", "Forest", "Forest", "Forest", "Forest", "Utopia Mycon", "Sporecrown Thallid" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 7, 4, 3, 6, 5, 2, 0, 1 });
    CHECK(Gen(s).front() == 6);
    CheckPermutation(s, o);
}

// The candidate-B list (param-keyed, never a name): Saproling Burst's fade-counter tokens make it a
// Season partner; Sol Ring is the accelerator (rock); a 2nd Slimefoot in hand is a dead legend.
// Board 2 -> need 3: Forest + Hickory Woodlot (untapped first), need - 2 = 1 -> Sol Ring kept;
// reach2 = 2 + 1 + 2 + 2 = 7 -> castable. Ladder: Forest, Woodlot, Sol Ring, [Season, Mycoloth,
// Burst], Sporecrown, Slimefoot.
TEST_CASE("Fungus discard F7: Saproling Burst joins the Season tier; Sol Ring accelerates; dead 2nd legend")
{
    const GameState s = MakeFungus(
        { "Saproling Burst", "Doubling Season", "Mycoloth", "Sol Ring", "Forest", kSlimefoot, kSlimefoot,
          "Sporecrown Thallid", "Hickory Woodlot" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 6, 5, 7, 0, 2, 1, 3, 8, 4 });
    CHECK(Gen(s).front() == 0);   // generic sheds Saproling Burst (MV 5, first)
    CheckPrefixStable(s);
}

// Board 1 Forest -> need 4: one hand Forest; reach2 = 1 + 1 = 2 < 5 -> NOT castable, so the tier gives
// way right after the beyond-core cards and BEFORE Beastmaster (then Sporecrown, the 1-drop, the land).
TEST_CASE("Fungus discard F8: an uncastable Season tier gives way before Beastmaster")
{
    const GameState s = MakeFungus(
        { "Forest", "Doubling Season", "Mycoloth", "Beastmaster Ascension", "Sporecrown Thallid",
          "Thallid", "Tukatongue Thallid", "Essence Warden" },
        { "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 7, 6, 2, 1, 3, 4, 5, 0 });
    CHECK(Gen(s).front() == 1);
    CheckPrefixStable(s);
}

// Board 2 -> need 3, one hand Forest -> need - 1 = 2 >= 1 -> ONE Wild Growth is kept (the second is
// surplus mana); reach2 = 2 + 1 + 1 = 4 -> not castable.
TEST_CASE("Fungus discard F9: one accelerator within the 5, the second is surplus")
{
    const GameState s = MakeFungus(
        { "Forest", "Wild Growth", "Wild Growth", "Doubling Season", "Mycoloth", "Sporecrown Thallid",
          "Thallid", "Sporesower Thallid" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 2, 4, 3, 7, 5, 6, 1, 0 });
    CHECK(Gen(s).front() == 3);
    CheckPrefixStable(s);
}

// A land Aura with no land to enchant (none in play, none kept) is not mana at all: surplus, first.
TEST_CASE("Fungus discard F10: Wild Growth with no host is surplus")
{
    const GameState s = MakeFungus(
        { "Wild Growth", "Thallid", "Sporecrown Thallid", "Mycoloth", "Doubling Season", "Utopia Mycon",
          "Sporesower Thallid", "Beastmaster Ascension" },
        {});
    const std::vector<int> o = Fu(s);
    CHECK(o.front() == 0);
    CHECK(Gen(s).front() == 3);
    CheckPermutation(s, o);
}

TEST_CASE("Fungus discard F11: a staged card is never named by the policy and sorts last")
{
    GameState s = MakeFungus(
        { "Forest", "Doubling Season", "Mycoloth", "Beastmaster Ascension", "Sporesower Thallid",
          "Sporecrown Thallid", "Utopia Mycon", "Thallid", "Essence Warden" },
        { "Forest", "Forest", "Simic Growth Chamber" });
    s.players[0].hand[2].m_is_staged = true;   // Mycoloth staged: the tier is the Season alone
    const std::vector<int> o = Fu(s);
    REQUIRE(o.size() == 9);
    CHECK(o.back() == 2);
    CHECK(std::find(o.begin(), o.end() - 1, 2) == o.end() - 1);
    CHECK(o == std::vector<int>{ 8, 7, 3, 4, 5, 6, 1, 0, 2 });
}
