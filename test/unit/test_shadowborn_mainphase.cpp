// Shadowborn Apostle: the USER's main-1 rule and cast order (2026-11-04; docs/design/shadowborn-main-phase-and-cast-
// order-draft.md). Two levers, each measured on its own:
//   * MTG_SB_MAIN1_RULE -- a ROOT pre-combat plan beyond the land drop is admitted only on a JETMIR TURN (an
//     attack-ready creature AND Jetmir on our battlefield, or a plan that can still deploy him before combat);
//     everything else is folded to its main-2 twin (ShadowbornProvider::Main1PlanAdmitted). The sick-turn Demon
//     search is offered after combat with it.
//   * MTG_SB_CAST_ORDER -- land, Dark Ritual, [Culling's hand victim], Culling, Thrumming Stone, Apostle, Reaper,
//     Sire, Overlord, Jetmir, Demonic Tutor, Diabolic Intent, Profane Tutor, Lotus Bloom; once Jetmir has arrived
//     this turn (either main), no Overlord fetch or hard cast.
#include <doctest/doctest.h>

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
const char* const kApM     = "Shadowborn Apostle";
const char* const kStoneM  = "Thrumming Stone";
const char* const kRitM    = "Dark Ritual";
const char* const kCullM   = "Culling the Weak";
const char* const kDtM     = "Demonic Tutor";
const char* const kIntentM = "Diabolic Intent";
const char* const kJetM    = "Jetmir, Nexus of Revels";
const char* const kOverM   = "Abhorrent Overlord";
const char* const kSwM     = "Swamp";
const char* const kLakeM   = "Lake of the Dead";

const CardDefinition& DefM(const std::string& n)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card CardM(const std::string& name, int number)
{
    Card c     = DefM(name).card;
    c.m_number = number;
    return c;
}
Permanent PermM(const std::string& name, int number, bool fresh = false)
{
    Permanent p;
    p.card              = CardM(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    return p;
}
const ShadowbornProvider& ProvM() { static const ShadowbornProvider p; return p; }

struct ArmM
{
    heurarm::Arm saved;
    ArmM() : saved(heurarm::t_arm) {}
    ArmM& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmM() { heurarm::t_arm = saved; }
};
struct SearchedM
{
    bool saved;
    SearchedM() : saved(g_searched_play) { TurnSolver::SetSearchedPlay(true); }
    ~SearchedM() { TurnSolver::SetSearchedPlay(saved); }
};

GameState BaseM(int swamps)
{
    (void)DefM(kSwM);
    GameState s;
    s.m_provider          = &ProvM();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.uses_second_main    = true;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < swamps; ++k) { s.battlefield.push_back(PermM(kSwM, 1 + k)); }
    return s;
}
void LibM(GameState& s, const std::vector<std::string>& names, int first = 500)
{
    std::vector<Card> cards;
    for (const std::string& nm : names) { cards.push_back(CardM(nm, first++)); }
    s.players[0].library.assign(cards.begin(), cards.end());
}
bool CastsName(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}
struct Census { int plans = 0, with_actions = 0, casting = 0; };
Census CensusM(const GameState& s, const std::string& name)
{
    Census c;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        ++c.plans;
        if (!p.actions.empty()) { ++c.with_actions; }
        if (CastsName(p, name)) { ++c.casting; }
    }
    return c;
}
std::vector<std::string> ActTargetsM(const GameState& s, bool pre_combat)
{
    std::vector<std::string> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut)
        { out.push_back(a.tutor_target.str()); }
    }
    return out;
}
int CastCountM(const GameState& s, const std::string& name, bool pre_combat)
{
    int n = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { ++n; } }
    return n;
}
}   // namespace

// ---- the main-1 rule ----------------------------------------------------------------------------------------------
TEST_CASE("SB main-1 rule: no attack-ready creature -> main 1 is the land drop alone")
{
    SearchedM sp;
    GameState s = BaseM(1);
    s.players[0].hand.push_back(CardM(kSwM, 60));
    s.players[0].hand.push_back(CardM(kApM, 61));
    s.players[0].hand.push_back(CardM(kRitM, 62));
    LibM(s, { kJetM, kSwM, kSwM, kSwM });
    {
        ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 0);
        const Census off = CensusM(s, kApM);
        CHECK(off.casting > 0);
    }
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    const Census on = CensusM(s, kApM);
    CHECK(on.plans > 0);
    CHECK(on.with_actions == 0);
    // ...and the land is still played in main 1 (every plan that remains plays the Swamp: the no-land plan is P1's).
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { CHECK(p.land_to_play == kSwM); }
}

TEST_CASE("SB main-1 rule: the d0 runner and human play keep the full menu (searched play only)")
{
    GameState s = BaseM(1);
    s.players[0].hand.push_back(CardM(kSwM, 60));
    s.players[0].hand.push_back(CardM(kApM, 61));
    LibM(s, { kJetM, kSwM, kSwM });
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    const bool saved = g_searched_play;
    TurnSolver::SetSearchedPlay(false);
    const Census c = CensusM(s, kApM);
    TurnSolver::SetSearchedPlay(saved);
    CHECK(c.casting > 0);
}

TEST_CASE("SB main-1 rule: attackers + Jetmir in the library, no granter -> a lone Apostle folds, a tutor stays")
{
    SearchedM sp;
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    GameState s = BaseM(2);
    s.battlefield.push_back(PermM(kApM, 41));          // attack-ready
    s.players[0].hand.push_back(CardM(kSwM, 60));
    s.players[0].hand.push_back(CardM(kApM, 61));
    s.players[0].hand.push_back(CardM(kDtM, 62));
    LibM(s, { kJetM, kStoneM, kSwM, kSwM, kSwM });
    int lone_apostle = 0, tutor = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (CastsName(p, kDtM)) { ++tutor; }
        else if (CastsName(p, kApM)) { ++lone_apostle; }
    }
    CHECK(lone_apostle == 0);   // 1 + 1 Apostles cannot reach the six, and nothing can reveal: main 2 does it
    CHECK(tutor > 0);           // the tutor's continuation could still find the Stone and go off before combat
}

TEST_CASE("SB main-1 rule: Jetmir on the battlefield with an attacker -> nothing folds (bodies pump him)")
{
    SearchedM sp;
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    GameState s = BaseM(2);
    s.battlefield.push_back(PermM(kJetM, 40));
    s.battlefield.push_back(PermM(kApM, 41));
    s.players[0].hand.push_back(CardM(kSwM, 60));
    s.players[0].hand.push_back(CardM(kApM, 61));
    LibM(s, { kOverM, kSwM, kSwM });
    CHECK(CensusM(s, kApM).casting > 0);
    // The same board with Jetmir summoning sick and no other old creature: no attacker, so it folds.
    GameState t = BaseM(2);
    t.battlefield.push_back(PermM(kJetM, 40, /*fresh=*/true));
    t.players[0].hand.push_back(CardM(kSwM, 60));
    t.players[0].hand.push_back(CardM(kApM, 61));
    LibM(t, { kOverM, kSwM, kSwM });
    CHECK(CensusM(t, kApM).casting == 0);
}

TEST_CASE("SB main-1 rule: a granter on the battlefield -> a cast stays in main 1 (a reveal chain may reach Jetmir)")
{
    SearchedM sp;
    // The ritual-payoff prune (default ON) would drop this lone Ritual on its own; the rule is tested alone.
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1).Set(heurarm::SB_RITUAL_PAYOFF, 0);
    GameState s = BaseM(2);
    s.battlefield.push_back(PermM(kStoneM, 40));
    s.battlefield.push_back(PermM(kApM, 41));
    s.players[0].hand.push_back(CardM(kSwM, 60));
    s.players[0].hand.push_back(CardM(kRitM, 61));
    LibM(s, { kApM, kApM, kSwM, kJetM, kSwM, kSwM, kSwM, kSwM });
    CHECK(CensusM(s, kRitM).casting > 0);
    // ...but not once Jetmir has left the library (nothing in main 1 can change combat any more).
    GameState t = s;
    LibM(t, { kApM, kApM, kSwM, kSwM, kSwM, kSwM, kSwM, kSwM });
    CHECK(CensusM(t, kRitM).casting == 0);
}

TEST_CASE("SB main-1 rule: a Lake of the Dead drop keeps its main-1 casts (the eaten Swamp's float empties at combat)")
{
    SearchedM sp;
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    GameState s = BaseM(2);
    s.players[0].hand.push_back(CardM(kLakeM, 60));
    s.players[0].hand.push_back(CardM(kApM, 61));
    LibM(s, { kSwM, kSwM, kSwM });
    int lake_cast = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { if (p.land_to_play == kLakeM && CastsName(p, kApM)) { ++lake_cast; } }
    CHECK(lake_cast > 0);
}

TEST_CASE("SB main-1 rule: the sick-turn Demon search is offered after combat too")
{
    GameState s = BaseM(1);
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    LibM(s, { kOverM, kJetM, kSwM, kSwM });
    {
        ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 0);
        CHECK(ActTargetsM(s, /*pre_combat=*/false).empty());
        CHECK(!ActTargetsM(s, /*pre_combat=*/true).empty());
    }
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1);
    const std::vector<std::string> m2 = ActTargetsM(s, /*pre_combat=*/false);
    CHECK(std::find(m2.begin(), m2.end(), kOverM) != m2.end());
    CHECK(std::find(m2.begin(), m2.end(), kJetM) != m2.end());   // USER: "in main 2 we should still allow Overlord -> Jetmir"
}

// ---- the activation order -----------------------------------------------------------------------------------------
TEST_CASE("SB order: Jetmir fetched this main -> no Overlord fetch and no Overlord hard cast after it")
{
    GameState s = BaseM(8);
    s.battlefield.push_back(PermM(kJetM, 30, /*fresh=*/true));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    s.players[0].hand.push_back(CardM(kOverM, 60));
    LibM(s, { kOverM, kSwM, kSwM });
    {
        // The baseline is the arm with neither lever: the main-1 rule's placement also refuses a post-Jetmir Overlord.
        ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 0).Set(heurarm::SB_MAIN1_RULE, 0);
        const std::vector<std::string> t = ActTargetsM(s, true);
        CHECK(std::find(t.begin(), t.end(), kOverM) != t.end());
        CHECK(CastCountM(s, kOverM, true) > 0);
    }
    ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1);
    CHECK(ActTargetsM(s, true).empty());
    CHECK(CastCountM(s, kOverM, true) == 0);
}

// USER 2026-11-05: "Overlord in Main-2 i worse in the case where we drop Jetmir in Main-1." -> once Jetmir has arrived
// this turn, main 2 offers neither the Overlord fetch nor its hard cast, whichever main he came in; a main-2
// Overlord -> Jetmir (no Jetmir yet this turn) stays offered.
TEST_CASE("SB order: a MAIN-2 Overlord after a main-1 Jetmir is pruned (fetch and hard cast)")
{
    ArmM arm;
    arm.Set(heurarm::SB_CAST_ORDER, 1).Set(heurarm::SB_MAIN1_RULE, 1);
    GameState s = BaseM(8);
    s.phase = Phase::PostCombatMain;
    s.battlefield.push_back(PermM(kJetM, 30, /*fresh=*/true));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    s.battlefield.push_back(PermM(kApM, 20));   // an old Apostle: it attacked, Jetmir came in main 1
    s.players[0].hand.push_back(CardM(kOverM, 60));
    LibM(s, { kOverM, kSwM, kSwM });
    {
        ArmM off; off.Set(heurarm::SB_CAST_ORDER, 0).Set(heurarm::SB_MAIN1_RULE, 0);   // both levers off: both stay offered
        const std::vector<std::string> t = ActTargetsM(s, /*pre_combat=*/false);
        CHECK(std::find(t.begin(), t.end(), kOverM) != t.end());
        CHECK(CastCountM(s, kOverM, false) > 0);
    }
    const std::vector<std::string> t = ActTargetsM(s, /*pre_combat=*/false);
    CHECK(std::find(t.begin(), t.end(), kOverM) == t.end());
    CHECK(CastCountM(s, kOverM, false) == 0);
    // Jetmir from an EARLIER turn (not fresh): the Overlord is still offered in main 2.
    GameState u = s;
    for (Permanent& p : u.battlefield) { if (p.card.m_name.str() == kJetM) { p.entered_this_turn = false; } }
    CHECK(CastCountM(u, kOverM, false) > 0);
    // No Jetmir yet this turn: main-2 Overlord -> Jetmir stays offered (sick turn, the main-1 rule's main 2).
    GameState w = BaseM(8);
    w.phase = Phase::PostCombatMain;
    for (int k = 0; k < 6; ++k) { w.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    LibM(w, { kOverM, kJetM, kSwM, kSwM });
    const std::vector<std::string> wt = ActTargetsM(w, /*pre_combat=*/false);
    CHECK(std::find(wt.begin(), wt.end(), kOverM) != wt.end());
}

// ---- the cast order -----------------------------------------------------------------------------------------------
TEST_CASE("SB order: the ranks follow the USER's table")
{
    ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1);
    GameState s = BaseM(1);
    const ShadowbornProvider& p = ProvM();
    const std::vector<std::string> want = { kRitM, kCullM, kStoneM, kApM, "Reaper from the Abyss", "Sire of Insanity",
                                            kOverM, kJetM, kDtM, kIntentM, "Profane Tutor", "Lotus Bloom" };
    for (std::size_t i = 0; i < want.size(); ++i)
    { CHECK_MESSAGE(p.CastOrderRank(s, DefM(want[i])) == static_cast<int>(i) + 1, want[i]); }
    CHECK(p.CastOrderTotalByName());
    ArmM off; off.Set(heurarm::SB_CAST_ORDER, 0);
    CHECK(p.CastOrderRank(s, DefM(kStoneM)) == 1);   // the shipped order: the granter first
    CHECK(!p.CastOrderTotalByName());
}

TEST_CASE("SB order: Culling's hand victim is cast right before Culling, ahead of the Stone")
{
    ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1);
    GameState s = BaseM(1);
    auto cast = [](const char* nm, int hi)
    {
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = InternedName(nm);
        a.def        = &DefM(nm);
        a.hand_index = hi;
        return a;
    };
    std::vector<Action> acts = { cast(kApM, 0), cast(kStoneM, 1), cast(kCullM, 2), cast(kApM, 3), cast(kRitM, 4) };
    acts[2].trick_hand_target = kApM;
    std::vector<int> order = { 0, 1, 2, 3, 4 };
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ProvM().ArrangeCastOrder(s, acts, order);
    std::vector<std::string> got;
    for (int i : order) { got.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    const std::vector<std::string> want = { kRitM, kApM, kCullM, kStoneM, kApM };
    CHECK(got == want);
}

// ---- the union plan: Ritual x2 + Stone + Apostle in ONE main (held-out 50047 gi46 T3) -----------------------------
// Three untapped Swamps: Ritual -> BBB, Ritual -> BBBBB, Stone {5}, an Apostle {B} off the third Swamp into the
// ripple. The shipped game split it across the mains (Ritual, Ritual, Stone in main 1; the Apostle in main 2); the
// main-1 rule folds a no-attacker turn to main 2, so the whole line has to be ONE main-2 plan.
TEST_CASE("SB union plan: Ritual, Ritual, Stone, Apostle is one post-combat plan")
{
    SearchedM sp;
    auto union_plans = [](int order)
    {
        ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, order);
        GameState s = BaseM(3);
        s.phase = Phase::PostCombatMain;
        s.players[0].lands_played_this_turn = 1;
        s.players[0].hand.push_back(CardM(kRitM, 60));
        s.players[0].hand.push_back(CardM(kRitM, 61));
        s.players[0].hand.push_back(CardM(kStoneM, 62));
        s.players[0].hand.push_back(CardM(kApM, 63));
        LibM(s, { kApM, kSwM, kSwM, kSwM, kApM, kApM, kSwM, kSwM, kSwM, kSwM });
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        {
            int rit = 0; bool stone = false, ap = false;
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::CastFromHand) { continue; }
                if (a.card_name.str() == kRitM) { ++rit; }
                if (a.card_name.str() == kStoneM) { stone = true; }
                if (a.card_name.str() == kApM) { ap = true; }
            }
            if (rit == 2 && stone && ap) { ++n; }
        }
        return n;
    };
    const int shipped = union_plans(0), ordered = union_plans(1);
    MESSAGE("union plans: shipped order ", shipped, ", USER order ", ordered);
    CHECK(ordered > 0);
}

// ---- the Overlord's placement (MTG_SB_MAIN1_RULE; USER 2026-11-05) ------------------------------------------------
// "My actual suggestion for Overlord is to move it wherever it needs to go to be before Jetmir. It should be in main-1
// if Jetmir is there and main-2 otherwise."
TEST_CASE("SB placement: Overlord then Jetmir, both in main 1, is reachable on a Jetmir turn")
{
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1).Set(heurarm::SB_CAST_ORDER, 1);
    GameState s = BaseM(4);
    s.battlefield.push_back(PermM(kApM, 9));                                                  // attack-ready
    for (int k = 0; k < 12; ++k) { s.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }   // two sixes
    LibM(s, { kOverM, kJetM, kSwM, kSwM });
    const std::vector<std::string> m1 = ActTargetsM(s, /*pre_combat=*/true);
    const auto ov = std::find(m1.begin(), m1.end(), kOverM), jt = std::find(m1.begin(), m1.end(), kJetM);
    CHECK(ov != m1.end());
    CHECK(jt != m1.end());
    CHECK(ov < jt);   // the Overlord ahead of Jetmir
    // ...and after the Overlord fetch (it ate six sick Apostles), Jetmir still follows in main 1.
    GameState t = BaseM(4);
    t.battlefield.push_back(PermM(kApM, 9));
    for (int k = 0; k < 6; ++k) { t.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    t.battlefield.push_back(PermM(kOverM, 30, /*fresh=*/true));
    LibM(t, { kJetM, kSwM, kSwM });
    const std::vector<std::string> m1b = ActTargetsM(t, /*pre_combat=*/true);
    CHECK(std::find(m1b.begin(), m1b.end(), kJetM) != m1b.end());
    // Without the lever the attack-turn main 1 offers Jetmir alone (the INT-9 table).
    ArmM off; off.Set(heurarm::SB_MAIN1_RULE, 0);
    const std::vector<std::string> m1o = ActTargetsM(s, /*pre_combat=*/true);
    CHECK(std::find(m1o.begin(), m1o.end(), kOverM) == m1o.end());
}

TEST_CASE("SB placement: a main-1 Overlord with no Jetmir to follow is pruned (it goes to main 2)")
{
    ArmM arm; arm.Set(heurarm::SB_MAIN1_RULE, 1).Set(heurarm::SB_CAST_ORDER, 1);
    // Sick turn: six fresh Apostles, nothing attack-ready -> no main-1 Jetmir -> the Overlord fetch is main 2 only.
    GameState s = BaseM(8);
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(PermM(kApM, 10 + k, /*fresh=*/true)); }
    s.players[0].hand.push_back(CardM(kOverM, 60));
    LibM(s, { kOverM, kJetM, kSwM, kSwM });
    const std::vector<std::string> m1 = ActTargetsM(s, /*pre_combat=*/true);
    CHECK(std::find(m1.begin(), m1.end(), kOverM) == m1.end());
    CHECK(CastCountM(s, kOverM, /*pre_combat=*/true) == 0);   // the hard cast too
    const std::vector<std::string> m2 = ActTargetsM(s, /*pre_combat=*/false);
    CHECK(std::find(m2.begin(), m2.end(), kOverM) != m2.end());
    CHECK(CastCountM(s, kOverM, /*pre_combat=*/false) > 0);
    // Jetmir already on the battlefield with an attacker: "if Jetmir is there" -> the main-1 hard cast is offered.
    GameState t = BaseM(8);
    t.battlefield.push_back(PermM(kJetM, 30));
    t.battlefield.push_back(PermM(kApM, 9));
    t.players[0].hand.push_back(CardM(kOverM, 60));
    LibM(t, { kSwM, kSwM });
    CHECK(CastCountM(t, kOverM, /*pre_combat=*/true) > 0);
}

// ---- the ritual-without-payoff prune (MTG_SB_RITUAL_PAYOFF; USER 2026-11-05) --------------------------------------
TEST_CASE("SB ritual payoff: a Dark Ritual that nothing spends is pruned; Ritual + a paid cast stays")
{
    SearchedM sp;
    auto census = [](int on, bool with_apostle)
    {
        ArmM arm; arm.Set(heurarm::SB_RITUAL_PAYOFF, on);
        GameState s = BaseM(1);
        s.turn_number = 1;
        s.phase = Phase::PostCombatMain;
        s.players[0].lands_played_this_turn = 1;
        s.players[0].hand.push_back(CardM(kRitM, 60));
        if (with_apostle) { s.players[0].hand.push_back(CardM(kApM, 61)); }
        LibM(s, { kSwM, kSwM, kSwM, kSwM });
        int rit_alone = 0, rit_and_ap = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        {
            const bool r = CastsName(p, kRitM), a = CastsName(p, kApM);
            if (r && !a) { ++rit_alone; }
            if (r && a) { ++rit_and_ap; }
        }
        return std::make_pair(rit_alone, rit_and_ap);
    };
    CHECK(census(0, false).first > 0);    // shipped: the lone Ritual is a plan
    CHECK(census(1, false).first == 0);   // pruned
    // With an Apostle in hand the Ritual-alone line is still pruned, and Ritual + Apostle is NOT (the Apostle spends).
    const auto on = census(1, true);
    CHECK(on.first == 0);
    CHECK(on.second > 0);
}

// The granter phantom (held-out 50047 gi46 T1 main 2): {Thrumming Stone, Dark Ritual} on one Swamp passed the flat sum
// on the Ritual's ripple-chain credit, which needs the Stone out first; the executor cast the Ritual for nothing.
TEST_CASE("SB ritual payoff: a granter that only the ritual's own ripple credit could pay is a phantom, pruned")
{
    SearchedM sp;
    auto stone_plans = [](int on)
    {
        ArmM arm; arm.Set(heurarm::SB_RITUAL_PAYOFF, on);
        GameState s = BaseM(1);
        s.turn_number = 1;
        s.phase = Phase::PostCombatMain;
        s.players[0].lands_played_this_turn = 1;
        s.players[0].hand.push_back(CardM(kStoneM, 60));
        s.players[0].hand.push_back(CardM(kRitM, 61));
        LibM(s, { kRitM, kRitM, kSwM, kSwM, kSwM, kSwM, kSwM, kSwM });
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        { if (CastsName(p, kStoneM)) { ++n; } }
        return n;
    };
    MESSAGE("Stone plans on one Swamp: off ", stone_plans(0), ", on ", stone_plans(1));
    CHECK(stone_plans(1) == 0);
    // Three Swamps: Ritual (BBB) + two Swamps = 5 pays the Stone -> the line stays.
    ArmM arm; arm.Set(heurarm::SB_RITUAL_PAYOFF, 1);
    GameState t = BaseM(3);
    t.phase = Phase::PostCombatMain;
    t.players[0].lands_played_this_turn = 1;
    t.players[0].hand.push_back(CardM(kStoneM, 60));
    t.players[0].hand.push_back(CardM(kRitM, 61));
    LibM(t, { kRitM, kSwM, kSwM, kSwM, kSwM, kSwM });
    int n = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, false)) { if (CastsName(p, kStoneM) && CastsName(p, kRitM)) { ++n; } }
    CHECK(n > 0);
}

// The unreachable activation (held-out 50047 gi46 T2 main 2): {Dark Ritual, sacrifice-six} with one Apostle out and no
// granter -- the activation cannot resolve, so the Ritual has no sink.
TEST_CASE("SB ritual payoff: a sacrifice-six activation the six cannot reach is no sink for a ritual")
{
    SearchedM sp;
    auto ritual_plans = [](int on, int apostles)
    {
        ArmM arm; arm.Set(heurarm::SB_RITUAL_PAYOFF, on);
        GameState s = BaseM(2);
        s.phase = Phase::PostCombatMain;
        s.players[0].lands_played_this_turn = 1;
        for (int k = 0; k < apostles; ++k) { s.battlefield.push_back(PermM(kApM, 20 + k)); }
        s.players[0].hand.push_back(CardM(kRitM, 61));
        LibM(s, { kJetM, kOverM, kSwM, kSwM, kSwM, kSwM });
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        { if (CastsName(p, kRitM)) { ++n; } }
        return n;
    };
    MESSAGE("Ritual plans, one Apostle: off ", ritual_plans(0, 1), ", on ", ritual_plans(1, 1));
    CHECK(ritual_plans(1, 1) == 0);
}

// ---- Ritual after the Stone if mana allows (MTG_SB_RITUAL_AFTER_STONE; USER 2026-11-11) ---------------------------
// "my recommendation would be to cast it after if mana allows and before if not." / "there is no extra search for
// clairvoyance to abuse. It has exactly one choice." Judged by the exact sequential walk, which credits no mana from
// ripple-revealed cards.
namespace
{
Action SbCastA(GameState& s, const char* nm, int number)
{
    s.players[0].hand.push_back(CardM(nm, number));
    const CardDefinition& d = DefM(nm);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = InternedName(nm);
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.is_noncreature = !d.card.IsCreature();
    a.hand_index     = static_cast<int>(s.players[0].hand.size()) - 1;
    if (d.params.ritual_floating_mana > 0) { a.ritual_float = static_cast<decltype(a.ritual_float)>(d.params.ritual_floating_mana); }
    return a;
}
std::vector<std::string> SbArranged(GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> order(acts.size());
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) { order[static_cast<std::size_t>(i)] = i; }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, order);
    std::vector<std::string> got;
    for (int i : order) { got.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return got;
}
}

TEST_CASE("SB order: Ritual goes after the Stone when that order pays, else before it")
{
    ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1).Set(heurarm::SB_RITUAL_AFTER_STONE, 1);
    {
        // Six Swamps: Stone off five, Ritual off the sixth, the Apostle off its float -> after.
        GameState s = BaseM(6);
        const std::vector<Action> acts = { SbCastA(s, kApM, 60), SbCastA(s, kStoneM, 61), SbCastA(s, kRitM, 62) };
        CHECK(SbArranged(s, acts) == std::vector<std::string>{ kStoneM, kRitM, kApM });
    }
    {
        // Four Swamps: the Stone needs the Ritual's float -> the Ritual keeps its rank-1 slot.
        GameState s = BaseM(4);
        const std::vector<Action> acts = { SbCastA(s, kApM, 60), SbCastA(s, kStoneM, 61), SbCastA(s, kRitM, 62) };
        CHECK(SbArranged(s, acts) == std::vector<std::string>{ kRitM, kStoneM, kApM });
    }
    {
        // Two Rituals on six Swamps: both move after the Stone; on four only the second one does.
        GameState s = BaseM(6);
        const std::vector<Action> acts = { SbCastA(s, kRitM, 60), SbCastA(s, kStoneM, 61), SbCastA(s, kRitM, 62),
                                           SbCastA(s, kApM, 63) };
        CHECK(SbArranged(s, acts) == std::vector<std::string>{ kStoneM, kRitM, kRitM, kApM });
        GameState t = BaseM(4);
        const std::vector<Action> acts2 = { SbCastA(t, kRitM, 60), SbCastA(t, kStoneM, 61), SbCastA(t, kRitM, 62),
                                            SbCastA(t, kApM, 63) };
        CHECK(SbArranged(t, acts2) == std::vector<std::string>{ kRitM, kStoneM, kRitM, kApM });
    }
    {
        // The arm off: always before.
        ArmM off; off.Set(heurarm::SB_RITUAL_AFTER_STONE, 0);
        GameState s = BaseM(6);
        const std::vector<Action> acts = { SbCastA(s, kApM, 60), SbCastA(s, kStoneM, 61), SbCastA(s, kRitM, 62) };
        CHECK(SbArranged(s, acts) == std::vector<std::string>{ kRitM, kStoneM, kApM });
    }
}

TEST_CASE("SB order: ripple-revealed mana (the chain credit) never moves the Ritual after the Stone")
{
    ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1).Set(heurarm::SB_RITUAL_AFTER_STONE, 1);
    // Six Swamps, Ritual + Stone + four Apostles. After the Stone the Ritual's own BBB pays three Apostles; the fourth
    // only with the free Rituals its ripple would reveal (the chain credit, hidden information) -> not "mana allows".
    GameState s = BaseM(6);
    std::vector<Action> acts = { SbCastA(s, kRitM, 60), SbCastA(s, kStoneM, 61), SbCastA(s, kApM, 62),
                                 SbCastA(s, kApM, 63), SbCastA(s, kApM, 64), SbCastA(s, kApM, 65) };
    acts[0].ritual_float       = static_cast<decltype(acts[0].ritual_float)>(acts[0].ritual_float + 4);
    acts[0].ripple_chain_float = 4;
    const std::vector<std::string> got = SbArranged(s, acts);
    REQUIRE(!got.empty());
    CHECK(got[0] == kRitM);
    CHECK(got[1] == kStoneM);
}

TEST_CASE("SB order: Ritual-after-Stone is ONE arrangement per line -- the plan count does not grow")
{
    SearchedM sp;
    auto census = [](int on, int swamps, int& stone_first)
    {
        ArmM arm; arm.Set(heurarm::SB_CAST_ORDER, 1).Set(heurarm::SB_RITUAL_AFTER_STONE, on);
        GameState s = BaseM(swamps);
        s.phase = Phase::PostCombatMain;
        s.players[0].lands_played_this_turn = 1;
        s.players[0].hand.push_back(CardM(kRitM, 60));
        s.players[0].hand.push_back(CardM(kStoneM, 61));
        s.players[0].hand.push_back(CardM(kApM, 62));
        s.players[0].hand.push_back(CardM(kApM, 63));
        LibM(s, { kApM, kSwM, kSwM, kSwM, kApM, kApM, kSwM, kSwM, kSwM, kSwM });
        int n = 0;
        stone_first = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        {
            ++n;
            std::vector<int> order(p.actions.size());
            for (int i = 0; i < static_cast<int>(order.size()); ++i) { order[static_cast<std::size_t>(i)] = i; }
            std::stable_sort(order.begin(), order.end(),
                             [&](int x, int y) { return CastOrderLess(s, p.actions[x], p.actions[y]); });
            ApplyDeckCastArrangement(s, p.actions, order);
            int rit = -1, stone = -1;
            for (int k = 0; k < static_cast<int>(order.size()); ++k)
            {
                const std::string nm = p.actions[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])].card_name.str();
                if (nm == kRitM && rit < 0) { rit = k; }
                if (nm == kStoneM) { stone = k; }
            }
            if (rit >= 0 && stone >= 0 && stone < rit) { ++stone_first; }
        }
        return n;
    };
    for (int swamps : { 4, 6, 7 })
    {
        int sf_off = 0, sf_on = 0;
        const int off = census(0, swamps, sf_off), on = census(1, swamps, sf_on);
        MESSAGE(swamps, " Swamps: plans off ", off, " on ", on, "; Stone-before-Ritual lines off ", sf_off, " on ", sf_on);
        CHECK(on == off);
        CHECK(sf_off == 0);
        if (swamps >= 6) { CHECK(sf_on > 0); } else { CHECK(sf_on == 0); }
    }
}
