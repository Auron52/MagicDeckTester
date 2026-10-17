// Unit tests for the Colossus Hammer claude-play sweep fixes (2026-10-17):
//   * DEFECT 1 -- an Equip onto a Lizard Blades the same plan reconfigures away. (a) the enumeration
//     offered it (`cast: Colossus Hammer, reconfigure Lizard Blades -> Kor Duelist, equip Colossus
//     Hammer -> Lizard Blades`, seed 2100003 T5); (b) the trailing pass, finding a non-creature host,
//     still PAID the equip -- {0}, it being Forge Anew's waived one -- and so burned the turn's free
//     first equip on an attach ApplyEquip refused. Both worlds.
//   * DEFECT 2 -- dominated no-op attach sequences with a SAME-PLAN HAND Equipment (seeds 2100003,
//     2100017, 2100018): a Theft overwritten by a later Equip / Theft of the one copy cast, a Theft onto
//     a Lizard the plan reconfigures (the Hammer falls off), and a Theft of an attached Lizard the plan
//     then unattaches.
// Path to Exile's human default target (DEFECT 3) is tested in test_path_to_exile.cpp.
//
// The folds are judged on what they DROP (never a plan that is not dominated by one still offered)
// and on what they KEEP (the shapes whose end state really differs) -- both are asserted below.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <functional>
#include <string>
#include <vector>

namespace
{

void EnsureCardsAf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefAf(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshAf()
{
    EnsureCardsAf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefAf("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int PutAf(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefAf(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return number;
}

int HandAf(GameState& s, const std::string& name, int number)
{
    Card c = DefAf(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

const Permanent* FindAf(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int TappedLandsAf(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

Action EquipAf(const std::string& name, int src, int host, int generic)
{
    Action a;
    a.kind          = Action::Kind::Equip;
    a.card_name     = name;
    a.def           = &DefAf(name);
    a.sac_source_id = src;
    a.sac_victim_id = host;
    a.cost          = ManaCost{};
    a.cost.generic  = generic;
    return a;
}

void ResolveAllAf(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// Rollout (ApplyPlan) and executor (AIEngine committed-line pop) realisations of one plan.
GameState RollAf(const GameState& s, const TurnSolver::Plan& p)
{
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    return t;
}
GameState ExecAf(const GameState& s, const TurnSolver::Plan& p)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState t = s;
    eng.TakeTurn(t, true, ResolveAllAf);
    return t;
}

bool IsTheftAf(const Action& a)
{
    return a.kind == Action::Kind::CastFromHand && a.attach_equip_id != 0;
}
bool IsReconfOf(const Action& a, int piece)
{
    return a.kind == Action::Kind::Equip && a.sac_source_id == piece;
}
bool IsUnattachOf(const Action& a, int piece)
{
    return a.kind == Action::Kind::ActivatePermAbility
        && a.ability_mode == Action::AbilityMode::ReconfigureUnattach && a.sac_source_id == piece;
}

int CountIf(const TurnSolver::Plan& p, const std::function<bool(const Action&)>& f)
{
    int n = 0;
    for (const Action& a : p.actions) { if (f(a)) { ++n; } }
    return n;
}

}   // namespace

// ---- DEFECT 1(b): the apply side, both worlds -----------------------------------------------------

TEST_CASE("Attach folds 1b: an equip onto a Lizard reconfigured earlier in the pass is not activated -- "
          "nothing paid, Forge Anew's waiver kept, in BOTH worlds")
{
    GameState s = FreshAf();
    PutAf(s, "Forge Anew", 1);
    const int lizard  = PutAf(s, "Lizard Blades", 2);
    const int duelist = PutAf(s, "Kor Duelist", 3);
    const int hammer  = PutAf(s, "Colossus Hammer", 4);
    PutAf(s, "Mountain", 50);
    PutAf(s, "Mountain", 51);
    TurnSolver::Plan p;
    p.actions = { EquipAf("Lizard Blades", lizard, duelist, 2), EquipAf("Colossus Hammer", hammer, lizard, 8) };
    p.value   = 1;
    // The Hammer's equip is the plan's waived one (the most expensive) -- the waiver it must not burn.
    REQUIRE(TurnSolver::StoriedWaivedEquipId(s, p.actions) == hammer);
    for (int world = 0; world < 2; ++world)
    {
        INFO((world == 0 ? "rollout (ApplyPlan)" : "executor (AIEngine)"));
        const GameState t = world == 0 ? RollAf(s, p) : ExecAf(s, p);
        REQUIRE(FindAf(t, lizard) != nullptr);
        CHECK(FindAf(t, lizard)->equipped_to == duelist);              // the reconfigure happened
        CHECK_FALSE(FindAf(t, lizard)->card.IsCreature());
        CHECK(FindAf(t, hammer)->equipped_to == 0);                    // the equip had no legal target
        CHECK_FALSE(t.players[0].equip_activated_this_turn);          // ...so it was never activated
        CHECK(FirstEquipWaiverOnBattlefield(t, 0));                   // the free equip is still there
        CHECK(TappedLandsAf(t) == 2);                                  // the reconfigure's {2} only
    }
}

TEST_CASE("Attach folds 1b: the waiver a refused equip was assigned moves on to the next equip of the pass")
{
    GameState s = FreshAf();
    PutAf(s, "Forge Anew", 1);
    const int lizard  = PutAf(s, "Lizard Blades", 2);
    const int duelist = PutAf(s, "Kor Duelist", 3);
    const int hammer  = PutAf(s, "Colossus Hammer", 4);
    const int lox     = PutAf(s, "Loxodon Warhammer", 5);
    PutAf(s, "Mountain", 50);
    PutAf(s, "Mountain", 51);
    TurnSolver::Plan p;
    p.actions = { EquipAf("Lizard Blades", lizard, duelist, 2), EquipAf("Colossus Hammer", hammer, lizard, 8),
                  EquipAf("Loxodon Warhammer", lox, duelist, 3) };
    p.value   = 1;
    for (int world = 0; world < 2; ++world)
    {
        INFO((world == 0 ? "rollout (ApplyPlan)" : "executor (AIEngine)"));
        const GameState t = world == 0 ? RollAf(s, p) : ExecAf(s, p);
        CHECK(FindAf(t, hammer)->equipped_to == 0);
        // The Hammer equip was never activated, so the Warhammer's IS the first equip: {0}, and the two
        // Mountains the reconfigure tapped were enough.
        CHECK(FindAf(t, lox)->equipped_to == duelist);
        CHECK(t.players[0].equip_activated_this_turn);
        CHECK(TappedLandsAf(t) == 2);
    }
}

// ---- DEFECT 1(a): the enumeration ------------------------------------------------------------------

namespace
{
// The seed-2100003 T5 shape: Forge Anew, a loose Lizard Blades, a Kor Duelist and a Swiftblade on the
// battlefield, Colossus Hammer in hand, four lands.
GameState Board1a(bool bf_hammer_first, bool hammer_attached)
{
    GameState s = FreshAf();
    if (bf_hammer_first) { PutAf(s, "Colossus Hammer", 4); }
    PutAf(s, "Forge Anew", 10);
    PutAf(s, "Kor Duelist", 15);
    PutAf(s, "Lizard Blades", 19);
    PutAf(s, "Swiftblade Vindicator", 57);
    for (int k = 0; k < 4; ++k) { PutAf(s, "Plains", 36 + k); }
    if (!bf_hammer_first) { HandAf(s, "Colossus Hammer", 5); }
    if (hammer_attached) { ApplyEquip(s, 0, 4, 57); }
    return s;
}
}   // namespace

TEST_CASE("Attach folds 1a: no plan equips onto a Lizard Blades AFTER its own reconfigure in the pass (hand Hammer)")
{
    const GameState s = Board1a(/*bf_hammer_first=*/false, false);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool onto_lizard_alone = false, reconf_plus_other = false;
    for (const TurnSolver::Plan& p : plans)
    {
        int pos_r = -1, pos_e = -1;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[static_cast<std::size_t>(i)];
            if (IsReconfOf(a, 19))                                         { pos_r = i; }
            if (a.kind == Action::Kind::Equip && a.sac_victim_id == 19)    { pos_e = i; }
        }
        INFO("plan: " << p.actions.size() << " actions");
        // The hand Hammer's equip is always emitted after the battlefield Lizard's, so any plan holding
        // both would run the reconfigure first: an equip with no legal target. None may be offered.
        CHECK_FALSE((pos_r >= 0 && pos_e >= 0));
        if (pos_e >= 0 && pos_r < 0) { onto_lizard_alone = true; }
        if (pos_r >= 0 && CountIf(p, [](const Action& a) { return a.kind == Action::Kind::Equip
                                                              && a.card_name.str() == "Colossus Hammer"; }) > 0)
        { reconf_plus_other = true; }
    }
    CHECK(onto_lizard_alone);       // the Hammer onto the double-striking Lizard is a real line
    CHECK(reconf_plus_other);       // ...and so is reconfigure + Hammer onto another host
}

TEST_CASE("Attach folds 1a: a LOOSE battlefield Hammer equipped onto the Lizard, then the Lizard reconfigured, "
          "is folded (identical to the reconfigure alone); an ATTACHED one is kept")
{
    for (int attached = 0; attached < 2; ++attached)
    {
        INFO((attached ? "Hammer attached to the Swiftblade" : "Hammer loose"));
        const GameState s = Board1a(/*bf_hammer_first=*/true, attached != 0);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        int both_e_first = 0, both_r_first = 0;
        for (const TurnSolver::Plan& p : plans)
        {
            int pos_r = -1, pos_e = -1;
            for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
            {
                const Action& a = p.actions[static_cast<std::size_t>(i)];
                if (IsReconfOf(a, 19))                                      { pos_r = i; }
                if (a.kind == Action::Kind::Equip && a.sac_victim_id == 19) { pos_e = i; }
            }
            if (pos_r >= 0 && pos_e >= 0) { (pos_e < pos_r ? both_e_first : both_r_first) += 1; }
        }
        CHECK(both_r_first == 0);                         // never: no legal target
        if (attached) { CHECK(both_e_first > 0); }        // a different end state (Hammer moved off): kept
        else          { CHECK(both_e_first == 0); }       // identical to the reconfigure alone: folded
    }
}

// ---- DEFECT 2: the same-plan HAND Equipment folds ---------------------------------------------------

namespace
{
GameState Board2(int hammers_in_hand, int thefts_in_hand, bool forge)
{
    GameState s = FreshAf();
    if (forge) { PutAf(s, "Forge Anew", 9); }
    PutAf(s, "Lizard Blades", 1);
    PutAf(s, "Kor Duelist", 3);
    PutAf(s, "Swiftblade Vindicator", 4);
    for (int k = 0; k < 5; ++k) { PutAf(s, "Mountain", 50 + k); }
    for (int k = 0; k < hammers_in_hand; ++k) { HandAf(s, "Colossus Hammer", 61 + 2 * k); }
    for (int k = 0; k < thefts_in_hand; ++k)  { HandAf(s, "Magnetic Theft", 60 + 2 * k); }
    return s;
}
bool TheftOfHandHammer(const Action& a)
{
    return IsTheftAf(a) && a.attach_equip_hand.str() == "Colossus Hammer";
}
bool EquipOfHammer(const Action& a)
{
    return a.kind == Action::Kind::Equip && a.card_name.str() == "Colossus Hammer";
}
int HammerCasts(const TurnSolver::Plan& p)
{
    return CountIf(p, [](const Action& a) { return a.kind == Action::Kind::CastFromHand
                                                && a.card_name.str() == "Colossus Hammer"; });
}
}   // namespace

TEST_CASE("Attach folds 2: one hand Hammer -- no Theft of it beside an Equip of it, a second Theft of it, "
          "or a Theft of it onto a Lizard the plan reconfigures")
{
    const GameState s = Board2(/*hammers=*/1, /*thefts=*/2, /*forge=*/true);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool theft_alone = false, equip_alone = false, theft_onto_lizard = false;
    for (const TurnSolver::Plan& p : plans)
    {
        const int thefts = CountIf(p, TheftOfHandHammer);
        const int equips = CountIf(p, EquipOfHammer);
        const bool onto_lizard = CountIf(p, [](const Action& a) { return TheftOfHandHammer(a) && a.enchant_target == 1; }) > 0;
        const bool reconf      = CountIf(p, [](const Action& a) { return IsReconfOf(a, 1); }) > 0;
        CHECK_FALSE((thefts >= 1 && equips >= 1));   // (b) the Equip overwrites the Theft
        CHECK(thefts <= 1);                          // (b) the later Theft overwrites the earlier
        CHECK_FALSE((onto_lizard && reconf));        // (c) the Hammer falls off the reconfigured Lizard
        if (thefts == 1 && equips == 0) { theft_alone = true; }
        if (thefts == 0 && equips == 1) { equip_alone = true; }
        if (onto_lizard && !reconf)     { theft_onto_lizard = true; }
    }
    CHECK(theft_alone);
    CHECK(equip_alone);
    CHECK(theft_onto_lizard);
}

TEST_CASE("Attach folds 2: what the folds claim -- the dropped plan reaches the board of the one kept, for less")
{
    GameState s = Board2(1, 1, /*forge=*/false);
    const int duelist = 3, swift = 4;
    Action cast_h;
    cast_h.kind      = Action::Kind::CastFromHand;
    cast_h.card_name = std::string("Colossus Hammer");
    cast_h.def       = &DefAf("Colossus Hammer");
    cast_h.cost      = DefAf("Colossus Hammer").card.m_mana_cost;
    cast_h.hand_index = 0;
    Action theft;
    theft.kind              = Action::Kind::CastFromHand;
    theft.card_name         = std::string("Magnetic Theft");
    theft.def               = &DefAf("Magnetic Theft");
    theft.cost              = DefAf("Magnetic Theft").card.m_mana_cost;
    theft.is_noncreature    = true;
    theft.hand_index        = 1;
    theft.attach_equip_id   = 61;
    theft.attach_equip_hand = std::string("Colossus Hammer");
    theft.enchant_target    = duelist;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 6; ++k) { PutAf(s, "Mountain", 70 + k); }   // 11 lands: the {8} equip is payable
    TurnSolver::Plan dropped, kept;
    dropped.actions = { cast_h, theft, EquipAf("Colossus Hammer", 61, swift, 8) };
    kept.actions    = { cast_h, EquipAf("Colossus Hammer", 61, swift, 8) };
    dropped.value = kept.value = 1;
    for (int world = 0; world < 2; ++world)
    {
        INFO((world == 0 ? "rollout (ApplyPlan)" : "executor (AIEngine)"));
        const GameState a = world == 0 ? RollAf(s, dropped) : ExecAf(s, dropped);
        const GameState b = world == 0 ? RollAf(s, kept) : ExecAf(s, kept);
        REQUIRE(FindAf(a, 61) != nullptr);
        REQUIRE(FindAf(b, 61) != nullptr);
        CHECK(FindAf(a, 61)->equipped_to == swift);                     // the later Equip wins
        CHECK(FindAf(b, 61)->equipped_to == swift);
        CHECK(TappedLandsAf(a) == TappedLandsAf(b) + 1);               // ...the Theft only cost its {R}
        CHECK(a.players[0].hand.size() + 1 == b.players[0].hand.size());   // ...and the card
    }
}

TEST_CASE("Attach folds 2: TWO Hammers cast -- a Theft and an Equip can take different copies, so nothing is folded")
{
    const GameState s = Board2(/*hammers=*/2, /*thefts=*/1, /*forge=*/true);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool both = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (HammerCasts(p) == 2 && CountIf(p, TheftOfHandHammer) == 1 && CountIf(p, EquipOfHammer) == 1)
        { both = true; break; }
    }
    CHECK(both);
}

TEST_CASE("Attach folds 2(f): a Theft of the attached Lizard is never offered beside the Lizard's own unattach")
{
    GameState s = FreshAf();
    const int lizard  = PutAf(s, "Lizard Blades", 1);
    const int duelist = PutAf(s, "Kor Duelist", 3);
    PutAf(s, "Speaker of the Heavens", 5);
    for (int k = 0; k < 4; ++k) { PutAf(s, "Mountain", 50 + k); }
    HandAf(s, "Magnetic Theft", 60);
    ApplyEquip(s, 0, lizard, duelist);
    REQUIRE_FALSE(FindAf(s, lizard)->card.IsCreature());
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool theft = false, unattach = false;
    for (const TurnSolver::Plan& p : plans)
    {
        const bool t = CountIf(p, [&](const Action& a) { return IsTheftAf(a) && a.attach_equip_id == lizard; }) > 0;
        const bool u = CountIf(p, [&](const Action& a) { return IsUnattachOf(a, lizard); }) > 0;
        CHECK_FALSE((t && u));
        theft    = theft || t;
        unattach = unattach || u;
    }
    CHECK(theft);
    CHECK(unattach);
    // ...and what the fold claims: Theft-then-unattach ends where the unattach alone does.
    Action th;
    th.kind            = Action::Kind::CastFromHand;
    th.card_name       = std::string("Magnetic Theft");
    th.def             = &DefAf("Magnetic Theft");
    th.cost            = DefAf("Magnetic Theft").card.m_mana_cost;
    th.is_noncreature  = true;
    th.hand_index      = 0;
    th.attach_equip_id = lizard;
    th.enchant_target  = 5;
    Action un;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (IsUnattachOf(a, lizard)) { un = a; } } }
    TurnSolver::Plan dropped, kept;
    dropped.actions = { th, un };
    kept.actions    = { un };
    dropped.value = kept.value = 1;
    const GameState a = RollAf(s, dropped);
    const GameState b = RollAf(s, kept);
    CHECK(FindAf(a, lizard)->equipped_to == 0);
    CHECK(FindAf(b, lizard)->equipped_to == 0);
    CHECK(FindAf(a, lizard)->card.IsCreature());
    CHECK(TappedLandsAf(a) == TappedLandsAf(b) + 1);
}

// ---- lockstep: every plan of the defect board realises the same board in both worlds ---------------

TEST_CASE("Attach folds: executor and rollout realise every plan of the seed-2100003 board identically")
{
    GameState s = Board1a(false, false);
    HandAf(s, "Magnetic Theft", 22);
    PutAf(s, "Mountain", 40);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    REQUIRE(!plans.empty());
    int checked = 0;
    for (const TurnSolver::Plan& line : plans)
    {
        const GameState roll = RollAf(s, line);
        const GameState exec = ExecAf(s, line);
        INFO("plan with " << line.actions.size() << " actions");
        for (const Permanent& p : roll.battlefield)
        {
            const Permanent* q = FindAf(exec, p.card.m_number);
            REQUIRE(q != nullptr);
            CHECK(q->equipped_to == p.equipped_to);
            CHECK(q->tapped == p.tapped);
        }
        CHECK(roll.battlefield.size() == exec.battlefield.size());
        CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
        CHECK(roll.players[0].equip_activated_this_turn == exec.players[0].equip_activated_this_turn);
        ++checked;
    }
    CHECK(checked > 10);
}
