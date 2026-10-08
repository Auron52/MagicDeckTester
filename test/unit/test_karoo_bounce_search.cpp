// Unit tests for the SEARCHED Karoo bounce (MTG_BOUNCE_SEARCH -> KarooBounceSearchOn, heurarm slot
// BOUNCE_SEARCH; Plan::bounce_choice), 2026-10-08. USER: "The Karoo decision should also be searched
// with heuristics. However, we may want to heuristic it more often than not, since the decision is
// usually an easy one."
//
// DecisionProvider::BounceSearchCandidates narrows the returnable lands on the state AT THE BOUNCE:
//  1. a TAPPED land with a clean (untapped) replay dominates everything else -> one candidate;
//  2. identical lands fold; with no other land in hand, tapped lands of one re-entry class fold;
//  3. an untapped land that re-enters tapped is dominated by a tapped land;
//  4. another Karoo / an enchanted land is never offered while something else is;
//  5. UNTAPPED lands of different names stay contested -- the Dragons s4004 gi47 board (a floatable
//     Mountain vs Haven of the Spirit Dragon's restricted mana) is two candidates;
//  6. the front is always BounceLandCandidates' front, and BounceKarooLand honours a pinned index
//     (ScriptedBounceChoice) only with the axis on.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"

#include <string>
#include <utility>
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

int Put(GameState& s, const std::string& name, int number, bool tapped)
{
    Permanent p;
    p.card             = D(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

struct Arm
{
    int slot; std::int8_t prev;
    Arm(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~Arm() { heurarm::t_arm[slot] = prev; }
};

// Every land the Karoo (the LAST slot) may return -- BounceKarooLand's own legal list.
std::vector<int> Legal(const GameState& s)
{
    std::vector<int> out;
    for (int i = 0; i + 1 < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.IsLand()) { out.push_back(i); } }
    return out;
}

std::vector<std::string> Names(const GameState& s, const std::vector<int>& idx)
{
    std::vector<std::string> out;
    for (int i : idx) { out.push_back(s.battlefield[static_cast<std::size_t>(i)].card.m_name.str()); }
    return out;
}

std::vector<int> Cands(const GameState& s, int* why = nullptr)
{
    const int self = static_cast<int>(s.battlefield.size()) - 1;
    return ResolveProvider(s).BounceSearchCandidates(s, 0, self, Legal(s), why);
}

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Karoo bounce search: a tapped land with a clean replay is the ONE candidate")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    GameState s = Fresh();
    Put(s, "Forest", 10, /*tapped=*/false);
    Put(s, "Mountain", 11, /*tapped=*/true);
    Put(s, "Mystic Monastery", 12, /*tapped=*/true);   // tapped, but re-enters tapped
    Put(s, "Gruul Turf", 13, true);
    Hand(s, "Island", 20);                             // another land in hand: no replay fold
    int why = -1;
    const std::vector<int> c = Cands(s, &why);
    CHECK(Names(s, c) == std::vector<std::string>{ "Mountain" });
    CHECK(why == DecisionProvider::kBounceTappedClean);
}

TEST_CASE("Karoo bounce search: identical lands fold; with no other land in hand, tapped lands fold by re-entry class")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    {
        GameState s = Fresh();
        Put(s, "Forest", 10, true);
        Put(s, "Forest", 11, true);
        Put(s, "Gruul Turf", 13, true);
        Hand(s, "Mountain", 20);
        int why = -1;
        CHECK(Cands(s, &why).size() == 1);
        CHECK(why == DecisionProvider::kBounceIdentical);
    }
    {
        // Two tapped basics of different colours: with a land in hand the colour left on the
        // battlefield matters next turn (contested); with none, the returned land IS next turn's
        // drop and the choice is moot (one candidate).
        GameState s = Fresh();
        Put(s, "Forest", 10, true);
        Put(s, "Mountain", 11, true);
        Put(s, "Gruul Turf", 13, true);
        int why = -1;
        CHECK(Cands(s, &why).size() == 1);
        CHECK(why == DecisionProvider::kBounceReplayFold);
        Hand(s, "Island", 20);
        CHECK(Cands(s, &why).size() == 2);
        CHECK(why == DecisionProvider::kBounceContested);
    }
}

TEST_CASE("Karoo bounce search: an untapped land that re-enters tapped is dominated by a tapped one; a clean untapped land is contested")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    GameState s = Fresh();
    Put(s, "Mystic Monastery", 10, /*tapped=*/true);    // spent, re-enters tapped
    Put(s, "Thundering Falls", 11, /*tapped=*/false);  // unspent AND re-enters tapped: dominated
    Put(s, "Izzet Boilerworks", 13, true);
    Hand(s, "Island", 20);
    int why = -1;
    CHECK(Names(s, Cands(s, &why)) == std::vector<std::string>{ "Mystic Monastery" });
    CHECK(why == DecisionProvider::kBounceDominated);
    // The USER's 2026-10-06 case: an untapped Forbidden Orchard (re-enters untapped) against the
    // tapped Monastery -- a real trade (this turn's mana vs next turn's tempo), so both are offered,
    // the lever's order (the Orchard) first.
    GameState s2 = Fresh();
    Put(s2, "Mystic Monastery", 10, true);
    Put(s2, "Thundering Falls", 11, false);
    Put(s2, "Forbidden Orchard", 12, false);
    Put(s2, "Izzet Boilerworks", 13, true);
    Hand(s2, "Island", 20);
    const std::vector<int> c = Cands(s2, &why);
    REQUIRE(c.size() == 2);
    CHECK(Names(s2, c) == std::vector<std::string>{ "Forbidden Orchard", "Mystic Monastery" });
    CHECK(why == DecisionProvider::kBounceContested);
}

TEST_CASE("Karoo bounce search: another Karoo is offered only when nothing else is")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    GameState s = Fresh();
    Put(s, "Gruul Turf", 10, /*tapped=*/true);
    Put(s, "Forest", 11, /*tapped=*/false);
    Put(s, "Gruul Turf", 13, true);
    CHECK(Names(s, Cands(s)) == std::vector<std::string>{ "Forest" });
}

TEST_CASE("Karoo bounce search: Dragons s4004 gi47 -- a floatable Mountain vs Haven of the Spirit Dragon is CONTESTED, and the pin returns Haven")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm search(heurarm::BOUNCE_SEARCH, true);
    // T4 of the held-out game: Lightning Greaves was paid by Sol Ring, so every land is untapped when
    // Gruul Turf's bounce resolves. The lever's order returns the Mountain (its {R} floats -- and dies
    // at the end of main 1 unused); Haven's mana is restricted, so it cannot float, but it is the land
    // whose mana the rest of the turn did not need (Scourge of Valkas firebreathes with the Mountains).
    GameState s = Fresh();
    Put(s, "Haven of the Spirit Dragon", 10, false);
    Put(s, "Sol Ring", 11, true);
    Put(s, "Mountain", 12, false);
    Put(s, "Mountain", 13, false);
    Hand(s, "Inferno of the Star Mounts", 20);
    Put(s, "Gruul Turf", 14, true);
    int why = -1;
    const std::vector<int> c = Cands(s, &why);
    REQUIRE(c.size() == 2);
    CHECK(Names(s, c) == std::vector<std::string>{ "Mountain", "Haven of the Spirit Dragon" });
    CHECK(why == DecisionProvider::kBounceContested);
    {
        // The front is BounceLandCandidates' front: the unpinned bounce is unchanged.
        GameState t = s;
        BounceKarooLand(t, 0, static_cast<int>(t.battlefield.size()) - 1);
        CHECK(InHand(t, "Mountain"));
        CHECK(t.floating_mana.red == 1);
    }
    {
        GameState t = s;
        ScriptedBounceChoice pin(1);
        BounceKarooLand(t, 0, static_cast<int>(t.battlefield.size()) - 1);
        CHECK(InHand(t, "Haven of the Spirit Dragon"));
        CHECK(t.floating_mana.Total() == 0);           // Haven's mana is restricted: nothing floats
        CHECK(g_scripted_bounce_choice == -1);          // consumed by the bounce
    }
    {
        // An index past the narrowed set clamps to its last candidate (duplicate, never a whiff).
        GameState t = s;
        ScriptedBounceChoice pin(5);
        BounceKarooLand(t, 0, static_cast<int>(t.battlefield.size()) - 1);
        CHECK(InHand(t, "Haven of the Spirit Dragon"));
    }
    {
        // Axis off: the pin is ignored and the front is returned, byte-identically to before.
        Arm off(heurarm::BOUNCE_SEARCH, false);
        GameState t = s;
        ScriptedBounceChoice pin(1);
        BounceKarooLand(t, 0, static_cast<int>(t.battlefield.size()) - 1);
        CHECK(InHand(t, "Mountain"));
    }
}

// ---- THE USER'S DEFAULT (MTG_BOUNCE_USER_RULE, 2026-10-08) ------------------------------------------
// "We don't need to search if there is a tapped land that comes into play untapped" + "I mean that it
// doesn't generate colours any other land doesn't" + "we need an exemption for cases where the land has
// an aura on it of course."
namespace
{
int PutAura(GameState& s, const std::string& aura, int number, int host_number)
{
    Permanent a;
    a.card             = D(aura).card;
    a.card.m_number    = number;
    a.controller_index = 0;
    a.owner_index      = 0;
    a.aura_attached_to = host_number;
    s.battlefield.push_back(a);
    s.deck_has_land_aura = true;
    return static_cast<int>(s.battlefield.size()) - 1;
}
// Legal returnable lands for a Karoo that is NOT the last slot (an Aura may sit after it).
std::vector<int> LegalExcept(const GameState& s, int self)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (i != self && s.battlefield[i].card.IsLand()) { out.push_back(i); } }
    return out;
}
std::vector<std::string> CandsAt(const GameState& s, int self, int* why)
{
    return Names(s, ResolveProvider(s).BounceSearchCandidates(s, 0, self, LegalExcept(s, self), why));
}
}   // namespace

TEST_CASE("Karoo bounce, USER rule: a tapped land that re-enters untapped and whose colours the kept lands make is returned with no search")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    GameState s = Fresh();
    Put(s, "Forest", 10, true);
    Put(s, "Island", 11, true);
    const int self = Put(s, "Azorius Chancery", 13, true);   // the Karoo stays: it makes {W}{U}
    Hand(s, "Mountain", 20);                                  // a land in hand: no replay fold
    int why = -1;
    {
        Arm off(heurarm::BOUNCE_USER_RULE, false);
        CHECK(CandsAt(s, self, &why).size() == 2);            // Forest vs Island: contested by colour
        CHECK(why == DecisionProvider::kBounceContested);
    }
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    // The Island's {U} is covered by the Chancery; the Forest's {G} by nothing else -> the Island.
    CHECK(CandsAt(s, self, &why) == std::vector<std::string>{ "Island" });
    CHECK(why == DecisionProvider::kBounceUserRule);
    // ...and it is the FRONT, so the unpinned bounce (d0, the playout, the prompt default) returns it.
    const std::vector<int> ranked = ResolveProvider(s).BounceLandCandidates(s, 0, self, LegalExcept(s, self));
    CHECK(s.battlefield[static_cast<std::size_t>(ranked.front())].card.m_name.str() == "Island");
}

TEST_CASE("Karoo bounce, USER rule: nothing qualifies when the land is untapped, re-enters tapped, or makes an uncovered colour")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    int why = -1;
    {
        GameState s = Fresh();   // the only {U}/{G} sources are the candidates themselves
        Put(s, "Forest", 10, true);
        Put(s, "Island", 11, true);
        const int self = Put(s, "Boros Garrison", 13, true);
        Hand(s, "Mountain", 20);
        CHECK(CandsAt(s, self, &why).size() == 2);
        CHECK(why != DecisionProvider::kBounceUserRule);
    }
    {
        GameState s = Fresh();   // a fastland that would re-enter TAPPED (3 other lands after the bounce)
        Put(s, "Razorverge Thicket", 10, true);
        Put(s, "Forest", 11, false);
        Put(s, "Plains", 12, false);
        const int self = Put(s, "Selesnya Sanctuary", 13, true);
        Hand(s, "Mountain", 20);
        CandsAt(s, self, &why);
        CHECK(why != DecisionProvider::kBounceUserRule);
    }
    {
        GameState s = Fresh();   // ...and the same fastland with one other land: re-enters untapped
        Put(s, "Razorverge Thicket", 10, true);
        Put(s, "Forest", 11, false);
        const int self = Put(s, "Selesnya Sanctuary", 13, true);
        Hand(s, "Mountain", 20);
        CHECK(CandsAt(s, self, &why) == std::vector<std::string>{ "Razorverge Thicket" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
}

TEST_CASE("Karoo bounce, USER rule: an enchanted land is never the pick, and its Aura's colour covers another land")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    int why = -1;
    {
        // A tapped Forest carrying Wild Growth would qualify ({G} covered by the Gruul Turf) -- but it
        // is enchanted. The tapped Mountain qualifies too ({R} covered by the Turf): it is the pick.
        GameState s = Fresh();
        Put(s, "Forest", 10, true);
        Put(s, "Mountain", 11, true);
        const int self = Put(s, "Gruul Turf", 13, true);
        PutAura(s, "Wild Growth", 30, 10);
        Hand(s, "Island", 20);
        CHECK(CandsAt(s, self, &why) == std::vector<std::string>{ "Mountain" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
    {
        // The enchanted Forest is the only land that would qualify: the rule does not fire.
        GameState s = Fresh();
        Put(s, "Forest", 10, true);
        Put(s, "Island", 11, false);
        const int self = Put(s, "Gruul Turf", 13, true);
        PutAura(s, "Wild Growth", 30, 10);
        Hand(s, "Plains", 20);
        CandsAt(s, self, &why);
        CHECK(why != DecisionProvider::kBounceUserRule);
    }
    {
        // A tapped Forest whose {G} only the Wild Growth on the (kept) Plains also makes: covered.
        GameState s = Fresh();
        Put(s, "Forest", 10, true);
        Put(s, "Plains", 11, false);
        const int self = Put(s, "Azorius Chancery", 13, true);
        PutAura(s, "Wild Growth", 30, 11);
        Hand(s, "Island", 20);
        CHECK(CandsAt(s, self, &why) == std::vector<std::string>{ "Forest" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
}

// ---- THE USER'S HINATA RULE (HinataProvider::BounceUserRulePick, MTG_HINATA_BOUNCE_RULE) ----------
namespace
{
// Hinata2's main-deck SPELLS, so the rule's "colour the deck needs less" is derived from the real
// list (lands carry no pips; they are left out). Held in the library, where the derivation reads them.
void HinataLibrary(GameState& s)
{
    const std::pair<const char*, int> spells[] = {
        { "Hinata, Dawn-Crowned", 4 }, { "Crackle with Power", 4 }, { "Distorting Wake", 2 },
        { "Icy Blast", 1 }, { "Soulfire Eruption", 4 }, { "Reality Spasm", 4 }, { "Remand", 3 },
        { "Sol Ring", 1 }, { "Memory Lapse", 2 }, { "Expressive Iteration", 1 },
        { "Ornithopter of Paradise", 3 }, { "Ponder", 4 }, { "Preordain", 4 }, { "Gamble", 1 },
        { "Irencrag Feat", 1 }, { "Magma Opus", 1 } };
    int n = 100;
    for (const auto& [name, count] : spells)
    {
        for (int k = 0; k < count; ++k)
        {
            Card c = D(name).card;
            c.m_number = n++;
            s.players[0].library.push_back(c);
        }
    }
}
std::vector<std::string> HinataCands(GameState& s, const HinataProvider& hp, int self, int* why)
{
    s.m_provider = &hp;
    return Names(s, hp.BounceSearchCandidates(s, 0, self, LegalExcept(s, self), why));
}
}   // namespace

TEST_CASE("Karoo bounce, HINATA rule: 2 Islands + 1 Mountain + Forbidden Orchard -> an Island (fewest colours, most copies)")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    Arm hin(heurarm::HINATA_BOUNCE_RULE, true);
    const HinataProvider hp;
    GameState s = Fresh();
    HinataLibrary(s);
    Put(s, "Island", 10, true);
    Put(s, "Mountain", 11, true);
    Put(s, "Forbidden Orchard", 12, true);
    Put(s, "Island", 14, true);
    const int self = Put(s, "Izzet Boilerworks", 13, true);
    Hand(s, "Ponder", 20);
    int why = -1;
    const std::vector<std::string> got = HinataCands(s, hp, self, &why);
    CHECK(got == std::vector<std::string>{ "Island" });
    CHECK(why == DecisionProvider::kBounceUserRule);
    // ...and the same board with the rule OFF falls through to the base rule (a pick, but the
    // base order's, not the Hinata ranking).
    {
        Arm off(heurarm::HINATA_BOUNCE_RULE, false);
        int why2 = -1;
        HinataCands(s, hp, self, &why2);
        CHECK(why2 == DecisionProvider::kBounceUserRule);
    }
}

TEST_CASE("Karoo bounce, HINATA rule: 1 Island + 1 Mountain -> the Mountain (red is the colour Hinata2 needs less)")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    Arm hin(heurarm::HINATA_BOUNCE_RULE, true);
    const HinataProvider hp;
    int why = -1;
    {
        GameState s = Fresh();
        HinataLibrary(s);
        Put(s, "Island", 10, true);
        Put(s, "Mountain", 11, true);
        const int self = Put(s, "Izzet Boilerworks", 13, true);
        Hand(s, "Ponder", 20);
        CHECK(HinataCands(s, hp, self, &why) == std::vector<std::string>{ "Mountain" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
    {
        // Same with the Island listed second and the Mountain UNTAPPED: an untapped land that
        // re-enters untapped qualifies (its mana floats on the return) and the ranking still holds.
        GameState s = Fresh();
        HinataLibrary(s);
        Put(s, "Mountain", 11, false);
        Put(s, "Island", 10, true);
        const int self = Put(s, "Izzet Boilerworks", 13, true);
        Hand(s, "Ponder", 20);
        CHECK(HinataCands(s, hp, self, &why) == std::vector<std::string>{ "Mountain" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
}

TEST_CASE("Karoo bounce, HINATA rule: an enchanted land is never the pick; a land that re-enters tapped never qualifies")
{
    LoadCards();
    Arm lever(heurarm::BOUNCE_UNTAPPED_FIRST, true);
    Arm on(heurarm::BOUNCE_USER_RULE, true);
    Arm hin(heurarm::HINATA_BOUNCE_RULE, true);
    const HinataProvider hp;
    int why = -1;
    {
        // The Mountain would outrank the Island (the less-needed colour) -- but it carries an Aura.
        GameState s = Fresh();
        HinataLibrary(s);
        Put(s, "Island", 10, true);
        Put(s, "Mountain", 11, true);
        const int self = Put(s, "Izzet Boilerworks", 13, true);
        PutAura(s, "Wild Growth", 30, 11);
        Hand(s, "Ponder", 20);
        CHECK(HinataCands(s, hp, self, &why) == std::vector<std::string>{ "Island" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
    {
        // A Mystic Monastery (re-enters tapped) beside the Orchard: only the Orchard qualifies.
        GameState s = Fresh();
        HinataLibrary(s);
        Put(s, "Mystic Monastery", 10, true);
        Put(s, "Forbidden Orchard", 11, true);
        const int self = Put(s, "Izzet Boilerworks", 13, true);
        Hand(s, "Ponder", 20);
        CHECK(HinataCands(s, hp, self, &why) == std::vector<std::string>{ "Forbidden Orchard" });
        CHECK(why == DecisionProvider::kBounceUserRule);
    }
}
