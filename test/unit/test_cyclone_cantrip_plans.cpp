// Unpredictable Cyclone, THE PLAN-LEVEL CANTRIP HARD PRUNES (USER design 2026-10-28; ledger "Hard prunes + known-top
// blind test"). USER: "search options that are vastly different when needed ... putting junk on top when we plan to
// throw away those cards vs setting up a big creature to cycle into, but the heuristic would choose how to fulfil those
// options. Similarly Ponder shuffle vs reorder, but the heuristic would choose the order of the reorder. Naturally, even
// for these we may be able to prune the second or third plan depending on the situation."
//
//   * MTG_CYCLONE_PB_PLANS: the Brainstorm / Brainsurge fan = the scored order's best SETUP pick (an Eldrazi back) and
//     its best JUNK pick (no Eldrazi back). Rank 0 is unchanged (the scorer's overall best). Width <= 2 in both the
//     resolution list and the search fan (CantripFanCap).
//   * MTG_CYCLONE_PONDER_PLANS: Ponder = the best KEEP order and the SHUFFLE (the scored fan's ranks 0-1).
//   * MTG_CYCLONE_CANTRIP_SITUATIONAL: no Cyclone available (hand, battlefield, a KNOWN library position, Ponder's look)
//     -> JUNK only / SHUFFLE only; ready to go off (k <= 1, a cycle, an Eldrazi, a Cyclone) -> SETUP only.
//   * All three DEFAULT OFF: with them off the fan is the full scored order (the ORDER test file pins it).
//   * The fast path and the reference enumeration agree rank for rank with the levers on.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr const char* kCycP   = "Unpredictable Cyclone";
constexpr const char* kWraP   = "Street Wraith";
constexpr const char* kEmrP   = "Emrakul, the Aeons Torn";
constexpr const char* kHunP   = "Ulamog, the Ceaseless Hunger";
constexpr const char* kDelP   = "Toxic Deluge";
constexpr const char* kTarnP  = "Scalding Tarn";
constexpr const char* kVentsP = "Steam Vents";
constexpr const char* kPonderP = "Ponder";

void EnsureCardsP()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardP(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

const CycloneProvider& ProvP() { static const CycloneProvider cp; return cp; }

struct ArmP
{
    heurarm::Arm saved;
    ArmP() : saved(heurarm::t_arm) {}
    ArmP& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmP() { heurarm::t_arm = saved; }
};

GameState StateP(const std::vector<std::string>& bf, const std::vector<std::string>& hand,
                 const std::vector<std::string>& lib = {}, int land_drops_used = 1)
{
    EnsureCardsP();
    GameState s;
    s.m_provider          = &ProvP();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = land_drops_used;
    s.deck_has_extra_turn = true;
    s.deck_has_cycle_draw_replacement = true;
    int num = 100;
    for (const std::string& n : bf)
    {
        Permanent p;
        p.card             = CardP(n, num++);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardP(n, num++)); }
    std::vector<Card> l;
    for (const std::string& n : lib) { l.push_back(CardP(n, num++)); }
    for (int k = 0; k < 12; ++k) { l.push_back(CardP("Island", 900 + k)); }
    s.players[0].library.assign(l.begin(), l.end());
    return s;
}

using NamesP = std::vector<std::string>;
NamesP PickP(const GameState& s, int r)
{
    const PutBackPick pk = PutBackPickAt(s, 0, 2, r);
    NamesP out;
    for (int j = 0; j < pk.take; ++j) { out.push_back(s.players[0].hand[static_cast<std::size_t>(pk.idx[j])].m_name.str()); }
    return out;
}
std::vector<NamesP> FanP(const GameState& s)
{
    std::vector<NamesP> out;
    const int n = PutBackPickAt(s, 0, 2, 0).count;
    for (int r = 0; r < n; ++r) { out.push_back(PickP(s, r)); }
    return out;
}
bool HasEldrazi(const NamesP& p)
{ return std::find(p.begin(), p.end(), std::string(kEmrP)) != p.end() || std::find(p.begin(), p.end(), std::string(kHunP)) != p.end(); }

std::vector<Card> LookP(const GameState& s, int n)
{
    std::vector<Card> out;
    for (int i = 0; i < n; ++i) { out.push_back(s.players[0].library.RawAt(static_cast<std::size_t>(i))); }
    return out;
}
struct DrawAfterP
{
    int saved;
    explicit DrawAfterP(int d) : saved(g_reorder_draw_after) { g_reorder_draw_after = d; }
    ~DrawAfterP() { g_reorder_draw_after = saved; }
};
}   // namespace

TEST_CASE("Cyclone cantrip plans: the put-back fan is the JUNK pick and, when a walk can reach it, the SETUP pick")
{
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    SUBCASE("go-off 2+ draws away: SETUP does not exist -> ONE pick, the two least kept, the MORE useful on top")
    {
        // Cyclone in hand, 3 lands, the drop used (k = 2). Kept: the Cyclone (tier 1), one land for next turn's drop
        // (tier 2), the Wraith (tier 4); the rest by the shared rank -- Deluge (0) and Emrakul (10, which cannot be used
        // before its redraw: a sixteenth). Emrakul is the more useful: on top; Deluge second.
        const GameState s = StateP({ kVentsP, "Island", "Island" }, { kCycP, kWraP, kEmrP, kDelP, "Island", "Mountain" });
        std::vector<NamesP> full;
        {
            ArmP off;
            off.Set(heurarm::CYCLONE_PB_PLANS, 0).Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 0);
            full = FanP(s);
            CHECK(ProvP().CantripFanCap(s, true) == -1);
        }
        REQUIRE(full.size() > 10);
        ArmP on;
        on.Set(heurarm::CYCLONE_PB_PLANS, 1);
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(fan.size() == 1);
        CHECK(fan[0] == NamesP{ kEmrP, kDelP });
        CHECK(ProvP().CantripFanCap(s, true) == 2);
        const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, 2);
        REQUIRE(ref.size() == 1);
        CHECK(PickP(s, 5) == fan[0]);   // a rank past the admitted list clamps onto it
    }
    SUBCASE("go-off this turn: TWO plans -- SETUP (the scorer's best Eldrazi pick) and JUNK; rank 0 the scorer's better")
    {
        const GameState s = StateP({ kCycP, "Island", "Island", "Island" }, { kWraP, kEmrP, kHunP, kDelP, "Island" });
        NamesP best_overall;
        {
            ArmP off;
            off.Set(heurarm::CYCLONE_PB_PLANS, 0);
            best_overall = FanP(s)[0];
        }
        ArmP on;
        on.Set(heurarm::CYCLONE_PB_PLANS, 1);
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(fan.size() == 2);
        CHECK(fan[0] == best_overall);
        CHECK(std::find(fan.begin(), fan.end(), NamesP{ kDelP, kEmrP }) != fan.end());   // the SETUP fulfilment
        // The reference enumeration agrees rank for rank.
        const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, 2);
        REQUIRE(ref.size() == fan.size());
        for (std::size_t r = 0; r < ref.size(); ++r)
        {
            NamesP rn;
            for (int hi : ref[r]) { rn.push_back(s.players[0].hand[static_cast<std::size_t>(hi)].m_name.str()); }
            CHECK(rn == fan[r]);
        }
    }
    SUBCASE("the core keep set keeps a cantrip and a cycler; the plain-rank handle does not")
    {
        // k >= 2 (no Cyclone: a missing piece). Hand: Ponder, Street Wraith, Curator, Remand, Island. The shared rank
        // alone puts back Remand (30) and Ponder (60); the core (1 cycler, 2 lands, 2 cantrips) keeps Ponder and sends the second cycler back.
        // (The land drop open: the Island in hand is usable before it would be redrawn, so it keeps its full rank.)
        const GameState s = StateP({ "Island", "Island" }, { kPonderP, kWraP, "Curator of Mysteries", "Remand", "Island" }, {}, 0);
        ArmP on;
        on.Set(heurarm::CYCLONE_PB_PLANS, 1);
        const NamesP pri = FanP(s)[0];
        CHECK(std::find(pri.begin(), pri.end(), std::string(kPonderP)) == pri.end());
        ArmP plain;
        plain.Set(heurarm::CYCLONE_PB_PLAIN_RANK, 1);
        const NamesP pl = FanP(s)[0];
        CHECK(std::find(pl.begin(), pl.end(), std::string(kPonderP)) != pl.end());
        CHECK(pl != pri);
    }
    SUBCASE("TWO Eldrazi in JUNK with no go-off this turn: the lesser on top, Emrakul second (d3 gi92)")
    {
        // d3 gi92's turn-1 Brainstorm on the play: Steam Vents out (the drop used), no Cyclone (k = 3). The two least kept
        // are the Eldrazi. The top card is redrawn next turn, so Emrakul -- the walk's best target -- goes SECOND
        // (isolated T1 test: Ulamog on top -0.44 se 0.22 against Emrakul first).
        const GameState s = StateP({ kVentsP }, { "Remand", "Ulamog, the Infinite Gyre", kEmrP, kTarnP, "Xander's Lounge",
                                                  "Curator of Mysteries", kTarnP, "Brainsurge" });
        ArmP on;
        on.Set(heurarm::CYCLONE_PB_PLANS, 1);
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(fan.size() == 1);
        CHECK(fan[0] == NamesP{ "Ulamog, the Infinite Gyre", kEmrP });
    }
    SUBCASE("MTG_CYCLONE_KEEP_AMEND (+ KEEP_MYRIAD), d5 gi179: the spare plain land and Emrakul go back, Myriad stays")
    {
        // d5 gi179's turn-1 Brainstorm: one Island out (the drop used), no Cyclone, Brainsurge held. Off: JUNK =
        // [Myriad, Emrakul] (Myriad the enters-tapped 5th land, surplus 20 over the Eldrazi's 10). The amended priority
        // with the ramp Myriad: Myriad (play T2, crack T3: the Cyclone's 5 mana a turn earlier) kept above a plain land;
        // the need (4) = Tarn, Bluffs, Mountain (red: short on red), Island -- the LAST, a plain land, demoted (the draws
        // before go-off bring a land); Emrakul above the surplus lands (Brainsurge held). JUNK = Island + Emrakul; the
        // ORDER is the go-off rule (no go-off this turn: the Eldrazi second): Island on top.
        const GameState s = StateP({ "Island" }, { kPonderP, "Cascade Bluffs", "Island", "Myriad Landscape", "Mountain",
                                                   kEmrP, "Brainsurge", "Curator of Mysteries", kTarnP });
        ArmP on;
        on.Set(heurarm::CYCLONE_PB_PLANS, 1);
        {
            const std::vector<NamesP> off = FanP(s);
            REQUIRE(off.size() == 1);
            CHECK(off[0] == NamesP{ "Myriad Landscape", kEmrP });
        }
        {
            ArmP am;
            am.Set(heurarm::CYCLONE_KEEP_AMEND, 1).Set(heurarm::CYCLONE_KEEP_MYRIAD, 1);
            const std::vector<NamesP> fan = FanP(s);
            REQUIRE(fan.size() == 1);
            CHECK(fan[0] == NamesP{ "Island", kEmrP });
            CHECK(ProvP().CantripFanCap(s, true) == 2);
        }
        {
            // The amendment without the Myriad sub-lever: Myriad is the enters-tapped surplus land again, so the two
            // 20s (the demoted Island and Myriad) go back and the Eldrazi (22, Brainsurge held) stays.
            ArmP am;
            am.Set(heurarm::CYCLONE_KEEP_AMEND, 1);
            const std::vector<NamesP> fan = FanP(s);
            REQUIRE(fan.size() == 1);
            CHECK(std::find(fan[0].begin(), fan[0].end(), std::string(kEmrP)) == fan[0].end());
            CHECK(std::find(fan[0].begin(), fan[0].end(), std::string("Myriad Landscape")) != fan[0].end());
        }
    }
}

TEST_CASE("Cyclone cantrip plans: the USER's go-off rules survive as the SETUP fulfilment")
{
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PB_PLANS, 1);
    SUBCASE("k = 0, one cycle: [dead noncreature, Emrakul] is the SETUP pick (Emrakul over Ulamog)")
    {
        const GameState s = StateP({ kCycP, "Island", "Island", "Island" }, { kWraP, kEmrP, kHunP, kDelP, "Island" });
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(!fan.empty());
        CHECK(fan[0] == NamesP{ kDelP, kEmrP });
    }
    SUBCASE("k = 0, two cycles, two Eldrazi: both back, Emrakul first")
    {
        const GameState s = StateP({ kCycP, "Island", "Island", "Island" }, { kWraP, kWraP, kEmrP, kHunP, kDelP });
        CHECK(FanP(s)[0] == NamesP{ kEmrP, kHunP });
    }
}

TEST_CASE("Cyclone cantrip plans: the SITUATIONAL layer")
{
    ArmP arm;
    // The situational layer alone: PB_PLANS pinned off whatever its default (the subcases that want it set it).
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 1).Set(heurarm::CYCLONE_PB_PLANS, 0);
    SUBCASE("no Cyclone anywhere -> JUNK only (every remaining pick keeps the Eldrazi in hand)")
    {
        const GameState s = StateP({ "Island", "Island" }, { kWraP, kEmrP, kDelP, "Island", kTarnP }, {}, 0);
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(!fan.empty());
        for (const NamesP& p : fan) { CHECK_FALSE(HasEldrazi(p)); }
        // With the plan lever too: exactly one pick (the rule-as-prune shape).
        ArmP pl;
        pl.Set(heurarm::CYCLONE_PB_PLANS, 1);
        CHECK(FanP(s).size() == 1);
    }
    SUBCASE("a Cyclone KNOWN in the library (put back earlier) counts as available: the Eldrazi picks stay")
    {
        GameState s = StateP({ "Island", "Island" }, { kWraP, kEmrP, kDelP, "Island", kTarnP }, {}, 0);
        s.players[0].library.insert(s.players[0].library.begin(), CardP(kCycP, 777));   // a placement: KNOWN
        REQUIRE(s.players[0].library.KnownAt(0));
        bool eld = false;
        for (const NamesP& p : FanP(s)) { eld |= HasEldrazi(p); }
        CHECK(eld);
    }
    SUBCASE("...but an UNKNOWN Cyclone on top (never seen) does not")
    {
        GameState s = StateP({ "Island", "Island" }, { kWraP, kEmrP, kDelP, "Island", kTarnP }, { kCycP }, 0);
        REQUIRE_FALSE(s.players[0].library.KnownAt(0));
        for (const NamesP& p : FanP(s)) { CHECK_FALSE(HasEldrazi(p)); }
    }
    SUBCASE("ready to go off (k = 0, a Wraith, Emrakul in hand) -> SETUP only")
    {
        const GameState s = StateP({ kCycP, "Island", "Island", "Island" }, { kWraP, kEmrP, kDelP, "Island" });
        const std::vector<NamesP> fan = FanP(s);
        REQUIRE(!fan.empty());
        for (const NamesP& p : fan) { CHECK(HasEldrazi(p)); }
        CHECK(fan[0] == NamesP{ kDelP, kEmrP });
    }
    SUBCASE("k >= 2 with a Cyclone in hand: both plans stay")
    {
        const GameState s = StateP({ kVentsP, "Island", "Island" }, { kCycP, kWraP, kEmrP, kDelP, "Island", "Mountain" });
        bool setup = false, junk = false;
        for (const NamesP& p : FanP(s)) { (HasEldrazi(p) ? setup : junk) = true; }
        CHECK(setup);
        CHECK(junk);
    }
}

TEST_CASE("Cyclone cantrip plans: Ponder = the best KEEP order and the SHUFFLE")
{
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    DrawAfterP da(1);
    const GameState s = StateP({ kCycP, "Island", "Island" }, { kWraP }, { kEmrP, "Island", "Mountain" });
    const std::vector<Card> look = LookP(s, 3);
    std::vector<TopDisposition> full;
    {
        ArmP off;
        off.Set(heurarm::CYCLONE_PONDER_PLANS, 0).Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 0);
        full = TopDispositionCandidates(s, look, LookKind::Reorder, -1);
    }
    REQUIRE(full.size() == 7);
    ArmP on;
    on.Set(heurarm::CYCLONE_PONDER_PLANS, 1);
    const std::vector<TopDisposition> fan = TopDispositionCandidates(s, look, LookKind::Reorder, -1);
    REQUIRE(fan.size() == 2);
    CHECK(fan[0].shuffle == full[0].shuffle);
    CHECK(fan[0].top_order == full[0].top_order);
    CHECK(fan[1].shuffle == full[1].shuffle);
    CHECK(fan[1].top_order == full[1].top_order);
    CHECK(fan[0].shuffle != fan[1].shuffle);   // one keep, one shuffle
    CHECK(ProvP().CantripFanCap(s, false) == 2);
}

TEST_CASE("Cyclone cantrip plans: Ponder's SITUATIONAL layer -- one plan only where it is clear")
{
    // USER 2026-10-28: "Sometimes you want to draw that useful card and then fetch-shuffle the rest away rather than using
    // ponder shuffle ... I'm fine with it being searched a chunk of the time."
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 1);
    DrawAfterP da(1);
    auto plans = [](const std::vector<TopDisposition>& fan) { bool k = false, sh = false; for (const TopDisposition& d : fan) { (d.shuffle ? sh : k) = true; } return std::make_pair(k, sh); };
    SUBCASE("every revealed card expendable while a piece is missing (no Cyclone) -> the SHUFFLE only")
    {
        const GameState s = StateP({ "Island", "Island" }, { kWraP }, { kDelP, kEmrP, "Remand" });
        const std::vector<TopDisposition> fan = TopDispositionCandidates(s, LookP(s, 3), LookKind::Reorder, -1);
        REQUIRE(fan.size() == 1);
        CHECK(fan[0].shuffle);
        CHECK(HeuristicTopDisposition(s, LookP(s, 3), LookKind::Reorder, -1).shuffle);   // the base plan's default too
    }
    SUBCASE("a useful card revealed (the missing Cyclone) and NO later shuffle -> KEEP only")
    {
        const GameState s = StateP({ "Island", "Island" }, { kWraP }, { kCycP, "Island", "Mountain" });
        const auto pk = plans(TopDispositionCandidates(s, LookP(s, 3), LookKind::Reorder, -1));
        CHECK(pk.first);
        CHECK_FALSE(pk.second);
    }
    SUBCASE("a useful card revealed AND a later shuffle reachable (a fetchland in hand) -> both stay searched")
    {
        const GameState s = StateP({ "Island", "Island" }, { kWraP, kTarnP }, { kCycP, "Island", "Mountain" });
        const auto pk = plans(TopDispositionCandidates(s, LookP(s, 3), LookKind::Reorder, -1));
        CHECK(pk.first);
        CHECK(pk.second);
    }
    SUBCASE("the situational layer alone keeps every order of an admitted plan (7 with a shuffle reachable)")
    {
        const GameState s = StateP({ kCycP, "Island", "Island" }, { kWraP, kTarnP }, { kEmrP, "Island", "Mountain" }, 0);
        CHECK(TopDispositionCandidates(s, LookP(s, 3), LookKind::Reorder, -1).size() == 7);
    }
}

TEST_CASE("Cyclone seed plans: Curator's scry is the provider's walk-aware rule, not a pin")
{
    constexpr const char* kCurP = "Curator of Mysteries";
    ArmP arm;
    SUBCASE("lever off: the generic rule and the pinned j fan")
    {
        arm.Set(heurarm::CYCLONE_SEED_PLANS, 0);
        const GameState s = StateP({ kCycP, "Island" }, { kWraP });
        CHECK_FALSE(ProvP().CycleScryPinHeuristic(s));
        CHECK(ProvP().ScryKeepOnTop(s, CardP(kWraP, 1)));   // generic: keep a nonland
    }
    SUBCASE("Cyclone out: bottom a non-Eldrazi creature (it would stop the walk), keep an Eldrazi or a noncreature")
    {
        arm.Set(heurarm::CYCLONE_SEED_PLANS, 1);
        const GameState s = StateP({ kCycP, kCurP, "Island", "Island" }, { kWraP });
        CHECK(ProvP().CycleScryPinHeuristic(s));
        CHECK_FALSE(ProvP().ScryKeepOnTop(s, CardP(kWraP, 1)));
        CHECK_FALSE(ProvP().ScryKeepOnTop(s, CardP(kCurP, 2)));
        CHECK(ProvP().ScryKeepOnTop(s, CardP(kEmrP, 3)));
        CHECK(ProvP().ScryKeepOnTop(s, CardP(kDelP, 4)));
        CHECK(ProvP().ScryKeepOnTop(s, CardP("Island", 5)));
    }
    SUBCASE("no Cyclone out: keep a card the go-off needs (a missing Cyclone), bottom a dead one")
    {
        arm.Set(heurarm::CYCLONE_SEED_PLANS, 1);
        const GameState s = StateP({ kCurP, "Island", "Island" }, { kWraP, "Island" });
        CHECK(ProvP().ScryKeepOnTop(s, CardP(kCycP, 1)));
        CHECK_FALSE(ProvP().ScryKeepOnTop(s, CardP(kDelP, 2)));
    }
}

TEST_CASE("Cyclone cycler rule: which creature cycler a cycle seed uses (USER 2026-10-28)")
{
    constexpr const char* kCurP = "Curator of Mysteries";
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CYCLER_RULE, 1);
    auto has = [](const std::vector<std::string>& v, const char* n) { return std::find(v.begin(), v.end(), std::string(n)) != v.end(); };
    // The admission is PLAN-level: `out` holds for a plan whose Cyclone is out (or that casts it before the cycle),
    // `notout` for one where no Cyclone will be out.
    auto sets = [](const GameState& s, std::vector<std::string>& out, std::vector<std::string>& notout)
    { REQUIRE(ProvP().CycleSeedAdmissionSets(s, out, notout)); };
    SUBCASE("Cyclone out, {U} spare -> Curator only (keeps the free Wraith cycle)")
    {
        const GameState s = StateP({ kCycP, "Island", "Island" }, { kWraP, kCurP });
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK(has(o, kCurP));
        CHECK_FALSE(has(o, kWraP));
        CHECK(o == n);   // a Cyclone on the battlefield: both cases are "out"
    }
    SUBCASE("Cyclone out, no untapped blue -> Street Wraith only")
    {
        const GameState s = StateP({ kCycP, "Mountain" }, { kWraP, kCurP });
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK(has(o, kWraP));
        CHECK_FALSE(has(o, kCurP));
    }
    SUBCASE("Cyclone out, the mana is needed for a Brainstorm in hand -> Street Wraith only")
    {
        const GameState s = StateP({ kCycP, "Island" }, { kWraP, kCurP, "Brainstorm" });
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK(has(o, kWraP));
        CHECK_FALSE(has(o, kCurP));
    }
    SUBCASE("Cyclone in hand, not out -> Street Wraith is kept for it; a plan that casts the Cyclone first may cycle it")
    {
        const GameState s = StateP({ "Island", "Island" }, { kCycP, kWraP, kCurP });
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK_FALSE(has(n, kWraP));
        CHECK(has(n, kCurP));
        CHECK(!o.empty());   // judged as out: one creature cycler admitted
    }
    SUBCASE("Cyclone in hand with the mana in reach -> the LAST creature cycler is never cycled before it is out")
    {
        const GameState s = StateP({ kVentsP, "Island", "Island", "Mountain" }, { kCycP, kCurP, "Island" }, {}, 0);
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK_FALSE(has(n, kCurP));
        CHECK_FALSE(has(n, kWraP));
        CHECK(has(o, kCurP));   // the plan casting the Cyclone first still cycles it
    }
    SUBCASE("Cyclone KNOWN on top (placed by us) counts like one in hand; an unknown one does not")
    {
        GameState s = StateP({ kVentsP, "Island", "Island", "Mountain" }, { kWraP, kCurP }, {}, 0);
        s.players[0].library.insert(s.players[0].library.begin(), CardP(kCycP, 777));
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK_FALSE(has(n, kWraP));
        CHECK(has(n, kCurP));
        GameState u = StateP({ kVentsP, "Island", "Island", "Mountain" }, { kWraP, kCurP }, { kCycP }, 0);
        sets(u, o, n);
        CHECK(has(n, kWraP));
        CHECK(has(n, kCurP));
    }
    SUBCASE("no Cyclone option -> both searched, Curator ordered first (SearchedCycleSources)")
    {
        const GameState s = StateP({ "Island", "Island" }, { kWraP, kCurP });
        const auto v = ProvP().SearchedCycleSources(s);
        REQUIRE(has(v, kWraP));
        REQUIRE(has(v, kCurP));
        CHECK(std::find(v.begin(), v.end(), std::string(kCurP)) < std::find(v.begin(), v.end(), std::string(kWraP)));
        std::vector<std::string> o, n; sets(s, o, n);
        CHECK(has(n, kWraP));
        CHECK(has(n, kCurP));
    }
    SUBCASE("per plan with the Cyclone out: the {U} is judged after the plan's own casts (d5 gi141)")
    {
        const GameState s = StateP({ "Island", "Island", "Blood Crypt" }, { kCycP, kWraP, kCurP, "Seething Song" });
        // A plan casting Seething Song and the Cyclone (net 8 - 5 = 3 of its own mana) leaves no {U}: the Wraith.
        CHECK(ProvP().CycleSeedAdmitOut(s, kWraP, 3, 0, nullptr));
        CHECK_FALSE(ProvP().CycleSeedAdmitOut(s, kCurP, 3, 0, nullptr));
        // A plan that spends nothing first keeps the {U}: the Curator.
        CHECK(ProvP().CycleSeedAdmitOut(s, kCurP, 0, 0, nullptr));
        CHECK_FALSE(ProvP().CycleSeedAdmitOut(s, kWraP, 0, 0, nullptr));
    }
    SUBCASE("lever off: no admission filter")
    {
        ArmP off;
        off.Set(heurarm::CYCLONE_CYCLER_RULE, 0);
        const GameState s = StateP({ kCycP, "Island", "Island" }, { kWraP, kCurP });
        std::vector<std::string> o, n;
        CHECK_FALSE(ProvP().CycleSeedAdmissionSets(s, o, n));
    }
}


TEST_CASE("Cyclone cantrip plans: two-Eldrazi JUNK follows the current or next-turn walk")
{
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PB_PLANS, 1)
       .Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 0).Set(heurarm::CYCLONE_KEEP_AMEND, 0);
    GameState s;
    int first = -1, second = -1, expected_k = -1;
    SUBCASE("this turn: Emrakul first in JUNK itself")
    {
        s = StateP({ kCycP, "Island", "Island", "Island" }, { kWraP, kEmrP, kHunP });
        first = 1; second = 2; expected_k = 0;
    }
    SUBCASE("next turn: lesser first, Emrakul preserved for the walk")
    {
        s = StateP({ "Island", "Island", "Mountain", "Mountain" }, { kCycP, kWraP, kEmrP, kHunP, "Island" });
        first = 3; second = 2; expected_k = 1;
    }
    CantripReadiness r;
    REQUIRE(ProvP().CantripReadinessOf(s, 0, r));
    REQUIRE(r.k == expected_k);
    REQUIRE_FALSE(r.missing_piece);
    const auto& hand = s.players[0].hand;
    TopDispositionView v;
    v.putback = true;
    v.n_removed = 2;
    v.n_top = 2;
    v.removed[0] = first;
    v.removed[1] = second;
    v.top[0] = &hand[static_cast<std::size_t>(first)];
    v.top[1] = &hand[static_cast<std::size_t>(second)];
    int kind = -1;
    REQUIRE(ProvP().CantripPlanOf(s, 0, r, v, kind));
    CHECK(kind == 1);   // JUNK, not merely a matching SETUP candidate
    const NamesP expected{ hand[static_cast<std::size_t>(first)].m_name.str(),
                           hand[static_cast<std::size_t>(second)].m_name.str() };
    const auto fan = FanP(s);
    CHECK(std::find(fan.begin(), fan.end(), expected) != fan.end());
}

TEST_CASE("Cyclone cantrip cache key: KEEP_AMEND includes remaining land density")
{
    ArmP arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_KEEP_AMEND, 1)
       .Set(heurarm::CYCLONE_KEEP_MYRIAD, 0).Set(heurarm::CYCLONE_CANTRIP_SITUATIONAL, 0);
    GameState a = StateP({ "Island" }, { kCycP, kWraP, "Brainstorm", "Island", "Island", "Island", "Island", kEmrP, kDelP });
    GameState b = a;
    std::vector<Card> lands, spells;
    for (int i = 0; i < 12; ++i)
    {
        lands.push_back(CardP("Island", 2000 + i));
        spells.push_back(CardP(kDelP, 2000 + i));
    }
    a.players[0].library.assign(lands.begin(), lands.end());
    b.players[0].library.assign(spells.begin(), spells.end());
    CantripReadiness ra, rb;
    REQUIRE(ProvP().CantripReadinessOf(a, 0, ra));
    REQUIRE(ProvP().CantripReadinessOf(b, 0, rb));
    REQUIRE(ra.keep_need != rb.keep_need);   // only the land-density demotion differs
    std::vector<std::uint64_t> ka, kb;
    REQUIRE(ProvP().CantripOrderKey(a, 0, ka));
    REQUIRE(ProvP().CantripOrderKey(b, 0, kb));
    CHECK(ka != kb);
}
