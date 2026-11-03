// Shadowborn Apostle onboarding, integrator INT-8 (2026-11-03; docs/design/analysis-Shadowborn Apostle.md,
// brief logs/shadowborn/int8-brief.md):
//   (A -- deleting the ripple late / deep waves -- was MEASURED too wide and is not shipped; see the ledger.)
//   B. 4a: only a tutor WITHOUT a creature-sacrifice cost (Demonic / Profane, not Diabolic Intent) fills the discard
//      Apostle #1 slot;
//   D. the 2d review's finding 7: the ritual chain credit follows the copies the plan casts (first copy = its own
//      chain, continue copies = the max increment), and an ORDER walk credits the chain share only after a ripple
//      granter is out;
//   E. EARLIEST TURN TO THE BAR (ShadowbornEarliestTurnToBar): the USER's two examples verbatim, Lotus Bloom by its
//      deploy turn, a granter-out bar; the discard buckets keep the fastest route and pitch off-route mana; the
//      tutor mana tier ranks candidates by the turn they reach the bar.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kAp8     = "Shadowborn Apostle";
const char* const kStone8  = "Thrumming Stone";
const char* const kIntent8 = "Diabolic Intent";
const char* const kDT8     = "Demonic Tutor";
const char* const kPT8     = "Profane Tutor";
const char* const kJet8    = "Jetmir, Nexus of Revels";
const char* const kOver8   = "Abhorrent Overlord";
const char* const kSire8   = "Sire of Insanity";
const char* const kLake8   = "Lake of the Dead";
const char* const kRit8    = "Dark Ritual";
const char* const kBloom8  = "Lotus Bloom";
const char* const kSw8     = "Swamp";

using Names8 = std::vector<std::string>;
using Route8 = ShadowbornProvider::ManaRoute;

const CardDefinition& Def8(const std::string& n)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card Card8(const std::string& name, int number)
{
    Card c     = Def8(name).card;
    c.m_number = number;
    return c;
}
Permanent Perm8(const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = Card8(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    return p;
}
const ShadowbornProvider& Prov8() { static const ShadowbornProvider p; return p; }

// Our main phase 1 (land drop unused unless `drop_used`): `bf` on our battlefield, `hand` in hand.
GameState Main8(const Names8& bf, const Names8& hand, bool drop_used = false)
{
    GameState s;
    s.m_provider          = &Prov8();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = drop_used ? 1 : 0;
    int num = 100;
    for (const std::string& n : bf) { s.battlefield.push_back(Perm8(n, num++)); }
    for (const std::string& n : hand) { s.players[0].hand.push_back(Card8(n, num++)); }
    return s;
}
// Our cleanup: as Main8, the land drop spent, a library of Apostles + Swamps.
GameState Cleanup8(const Names8& bf, const Names8& hand)
{
    GameState s = Main8(bf, hand, /*drop_used=*/true);
    s.phase = Phase::Ending;
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(Card8(kAp8, 900 + k)); }
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(Card8(kSw8, 950 + k)); }
    return s;
}
Names8 RouteNames8(const GameState& s, const Route8& r)
{
    Names8 out;
    for (int i : r.route) { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
    std::sort(out.begin(), out.end());
    return out;
}
Names8 Shed8(const GameState& s)
{
    Names8 out;
    for (int i : Prov8().CleanupDiscardShedOrder(s, nullptr))
    { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
    return out;
}
std::string First8(const GameState& s)
{
    const std::vector<int> c = Prov8().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(c.size() == 1);
    return s.players[0].hand[static_cast<std::size_t>(c.front())].m_name.str();
}
int Pos8(const Names8& o, const std::string& n)
{ const auto it = std::find(o.begin(), o.end(), n); return it == o.end() ? -1 : static_cast<int>(it - o.begin()); }
int LastPos8(const Names8& o, const std::string& n)
{
    for (int i = static_cast<int>(o.size()) - 1; i >= 0; --i) { if (o[static_cast<std::size_t>(i)] == n) { return i; } }
    return -1;
}
}   // namespace

// ---- E. the helper -----------------------------------------------------------------------------------------------
TEST_CASE("INT-8 earliest turn: USER example 1 -- Lake of the Dead + 2 Swamps in hand, nothing on the field -> keep the Lake")
{
    // USER: "if you had Lake of the Dead, 2 swamps in hand and nothing on the field, you would have enough to go off in
    // 3 turns. So, Lake of the Dead would be kept as long as we didn't have a faster route."
    const GameState s = Main8({}, { kLake8, kSw8, kSw8 });
    const Route8 r = ShadowbornProvider::ShadowbornEarliestTurnToBar(s, 0, ShadowbornProvider::kManaGoal);
    CHECK(r.turn == 2);   // Swamp, Swamp, then Lake (sacrificing one) + crack with the other: 2 + 4 = 6 on the 3rd turn
    CHECK(RouteNames8(s, r) == Names8{ kLake8, kSw8, kSw8 });   // the Lake is ON the route
    // Without the Lake: two lands never make six.
    const GameState n = Main8({}, { kSw8, kSw8 });
    CHECK(ShadowbornProvider::ShadowbornEarliestTurnToBar(n, 0, ShadowbornProvider::kManaGoal).turn
          == ShadowbornProvider::kManaRouteNever);
}

TEST_CASE("INT-8 earliest turn: USER example 2 -- 2 Swamps + 2 Dark Ritual + Lake -> the Lake is an easy pitch")
{
    // USER: "If we had 2 swamps and 2 dark ritual, though, the lake would be an easy pitch."
    const GameState s = Main8({}, { kLake8, kSw8, kSw8, kRit8, kRit8 });
    const Route8 r = ShadowbornProvider::ShadowbornEarliestTurnToBar(s, 0, ShadowbornProvider::kManaGoal);
    CHECK(r.turn == 1);   // two lands + two Rituals (+2 each) = 6 next turn
    CHECK(RouteNames8(s, r) == Names8{ kRit8, kRit8, kSw8, kSw8 });
    CHECK_FALSE(r.On(0));   // the Lake (hand slot 0) is OFF the route
}

TEST_CASE("INT-8 earliest turn: Lotus Bloom is valued by its deploy turn (USER: 'Same deal with Lotus Bloom')")
{
    // Three Swamps out, Bloom in hand: suspend now, it arrives three upkeeps later -> 3 + 3 = 6 on turn 3.
    const GameState s = Main8({ kSw8, kSw8, kSw8 }, { kBloom8 }, /*drop_used=*/true);
    const Route8 r = ShadowbornProvider::ShadowbornEarliestTurnToBar(s, 0, ShadowbornProvider::kManaGoal);
    CHECK(r.turn == 3);
    CHECK(r.On(0));
    // A faster route (a Swamp + a Dark Ritual: 4 + 2 next turn) takes the Bloom off it.
    const GameState f = Main8({ kSw8, kSw8, kSw8 }, { kBloom8, kSw8, kRit8 }, /*drop_used=*/true);
    const Route8 rf = ShadowbornProvider::ShadowbornEarliestTurnToBar(f, 0, ShadowbornProvider::kManaGoal);
    CHECK(rf.turn == 1);
    CHECK_FALSE(rf.On(0));
    CHECK(RouteNames8(f, rf) == Names8{ kRit8, kSw8 });
    // An already-suspended Bloom arriving next upkeep counts next turn.
    GameState a = Main8({ kSw8, kSw8, kSw8 }, {}, /*drop_used=*/true);
    SuspendedCard sc;
    sc.card        = Card8(kBloom8, 77);
    sc.arrive_turn = a.TurnSeq() + 1;
    a.players[0].suspended_cards.push_back(sc);
    CHECK(ShadowbornProvider::ShadowbornEarliestTurnToBar(a, 0, ShadowbornProvider::kManaGoal).turn == 1);
}

TEST_CASE("INT-8 earliest turn: a granter on the battlefield lowers the bar; tapped lands and the spent drop count this turn")
{
    // Stone + 2 untapped Swamps, a Dark Ritual in hand: 2 + 2 = 4 >= 3 this turn.
    const GameState g = Main8({ kStone8, kSw8, kSw8 }, { kRit8, kLake8 }, /*drop_used=*/true);
    const Route8 r = ShadowbornProvider::ShadowbornEarliestTurnToBar(g, 0, ShadowbornProvider::kManaGoalRipple);
    CHECK(r.turn == 0);
    CHECK(RouteNames8(g, r) == Names8{ kRit8 });
    // The same two Swamps TAPPED: nothing this turn (the Ritual needs a mana first); next turn 2 + 2.
    GameState t = g;
    for (Permanent& p : t.battlefield) { if (p.card.IsLand()) { p.tapped = true; } }
    CHECK(ShadowbornProvider::ShadowbornEarliestTurnToBar(t, 0, ShadowbornProvider::kManaGoalRipple).turn == 1);
    // From the cleanup (start_next_turn) the first turn counted is next turn.
    CHECK(ShadowbornProvider::ShadowbornEarliestTurnToBar(g, 0, ShadowbornProvider::kManaGoalRipple, true).turn == 1);
}

// ---- E + B. the discard buckets ------------------------------------------------------------------------------------
TEST_CASE("INT-8 discard: USER example 1 at the cleanup -- the Lake fuelled by HAND Swamps is kept")
{
    // Empty board; Lake + 2 Swamps is the only route to six (turn 3). INT-6 shed the Lake first (no Swamps OUT).
    const GameState s = Cleanup8({}, { kLake8, kSw8, kSw8, kStone8, kAp8, kAp8, kAp8, kAp8 });
    CHECK(First8(s) == kAp8);   // an extra Apostle goes, not the Lake
    const Names8 o = Shed8(s);
    // Kept: the Stone, both Swamps (the floor), Apostle #1, the Lake (on the route), Apostle #2; two extra Apostles overflow.
    CHECK(o == Names8{ kAp8, kAp8, kAp8, kLake8, kAp8, kSw8, kSw8, kStone8 });
}

TEST_CASE("INT-8 discard: USER example 2 at the cleanup -- 2 Swamps + 2 Dark Ritual make the Lake the pitch")
{
    const GameState s = Cleanup8({}, { kLake8, kSw8, kSw8, kRit8, kRit8, kStone8, kAp8, kAp8 });
    CHECK(First8(s) == kLake8);
    const Names8 o = Shed8(s);
    CHECK(Pos8(o, kRit8) > Pos8(o, kLake8));
}

TEST_CASE("INT-8 discard: Lotus Bloom off a faster route is overflow; the only route keeps it")
{
    // 3 Swamps out + a Dark Ritual + a Swamp in hand reach six next turn: the Bloom (turn 4 from the cleanup) is off.
    const GameState f = Cleanup8({ kSw8, kSw8, kSw8 }, { kBloom8, kRit8, kSw8, kStone8, kAp8, kAp8, kAp8, kSw8 });
    const Names8 of = Shed8(f);
    CHECK(Pos8(of, kBloom8) < Pos8(of, kRit8));
    CHECK(Pos8(of, kBloom8) < Pos8(of, kStone8));
    // No faster route: 3 Swamps out, Bloom + no other mana -> the Bloom is the route (3 + 3 on turn 4) and is kept.
    const GameState k = Cleanup8({ kSw8, kSw8, kSw8 }, { kBloom8, kStone8, kAp8, kAp8, kJet8, kOver8, kSire8, kAp8 });
    const Names8 ok = Shed8(k);
    CHECK(Pos8(ok, kBloom8) > Pos8(ok, kSire8));
    CHECK(Pos8(ok, kBloom8) > Pos8(ok, kOver8));
    CHECK(First8(k) != kBloom8);
}

TEST_CASE("INT-8 discard 4a: Demonic / Profane Tutor may hold the Apostle #1 slot, Diabolic Intent may not")
{
    // Stone out (no Stone slot), an Apostle out (fodder for an Intent), no Apostle in hand. 3 lands out + 1 drop = the
    // granter-out quota of 4 next turn, so one hand Swamp is the route and the rest are surplus.
    const Names8 bf = { kStone8, kAp8, kSw8, kSw8, kSw8 };
    const GameState i = Cleanup8(bf, { kIntent8, kSw8, kSw8, kSw8, kSw8, kSw8, kSw8, kJet8 });
    const Names8 oi = Shed8(i);
    CHECK(Pos8(oi, kIntent8) < LastPos8(oi, kSw8));   // NOT kept for the slot: shed before the route Swamp
    const GameState d = Cleanup8(bf, { kDT8, kSw8, kSw8, kSw8, kSw8, kSw8, kSw8, kJet8 });
    CHECK(Shed8(d).back() == kDT8);                   // kept in the slot (filled before the route land)
    const GameState p = Cleanup8(bf, { kPT8, kSw8, kSw8, kSw8, kSw8, kSw8, kSw8, kJet8 });
    CHECK(Shed8(p).back() == kPT8);
    // Demonic over Profane for the slot.
    const GameState b = Cleanup8(bf, { kPT8, kDT8, kSw8, kSw8, kSw8, kSw8, kSw8, kJet8 });
    CHECK(Shed8(b).back() == kDT8);
}

// ---- E. the tutor tiers ----------------------------------------------------------------------------------------------
TEST_CASE("INT-8 tutor: mana candidates rank by the turn they reach the bar (Dark Ritual ahead of a fuelled Lake when sooner)")
{
    // No granter out (a Stone in hand), 4 untapped Swamps, the drop spent, nothing else: 4 now, 4 next turn -> short.
    GameState s = Main8({ kSw8, kSw8, kSw8, kSw8 }, { kStone8 }, /*drop_used=*/true);
    for (const char* n : { kLake8, kRit8, kAp8, kSw8, kSw8 }) { s.players[0].library.push_back(Card8(n, 500 + static_cast<int>(s.players[0].library.size()))); }
    const std::vector<std::string> c = Prov8().TutorCandidates(s, 0, Def8(kDT8).params);
    // Dark Ritual reaches 6 THIS turn (4 + 2); the Lake only next turn (drop + crack). INT-5 put the Lake first.
    REQUIRE(c.size() >= 3);
    CHECK(c[0] == kRit8);
    CHECK(c[1] == kLake8);
    CHECK(c[2] == kAp8);
    // A turn with the drop still open: the Lake reaches 4 + 1 + 3 = 8 THIS turn too -> tie, Lake first (precedence).
    GameState t = s;
    t.players[0].lands_played_this_turn = 0;
    const std::vector<std::string> ct = Prov8().TutorCandidates(t, 0, Def8(kDT8).params);
    REQUIRE(ct.size() >= 2);
    CHECK(ct[0] == kLake8);
    CHECK(ct[1] == kRit8);
}

// ---- D. the ritual chain credit ------------------------------------------------------------------------------------
TEST_CASE("INT-8 ritual chain credit: the first copy carries its own chain, a continue copy the increment")
{
    // Stone out, two Dark Rituals in hand. The first copy's ripple reveals four Swamps (no free Ritual); the SECOND
    // copy's reveals a Dark Ritual. INT-3 credited the whole two-copy chain (3) on the first copy, so a plan casting
    // ONE Ritual was credited mana its chain never makes.
    GameState s = Main8({ kStone8, kSw8, kSw8 }, { kRit8, kRit8 }, /*drop_used=*/true);
    for (int k = 0; k < 4; ++k) { s.players[0].library.push_back(Card8(kSw8, 500 + k)); }
    s.players[0].library.push_back(Card8(kRit8, 510));
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(Card8(kSw8, 520 + k)); }
    const int per = Def8(kRit8).params.ritual_floating_mana;
    int first_all = -1, cont = -1;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kRit8 || !a.ripple_pinned) { continue; }
        if (a.ripple_reveals == kRippleContinue) { cont = a.ripple_chain_float; CHECK(a.ritual_float == per + a.ripple_chain_float); continue; }
        if (a.ripple_reveals == kRippleRevealAll && (a.ripple_decline & 0x0F) == 0) { first_all = a.ripple_chain_float; }
    }
    CHECK(first_all == 0);    // its own chain reveals Swamps only
    CHECK(cont == per);       // the second copy's chain casts one free Ritual
}

TEST_CASE("INT-8 ritual chain credit: an ORDER walk credits the chain share only after the granter is cast")
{
    // Three Swamps; a plan of Thrumming Stone ({5}), Dark Ritual (gross 3 + a chain share of 3) and two Apostles, in
    // the deck's order (Stone first: unpayable off three lands). Ritual-first "pays" only on the chain's mana
    // (3 - 1 + 6 = 8: Stone 5, Apostles 2), and a Ritual cast BEFORE the Stone ripples nothing -- so the payable-order
    // rescue must not re-sequence onto that phantom (3 - 1 + 3 = 5: the Stone, then no mana for the Apostles).
    const GameState s = Main8({ kSw8, kSw8, kSw8 }, { kRit8, kStone8, kAp8, kAp8 }, /*drop_used=*/true);
    auto cast = [&](const std::string& n, int hand_index)
    {
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = InternedName(n);
        a.def        = &Def8(n);
        a.cost       = Def8(n).card.m_mana_cost;
        a.hand_index = hand_index;
        return a;
    };
    std::vector<Action> acts{ cast(kRit8, 0), cast(kStone8, 1), cast(kAp8, 2), cast(kAp8, 3) };
    acts[0].ritual_float       = 3 + 3;
    acts[0].ripple_chain_float = 3;
    acts[0].ripple_pinned      = true;
    acts[0].ripple_reveals     = kRippleRevealAll;
    std::vector<int> order{ 1, 0, 2, 3 };
    ApplyPayableCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 1, 0, 2, 3 });   // no order really pays: left as the deck's order
    // The INT-7 reading (the whole 6 credited in any order) rescued Ritual-first on the phantom chain.
    std::vector<Action> phantom = acts;
    phantom[0].ripple_chain_float = 0;
    std::vector<int> po{ 1, 0, 2, 3 };
    ApplyPayableCastOrder(s, phantom, po);
    CHECK(std::find(po.begin(), po.end(), 0) < std::find(po.begin(), po.end(), 1));
    // With the Stone ALREADY out the chain is live from the first cast. Stone + one Swamp; Apostle, Ritual, three
    // Apostles in hand order: Apostle-first leaves nothing for the Ritual, and Ritual-first pays all four Apostles
    // ONLY with the live chain (1 - 1 + 6 = 6; without the chain share 3 < 4) -- so the rescue takes it.
    const GameState o = Main8({ kStone8, kSw8 }, { kRit8, kAp8, kAp8, kAp8, kAp8 }, /*drop_used=*/true);
    std::vector<Action> acts2{ cast(kAp8, 1), cast(kRit8, 0), cast(kAp8, 2), cast(kAp8, 3), cast(kAp8, 4) };
    acts2[1].ritual_float       = 6;
    acts2[1].ripple_chain_float = 3;
    std::vector<int> order2{ 0, 1, 2, 3, 4 };
    ApplyPayableCastOrder(o, acts2, order2);
    CHECK(order2.front() == 1);   // Ritual first: 1 - 1 + 6 = 6 pays four Apostles only because the Stone is out
}
