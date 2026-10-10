// Unit cover for the USER's Dragonstorm cast order (MTG_DRAGONSTORM_USER_ORDER;
// docs/design/dragonstorm-user-cast-order.md).
//
// USER 2026-10-10: "We should do things in the order I said, once they become available, so after the
// breakpoint the cast order should follow my order. If it wasn't doing that it should be fixed." And:
// "Ruby medallion should be flexible. It should be cast as early as possible while still maintaining the
// line. It has to be cast later if cost does not allow it to be earlier."
//
// The order: Ruby Medallion (earliest payable slot) < Rite of Flame < Pyretic Ritual < Desperate Ritual (no
// splice) < Seething Song < Desperate Ritual (spliced) < Apex of Power < a hard-cast Dragon < Irencrag Feat
// (second-to-last) < Dragonstorm (last).
//
// What is pinned:
//   1. the rank lays the deck's cards out in that list (lever on) and is the generic tier (lever off);
//   2. the shared comparator sorts a shuffled go-off set into it, a SPLICED Desperate after Seething;
//   3. the Medallion goes to its EARLIEST PAYABLE slot -- not first when first cannot be paid, after one
//      ritual when that pays -- and the old payable-order fallback alone does not put it there;
//   4. executor level (the rollout apply, ApplyPlanDirect -- the executor's lockstep twin, same shared
//      order functions): that Medallion line casts every spell, where the generic order strands the Dragon;
//   5. an Apex set holding Irencrag: Apex BEFORE Irencrag (lever off: Irencrag -> Apex, the trap), and the
//      exact walk refuses the trap order;
//   6. Irencrag before a lone Dragon closer; splice legality; the searched orderings.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
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

void EnsureCardsUo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefUo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutUo(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefUo(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardUo(const std::string& name, int number)
{
    Card c     = DefUo(name).card;
    c.m_number = number;
    return c;
}

int InHandUo(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

int OnBattlefieldUo(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

const char* kMed   = "Ruby Medallion";
const char* kRite  = "Rite of Flame";
const char* kPyr   = "Pyretic Ritual";
const char* kDesp  = "Desperate Ritual";
const char* kSeeth = "Seething Song";
const char* kApex  = "Apex of Power";
const char* kScour = "Scourge of Valkas";
const char* kIren  = "Irencrag Feat";
const char* kStorm = "Dragonstorm";

// The levers this file reads, forced for this thread and restored on scope exit. The payable-order
// rule and its ritual domain are the adopted defaults; the opaque rank order is the deck's default.
struct UoArms
{
    explicit UoArms(bool user_order)
    {
        heurarm::t_arm[heurarm::DRAGONSTORM_USER_ORDER]   = user_order ? 1 : 0;
        heurarm::t_arm[heurarm::DRAGONSTORM_ORDER_OPAQUE] = 1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]            = 1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER_RITUALS]    = 1;
    }
    ~UoArms()
    {
        heurarm::t_arm[heurarm::DRAGONSTORM_USER_ORDER]   = -1;
        heurarm::t_arm[heurarm::DRAGONSTORM_ORDER_OPAQUE] = -1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]            = -1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER_RITUALS]    = -1;
    }
};

const DragonstormProvider& DsProvUo()
{
    static const DragonstormProvider p;
    return p;
}

// A pre-combat main on `mountains` untapped Mountains, land drop made, the given hand, a library of
// Mountains (Apex exiles nothing castable).
GameState BoardUo(int mountains, const std::vector<std::string>& hand, int turn = 4)
{
    EnsureCardsUo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &DsProvUo();
    for (int k = 0; k < mountains; ++k) { PutUo(s, "Mountain", 300 + k); }
    int num = 10;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardUo(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardUo("Mountain", 900 + k)); }
    return s;
}

// A hand-cast action priced at the CURRENT board (EffectiveCost, as the enumerator stamps it), with its
// ritual float (splice-scaled) -- the fields every order function reads.
Action CastUo(const GameState& s, const std::string& name, int splice = 0)
{
    const CardDefinition& d = DefUo(name);
    Action a;
    a.kind        = Action::Kind::CastFromHand;
    a.card_name   = name;
    a.def         = &d;
    a.splice_count = splice;
    a.cost        = EffectiveSpellCost(d, s, splice + 1);
    a.card_mv     = d.card.m_mana_cost.ManaValue();
    if (IsManaRitual(d)) { a.ritual_float = RitualFloatAmount(s, d, 0, splice + 1); }
    a.max_casts_after = d.params.max_casts_after;
    return a;
}

std::vector<std::string> NamesUo(const std::vector<Action>& acts, const std::vector<int>& ord)
{
    std::vector<std::string> v;
    for (int i : ord)
    {
        std::string n = acts[i].card_name.str();
        if (acts[i].splice_count > 0) { n += "+s" + std::to_string(acts[i].splice_count); }
        v.push_back(n);
    }
    return v;
}

// The order an apply site realises: comparator sort, the deck's arrangement, the payable fallback.
std::vector<int> RealisedUo(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> ord(acts.size());
    for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    ApplyPayableCastOrder(s, acts, ord);
    return ord;
}

}   // namespace

TEST_CASE("Dragonstorm USER order: the rank lays the deck's cards out in the USER's list; lever off = generic tiers")
{
    EnsureCardsUo();
    const GameState s = BoardUo(0, {});
    const std::vector<std::string> list = { kMed, kRite, kPyr, kDesp, kSeeth, kApex, kScour, kIren, kStorm };
    {
        UoArms arms(true);
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
        {
            CAPTURE(list[i]); CAPTURE(list[i + 1]);
            CHECK(DsProvUo().CastOrderRank(s, DefUo(list[i])) < DsProvUo().CastOrderRank(s, DefUo(list[i + 1])));
        }
        // Every Dragon in the list is a closer, ranked with Scourge.
        for (const char* d : { "Lathliss, Dragon Queen", "Utvara Hellkite", "Dragonlord Kolaghan",
                               "Karrthus, Tyrant of Jund" })
        { CHECK(DsProvUo().CastOrderRank(s, DefUo(d)) == DsProvUo().CastOrderRank(s, DefUo(kScour))); }
    }
    {
        UoArms arms(false);
        const GenericProvider generic;
        for (const std::string& n : list)
        { CAPTURE(n); CHECK(DsProvUo().CastOrderRank(s, DefUo(n)) == generic.CastOrderRank(s, DefUo(n))); }
        // The generic tiers the USER's order replaces: Medallion after the rituals, Irencrag before Apex,
        // a hard-cast Dragon before the rituals.
        CHECK(DsProvUo().CastOrderRank(s, DefUo(kMed))   > DsProvUo().CastOrderRank(s, DefUo(kRite)));
        CHECK(DsProvUo().CastOrderRank(s, DefUo(kIren))  < DsProvUo().CastOrderRank(s, DefUo(kApex)));
        CHECK(DsProvUo().CastOrderRank(s, DefUo(kScour)) < DsProvUo().CastOrderRank(s, DefUo(kRite)));
    }
}

TEST_CASE("Dragonstorm USER order: the comparator sorts a shuffled go-off set; a spliced Desperate goes after Seething")
{
    // Plenty of mana (twelve Mountains) so the payable fallback never has a say.
    const GameState s = BoardUo(12, { kStorm, kIren, kSeeth, kDesp, kDesp, kDesp, kPyr, kRite, kScour, kApex });
    std::vector<Action> acts;
    for (const char* n : { kStorm, kIren, kScour, kApex, kSeeth, kPyr, kRite }) { acts.push_back(CastUo(s, n)); }
    acts.push_back(CastUo(s, kDesp, 1));
    acts.push_back(CastUo(s, kDesp, 2));
    acts.push_back(CastUo(s, kDesp, 0));
    {
        UoArms arms(true);
        std::vector<int> ord(acts.size());
        for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
        // Comparator alone: the plain Desperate before Seething, the spliced ones after it (more splices first).
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kRite, kPyr, kDesp, kSeeth, "Desperate Ritual+s2",
                                                              "Desperate Ritual+s1", kApex, kScour, kIren, kStorm });
        // ...and the arrangement keeps the splice LEGAL: three copies in hand, the +2 cast reveals both others,
        // so the plain copy cannot be cast before it -- it moves to just after the last spliced cast.
        ApplyDeckCastArrangement(s, acts, ord);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kRite, kPyr, kSeeth, "Desperate Ritual+s2",
                                                              "Desperate Ritual+s1", kDesp, kApex, kScour, kIren,
                                                              kStorm });
    }
    {
        UoArms arms(false);   // the generic tiers: the Dragon first, Irencrag before Apex
        std::vector<int> ord(acts.size());
        for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
        const std::vector<std::string> names = NamesUo(acts, ord);
        CHECK(names.front() == kScour);
        CHECK(std::find(names.begin(), names.end(), kIren) < std::find(names.begin(), names.end(), kApex));
    }
}

TEST_CASE("Dragonstorm USER order: a plain Desperate keeps its slot before Seething when a spare copy stays in hand")
{
    // Three copies in hand, the plan casts two (one splicing one): the third copy stays in hand to be revealed,
    // so the plain cast keeps the USER's slot before Seething Song.
    const GameState s = BoardUo(8, { kDesp, kDesp, kDesp, kSeeth, kRite });
    std::vector<Action> acts = { CastUo(s, kDesp, 1), CastUo(s, kSeeth), CastUo(s, kDesp, 0), CastUo(s, kRite) };
    UoArms arms(true);
    CHECK(NamesUo(acts, RealisedUo(s, acts))
          == std::vector<std::string>{ kRite, kDesp, kSeeth, "Desperate Ritual+s1" });
    // Two copies only: the spliced cast reveals the other, so the plain one follows it.
    const GameState s2 = BoardUo(8, { kDesp, kDesp, kSeeth, kRite });
    std::vector<Action> acts2 = { CastUo(s2, kDesp, 1), CastUo(s2, kSeeth), CastUo(s2, kDesp, 0), CastUo(s2, kRite) };
    CHECK(NamesUo(acts2, RealisedUo(s2, acts2))
          == std::vector<std::string>{ kRite, kSeeth, "Desperate Ritual+s1", kDesp });
}

TEST_CASE("Dragonstorm USER order: Ruby Medallion at its EARLIEST PAYABLE slot -- unpayable first, payable after one ritual")
{
    // Two Mountains. Medallion first spends both and Rite of Flame cannot be paid. Rite first floats RR:
    // 1 Mountain + RR = 3, Medallion (2) leaves 1, Pyretic {1}{R}-1 = {R} -> 3, Seething {2}{R}-1 -> 6,
    // Scourge {2}{R}{R}{R}-1 = 4 -> 2. Earliest payable slot: 1 (after one ritual).
    const GameState s = BoardUo(2, { kMed, kRite, kPyr, kSeeth, kScour });
    std::vector<Action> acts;
    for (const char* n : { kScour, kSeeth, kPyr, kMed, kRite }) { acts.push_back(CastUo(s, n)); }
    {
        UoArms arms(true);
        std::vector<int> ord(acts.size());
        for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
        REQUIRE(NamesUo(acts, ord) == std::vector<std::string>{ kMed, kRite, kPyr, kSeeth, kScour });
        CHECK(ExactOrderFirstUnpayable(s, acts, ord) == 1);   // Medallion first: Rite cannot be paid
        ApplyDeckCastArrangement(s, acts, ord);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kRite, kMed, kPyr, kSeeth, kScour });
        CHECK(ExactOrderFirstUnpayable(s, acts, ord) == -1);
        const std::vector<int> arranged = ord;
        ApplyPayableCastOrder(s, acts, ord);   // the arranged order pays: the fallback leaves it alone
        CHECK(ord == arranged);
    }
    {
        // What the payable-order fallback ALONE does with "Medallion first" (no arrangement, the plain walk):
        // its walk never credits the Medallion's discount, so it cannot see that slot 1 pays the line -- the
        // order-free bound (13 to pay against 2 + 10 float) says NO permutation pays and it returns the order
        // untouched: Medallion first, the rest stranded. It does NOT produce the earliest payable slot.
        UoArms arms(false);
        std::vector<int> ord;
        for (const char* n : { kMed, kRite, kPyr, kSeeth, kScour })
        {
            for (int i = 0; i < static_cast<int>(acts.size()); ++i)
            { if (acts[i].card_name.str() == n) { ord.push_back(i); break; } }
        }
        ApplyPayableCastOrder(s, acts, ord);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kMed, kRite, kPyr, kSeeth, kScour });
    }
}

TEST_CASE("Dragonstorm USER order, apply level: the Medallion line casts every spell; the generic order strands the Dragon")
{
    // The rollout apply (ApplyPlanDirect) -- the executor's lockstep twin through the same shared order
    // functions. A clean set (no breakpoint card), in raw plan order Scourge, Seething, Pyretic, Medallion, Rite.
    for (const bool lever : { true, false })
    {
        CAPTURE(lever);
        UoArms arms(lever);
        GameState s = BoardUo(2, { kMed, kRite, kPyr, kSeeth, kScour });
        TurnSolver::Plan plan;
        for (const char* n : { kScour, kSeeth, kPyr, kMed, kRite }) { plan.actions.push_back(CastUo(s, n)); }
        plan.land_decided = true;
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        if (lever)
        {
            for (const char* n : { kMed, kRite, kPyr, kSeeth, kScour }) { CAPTURE(n); CHECK(InHandUo(s, n) == 0); }
            CHECK(OnBattlefieldUo(s, kScour) == 1);
            CHECK(OnBattlefieldUo(s, kMed) == 1);
        }
        else
        {
            // Generic tiers: Scourge (creature 10) first -- unpayable on two Mountains -- and no order the
            // plain walk can find pays the whole line, so the Dragon is never cast.
            CHECK(InHandUo(s, kScour) == 1);
        }
    }
}

TEST_CASE("Dragonstorm USER order: an Apex set holding Irencrag casts Apex BEFORE Irencrag; Irencrag -> Apex is refused")
{
    // Five Mountains: Rite (->6), Pyretic (->7), Desperate (->8), Seething (->10), Apex (10 -> 0, +10),
    // Irencrag (4 -> 6, +7 = 13), Dragonstorm (9). The golden line: rituals, Apex, Irencrag, Dragonstorm.
    const GameState s = BoardUo(5, { kStorm, kIren, kApex, kSeeth, kDesp, kPyr, kRite }, 5);
    std::vector<Action> acts;
    for (const char* n : { kStorm, kIren, kApex, kSeeth, kDesp, kPyr, kRite }) { acts.push_back(CastUo(s, n)); }
    CHECK(OrderingOpaque(kApex));
    {
        UoArms arms(true);
        const std::vector<int> ord = RealisedUo(s, acts);
        CHECK(NamesUo(acts, ord) == std::vector<std::string>{ kRite, kPyr, kDesp, kSeeth, kApex, kIren, kStorm });
        CHECK(ExactOrderFirstUnpayable(s, acts, ord) == -1);
        // The trap order: Irencrag's "one more spell" is Apex -- the exact walk refuses it at Apex's slot.
        std::vector<int> trap = ord;
        std::swap(trap[4], trap[5]);
        REQUIRE(NamesUo(acts, trap)[4] == kIren);
        CHECK(ExactOrderFirstUnpayable(s, acts, trap) == 5);
    }
    {
        UoArms arms(false);   // the generic tiers: Irencrag (18) before Apex (20) -- the trap
        std::vector<int> ord(acts.size());
        for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
        const std::vector<std::string> names = NamesUo(acts, ord);
        CHECK(std::find(names.begin(), names.end(), kIren) < std::find(names.begin(), names.end(), kApex));
    }
}

TEST_CASE("Dragonstorm USER order: Irencrag goes second-to-last before a lone Dragon closer, and the line pays")
{
    // Two Mountains: Rite (->3), Seething (->5), Irencrag (4 -> 1, +7 = 8), Scourge (5). Without Dragonstorm the
    // rank leaves Irencrag last behind the Dragon; the arrangement puts it in front of the closer.
    UoArms arms(true);
    GameState s = BoardUo(2, { kScour, kIren, kSeeth, kRite });
    std::vector<Action> acts;
    for (const char* n : { kScour, kIren, kSeeth, kRite }) { acts.push_back(CastUo(s, n)); }
    CHECK(NamesUo(acts, RealisedUo(s, acts)) == std::vector<std::string>{ kRite, kSeeth, kIren, kScour });
    TurnSolver::Plan plan;
    plan.actions = acts;
    plan.land_decided = true;
    TurnSolver::ApplyPlan(s, plan, true);
    CHECK(OnBattlefieldUo(s, kScour) == 1);
    CHECK(InHandUo(s, kIren) == 0);
}

TEST_CASE("Dragonstorm USER order: the searched orderings are the canonical order (+ the splice fallback); lever off -> the engine's")
{
    const GameState s = BoardUo(2, { kMed, kRite, kPyr, kSeeth, kScour });
    std::vector<Action> acts;
    for (const char* n : { kScour, kSeeth, kPyr, kMed, kRite }) { acts.push_back(CastUo(s, n)); }
    {
        UoArms arms(true);
        std::vector<std::vector<int>> out;
        REQUIRE(DsProvUo().CastOrderingCandidates(s, acts, acts, out));
        REQUIRE(out.size() == 1);   // no spliced Desperate: no fallback variant
        CHECK(NamesUo(acts, out[0]) == std::vector<std::string>{ kRite, kMed, kPyr, kSeeth, kScour });
    }
    {
        UoArms arms(false);
        std::vector<std::vector<int>> out;
        CHECK_FALSE(DsProvUo().CastOrderingCandidates(s, acts, acts, out));
    }
    {
        // The splice fallback (USER rule 5: individual-before is the fallback): the spliced Desperate in
        // front of Seething Song as a second candidate.
        UoArms arms(true);
        const GameState s3 = BoardUo(8, { kDesp, kDesp, kDesp, kSeeth, kRite });
        std::vector<Action> a3 = { CastUo(s3, kDesp, 1), CastUo(s3, kSeeth), CastUo(s3, kRite) };
        std::vector<std::vector<int>> out;
        REQUIRE(DsProvUo().CastOrderingCandidates(s3, a3, a3, out));
        REQUIRE(out.size() == 2);
        CHECK(NamesUo(a3, out[0]) == std::vector<std::string>{ kRite, kSeeth, "Desperate Ritual+s1" });
        CHECK(NamesUo(a3, out[1]) == std::vector<std::string>{ kRite, "Desperate Ritual+s1", kSeeth });
    }
}

TEST_CASE("Dragonstorm USER order: after Irencrag resolves only the closer is offered; a spent cap offers nothing")
{
    GameState s = BoardUo(4, { kApex, kRite, kMed, kStorm, kScour });
    s.casts_remaining_this_turn = 1;   // Irencrag resolved: one more spell
    {
        UoArms arms(true);
        CHECK(DsProvUo().CastUnderCastCap(s, DefUo(kStorm)));
        CHECK(DsProvUo().CastUnderCastCap(s, DefUo(kScour)));
        CHECK_FALSE(DsProvUo().CastUnderCastCap(s, DefUo(kApex)));   // never Irencrag -> Apex
        CHECK_FALSE(DsProvUo().CastUnderCastCap(s, DefUo(kRite)));   // a ritual would waste the slot
        CHECK_FALSE(DsProvUo().CastUnderCastCap(s, DefUo(kMed)));    // never after Irencrag
        s.casts_remaining_this_turn = 0;
        CHECK_FALSE(DsProvUo().CastUnderCastCap(s, DefUo(kStorm)));
        s.casts_remaining_this_turn = 1;
    }
    {
        UoArms arms(false);
        for (const char* n : { kStorm, kScour, kApex, kRite, kMed }) { CAPTURE(n); CHECK(DsProvUo().CastUnderCastCap(s, DefUo(n))); }
    }
}

TEST_CASE("Dragonstorm USER order: a subset whose Apex is payable only after Irencrag (the trap) is not a line")
{
    // Seven Mountains: Apex (10) cannot be paid before Irencrag (7 -> 3 + 7 = 10 only after it).
    const GameState s = BoardUo(7, { kApex, kIren });
    std::vector<Action> cands = { CastUo(s, kApex), CastUo(s, kIren) };
    const std::vector<int> sel = { 0, 1 };
    {
        UoArms arms(true);
        CHECK_FALSE(DsProvUo().OrderedSubsetRealisable(s, cands, sel));
    }
    {
        UoArms arms(false);
        CHECK(DsProvUo().OrderedSubsetRealisable(s, cands, sel));
    }
    // The golden line on five Mountains -- rituals, Apex, Irencrag, Dragonstorm -- is realisable.
    const GameState s5 = BoardUo(5, { kStorm, kIren, kApex, kSeeth, kDesp, kPyr, kRite }, 5);
    std::vector<Action> c5;
    for (const char* n : { kStorm, kIren, kApex, kSeeth, kDesp, kPyr, kRite }) { c5.push_back(CastUo(s5, n)); }
    const std::vector<int> all = { 0, 1, 2, 3, 4, 5, 6 };
    UoArms arms(true);
    CHECK(DsProvUo().OrderedSubsetRealisable(s5, c5, all));
}

TEST_CASE("Dragonstorm USER order: the payable fallback never buys payability with an illegal splice")
{
    // Two Desperate Rituals in hand, the plan casts one plain and one splicing the other. The spliced cast
    // must come FIRST (its revealed copy has to be in hand) -- it costs {2}{R}{R} and three mana cannot pay
    // it. The plain-first order pays, but only by splicing a copy that has already left the hand.
    const GameState s = BoardUo(3, { kDesp, kDesp, kStorm });
    std::vector<Action> acts = { CastUo(s, kDesp, 0), CastUo(s, kDesp, 1) };
    UoArms arms(true);
    const std::vector<int> plain_first = { 0, 1 };
    CHECK(ExactOrderFirstUnpayable(s, acts, plain_first) == 1);   // the splice is illegal there
    const std::vector<int> ord = RealisedUo(s, acts);
    CHECK(NamesUo(acts, ord) == std::vector<std::string>{ "Desperate Ritual+s1", kDesp });
    // ...and as a SUBSET it is no line: the only order that pays is the illegal one.
    CHECK_FALSE(DsProvUo().OrderedSubsetRealisable(s, acts, std::vector<int>{ 0, 1 }));
    // With four Mountains the legal order pays (spliced first: 4 -> 6, then the plain copy: 2 -> +3).
    const GameState s4 = BoardUo(4, { kDesp, kDesp, kStorm });
    std::vector<Action> a4 = { CastUo(s4, kDesp, 0), CastUo(s4, kDesp, 1) };
    CHECK(DsProvUo().OrderedSubsetRealisable(s4, a4, std::vector<int>{ 0, 1 }));
    {
        UoArms off(false);
        CHECK(DsProvUo().OrderedSubsetRealisable(s, acts, std::vector<int>{ 0, 1 }));
    }
}
