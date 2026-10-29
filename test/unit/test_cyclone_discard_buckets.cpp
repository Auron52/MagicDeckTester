// Unpredictable Cyclone, THE BUCKETED CLEANUP DISCARD (docs/design/cyclone-discard-buckets.md; the USER's design with
// rulings Q-D1..Q-D6). MTG_CYCLONE_BUCKET_DISCARD (per-job heurarm slot, DEFAULT OFF until the USER adopts it).
//
//   * the sole Cyclone (or Long-Term Plans standing in for it) is never shed while any other card is in hand;
//   * every bucket boundary of the global shed order: dead, Eldrazi beyond two, lands beyond the quota + 2, the second
//     Eldrazi, Remand, the 2 extra lands, surplus cyclers, surplus dig, the first Eldrazi, then the quota cards;
//   * the land keep order (Tarn last), the dig fill order, Wraith over Curator, the second Song dead;
//   * the known-top rule for the graveyard-shuffling Eldrazi (Emrakul, the Infinite Gyre) and the shuffle plan;
//   * the rule is asked again after every shed (CleanupDiscardShedStable false); lever off = the generic ranking.
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

constexpr const char* kCycD  = "Unpredictable Cyclone";
constexpr const char* kCurD  = "Curator of Mysteries";
constexpr const char* kWraD  = "Street Wraith";
constexpr const char* kEmrD  = "Emrakul, the Aeons Torn";
constexpr const char* kHunD  = "Ulamog, the Ceaseless Hunger";
constexpr const char* kGyrD  = "Ulamog, the Infinite Gyre";
constexpr const char* kDelD  = "Toxic Deluge";
constexpr const char* kSongD = "Seething Song";
constexpr const char* kRemD  = "Remand";
constexpr const char* kBstD  = "Brainstorm";
constexpr const char* kBsuD  = "Brainsurge";
constexpr const char* kPonD  = "Ponder";
constexpr const char* kLtpD  = "Long-Term Plans";
constexpr const char* kTarnD = "Scalding Tarn";
constexpr const char* kVenD  = "Steam Vents";
constexpr const char* kMyrD  = "Myriad Landscape";
constexpr const char* kLouD  = "Xander's Lounge";
constexpr const char* kIslD  = "Island";
constexpr const char* kMtnD  = "Mountain";

void EnsureCardsD()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardD(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

const CycloneProvider& ProvD() { static const CycloneProvider cp; return cp; }

struct ArmD
{
    heurarm::Arm saved;
    ArmD() : saved(heurarm::t_arm) {}
    ArmD& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmD() { heurarm::t_arm = saved; }
};

using Names = std::vector<std::string>;

// Round 4, our cleanup. `bf` on our battlefield, `hand` the hand being shed from, `lib` on top of the library.
GameState StateD(const Names& bf, const Names& hand, const Names& lib = {}, int life = 20)
{
    EnsureCardsD();
    GameState s;
    s.m_provider          = &ProvD();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.on_the_play         = true;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    s.deck_has_extra_turn = true;
    s.deck_has_cycle_draw_replacement = true;
    int num = 100;
    for (const std::string& n : bf)
    {
        Permanent p;
        p.card             = CardD(n, num++);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardD(n, num++)); }
    for (const std::string& n : lib)  { s.players[0].library.push_back(CardD(n, num++)); }
    for (int k = 0; k < 10; ++k)      { s.players[0].library.push_back(CardD(kIslD, 900 + k)); }
    return s;
}

// The whole shed order (most expendable first) as names.
Names ShedNames(const GameState& s)
{
    Names out;
    for (int i : ProvD().CleanupDiscardShedOrder(s, nullptr)) { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
    return out;
}
std::string FirstShed(const GameState& s)
{
    const std::vector<int> c = ProvD().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(!c.empty());
    return s.players[0].hand[static_cast<std::size_t>(c.front())].m_name.str();
}
// Position of the FIRST occurrence of `name` in the shed order (-1 if absent).
int PosOf(const Names& order, const std::string& name)
{
    const auto it = std::find(order.begin(), order.end(), name);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin());
}
int LastPosOf(const Names& order, const std::string& name)
{
    for (int i = static_cast<int>(order.size()) - 1; i >= 0; --i) { if (order[static_cast<std::size_t>(i)] == name) { return i; } }
    return -1;
}

}   // namespace

TEST_CASE("Cyclone bucket discard: lever OFF is the generic rule (byte-identical) and sheds the sole Cyclone (Q44(2))")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 0);
    const GameState s = StateD({ kIslD, kIslD }, { kCycD, kIslD, kBstD, kPonD, kRemD, kWraD, kCurD, kMtnD });
    const std::vector<int> got = ProvD().CleanupDiscardCandidates(s, nullptr);
    CHECK(got == CleanupDiscardRanking(s, nullptr));
    CHECK(ProvD().CleanupDiscardShedStable());
    CHECK(FirstShed(s) == kCycD);   // max mana value first, ties to the earlier card: the defect the buckets fix
}

TEST_CASE("Cyclone bucket discard: the sole Cyclone and its LTP stand-in are never shed")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    SUBCASE("the generic rule's victim is kept; ONE candidate; asked again per shed")
    {
        const GameState s = StateD({ kIslD, kIslD }, { kCycD, kIslD, kBstD, kPonD, kRemD, kWraD, kCurD, kMtnD });
        CHECK(ProvD().CleanupDiscardCandidates(s, nullptr).size() == 1);
        CHECK(FirstShed(s) == kRemD);
        CHECK(ShedNames(s).back() == kCycD);
        CHECK_FALSE(ProvD().CleanupDiscardShedStable());
    }
    SUBCASE("an eight-card hand of quota cards still keeps the Cyclone last")
    {
        // Board 0 lands, no Song: land quota 5. Hand: Cyclone, 3 lands, 3 cyclers, Brainstorm -- all within quota.
        const GameState s = StateD({}, { kCycD, kTarnD, kIslD, kMtnD, kWraD, kWraD, kCurD, kBstD });
        const Names o = ShedNames(s);
        CHECK(o.back() == kCycD);
        // Quota shed order: dig slot 2 (none), lands beyond the minimum, the Song (none), cyclers (Curator first),
        // dig slot 1, the minimum land, the engine.
        CHECK(o.front() == kIslD);   // no red on the board: the Mountain is kept over the Island
        CHECK(PosOf(o, kCurD) < PosOf(o, kWraD));
        CHECK(PosOf(o, kWraD) < PosOf(o, kBstD));
        CHECK(PosOf(o, kBstD) < PosOf(o, kTarnD));
    }
    SUBCASE("no Cyclone found: Long-Term Plans stands in and is shed last")
    {
        const GameState s = StateD({ kIslD, kIslD, kMtnD }, { kLtpD, kIslD, kBstD, kPonD, kRemD, kWraD, kCurD, kMtnD });
        CHECK(ShedNames(s).back() == kLtpD);
    }
    SUBCASE("a Cyclone held: LTP is ordinary dig; a Cyclone on the battlefield makes a hand Cyclone DEAD")
    {
        const GameState held = StateD({ kIslD }, { kCycD, kLtpD, kBstD, kPonD, kIslD, kWraD, kCurD, kMtnD });
        CHECK(ShedNames(held).back() == kCycD);
        CHECK(FirstShed(held) == kLtpD);   // dig beyond two (Brainstorm + Ponder kept), shed first here
        const GameState out = StateD({ kCycD, kIslD, kIslD, kMtnD, kMtnD }, { kCycD, kBstD, kPonD, kIslD, kWraD, kCurD, kRemD, kEmrD });
        CHECK(FirstShed(out) == kCycD);
    }
}

TEST_CASE("Cyclone bucket discard: the global shed order, bucket by bucket")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    SUBCASE("dead first: Toxic Deluge, then a second Song")
    {
        const GameState s = StateD({ kIslD, kIslD }, { kSongD, kCycD, kSongD, kDelD, kWraD, kBstD, kIslD, kEmrD });
        const Names o = ShedNames(s);
        CHECK(o[0] == kDelD);
        CHECK(o[1] == kSongD);
        CHECK(LastPosOf(o, kSongD) > PosOf(o, kEmrD));   // the one Song is quota before Cyclone
    }
    SUBCASE("one Emrakul above Remand, the extra lands, spare cyclers and spare dig; shed before any quota card")
    {
        // Board 3 lands (quota 2): Tarn + Island quota, Mountain + Island extra, the 5th land beyond.
        const GameState s = StateD({ kIslD, kIslD, kMtnD },
                                   { kCycD, kEmrD, kRemD, kTarnD, kIslD, kMtnD, kIslD, kIslD, kWraD, kWraD, kCurD, kCurD,
                                     kBstD, kPonD, kPonD });
        const Names o = ShedNames(s);
        const int emr = PosOf(o, kEmrD);
        CHECK(emr > PosOf(o, kRemD));
        CHECK(emr > PosOf(o, kPonD));           // after the surplus Ponder (the first Ponder shed)
        CHECK(emr > PosOf(o, kCurD));           // after the surplus cycler
        // ...and it is the first card shed once only quota cards remain: everything after it is quota.
        CHECK(o[static_cast<std::size_t>(emr + 1)] == kPonD);   // dig slot 2 is the first quota card shed
        CHECK(o.back() == kCycD);
        // Lands beyond the quota + 2 shed BEFORE Remand; the 2 extra lands AFTER it.
        CHECK(PosOf(o, kIslD) < PosOf(o, kRemD));
        CHECK(LastPosOf(o, kIslD) > PosOf(o, kRemD));
        CHECK(PosOf(o, kMtnD) > PosOf(o, kRemD));
        // Surplus cyclers: Curator before Wraith.
        CHECK(PosOf(o, kCurD) < PosOf(o, kWraD));
    }
    SUBCASE("the second Eldrazi: below Remand by default, above it under the variant; a third sheds earlier still")
    {
        const Names hand = { kCycD, kEmrD, kHunD, kGyrD, kRemD, kWraD, kBstD, kIslD };
        const GameState s = StateD({ kIslD, kIslD, kMtnD }, hand);
        const Names o = ShedNames(s);
        CHECK(o[0] == kGyrD);                        // Eldrazi beyond two, unknown top: the Gyre first
        CHECK(PosOf(o, kHunD) < PosOf(o, kRemD));    // the second Eldrazi sheds before Remand
        CHECK(PosOf(o, kEmrD) > PosOf(o, kRemD));    // Emrakul is the kept one
        ArmD v;
        v.Set(heurarm::CYCLONE_DISCARD_ELDRAZI2_OVER_REMAND, 1);
        const Names ov = ShedNames(s);
        CHECK(ov[0] == kGyrD);
        CHECK(PosOf(ov, kHunD) > PosOf(ov, kRemD));
        CHECK(PosOf(ov, kHunD) < PosOf(ov, kEmrD));
    }
    SUBCASE("no Emrakul: the first Eldrazi slot goes to the Ceaseless Hunger over the Gyre")
    {
        const GameState s = StateD({ kIslD, kIslD, kMtnD }, { kCycD, kGyrD, kHunD, kRemD, kWraD, kBstD, kIslD, kPonD });
        const Names o = ShedNames(s);
        CHECK(PosOf(o, kGyrD) < PosOf(o, kHunD));
    }
    SUBCASE("Seething Song: quota before Cyclone, with the extra lands after it")
    {
        const GameState before = StateD({ kIslD, kMtnD }, { kCycD, kSongD, kIslD, kWraD, kBstD, kPonD, kRemD, kEmrD });
        CHECK(PosOf(ShedNames(before), kSongD) > PosOf(ShedNames(before), kEmrD));
        const GameState after = StateD({ kCycD, kIslD, kMtnD, kIslD, kMtnD }, { kSongD, kIslD, kWraD, kBstD, kPonD, kRemD, kEmrD, kCurD });
        const Names o = ShedNames(after);
        CHECK(PosOf(o, kSongD) > PosOf(o, kRemD));
        CHECK(PosOf(o, kSongD) < PosOf(o, kIslD));   // the Song sheds first among the extra lands
        CHECK(PosOf(o, kSongD) < PosOf(o, kEmrD));
    }
    SUBCASE("Street Wraith that cannot pay its life is dead")
    {
        const GameState s = StateD({ kIslD, kIslD }, { kCycD, kWraD, kCurD, kBstD, kIslD, kMtnD, kRemD, kEmrD }, {}, 2);
        CHECK(FirstShed(s) == kWraD);
    }
}

TEST_CASE("Cyclone bucket discard: the land keep order (Tarn is the last land shed) and the dig fill order")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    SUBCASE("lands: second Myriad, then Lounge, then the others, then Vents, the first Myriad and Tarn")
    {
        const GameState s = StateD({}, { kCycD, kMyrD, kLouD, kIslD, kVenD, kTarnD, kMyrD, kMtnD, kWraD });
        Names lands;
        for (const std::string& n : ShedNames(s))
        {
            const CardDefinition* d = CardDatabase::Instance().Lookup(n);
            if (d->card.IsLand()) { lands.push_back(n); }
        }
        REQUIRE(lands.size() == 7);
        CHECK(lands[0] == kMyrD);
        CHECK(lands[1] == kLouD);
        CHECK(lands[4] == kVenD);
        CHECK(lands[5] == kMyrD);
        CHECK(lands[6] == kTarnD);
        // No red on the board: Mountain is kept over Island.
        CHECK(PosOf(lands, kIslD) < PosOf(lands, kMtnD));
    }
    SUBCASE("a Myriad on the battlefield makes the hand Myriad the 'second'")
    {
        const GameState s = StateD({ kMyrD }, { kCycD, kMyrD, kLouD, kIslD, kWraD, kBstD, kPonD, kRemD });
        Names lands;
        for (const std::string& n : ShedNames(s)) { if (CardDatabase::Instance().Lookup(n)->card.IsLand()) { lands.push_back(n); } }
        CHECK(lands.front() == kMyrD);
    }
    SUBCASE("dig: Ponder, Ponder, Brainsurge keeps Ponder + Brainsurge")
    {
        const GameState s = StateD({ kIslD, kIslD, kMtnD }, { kCycD, kPonD, kPonD, kBsuD, kWraD, kIslD, kIslD, kEmrD });
        CHECK(FirstShed(s) == kPonD);
        const Names o = ShedNames(s);
        CHECK(LastPosOf(o, kPonD) < PosOf(o, kBsuD));   // the slot-1 put-back spell sheds after the slot-2 Ponder
    }
    SUBCASE("dig variant after Cyclone: Brainsurge over Ponder for slot 2")
    {
        const Names hand = { kBstD, kPonD, kBsuD, kWraD, kIslD, kRemD, kEmrD, kCurD };
        const GameState s = StateD({ kCycD, kIslD, kIslD, kMtnD, kMtnD }, hand);
        CHECK(PosOf(ShedNames(s), kBsuD) < PosOf(ShedNames(s), kPonD));   // default: Brainstorm + Ponder kept
        ArmD v;
        v.Set(heurarm::CYCLONE_DISCARD_DIG_PUTBACK_FIRST, 1);
        CHECK(PosOf(ShedNames(s), kPonD) < PosOf(ShedNames(s), kBsuD));
        const GameState pre = StateD({ kIslD, kIslD, kMtnD }, { kCycD, kBstD, kPonD, kBsuD, kWraD, kIslD, kRemD, kEmrD });
        CHECK(PosOf(ShedNames(pre), kBsuD) < PosOf(ShedNames(pre), kPonD));   // the variant applies only after Cyclone
    }
}

TEST_CASE("Cyclone bucket discard: the known-top rule for the shuffling Eldrazi, and the shuffle plan")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    auto stack_top = [](GameState& s)
    {
        s.top_stacked_turn        = s.TurnSeq();
        s.top_stacked_card_number = s.players[0].library.front().m_number;
    };
    const Names hand = { kCycD, kEmrD, kEmrD, kEmrD, kHunD, kRemD, kWraD, kBstD, kIslD };
    SUBCASE("unknown top: among the Eldrazi beyond two, the Hunger before a surplus Emrakul")
    {
        const GameState s = StateD({ kIslD, kIslD, kMtnD }, hand, { kIslD });
        CHECK(CycloneKnownTopOf(s, 0) == CycloneKnownTop::Unknown);
        CHECK(FirstShed(s) == kHunD);
    }
    SUBCASE("a known JUNK top: a surplus Emrakul is a free shuffle, ahead of the Hunger")
    {
        GameState s = StateD({ kIslD, kIslD, kMtnD }, hand, { kDelD });
        stack_top(s);
        CHECK(CycloneKnownTopOf(s, 0) == CycloneKnownTop::Junk);
        CHECK(FirstShed(s) == kEmrD);
    }
    SUBCASE("a known SETUP top: the shuffling Eldrazi shed after every non-shuffle card below Remand")
    {
        const Names h2 = { kCycD, kEmrD, kHunD, kGyrD, kRemD, kWraD, kBstD, kIslD };
        GameState s = StateD({ kIslD, kIslD, kMtnD }, h2, { kWraD });
        stack_top(s);
        CHECK(CycloneKnownTopOf(s, 0) == CycloneKnownTop::Setup);
        const Names o = ShedNames(s);
        CHECK(o[0] == kHunD);                        // the Hunger does not shuffle: it goes first
        CHECK(PosOf(o, kGyrD) < PosOf(o, kRemD));    // the Gyre still before Remand...
        CHECK(PosOf(o, kGyrD) > PosOf(o, kHunD));    // ...but after the non-shuffle second Eldrazi
        // Unknown top: the Gyre first.
        s.top_stacked_turn = -1;
        CHECK(ShedNames(s)[0] == kGyrD);
    }
    SUBCASE("a consumed stack is no longer known; a stale turn stamp neither")
    {
        GameState s = StateD({ kIslD }, hand, { kWraD });
        stack_top(s);
        s.players[0].library.erase(s.players[0].library.begin());
        CHECK(CycloneKnownTopOf(s, 0) == CycloneKnownTop::Unknown);
        GameState t = StateD({ kIslD }, hand, { kWraD });
        stack_top(t);
        t.top_stacked_turn -= 1;
        CHECK(CycloneKnownTopOf(t, 0) == CycloneKnownTop::Unknown);
    }
    SUBCASE("MTG_CYCLONE_DISCARD_SHUFFLE_PLAN: two candidates of different kinds, only with the top known")
    {
        ArmD v;
        v.Set(heurarm::CYCLONE_DISCARD_SHUFFLE_PLAN, 1);
        GameState s = StateD({ kIslD, kIslD, kMtnD }, hand, { kDelD });
        CHECK(ProvD().CleanupDiscardCandidates(s, nullptr).size() == 1);   // unknown: the rule alone
        stack_top(s);
        const std::vector<int> c = ProvD().CleanupDiscardCandidates(s, nullptr);
        REQUIRE(c.size() == 2);
        CHECK(s.players[0].hand[static_cast<std::size_t>(c[0])].m_name.str() == kEmrD);   // the rule: a free shuffle
        CHECK(s.players[0].hand[static_cast<std::size_t>(c[1])].m_name.str() == kHunD);   // the best non-shuffle shed
    }
}

TEST_CASE("Cyclone bucket discard: the rule is asked again after every shed")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    // Ten cards, two to shed. Before Cyclone with the Song kept the land quota is 5 - 2 - 2 = 1, so Tarn is quota,
    // Island + Mountain extra, the last Island beyond.
    const Names hand = { kCycD, kSongD, kTarnD, kIslD, kMtnD, kIslD, kEmrD, kHunD, kRemD, kWraD };
    GameState s = StateD({ kIslD, kMtnD }, hand);
    // The reference: ask, shed, ask again.
    GameState ref = s;
    Names expect;
    for (int k = 0; k < 2; ++k)
    {
        const std::vector<int> c = ProvD().CleanupDiscardCandidates(ref, nullptr);
        REQUIRE(!c.empty());
        expect.push_back(ref.players[0].hand[static_cast<std::size_t>(c.front())].m_name.str());
        ref.players[0].hand.erase(ref.players[0].hand.begin() + c.front());
    }
    bool consulted = false;
    const std::size_t gy0 = s.players[0].graveyard.size();
    CHECK(CleanupDiscardShed(s, nullptr, 2, -1, false, true, &consulted) == 2);
    CHECK(consulted);
    Names got;
    for (std::size_t i = gy0; i < s.players[0].graveyard.size(); ++i) { got.push_back(s.players[0].graveyard[i].m_name.str()); }
    CHECK(got == expect);
    CHECK(expect[0] == kIslD);   // the land beyond the quota + 2
    CHECK(expect[1] == kHunD);   // the second Eldrazi, below Remand
}

TEST_CASE("Cyclone bucket discard: MTG_CYCLONE_BUCKET_KEEPNEED puts the scored put-back on the same model")
{
    ArmD arm;
    arm.Set(heurarm::CYCLONE_BUCKET_DISCARD, 1);
    // Board 4 lands, Cyclone held (quota 1): Island quota, Mountain + Island extra; Remand; one Emrakul.
    const GameState s = StateD({ kIslD, kIslD, kMtnD, kMtnD }, { kCycD, kRemD, kIslD, kMtnD, kIslD, kEmrD, kWraD });
    CantripReadiness off, on;
    REQUIRE(ProvD().CantripReadinessOf(s, 0, off));
    auto need_of = [&](const CantripReadiness& r, const char* nm, bool last)
    {
        const auto& h = s.players[0].hand;
        int idx = -1;
        for (int i = 0; i < static_cast<int>(h.size()); ++i) { if (h[static_cast<std::size_t>(i)].m_name == nm) { idx = i; if (!last) { break; } } }
        return r.keep_need[static_cast<std::size_t>(idx)];
    };
    // The kCyc tiers: Remand (30) above a surplus land (20), Emrakul lowest of the live cards (10).
    CHECK(need_of(off, kRemD, false) > need_of(off, kIslD, true));
    CHECK(need_of(off, kEmrD, false) < need_of(off, kRemD, false));
    ArmD v;
    v.Set(heurarm::CYCLONE_BUCKET_KEEPNEED, 1);
    REQUIRE(ProvD().CantripReadinessOf(s, 0, on));
    // The bucket model: the extra land above Remand, the one Emrakul above both, the Cyclone the engine.
    CHECK(need_of(on, kMtnD, false) > need_of(on, kRemD, false));
    CHECK(need_of(on, kEmrD, false) > need_of(on, kMtnD, false));
    CHECK(need_of(on, kCycD, false) == 1000);
}
