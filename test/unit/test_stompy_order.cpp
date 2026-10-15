// Unit cover for the USER's StompySurprise cast order (MTG_STOMPY_ORDER; default OFF since 2026-10-15 pending the bar;
// docs/design/stompy-order-2026-10-14.md, docs/design/cast-order-rankings.md §StompySurprise).
//
// USER 2026-10-14: "Let's try out your stompy suggestion. It seems pretty close to what I was looking for
// and we can fix any issues that show up." and "Let's fully order the cards in each list inasmuch as
// possible. I don't like the randomness of choosing the order."
//
// What is pinned:
//   1. the rank lays the list out in the approved order, TOTAL (lever off = the generic tiers);
//   2. the realised order does not depend on plan / hand order (every permutation, same sequence);
//   3. the four dual-position cards read the PLAN: Worldly Tutor mid only with a Turntimber in the plan,
//      Turntimber after it then, Natural Order early only before a mid tutor, a stacked top consumed where
//      it stands, Vaultborn no consumer; World War Hulk reads the hand;
//   4. the searched two-way candidates: the canonical order first, one variant per dual card, deduplicated,
//      none for a set with no dual card;
//   5. the TOP_RESOLVE opt-in is the deck's own and leaves the global lever (and other decks) alone;
//   6. the shared comparator's fixed key: by name for this deck, plan order for the others;
//   7. MTG_STOMPY_NO_TARGET_RANK: Natural Order copies resolve in their targets' slot order;
//   8. MTG_HOLD_RELEASE_YIELD: the prepay's hold ladder is battlefield-position-free (gi1526, gi287);
//   9. MTG_SAC_VICTIM_SAME_NAME: a stale Natural Order victim pin moves to another copy of its name (gi160).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
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

void EnsureCardsSo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSo(const std::string& name, int number)
{
    Card c     = DefSo(name).card;
    c.m_number = number;
    return c;
}

const char* kMystic   = "Elvish Mystic";
const char* kFyndhorn = "Fyndhorn Elves";
const char* kLlanowar = "Llanowar Elves";
const char* kSolRing  = "Sol Ring";
const char* kPriest   = "Priest of Titania";
const char* kArch     = "Elvish Archdruid";
const char* kCall     = "Call of the Wild";
const char* kTT       = "Turntimber Symbiosis";
const char* kNO       = "Natural Order";
const char* kWT       = "Worldly Tutor";
const char* kVault    = "Vaultborn Tyrant";
const char* kGhalta   = "Ghalta, Stampede Tyrant";
const char* kHulk     = "World War Hulk";
const char* kHornet   = "Hornet Queen";
const char* kElder    = "Elderscale Wurm";
const char* kApex     = "Apex Altisaur";
const char* kSpine    = "Worldspine Wurm";
const char* kTera     = "Terastodon";
const char* kHoof     = "Craterhoof Behemoth";

// The levers this file reads, forced for this thread and restored on scope exit.
struct SoArms
{
    explicit SoArms(bool order, bool search = true, bool top = true)
    {
        heurarm::t_arm[heurarm::STOMPY_ORDER]        = order ? 1 : 0;
        heurarm::t_arm[heurarm::STOMPY_ORDER_SEARCH] = search ? 1 : 0;
        heurarm::t_arm[heurarm::STOMPY_TOP_RESOLVE]  = top ? 1 : 0;
        heurarm::t_arm[heurarm::TOP_RESOLVE]         = 0;   // the GLOBAL lever stays off
    }
    ~SoArms()
    {
        heurarm::t_arm[heurarm::STOMPY_ORDER]        = -1;
        heurarm::t_arm[heurarm::STOMPY_ORDER_SEARCH] = -1;
        heurarm::t_arm[heurarm::STOMPY_TOP_RESOLVE]  = -1;
        heurarm::t_arm[heurarm::TOP_RESOLVE]         = -1;
    }
};

const StompyProvider& ProvSo()
{
    static const StompyProvider p;
    return p;
}

// A pre-combat main on twenty untapped Forests (every set pays), land drop made, the given hand, a
// library of Forests.
GameState BoardSo(const std::vector<std::string>& hand, int turn = 5)
{
    EnsureCardsSo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvSo();
    for (int k = 0; k < 20; ++k)
    {
        Permanent p;
        p.card             = DefSo("Forest").card;
        p.card.m_number    = 300 + k;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    int num = 10;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardSo(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardSo("Forest", 900 + k)); }
    return s;
}

Action CastSo(const GameState& s, const std::string& name, const std::string& target = "")
{
    const CardDefinition& d = DefSo(name);
    Action a;
    a.kind         = Action::Kind::CastFromHand;
    a.card_name    = name;
    a.def          = &d;
    a.cost         = EffectiveSpellCost(d, s);
    a.card_mv      = d.card.m_mana_cost.ManaValue();
    if (!target.empty()) { a.tutor_target = target; }
    return a;
}

std::vector<std::string> NamesSo(const std::vector<Action>& acts, const std::vector<int>& ord)
{
    std::vector<std::string> v;
    for (int i : ord) { v.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return v;
}

// The order an un-searched apply site realises: the comparator, then the deck's arrangement.
std::vector<std::string> RealisedSo(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> ord(acts.size());
    for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    return NamesSo(acts, ord);
}

std::vector<Action> CastsSo(const GameState& s, const std::vector<std::string>& names)
{
    std::vector<Action> v;
    for (const std::string& n : names) { v.push_back(CastSo(s, n)); }
    return v;
}

}   // namespace

TEST_CASE("Stompy order: the rank lays the list out in the approved order, total; lever off = generic tiers")
{
    const GameState empty = BoardSo({});
    const GameState hoof  = BoardSo({ kHoof });   // a Craterhoof in hand moves World War Hulk late
    SoArms arms(true);
    auto r = [&](const GameState& s, const char* n) { return ProvSo().CastOrderRank(s, DefSo(n)); };
    // The three 1-drops share a rank (identical text); the fixed key orders them by name.
    CHECK(r(empty, kMystic) == r(empty, kFyndhorn));
    CHECK(r(empty, kFyndhorn) == r(empty, kLlanowar));
    const std::vector<std::pair<const GameState*, const char*>> list = {
        { &empty, kLlanowar }, { &empty, kSolRing }, { &empty, kPriest }, { &empty, kArch }, { &empty, kCall },
        { &empty, kTT }, { &empty, kVault }, { &empty, kGhalta }, { &empty, kHulk }, { &empty, kHornet },
        { &empty, kElder }, { &empty, kApex }, { &empty, kSpine }, { &empty, kTera }, { &empty, kNO },
        { &hoof, kHulk }, { &empty, kHoof }, { &empty, kWT } };
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
    {
        CAPTURE(list[i].second); CAPTURE(list[i + 1].second);
        CHECK(r(*list[i].first, list[i].second) < r(*list[i + 1].first, list[i + 1].second));
    }
    // The late ends of the four dual cards (breakpoint condemnation reads CastOrderRankLatest).
    CHECK(ProvSo().CastOrderRankLatest(empty, DefSo(kTT))   >  r(empty, kTT));     // after the tutor
    CHECK(ProvSo().CastOrderRankLatest(empty, DefSo(kTT))   <  r(empty, kVault));
    CHECK(ProvSo().CastOrderRankLatest(empty, DefSo(kWT))   == r(empty, kWT));     // late is the default
    CHECK(ProvSo().CastOrderRankLatest(empty, DefSo(kNO))   == r(empty, kNO));
    CHECK(ProvSo().CastOrderRankLatest(empty, DefSo(kHulk)) == r(hoof, kHulk));
    {
        SoArms off(false);
        const GenericProvider generic;
        for (const auto& e : list)
        { CAPTURE(e.second); CHECK(ProvSo().CastOrderRank(*e.first, DefSo(e.second)) == generic.CastOrderRank(*e.first, DefSo(e.second))); }
        CHECK_FALSE(ProvSo().CastOrderTotalByName());
        CHECK_FALSE(ProvSo().WantsCastOrderingSearch());
        CHECK_FALSE(ProvSo().TopResolveOptIn());
    }
}

TEST_CASE("Stompy order: the realised order is TOTAL -- every permutation of the plan gives one sequence")
{
    const std::vector<std::string> set = { kLlanowar, kMystic, kFyndhorn, kSolRing, kPriest, kArch, kCall,
                                           kHornet, kElder, kTera, kApex, kSpine, kHoof };
    const std::vector<std::string> want = { kMystic, kFyndhorn, kLlanowar, kSolRing, kPriest, kArch, kCall,
                                            kHornet, kElder, kApex, kSpine, kTera, kHoof };
    SoArms arms(true);
    std::vector<std::string> hand = set;
    // Hand order is permuted too (it is what the plan order comes from); 40 rotations x reversals.
    for (int k = 0; k < 40; ++k)
    {
        std::rotate(hand.begin(), hand.begin() + (k % static_cast<int>(hand.size())), hand.end());
        if (k % 3 == 0) { std::reverse(hand.begin(), hand.end()); }
        const GameState s = BoardSo(hand);
        std::vector<std::string> plan = hand;
        std::rotate(plan.begin(), plan.begin() + ((k * 7) % static_cast<int>(plan.size())), plan.end());
        CAPTURE(k);
        CHECK(RealisedSo(s, CastsSo(s, plan)) == want);
    }
    // Two copies of a dual card with DIFFERENT targets are ordered by the target, not by plan order: the
    // shared comparator keeps copies of one card in plan order (the canonical CastOrderTotalByName), and
    // the deck's arrangement -- which every realising site runs after it -- orders them by the target.
    const GameState s = BoardSo({ kWT, kWT });
    std::vector<Action> a1 = { CastSo(s, kWT, kSpine), CastSo(s, kWT, kHoof) };
    std::vector<Action> a2 = { a1[1], a1[0] };
    std::vector<int> o1 = { 0, 1 }, o2 = { 0, 1 };
    std::stable_sort(o1.begin(), o1.end(), [&](int x, int y) { return CastOrderLess(s, a1[x], a1[y]); });
    std::stable_sort(o2.begin(), o2.end(), [&](int x, int y) { return CastOrderLess(s, a2[x], a2[y]); });
    ApplyDeckCastArrangement(s, a1, o1);
    ApplyDeckCastArrangement(s, a2, o2);
    CHECK(a1[o1[0]].tutor_target.str() == a2[o2[0]].tutor_target.str());
    CHECK(a1[o1[0]].tutor_target.str() == std::string(kHoof));   // "Craterhoof..." < "Worldspine..."
}

TEST_CASE("Stompy order (MTG_STOMPY_NO_TARGET_RANK): Natural Order copies resolve in their TARGETS' slot order")
{
    // stompy2hg gi160: Natural Order -> Ghalta must resolve before Natural Order -> Craterhoof, so the
    // Craterhoof Ghalta frees from hand counts the extra body. By target NAME ("Craterhoof" < "Ghalta") it
    // was the other way round, 29 trample against 30.
    SoArms arms(true);
    const GameState s = BoardSo({ kNO, kNO, kWT, kWT });
    auto first_target = [&](const std::vector<Action>& acts)
    {
        std::vector<int> o = { 0, 1 };
        std::stable_sort(o.begin(), o.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
        ApplyDeckCastArrangement(s, acts, o);
        return acts[static_cast<std::size_t>(o[0])].tutor_target.str();
    };
    const std::vector<Action> no1 = { CastSo(s, kNO, kHoof), CastSo(s, kNO, kGhalta) };
    const std::vector<Action> no2 = { no1[1], no1[0] };
    const std::vector<Action> wt1 = { CastSo(s, kWT, kSpine), CastSo(s, kWT, kHoof) };
    const std::vector<Action> wt2 = { wt1[1], wt1[0] };
    for (int lever = 0; lever <= 1; ++lever)
    {
        heurarm::t_arm[heurarm::STOMPY_NO_TARGET_RANK] = lever;
        CAPTURE(lever);
        const std::string want = lever ? kGhalta : kHoof;   // slot 1700 < 2100 ; else by name
        CHECK(first_target(no1) == want);
        CHECK(first_target(no2) == want);
        // Worldly Tutor copies are not Natural Order: by target name either way.
        CHECK(first_target(wt1) == std::string(kHoof));
        CHECK(first_target(wt2) == std::string(kHoof));
    }
    heurarm::t_arm[heurarm::STOMPY_NO_TARGET_RANK] = -1;
}

TEST_CASE("Stompy order: the dual slots read the PLAN, not the hand")
{
    SoArms arms(true);
    {
        // A Turntimber in the plan takes the tutor's card: tutor mid, Turntimber after it.
        const GameState s = BoardSo({ kTT, kWT, kElder });
        CHECK(RealisedSo(s, CastsSo(s, { kTT, kElder, kWT })) == std::vector<std::string>{ kWT, kTT, kElder });
    }
    {
        // ...and a Natural Order in that plan goes EARLY, so its shuffle precedes the stack.
        const GameState s = BoardSo({ kTT, kWT, kNO, kElder });
        CHECK(RealisedSo(s, CastsSo(s, { kElder, kTT, kWT, kNO }))
              == std::vector<std::string>{ kNO, kWT, kTT, kElder });
    }
    {
        // A Turntimber only in HAND is no reason (the 08-21 over-trigger): tutor late, Natural Order late.
        const GameState s = BoardSo({ kTT, kWT, kNO, kElder });
        CHECK(RealisedSo(s, CastsSo(s, { kWT, kNO, kElder })) == std::vector<std::string>{ kElder, kNO, kWT });
    }
    {
        // Vaultborn is not a consumer (the retired agent clause): the tutor stays last.
        const GameState s = BoardSo({ kWT, kVault });
        CHECK(RealisedSo(s, CastsSo(s, { kWT, kVault })) == std::vector<std::string>{ kVault, kWT });
    }
    {
        // The top already stacked this turn, its card still there: Turntimber consumes it where it stands.
        GameState s = BoardSo({ kTT, kWT });
        s.players[0].library.insert(s.players[0].library.begin(), CardSo(kHoof, 777));
        s.top_stacked_turn        = s.turn_number;
        s.top_stacked_card_number = 777;
        CHECK(RealisedSo(s, CastsSo(s, { kWT, kTT })) == std::vector<std::string>{ kTT, kWT });
        // ...a stack already consumed (another card on top now) is no stack.
        s.top_stacked_card_number = 778;
        CHECK(RealisedSo(s, CastsSo(s, { kWT, kTT })) == std::vector<std::string>{ kWT, kTT });
    }
    {
        // World War Hulk reads the HAND: nothing to free but bodies -> before the fatties and Natural Order.
        const GameState s = BoardSo({ kHulk, kElder, kNO, kSpine });
        CHECK(RealisedSo(s, CastsSo(s, { kNO, kElder, kHulk })) == std::vector<std::string>{ kHulk, kElder, kNO });
        // A Craterhoof in hand: after every body, before the hard-cast Craterhoof.
        const GameState h = BoardSo({ kHulk, kElder, kNO, kHoof });
        CHECK(RealisedSo(h, CastsSo(h, { kHoof, kHulk, kNO, kElder }))
              == std::vector<std::string>{ kElder, kNO, kHulk, kHoof });
    }
}

TEST_CASE("Stompy order: the searched two-way candidates -- canonical first, one variant per dual card")
{
    SoArms arms(true);
    std::vector<std::vector<int>> out;
    {
        // No dual card: no candidate at all (the base plan is kept un-searched -- nothing applied twice).
        const GameState s = BoardSo({ kElder, kHornet });
        const std::vector<Action> r = CastsSo(s, { kElder, kHornet });
        out = { { 9 } };
        CHECK(ProvSo().CastOrderingCandidates(s, r, r, out));
        CHECK(out.empty());
    }
    {
        // {Worldly Tutor, Turntimber}: tutor -> Turntimber first, then Turntimber on the unknown top.
        const GameState s = BoardSo({ kTT, kWT });
        const std::vector<Action> r = CastsSo(s, { kTT, kWT });
        REQUIRE(ProvSo().CastOrderingCandidates(s, r, r, out));
        REQUIRE(out.size() == 2);   // the tutor's flip and Turntimber's flip are the same sequence
        CHECK(NamesSo(r, out[0]) == std::vector<std::string>{ kWT, kTT });
        CHECK(NamesSo(r, out[1]) == std::vector<std::string>{ kTT, kWT });
    }
    {
        // {Tutor, Turntimber, Natural Order}: canonical NO -> WT -> TT, then each card at its other slot.
        const GameState s = BoardSo({ kTT, kWT, kNO });
        const std::vector<Action> r = CastsSo(s, { kTT, kWT, kNO });
        REQUIRE(ProvSo().CastOrderingCandidates(s, r, r, out));
        REQUIRE(out.size() == 4);
        CHECK(NamesSo(r, out[0]) == std::vector<std::string>{ kNO, kWT, kTT });   // the USER's slots
        CHECK(NamesSo(r, out[1]) == std::vector<std::string>{ kTT, kNO, kWT });   // Turntimber first
        CHECK(NamesSo(r, out[2]) == std::vector<std::string>{ kWT, kTT, kNO });   // Natural Order late
        CHECK(NamesSo(r, out[3]) == std::vector<std::string>{ kNO, kTT, kWT });   // the tutor late
    }
    {
        // World War Hulk: early (no Craterhoof in hand) and the late variant.
        const GameState s = BoardSo({ kHulk, kElder });
        const std::vector<Action> r = CastsSo(s, { kElder, kHulk });
        REQUIRE(ProvSo().CastOrderingCandidates(s, r, r, out));
        REQUIRE(out.size() == 2);
        CHECK(NamesSo(r, out[0]) == std::vector<std::string>{ kHulk, kElder });
        CHECK(NamesSo(r, out[1]) == std::vector<std::string>{ kElder, kHulk });
    }
    CHECK(ProvSo().WantsCastOrderingSearch());
    {
        SoArms canon(true, /*search=*/false);
        CHECK_FALSE(ProvSo().WantsCastOrderingSearch());
    }
    {
        SoArms off(false);
        const GameState s = BoardSo({ kTT, kWT });
        const std::vector<Action> r = CastsSo(s, { kTT, kWT });
        CHECK_FALSE(ProvSo().CastOrderingCandidates(s, r, r, out));
    }
}

TEST_CASE("Stompy order: the TOP_RESOLVE opt-in is the deck's own; the global lever and other decks are untouched")
{
    const GameState s = BoardSo({});
    GameState g = s;
    const GenericProvider generic;
    g.m_provider = &generic;
    {
        SoArms arms(true);
        CHECK(TopResolveActive(s));
        CHECK_FALSE(TopResolveActive(g));
        CHECK_FALSE(TopResolveEnabled());
    }
    {
        SoArms no_top(true, true, /*top=*/false);
        CHECK_FALSE(TopResolveActive(s));
    }
    {
        SoArms off(false);
        CHECK_FALSE(TopResolveActive(s));
    }
}

TEST_CASE("Stompy order: the shared comparator's fixed key -- by name for this deck, plan order elsewhere")
{
    SoArms arms(true);
    const GameState s = BoardSo({ kLlanowar, kMystic });
    const Action ll = CastSo(s, kLlanowar), em = CastSo(s, kMystic);
    CHECK(CastOrderLess(s, em, ll));
    CHECK_FALSE(CastOrderLess(s, ll, em));
    GameState g = s;
    const GenericProvider generic;
    g.m_provider = &generic;
    CHECK_FALSE(CastOrderLess(g, em, ll));   // a tie: stable, plan order decides
    CHECK_FALSE(CastOrderLess(g, ll, em));
}

// ---- MTG_HOLD_RELEASE_YIELD: the prepay hold ladder is position-free and spends Priest first ------
//
// StompySurprise gi1526 T3 (docs/design/stompy-order-2026-10-14.md §4): Forest x2, Sol Ring, Fyndhorn
// Elves, Elvish Mystic, Priest of Titania untapped; the line is Elvish Archdruid + Natural Order (7 mana,
// Priest the sacrifice). The kill needs both 1/1 elves UNTAPPED to attack: the lands + Sol Ring pay 4 and
// Priest's tap the other 3. The historical ladder held ONE attacker (the lowest battlefield index among
// the equal-power bodies) before it tried holding two, so with Priest entered after both 1-drops a 1/1
// paid instead of Priest.
namespace
{
GameState HoldBoardSo(const std::vector<std::string>& creatures_in_order)
{
    EnsureCardsSo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvSo();
    int num = 300;
    auto put = [&](const std::string& n)
    {
        Permanent p;
        p.card             = DefSo(n).card;
        p.card.m_number    = num++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    };
    put("Forest"); put("Forest"); put("Sol Ring");
    for (const std::string& c : creatures_in_order) { put(c); }
    s.players[0].hand.push_back(CardSo(kArch, 10));
    s.players[0].hand.push_back(CardSo(kNO, 11));
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardSo("Forest", 900 + k)); }
    return s;
}

int NumberOfSo(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return p.card.m_number; } }
    return 0;
}

bool TappedSo(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return p.tapped; } }
    return false;
}
}   // namespace

TEST_CASE("Payment fix (MTG_HOLD_RELEASE_YIELD): the prepay keeps both 1/1 elves up and spends Priest, wherever it sits")
{
    const std::vector<std::vector<std::string>> orders = {
        { kFyndhorn, kMystic, kPriest },   // Priest after both 1-drops: the historical ladder tapped one
        { kMystic, kFyndhorn, kPriest },
        { kPriest, kFyndhorn, kMystic },
        { kMystic, kPriest, kFyndhorn },
        { kFyndhorn, kPriest, kMystic } };
    for (const bool fix : { true, false })
    {
        heurarm::t_arm[heurarm::HOLD_RELEASE_YIELD] = fix ? 1 : 0;
        bool every_order_keeps_both = true;
        for (const auto& ord : orders)
        {
            CAPTURE(fix); CAPTURE(ord[0]); CAPTURE(ord[1]); CAPTURE(ord[2]);
            GameState s = HoldBoardSo(ord);
            std::vector<Action> acts = { CastSo(s, kArch), CastSo(s, kNO, kHoof) };
            acts[1].soulfire_own_targets = NumberOfSo(s, kPriest);   // Priest is the sacrifice
            REQUIRE(TurnSolver::BatchPrepayMainCasts(s, acts));
            const bool both_up = !TappedSo(s, kFyndhorn) && !TappedSo(s, kMystic);
            if (fix) { CHECK(both_up); CHECK(TappedSo(s, kPriest)); }
            every_order_keeps_both = every_order_keeps_both && both_up;
        }
        // Off: the historical ladder spends an attacker in at least one battlefield order (the defect).
        if (!fix) { CHECK_FALSE(every_order_keeps_both); }
    }
    heurarm::t_arm[heurarm::HOLD_RELEASE_YIELD] = -1;
}

TEST_CASE("Payment fix (MTG_HOLD_RELEASE_YIELD): gi287's 6-mana turn holds BOTH Fyndhorns and taps Priest, wherever it sits")
{
    // T3, Forest x3, Fyndhorn Elves x2, Priest of Titania; the line is Elvish Archdruid x2 (6 mana): the
    // lands pay 3 and Priest's one tap (3 Elves) the rest, so both 1/1s swing. The historical ladder tried
    // the single-attacker rung first (hold ONE Fyndhorn) and then tapped the other ahead of Priest when
    // Priest sat later on the battlefield.
    for (const auto& ord : std::vector<std::vector<std::string>>{
             { kFyndhorn, kFyndhorn, kPriest }, { kPriest, kFyndhorn, kFyndhorn }, { kFyndhorn, kPriest, kFyndhorn } })
    {
        CAPTURE(ord[0]); CAPTURE(ord[1]); CAPTURE(ord[2]);
        heurarm::t_arm[heurarm::HOLD_RELEASE_YIELD] = 1;
        GameState s = HoldBoardSo(ord);
        // The board helper lays Forest, Forest, Sol Ring: make the third source a Forest.
        for (Permanent& p : s.battlefield)
        {
            if (p.card.m_name.str() != kSolRing) { continue; }
            const int num = p.card.m_number;
            p.card = DefSo("Forest").card;
            p.card.m_number = num;
        }
        s.players[0].hand.push_back(CardSo(kArch, 12));
        std::vector<Action> acts = { CastSo(s, kArch), CastSo(s, kArch) };
        REQUIRE(TurnSolver::BatchPrepayMainCasts(s, acts));
        int fyndhorn_up = 0;
        for (const Permanent& p : s.battlefield)
        { if (p.card.m_name.str() == kFyndhorn && !p.tapped) { ++fyndhorn_up; } }
        CHECK(fyndhorn_up == 2);
        CHECK(TappedSo(s, kPriest));
        heurarm::t_arm[heurarm::HOLD_RELEASE_YIELD] = -1;
    }
}

TEST_CASE("Sacrifice victim (MTG_SAC_VICTIM_SAME_NAME): a stale pin eats another copy of its name, wherever it sits")
{
    // stompy2hg gi160, T4: two Natural Orders each pin "Priest of Titania" -- the SAME copy (#3, the
    // pre-plan board's most-expendable Priest). The first ate it; the second's pin is stale. The kill
    // needs the second to eat the other (tapped) Priest and keep Fyndhorn Elves up to attack.
    EnsureCardsSo();
    for (int lever = 0; lever <= 1; ++lever)
    {
        CAPTURE(lever);
        heurarm::t_arm[heurarm::SAC_VICTIM_SAME_NAME] = lever;
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = 30;
        s.m_provider = &ProvSo();
        auto put = [&](const char* n, int num, bool tapped)
        {
            Permanent p;
            p.card             = CardSo(n, num);
            p.controller_index = 0;
            p.owner_index      = 0;
            p.tapped           = tapped;
            s.battlefield.push_back(p);
        };
        put("Forest", 1, true);
        put(kFyndhorn, 5, false);   // BEFORE the remaining Priest on the battlefield
        put(kPriest, 4, true);
        s.players[0].graveyard.push_back(CardSo(kPriest, 3));   // the first Natural Order's victim
        CHECK(ResolveSacVictimId(s, "G", 4) == 4);                // a live pin is untouched
        CHECK(ResolveSacVictimId(s, "G", 0) == 0);
        CHECK(ResolveSacVictimId(s, "G", 3) == (lever ? 4 : 3));
        PerformSacrificeCreatureCost(s, kNO, "G", 3);
        bool fyndhorn = false, priest = false;
        for (const Permanent& p : s.battlefield)
        {
            if (p.card.m_name.str() == kFyndhorn) { fyndhorn = true; }
            if (p.card.m_name.str() == kPriest)   { priest = true; }
        }
        // Lever off: the stale pin falls to power then battlefield position -- Fyndhorn (the defect).
        CHECK(fyndhorn == (lever == 1));
        CHECK(priest == (lever == 0));
    }
    heurarm::t_arm[heurarm::SAC_VICTIM_SAME_NAME] = -1;
}
