// Generator Servant (NivMizzetCombo onboarding, 2026-10-17):
//   "{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains
//    haste until end of turn."
//
// The Lotus-Bloom sac-for-mana machinery (sac_for_mana_amount 2, pinned "C") plus two new params:
//   * sac_for_mana_requires_tap -- {T} in a CREATURE's cost: summoning sickness binds it (CR 302.6), at
//     the enumeration and again at ApplySacForMana;
//   * sac_for_mana_grants_haste -- the floated {C} are TAGGED (GameState::floating_haste_c); a creature
//     SPELL's payment spends one tagged {C} on its own bill first (TakeHasteFloatForCreatureSpell, both
//     cast sites) and the creature enters with temp_haste; every other spend takes untagged {C} first.
// These pin, in BOTH worlds (rollout ApplyPlan and the executor through the committed-line seam):
//   1. a Servant that arrived this turn offers no crack, a hasted one does, and a direct apply on a sick
//      one is a no-op;
//   2. the crack floats {C}{C}, tags both, and the Servant DIES (graveyard);
//   3. a creature spell paid with it enters hasty -- it can attack and tap the turn it arrives (a second
//      Servant cast off the first is itself crackable at once);
//   4. a NONCREATURE spell spends the mana without granting anything, and a {C} a later tap
//      over-produces never inherits the tag (no phantom haste);
//   5. a creature whose bill has no generic / {C} pip takes no {C} and gains no haste.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

constexpr const char* kGs = "Generator Servant";

void EnsureCardsGs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefGs(const std::string& name)
{
    EnsureCardsGs();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutGs(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefGs(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandGs(GameState& s, const std::string& name, int number)
{
    Card c = DefGs(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshGs()
{
    EnsureCardsGs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 9191;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    for (int k = 0; k < 8; ++k)
    {
        // Expensive, uncastable filler: nothing drawn mid-test can be played on.
        Card c = DefGs("Lightning Bolt").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* FindGs(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool InGyGs(const GameState& s, int number)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

void ResolveStackGs(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorGs(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackGs);
    return exec;
}

bool HasSacOf(const TurnSolver::Plan& p, int src)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::SacForMana && a.sac_source_id == src) { return true; } }
    return false;
}
bool CastsGs(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name == name) { return true; } }
    return false;
}
int CastCount(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
    return n;
}

// The first enumerated plan that cracks `src`, casts exactly `casts` (by name, any order) and plays no
// land. The returned pointer points INTO `plans`.
const TurnSolver::Plan* FindPlanGs(const std::vector<TurnSolver::Plan>& plans, int src,
                                   const std::vector<std::string>& casts)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty() || !HasSacOf(p, src)) { continue; }
        if (CastCount(p) != static_cast<int>(casts.size())) { continue; }
        bool all = true;
        for (const std::string& n : casts) { if (!CastsGs(p, n)) { all = false; break; } }
        if (all) { return &p; }
    }
    return nullptr;
}
const TurnSolver::Plan* FindPlanGs(std::vector<TurnSolver::Plan>&&, int, const std::vector<std::string>&) = delete;

bool AnySacOf(const std::vector<TurnSolver::Plan>& plans, int src)
{
    for (const TurnSolver::Plan& p : plans) { if (HasSacOf(p, src)) { return true; } }
    return false;
}

std::string WorldGs(const GameState* g, const GameState* roll)
{ return std::string(g == roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"); }

}   // namespace

TEST_CASE("Generator Servant: the card data -- a 2/1 Elemental for {1}{R}, {C}{C} pinned, tap + haste rider")
{
    const CardDefinition& d = DefGs(kGs);
    CHECK(d.card.IsCreature());
    CHECK(d.card.m_mana_cost.ManaValue() == 2);
    CHECK(d.card.m_mana_cost.red == 1);
    CHECK(d.card.m_mana_cost.generic == 1);
    CHECK(d.params.sac_for_mana_amount == 2);
    CHECK(d.params.sac_for_mana_color == "C");
    CHECK(d.params.sac_for_mana_requires_tap);
    CHECK(d.params.sac_for_mana_grants_haste);
    CHECK_FALSE(IsPaySacSource(d));   // a 2-mana lump: an enumerated SacForMana action, not a payer source
}

TEST_CASE("Generator Servant: summoning sickness -- no crack the turn it arrives, unless it has haste")
{
    // Sick: no SacForMana anywhere in the menu.
    {
        GameState s = FreshGs();
        PutGs(s, kGs, 10, /*sick=*/true);
        PutGs(s, "Mountain", 1);
        HandGs(s, "Goblin Guide", 30);
        CHECK_FALSE(AnySacOf(TurnSolver::EnumerateMainPlans(s, true), 10));
        // A direct apply (a stale plan) is refused the same way: nothing floats, it stays in play.
        ApplySacForMana(s, 0, 10, "C", 2);
        CHECK(s.floating_mana.colorless == 0);
        CHECK(s.floating_haste_c == 0);
        CHECK(FindGs(s, 10) != nullptr);
    }
    // Not sick: offered.
    {
        GameState s = FreshGs();
        PutGs(s, kGs, 10);
        PutGs(s, "Mountain", 1);
        HandGs(s, "Goblin Guide", 30);
        CHECK(AnySacOf(TurnSolver::EnumerateMainPlans(s, true), 10));
    }
    // Sick but hasted (e.g. another Servant's mana was spent on it): offered.
    {
        GameState s = FreshGs();
        const int bi = PutGs(s, kGs, 10, /*sick=*/true);
        s.battlefield[bi].temp_haste = true;
        PutGs(s, "Mountain", 1);
        HandGs(s, "Goblin Guide", 30);
        CHECK(AnySacOf(TurnSolver::EnumerateMainPlans(s, true), 10));
    }
}

TEST_CASE("Generator Servant: the crack floats {C}{C}, tags both, and the Servant dies")
{
    GameState s = FreshGs();
    PutGs(s, kGs, 10);
    ApplySacForMana(s, 0, 10, "C", 2);
    CHECK(s.floating_mana.colorless == 2);
    CHECK(s.floating_mana.Total() == 2);
    CHECK(s.floating_haste_c == 2);
    CHECK(HasteFloatUnits(s) == 2);
    CHECK(FindGs(s, 10) == nullptr);
    CHECK(InGyGs(s, 10));
}

TEST_CASE("Generator Servant: a creature spell paid with it gains haste -- attacks and taps at once, both worlds")
{
    // Servant A cracks for {C}{C}; Servant B ({1}{R}) is cast with one tagged {C} + the Mountain's {R}.
    GameState s = FreshGs();
    PutGs(s, kGs, 10);
    PutGs(s, "Mountain", 1);
    HandGs(s, kGs, 30);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = FindPlanGs(plans, 10, { kGs });
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    GameState exec = RunExecutorGs(s, *p);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << WorldGs(g, &roll));
        CHECK(InGyGs(*g, 10));                      // A died paying {T}+sacrifice
        const Permanent* b = FindGs(*g, 30);
        REQUIRE(b != nullptr);
        CHECK(b->entered_this_turn);
        CHECK(b->temp_haste);                       // "it gains haste until end of turn"
        CHECK(CanAttackFull(*b, g->battlefield, 0));
        CHECK(CanTapNow(*b, g->battlefield));
        CHECK(FindGs(*g, 1)->tapped);               // the {R}
        CHECK(g->floating_mana.colorless == 1);     // the second {C} still floats...
        CHECK(HasteFloatUnits(*g) == 1);            // ...still tagged
    }
    // The hasted B is crackable this very turn.
    CHECK(AnySacOf(TurnSolver::EnumerateMainPlans(roll, true), 30));
}

TEST_CASE("Generator Servant: a noncreature spell spends it without haste; the rest stays tagged")
{
    // Sol Ring ({1}) takes one {C} -- tagged, as none is untagged -- and the other {C} still hastes a
    // creature cast after it.
    GameState s = FreshGs();
    PutGs(s, kGs, 10);
    PutGs(s, "Mountain", 1);
    HandGs(s, "Sol Ring", 31);
    HandGs(s, kGs, 30);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = FindPlanGs(plans, 10, { "Sol Ring", kGs });
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    GameState exec = RunExecutorGs(s, *p);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << WorldGs(g, &roll));
        REQUIRE(FindGs(*g, 31) != nullptr);          // Sol Ring resolved
        CHECK_FALSE(FindGs(*g, 31)->temp_haste);     // (nothing to grant a noncreature)
        const Permanent* b = FindGs(*g, 30);
        REQUIRE(b != nullptr);
        INFO("cast order: Sol Ring " << (p->actions.size()) << " actions");
        // Whichever order the plan applies: Sol Ring and B each take one {C}; B's is tagged either way
        // (a creature spell takes a tagged unit first; Sol Ring spends the untagged first, none left).
        CHECK(b->temp_haste);
    }

    // A noncreature spell that eats BOTH {C} leaves nothing tagged: Izzet Signet's {2}.
    GameState t = FreshGs();
    PutGs(t, kGs, 10);
    PutGs(t, "Island", 1);
    HandGs(t, "Izzet Signet", 32);
    const std::vector<TurnSolver::Plan> tplans = TurnSolver::EnumerateMainPlans(t, true);
    const TurnSolver::Plan* tp = FindPlanGs(tplans, 10, { "Izzet Signet" });
    REQUIRE(tp != nullptr);
    GameState troll = t;
    TurnSolver::ApplyPlan(troll, *tp, true);
    CHECK(FindGs(troll, 32) != nullptr);
    CHECK(troll.floating_mana.colorless == 0);
    CHECK(HasteFloatUnits(troll) == 0);
    CHECK(troll.floating_haste_c == 0);
}

TEST_CASE("Generator Servant: a {C} a later tap over-produces never inherits the tag (no phantom haste)")
{
    // {C}{C} tagged -> the payer spends floating first, so a noncreature {2} (Izzet Signet) takes both.
    // Then a creature is paid partly by Sol Ring: its leftover {C} floats UNTAGGED.
    GameState s = FreshGs();
    PutGs(s, kGs, 10);
    PutGs(s, "Sol Ring", 11);
    PutGs(s, "Mountain", 1);
    ApplySacForMana(s, 0, 10, "C", 2);
    REQUIRE(HasteFloatUnits(s) == 2);
    {
        ManaCost signet = DefGs("Izzet Signet").card.m_mana_cost;
        REQUIRE(TapForCostShared(s, signet, /*for_creature=*/false, nullptr, true));
    }
    CHECK(s.floating_mana.colorless == 0);
    CHECK(HasteFloatUnits(s) == 0);
    // A creature spell's {1}{R}: the Mountain pays {R}, Sol Ring {1} and over-produces one {C}.
    ManaCost b = DefGs(kGs).card.m_mana_cost;
    CHECK_FALSE(TakeHasteFloatForCreatureSpell(s, b));   // nothing tagged to take
    REQUIRE(TapForCostShared(s, b, /*for_creature=*/true, nullptr, true));
    CHECK(s.floating_mana.colorless == 1);               // Sol Ring's spare {C}...
    CHECK(HasteFloatUnits(s) == 0);                      // ...is plain {C}
    CHECK(s.floating_haste_c == 0);
}

TEST_CASE("Generator Servant: a bill with no generic or {C} pip takes no {C} and gains no haste")
{
    GameState s = FreshGs();
    PutGs(s, kGs, 10);
    PutGs(s, "Forest", 1);
    ApplySacForMana(s, 0, 10, "C", 2);
    ManaCost birds = DefGs("Birds of Paradise").card.m_mana_cost;   // {G}
    CHECK_FALSE(TakeHasteFloatForCreatureSpell(s, birds));
    CHECK(birds.green == 1);
    CHECK(HasteFloatUnits(s) == 2);
    // A failed payment after a take is undone exactly.
    GameState t = FreshGs();
    PutGs(t, kGs, 10);
    ApplySacForMana(t, 0, 10, "C", 2);
    ManaCost bill = DefGs("Goblin Guide").card.m_mana_cost;   // {R}: no generic either
    CHECK_FALSE(TakeHasteFloatForCreatureSpell(t, bill));
    ManaCost two; two.generic = 3;                             // {3}: one tagged {C} taken, then unpayable
    REQUIRE(TakeHasteFloatForCreatureSpell(t, two));
    CHECK(two.generic == 2);
    CHECK(t.floating_mana.colorless == 1);
    CHECK(t.floating_haste_c == 1);
    UndoHasteFloatTake(t);
    CHECK(t.floating_mana.colorless == 2);
    CHECK(t.floating_haste_c == 2);
}

TEST_CASE("An Aura / soulbond host the SAME plan sacrifices for mana is never a plan's host (identity fold)")
{
    // Generator Servant's SacForMana is cracked in the apply pre-pass, before any cast, so a Curiosity
    // aimed at it (or a Tandem Lookout told to pair with it) names a host that is gone on resolution;
    // ResolveEnchantTarget then silently falls back ([enchant-retarget]). The fold rejects that subset;
    // the same plan with a LEGAL host must still be enumerated.
    GameState s = FreshGs();
    s.players[0].lands_played_this_turn = 1;
    PutGs(s, "Island", 18);
    PutGs(s, "Island", 19);
    PutGs(s, "Generator Servant", 12);
    PutGs(s, "Tandem Lookout", 57);
    HandGs(s, "Curiosity", 7);
    HandGs(s, "Tandem Lookout", 58);
    const long long before = g_enchant_retargets.load();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool legal_member = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool sacks_servant = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::SacForMana && a.sac_source_id == 12) { sacks_servant = true; } }
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (sacks_servant) { CHECK_MESSAGE(a.enchant_target != 12, "host sacrificed by its own plan: ", a.card_name.str()); }
            if (sacks_servant && a.card_name.str() == "Curiosity" && a.enchant_target == 57) { legal_member = true; }
        }
    }
    CHECK(legal_member);   // the class keeps its legal-host member
    // Realising every plan never trips the silent retarget.
    for (const TurnSolver::Plan& p : plans)
    {
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, /*is_pre_combat=*/true);
    }
    CHECK(g_enchant_retargets.load() == before);
}

// ---- THE USER's GENERATOR SERVANT RULE (2026-10-22, MTG_NIV_SERVANT_FIREMIND_ONLY, DEFAULT ON) --------
// "That one maybe should be specified by the search in cases where we have sufficient mana to cast it
//  otherwise. But we should only use it for Niv-mizzet the firemind in that case."
// NivMizzetComboProvider::SacForManaOnlyIfNeeded names the Servant; the engine's subset walks drop a
// non-winning plan whose Servant sacrifice is UNNEEDED (every other action pays without it, as the
// executor pays) unless that plan casts Niv-Mizzet, the Firemind. Pinned on the search's own menu
// (EnumerateMainPlans): the Firemind keeps both branches; any other spell the lands pay keeps only the
// no-sacrifice plan; a spell that NEEDS the {C}{C} keeps the sacrifice (ramp); the lever's =0 arm and
// every other deck's provider keep the full menu. (Human play is never pruned -- HumanPlayActive() is a
// process-static env read a unit test cannot switch on; the call sites gate it.)
namespace
{
const NivMizzetComboProvider& NivProvGs()
{
    static const NivMizzetComboProvider p;
    return p;
}

// Has some plan cast exactly `casts` (by name) with the Servant `src` sacrificed (sac) or kept (!sac)?
bool OfferedGs(const std::vector<TurnSolver::Plan>& plans, int src, bool sac,
               const std::vector<std::string>& casts)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty() || HasSacOf(p, src) != sac) { continue; }
        if (CastCount(p) != static_cast<int>(casts.size())) { continue; }
        bool all = true;
        for (const std::string& n : casts) { if (!CastsGs(p, n)) { all = false; break; } }
        if (all) { return true; }
    }
    return false;
}

struct ArmGs
{
    ArmGs(heurarm::Slot s, bool on) { heurarm::t_arm[static_cast<int>(s)] = on ? 1 : 0; }
    ~ArmGs() { heurarm::Clear(); }
};
}   // namespace

TEST_CASE("Niv Servant rule: the Firemind payable WITHOUT the Servant -- both branches are searched")
{
    // {2}{U}{U}{R}{R} off two Islands, two Mountains and Sol Ring: the Servant's {C}{C} is not needed, but
    // its haste lets the Firemind {T}: draw the turn it lands (the Lookout / Curiosity kill).
    GameState s = FreshGs();
    s.m_provider = &NivProvGs();
    PutGs(s, "Island", 1);
    PutGs(s, "Island", 2);
    PutGs(s, "Mountain", 3);
    PutGs(s, "Mountain", 4);
    PutGs(s, "Sol Ring", 5);
    PutGs(s, kGs, 10);
    HandGs(s, "Niv-Mizzet, the Firemind", 31);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(OfferedGs(plans, 10, /*sac=*/false, { "Niv-Mizzet, the Firemind" }));
    CHECK(OfferedGs(plans, 10, /*sac=*/true,  { "Niv-Mizzet, the Firemind" }));
    // A sacrifice that pays for nothing is not a Firemind line: gone.
    CHECK_FALSE(OfferedGs(plans, 10, /*sac=*/true, {}));
}

TEST_CASE("Niv Servant rule: a Tandem Lookout the lands pay -- no sacrifice branch (and no other one)")
{
    GameState s = FreshGs();
    s.m_provider = &NivProvGs();
    PutGs(s, "Island", 1);
    PutGs(s, "Island", 2);
    PutGs(s, "Island", 3);
    PutGs(s, kGs, 10);
    HandGs(s, "Tandem Lookout", 32);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(OfferedGs(plans, 10, /*sac=*/false, { "Tandem Lookout" }));
    CHECK_FALSE(OfferedGs(plans, 10, /*sac=*/true, { "Tandem Lookout" }));
    CHECK_FALSE(AnySacOf(plans, 10));   // no unneeded sacrifice survives anywhere on this menu

    // The lever's =0 arm restores the full menu (the A/B's control)...
    {
        ArmGs off(heurarm::NIV_SERVANT_FIREMIND_ONLY, false);
        const std::vector<TurnSolver::Plan> all = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(OfferedGs(all, 10, /*sac=*/true, { "Tandem Lookout" }));
    }
    // ...and so does every other deck's provider: the rule is NivMizzetCombo's alone.
    {
        GameState g = s;
        g.m_provider = nullptr;
        CHECK(OfferedGs(TurnSolver::EnumerateMainPlans(g, true), 10, /*sac=*/true, { "Tandem Lookout" }));
    }
}

TEST_CASE("Niv Servant rule: a spell NOT payable without the Servant still spends it (ramp is untouched)")
{
    // {2}{U} off one Island: only the Servant's {C}{C} makes the Lookout castable.
    GameState s = FreshGs();
    s.m_provider = &NivProvGs();
    PutGs(s, "Island", 1);
    PutGs(s, kGs, 10);
    HandGs(s, "Tandem Lookout", 32);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = FindPlanGs(plans, 10, { "Tandem Lookout" });
    REQUIRE(p != nullptr);
    // ...and it really resolves: the Lookout is on the battlefield, the Servant in the graveyard.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    CHECK(FindGs(roll, 32) != nullptr);
    CHECK(InGyGs(roll, 10));
    // The Firemind needing the Servant (one Mountain short) is kept as well, of course.
    GameState t = FreshGs();
    t.m_provider = &NivProvGs();
    PutGs(t, "Island", 1);
    PutGs(t, "Island", 2);
    PutGs(t, "Mountain", 3);
    PutGs(t, "Mountain", 4);
    PutGs(t, kGs, 10);
    HandGs(t, "Niv-Mizzet, the Firemind", 31);
    const std::vector<TurnSolver::Plan> tplans = TurnSolver::EnumerateMainPlans(t, true);
    CHECK(OfferedGs(tplans, 10, /*sac=*/true, { "Niv-Mizzet, the Firemind" }));
    CHECK_FALSE(OfferedGs(tplans, 10, /*sac=*/false, { "Niv-Mizzet, the Firemind" }));   // unpayable without
}

TEST_CASE("Niv Servant rule amendment (MTG_NIV_SERVANT_UNPAIR, DEFAULT OFF): a Servant holding a Lookout's pair")
{
    // A Tandem Lookout paired with the Servant cannot pair with an entering Niv-Mizzet, Parun; sacrificing
    // the Servant (its {C}{C} pays nothing of {U}{U}{U}{R}{R}{R}) breaks the pair so the Lookout pairs with
    // the Niv. The rule prunes that sacrifice; the amendment arm searches it.
    GameState s = FreshGs();
    s.m_provider = &NivProvGs();
    for (int k = 1; k <= 3; ++k) { PutGs(s, "Island", k); }
    for (int k = 4; k <= 6; ++k) { PutGs(s, "Mountain", k); }
    PutGs(s, kGs, 10);
    const int li = PutGs(s, "Tandem Lookout", 57);
    s.battlefield[li].paired_with = 10;
    REQUIRE(SoulbondPartnerIndex(s.battlefield[li], s) >= 0);
    HandGs(s, "Niv-Mizzet, Parun", 33);
    {
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(OfferedGs(plans, 10, /*sac=*/false, { "Niv-Mizzet, Parun" }));
        CHECK_FALSE(OfferedGs(plans, 10, /*sac=*/true, { "Niv-Mizzet, Parun" }));   // the rule as shipped
    }
    {
        ArmGs amend(heurarm::NIV_SERVANT_UNPAIR, true);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(OfferedGs(plans, 10, /*sac=*/true, { "Niv-Mizzet, Parun" }));
        CHECK(OfferedGs(plans, 10, /*sac=*/false, { "Niv-Mizzet, Parun" }));
        // ...and only while the Servant really holds the pair.
        GameState u = s;
        for (Permanent& p : u.battlefield) { if (p.card.m_number == 57) { p.paired_with = 0; } }
        CHECK_FALSE(OfferedGs(TurnSolver::EnumerateMainPlans(u, true), 10, /*sac=*/true, { "Niv-Mizzet, Parun" }));
    }
}
