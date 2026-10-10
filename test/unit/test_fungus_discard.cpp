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

// Board 2 Forests -> need 3: keep 3 of the 4 hand Forests (1 + 1 + 1 covers it); the need is met, so
// Wild Growth is SURPLUS ("the max 5 includes the accelerator mana"). Board 2 + kept 3 = 5 -> the tier
// IS castable (integrator reading 2026-10-09) and is kept above Sporecrown: read backwards, Sporecrown,
// Mycoloth, the Season, the Forests. (The old 2-turn reading shed Mycoloth before Sporecrown here.)
TEST_CASE("Fungus discard F2: mana to 5 counting the board -- the 4th land and the accelerator are surplus")
{
    const GameState s = MakeFungus(
        { "Forest", "Forest", "Forest", "Forest", "Wild Growth", "Doubling Season", "Mycoloth",
          "Sporecrown Thallid" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 3, 4, 7, 6, 5, 2, 1, 0 });
    CHECK(Gen(s).front() == 5);   // generic sheds Doubling Season
    CheckPrefixStable(s);
}

// THE REVIEWER'S BOARD for the integrator reading (2026-10-09): Forest x2 in play, Forest x3 kept make
// exactly the 5 the USER sized the bucket to, so the Season tier is castable and kept TOGETHER above
// Beastmaster and the core. Ladder: Forests, Season, Mycoloth, Mycon, Sporecrown, Sporesower,
// Beastmaster -> backwards Beastmaster gives way first. (The 2-turn reading shed Mycoloth, then the
// Season -- exactly what the generic rule does.)
TEST_CASE("Fungus discard F2b: 2 Forests in play + 3 kept = 5 -> the Season tier is castable")
{
    const GameState s = MakeFungus(
        { "Forest", "Forest", "Forest", "Doubling Season", "Mycoloth", "Beastmaster Ascension",
          "Sporecrown Thallid", "Sporesower Thallid", "Utopia Mycon" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 5, 7, 6, 8, 4, 3, 2, 1, 0 });
    CHECK(Gen(s).front() == 3);   // generic sheds the Season
    CheckPrefixStable(s);
}

// LAND MANA BY WHAT IT MAKES (review fix 2026-10-09): board 2 -> need 3. The candidate-B depletion lands
// make 2 each, so the first Woodlot (2) and the second (4) cover it -- the Peat Bog is surplus, and with
// the need met the Sol Ring is surplus too. Board 2 + kept 4 = 6 -> the tier is castable.
// Shed: Peat Bog, Sol Ring, then the ladder (Woodlot, Woodlot, Season, Mycoloth, Mycon, Sporecrown)
// backwards. (Counting each land as 1 kept all three lands and shed the Sol Ring first.)
TEST_CASE("Fungus discard F2c: a 2-mana land fills two of the five")
{
    const GameState s = MakeFungus(
        { "Hickory Woodlot", "Hickory Woodlot", "Peat Bog", "Sol Ring", "Doubling Season", "Mycoloth",
          "Sporecrown Thallid", "Utopia Mycon" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 2, 3, 6, 7, 5, 4, 1, 0 });
    CHECK(Gen(s).front() == 4);   // generic sheds the Season
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

// USER 2026-10-10: "Creature quotas are less worried about the board than land quotas." A Mycon and a
// Sporecrown already in play fill NOTHING: the hand Mycon is the core 1-drop and the hand Sporecrown the
// core lord. Board 5 Forests -> need 0, the hand Forest is surplus. Ladder: Mycon, Sporecrown, then
// beyond the core Thallid, Tukatongue (rank 2), Psychotrope (3), Shell-Dweller (4), Warden last.
// Backwards: Warden, Shell-Dweller, Psychotrope, Tukatongue, Thallid, Sporecrown, Mycon.
// (Netting against the board made both hand copies beyond-core, the Mycon shed before the Sporecrown.)
TEST_CASE("Fungus discard F6: creatures in play fill no core slot -- the hand Mycon and lord are the core")
{
    const GameState s = MakeFungus(
        { "Utopia Mycon", "Sporecrown Thallid", "Thallid", "Thallid Shell-Dweller", "Essence Warden",
          "Tukatongue Thallid", "Psychotrope Thallid", "Forest" },
        { "Forest", "Forest", "Forest", "Forest", "Forest", "Utopia Mycon", "Sporecrown Thallid" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 7, 4, 3, 6, 5, 2, 1, 0 });
    CHECK(Gen(s).front() == 6);
    CheckPrefixStable(s);
}

// Same ruling for the Sporesower slot and the Season tier's Mycoloth (a partner is a threat): with a
// Sporesower and a Mycoloth in play, the hand copies still fill their slots. Board 5 Forests -> the
// hand Forest is surplus; 5 >= 5 -> the tier (Mycoloth alone, no Season) is castable. 1-drop: no Mycon
// -> the first Thallid. Ladder: Mycoloth, Thallid, Sporecrown, Sporesower, Beastmaster, then beyond the
// core Tukatongue (a new name) and the 2nd Thallid. Backwards: Thallid, Tukatongue, Beastmaster,
// Sporesower, Sporecrown, Thallid, Mycoloth. (Netting against the board left the hand Sporesower and
// Mycoloth beyond the core, kept BELOW Beastmaster.)
TEST_CASE("Fungus discard F12: a Sporesower and a Mycoloth in play do not fill the hand's slots")
{
    const GameState s = MakeFungus(
        { "Sporesower Thallid", "Mycoloth", "Beastmaster Ascension", "Thallid", "Thallid", "Forest",
          "Sporecrown Thallid", "Tukatongue Thallid" },
        { "Forest", "Forest", "Forest", "Forest", "Forest", "Sporesower Thallid", "Mycoloth" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 5, 4, 7, 2, 0, 6, 3, 1 });
    CHECK(Gen(s).front() == 1);   // generic sheds Mycoloth
    CheckPrefixStable(s);
}

// The "at most 1" ENABLER rule still counts the board: a Beastmaster in play makes the hand copy
// surplus (shed right after the surplus land), unlike the creatures above.
TEST_CASE("Fungus discard F13: a Beastmaster in play still makes the hand copy surplus")
{
    const GameState s = MakeFungus(
        { "Beastmaster Ascension", "Forest", "Sporecrown Thallid", "Thallid", "Mycoloth",
          "Sporesower Thallid", "Utopia Mycon", "Tukatongue Thallid" },
        { "Forest", "Forest", "Forest", "Forest", "Forest", "Beastmaster Ascension" });
    // need 0 -> Forest(1) surplus; Beastmaster(0) surplus (one in play). Ladder: Mycoloth(4), Mycon(6),
    // Sporecrown(2), Sporesower(5), then Thallid(3), Tukatongue(7) beyond the core.
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 1, 0, 7, 3, 5, 2, 6, 4 });
    CHECK(Gen(s).front() == 4);   // generic sheds Mycoloth
    CheckPrefixStable(s);
}

// The candidate-B list (param-keyed, never a name): Saproling Burst's fade-counter tokens make it a
// Season partner; Sol Ring is the accelerator (rock); a 2nd Slimefoot in hand is a dead legend.
// Board 2 -> need 3: Forest (1, untapped first) + Hickory Woodlot (2) cover it, so the Sol Ring is
// SURPLUS mana (it sheds right after the dead legend); 2 + 3 = 5 -> castable. Ladder: Forest, Woodlot,
// [Season, Mycoloth, Burst], Sporecrown, Slimefoot.
TEST_CASE("Fungus discard F7: Saproling Burst joins the Season tier; dead 2nd legend; Sol Ring surplus")
{
    const GameState s = MakeFungus(
        { "Saproling Burst", "Doubling Season", "Mycoloth", "Sol Ring", "Forest", kSlimefoot, kSlimefoot,
          "Sporecrown Thallid", "Hickory Woodlot" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 6, 3, 5, 7, 0, 2, 1, 8, 4 });
    CHECK(Gen(s).front() == 0);   // generic sheds Saproling Burst (MV 5, first)
    CheckPrefixStable(s);
}

// Sol Ring IS the accelerator when the lands fall short: board 2, one Forest kept -> 1 of need 3, so the
// rock is kept (+2 -> 3, met); 2 + 3 = 5 -> castable.
TEST_CASE("Fungus discard F7b: Sol Ring kept as the one accelerator when the lands fall short")
{
    const GameState s = MakeFungus(
        { "Saproling Burst", "Doubling Season", "Mycoloth", "Sol Ring", "Forest", "Sporecrown Thallid",
          "Utopia Mycon", "Thallid" },
        { "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    // Ladder: Forest(4), Sol Ring(3), [Season(1), Mycoloth(2), Burst(0)], Mycon(6), Sporecrown(5),
    // Thallid(7) beyond the core.
    CHECK(o == std::vector<int>{ 7, 5, 6, 0, 2, 1, 3, 4 });
    CHECK(Gen(s).front() == 0);
    CheckPrefixStable(s);
}

// Board 1 Forest -> need 4: one hand Forest; board + kept = 1 + 1 = 2 < 5 -> NOT castable (the bucket
// cannot be filled), so the tier gives way right after the beyond-core cards and BEFORE Beastmaster
// (then Sporecrown, the 1-drop, the land).
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
// surplus mana); board + kept = 2 + 1 + 1 = 4 < 5 -> not castable.
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

// ---- USER 2026-10-10, cross-deck: "Colour beats the land count is correct." --------------------------
// A land that gives a KEPT spell a colour nothing on the board or already kept provides is kept even past
// the mana quota, right behind that spell in the ladder (so it gives way just before the spell).

// Candidate B: 5 Forests in play -> need 0, so by the quota every hand land is surplus. But Slimefoot
// ({1}{B}{G}) is kept (beyond the core) and nothing makes black: the Peat Bog is kept right behind it, and
// Deathspore ({1}{B}) then needs nothing more. Ladder: Mycoloth (tier), Mycon, Sporecrown, Slimefoot,
// Peat Bog, Thallid, Deathspore, Tukatongue. Backwards: Tukatongue, Deathspore, Thallid, Peat Bog,
// Slimefoot, Sporecrown, Mycon, Mycoloth. (The quota alone shed the Peat Bog FIRST.)
TEST_CASE("Fungus discard F14: a black land for Slimefoot is kept past the mana quota (candidate B)")
{
    const GameState s = MakeFungus(
        { "Peat Bog", kSlimefoot, "Sporecrown Thallid", "Thallid", "Mycoloth", "Utopia Mycon",
          "Deathspore Thallid", "Tukatongue Thallid" },
        { "Forest", "Forest", "Forest", "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 7, 6, 3, 0, 1, 2, 5, 4 });
    CHECK(Gen(s).front() == 4);   // generic sheds Mycoloth
    CheckPrefixStable(s);
}

// The BEST such land in the land keep order: Secluded Courtyard (untapped; its colours pay creature
// spells, and Slimefoot is one) comes before the Peat Bog (enters tapped), so the Courtyard is kept
// behind Slimefoot and the Peat Bog and the Forest stay surplus. Ladder: Mycoloth, Mycon, Sporecrown,
// Slimefoot, Courtyard, Thallid. (The quota alone shed all three lands first: [0, 7, 2, ...].)
TEST_CASE("Fungus discard F15: the best colour land is kept -- Secluded Courtyard pays a creature's black")
{
    const GameState s = MakeFungus(
        { "Peat Bog", kSlimefoot, "Secluded Courtyard", "Sporecrown Thallid", "Mycoloth", "Utopia Mycon",
          "Thallid", "Forest" },
        { "Forest", "Forest", "Forest", "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 2, 1, 3, 5, 4 });
    CHECK(Gen(s).front() == 4);
    CheckPrefixStable(s);
}

// The shipped all-green 60: every spell is green and green is on the board, so the rule keeps nothing
// extra -- the surplus Chamber and Forests shed exactly as the quota says (Chamber first: the land keep
// order puts the Karoo last). Ladder: Season, Mycoloth, Thallid (no Mycon), Sporecrown, Warden.
TEST_CASE("Fungus discard F16: the colour rule is inert on the shipped all-green list")
{
    const GameState s = MakeFungus(
        { "Forest", "Simic Growth Chamber", "Forest", "Sporecrown Thallid", "Thallid", "Mycoloth",
          "Doubling Season", "Essence Warden" },
        { "Forest", "Forest", "Forest", "Forest", "Forest" });
    const std::vector<int> o = Fu(s);
    CHECK(o == std::vector<int>{ 1, 2, 0, 7, 3, 4, 5, 6 });
    CHECK(Gen(s).front() == 5);   // generic sheds Mycoloth
    CheckPrefixStable(s);
}
