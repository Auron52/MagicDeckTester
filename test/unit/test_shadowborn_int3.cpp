// Shadowborn Apostle onboarding, integrator INT-3 (2026-11-03; docs/design/analysis-Shadowborn
// Apostle.md): Thrumming Stone's "Spells you cast have ripple 4" (CR 702.60) -- the executor's stack
// (EffectHandler::PushCastTriggers / ResolveRippleTrigger), the rollout + viewer twin (apply_one's ripple
// block), the upkeep twin (CastOffSuspend -> ResolveSuspendRipple), the searched pins (EmitRipplePins) and
// the INT-2 activation counting ripple-cast Apostles.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kApostle3 = "Shadowborn Apostle";
const char* const kStone    = "Thrumming Stone";
const char* const kIntent   = "Diabolic Intent";

void EnsureCardsSb3()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def3(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card3(const std::string& name, int number)
{
    Card c     = Def3(name).card;
    c.m_number = number;
    return c;
}

Permanent Perm3(const std::string& name, int number, bool fresh = false)
{
    Permanent p;
    p.card              = Card3(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    return p;
}

const ShadowbornProvider& Prov3() { static const ShadowbornProvider p; return p; }

GameState Base3(int swamps = 4)
{
    EnsureCardsSb3();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.m_provider          = &Prov3();
    for (int k = 0; k < swamps; ++k) { s.battlefield.push_back(Perm3("Swamp", 1 + k)); }
    return s;
}

void Lib(GameState& s, const std::vector<std::string>& names, int first_number = 500)
{
    int n = first_number;
    for (const std::string& nm : names) { s.players[0].library.push_back(Card3(nm, n++)); }
}
void SwampFill(GameState& s, int k, int first_number = 800)
{
    for (int i = 0; i < k; ++i) { s.players[0].library.push_back(Card3("Swamp", first_number + i)); }
}
// Filler nothing in these tests can cast or play (a tutored card is then just a card in hand: the plan
// apply's post-tutor re-solve has nothing to do with it).
void DemonFill(GameState& s, int k, int first_number = 800)
{
    for (int i = 0; i < k; ++i) { s.players[0].library.push_back(Card3("Sire of Insanity", first_number + i)); }
}

// The ripple pins a plan cast carries (EmitRipplePins' fields).
struct Pins
{
    std::int8_t   reveals  = -1;
    std::int8_t   take_cap = -1;
    std::uint8_t  mask     = 0;
    std::uint8_t  decline  = 0;
    std::uint16_t ft       = 0;
    std::uint16_t fv       = 0;
};

Action CastAct(const GameState& s, const std::string& name, const Pins& p, const std::string& tutor = "", int victim = 0)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &Def3(name);
    a.cost      = EffectiveSpellCost(Def3(name), s, 1);
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[i].m_name == name) { a.hand_index = i; break; } }
    a.tutor_target          = tutor;
    a.soulfire_own_targets  = victim;
    a.ripple_pinned         = true;
    a.ripple_reveals        = p.reveals;
    a.ripple_take_cap       = p.take_cap;
    a.ripple_mask           = p.mask;
    a.ripple_decline        = p.decline;
    a.ripple_free_tutor     = p.ft;
    a.ripple_free_victim    = p.fv;
    return a;
}

// THE ROLLOUT (also the viewer's path): one cast through the shared plan apply.
void RolloutCast(GameState& s, const Action& a)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    p.actions.push_back(a);
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
}

// THE EXECUTOR: a hard cast exactly as AIEngine::CastSpellFromHand puts it on the stack (cost unpaid --
// the comparisons below never read mana), then the stack resolved LIFO as GameEngine::ResolveStack does.
void ResolveAll3(GameState& s)
{
    while (!s.stack.empty())
    {
        StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        REQUIRE(d != nullptr);
        EffectHandler::Resolve(s, e, *d);
    }
}
void ExecutorCast(GameState& s, const std::string& name, const Pins& p, const std::string& tutor = "", int victim = 0)
{
    const CardDefinition& d = Def3(name);
    auto& hand = s.players[0].hand;
    auto it = std::find_if(hand.begin(), hand.end(), [&](const Card& c) { return c.m_name == name; });
    REQUIRE(it != hand.end());
    const Card c = *it;
    hand.erase(it);
    if (SacCreatureCostFoldsByReadiness(d.params))
    { PerformSacrificeCreatureCost(s, name, d.params.sac_additional_creature_color, victim, true); }
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = d.card;
    e.source.m_number  = c.m_number;
    e.controller_index = 0;
    e.tutor_target     = tutor;
    ++s.spells_cast_this_turn;
    s.stack.push_back(e);
    StampRippleScript(s, d, true, p.reveals, p.take_cap, p.mask, p.decline, p.ft, p.fv);
    EffectHandler::PushCastTriggers(s, d, 0);
    ResolveAll3(s);
}

std::vector<int> Nums(const std::vector<Card>& v) { std::vector<int> o; for (const Card& c : v) { o.push_back(c.m_number); } return o; }
std::vector<int> LibNums(const GameState& s) { std::vector<int> o; for (const Card& c : s.players[0].library) { o.push_back(c.m_number); } return o; }
std::vector<int> NonLandBf(const GameState& s)
{
    std::vector<int> o;
    for (const Permanent& p : s.battlefield) { if (!p.card.IsLand()) { o.push_back(p.card.m_number); } }
    std::sort(o.begin(), o.end());
    return o;
}
std::vector<int> Sorted(std::vector<int> v) { std::sort(v.begin(), v.end()); return v; }
int CountBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// Same zones in both worlds (mana / tapped state aside: the executor emulation does not pay).
void CheckLockstep(const GameState& ex, const GameState& ro)
{
    CHECK(NonLandBf(ex) == NonLandBf(ro));
    CHECK(LibNums(ex) == LibNums(ro));
    CHECK(Sorted(Nums(ex.players[0].hand)) == Sorted(Nums(ro.players[0].hand)));
    CHECK(Sorted(Nums(ex.players[0].graveyard)) == Sorted(Nums(ro.players[0].graveyard)));
    CHECK(ex.spells_cast_this_turn == ro.spells_cast_this_turn);
}
}   // namespace

TEST_CASE("Shadowborn INT-3: Thrumming Stone card data, provider hooks")
{
    EnsureCardsSb3();
    const CardDefinition& st = Def3(kStone);
    CHECK(st.card.m_mana_cost.ManaValue() == 5);
    CHECK(st.card.m_mana_cost.generic == 5);
    CHECK(st.params.grants_ripple == 4);
    CHECK(st.card.HasType(CardType::Artifact));
    CHECK(RippleChainOrderInert(st));
    CHECK(RippleChainOrderInert(Def3(kApostle3)));
    CHECK(RippleChainOrderInert(Def3("Dark Ritual")));
    CHECK_FALSE(RippleChainOrderInert(Def3(kIntent)));
    CHECK(RippleResolutionShuffles(Def3(kIntent)));
    CHECK(RippleResolutionShuffles(Def3("Profane Tutor")));
    GameState s = Base3();
    CHECK(Prov3().FreeCastTakeDominated(s, 0, Def3(kApostle3)));
    CHECK_FALSE(Prov3().FreeCastTakeDominated(s, 0, Def3("Dark Ritual")));
    CHECK_FALSE(Prov3().FreeCastTakeDominated(s, 0, Def3(kIntent)));
    CHECK(Prov3().CastOrderRank(s, st) < Prov3().CastOrderRank(s, Def3(kApostle3)));
    // No Stone out: nothing ripples (the Stone's own first cast included). One Stone out: one instance.
    CHECK(RippleInstanceNs(s, 0, st).empty());
    s.battlefield.push_back(Perm3(kStone, 90));
    CHECK(RippleInstanceNs(s, 0, Def3(kApostle3)) == std::vector<int>{ 4 });
    CHECK(RippleInstanceNs(s, 0, Def3("Swamp")).empty());   // lands are not cast
}

TEST_CASE("Ripple: an Apostle chain casts free Apostles that ripple again -- executor and rollout agree")
{
    // Library: A1 S S S | A2 S S S | S S S S | ...  The hard Apostle reveals A1 (cast), A1 reveals A2
    // (cast), A2 reveals four Swamps (bottomed). Budget 10 = every trigger reveals.
    GameState s = Base3();
    s.battlefield.push_back(Perm3(kStone, 90));
    s.players[0].hand.push_back(Card3(kApostle3, 100));
    Lib(s, { kApostle3, "Swamp", "Swamp", "Swamp", kApostle3, "Swamp", "Swamp", "Swamp" });
    SwampFill(s, 12);
    Pins p; p.reveals = 10;
    GameState ex = s, ro = s;
    ExecutorCast(ex, kApostle3, p);
    RolloutCast(ro, CastAct(ro, kApostle3, p));
    CHECK(CountBf(ex, kApostle3) == 3);
    CHECK(CountBf(ro, kApostle3) == 3);
    CHECK(ro.spells_cast_this_turn == 3);
    // The bottom holds the three reveals' non-casts in REVEAL order: 501-503, 505-507, 800-803.
    const std::vector<int> lib = LibNums(ro);
    REQUIRE(lib.size() >= 10);
    const std::vector<int> tail(lib.end() - 10, lib.end());
    CHECK(tail == std::vector<int>{ 501, 502, 503, 505, 506, 507, 800, 801, 802, 803 });
    CheckLockstep(ex, ro);
    // Budget 1 (F1: only the NUMBER of revealing triggers matters): the hard cast's reveal only.
    GameState ex1 = s, ro1 = s;
    Pins q; q.reveals = 1;
    ExecutorCast(ex1, kApostle3, q);
    RolloutCast(ro1, CastAct(ro1, kApostle3, q));
    CHECK(CountBf(ro1, kApostle3) == 2);
    CHECK(LibNums(ro1).front() == 504);   // A2 is now on top
    CheckLockstep(ex1, ro1);
    // Take cap 0 (the late wave's decline): the revealed Apostle goes to the bottom.
    GameState ex0 = s, ro0 = s;
    Pins c; c.reveals = 10; c.take_cap = 0;
    ExecutorCast(ex0, kApostle3, c);
    RolloutCast(ro0, CastAct(ro0, kApostle3, c));
    CHECK(CountBf(ro0, kApostle3) == 1);
    CHECK(LibNums(ro0).back() == 503);
    CheckLockstep(ex0, ro0);
}

TEST_CASE("Ripple: a free Dark Ritual adds its {B}{B}{B}; a declined one is bottomed")
{
    GameState s = Base3(1);
    s.battlefield.push_back(Perm3(kStone, 90));
    s.players[0].hand.push_back(Card3("Dark Ritual", 100));
    Lib(s, { "Dark Ritual", "Swamp", "Swamp", "Swamp" });
    SwampFill(s, 10);
    Pins p; p.reveals = 5;
    GameState ro = s, ex = s;
    RolloutCast(ro, CastAct(ro, "Dark Ritual", p));
    ExecutorCast(ex, "Dark Ritual", p);
    CHECK(ro.floating_mana.black == 6);
    CHECK(ex.floating_mana.black == 6);
    CheckLockstep(ex, ro);
    Pins d; d.reveals = 5; d.decline = 1;   // decline the first searched match
    GameState rd = s, ed = s;
    RolloutCast(rd, CastAct(rd, "Dark Ritual", d));
    ExecutorCast(ed, "Dark Ritual", d);
    CHECK(rd.floating_mana.black == 3);
    CHECK(LibNums(rd).back() == 503);
    CHECK(std::find(LibNums(rd).begin(), LibNums(rd).end(), 500) != LibNums(rd).end());
    CheckLockstep(ed, rd);
}

TEST_CASE("Ripple: a no-match reveal bottoms the four in reveal order; budget 0 moves nothing")
{
    GameState s = Base3(6);
    s.battlefield.push_back(Perm3(kStone, 90));
    s.players[0].hand.push_back(Card3(kApostle3, 100));
    SwampFill(s, 12);
    Pins p; p.reveals = 1;
    GameState ro = s, ex = s;
    RolloutCast(ro, CastAct(ro, kApostle3, p));
    ExecutorCast(ex, kApostle3, p);
    std::vector<int> want;
    for (int k = 4; k < 12; ++k) { want.push_back(800 + k); }
    for (int k = 0; k < 4; ++k)  { want.push_back(800 + k); }
    CHECK(LibNums(ro) == want);
    CheckLockstep(ex, ro);
    Pins z; z.reveals = 0;
    GameState r0 = s;
    RolloutCast(r0, CastAct(r0, kApostle3, z));
    CHECK(LibNums(r0) == LibNums(s));
    // Fewer than four cards left: all of them are revealed (702.60a).
    GameState t = Base3();
    t.battlefield.push_back(Perm3(kStone, 90));
    t.players[0].hand.push_back(Card3(kApostle3, 100));
    Lib(t, { "Swamp", kApostle3 });
    RolloutCast(t, CastAct(t, kApostle3, p));
    CHECK(CountBf(t, kApostle3) == 2);
}

TEST_CASE("Ripple: Thrumming Stone's legend rule -- a second Stone ripples, resolves, one Stone stays")
{
    GameState s = Base3(5);
    s.battlefield.push_back(Perm3(kStone, 90));
    s.players[0].hand.push_back(Card3(kStone, 91));
    Lib(s, { kStone, "Swamp", "Swamp", "Swamp" });
    SwampFill(s, 8);
    Pins p; p.reveals = 2;   // both the hard Stone's trigger and the free Stone's
    GameState ro = s, ex = s;
    RolloutCast(ro, CastAct(ro, kStone, p));
    ExecutorCast(ex, kStone, p);
    CHECK(CountBf(ro, kStone) == 1);
    CHECK(CountBf(ex, kStone) == 1);
    CHECK(ro.spells_cast_this_turn == 2);   // the revealed Stone was cast (searched take)
    CHECK(RippleInstanceNs(ro, 0, Def3(kApostle3)).size() == 1);
    CheckLockstep(ex, ro);
}

TEST_CASE("Ripple: a free Diabolic Intent pays its sacrifice and searches with its pinned pick")
{
    GameState s = Base3(4);
    s.battlefield.push_back(Perm3(kStone, 90));
    s.battlefield.push_back(Perm3(kApostle3, 60));
    s.battlefield.push_back(Perm3(kApostle3, 61, /*fresh=*/true));
    s.players[0].hand.push_back(Card3(kIntent, 100));
    Lib(s, { kIntent, "Reaper from the Abyss", "Sire of Insanity", "Sire of Insanity" });
    DemonFill(s, 10);
    Pins p; p.ft = 1; p.fv = 1;   // free tutor rank 0, free victim rank 0
    GameState ro = s, ex = s;
    RolloutCast(ro, CastAct(ro, kIntent, p, "Reaper from the Abyss", 60));
    ExecutorCast(ex, kIntent, p, "Reaper from the Abyss", 60);
    CHECK(CountBf(ro, kApostle3) == 0);   // one sacrifice per Intent
    CHECK(CountBf(ex, kApostle3) == 0);
    CHECK(ro.players[0].hand.size() == 2);   // two tutored cards
    CHECK(ro.spells_cast_this_turn == 2);
    CheckLockstep(ex, ro);
    // No creature left for the free copy: it cannot be cast (CR 601.2h) and goes to the bottom.
    GameState n = s;
    n.battlefield.erase(std::remove_if(n.battlefield.begin(), n.battlefield.end(),
                                       [](const Permanent& q) { return q.card.m_number == 61; }), n.battlefield.end());
    GameState nr = n, ne = n;
    RolloutCast(nr, CastAct(nr, kIntent, p, "Reaper from the Abyss", 60));
    ExecutorCast(ne, kIntent, p, "Reaper from the Abyss", 60);
    CHECK(nr.spells_cast_this_turn == 1);
    CHECK(nr.players[0].hand.size() == 1);
    CheckLockstep(ne, nr);
}

TEST_CASE("Ripple: one reveal casting two tutors -- both chains reveal BEFORE either tutor shuffles")
{
    // DI1 (hand) reveals DI_b, DI_c (both cast; four bodies pay four sacrifices). Real stack:
    // [DI1, DI_b, DI_c, R_b, R_c] -> R_c reveals Z1..Z4, R_b reveals DI_d (cast) ... then the shuffles.
    // A depth-first order would let DI_c's shuffle run before R_b's reveal, and DI_d would not be found.
    GameState s = Base3(4);
    s.battlefield.push_back(Perm3(kStone, 90));
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(Perm3(kApostle3, 60 + k, true)); }
    s.players[0].hand.push_back(Card3(kIntent, 100));
    Lib(s, { kIntent, kIntent, "Sire of Insanity", "Sire of Insanity", "Reaper from the Abyss",
             "Sire of Insanity", "Sire of Insanity", "Sire of Insanity", kIntent });
    DemonFill(s, 20);
    Pins p; p.ft = 0x111; p.fv = 0x111;   // three free tutors / victims at rank 0
    GameState ro = s, ex = s;
    RolloutCast(ro, CastAct(ro, kIntent, p, "Reaper from the Abyss", 60));
    ExecutorCast(ex, kIntent, p, "Reaper from the Abyss", 60);
    CHECK(ro.spells_cast_this_turn == 4);   // DI1 + DI_b + DI_c + DI_d
    CHECK(ex.spells_cast_this_turn == 4);
    CHECK(CountBf(ro, kApostle3) == 0);
    CheckLockstep(ex, ro);
}

TEST_CASE("Ripple off suspend: Lotus Bloom's upkeep reveal and a Profane Tutor taking its twin")
{
    GameState s = Base3();
    s.battlefield.push_back(Perm3(kStone, 90));
    SuspendedCard sc; sc.card = Card3("Lotus Bloom", 70); sc.arrive_turn = 0;
    s.players[0].suspended_cards.push_back(sc);
    SwampFill(s, 10);
    GameState rev = s;
    rev.scripted_upkeep_land_search = -1;   // unpinned = rank 0 = reveal (the front)
    ProcessSuspendArrivals(rev, 0);
    CHECK(CountBf(rev, "Lotus Bloom") == 1);
    CHECK(LibNums(rev).front() == 804);     // the top four went to the bottom before the draw
    GameState keep = s;
    keep.scripted_upkeep_land_search = 1;   // rank 1 = don't reveal
    ProcessSuspendArrivals(keep, 0);
    CHECK(LibNums(keep).front() == 800);
    // Profane Tutor (F2: its search shuffles) with its twin in the top four: rank 0 casts the twin, whose
    // search consumes the next ordinal, then the arrival's own.
    GameState pt = Base3();
    pt.battlefield.push_back(Perm3(kStone, 90));
    SuspendedCard pc; pc.card = Card3("Profane Tutor", 71); pc.arrive_turn = 0;
    pt.players[0].suspended_cards.push_back(pc);
    Lib(pt, { "Swamp", "Profane Tutor", "Swamp", "Dark Ritual" });
    SwampFill(pt, 10);
    GameState take = pt;
    take.scripted_upkeep_land_search = 0;
    ProcessSuspendArrivals(take, 0);
    CHECK(take.players[0].hand.size() == 2);   // two searches
    CHECK(take.spells_cast_this_turn == 2);
    GameState decl = pt;
    decl.scripted_upkeep_land_search = 1;      // rank 1 = decline the twin
    ProcessSuspendArrivals(decl, 0);
    CHECK(decl.players[0].hand.size() == 1);
    CHECK(decl.spells_cast_this_turn == 1);
}

TEST_CASE("Ripple pins: the first Apostle carries the reveal budget, the other copies continue it")
{
    GameState s = Base3(2);
    s.battlefield.push_back(Perm3(kStone, 90));
    s.players[0].hand.push_back(Card3(kApostle3, 100));
    s.players[0].hand.push_back(Card3(kApostle3, 101));
    Lib(s, { kApostle3, "Swamp", "Swamp", "Swamp" });
    SwampFill(s, 12);
    std::vector<int> budgets;
    int cont = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kApostle3) { continue; }
        CHECK(a.ripple_pinned);
        if (a.ripple_reveals == kRippleContinue) { ++cont; continue; }
        CHECK(a.ripple_take_cap == -1);   // no late wave outside a large-budget decision
        budgets.push_back(a.ripple_reveals);
    }
    CHECK(cont == 1);
    // Two hand copies + one revealed Apostle = three triggers: B = 3, 2, 1, 0.
    CHECK(budgets == std::vector<int>{ 3, 2, 1, 0 });
    // Stone only in HAND: every revealing variant needs the Stone cast in the same plan; B = 0 does not.
    GameState h = Base3(6);
    h.players[0].hand.push_back(Card3(kStone, 91));
    h.players[0].hand.push_back(Card3(kApostle3, 100));
    SwampFill(h, 12);
    for (const Action& a : TurnSolver::CollectActionsForTest(h, true))
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kApostle3) { continue; }
        CHECK(a.ripple_granter_req == (a.ripple_reveals == 0 ? 0 : 1));
    }
}

TEST_CASE("Ripple + INT-2: Apostles a chain will cast count toward the six")
{
    // Three Apostles out, one in hand; the chain reveals two more -> six, so the activation is in the menu.
    GameState s = Base3(2);
    s.battlefield.push_back(Perm3(kStone, 90));
    for (int k = 0; k < 3; ++k) { s.battlefield.push_back(Perm3(kApostle3, 60 + k)); }
    s.players[0].hand.push_back(Card3(kApostle3, 100));
    Lib(s, { kApostle3, "Swamp", "Swamp", "Swamp", kApostle3, "Swamp", "Swamp", "Swamp", "Reaper from the Abyss" });
    SwampFill(s, 12);
    auto activations = [](const GameState& g)
    {
        int n = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(g, true))
        { if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut) { ++n; } }
        return n;
    };
    CHECK(activations(s) > 0);
    GameState no_stone = s;
    no_stone.battlefield.erase(std::remove_if(no_stone.battlefield.begin(), no_stone.battlefield.end(),
                                              [](const Permanent& q) { return q.card.m_number == 90; }),
                               no_stone.battlefield.end());
    CHECK(activations(no_stone) == 0);
}
