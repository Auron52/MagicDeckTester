// ELVES onboarding (2026-11-26, docs/design/analysis-Elves.md): every new Elves mechanic, each checked in
// BOTH worlds -- the rollout (TurnSolver::ApplyPlan) and the real executor (a committed plan run by
// AIEngine::TakeTurn) -- from the SAME enumerated plan, so the search can only predict what the game does.
//
//  1. ALLOSAURUS SHEPHERD (PermAbilityMode::TeamBasePT): a layer-7b base set (CR 613.4b), so lords, until-EOT
//     pumps and +1/+1 counters (7c, CR 613.4c) still apply on top; no stacking on a second activation; the
//     activation runs AFTER the plan's casts (an Elf cast in the same plan is set too); cleanup ends it.
//  2. JORAGA WARCALLER: the multikick count is a searched cast axis; K counters on entry; the per-counter lord.
//  3. GALADHRIM BRIGADE: squad K token copies, each one a lord.
//  4. IMMACULATE MAGISTRATE: N +1/+1 counters on the target, N = Elves we control.
//  5. MARWYN, THE NURTURER: a counter per entering Elf; taps for {G} x its live power.
//  6. LEAF-CROWNED VISIONARY: the optional {G} pay-to-draw is a searched pin; paid draws, declined does not.
//  7. CONCORDANT CROSSROADS: the world rule (CR 704.5m) -- the newest World permanent survives.
//  8. ELVISH CLANCALLER: the tutor finds only a card NAMED Elvish Clancaller.
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace {
const CardDefinition& ED(const std::string& name)
{
    static const bool loaded = [] { CardDatabase::Instance().LoadFromJson(
        ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card ECard(const std::string& name, int id) { Card c = ED(name).card; c.m_number = id; return c; }
int EPut(GameState& s, const std::string& name, int id, bool tapped = false, bool sick = false)
{
    Permanent p; p.card = ECard(name, id); p.controller_index = 0; p.owner_index = 0;
    p.tapped = tapped; p.entered_this_turn = sick; s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
void EHand(GameState& s, const std::string& name, int id) { s.players[0].hand.push_back(ECard(name, id)); }
void ELib(GameState& s, const std::string& name, int id) { s.players[0].library.push_back(ECard(name, id)); }
void EForests(GameState& s, int n, int first = 100) { for (int i = 0; i < n; ++i) { EPut(s, "Forest", first + i); } }
GameState EBase()
{
    GameState s; s.active_player_index = 0; s.turn_number = 5; s.game_seed = 777;
    s.players[0].life = 20; s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop in these boards
    for (int i = 0; i < 6; ++i) { ELib(s, "Forest", 900 + i); }
    return s;
}
const Permanent* EN(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
int EPow(const GameState& s, int num) { const Permanent* p = EN(s, num); REQUIRE(p != nullptr); return CombatPowerOf(*p, s); }
int ETou(const GameState& s, int num) { const Permanent* p = EN(s, num); REQUIRE(p != nullptr); return LethalToughness(*p, s); }
int ETapped(const GameState& s)
{ int n = 0; for (const Permanent& p : s.battlefield) { n += (p.tapped && p.card.IsLand()) ? 1 : 0; } return n; }
void EResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
bool IsAct(const Action& a, Action::AbilityMode m)
{ return a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == m; }
bool IsCast(const Action& a, const std::string& name)
{ return a.kind == Action::Kind::CastFromHand && a.card_name.str() == name; }
// The SMALLEST enumerated plan matching `pred` (fewest actions), or null.
const TurnSolver::Plan* EFind(const std::vector<TurnSolver::Plan>& plans,
                              const std::function<bool(const TurnSolver::Plan&)>& pred)
{
    const TurnSolver::Plan* best = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!pred(p)) { continue; }
        if (best == nullptr || p.actions.size() < best->actions.size()) { best = &p; }
    }
    return best;
}
int ECountCasts(const TurnSolver::Plan& p)
{ int n = 0; for (const Action& a : p.actions) { n += a.kind == Action::Kind::CastFromHand ? 1 : 0; } return n; }
// Run `plan` in both worlds from `s`: the rollout's ApplyPlan and the executor's committed-plan TakeTurn.
void EBoth(const GameState& s, const TurnSolver::Plan& plan, GameState& roll, GameState& real)
{
    roll = s;
    TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
    real = s;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    eng.TakeTurn(real, true, EResolveAll);
}
}   // namespace

// ---- 1. ALLOSAURUS SHEPHERD ----------------------------------------------------------------------------------

TEST_CASE("Allosaurus Shepherd: base 5/5 is layer 7b -- lords, an earlier pump and +1/+1 counters apply on top")
{
    GameState s = EBase();
    EPut(s, "Allosaurus Shepherd", 1);
    EPut(s, "Elvish Clancaller", 2);                         // other Elves +1/+1
    const int li = EPut(s, "Llanowar Elves", 3);
    s.battlefield[static_cast<std::size_t>(li)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    s.battlefield[static_cast<std::size_t>(li)].temp_power_bonus = 3;   // an earlier until-EOT +3/+3 (Ezuri)
    s.battlefield[static_cast<std::size_t>(li)].temp_tough_bonus = 3;
    EPut(s, "Keldon Halberdier", 4);                         // not an Elf
    CHECK(EPow(s, 3) == 1 + 1 + 2 + 3);
    const int halberdier_p = EPow(s, 4), halberdier_t = ETou(s, 4);

    ApplyPermAbility(s, 0, 1, PermAbilityMode::TeamBasePT);
    // Llanowar: base 5 (was printed 1) + Clancaller 1 + counters 2 + the pump 3 = 11/11.
    CHECK(EPow(s, 3) == 5 + 1 + 2 + 3);
    CHECK(ETou(s, 3) == 5 + 1 + 2 + 3);
    // The delta lives in the until-EOT bonus beside the pump: 3 + (5 - 1).
    CHECK(EN(s, 3)->temp_power_bonus == 3 + 4);
    CHECK(EN(s, 3)->temp_base_set);
    // The Shepherd itself (an Elf) and the Clancaller (its own lord excludes itself).
    CHECK(EPow(s, 1) == 5 + 1);
    CHECK(EPow(s, 2) == 5);
    // A non-Elf is untouched.
    CHECK(EPow(s, 4) == halberdier_p);
    CHECK(ETou(s, 4) == halberdier_t);
    CHECK_FALSE(EN(s, 4)->temp_base_set);

    // A 7c effect AFTER the set still adds (a pump, a counter, a lord entering).
    s.battlefield[static_cast<std::size_t>(li)].counters.Add(Counter::Type::PlusOnePlusOne, 1);
    CHECK(EPow(s, 3) == 12);
    EPut(s, "Elvish Clancaller", 5);                         // a second lord: +1 more, and it is NOT set (CR 611.2c)
    CHECK(EPow(s, 3) == 13);
    CHECK(EPow(s, 5) == 1 + 1);                              // printed 1/1 + the other Clancaller

    // A second activation does not stack on the already-set creatures; it reaches only the newcomer.
    ApplyPermAbility(s, 0, 1, PermAbilityMode::TeamBasePT);
    CHECK(EPow(s, 3) == 13);
    CHECK(EPow(s, 1) == 5 + 2);
    CHECK(EPow(s, 5) == 5 + 1);
    CHECK(EN(s, 5)->temp_base_set);
}

TEST_CASE("Allosaurus Shepherd: an Elf cast in the same plan becomes 5/5 in BOTH worlds; cleanup ends the set")
{
    GameState s = EBase();
    EPut(s, "Allosaurus Shepherd", 1);
    EForests(s, 7);
    EHand(s, "Llanowar Elves", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    // The activation is offered on its own, and beside the cast in one plan.
    CHECK(EFind(plans, [](const TurnSolver::Plan& p)
          { return p.actions.size() == 1 && IsAct(p.actions[0], Action::AbilityMode::TeamBasePT); }) != nullptr);
    const TurnSolver::Plan* both = EFind(plans, [](const TurnSolver::Plan& p)
    {
        bool cast = false, act = false;
        for (const Action& a : p.actions)
        { cast |= IsCast(a, "Llanowar Elves"); act |= IsAct(a, Action::AbilityMode::TeamBasePT); }
        return cast && act && ECountCasts(p) == 1;
    });
    REQUIRE(both != nullptr);

    GameState roll, real;
    EBoth(s, *both, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        REQUIRE(EN(*g, 20) != nullptr);
        CHECK(EPow(*g, 20) == 5);          // cast first, then the trailing activation set it
        CHECK(ETou(*g, 20) == 5);
        CHECK(EPow(*g, 1) == 5);
        CHECK(ETapped(*g) == 7);           // G for the Elf + {4}{G}{G}
    }
    // Cleanup ends it (CR 514.2): the bonus and the set mark both reset.
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    ge.PlayOutFrom(roll, roll.turn_number, GameEngine::ResumeAt::Cleanup);
    REQUIRE(EN(roll, 20) != nullptr);
    CHECK(EN(roll, 20)->temp_power_bonus == 0);
    CHECK_FALSE(EN(roll, 20)->temp_base_set);
    CHECK(EPow(roll, 20) == 1);
}

// ---- 2. JORAGA WARCALLER -------------------------------------------------------------------------------------

TEST_CASE("Joraga Warcaller: every kick count is its own plan; K counters on entry; +1/+1 per counter to other Elves")
{
    GameState s = EBase();
    EForests(s, 5);
    EPut(s, "Llanowar Elves", 3);
    EHand(s, "Joraga Warcaller", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<int> ks;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (IsCast(a, "Joraga Warcaller")) { ks.insert(a.kicked ? a.kick_count : 0); } }
    }
    // {G} + K x {1}{G} out of five Forests: K = 0, 1, 2.
    CHECK(ks == std::set<int>{ 0, 1, 2 });
    const TurnSolver::Plan* k2 = EFind(plans, [](const TurnSolver::Plan& p)
    { return p.actions.size() == 1 && IsCast(p.actions[0], "Joraga Warcaller") && p.actions[0].kick_count == 2; });
    REQUIRE(k2 != nullptr);
    GameState roll, real;
    EBoth(s, *k2, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        REQUIRE(EN(*g, 20) != nullptr);
        CHECK(PlusCountersOn(*EN(*g, 20)) == 2);
        CHECK(EN(*g, 20)->card.m_name.str() == "Joraga Warcaller");   // the printed card, not the face
        CHECK(EPow(*g, 20) == 1 + 2);                                  // its own lord excludes itself
        CHECK(EPow(*g, 3) == 1 + 2);                                   // Llanowar: +1/+1 per counter
        CHECK(ETapped(*g) == 5);
    }
    // The lord reads the counters LIVE: one more counter, one more point on every other Elf.
    GameState t = roll;
    for (Permanent& p : t.battlefield)
    { if (p.card.m_number == 20) { p.counters.Add(Counter::Type::PlusOnePlusOne, 1); } }
    CHECK(EPow(t, 3) == 1 + 3);
}

// ---- 3. GALADHRIM BRIGADE ------------------------------------------------------------------------------------

TEST_CASE("Galadhrim Brigade: squad K makes K token copies in BOTH worlds, and every copy is a lord")
{
    GameState s = EBase();
    EForests(s, 7);
    EHand(s, "Galadhrim Brigade", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<int> ks;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (IsCast(a, "Galadhrim Brigade")) { ks.insert(a.kicked ? a.kick_count : 0); } }
    }
    CHECK(ks == std::set<int>{ 0, 1, 2 });   // {2}{G} + K x {1}{G} out of seven
    const TurnSolver::Plan* k2 = EFind(plans, [](const TurnSolver::Plan& p)
    { return p.actions.size() == 1 && IsCast(p.actions[0], "Galadhrim Brigade") && p.actions[0].kick_count == 2; });
    REQUIRE(k2 != nullptr);
    GameState roll, real;
    EBoth(s, *k2, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        int brigades = 0, tokens = 0;
        for (const Permanent& p : g->battlefield)
        {
            if (p.card.m_name.str() != "Galadhrim Brigade") { continue; }
            ++brigades; tokens += p.is_token ? 1 : 0;
            CHECK(CombatPowerOf(p, *g) == 2 + 2);   // each pumped by the OTHER two
        }
        CHECK(brigades == 3);
        CHECK(tokens == 2);
        CHECK(ETapped(*g) == 7);
    }
}

// ---- 4. IMMACULATE MAGISTRATE --------------------------------------------------------------------------------

TEST_CASE("Immaculate Magistrate: the target gets one +1/+1 counter per Elf we control, in BOTH worlds")
{
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Immaculate Magistrate", 1);
    EPut(s, "Llanowar Elves", 3, /*tapped=*/true);
    EPut(s, "Llanowar Elves", 4, /*tapped=*/true);
    EPut(s, "Keldon Halberdier", 5);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<int> targets;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (IsAct(a, Action::AbilityMode::TapTargetPump)) { targets.insert(a.sac_victim_id); } }
    }
    CHECK_FALSE(targets.empty());
    const TurnSolver::Plan* on_elf = EFind(plans, [](const TurnSolver::Plan& p)
    {
        return p.actions.size() == 1 && IsAct(p.actions[0], Action::AbilityMode::TapTargetPump)
            && (p.actions[0].sac_victim_id == 3 || p.actions[0].sac_victim_id == 4);
    });
    REQUIRE(on_elf != nullptr);
    const int tgt = on_elf->actions[0].sac_victim_id;
    GameState roll, real;
    EBoth(s, *on_elf, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(PlusCountersOn(*EN(*g, tgt)) == 3);   // Magistrate + two Llanowar Elves (the Halberdier is no Elf)
        CHECK(EN(*g, tgt)->temp_power_bonus == 0);  // counters, not an until-EOT pump
        CHECK(EN(*g, 1)->tapped);                   // the {T}
    }
}

// ---- 5. MARWYN, THE NURTURER ---------------------------------------------------------------------------------

TEST_CASE("Marwyn: a counter on Marwyn per entering Elf (both worlds), and its tap yields its live power")
{
    GameState s = EBase();
    EForests(s, 1);
    EPut(s, "Marwyn, the Nurturer", 1);
    EPut(s, "Elvish Clancaller", 2);
    EHand(s, "Llanowar Elves", 20);
    const CardDefinition& marwyn = ED("Marwyn, the Nurturer");
    CHECK(IsScaledManaDork(marwyn));
    CHECK(ScaledDorkCount(s, 0, marwyn) == 1 + 1);   // printed 1 + the Clancaller lord
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = EFind(plans, [](const TurnSolver::Plan& p)
    { return ECountCasts(p) == 1 && IsCast(p.actions[0], "Llanowar Elves"); });
    REQUIRE(cast != nullptr);
    GameState roll, real;
    EBoth(s, *cast, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        REQUIRE(EN(*g, 20) != nullptr);
        CHECK(PlusCountersOn(*EN(*g, 1)) == 1);       // on MARWYN (Scryfall), not on the entering Elf
        CHECK(PlusCountersOn(*EN(*g, 20)) == 0);
        CHECK(ScaledDorkCount(*g, 0, marwyn) == 1 + 1 + 1);
    }
}

// ---- 6. LEAF-CROWNED VISIONARY -------------------------------------------------------------------------------

TEST_CASE("Leaf-Crowned Visionary: the {G} pay-to-draw is a searched pin -- paid draws, declined does not")
{
    {   // The shared trigger, pinned both ways.
        for (const bool pay : { true, false })
        {
            CAPTURE(pay);
            GameState s = EBase();
            EPut(s, "Leaf-Crowned Visionary", 1);
            s.floating_mana.green = 1;
            s.scripted_mihail_pay_choice.Pin(1);
            s.scripted_mihail_pay_choice.bits[0] = pay ? 1 : 0;
            FireOnCastTriggers(s, ED("Llanowar Elves"), /*mana_spent=*/1);
            CHECK(s.players[0].hand.size() == (pay ? 1u : 0u));
            CHECK(s.floating_mana.green == (pay ? 0 : 1));
        }
        // A non-Elf spell does not trigger it.
        GameState s = EBase();
        EPut(s, "Leaf-Crowned Visionary", 1);
        s.floating_mana.green = 1;
        s.scripted_mihail_pay_choice.Pin(1);
        s.scripted_mihail_pay_choice.bits[0] = 1;
        FireOnCastTriggers(s, ED("Genesis Wave"), /*mana_spent=*/1);
        CHECK(s.players[0].hand.empty());
        CHECK(s.scripted_mihail_trigger_ordinal == 0);
    }
    // The plan variants: both a paying and a declining Llanowar cast exist, and each realises identically.
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Leaf-Crowned Visionary", 1);
    EHand(s, "Llanowar Elves", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const bool pay : { true, false })
    {
        CAPTURE(pay);
        const TurnSolver::Plan* p = EFind(plans, [pay](const TurnSolver::Plan& q)
        {
            return ECountCasts(q) == 1 && IsCast(q.actions[0], "Llanowar Elves")
                && q.mihail_pay_choice.Paid(0) == pay;
        });
        REQUIRE(p != nullptr);
        GameState roll, real;
        EBoth(s, *p, roll, real);
        for (const GameState* g : { &roll, &real })
        {
            CHECK(g->players[0].hand.size() == (pay ? 1u : 0u));
            CHECK(ETapped(*g) == (pay ? 2 : 1));
            CHECK(EN(*g, 20) != nullptr);
        }
    }
}

// A paid Visionary trigger draws, the draw arms the site-10 breakpoint, and the search scores that breakpoint's
// continuations by RESUMING from the node snapshot. A resumed child must reach exactly the board the same child
// reaches from scratch (the executor's and the fd-pred replay's apply): the resume used to re-zero the cast-
// trigger ordinal, so a continuation Elf read the base plan's spent bit 0, paid again and drew one card more
// (Elves seed 7036 gi5 d5 b20: Wirewood Lodge drawn, a T5 kill scored that realised T6).
TEST_CASE("Leaf-Crowned Visionary: a breakpoint child resumed from the node snapshot draws what a full apply draws")
{
    GameState s = EBase();
    s.players[0].library = Library{};
    ELib(s, "Taunting Elf", 30);    // the paid trigger's draw: an Elf, castable in the continuation
    ELib(s, "Wirewood Lodge", 31);  // a second paid trigger would draw this
    for (int i = 0; i < 6; ++i) { ELib(s, "Forest", 900 + i); }
    EForests(s, 4);                 // Llanowar 1 + pay 1 + Taunting Elf 1, with {G} spare for a phantom second pay
    EPut(s, "Leaf-Crowned Visionary", 1);
    EHand(s, "Llanowar Elves", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    // Human play (process-static MTG_HUMAN_PLAY) emits no pay-pin variants -- the viewer asks the human at the
    // trigger -- so there is no searched paid plan to resume, and nothing here to compare.
    if (HumanPlayActive()) { CHECK(std::none_of(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& q) { return q.mihail_pay_choice.IsPinned(); })); return; }
    const TurnSolver::Plan* paid = EFind(plans, [](const TurnSolver::Plan& q)
    { return ECountCasts(q) == 1 && IsCast(q.actions[0], "Llanowar Elves") && q.mihail_pay_choice.Paid(0); });
    REQUIRE(paid != nullptr);
    bool saw_taunting_cast = false;
    for (const int k : { 0, 1, 2, 3, TurnSolver::kBpEmptyChoice })
    {
        CAPTURE(k);
        ManaCost cap, res;
        GameState complete, resumed;
        REQUIRE(TurnSolver::BpResumeLineHoldForTest(s, *paid, /*is_pre_combat=*/true, cap, res,
                                                    &complete, &resumed, k));
        std::vector<int> ch, rh;
        for (const Card& c : complete.players[0].hand) { ch.push_back(c.m_number); }
        for (const Card& c : resumed.players[0].hand)  { rh.push_back(c.m_number); }
        CHECK(ch == rh);
        CHECK(complete.players[0].library.size() == resumed.players[0].library.size());
        CHECK(ETapped(complete) == ETapped(resumed));
        CHECK((EN(complete, 30) != nullptr) == (EN(resumed, 30) != nullptr));
        // Whether the continuation's own Taunting Elf trigger pays is that RANK's pin (ContPins::mihail, the next
        // test); either way the resumed child must agree with the from-scratch one (the hand check above).
        if (EN(complete, 30) != nullptr) { saw_taunting_cast = true; }
    }
    CHECK(saw_taunting_cast);   // some continuation rank casts the drawn Elf, so the case is exercised
}

// LOCKSTEP #21: a breakpoint CONTINUATION's own pay-{G} pin. The continuation enumeration emits Visionary pay
// variants (so a drawn Elf's own cast can pay again), but ContPins carried no pay pin, so those variants ran under
// the OUTER plan's pin at the continued ordinal -- past its bits -- and always declined: "pay {G} again for the Elf
// drawn off Visionary" was unfindable inside a searched continuation. The paying continuation must draw (the
// second card, Wirewood Lodge #31) and its declining twin must not, in the rollout (recording the line) and in the
// executor replaying that line.
TEST_CASE("Leaf-Crowned Visionary: a breakpoint continuation's own pay-{G} pin pays, in BOTH worlds")
{
    GameState s = EBase();
    s.players[0].library = Library{};
    ELib(s, "Taunting Elf", 30);    // the base plan's paid draw: an Elf, cast in the continuation
    ELib(s, "Wirewood Lodge", 31);  // the CONTINUATION's paid draw
    for (int i = 0; i < 6; ++i) { ELib(s, "Forest", 900 + i); }
    EForests(s, 4);                 // Llanowar + pay + Taunting Elf + pay
    EPut(s, "Leaf-Crowned Visionary", 1);
    EHand(s, "Llanowar Elves", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    if (HumanPlayActive()) { CHECK(std::none_of(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& q) { return q.mihail_pay_choice.IsPinned(); })); return; }
    const TurnSolver::Plan* paid = EFind(plans, [](const TurnSolver::Plan& q)
    { return ECountCasts(q) == 1 && IsCast(q.actions[0], "Llanowar Elves") && q.mihail_pay_choice.Paid(0); });
    REQUIRE(paid != nullptr);
    // The breakpoint state the continuation list is enumerated at: the paid Llanowar cast, then an EMPTY
    // continuation (Taunting Elf drawn, still in hand).
    TurnSolver::Plan prefix = *paid;
    prefix.bp_choice = TurnSolver::kBpEmptyChoice;
    prefix.bp_all    = true;
    GameState reached = s;
    TurnSolver::ApplyPlan(reached, prefix, true);
    REQUIRE(std::any_of(reached.players[0].hand.begin(), reached.players[0].hand.end(),
                        [](const Card& c) { return c.m_number == 30; }));
    const std::vector<TurnSolver::Plan> cands = TurnSolver::EnumerateBreakpointPlans(reached, true);
    auto cont_of = [&](bool pay) -> int
    {
        for (std::size_t i = 0; i < cands.size(); ++i)
        {
            const TurnSolver::Plan& c = cands[i];
            if (ECountCasts(c) != 1 || !IsCast(c.actions[0], "Taunting Elf")) { continue; }
            if (c.mihail_pay_choice.Paid(0) == pay && (pay || !c.mihail_pay_choice.IsPinned()))
            { return static_cast<int>(i); }
        }
        return -1;
    };
    for (const bool pay : { true, false })
    {
        CAPTURE(pay);
        const int k = cont_of(pay);
        REQUIRE(k >= 0);
        TurnSolver::Plan recorded = *paid;
        recorded.bp_at     = 0;
        recorded.bp_choice = k;
        GameState roll = s;
        TurnSolver::ApplyPlanWithRecordsForTest(roll, recorded, true);
        GameState real = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, recorded, true);
        eng.TakeTurn(real, true, EResolveAll);
        for (const GameState* g : { &roll, &real })
        {
            CAPTURE(g == &real);
            CHECK(EN(*g, 20) != nullptr);   // Llanowar Elves
            CHECK(EN(*g, 30) != nullptr);   // Taunting Elf, cast by the continuation
            const bool drew_lodge = std::any_of(g->players[0].hand.begin(), g->players[0].hand.end(),
                                                [](const Card& c) { return c.m_number == 31; });
            CHECK(drew_lodge == pay);
            CHECK(ETapped(*g) == (pay ? 4 : 3));
        }
    }
}

// ---- 7. CONCORDANT CROSSROADS: THE WORLD RULE ----------------------------------------------------------------

TEST_CASE("Concordant Crossroads: a second copy puts the older one into the graveyard (CR 704.5m), both worlds")
{
    GameState s = EBase();
    EForests(s, 1);
    EPut(s, "Concordant Crossroads", 10);
    EHand(s, "Concordant Crossroads", 20);
    {   // The SBA itself: one World permanent is left alone.
        GameState t = s;
        EnforceWorldRule(t);
        CHECK(EN(t, 10) != nullptr);
    }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = EFind(plans, [](const TurnSolver::Plan& p)
    { return ECountCasts(p) == 1 && IsCast(p.actions[0], "Concordant Crossroads"); });
    REQUIRE(cast != nullptr);
    GameState roll, real;
    EBoth(s, *cast, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(EN(*g, 10) == nullptr);           // the older one
        CHECK(EN(*g, 20) != nullptr);           // the newest survives
        const auto& gy = g->players[0].graveyard;
        CHECK(std::any_of(gy.begin(), gy.end(), [](const Card& c) { return c.m_number == 10; }));
    }
}

// ---- 8. ELVISH CLANCALLER ------------------------------------------------------------------------------------

TEST_CASE("Elvish Clancaller: the tutor finds only a card named Elvish Clancaller, in BOTH worlds")
{
    const CardParams& pp = ED("Elvish Clancaller").params;
    CHECK(TapTutorPutLibraryCandidate(pp, ED("Elvish Clancaller").card));
    CHECK_FALSE(TapTutorPutLibraryCandidate(pp, ED("Llanowar Elves").card));
    CHECK_FALSE(TapTutorPutLibraryCandidate(pp, ED("Joraga Warcaller").card));

    GameState s = EBase();
    s.players[0].library = Library{};
    ELib(s, "Llanowar Elves", 30);
    ELib(s, "Elvish Clancaller", 31);
    ELib(s, "Joraga Warcaller", 32);
    EForests(s, 6);
    EPut(s, "Elvish Clancaller", 1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<std::string> finds;
    const TurnSolver::Plan* tut = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (!IsAct(a, Action::AbilityMode::TapTutorPut)) { continue; }
            finds.insert(a.tutor_target.str());
            if (p.actions.size() == 1 && a.tutor_target.str() == "Elvish Clancaller") { tut = &p; }
        }
    }
    CHECK(finds.count("Llanowar Elves") == 0);
    CHECK(finds.count("Joraga Warcaller") == 0);
    REQUIRE(tut != nullptr);
    GameState roll, real;
    EBoth(s, *tut, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(EN(*g, 31) != nullptr);                     // put onto the battlefield
        CHECK(EN(*g, 1)->tapped);                         // the {T}
        CHECK(g->players[0].library.size() == 2u);
        CHECK(EPow(*g, 31) == 1 + 1);                     // pumped by the first Clancaller
    }
}
