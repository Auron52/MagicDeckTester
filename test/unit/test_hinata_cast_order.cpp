// Unit cover for the USER's settled Hinata2 cast order (MTG_HINATA_ORDER_FULL; docs/design/hinata-cast-order.md).
//
// USER 2026-10-14, the settled order (TOTAL, all main 1):
//   land > Sol Ring > Hinata > Reality Spasm > Gamble > Preordain > Ponder > Expressive Iteration > Ornithopter
//   > [Irencrag Feat] > Soulfire Eruption > Magma Opus > Crackle with Power > the never-cast four.
// "Let's fully order the cards in each list inasmuch as possible. I don't like the randomness of choosing the
// order." -- no two different names compare equal, and a tie falls back to the NAME, never to plan/hand order.
// Irencrag Feat (USER, verbatim): "Irencrag needs to be castable after Soulfire and after Magma Opus, since it
// needs to go second last. There has to be flexibility around that." / "So, in reality the Irencrag can go in
// any of three spots depending on what other spells are cast, but it is always second last." / "So we can
// restrict it using that." -> a SET-LEVEL placement (HinataProvider::ArrangeCastOrder): Irencrag immediately
// before whichever payoff (Soulfire / Opus / Crackle) is the plan's last cast; a plan holding Irencrag where it
// cannot be second-last before one of them is pruned (the waste gate, now judging the arranged follower).
//
// What is pinned:
//   1. the rank lays the deck's cards out in the settled list, strictly; lever off = the generic tiers;
//   2. the realised order (comparator + arrangement) does not depend on the hand / plan order -- every
//      permutation of an 8-card set realises the same sequence, and a tie arm (MTG_HINATA_DORK_TIE) breaks by
//      name;
//   3. Irencrag second-last before each payoff combination;
//   4. the walks count Irencrag's follower in the SAME arranged order: {Soulfire, Irencrag, Magma Opus} is a
//      line (it was two followers by rank), the line pays when applied, and a pre-set the board cannot pay
//      without the float is refused;
//   5. the prune: Irencrag with no payoff to precede is never offered, and the follower must need the float
//      (rule 3) unless the plan is lethal;
//   6. the opt-in (OrderOpaqueCastsByRank): a plan holding a cantrip plays the rank order, so the cantrip-first
//      partition's hoist of Irencrag (it has ritual_float) ahead of Ponder is undone.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsHo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHo(const std::string& name)
{
    EnsureCardsHo();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// The levers this file reads, forced for this thread and restored on scope exit.
struct HoArms
{
    std::vector<std::pair<int, std::int8_t>> saved;
    void Set(int slot, bool on) { saved.emplace_back(slot, heurarm::t_arm[slot]); heurarm::t_arm[slot] = on ? 1 : 0; }
    HoArms(bool order, bool second_last = true)
    {
        Set(heurarm::HINATA_ORDER_FULL, order);
        Set(heurarm::HINATA_IREN_SECOND_LAST, second_last);
    }
    ~HoArms() { for (auto it = saved.rbegin(); it != saved.rend(); ++it) { heurarm::t_arm[it->first] = it->second; } }
};

const HinataProvider& HoProv()
{
    static const HinataProvider p;
    return p;
}

// `board` on our side (untapped, not sick), `hand` in hand, land drop spent, pre-combat. `salt` keeps the
// per-thread Solve memo (keyed on the state) from serving one arm's answer to another.
GameState BoardHo(const std::vector<std::string>& board, const std::vector<std::string>& hand, int opp_life,
                  int salt)
{
    EnsureCardsHo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20 + salt;
    s.players[1].life     = opp_life;
    s.m_provider          = &HoProv();
    int num = 1;
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefHo(n).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    for (const std::string& n : hand) { Card c = DefHo(n).card; c.m_number = num++; s.players[0].hand.push_back(c); }
    for (int k = 0; k < 10; ++k) { Card c = DefHo("Island").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    return s;
}

std::vector<std::string> Mountains(int n) { return std::vector<std::string>(static_cast<std::size_t>(n), "Mountain"); }

// A hand-cast action priced at the current board, with the fields every order function reads.
Action CastHo(const GameState& s, const std::string& name)
{
    const CardDefinition& d = DefHo(name);
    Action a;
    a.kind            = Action::Kind::CastFromHand;
    a.card_name       = name;
    a.def             = &d;
    a.cost            = EffectiveSpellCost(d, s, 1);
    a.card_mv         = d.card.m_mana_cost.ManaValue();
    if (IsManaRitual(d)) { a.ritual_float = RitualFloatAmount(s, d, 0, 1); }
    a.max_casts_after = d.params.max_casts_after;
    return a;
}

std::vector<Action> CastsHo(const GameState& s, const std::vector<std::string>& names)
{
    std::vector<Action> v;
    for (const std::string& n : names) { v.push_back(CastHo(s, n)); }
    return v;
}

std::vector<std::string> NamesHo(const std::vector<Action>& acts, const std::vector<int>& ord)
{
    std::vector<std::string> v;
    for (int i : ord) { v.push_back(acts[i].card_name.str()); }
    return v;
}

// The order both apply worlds realise for a set: the shared comparator, then the deck's arrangement.
std::vector<int> RealisedHo(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> ord(acts.size());
    for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    return ord;
}

std::vector<std::string> RealisedNamesHo(const GameState& s, const std::vector<std::string>& hand_order)
{
    const std::vector<Action> acts = CastsHo(s, hand_order);
    return NamesHo(acts, RealisedHo(s, acts));
}

bool CastsAllHo(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    for (const std::string& n : names)
    {
        bool found = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == n) { found = true; break; } }
        if (!found) { return false; }
    }
    return true;
}

int CountCastsHo(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
    return n;
}

// Does EnumerateMainPlans offer a plan casting EXACTLY these cards (and nothing else)?
bool OffersExactlyHo(const GameState& s, const std::vector<std::string>& names)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (CastsAllHo(p, names) && CountCastsHo(p) == static_cast<int>(names.size())) { return true; } }
    return false;
}

bool OffersAnyWithHo(const GameState& s, const std::string& name)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (CastsAllHo(p, { name })) { return true; } }
    return false;
}

TurnSolver::Plan GreedyHo(const GameState& s)
{
    return TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
}

int InHandHo(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

const char* kSol   = "Sol Ring";
const char* kHin   = "Hinata, Dawn-Crowned";
const char* kSpasm = "Reality Spasm";
const char* kGam   = "Gamble";
const char* kPre   = "Preordain";
const char* kPon   = "Ponder";
const char* kEI    = "Expressive Iteration";
const char* kOrni  = "Ornithopter of Paradise";
const char* kIren  = "Irencrag Feat";
const char* kSoul  = "Soulfire Eruption";
const char* kOpus  = "Magma Opus";
const char* kCrk   = "Crackle with Power";

std::vector<std::string> WithIsland(int mountains)
{
    std::vector<std::string> v = Mountains(mountains);
    v.push_back("Island");
    return v;
}

}   // namespace

TEST_CASE("Hinata settled order: the rank lays the deck out in the USER's list, strictly; lever off = generic tiers")
{
    const GameState s = BoardHo({}, {}, 20, 0);
    const std::vector<std::string> list = { kSol, kHin, kSpasm, kGam, kPre, kPon, kEI, kOrni, kIren, kSoul, kOpus, kCrk };
    const std::vector<std::string> inert = { "Icy Blast", "Memory Lapse", "Remand", "Distorting Wake" };
    {
        HoArms arms(true);
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
        {
            CAPTURE(list[i]); CAPTURE(list[i + 1]);
            CHECK(HoProv().CastOrderRank(s, DefHo(list[i])) < HoProv().CastOrderRank(s, DefHo(list[i + 1])));
        }
        for (const std::string& n : inert)
        { CAPTURE(n); CHECK(HoProv().CastOrderRank(s, DefHo(n)) > HoProv().CastOrderRank(s, DefHo(kCrk))); }
        // TOTAL: the comparator never calls two different names equal, even where the rank ties.
        for (const std::string& a : inert)
        {
            for (const std::string& b : inert)
            {
                if (a == b) { continue; }
                const Action x = CastHo(s, a), y = CastHo(s, b);
                CAPTURE(a); CAPTURE(b);
                CHECK(CastOrderLess(s, x, y) != CastOrderLess(s, y, x));
            }
        }
        CHECK(HoProv().OrderOpaqueCastsByRank());
        CHECK(OpaqueCastOrderActive(s));
        CHECK(HoProv().RestrictorFollowsArrangement(s));
    }
    {
        HoArms arms(false);
        const GenericProvider generic;
        for (const std::string& n : list)
        { CAPTURE(n); CHECK(HoProv().CastOrderRank(s, DefHo(n)) == generic.CastOrderRank(s, DefHo(n))); }
        CHECK_FALSE(HoProv().OrderOpaqueCastsByRank());
        CHECK_FALSE(HoProv().RestrictorFollowsArrangement(s));
        // Lever off: a tie keeps plan order (the generic tiers tie Gamble, Ponder and Preordain at 20).
        const Action g = CastHo(s, kGam), p = CastHo(s, kPon);
        CHECK_FALSE(CastOrderLess(s, g, p));
        CHECK_FALSE(CastOrderLess(s, p, g));
    }
    {
        // The scalar-only arm (placement slot off): Irencrag keeps its scalar spot, ahead of every payoff.
        HoArms arms(true, /*second_last=*/false);
        CHECK_FALSE(HoProv().RestrictorFollowsArrangement(s));
        CHECK(HoProv().CastOrderRank(s, DefHo(kIren)) < HoProv().CastOrderRank(s, DefHo(kSoul)));
    }
}

TEST_CASE("Hinata settled order: every hand order of a set realises the SAME sequence (total; name fallback on a tie)")
{
    const GameState s = BoardHo(Mountains(20), {}, 20, 1);
    std::vector<std::string> hand = { kCrk, kIren, kOrni, kEI, kPon, kPre, kGam, kHin };
    std::sort(hand.begin(), hand.end());
    {
        HoArms arms(true);
        const std::vector<std::string> want = { kHin, kGam, kPre, kPon, kEI, kOrni, kIren, kCrk };
        int perms = 0, bad = 0;
        do
        {
            ++perms;
            if (RealisedNamesHo(s, hand) != want) { ++bad; }
        } while (std::next_permutation(hand.begin(), hand.end()));
        CHECK(perms == 40320);
        CHECK(bad == 0);
    }
    {
        // A TIE arm (MTG_HINATA_DORK_TIE puts the dork on her rank): the tie breaks by NAME, never by hand order.
        HoArms arms(true);
        arms.Set(heurarm::HINATA_DORK_TIE, true);
        REQUIRE(HoProv().CastOrderRank(s, DefHo(kOrni)) == HoProv().CastOrderRank(s, DefHo(kHin)));
        std::vector<std::string> h = { kOrni, kHin, kGam };
        std::sort(h.begin(), h.end());
        do
        { CHECK(RealisedNamesHo(s, h) == std::vector<std::string>{ kHin, kOrni, kGam }); }
        while (std::next_permutation(h.begin(), h.end()));
    }
}

TEST_CASE("Hinata settled order: Irencrag is the SECOND-LAST cast, right before the plan's last payoff")
{
    const GameState s = BoardHo(Mountains(30), {}, 20, 2);
    HoArms arms(true);
    struct C { std::vector<std::string> hand, want; };
    const std::vector<C> cases = {
        { { kSoul, kIren },               { kIren, kSoul } },
        { { kOpus, kIren },               { kIren, kOpus } },
        { { kCrk, kIren },                { kIren, kCrk } },
        { { kCrk, kSoul, kIren },         { kSoul, kIren, kCrk } },
        { { kCrk, kOpus, kIren },         { kOpus, kIren, kCrk } },
        { { kCrk, kOpus, kSoul, kIren },  { kSoul, kOpus, kIren, kCrk } },
        { { kOpus, kSoul, kIren },        { kSoul, kIren, kOpus } },
        // the set-up casts all go ahead of it, whatever the hand order
        { { kCrk, kIren, kSoul, kOrni, kPon, kSpasm, kSol },
          { kSol, kSpasm, kPon, kOrni, kSoul, kIren, kCrk } },
        // no payoff to precede: it stays at its scalar spot (last); the subset walks prune such a plan
        { { kIren, kOrni, kPon },         { kPon, kOrni, kIren } },
    };
    for (const C& c : cases)
    {
        std::vector<std::string> h = c.hand;
        CAPTURE(c.want.back());
        CHECK(RealisedNamesHo(s, h) == c.want);
        std::reverse(h.begin(), h.end());
        CHECK(RealisedNamesHo(s, h) == c.want);
    }
    // The placement slot off: the scalar rank alone, Irencrag ahead of every payoff.
    HoArms off(true, /*second_last=*/false);
    CHECK(RealisedNamesHo(s, { kCrk, kSoul, kIren }) == std::vector<std::string>{ kIren, kSoul, kCrk });
}

TEST_CASE("Hinata settled order: the walks count Irencrag's follower in the ARRANGED order")
{
    // Thirteen Mountains + an Island: Soulfire ({6}{R}{R}{R}) and Irencrag ({1}{R}{R}{R}) both come off the
    // board, the Island and the float (seven red) pay Magma Opus ({6}{U}{R}). Opponent at 20, nothing lethal.
    // By RANK (Irencrag 18 < Soulfire 20 < Opus 21) the plan has TWO casts after Irencrag and is illegal;
    // arranged it is Soulfire -> Irencrag -> Opus, one follower -- and that follower NEEDED the float (the
    // board has one land left once Soulfire and Irencrag are paid), so the waste gate keeps it.
    {
        HoArms arms(true);
        const GameState s = BoardHo(WithIsland(13), { kSoul, kIren, kOpus }, 20, 3);
        CHECK(OffersExactlyHo(s, { kSoul, kIren, kOpus }));
    }
    {
        HoArms arms(true, /*second_last=*/false);   // the scalar 18: two followers by rank
        const GameState s = BoardHo(WithIsland(13), { kSoul, kIren, kOpus }, 20, 4);
        CHECK_FALSE(OffersExactlyHo(s, { kSoul, kIren, kOpus }));
    }
    {
        // ...and the apply realises it: every spell resolves (Irencrag's one allowed cast is the Opus).
        HoArms arms(true);
        GameState s = BoardHo(WithIsland(13), { kSoul, kIren, kOpus }, 20, 5);
        TurnSolver::Plan plan;
        plan.actions = CastsHo(s, { kOpus, kIren, kSoul });
        plan.land_decided = true;
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        CHECK(InHandHo(s, kSoul) == 0);
        CHECK(InHandHo(s, kIren) == 0);
        CHECK(InHandHo(s, kOpus) == 0);
    }
    {
        // The PRE-SET in the arranged order: on eleven Mountains + an Island, Soulfire + Irencrag (13) cannot
        // both come off the board, so every payable order casts Irencrag BEFORE Soulfire -- Soulfire then takes
        // his one allowed cast and the Opus is stranded. Refused (MTG_RESTRICTOR_ORDER_EXACT); the two-card
        // line it can pay stays.
        HoArms arms(true);
        const GameState s = BoardHo(WithIsland(11), { kSoul, kIren, kOpus }, 20, 6);
        CHECK_FALSE(OffersExactlyHo(s, { kSoul, kIren, kOpus }));
        CHECK(OffersExactlyHo(s, { kSoul, kIren }));
    }
}

TEST_CASE("Hinata settled order: Irencrag with no payoff to precede is pruned; the follower must need the float")
{
    HoArms arms(true);
    {
        // Irencrag + Ponder + Ornithopter, no payoff in hand: arranged, Irencrag is the last cast -- nothing
        // spends the seven red. No plan casts it, and it is never the greedy pick.
        const GameState s = BoardHo(WithIsland(7), { kIren, kPon, kOrni }, 20, 7);
        CHECK_FALSE(OffersAnyWithHo(s, kIren));
        CHECK_FALSE(CastsAllHo(GreedyHo(s), { kIren }));
        CHECK(OffersExactlyHo(s, { kPon, kOrni }));
    }
    {
        // Irencrag -> Soulfire on THIRTEEN Mountains: the board pays the Soulfire even after Irencrag, so the
        // float bought nothing (rule 3, USER 2026-09-01: "casting Irencrag to play Soulfire is pretty bad
        // value") -- pruned when not lethal (a library of Islands: Soulfire deals 0).
        const GameState s = BoardHo(Mountains(13), { kIren, kSoul }, 20, 8);
        CHECK_FALSE(OffersExactlyHo(s, { kIren, kSoul }));
        CHECK(OffersExactlyHo(s, { kSoul }));
    }
    {
        // ...on SIX Mountains the Soulfire is castable only off the float: Irencrag -> Soulfire is a line
        // (hold gi 664's T3 shape: Irencrag is the only way to cast the deck's dig).
        const GameState s = BoardHo(Mountains(6), { kIren, kSoul }, 20, 9);
        CHECK(OffersExactlyHo(s, { kIren, kSoul }));
    }
}

TEST_CASE("Hinata settled order: the opt-in binds the order inside cantrip plans -- no Irencrag hoist over Ponder")
{
    // The cantrip-first partition (searched play) sorts a cantrip plan [may-precede] -> [cantrips] -> [rest],
    // and Irencrag MAY precede (it has ritual_float): the plan's own order reads Irencrag, Ponder, Crackle --
    // Ponder would take Irencrag's one allowed cast. With the opt-in both apply worlds rank-sort the set and
    // arrange it, so the realised order is Ponder, Irencrag, Crackle -- the order the walks counted.
    HoArms arms(true);
    TurnSolver::SetSearchedPlay(true);
    const GameState s = BoardHo(WithIsland(8), { kCrk, kPon, kIren }, 15, 10);
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!CastsAllHo(p, { kPon, kIren, kCrk })) { continue; }
        seen = true;
        std::vector<int> ord;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        { if (p.actions[i].kind == Action::Kind::CastFromHand) { ord.push_back(i); } }
        // The plan's own (partition) order: the hazard the opt-in removes.
        CHECK(p.actions[ord.front()].card_name.str() == kIren);
        REQUIRE(OpaqueCastOrderActive(s));
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, p.actions[x], p.actions[y]); });
        ApplyDeckCastArrangement(s, p.actions, ord);
        CHECK(NamesHo(p.actions, ord) == std::vector<std::string>{ kPon, kIren, kCrk });
    }
    TurnSolver::SetSearchedPlay(false);
    CHECK(seen);
    // Lever off: no opt-in, the opaque set keeps its plan order.
    HoArms off(false);
    CHECK_FALSE(OpaqueCastOrderActive(s));
}

TEST_CASE("Hinata PROPOSED MTG_HINATA_SPASM_LATE: Reality Spasm at its latest payable slot before the payoff")
{
    // Two Islands + three Mountains: Gamble ({R}), Reality Spasm ({U}{U} at X = 0 here) and the Crackle ({R}{R})
    // all come off the board, so a Spasm cast AFTER Gamble refunds the Gamble's land too. Off: the scalar slot
    // (Spasm 7 before Gamble 8). On: Spasm moves to just before the payoff.
    const GameState s = BoardHo({ "Island", "Island", "Mountain", "Mountain", "Mountain" }, {}, 20, 11);
    std::vector<Action> acts = CastsHo(s, { kCrk, kGam, kSpasm });
    {
        HoArms arms(true);
        CHECK(NamesHo(acts, RealisedHo(s, acts)) == std::vector<std::string>{ kSpasm, kGam, kCrk });
    }
    {
        HoArms arms(true);
        arms.Set(heurarm::HINATA_SPASM_LATE, true);
        CHECK(NamesHo(acts, RealisedHo(s, acts)) == std::vector<std::string>{ kGam, kSpasm, kCrk });
        // ...and never past Irencrag: it stays ahead of the terminal, so Irencrag's follower is unchanged.
        std::vector<Action> a2 = CastsHo(s, { kCrk, kGam, kSpasm, kIren });
        const std::vector<std::string> got = NamesHo(a2, RealisedHo(s, a2));
        REQUIRE(got.size() == 4);
        CHECK(got[2] == kIren);
        CHECK(got[3] == kCrk);
    }
    {
        // FUNDING: on two Islands alone the Gamble's {R} exists only as the Spasm's output (credited here as
        // three red), so no later slot pays the line and the Spasm keeps its place ahead of the Gamble.
        const GameState s2 = BoardHo({ "Island", "Island" }, {}, 20, 12);
        std::vector<Action> a3 = CastsHo(s2, { kCrk, kGam, kSpasm });
        for (Action& a : a3)
        { if (a.card_name.str() == kSpasm) { a.ritual_float = 3; a.chosen_float_color = std::string("R"); } }
        HoArms arms(true);
        arms.Set(heurarm::HINATA_SPASM_LATE, true);
        CHECK(NamesHo(a3, RealisedHo(s2, a3)) == std::vector<std::string>{ kSpasm, kGam, kCrk });
    }
}
