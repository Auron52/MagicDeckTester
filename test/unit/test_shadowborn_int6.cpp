// Shadowborn Apostle onboarding, integrator INT-6 (2026-11-03; docs/design/analysis-Shadowborn Apostle.md): the
// USER-designed cleanup-discard BUCKETS (analyze-deck 5i; ShadowbornProvider::CleanupDiscardShedOrder).
//   * the lever (MTG_SHADOWBORN_BUCKET_DISCARD, heurarm slot) off = the generic ranking;
//   * "I don't want to see it keep Thrumming Stone and 6 lands": Stone + 6 lands + Apostle sheds a land;
//   * a granter in hand while one is on the battlefield sheds FIRST (ahead of the Demons);
//   * fast mana (every Dark Ritual, the fuelled Lake, Culling with fodder) is kept past the 6-mana quota;
//   * fodder for Culling / Diabolic Intent is kept ahead of the mana; without fodder Culling is overflow;
//   * a granter on the battlefield: quota 4, every Apostle, then Reaper (the hardcast threat) over surplus lands;
//   * the Demons' shed order (colour-unreachable, then the hand-discarder, then mana value).
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
const char* const kAp6     = "Shadowborn Apostle";
const char* const kStone6  = "Thrumming Stone";
const char* const kIntent6 = "Diabolic Intent";
const char* const kDT6     = "Demonic Tutor";
const char* const kOver6   = "Abhorrent Overlord";
const char* const kJet6    = "Jetmir, Nexus of Revels";
const char* const kReaper6 = "Reaper from the Abyss";
const char* const kSire6   = "Sire of Insanity";
const char* const kLake6   = "Lake of the Dead";
const char* const kCull6   = "Culling the Weak";
const char* const kRit6    = "Dark Ritual";
const char* const kBloom6  = "Lotus Bloom";
const char* const kSw6     = "Swamp";

using Names6 = std::vector<std::string>;

struct Arm6
{
    heurarm::Arm saved;
    Arm6() : saved(heurarm::t_arm) {}
    Arm6& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~Arm6() { heurarm::t_arm = saved; }
};

Card Card6(const std::string& name, int number)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}
const ShadowbornProvider& Prov6() { static const ShadowbornProvider p; return p; }

// Our cleanup on turn 4: `bf` on our battlefield, `hand` being shed from; a library of Apostles + Swamps.
GameState State6(const Names6& bf, const Names6& hand)
{
    GameState s;
    s.m_provider          = &Prov6();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    int num = 100;
    for (const std::string& n : bf)
    {
        Permanent p;
        p.card             = Card6(n, num++);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(Card6(n, num++)); }
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(Card6(kAp6, 900 + k)); }
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(Card6(kSw6, 950 + k)); }
    return s;
}
Names6 Shed6(const GameState& s)
{
    Names6 out;
    for (int i : Prov6().CleanupDiscardShedOrder(s, nullptr))
    { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
    return out;
}
std::string First6(const GameState& s)
{
    const std::vector<int> c = Prov6().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(c.size() == 1);   // ONE index (5i rule 5)
    return s.players[0].hand[static_cast<std::size_t>(c.front())].m_name.str();
}
int Pos6(const Names6& o, const std::string& n)
{ const auto it = std::find(o.begin(), o.end(), n); return it == o.end() ? -1 : static_cast<int>(it - o.begin()); }
int LastPos6(const Names6& o, const std::string& n)
{
    for (int i = static_cast<int>(o.size()) - 1; i >= 0; --i) { if (o[static_cast<std::size_t>(i)] == n) { return i; } }
    return -1;
}
}   // namespace

TEST_CASE("Shadowborn INT-6 discard: lever off is the generic ranking; on returns ONE index and re-asks per shed")
{
    const Names6 hand = { kStone6, kSw6, kSw6, kSw6, kSw6, kSw6, kSw6, kAp6 };
    const GameState s = State6({}, hand);
    {
        Arm6 arm;
        arm.Set(heurarm::SHADOWBORN_BUCKET_DISCARD, 0);
        CHECK(Prov6().CleanupDiscardCandidates(s, nullptr) == CleanupDiscardRanking(s, nullptr));
        CHECK(Prov6().CleanupDiscardShedStable());
        const std::vector<int> g = Prov6().CleanupDiscardCandidates(s, nullptr);
        REQUIRE(!g.empty());
        CHECK(s.players[0].hand[static_cast<std::size_t>(g.front())].m_name.str() == kStone6);   // generic: max MV -- the Stone
    }
    CHECK(!Prov6().CleanupDiscardShedStable());
    CHECK(Prov6().CleanupDiscardShedOrder(s, nullptr).size() == hand.size());   // the full order
}

TEST_CASE("Shadowborn INT-6 discard: Stone + 6 lands + Apostle sheds a LAND (USER: never Stone + 6 lands over an Apostle)")
{
    const GameState s = State6({}, { kStone6, kSw6, kSw6, kSw6, kSw6, kSw6, kSw6, kAp6 });
    CHECK(First6(s) == kSw6);
    const Names6 o = Shed6(s);
    CHECK(Pos6(o, kAp6) == 4);    // the four quota lands go before the Apostle (only the two-land floor outlasts it)
    CHECK(o.back() == kStone6);   // the Stone slot fills first, so it is the last card shed
    // The same with a board: 3 lands out -> only 3 more needed, so three Swamps are surplus and go first.
    const GameState b = State6({ kSw6, kSw6, kSw6 }, { kStone6, kSw6, kSw6, kSw6, kSw6, kSw6, kSw6, kAp6 });
    const Names6 ob = Shed6(b);
    CHECK(ob[0] == kSw6);
    CHECK(ob[1] == kSw6);
    CHECK(ob[2] == kSw6);
    CHECK(Pos6(ob, kAp6) > 2);
}

TEST_CASE("Shadowborn INT-6 discard: a granter in hand with one on the battlefield sheds FIRST")
{
    const GameState s = State6({ kStone6, kSw6, kSw6, kSw6 }, { kStone6, kJet6, kOver6, kAp6, kRit6, kSw6, kSw6, kAp6 });
    CHECK(First6(s) == kStone6);
    const Names6 o = Shed6(s);
    CHECK(o[0] == kStone6);
    CHECK(o[1] == kJet6);    // then the Demons, colour-unreachable first
    CHECK(o[2] == kOver6);
    // Without a granter on the battlefield the same Stone is the slot's keeper and goes last.
    const GameState t = State6({ kSw6, kSw6, kSw6 }, { kStone6, kJet6, kOver6, kAp6, kRit6, kSw6, kSw6, kAp6 });
    CHECK(Shed6(t).back() == kStone6);
    CHECK(First6(t) == kJet6);
}

TEST_CASE("Shadowborn INT-6 discard: a second granter (none out) sheds after the Demons, before Lotus Bloom and lands")
{
    const GameState s = State6({ kSw6, kSw6 }, { kStone6, kStone6, kSire6, kBloom6, kSw6, kSw6, kAp6, kAp6 });
    const Names6 o = Shed6(s);
    CHECK(o[0] == kSire6);
    CHECK(o[1] == kStone6);
    // INT-8 (earliest turn to the bar): 2 Swamps out + Bloom (suspended next turn, arriving 3 later) + ONE hand
    // Swamp reach six on turn 4, and nothing faster exists -- the Bloom is ON the route (kept) and the second hand
    // Swamp is the off-route surplus that goes next.
    CHECK(o[2] == kSw6);
    CHECK(Pos6(o, kBloom6) > 2);
}

TEST_CASE("Shadowborn INT-6 discard: every Dark Ritual is kept past the 6-mana quota (fast mana, not capped)")
{
    // 3 lands out + 2 Rituals (+2 each) = 7 >= 6: every hand Swamp is surplus, the Rituals stay.
    const GameState s = State6({ kSw6, kSw6, kSw6 }, { kStone6, kAp6, kRit6, kRit6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(s) == kSw6);
    const Names6 o = Shed6(s);
    for (int k = 0; k < 4; ++k) { CHECK(o[static_cast<std::size_t>(k)] == kSw6); }
    CHECK(Pos6(o, kRit6) > 3);
    // Even with no board at all a Ritual outlasts the lands beyond the two-land floor.
    const GameState z = State6({}, { kStone6, kAp6, kRit6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    const Names6 oz = Shed6(z);
    CHECK(oz[0] == kSw6);
    CHECK(oz[1] == kSw6);
    CHECK(oz[2] == kSw6);
    CHECK(Pos6(oz, kRit6) == 3);   // the surplus and both quota lands go first; only the floor lands outlast it
}

TEST_CASE("Shadowborn INT-6 discard: Culling the Weak with fodder is kept WITH its fodder; without, it is overflow")
{
    // No creature out: the second Apostle becomes Culling's fodder, kept ahead of the mana.
    const GameState s = State6({ kSw6, kSw6, kSw6 }, { kStone6, kAp6, kAp6, kCull6, kSw6, kSw6, kSw6, kSw6 });
    const Names6 o = Shed6(s);
    CHECK(First6(s) == kSw6);
    CHECK(Pos6(o, kCull6) > LastPos6(o, kSw6));
    CHECK(Pos6(o, kAp6) > LastPos6(o, kSw6));
    // One Apostle only and no creature out: nothing to sacrifice but Apostle #1 -> Culling is overflow.
    const GameState t = State6({ kSw6, kSw6, kSw6 }, { kStone6, kAp6, kCull6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(t) == kCull6);
    // A creature out is fodder too: Culling stays and needs no hand Apostle.
    const GameState u = State6({ kSw6, kSw6, kSw6, kAp6 }, { kStone6, kAp6, kCull6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(u) == kSw6);
    CHECK(Pos6(Shed6(u), kCull6) > LastPos6(Shed6(u), kSw6));
}

TEST_CASE("Shadowborn INT-6 discard: a Diabolic Intent holding the Stone slot keeps an Apostle of fodder ahead of the mana")
{
    // No granter anywhere: the Intent holds the slot; no creature out, so the second Apostle is its fodder.
    const GameState s = State6({ kSw6, kSw6 }, { kIntent6, kAp6, kAp6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    const Names6 o = Shed6(s);
    CHECK(First6(s) == kSw6);
    CHECK(Pos6(o, kAp6) > LastPos6(o, kSw6));   // both Apostles outlast every Swamp
    CHECK(o.back() == kIntent6);
    // With a creature out the Intent's fodder is on the battlefield: the second Apostle is the plain Apostle #2,
    // filled after the mana, so it sheds right after the surplus Swamp.
    const GameState t = State6({ kSw6, kSw6, kAp6 }, { kIntent6, kAp6, kAp6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    const Names6 ot = Shed6(t);
    CHECK(ot[0] == kSw6);
    CHECK(ot[1] == kAp6);
    // A Demonic Tutor is preferred over the Intent for the slot (no fodder needed).
    const GameState u = State6({ kSw6, kSw6 }, { kIntent6, kDT6, kAp6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(Shed6(u).back() == kDT6);
}

TEST_CASE("Shadowborn INT-6 discard: Lake of the Dead is kept when fuelled (>= 2 Swamps out, no Lake out), else overflow")
{
    const GameState s = State6({ kSw6, kSw6 }, { kStone6, kAp6, kLake6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    const Names6 o = Shed6(s);
    CHECK(First6(s) == kSw6);
    CHECK(Pos6(o, kLake6) > LastPos6(o, kSw6));   // 2 out + Lake 3 = 5: one Swamp kept for the quota, after it
    const GameState t = State6({ kSw6 }, { kStone6, kAp6, kLake6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    // INT-8 (USER 4c + earliest turn): one Swamp out + one hand Swamp fuel it -- Swamp next turn, Lake + crack the
    // turn after (2 + 1 + 3 = 6 on turn 2, against turn 5 on Swamps alone): the Lake is on the route and kept.
    CHECK(First6(t) == kSw6);
    CHECK(Pos6(Shed6(t), kLake6) == 4);   // after the four surplus Swamps (the floor Swamp is the route's land)
    const GameState u = State6({ kSw6, kSw6, kLake6 }, { kStone6, kAp6, kLake6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(u) == kLake6);   // a Lake already out
}

TEST_CASE("Shadowborn INT-6 discard: a granter on the battlefield -- quota 4, every Apostle, then Reaper over surplus lands")
{
    // 2 lands out + 2 kept = 4: three Swamps surplus, shed before any Apostle; Reaper outlasts them.
    const GameState s = State6({ kStone6, kSw6, kSw6 }, { kAp6, kAp6, kAp6, kReaper6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(s) == kSw6);
    const Names6 o = Shed6(s);
    CHECK(o[0] == kSw6);
    CHECK(o[1] == kSw6);
    CHECK(Pos6(o, kReaper6) == 2);   // the last card of fill step 6: first of the kept cards to go
    CHECK(Pos6(o, kAp6) > Pos6(o, kReaper6));
    // Reaper + 7 Swamps with the Stone out: a Swamp goes, Reaper stays.
    const GameState r = State6({ kStone6, kSw6, kSw6, kSw6, kSw6 }, { kReaper6, kSw6, kSw6, kSw6, kSw6, kSw6, kSw6, kSw6 });
    CHECK(First6(r) == kSw6);
    CHECK(Shed6(r).back() == kReaper6);
    // No Stone out: Reaper is a Demon in hand -- overflow, shed first.
    const GameState n = State6({ kSw6, kSw6 }, { kStone6, kReaper6, kSw6, kSw6, kSw6, kSw6, kAp6, kAp6 });
    CHECK(First6(n) == kReaper6);
}

TEST_CASE("Shadowborn INT-6 discard: Demons in hand -- colour-unreachable, then the hand-discarder, then mana value")
{
    const GameState s = State6({ kSw6, kSw6, "Blood Crypt" }, { kOver6, kReaper6, kSire6, kJet6, kStone6, kAp6, kSw6, kSw6 });
    const Names6 o = Shed6(s);
    CHECK(o[0] == kJet6);     // {1}{R}{G}{W}: no G / W source
    CHECK(o[1] == kSire6);    // red reachable (Blood Crypt); discards our hand
    CHECK(o[2] == kOver6);
    CHECK(o[3] == kReaper6);
}
