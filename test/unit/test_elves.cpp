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
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <array>
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
    // Human play (process-static MTG_HUMAN_PLAY) offers ONE representative target per X, by design: the human
    // picks the real target on the live board at activation (ChooseTapTargetPumpTargetAtActivation), so the
    // menu does not fan targets out. That representative is the first legal creature -- the Magistrate itself,
    // also an Elf -- so the counter count is checked on whichever Elf the mode offers.
    const bool human = HumanPlayActive();
    if (human) { CHECK(targets.size() == 1u); }
    const TurnSolver::Plan* on_elf = EFind(plans, [human](const TurnSolver::Plan& p)
    {
        if (p.actions.size() != 1 || !IsAct(p.actions[0], Action::AbilityMode::TapTargetPump)) { return false; }
        const int v = p.actions[0].sac_victim_id;
        return v == 3 || v == 4 || (human && v == 1);
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

TEST_CASE("Immaculate Magistrate: a creature the SAME plan casts is a searched target, in BOTH worlds")
{
    // The Elves claude-play sweep's hole (2026-11-26): the activation runs in the trailing pass, after the
    // plan's casts, but its target list was read off the battlefield BEFORE them -- so "cast Joraga
    // Warcaller, then five counters on it" (24 damage: a 6/6 hasty Warcaller and three 6/6 Llanowars) was
    // unfindable at any budget. TapTargetPumpSamePlanTargets offers the hand creature, pinned to the copy
    // the cast lands, only in subsets that also cast it (SubsetHasMissingTrickTarget).
    GameState s = EBase();
    EForests(s, 1);
    EPut(s, "Concordant Crossroads", 2);   // haste: the Warcaller attacks
    EPut(s, "Immaculate Magistrate", 1);
    EPut(s, "Llanowar Elves", 3);
    EPut(s, "Llanowar Elves", 4);
    EPut(s, "Llanowar Elves", 5);
    EHand(s, "Joraga Warcaller", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool stranded = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool pin = false, cast = false;
        for (const Action& a : p.actions)
        {
            if (IsAct(a, Action::AbilityMode::TapTargetPump) && a.sac_victim_id == 20) { pin = true; }
            if (IsCast(a, "Joraga Warcaller")) { cast = true; }
        }
        if (pin && !cast) { stranded = true; }
    }
    CHECK_FALSE(stranded);   // never offered without the cast that creates its target
    // Human play keeps ONE battlefield representative and the viewer picks the target on the live board
    // at activation (ChooseTapTargetPumpTargetAtActivation), where the cast Warcaller already stands --
    // so the searched pin below exists only for the autonomous search.
    if (HumanPlayActive()) { return; }
    const TurnSolver::Plan* line = EFind(plans, [](const TurnSolver::Plan& p)
    {
        bool pin = false, cast = false;
        for (const Action& a : p.actions)
        {
            if (IsAct(a, Action::AbilityMode::TapTargetPump) && a.sac_victim_id == 20) { pin = true; }
            if (IsCast(a, "Joraga Warcaller") && !a.kicked) { cast = true; }
        }
        return pin && cast && p.actions.size() == 2;
    });
    REQUIRE(line != nullptr);
    GameState roll, real;
    EBoth(s, *line, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        REQUIRE(EN(*g, 20) != nullptr);
        CHECK(PlusCountersOn(*EN(*g, 20)) == 5);   // Magistrate + Warcaller + three Llanowar Elves
        CHECK(EN(*g, 1)->tapped);                  // the {T}
        CHECK(EPow(*g, 20) == 6);                  // 1/1 + five counters
        CHECK(EPow(*g, 3) == 6);                   // 1/1 + the Warcaller's +1/+1 per counter
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
    // Human play (process-static MTG_HUMAN_PLAY) emits NO pay-pin variants, by design: the viewer asks the human
    // at the trigger (the dig-chooser yes/no in FireOnCastTriggers). Unpinned with no chooser installed = decline,
    // in both worlds.
    if (HumanPlayActive())
    {
        CHECK(std::none_of(plans.begin(), plans.end(),
                           [](const TurnSolver::Plan& q) { return q.mihail_pay_choice.IsPinned(); }));
        const TurnSolver::Plan* p = EFind(plans, [](const TurnSolver::Plan& q)
        { return ECountCasts(q) == 1 && IsCast(q.actions[0], "Llanowar Elves"); });
        REQUIRE(p != nullptr);
        GameState roll, real;
        EBoth(s, *p, roll, real);
        for (const GameState* g : { &roll, &real })
        {
            CHECK(g->players[0].hand.empty());
            CHECK(ETapped(*g) == 1);
            CHECK(EN(*g, 20) != nullptr);
        }
        return;
    }
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

// ---- 9. DISPLAY LABELS + THE act= TOKEN (display only: no key, no digest) ------------------------------------

TEST_CASE("Elves labels: Clancaller names its find, Magistrate its counter count, Warcaller its kick count")
{
    const CardDefinition& clan = ED("Elvish Clancaller");
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapTutorPut, &clan).find("named Elvish Clancaller") != std::string::npos);
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapTutorPut, &clan).find("Merfolk") == std::string::npos);
    // The play digest keeps folding the pre-relabel text, so no fingerprint moves.
    CHECK(PermAbilityLabelForDigest(PermAbilityMode::TapTutorPut, &clan) == PermAbilityLabel(PermAbilityMode::TapTutorPut));
    const CardDefinition& sea = ED("Seahunter");
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapTutorPut, &sea) == PermAbilityLabel(PermAbilityMode::TapTutorPut));

    // Magistrate: 1 (itself) + 1 Llanowar on the board + 1 Llanowar cast by the plan before the trailing activation.
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Immaculate Magistrate", 1);
    EPut(s, "Llanowar Elves", 3);
    EHand(s, "Llanowar Elves", 20);
    TurnSolver::Plan p;
    Action cast; cast.kind = Action::Kind::CastFromHand; cast.card_name = "Llanowar Elves"; cast.def = &ED("Llanowar Elves");
    Action act; act.kind = Action::Kind::ActivatePermAbility; act.ability_mode = Action::AbilityMode::TapTargetPump;
    act.card_name = "Immaculate Magistrate"; act.def = &ED("Immaculate Magistrate"); act.sac_source_id = 1; act.sac_victim_id = 3;
    p.actions = { cast, act };
    CHECK(TurnSolver::TapTargetCountersProjected(s, p, act) == 3);
    CHECK(TurnSolver::TapTargetCountersProjected(s, p, cast) == 0);
    const std::string sum = TurnSolver::PlanLineSummary(p, s);
    CHECK(sum.find("3 +1/+1 counters") != std::string::npos);
    CHECK(sum.find("X=0") == std::string::npos);

    // Warcaller: the kicked variant's summary carries its count; the unkicked one is unchanged.
    Action w; w.kind = Action::Kind::CastFromHand; w.card_name = "Joraga Warcaller"; w.def = &ED("Joraga Warcaller");
    CHECK(TurnSolver::RepeatKickSummaryTag(w).empty());
    w.kicked = true; w.kick_count = 2;
    CHECK(TurnSolver::RepeatKickSummaryTag(w) == " (kicked x2)");
    TurnSolver::Plan pk; pk.actions = { w };
    TurnSolver::Plan pu = pk; pu.actions[0].kicked = false; pu.actions[0].kick_count = 0;
    CHECK(TurnSolver::PlanLineSummary(pk, s) != TurnSolver::PlanLineSummary(pu, s));
}

TEST_CASE("CheckLine: act=<card> with the card only in hand is a board activation with no source, not a hand cast")
{
    using V = TurnSolver::LineCheck::Verdict;
    GameState s = EBase();
    EForests(s, 6);
    EHand(s, "Elvish Clancaller", 20);
    TurnSolver::LineSpec act;
    act.casts.push_back("Elvish Clancaller");
    act.board_acts.push_back("Elvish Clancaller");
    const auto r = TurnSolver::CheckLine(s, true, act);
    CHECK(r.verdict == V::Illegal);
    CHECK(r.reason.find("act=Elvish Clancaller") != std::string::npos);
    INFO(r.reason);
    // The same name as cast= is the hand cast, and is legal.
    TurnSolver::LineSpec cast;
    cast.casts.push_back("Elvish Clancaller");
    CHECK(TurnSolver::CheckLine(s, true, cast).verdict != V::Illegal);
}

// ---- 10. GENESIS WAVE: THE POST-PUT CONTINUATION (site 10's battlefield-put half) ----------------------------
// Elves seed 9500006 T4: the Wave put a hasted Archdruid (Concordant Crossroads beside it) whose mana paid for the
// hand's Visionary + Ezuri, but every plan is priced against the battlefield the main STARTED from and nothing
// re-decided the turn after the put, so the line was unfindable at any depth or budget. Small form: Wave X=2 puts
// Llanowar Elves + Crossroads; the hasted Llanowar's {G} pays the Elvish Mystic in hand. Checked as the whole
// contract: the outcome predicate, the plan mark, a searched continuation that casts the Mystic, and the executor
// realising the recorded continuation exactly (lockstep).
TEST_CASE("Genesis Wave: the put opens site 10 -- a continuation spends the hasted dork it put, in BOTH worlds")
{
    GameState initial = EBase();
    initial.players[0].library = Library{};
    ELib(initial, "Llanowar Elves", 60);
    ELib(initial, "Concordant Crossroads", 61);
    for (int i = 0; i < 6; ++i) { ELib(initial, "Forest", 900 + i); }
    EForests(initial, 5);                       // exactly {2}{G}{G}{G}: nothing left after the Wave
    EHand(initial, "Genesis Wave", 70);
    EHand(initial, "Elvish Mystic", 71);

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(initial, true);
    const TurnSolver::Plan* wave = EFind(plans, [](const TurnSolver::Plan& q)
    { return ECountCasts(q) == 1 && q.actions.size() == 1 && IsCast(q.actions[0], "Genesis Wave")
          && q.actions[0].chosen_x == 2; });
    REQUIRE(wave != nullptr);
    // No base plan casts the X=2 Wave AND the Mystic: the Mystic is payable only off the mana that Wave puts. (A
    // smaller X leaves {G} for it, and the unpruned / human menus offer X=1, so the check names X=2.)
    CHECK(EFind(plans, [](const TurnSolver::Plan& q)
    { return std::any_of(q.actions.begin(), q.actions.end(), [](const Action& a)
                         { return IsCast(a, "Genesis Wave") && a.chosen_x == 2; })
          && std::any_of(q.actions.begin(), q.actions.end(), [](const Action& a) { return IsCast(a, "Elvish Mystic"); }); })
          == nullptr);
    CHECK((TurnSolver::PlanOpensBreakpointForTest(initial, *wave) & (1 << 10)) != 0);
    // Human play arms no deferred re-solve (both worlds stand down; the human gets the segment re-prompt instead),
    // so the continuation half below is the searched engine's only. HumanPlayActive() is process-static.
    if (HumanPlayActive()) { return; }

    // The board right after the put (no continuation).
    TurnSolver::Plan prefix = *wave;
    prefix.bp_choice = TurnSolver::kBpEmptyChoice;
    prefix.bp_all    = true;
    GameState reached = initial;
    TurnSolver::ApplyPlan(reached, prefix, true);
    REQUIRE(EN(reached, 60) != nullptr);
    REQUIRE(EN(reached, 61) != nullptr);
    CHECK(TurnSolver::CastPutACardPermanent(TurnSolver::PutWindowNumbers(initial), reached, "Genesis Wave", false));
    {   // ...and a plain creature cast from hand puts nothing: the cast card is not a put
        GameState after = initial;
        const std::vector<int> snap = TurnSolver::PutWindowNumbers(after);
        after.players[0].hand.erase(std::remove_if(after.players[0].hand.begin(), after.players[0].hand.end(),
                                                   [](const Card& c) { return c.m_number == 71; }),
                                    after.players[0].hand.end());
        EPut(after, "Elvish Mystic", 71, false, true);
        CHECK_FALSE(TurnSolver::CastPutACardPermanent(snap, after, "Elvish Mystic", false));
    }

    const std::vector<TurnSolver::Plan> cands = TurnSolver::EnumerateBreakpointPlans(reached, true);
    const auto mystic = std::find_if(cands.begin(), cands.end(), [](const TurnSolver::Plan& p)
    { return std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a) { return IsCast(a, "Elvish Mystic"); }); });
    REQUIRE(mystic != cands.end());

    TurnSolver::Plan recorded = *wave;
    recorded.bp_at     = 0;
    recorded.bp_choice = static_cast<int>(mystic - cands.begin());
    GameState roll = initial;
    TurnSolver::ApplyPlanWithRecordsForTest(roll, recorded, true);
    CHECK(EN(roll, 71) != nullptr);   // the continuation cast the Mystic off the put Llanowar's {G}
    CHECK(std::any_of(recorded.breakpoint_actions.begin(), recorded.breakpoint_actions.end(),
                      [](const Action& a) { return IsCast(a, "Elvish Mystic"); }));

    GameState exec = initial;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, recorded, true);
    eng.TakeTurn(exec, true, EResolveAll);
    CHECK(EN(exec, 71) != nullptr);
    REQUIRE(exec.battlefield.size() == roll.battlefield.size());
    for (std::size_t i = 0; i < roll.battlefield.size(); ++i)
    {
        INFO("permanent ", roll.battlefield[i].card.m_number);
        CHECK(exec.battlefield[i].card.m_number == roll.battlefield[i].card.m_number);
        CHECK(exec.battlefield[i].tapped == roll.battlefield[i].tapped);
    }
}
// ---- 11. ELVES MANA ACCOUNTING (the 2026-11-26 claude-play sweep's mana cluster) ---------------------------------

namespace {
bool PlanCastsInOrder(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    std::vector<std::string> got;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { got.push_back(a.card_name.str()); } }
    return got == names;
}
}   // namespace

TEST_CASE("Elves mana: a HASTED Priest of Titania is credited its Elf count, not one (seed 9500015 T3)")
{
    // Concordant Crossroads; two Forests, Joraga Warcaller, Priest of Titania (2 Elves -> 2). Visionary off the
    // Forests, Priest off the first Priest (3 Elves -> 3, {G} floats), then the HASTED second Priest taps for 4.
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Concordant Crossroads", 1);
    EPut(s, "Joraga Warcaller", 2);
    EPut(s, "Priest of Titania", 3);
    EHand(s, "Leaf-Crowned Visionary", 20);
    EHand(s, "Priest of Titania", 21);
    EHand(s, "Ezuri, Renegade Leader", 22);
    CHECK(ScaledDorkEntryYield(s, 0, ED("Priest of Titania")) == 3);   // Warcaller + Priest + itself
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = EFind(plans, [](const TurnSolver::Plan& q)
    {
        return PlanCastsInOrder(q, { "Leaf-Crowned Visionary", "Priest of Titania", "Ezuri, Renegade Leader" })
            && !q.mihail_pay_choice.Paid(0) && !q.mihail_pay_choice.Paid(1);
    });
    REQUIRE(p != nullptr);
    GameState roll, real;
    EBoth(s, *p, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(EN(*g, 20) != nullptr);
        CHECK(EN(*g, 21) != nullptr);
        CHECK(EN(*g, 22) != nullptr);
    }
    // The validator agrees (the walk resolves the Visionary and the hasted Priest before pricing Ezuri).
    TurnSolver::LineSpec spec;
    spec.casts = { "Leaf-Crowned Visionary", "Priest of Titania", "Ezuri, Renegade Leader" };
    const TurnSolver::LineCheck chk = TurnSolver::CheckLine(s, true, spec, &plans);
    CHECK(chk.verdict != TurnSolver::LineCheck::Verdict::Illegal);
}

TEST_CASE("Elves mana: with no live Priest, only the hasted Priest's own count can fund the line")
{
    // Concordant Crossroads, three Forests, Joraga Warcaller. Priest of Titania off two Forests; HASTED it taps for 2
    // (Warcaller + itself), which with the third Forest pays Ezuri's {1}{G}{G}. Credited one mana, the pair read as
    // 4 of the 5 it costs and was never enumerated (no live Priest on the board, so no growth credit covers it).
    GameState s = EBase();
    EForests(s, 3);
    EPut(s, "Concordant Crossroads", 1);
    EPut(s, "Joraga Warcaller", 2);
    EHand(s, "Priest of Titania", 21);
    EHand(s, "Ezuri, Renegade Leader", 22);
    CHECK(ScaledDorkEntryYield(s, 0, ED("Priest of Titania")) == 2);   // Warcaller + itself
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = EFind(plans, [](const TurnSolver::Plan& q)
    { return PlanCastsInOrder(q, { "Priest of Titania", "Ezuri, Renegade Leader" }); });
    REQUIRE(p != nullptr);
    GameState roll, real;
    EBoth(s, *p, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(EN(*g, 21) != nullptr);
        CHECK(EN(*g, 22) != nullptr);
    }
    // The validator, handed no menu, prices it with its own real-payment walk: the Priest resolves onto the copy.
    TurnSolver::LineSpec spec;
    spec.casts = { "Priest of Titania", "Ezuri, Renegade Leader" };
    const TurnSolver::LineCheck chk = TurnSolver::CheckLine(s, true, spec, nullptr);
    CHECK(chk.verdict != TurnSolver::LineCheck::Verdict::Illegal);
}

TEST_CASE("Elves mana: a batch the Priests' own Elves grow is not prepaid at the plan-start count (seed 9500015 T3)")
{
    // Two untapped Priests at 3 (Warcaller + both Priests), nothing else. {Visionary, Ezuri} = 5 of the 6 the
    // plan-start count makes, so the old whole-turn prepay tapped both Priests at 3 and floated {G}. Visionary
    // first, then the second Priest at 4, floats {G}{G}.
    GameState s = EBase();
    EPut(s, "Joraga Warcaller", 2);
    EPut(s, "Priest of Titania", 3);
    EPut(s, "Priest of Titania", 4);
    EHand(s, "Leaf-Crowned Visionary", 20);
    EHand(s, "Ezuri, Renegade Leader", 22);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = EFind(plans, [](const TurnSolver::Plan& q)
    {
        return PlanCastsInOrder(q, { "Leaf-Crowned Visionary", "Ezuri, Renegade Leader" })
            && !q.mihail_pay_choice.Paid(0);
    });
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, /*is_pre_combat=*/true);
    REQUIRE(EN(roll, 22) != nullptr);
    CHECK(roll.floating_mana.Total() + roll.floating_creature_mana.Total() == 2);
}

TEST_CASE("Elves mana: the prepay does not tap Marwyn before an Elf in the same batch grows it (seed 9500008 T6)")
{
    // Forest x2, Llanowar Elves, Leaf-Crowned Visionary, Marwyn (1 + the Visionary = power 2). {Taunting Elf,
    // Immaculate Magistrate} paying the Visionary's {G} on Taunting Elf's cast = 6: legal only with Marwyn tapped
    // after Taunting Elf enters (its counter -> power 3).
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Llanowar Elves", 2);
    EPut(s, "Leaf-Crowned Visionary", 3);
    EPut(s, "Marwyn, the Nurturer", 4);
    EHand(s, "Taunting Elf", 20);
    EHand(s, "Immaculate Magistrate", 21);
    const CardDefinition& marwyn = ED("Marwyn, the Nurturer");
    CHECK(CastGrowsScaledDork(marwyn, *EN(s, 4), ED("Taunting Elf")));
    CHECK(CastGrowsScaledDork(marwyn, *EN(s, 4), ED("Elvish Clancaller")));   // a lord covering an Elf Marwyn
    CHECK_FALSE(CastGrowsScaledDork(marwyn, *EN(s, 4), ED("Concordant Crossroads")));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = EFind(plans, [](const TurnSolver::Plan& q)
    {
        return PlanCastsInOrder(q, { "Taunting Elf", "Immaculate Magistrate" })
            && q.mihail_pay_choice.Paid(0) && !q.mihail_pay_choice.Paid(1);
    });
    REQUIRE(p != nullptr);
    GameState roll, real;
    EBoth(s, *p, roll, real);
    for (const GameState* g : { &roll, &real })
    {
        CHECK(EN(*g, 20) != nullptr);
        CHECK(EN(*g, 21) != nullptr);
        CHECK(g->players[0].hand.size() == 1u);   // the paid Visionary draw
    }
}

TEST_CASE("Elves mana: Wirewood Lodge's burst is fed from the float when the float is the only {G} (seed 9500005 T3)")
{
    // After Clancaller and a Brigade: floating {G}, Priest of Titania TAPPED (4 Elves), Wirewood Lodge untapped.
    // {2}{G}: the {G} feeds the Lodge, the Priest untaps and taps again for 4 -- the float drain used to spend the
    // {G} on the bill and strand the Lodge.
    GameState s = EBase();
    EPut(s, "Elvish Mystic", 1, /*tapped=*/true);
    EPut(s, "Priest of Titania", 2, /*tapped=*/true);
    EPut(s, "Elvish Clancaller", 3, false, /*sick=*/true);
    EPut(s, "Galadhrim Brigade", 4, false, /*sick=*/true);
    EPut(s, "Wirewood Lodge", 5);
    s.floating_mana.green = 1;
    ManaCost cost; cost.generic = 2; cost.green = 1;
    GameState pay = s;
    REQUIRE(TapForCostDirect(pay, cost, /*for_creature=*/true));
    CHECK(EN(pay, 5)->tapped);
    CHECK(pay.floating_mana.Total() == 1);   // {G}x4 for the feed's {G}, minus {2}{G}
}

TEST_CASE("Elves mana: a Cavern-class float unit pays a noncreature cost only as {C} (seed 9500008 T6)")
{
    for (const bool creature : { false, true })
    {
        CAPTURE(creature);
        GameState s = EBase();
        s.floating_mana.wild = 1;
        SetCcoFloatWild(s, 1);
        REQUIRE(CcoFloatWild(s) == 1);
        ManaCost g; g.green = 1;
        GameState a = s;
        CHECK(TapForCostDirect(a, g, creature) == creature);   // a {G} pip: creature spells only
        if (!creature) { CHECK(a.floating_mana.wild == 1); CHECK(CcoFloatWild(a) == 1); }   // untouched
        else { CHECK(a.floating_mana.Total() == 0); CHECK(CcoFloatWild(a) == 0); }
        ManaCost one; one.generic = 1;
        GameState b = s;
        CHECK(TapForCostDirect(b, one, creature));              // a generic pip: anything (as {C})
        CHECK(b.floating_mana.Total() == 0);
    }
    // A noncreature {G} with a Forest-made {G} beside it takes the {G} and leaves the Cavern unit marked.
    GameState s = EBase();
    s.floating_mana.wild = 1;
    s.floating_mana.green = 1;
    SetCcoFloatWild(s, 1);
    ManaCost g; g.green = 1;
    REQUIRE(TapForCostDirect(s, g, /*for_creature=*/false));
    CHECK(s.floating_mana.green == 0);
    CHECK(s.floating_mana.wild == 1);
    CHECK(CcoFloatWild(s) == 1);
}

// The SIDE PATHS that spend the float without the shared payer: PayCostFromFloat (an optional trigger's cost, an
// activation's feed) and the manual tap of a hybrid filter (its {U/R} feed is an activation cost). Each must see
// the prepay's Cavern-class unit as {C} only: never a coloured pip, always a generic one, and the mark survives a
// refusal (CR 106.6: "Spend this mana only to cast a creature spell").
TEST_CASE("Elves mana: a Cavern-class float unit is {C} on every float-only path (trigger cost, filter feed)")
{
    SUBCASE("PayCostFromFloat / PayOptionalTriggerCost: {G} refused, {1} paid")
    {
        GameState s = EBase();
        s.floating_mana.wild = 1;
        SetCcoFloatWild(s, 1);
        ManaCost g; g.green = 1;
        GameState a = s;
        CHECK_FALSE(PayCostFromFloat(a, g));
        CHECK(a.floating_mana.wild == 1);
        CHECK(a.floating_mana.colorless == 0);
        CHECK(CcoFloatWild(a) == 1);                 // the mark survives the refusal
        GameState b = s;
        CHECK_FALSE(PayOptionalTriggerCost(b, g));   // Leaf-Crowned Visionary-class "you may pay {G}"
        CHECK(CcoFloatWild(b) == 1);
        ManaCost one; one.generic = 1;
        GameState c = s;
        CHECK(PayCostFromFloat(c, one));
        CHECK(c.floating_mana.Total() == 0);
        CHECK(CcoFloatWild(c) == 0);
        // An unmarked any-colour unit still pays the {G} (the scope is inert without the mark).
        GameState d = EBase();
        d.floating_mana.wild = 1;
        CHECK(PayCostFromFloat(d, g));
    }
    SUBCASE("manual tap of a hybrid filter: the Cavern unit cannot feed {U/R} (the feed reads produced colours only)")
    {
        GameState s = EBase();
        EPut(s, "Cascade Bluffs", 40);
        s.floating_mana.wild = 1;
        SetCcoFloatWild(s, 1);
        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=Cascade Bluffs#40:UU", t));
        GameState a = s;
        CHECK_FALSE(ApplyHumanPreTap(a, t).empty());   // refused: the only float is a Cavern unit
        CHECK(a.floating_mana.wild == 1);
        CHECK(CcoFloatWild(a) == 1);
        CHECK_FALSE(EN(a, 40)->tapped);
    }
}

// ---- USER 2026-11-27: the kick-count bound, and the beyond-horizon Visionary policy ----------------------------
// "I think Kick counts should be capped at lethal size of 20", then on 2HG: "Yeah, we may want to scale it up for 2HG
// arguably." The DB synthesizes the "(Kicked xK)" faces K = 1..kMaxMultikick (40, the largest starting life of any
// format); CollectActions offers every affordable K up to the opponent's STARTING life (gamesetup::StartingLife()):
// 20 standard, 30 in Two-Headed Giant. MTG_ELVES_R2=0 (TEMPORARY A/B selector) is the round-1 flat 20.
namespace {
int EWarcallerTopK(GameState s, int forests)
{
    EForests(s, forests);
    EHand(s, "Joraga Warcaller", 20);
    int kmax = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        { if (IsCast(a, "Joraga Warcaller") && a.kicked) { kmax = std::max<int>(kmax, a.kick_count); } }
    }
    return kmax;
}
struct ELife
{
    int saved;
    explicit ELife(int life) : saved(gamesetup::t_setup.starting_life) { gamesetup::t_setup.starting_life = life; }
    ~ELife() { gamesetup::t_setup.starting_life = saved; }
};
}   // namespace

TEST_CASE("Joraga Warcaller: the kick-count cap is the opponent's starting life -- 20 standard, 30 in 2HG")
{
    CHECK(kMaxMultikick == 40);
    for (const char* n : { "Joraga Warcaller", "Galadhrim Brigade" })
    {
        CAPTURE(n);
        (void)ED(n);
        CHECK(CardDatabase::Instance().Lookup(MultikickFaceName(n, 40)) != nullptr);
        CHECK(CardDatabase::Instance().Lookup(MultikickFaceName(n, 41)) == nullptr);
    }
    {
        ELife life(20);
        CHECK(MultikickCountCap() == 20);
        // {G} + 20 x {1}{G} = 41 mana: K = 20 affordable; 21 (43) is affordable too but past the cap.
        CHECK(EWarcallerTopK(EBase(), 45) == 20);
    }
    {
        ELife life(30);   // Two-Headed Giant: the opposing team's 30 life
        CHECK(MultikickCountCap() == 30);
        CHECK(EWarcallerTopK(EBase(), 65) == 30);   // 61 mana pays K = 30; 31 (63) is past the cap
        CHECK(EWarcallerTopK(EBase(), 45) == 22);   // below the cap the pool binds: 45 = {G} + 22 x {1}{G}
        const std::int8_t saved = heurarm::t_arm[heurarm::ELVES_R2];
        heurarm::t_arm[heurarm::ELVES_R2] = 0;     // round 1: the flat 20
        CHECK(MultikickCountCap() == 20);
        CHECK(EWarcallerTopK(EBase(), 65) == 20);
        heurarm::t_arm[heurarm::ELVES_R2] = saved;
    }
}

// "Leaf crowned-visionary should probably not decline the draw automatically even beyond the search horizon" ...
// "If you don't have anything else to use the mana for it should draw." A greedy plan (GreedyPermit: the horizon
// playout / the depth-0 runner) carries MihailPayChoices::PaySpare; its unpinned triggers pay when the {G} is spare.
namespace {
MihailPayChoices ESpare(bool pre_combat, const ManaCost& reserve = {})
{
    MihailPayChoices c;
    c.SetPaySpare(pre_combat, reserve);
    return c;
}
ManaCost EG(int generic, int green) { ManaCost c; c.generic = generic; c.green = green; return c; }
std::size_t EFire(GameState s, const MihailPayChoices& c, const ManaCost* line = nullptr)
{
    s.scripted_mihail_pay_choice = c;
    if (line != nullptr)
    {
        LineUnpaidCostScope luc(*line);
        FireOnCastTriggers(s, ED("Llanowar Elves"), /*mana_spent=*/1);
        return s.players[0].hand.size();
    }
    FireOnCastTriggers(s, ED("Llanowar Elves"), /*mana_spent=*/1);
    return s.players[0].hand.size();
}
}   // namespace

TEST_CASE("Leaf-Crowned Visionary beyond the horizon: an unpinned trigger pays only when the {G} is spare")
{
    GameState s = EBase();
    EPut(s, "Leaf-Crowned Visionary", 1);
    EForests(s, 2);
    // Inside the search window (no pay_spare) unpinned still declines.
    CHECK(EFire(s, MihailPayChoices{}) == 0u);
    // Spare: nothing else in the line.
    CHECK(EFire(s, ESpare(true)) == 1u);
    CHECK(EFire(s, ESpare(false)) == 1u);
    // The rest of the line owes {1}{G}: two Forests cover it OR the {G}, not both -> decline; three cover both.
    const ManaCost line = EG(1, 1);
    CHECK(EFire(s, ESpare(true), &line) == 0u);
    {
        GameState t = s; EForests(t, 1, 200);
        CHECK(EFire(t, ESpare(true), &line) == 1u);
    }
    // The plan's activations (the reserve) are not in the line hold: reserved the same way.
    CHECK(EFire(s, ESpare(true, EG(1, 1))) == 0u);
    CHECK(EFire(s, ESpare(true, EG(0, 1))) == 1u);
    // An empty library: no draw to buy.
    {
        GameState t = s; while (!t.players[0].library.empty()) { (void)t.players[0].library.DrawTop(); }
        CHECK(EFire(t, ESpare(true)) == 0u);
    }
    // A searched pin is never overridden: pinned decline stays a decline.
    {
        MihailPayChoices c = ESpare(true);
        c.Pin(1);
        CHECK(EFire(s, c) == 0u);
    }
}

TEST_CASE("Leaf-Crowned Visionary beyond the horizon: before combat the {G} may not cost an Ezuri pump")
{
    GameState s = EBase();
    EPut(s, "Leaf-Crowned Visionary", 1);
    EPut(s, "Ezuri, Renegade Leader", 2);
    // Five Forests = exactly one {2}{G}{G}{G} pump: the {G} would cost it before combat, not after.
    EForests(s, 5);
    CHECK(EFire(s, ESpare(true)) == 0u);
    CHECK(EFire(s, ESpare(false)) == 1u);
    // Six: one pump either way -> spare.
    EForests(s, 1, 200);
    CHECK(EFire(s, ESpare(true)) == 1u);
}

TEST_CASE("Leaf-Crowned Visionary beyond the horizon: before combat the {G} may not tap an attack-ready creature")
{
    // USER: "If you don't have anything else to use the mana for it should draw" -- an attack-ready dork's other use
    // before combat is attacking (smoke d0 gi130: Llanowar Elves + Elvish Mystic tapped for draws, attack 8 -> 2).
    GameState s = EBase();
    EPut(s, "Leaf-Crowned Visionary", 1);
    const int dork = EPut(s, "Llanowar Elves", 2);
    // The only {G} is the attack-ready dork: decline before combat, pay after.
    CHECK(EFire(s, ESpare(true)) == 0u);
    CHECK(EFire(s, ESpare(false)) == 1u);
    // A summoning-sick dork cannot attack: its {G} is spare... except it cannot tap either; a Forest beside it pays.
    {
        GameState t = EBase();
        EPut(t, "Leaf-Crowned Visionary", 1);
        EPut(t, "Llanowar Elves", 2, /*tapped=*/false, /*sick=*/true);
        EForests(t, 1);
        CHECK(EFire(t, ESpare(true)) == 1u);
    }
    // A Forest beside the attack-ready dork: pay, and the dork stays untapped to attack.
    {
        GameState t = s; EForests(t, 1);
        t.scripted_mihail_pay_choice = ESpare(true);
        FireOnCastTriggers(t, ED("Llanowar Elves"), /*mana_spent=*/1);
        CHECK(t.players[0].hand.size() == 1u);
        CHECK_FALSE(t.battlefield[static_cast<std::size_t>(dork)].tapped);
    }
}

TEST_CASE("Leaf-Crowned Visionary beyond the horizon: greedy Solve marks its plan; both worlds draw the same card")
{
    GameState s = EBase();
    EForests(s, 2);
    EPut(s, "Leaf-Crowned Visionary", 1);
    EHand(s, "Llanowar Elves", 20);
    const TurnSolver::Plan g =
        TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    CHECK(g.mihail_pay_choice.PaySpare());
    CHECK_FALSE(g.mihail_pay_choice.IsPinned());
    CHECK(g.mihail_pay_choice.SparePreCombat());
    REQUIRE(std::any_of(g.actions.begin(), g.actions.end(),
                        [](const Action& a) { return IsCast(a, "Llanowar Elves"); }));
    GameState roll, real;
    EBoth(s, g, roll, real);
    for (const GameState* gs : { &roll, &real })
    {
        CHECK(gs->players[0].hand.size() == 1u);   // the paid draw
        CHECK(ETapped(*gs) == 2);
    }
    CHECK(roll.players[0].hand[0].m_number == real.players[0].hand[0].m_number);
    // No pay-draw source: the plan is not marked (every other deck byte-identical).
    GameState n = EBase();
    EForests(n, 2);
    EHand(n, "Llanowar Elves", 20);
    CHECK_FALSE(TurnSolver::Solve(n, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0))
                    .mihail_pay_choice.PaySpare());
}

// ---- MTG_ELVES_TAP_ORDER (TEMPORARY, default OFF; USER 2026-11-27: "we should take control of the tap order a bit in
// the heuristic provider to make sure that we maintain whatever thing we want to maintain, whether that is the
// attackers or the scaling mana") ---------------------------------------------------------------------------------
// Priest of Titania with 5 Elves (Priest, 2 attack-capable Llanowars, 2 Clancallers) taps for 5; no lands. Galadhrim
// Brigade squad x1 costs {2}{G} + {1}{G} = 5. PRESERVE ATTACKERS (a pre-combat sink plan): the one Priest tap pays it
// and both Llanowars stay untapped to attack, in BOTH worlds. A plan with no scaled sink (the unkicked Brigade, 3) keeps
// the inherited order whatever the selector says.
namespace {
struct ETapArm
{
    std::int8_t saved;
    explicit ETapArm(int v) : saved(heurarm::t_arm[heurarm::ELVES_TAP_ORDER])
    { heurarm::t_arm[heurarm::ELVES_TAP_ORDER] = static_cast<std::int8_t>(v); }
    ~ETapArm() { heurarm::t_arm[heurarm::ELVES_TAP_ORDER] = saved; }
};
GameState ETapBoard()
{
    static const ElvesProvider prov;
    GameState s = EBase();
    s.m_provider = &prov;
    EPut(s, "Priest of Titania", 10);
    EPut(s, "Llanowar Elves", 11);
    EPut(s, "Llanowar Elves", 12);
    EPut(s, "Elvish Clancaller", 13);
    EPut(s, "Elvish Clancaller", 14);
    EHand(s, "Galadhrim Brigade", 20);
    return s;
}
// Tapped flags of the Priest and the two Llanowars after the Brigade plan with squad count k, per world.
std::vector<std::array<bool, 3>> ETapAfter(const GameState& s, int k)
{
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = EFind(plans, [k](const TurnSolver::Plan& q)
    {
        if (ECountCasts(q) != 1) { return false; }
        for (const Action& a : q.actions) { if (IsCast(a, "Galadhrim Brigade") && a.kick_count == k) { return true; } }
        return false;
    });
    REQUIRE(p != nullptr);
    GameState roll, real;
    EBoth(s, *p, roll, real);
    std::vector<std::array<bool, 3>> out;
    for (const GameState* g : { &roll, &real })
    {
        REQUIRE(EN(*g, 10) != nullptr);
        out.push_back({ EN(*g, 10)->tapped, EN(*g, 11)->tapped, EN(*g, 12)->tapped });
    }
    return out;
}
}   // namespace

TEST_CASE("Elves tap order: a sink turn PRESERVES ATTACKERS -- the Priest pays the squad, both Llanowars stay untapped")
{
    const GameState s = ETapBoard();
    std::vector<std::array<bool, 3>> off, on;
    { ETapArm a(0); off = ETapAfter(s, 1); }
    { ETapArm a(1); on  = ETapAfter(s, 1); }
    for (const auto& w : on)
    {
        CHECK(w[0]);         // the Priest tapped for 5
        CHECK_FALSE(w[1]);   // both Llanowars untapped
        CHECK_FALSE(w[2]);
    }
    // The inherited order (selector off), recorded for the report: the Brigade is an Elf, so the plan "casts scaler
    // food" and the Priest ranks LAST -- the Llanowars tap first.
    for (const auto& w : off) { MESSAGE("off: priest=" << w[0] << " llanowar1=" << w[1] << " llanowar2=" << w[2]); }
    CHECK(ETapArm(-1).saved == -1);   // the selector is unset by default (env default OFF)
}

TEST_CASE("Elves tap order: a plan with no scaled sink keeps the inherited (scaling-preserving) order")
{
    const GameState s = ETapBoard();
    std::vector<std::array<bool, 3>> off, on;
    { ETapArm a(0); off = ETapAfter(s, 0); }
    { ETapArm a(1); on  = ETapAfter(s, 0); }
    CHECK(off == on);
}
