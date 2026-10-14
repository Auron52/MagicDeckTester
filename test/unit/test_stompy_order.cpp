// Unit cover for the USER's StompySurprise cast order (MTG_STOMPY_ORDER, default ON since 2026-10-14;
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
//   6. the shared comparator's fixed key: by name for this deck, plan order for the others.
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
        CHECK_FALSE(ProvSo().CastOrderTotal());
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
    // Two copies of a dual card with DIFFERENT targets are ordered by the target, not by plan order.
    const GameState s = BoardSo({ kWT, kWT });
    std::vector<Action> a1 = { CastSo(s, kWT, kSpine), CastSo(s, kWT, kHoof) };
    std::vector<Action> a2 = { a1[1], a1[0] };
    std::vector<int> o1 = { 0, 1 }, o2 = { 0, 1 };
    std::stable_sort(o1.begin(), o1.end(), [&](int x, int y) { return CastOrderLess(s, a1[x], a1[y]); });
    std::stable_sort(o2.begin(), o2.end(), [&](int x, int y) { return CastOrderLess(s, a2[x], a2[y]); });
    CHECK(a1[o1[0]].tutor_target.str() == a2[o2[0]].tutor_target.str());
    CHECK(a1[o1[0]].tutor_target.str() == std::string(kHoof));   // "Craterhoof..." < "Worldspine..."
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
