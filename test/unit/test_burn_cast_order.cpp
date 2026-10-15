// The USER's burn ruling (2026-10-14; docs/design/burn-cast-order-2026-10-15.md), its three parts:
//   1. MTG_BURN_ORDER  -- the TOTAL cast order (land > Goblin Guide > Monastery Swiftspear > Lightning
//      Bolt > Shard Volley > Searing Blaze > Searing Blood > Skullcrack > Light Up the Stage > Eidolon of
//      the Great Revel), Shard Volley sequenced at its rank, the land drop's two exceptions;
//   2. MTG_BURN_M2_RULE (+ MTG_BURN_M2_NARROW) -- what main 2 may cast;
//   3. MTG_SPECTACLE_PLAN_CREDIT -- Swiftspear -> burn -> Light Up {R} becomes a plan.
// Every expected order below is the USER's list, written out by hand -- not read off the code -- and
// every lever is checked ON and OFF (the OFF arm is the control that must differ).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace
{

void EnsureCardsBurnO()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefB(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const char* kGG     = "Goblin Guide";
const char* kSS     = "Monastery Swiftspear";
const char* kBolt   = "Lightning Bolt";
const char* kVolley = "Shard Volley";
const char* kBlaze  = "Searing Blaze";
const char* kBlood  = "Searing Blood";
const char* kSkull  = "Skullcrack";
const char* kLuts   = "Light Up the Stage";
const char* kEid    = "Eidolon of the Great Revel";
const char* kMtn    = "Mountain";

// The USER's order, nonland part.
const std::vector<std::string> kUserOrder = { kGG, kSS, kBolt, kVolley, kBlaze, kBlood, kSkull, kLuts, kEid };

const BurnProvider& ProvB()
{
    static const BurnProvider p;
    return p;
}

// Pins one lever for a scope (heurarm per-thread override; -1 restores the env default).
struct Pin
{
    heurarm::Slot s;
    Pin(heurarm::Slot slot, bool on) : s(slot) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~Pin() { heurarm::t_arm[s] = -1; }
};

Card HandB(const std::string& name, int number)
{
    Card c = DefB(name).card;
    c.m_number = number;
    return c;
}

void PutB(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefB(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

// A burn state: the burn provider, `hand` in hand, `lands` untapped Mountains (+ `board` bodies) on our
// side, a Mountain library. `salt` (our life) keeps any per-thread memo from serving one arm to another.
GameState BurnState(const std::vector<std::string>& hand, int lands, const std::vector<std::string>& board,
                    int opp_life, int turn, int salt, bool land_played)
{
    EnsureCardsBurnO();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.m_provider          = &ProvB();
    s.players[0].life     = 20 + salt;
    s.players[1].life     = opp_life;
    s.uses_second_main    = true;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(HandB(n, num++)); }
    for (int k = 0; k < lands; ++k) { PutB(s, kMtn, num++); }
    for (const std::string& n : board) { PutB(s, n, num++); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(HandB(kMtn, 100 + k)); }
    s.players[0].lands_played_this_turn = land_played ? 1 : 0;
    return s;
}

Action CastOf(const std::string& name, int hand_index)
{
    const CardDefinition& d = DefB(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.hand_index     = hand_index;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.sacrifice_land = d.params.sacrifice_land;
    a.has_spectacle  = d.params.spectacle_cost.has_value();
    a.is_noncreature = !d.card.IsCreature();
    return a;
}

std::vector<std::string> Realised(const GameState& s, const std::vector<std::string>& names)
{
    TurnSolver::Plan p;
    for (int i = 0; i < static_cast<int>(names.size()); ++i) { p.actions.push_back(CastOf(names[static_cast<std::size_t>(i)], i)); }
    return TurnSolver::PlanCastOrderNames(s, p);
}

bool PlanCasts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("burn order: the USER's total order, by params with a name tie-break, independent of hand order")
{
    const GameState s = BurnState({}, 3, {}, 20, 4, 0, true);
    Pin o(heurarm::BURN_ORDER, true);
    // The ranks are tiers from params: haste creature 10+mv, burn 20+2*mv, spectacle 30, Eidolon 40.
    CHECK(ProvB().CastOrderRank(s, DefB(kGG)) == 11);
    CHECK(ProvB().CastOrderRank(s, DefB(kSS)) == 11);
    CHECK(ProvB().CastOrderRank(s, DefB(kBolt)) == 22);
    CHECK(ProvB().CastOrderRank(s, DefB(kVolley)) == 22);
    CHECK(ProvB().CastOrderRank(s, DefB(kBlaze)) == 24);
    CHECK(ProvB().CastOrderRank(s, DefB(kBlood)) == 24);
    CHECK(ProvB().CastOrderRank(s, DefB(kSkull)) == 24);
    CHECK(ProvB().CastOrderRank(s, DefB(kLuts)) == 30);
    CHECK(ProvB().CastOrderRank(s, DefB(kEid)) == 40);
    CHECK(ProvB().SacLandCastsInOrder());
    CHECK(ProvB().OrderOpaqueCastsByRank());
    CHECK(ProvB().CastOrderTierName(30) != nullptr);

    // Every permutation of the nine names realises the same order (a seeded sample of 9! plus every
    // permutation of a five-card subset that holds every tie).
    CHECK(Realised(s, kUserOrder) == kUserOrder);
    std::vector<std::string> rev(kUserOrder.rbegin(), kUserOrder.rend());
    CHECK(Realised(s, rev) == kUserOrder);
    std::mt19937 rng(11600001u);
    for (int k = 0; k < 200; ++k)
    {
        std::vector<std::string> h = kUserOrder;
        std::shuffle(h.begin(), h.end(), rng);
        REQUIRE(Realised(s, h) == kUserOrder);
    }
    std::vector<std::string> sub = { kBlaze, kBolt, kGG, kSkull, kSS };   // sorted for next_permutation
    std::sort(sub.begin(), sub.end());
    const std::vector<std::string> want = { kGG, kSS, kBolt, kBlaze, kSkull };
    int perms = 0;
    do { REQUIRE(Realised(s, sub) == want); ++perms; } while (std::next_permutation(sub.begin(), sub.end()));
    CHECK(perms == 120);
    // Duplicates keep their place among themselves and the order holds around them.
    CHECK(Realised(s, { kEid, kBolt, kSS, kBolt, kVolley, kGG })
          == std::vector<std::string>({ kGG, kSS, kBolt, kBolt, kVolley, kEid }));
}

TEST_CASE("burn order, the PROPOSED amendment MTG_BURN_BLOOD_FIRST: Searing Blood before Searing Blaze only when Blood alone cannot kill")
{
    Pin o(heurarm::BURN_ORDER, true);
    auto with_opp = [](const char* body, int salt)
    {
        GameState s = BurnState({}, 3, {}, 20, 4, salt, true);
        PutB(s, body, 90);
        s.battlefield.back().controller_index = 1;
        s.battlefield.back().owner_index = 1;
        return s;
    };
    const GameState big   = with_opp("Boros Reckoner", 30);   // a 3/3: Blood alone does not kill it
    const GameState small = with_opp(kGG, 31);                // a 2/2: Blood alone kills it
    const GameState none  = BurnState({}, 3, {}, 20, 4, 32, true);
    const std::vector<std::string> blood_first = { kGG, kSS, kBolt, kVolley, kBlood, kBlaze, kSkull, kLuts, kEid };
    std::vector<std::string> rev(kUserOrder.rbegin(), kUserOrder.rend());
    {
        Pin b(heurarm::BURN_BLOOD_FIRST, true);
        CHECK(Realised(big, rev) == blood_first);
        CHECK(Realised(big, kUserOrder) == blood_first);
        CHECK(Realised(small, rev) == kUserOrder);   // Blaze first: Blood can still pump our own Swiftspear
        CHECK(Realised(none, rev) == kUserOrder);
    }
    Pin b(heurarm::BURN_BLOOD_FIRST, false);
    CHECK(Realised(big, rev) == kUserOrder);          // the USER's list, verbatim
}

TEST_CASE("burn order OFF: the generic tiers, Shard Volley trails, ties keep plan order (the control)")
{
    const GameState s = BurnState({}, 3, {}, 20, 4, 1, true);
    Pin o(heurarm::BURN_ORDER, false);
    CHECK_FALSE(ProvB().SacLandCastsInOrder());
    CHECK_FALSE(ProvB().OrderOpaqueCastsByRank());
    CHECK(ProvB().CastOrderRank(s, DefB(kGG)) == ProvB().CastOrderRank(s, DefB(kSS)));
    // Plan order breaks the creature tie and the burn tie; Volley goes last of all, Eidolon included.
    CHECK(Realised(s, { kSS, kVolley, kEid, kSkull, kBolt, kGG })
          == std::vector<std::string>({ kSS, kGG, kSkull, kBolt, kEid, kVolley }));
    // ...so a permuted hand realises a DIFFERENT order -- what the total order removes.
    CHECK(Realised(s, { kGG, kVolley, kEid, kBolt, kSkull, kSS })
          != Realised(s, { kSS, kVolley, kEid, kSkull, kBolt, kGG }));
}

TEST_CASE("burn order: the land drop's two exceptions as equal-plan keys")
{
    Pin o(heurarm::BURN_ORDER, true);
    const GameState s = BurnState({ kMtn, kBlaze, kLuts, kBolt }, 2, {}, 20, 3, 2, false);
    const std::vector<Action> blaze = { CastOf(kBlaze, 1) };
    const std::vector<Action> luts  = { CastOf(kBolt, 3), CastOf(kLuts, 2) };
    const std::vector<Action> bolt  = { CastOf(kBolt, 3) };
    // Land first...
    CHECK(ProvB().LandPlanTieKey(s, bolt, true, false) == 1);
    CHECK(ProvB().LandPlanTieKey(s, bolt, false, false) == 2);
    // ...before Searing Blaze whenever Blaze is cast with a land in hand (even while banking)...
    CHECK(ProvB().LandPlanTieKey(s, blaze, false, false) == 2);
    CHECK(ProvB().LandPlanTieKey(s, blaze, false, true) == 2);
    // ...after Light Up the Stage when the plan pays for it without the land.
    CHECK(ProvB().LandPlanTieKey(s, luts, false, false) == 0);
    // The bank (PreferHoldLandDrop) keeps an ordinary holder first, as before.
    CHECK(ProvB().LandPlanTieKey(s, bolt, false, true) == 0);
    Pin off(heurarm::BURN_ORDER, false);
    CHECK(ProvB().LandPlanTieKey(s, luts, false, false) == -1);
}

TEST_CASE("burn order: the depth-0 land hold after a self-paid Light Up the Stage")
{
    Pin o(heurarm::BURN_ORDER, true);
    {   // three lands pay Light Up at full cost: hold the drop
        const GameState s = BurnState({ kMtn, kLuts }, 3, {}, 20, 4, 3, false);
        CHECK(ProvB().LandDropAfterDrawEngine(s, 0));
    }
    {   // ...but never with a Searing Blaze in hand (land before Blaze)
        const GameState s = BurnState({ kMtn, kLuts, kBlaze }, 3, {}, 20, 4, 4, false);
        CHECK_FALSE(ProvB().LandDropAfterDrawEngine(s, 0));
    }
    {   // two lands: Bolt + Light Up {R} pays only under the spectacle credit
        const GameState s = BurnState({ kMtn, kLuts, kBolt }, 2, {}, 20, 3, 5, false);
        {
            Pin c(heurarm::SPECTACLE_PLAN_CREDIT, true);
            CHECK(ProvB().LandDropAfterDrawEngine(s, 0));
        }
        Pin c(heurarm::SPECTACLE_PLAN_CREDIT, false);
        CHECK_FALSE(ProvB().LandDropAfterDrawEngine(s, 0));
    }
    {   // two lands, Swiftspear too: the land's mana is needed before Light Up -> land first
        const GameState s = BurnState({ kMtn, kLuts, kBolt, kSS }, 2, {}, 20, 3, 6, false);
        Pin c(heurarm::SPECTACLE_PLAN_CREDIT, true);
        CHECK_FALSE(ProvB().LandDropAfterDrawEngine(s, 0));
    }
    Pin off(heurarm::BURN_ORDER, false);
    const GameState s = BurnState({ kMtn, kLuts }, 3, {}, 20, 4, 7, false);
    CHECK_FALSE(ProvB().LandDropAfterDrawEngine(s, 0));
}

TEST_CASE("burn main-2 rule: Light Up, Eidolon, Blaze with the drop missing, and what is played off a main-2 Light Up")
{
    Pin r(heurarm::BURN_M2_RULE, true);
    CHECK(ProvB().RestrictsMain2Casts());
    GameState s = BurnState({ kBolt, kLuts, kEid, kBlaze, kSkull, kVolley, kSS }, 3, {}, 20, 4, 8, true);
    auto allowed = [&](int hi) { return ProvB().Main2CastAllowed(s, CastOf(s.players[0].hand[static_cast<std::size_t>(hi)].m_name, hi)); };
    CHECK_FALSE(allowed(0));   // Bolt
    CHECK(allowed(1));         // Light Up the Stage
    CHECK(allowed(2));         // Eidolon
    CHECK_FALSE(allowed(3));   // Searing Blaze -- a land has entered this turn
    CHECK_FALSE(allowed(4));   // Skullcrack
    CHECK_FALSE(allowed(5));   // Shard Volley
    CHECK_FALSE(allowed(6));   // Swiftspear
    s.players[0].lands_played_this_turn = 0;
    CHECK(allowed(3));         // Searing Blaze while the drop is missing

    // A main-2 Light Up: the stamp records what was already staged (an old exiled Skullcrack).
    s.players[0].hand[4].m_is_staged = true;
    s.players[0].hand[4].m_staged_expiry = s.turn_number;
    NoteMain2StagerCast(s, DefB(kLuts));
    CHECK(s.m2_stage_turn == s.turn_number);
    // Something the Light Up exiled.
    Card ex = HandB(kBolt, 50);
    ex.m_is_staged = true;
    ex.m_staged_expiry = s.turn_number + 1;
    s.players[0].hand.push_back(ex);
    const int ex_i = static_cast<int>(s.players[0].hand.size()) - 1;
    // The broad reading (default): everything after it.
    CHECK(allowed(0));
    CHECK(allowed(4));
    CHECK(allowed(ex_i));
    {   // the narrow reading: only the cards it exiled
        Pin n(heurarm::BURN_M2_NARROW, true);
        CHECK_FALSE(allowed(0));
        CHECK_FALSE(allowed(4));   // staged BEFORE the main-2 Light Up
        CHECK(allowed(ex_i));
        CHECK(allowed(1));         // the three exceptions still hold
    }
    // A second stager in the same main does not move the snapshot.
    const uint64_t m0 = s.m2_prestaged_mask[0];
    NoteMain2StagerCast(s, DefB(kLuts));
    CHECK(s.m2_prestaged_mask[0] == m0);
    // A non-stager never stamps; the rule off never stamps.
    GameState t = BurnState({ kBolt }, 3, {}, 20, 4, 9, true);
    NoteMain2StagerCast(t, DefB(kBolt));
    CHECK(t.m2_stage_turn == -1);
    Pin off(heurarm::BURN_M2_RULE, false);
    CHECK_FALSE(ProvB().RestrictsMain2Casts());
    NoteMain2StagerCast(t, DefB(kLuts));
    CHECK(t.m2_stage_turn == -1);
}

TEST_CASE("burn main-2 rule through the enumeration: main 2 offers no Bolt, main 1 is untouched")
{
    Pin o(heurarm::BURN_ORDER, true);
    auto any_casts = [](const std::vector<TurnSolver::Plan>& ps, const std::string& n)
    { for (const TurnSolver::Plan& p : ps) { if (PlanCasts(p, n)) { return true; } } return false; };
    for (int on = 0; on <= 1; ++on)
    {
        Pin r(heurarm::BURN_M2_RULE, on != 0);
        const GameState s = BurnState({ kBolt, kLuts, kEid }, 3, { kGG }, 14, 4, 10 + on, true);
        const std::vector<TurnSolver::Plan> m2 = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
        const std::vector<TurnSolver::Plan> m1 = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(any_casts(m2, kBolt) == (on == 0));   // the control arm offers it
        CHECK(any_casts(m2, kLuts));
        CHECK(any_casts(m2, kEid));
        CHECK(any_casts(m1, kBolt));                // main 1 keeps everything
        CHECK(any_casts(m1, kEid));
    }
}

TEST_CASE("spectacle plan credit: seed 9295 gi 294's turn-3 line {Swiftspear, Shard Volley, Light Up} is a plan")
{
    // T3, opponent at 13, board: 2 Mountains, Goblin Guide x2, Swiftspear; hand: Mountain, Swiftspear,
    // Shard Volley, Light Up the Stage. Land, Swiftspear, Volley (3 to the face), Light Up {R}: three
    // mana, and the attack is 2+2+3+3 = 10 -- lethal (13).
    Pin o(heurarm::BURN_ORDER, true);
    auto the_line = [](const TurnSolver::Plan& p)
    { return PlanCasts(p, kSS) && PlanCasts(p, kVolley) && PlanCasts(p, kLuts); };
    for (int on = 0; on <= 1; ++on)
    {
        Pin c(heurarm::SPECTACLE_PLAN_CREDIT, on != 0);
        const GameState s = BurnState({ kMtn, kSS, kVolley, kLuts }, 2, { kGG, kGG, kSS }, 13, 3, 20 + on, false);
        bool found = false, found_win = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            if (!the_line(p)) { continue; }
            found = true;
            if (p.wins_this_turn) { found_win = true; }
        }
        CHECK(found == (on != 0));          // the control arm never offers it
        CHECK(found_win == (on != 0));
    }
}

TEST_CASE("kill-trigger combo credit: two Searing Bloods kill a 4/4 -- the Volley that finishes is lethal, not held")
{
    // Five Mountains (drop made), hand Searing Blood x2 + Shard Volley, the opponent at 9 with ONE 4/4.
    // Blood + Blood = 4 kills it, so both death triggers fire (6) and Volley's 3 is the kill. Neither
    // Blood kills alone, so CollectActions credits each with 0 face damage; without the credit the plan
    // reads 3 of 9, and the Volley hold (exception (a) is "the plan wins this turn") drops it.
    Pin o(heurarm::BURN_ORDER, true);
    auto the_kill = [](const TurnSolver::Plan& p)
    {
        int bloods = 0;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kBlood) { ++bloods; } }
        return bloods == 2 && PlanCasts(p, kVolley);
    };
    for (int on = 0; on <= 1; ++on)
    {
        Pin k(heurarm::KILL_TRIGGER_COMBO, on != 0);
        GameState s = BurnState({ kBlood, kBlood, kVolley }, 5, {}, 9, 5, 50 + on, true);
        PutB(s, "Atsushi, the Blazing Sky", 91);
        s.battlefield.back().controller_index = 1;
        s.battlefield.back().owner_index = 1;
        bool found_win = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        { if (the_kill(p) && p.wins_this_turn) { found_win = true; } }
        CHECK(found_win == (on != 0));
    }
}

TEST_CASE("spectacle plan credit, search-only arm: the search still finds the 9295 line, the greedy walk does not credit")
{
    Pin o(heurarm::BURN_ORDER, true);
    Pin c(heurarm::SPECTACLE_PLAN_CREDIT, true);
    Pin so(heurarm::SPECTACLE_CREDIT_SEARCH_ONLY, true);
    const GameState s = BurnState({ kMtn, kSS, kVolley, kLuts }, 2, { kGG, kGG, kSS }, 13, 3, 40, false);
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (PlanCasts(p, kSS) && PlanCasts(p, kVolley) && PlanCasts(p, kLuts)) { found = true; } }
    CHECK(found);
    // The greedy walk (Solve) keeps the pre-plan cost: 1 + 1 + 3 = 5 on 3 lands is never one plan.
    GameState g = s;
    g.players[0].lands_played_this_turn = 1;   // the drop already made: 3 lands' worth of mana
    PutB(g, kMtn, 77);
    g.players[0].hand.erase(g.players[0].hand.begin());   // the Mountain went down
    const TurnSolver::Plan sp = TurnSolver::Solve(g, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    CHECK_FALSE((PlanCasts(sp, kSS) && PlanCasts(sp, kVolley) && PlanCasts(sp, kLuts)));
    // ...and the d0 land hold agrees with what Solve can find.
    const GameState h = BurnState({ kMtn, kLuts, kBolt }, 2, {}, 20, 3, 41, false);
    CHECK_FALSE(ProvB().LandDropAfterDrawEngine(h, 0));
}
