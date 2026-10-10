// Unit cover for the Bruna Stage 5d claude-play sweep findings (2026-10-05, seeds 77001-77020).
// Each case pins one confirmed defect from the sweep and is written so that it FAILS on the pre-fix
// tree (the control): the sweep ledger (docs/design/analysis-Bruna.md, "## Claude-play sweep") names
// the fix commit for each.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/Combat.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsBs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefBs(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardBs(const std::string& name, int number)
{
    Card c     = DefBs(name).card;
    c.m_number = number;
    c.RehashName();
    return c;
}

const DecisionProvider& Bruna()
{
    static const BrunaProvider prov;
    return prov;
}

struct BoardBs
{
    GameState s;
    int next = 1;
    BoardBs()
    {
        EnsureCardsBs();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &Bruna();
        // A dead library so a cantrip / rollout draw never perturbs the board.
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardBs("Boseiju, Who Endures", 900 + k)); }
    }
    int Put(const std::string& name, bool tapped = false, bool sick = false)
    {
        Permanent p;
        p.card              = CardBs(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.tapped            = tapped;
        p.entered_this_turn = sick;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardBs(name, next)); return next++; }
};

std::vector<std::string> CastNames(const TurnSolver::Plan& p)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    return v;
}

bool PlanCasts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const std::string& n : CastNames(p)) { if (n == name) { return true; } }
    return false;
}

int CountOnBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

// ---- A-i: Somberwald Sage's creature-only surplus in a MIXED batch prepay ----------------------
// Seed 77012 T3: Avacyn's Pilgrim {G} + Lightning Greaves {2} + Mother of Runes {W} off Forest, an
// old Pilgrim, Skycloud Expanse and an old Sage. The combined (noncreature) solve cannot use the
// Sage, so the mixed two-stage solve pays the creature stage with it -- and the surplus used to land
// in the GENERAL float, spendable on Colossification.
TEST_CASE("Bruna sweep A-i: a mixed batch never launders Somberwald Sage's creature-only surplus")
{
    BoardBs b;
    b.s.players[0].lands_played_this_turn = 1;
    b.Put("Forest");
    b.Put("Avacyn's Pilgrim");
    b.Put("Skycloud Expanse");
    b.Put("Somberwald Sage");
    b.Hand("Avacyn's Pilgrim");
    b.Hand("Lightning Greaves");
    b.Hand("Mother of Runes");

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* all3 = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool equip = false;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::Equip) { equip = true; } }
        if (!equip && CastNames(p).size() == 3) { all3 = &p; break; }
    }
    REQUIRE_MESSAGE(all3 != nullptr, "the three-cast plan was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *all3, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Avacyn's Pilgrim") == 2);
    CHECK(CountOnBf(after, "Lightning Greaves") == 1);
    CHECK(CountOnBf(after, "Mother of Runes") == 1);
    // The Sage made three of one colour; one paid a creature pip, the rest is creature-only.
    CHECK_MESSAGE(after.floating_mana.Total() == 0, "creature-only mana leaked into the general float");
    CHECK(after.floating_creature_mana.Total() == 2);
}

// ---- A-ii: a Greaves-hasted Sage's creature-only mana is not credited to a noncreature payoff ----
// Seed 77003 T4: Sage {2}{G} + Colossification {5}{G}{G} + equip Greaves -> Sage off 7 general mana.
// The haste-dork credit used to add the Sage's three units to the joint check, offering six plans
// that each drop a cast.
TEST_CASE("Bruna sweep A-ii: no plan pays a noncreature Aura with a hasted Sage's creature-only mana")
{
    BoardBs b;
    for (int k = 0; k < 6; ++k) { b.Put("Forest"); }
    b.Put("Birds of Paradise");
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Colossification");
    b.Hand("Mythic Proportions");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    for (const TurnSolver::Plan& p : plans)
    {
        const bool sage = PlanCasts(p, "Somberwald Sage");
        CHECK_MESSAGE(!(sage && PlanCasts(p, "Colossification")), "unpayable Sage + Colossification offered");
        CHECK_MESSAGE(!(sage && PlanCasts(p, "Mythic Proportions")), "unpayable Sage + Mythic Proportions offered");
    }
}

// The cap must not remove a line the restricted mana CAN fund: Sage + equip + Mother of Runes off
// three Forests -- the Forests pay the Sage, the hasted Sage's {W}{W}{W} pays Mother's {W}.
TEST_CASE("Bruna sweep A-ii: a hasted Sage still funds a creature payoff")
{
    BoardBs b;
    for (int k = 0; k < 3; ++k) { b.Put("Forest"); }
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Mother of Runes");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    { if (PlanCasts(p, "Somberwald Sage") && PlanCasts(p, "Mother of Runes")) { line = &p; break; } }
    REQUIRE_MESSAGE(line != nullptr, "Sage + equip + Mother of Runes was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Mother of Runes") == 1);
    CHECK(CountOnBf(after, "Somberwald Sage") == 1);
    CHECK(after.floating_mana.Total() == 0);
}

// ...and the SEARCH must be able to haste the Sage at all when a bigger creature is in hand: the
// width-1 haste ranking used to give Greaves only to Bruna (the attack pick), so "Sage + equip ->
// Sage + Bruna" (three lands pay the Sage, its {W}{W}{W} + three more pay Bruna) was inexpressible
// outside human play.
TEST_CASE("Bruna sweep A-iii: the search offers Greaves -> a locked mana dork beside the attack host")
{
    BoardBs b;
    for (int k = 0; k < 3; ++k) { b.Put("Forest"); }
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Island");
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Bruna, Light of Alabaster");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    { if (PlanCasts(p, "Somberwald Sage") && PlanCasts(p, "Bruna, Light of Alabaster")) { line = &p; break; } }
    REQUIRE_MESSAGE(line != nullptr, "Sage + equip -> Sage + Bruna was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Bruna, Light of Alabaster") == 1);
    CHECK(CountOnBf(after, "Somberwald Sage") == 1);
}

// ---- B: Aura spells vs a Greaves'd creature, ordered against the plan's own equips --------------
namespace
{
// Mother of Runes wearing Lightning Greaves (seed 77002 / 77014 shape).
struct GreavesOnMother
{
    BoardBs b;
    int mother = 0, greaves = 0;
    GreavesOnMother()
    {
        mother  = b.Put("Mother of Runes");
        greaves = b.Put("Lightning Greaves");
        for (Permanent& p : b.s.battlefield) { if (p.card.m_number == greaves) { p.equipped_to = mother; } }
    }
};

int AuraHostOf(const GameState& s, const std::string& aura)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == aura) { return p.aura_attached_to; } }
    return -1;
}

int EquipHostOf(const GameState& s, int equip)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == equip) { return p.equipped_to; } }
    return -1;
}

// The AURA CAST HOST RANKING's proof CONTROL arm (MTG_AURA_HOST_BRANCH=1) for the scope: the search
// keeps every creature-Aura host as its own plan. The expressibility cases below ask "can the engine
// state this line at all", which is the branched plan list's question; the shipped arm keeps the
// ranking's pick per plan class (its own cases at the end of this file).
struct BranchArm
{
    BranchArm()  { heurarm::t_arm[heurarm::AURA_HOST_BRANCH] = 1; }
    ~BranchArm() { heurarm::t_arm[heurarm::AURA_HOST_BRANCH] = -1; }
};
}   // namespace

// Seed 77002 T4: three lands, Pilgrim + Courage in hand. The only way to cast both is Pilgrim, equip
// Greaves -> Pilgrim (haste: it taps for {W}), Courage -> Mother (no longer shrouded). The search used
// to offer only "Courage -> Pilgrim" (illegal once the Pilgrim is Greaves'd) and the executor silently
// put Courage on Mother. Every enumerated plan must now realise exactly as labelled.
TEST_CASE("Bruna sweep B: Courage -> Mother after Greaves moves to the fresh Pilgrim; no silent retarget")
{
    GreavesOnMother g;
    g.b.Put("Forest");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Razorverge Thicket");
    const int pilgrim = g.b.Hand("Avacyn's Pilgrim");
    g.b.Hand("Unflinching Courage");
    g.b.s.players[0].lands_played_this_turn = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Unflinching Courage"
                && a.enchant_target == g.mother && PlanCasts(p, "Avacyn's Pilgrim")) { line = &p; }
        }
    }
    REQUIRE_MESSAGE(line != nullptr, "Pilgrim + equip -> Pilgrim + Courage -> Mother was not enumerated");
    GameState after = g.b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(AuraHostOf(after, "Unflinching Courage") == g.mother);
    CHECK(EquipHostOf(after, g.greaves) == pilgrim);
    // No enumerated plan may realise a different Aura host than it names.
    const long long before = g_enchant_retargets.load();
    for (const TurnSolver::Plan& p : plans)
    {
        GameState t = g.b.s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    }
    CHECK_MESSAGE(g_enchant_retargets.load() == before, "a plan's Aura was silently retargeted");
}

// Seed 77014 T4: Pilgrim already on the battlefield, Greaves on Mother. "Equip Greaves -> Pilgrim, THEN
// Arcanum Wings -> Mother" needs the equip BEFORE the cast; equips used to trail every cast, so the
// line was inexpressible.
TEST_CASE("Bruna sweep B: equip Greaves to another creature, then an Aura on the creature it left")
{
    BranchArm arm;   // expressibility: the branched plan list (see BranchArm)
    GreavesOnMother g;
    const int pilgrim = g.b.Put("Avacyn's Pilgrim");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Island");
    g.b.Hand("Arcanum Wings");
    g.b.s.players[0].lands_played_this_turn = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool wings_on_mother = false, move = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.enchant_target == g.mother) { wings_on_mother = true; }
            if (a.kind == Action::Kind::Equip && a.sac_victim_id == pilgrim) { move = true; }
        }
        if (wings_on_mother && move) { line = &p; }
    }
    REQUIRE_MESSAGE(line != nullptr, "equip Greaves -> Pilgrim + Wings -> Mother was not enumerated");
    GameState after = g.b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(AuraHostOf(after, "Arcanum Wings") == g.mother);
    CHECK(EquipHostOf(after, g.greaves) == pilgrim);
    // ...and without the move the Aura is still not offered onto the shrouded Mother.
    for (const TurnSolver::Plan& p : plans)
    {
        bool wings_on_mother = false, move = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.enchant_target == g.mother) { wings_on_mother = true; }
            if (a.kind == Action::Kind::Equip && a.sac_victim_id == pilgrim) { move = true; }
        }
        CHECK_MESSAGE(!(wings_on_mother && !move), "an Aura targets the shrouded Mother with no Greaves move");
    }
}

// The autonomous plan dedup used to key a creature Aura by NAME only and keep the FIRST enumerated
// host (battlefield order). Now the class keeps the shared host ranking's pick; the proof's control
// arm (MTG_AURA_HOST_BRANCH=1) keeps every host. Here the first-enumerated host is a summoning-sick
// Pilgrim, so the old fold and the ranking disagree: Mythic Proportions must go on Mother, who can
// attack this turn.
TEST_CASE("Aura host ranking: one ranked host per plan class; the control arm keeps every host")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    const int mother  = b.Put("Mother of Runes");
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Mythic Proportions");
    b.s.players[0].lands_played_this_turn = 1;
    auto hosts = [&](const std::vector<TurnSolver::Plan>& plans, bool& on_mother, bool& on_pilgrim)
    {
        on_mother = on_pilgrim = false;
        for (const TurnSolver::Plan& p : plans)
        {
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Mythic Proportions") { continue; }
                if (a.enchant_target == mother)  { on_mother = true; }
                if (a.enchant_target == pilgrim) { on_pilgrim = true; }
            }
        }
    };
    bool m = false, pg = false;
    hosts(TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true), m, pg);
    CHECK(m);
    CHECK_FALSE(pg);
    {
        BranchArm arm;
        hosts(TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true), m, pg);
        CHECK(m);
        CHECK(pg);
    }
}

// ---- C: the orphaned-Aura SBA (CR 704.5m) runs as the land leaves, not at the next combat --------
TEST_CASE("Bruna sweep C: Wild Growth on a depleted Remote Farm goes to the graveyard with the sack")
{
    BoardBs b;
    const int farm = b.Put("Remote Farm", /*tapped=*/true);
    b.Put("Forest");
    for (Permanent& p : b.s.battlefield)
    { if (p.card.m_number == farm) { p.counters.Add(Counter{ Counter::Type::Depletion, 0 }); } }
    Permanent wg;
    wg.card = CardBs("Wild Growth", 50); wg.controller_index = 0; wg.owner_index = 0;
    wg.aura_attached_to = farm;
    b.s.battlefield.push_back(wg);
    SacrificeDepletedLands(b.s);
    CHECK(CountOnBf(b.s, "Remote Farm") == 0);
    CHECK_MESSAGE(CountOnBf(b.s, "Wild Growth") == 0, "the orphaned Wild Growth stayed on the battlefield");
    bool in_gy = false;
    for (const Card& c : b.s.players[0].graveyard) { if (c.m_name.str() == "Wild Growth") { in_gy = true; } }
    CHECK(in_gy);
}

TEST_CASE("Bruna sweep C: an Azorius Chancery bouncing the Wild Growth'd land sends the Aura to the graveyard")
{
    BoardBs b;
    const int sanctum = b.Put("Botanical Sanctum", /*tapped=*/true);
    Permanent wg;
    wg.card = CardBs("Wild Growth", 50); wg.controller_index = 0; wg.owner_index = 0;
    wg.aura_attached_to = sanctum;
    b.s.battlefield.push_back(wg);
    const int chancery = b.Put("Azorius Chancery", /*tapped=*/true);
    BounceKarooLand(b.s, 0, static_cast<int>(b.s.battlefield.size()) - 1);
    CHECK(CountOnBf(b.s, "Botanical Sanctum") == 0);   // the only other land: the bounce takes it
    CHECK(CountOnBf(b.s, "Azorius Chancery") == 1);
    CHECK_MESSAGE(CountOnBf(b.s, "Wild Growth") == 0, "the orphaned Wild Growth stayed on the battlefield");
    bool chancery_ok = false;
    for (const Permanent& p : b.s.battlefield) { if (p.card.m_number == chancery) { chancery_ok = true; } }
    CHECK(chancery_ok);
}

// ---- D: Wild Growth on the Azorius Chancery played this same turn -------------------------------
// Seed 77001 T2: the karoo is played AFTER the casts (deferred), so it was never on the battlefield at
// enumeration and "Wild Growth -> the new Chancery" was inexpressible at any budget. The plan now
// names the in-hand karoo as the host and both worlds cast the Aura right after the karoo lands.
TEST_CASE("Bruna sweep D: Wild Growth can enchant the Azorius Chancery played this turn")
{
    BoardBs b;
    b.Put("Birds of Paradise");
    b.Put("Botanical Sanctum");
    const int chancery = b.Hand("Azorius Chancery");
    b.Hand("Wild Growth");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play != "Azorius Chancery") { continue; }
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.enchant_target == chancery) { line = &p; } }
    }
    REQUIRE_MESSAGE(line != nullptr, "land=Chancery + Wild Growth -> Chancery was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(AuraHostOf(after, "Wild Growth") == chancery);
    CHECK(CountOnBf(after, "Azorius Chancery") == 1);
}

// ---- E (seed 77001 T4): the repeated Arcanum Wings swap in one main phase ------------------------
// Swap Wings <-> Conscription, recast Wings on Mother, swap again: 1 + 10 + 10 = 21. The second swap
// needed a site-9 continuation inside a site-10 continuation -- inexpressible (T5 at any budget).
TEST_CASE("Bruna sweep E: Wings swap chain -- swap, recast, swap -- is one searched plan")
{
    BoardBs b;
    const int mother = b.Put("Mother of Runes");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = mother;
    b.s.battlefield.push_back(w);
    // Exactly the 8 mana the chain costs ({2}{U} + {1}{U} + {2}{U}), three blue: Chancery's {W}{U},
    // Birds, and the Botanical Sanctum drop (one other land -> it enters untapped).
    b.Put("Avacyn's Pilgrim");
    b.Put("Avacyn's Pilgrim");
    b.Put("Azorius Chancery");
    b.Put("Birds of Paradise");
    b.Put("Sol Ring");
    b.Hand("Botanical Sanctum");
    b.Hand("Eldrazi Conscription");
    b.Hand("Eldrazi Conscription");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::AuraSwap && a.chosen_x == 2) { line = &p; } }
    }
    REQUIRE_MESSAGE(line != nullptr, "the two-link Wings swap chain was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    int conscriptions_on_mother = 0;
    for (const Permanent& p : after.battlefield)
    { if (p.card.m_name.str() == "Eldrazi Conscription" && p.aura_attached_to == mother) { ++conscriptions_on_mother; } }
    CHECK(conscriptions_on_mother == 2);
}

// ---- G: the payer spends Somberwald Sage's creature-only mana on a creature spell first -----------
// Seed 77019 T4: Bruna {3}{W}{W}{U} off Birds, Sanctum, Thicket, Citadel, Forest and an old Sage. The
// Sage was ranked LAST: five general sources tapped, then the Sage for the last generic pip -- eight
// mana for a six-cost spell. Now Sage + three lands pay it and two general sources stay up.
TEST_CASE("Bruna sweep G: a creature spell taps the creature-only Sage before general sources")
{
    BoardBs b;
    b.Put("Birds of Paradise");
    b.Put("Botanical Sanctum");
    b.Put("Razorverge Thicket");
    b.Put("Seaside Citadel");
    b.Put("Forest");
    const int sage = b.Put("Somberwald Sage");
    const CardDefinition& bruna = DefBs("Bruna, Light of Alabaster");
    REQUIRE(TapForCostShared(b.s, bruna.card.m_mana_cost, /*for_creature=*/true, nullptr, true));
    int untapped = 0; bool sage_tapped = false;
    for (const Permanent& p : b.s.battlefield)
    {
        if (p.card.m_number == sage) { sage_tapped = p.tapped; continue; }
        if (!p.tapped) { ++untapped; }
    }
    CHECK(sage_tapped);
    CHECK_MESSAGE(untapped == 2, "general sources wasted on a creature spell the Sage could pay");
}

// ---- F: a plan's canonical order must realise the casts it was admitted on -----------------------
// Seed 77015 T3: Lightning Greaves {2} + Mother of Runes {W} + Wild Growth {G} off three lands is
// payable only with Wild Growth FIRST (its host then taps for two). The enumerator credits that ramp,
// but the canonical order ranked creatures (10) before the Aura (20), so the apply cast Wild Growth
// last off nothing and dropped it -- the search could not express the working order at all.
TEST_CASE("Bruna sweep F: Wild Growth's same-turn ramp is cast before the spells it funds")
{
    BoardBs b;
    b.Put("Botanical Sanctum");
    b.Put("Razorverge Thicket");
    b.Put("Boseiju, Who Endures");
    b.Hand("Lightning Greaves");
    b.Hand("Mother of Runes");
    b.Hand("Wild Growth");
    b.s.players[0].lands_played_this_turn = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool equip = false;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::Equip) { equip = true; } }
        if (!equip && CastNames(p).size() == 3) { line = &p; break; }
    }
    REQUIRE_MESSAGE(line != nullptr, "the three-cast plan was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Lightning Greaves") == 1);
    CHECK(CountOnBf(after, "Mother of Runes") == 1);
    CHECK_MESSAGE(CountOnBf(after, "Wild Growth") == 1, "the plan dropped Wild Growth");
}

// B (destination half): "Courage -> Pilgrim, move Greaves Mother -> Pilgrim, Wings -> Mother" is legal
// in that order (the Aura onto the move's destination resolves before the Greaves arrives).
TEST_CASE("Bruna sweep B: an Aura on the Greaves move's destination resolves before the move")
{
    BranchArm arm;   // expressibility: the branched plan list (see BranchArm)
    GreavesOnMother g;
    const int pilgrim = g.b.Put("Avacyn's Pilgrim");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Botanical Sanctum");
    g.b.Put("Island");
    g.b.Put("Forest");
    g.b.Hand("Unflinching Courage");
    g.b.Hand("Arcanum Wings");
    g.b.s.players[0].lands_played_this_turn = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool c_on_p = false, w_on_m = false, move = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Unflinching Courage" && a.enchant_target == pilgrim) { c_on_p = true; }
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Arcanum Wings" && a.enchant_target == g.mother) { w_on_m = true; }
            if (a.kind == Action::Kind::Equip && a.sac_victim_id == pilgrim) { move = true; }
        }
        if (c_on_p && w_on_m && move) { line = &p; }
    }
    REQUIRE_MESSAGE(line != nullptr, "Courage -> Pilgrim + move Greaves -> Pilgrim + Wings -> Mother was not enumerated");
    const long long before = g_enchant_retargets.load();
    GameState after = g.b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(AuraHostOf(after, "Unflinching Courage") == pilgrim);
    CHECK(AuraHostOf(after, "Arcanum Wings") == g.mother);
    CHECK(EquipHostOf(after, g.greaves) == pilgrim);
    CHECK(g_enchant_retargets.load() == before);
}

// ---- G follow-up: a creature-only source that could ATTACK keeps its late rank -------------------
// 2026-10-05 regression verdict, Angels: Giada, Font of Hope (2/2 vigilance, Angel-only {W}) ranked
// first paid Righteous Valkyrie pre-combat and could not attack -- 146 slower games. A tap that costs
// an attack is not free: the payer spends the lands and leaves Giada up. (The power-0 Sage case --
// creature-first still applies -- is the G test above.)
TEST_CASE("Bruna sweep G: an attack-capable creature-only source is not tapped first")
{
    BoardBs b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Plains");
    const int giada = b.Put("Giada, Font of Hope");
    const CardDefinition& valk = DefBs("Righteous Valkyrie");
    SpellSubtypePayScope scope(&valk.card);
    REQUIRE(TapForCostShared(b.s, valk.card.m_mana_cost, /*for_creature=*/true, nullptr, true));
    bool giada_tapped = false; int lands_tapped = 0;
    for (const Permanent& p : b.s.battlefield)
    {
        if (p.card.m_number == giada) { giada_tapped = p.tapped; continue; }
        if (p.tapped) { ++lands_tapped; }
    }
    CHECK_MESSAGE(!giada_tapped, "an attack-capable creature paid a pre-combat creature spell first");
    CHECK(lands_tapped == 3);
}

// ---- A-iii follow-up: the mana-unlock host is a SEARCHED widening only ---------------------------
// Suite verdict 2026-10-05, FiveColour d0 (reg s2002 gi445 / gi953, smoke gi188): the kept unlock
// host was also offered to Solve's greedy (d0 decision + rollout leaves), which has no haste-dork
// mana credit, so it was taken on its DMG haste eval -- Greaves -> Birds of Paradise (a 0-power
// dork whose mana funded nothing) over the attack host, one damage short. The greedy must equip the
// attack pick; the search (EnumerateMainPlans) still offers the dork.
TEST_CASE("Bruna sweep A-iii: the greedy Solve does not haste a mana dork the attack ranking passed over")
{
    BoardBs b;
    b.Put("Forest");
    b.Put("Forest");
    const int greaves = b.Put("Lightning Greaves");
    const int birds   = b.Put("Birds of Paradise", /*tapped=*/false, /*sick=*/true);
    const int shaman  = b.Put("Deathrite Shaman", /*tapped=*/false, /*sick=*/true);
    const TurnSolver::Plan plan = TurnSolver::Solve(b.s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    int host = 0;
    for (const Action& a : plan.actions)
    { if (a.kind == Action::Kind::Equip && a.sac_source_id == greaves) { host = a.sac_victim_id; } }
    CHECK_MESSAGE(host != birds, "greedy hasted Birds of Paradise (unlock-only host)");
    CHECK(host == shaman);
    // The searched enumeration still carries the unlock host.
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::Equip && a.sac_victim_id == birds) { offered = true; } }
    }
    CHECK(offered);
}

// ---- AURA CAST HOST RANKING (USER 2026-10-06) -----------------------------------------------------
// One ranking (AuraPlanHostKey / AuraCastHostByRank, core/SpellEffects.h "AURA CAST HOST RANKING")
// decides which creature an Aura SPELL enchants -- the search's plan classes, Solve (d0 + rollout
// leaves) and the resolution fallback. Each case puts the host the OLD first-enumerated fold would
// have kept FIRST on the battlefield, so it fails if the ranking's term under test is removed.
namespace
{
// The host every enumerated plan that casts `aura` names (0 = never cast, -1 = more than one host).
int OnlyHostOf(const std::vector<TurnSolver::Plan>& plans, const std::string& aura)
{
    int host = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != aura) { continue; }
            if (host == 0) { host = a.enchant_target; }
            else if (host != a.enchant_target) { return -1; }
        }
    }
    return host;
}

int SolveHostOf(const GameState& s, const std::string& aura)
{
    const TurnSolver::Plan plan = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    for (const Action& a : plan.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == aura) { return a.enchant_target; } }
    return 0;
}
}   // namespace

// A mana dork the plan must tap to pay for the Aura cannot attack this turn: Courage {1}{G}{W} off two
// lands needs the Pilgrim's {W}, so Courage goes on Mother (who attacks for 3), not the Pilgrim.
TEST_CASE("Aura host ranking: a host the plan taps for mana loses its attack")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    const int mother  = b.Put("Mother of Runes");
    b.Put("Forest");
    b.Put("Forest");
    b.Hand("Unflinching Courage");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Unflinching Courage") == mother);
    CHECK(SolveHostOf(b.s, "Unflinching Courage") == mother);
    // With a third land nothing taps the Pilgrim and the damage key ties; the tie-break keeps the
    // Aura off the creature mana source (it will be tapped for mana on later turns).
    b.Put("Plains");
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Unflinching Courage") == mother);
    CHECK(SolveHostOf(b.s, "Unflinching Courage") == mother);
    (void)pilgrim;
}

// Colossification taps its host as it enters. On Mother (ready to attack) that costs this turn's
// swing; on a summoning-sick Pilgrim it costs nothing -- the Pilgrim takes it.
TEST_CASE("Aura host ranking: Colossification's ETB tap goes on a host that cannot attack anyway")
{
    BoardBs b;
    const int mother  = b.Put("Mother of Runes");
    const int pilgrim = b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Colossification");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Colossification") == pilgrim);
    CHECK(SolveHostOf(b.s, "Colossification") == pilgrim);
    (void)mother;
}

// Bruna gathers every Aura when she attacks, so Colossification on Bruna (tapped: no attack) loses the
// whole swing, while Colossification on the Pilgrim is gathered onto an attacking Bruna (+20 now).
TEST_CASE("Aura host ranking: a gathering attacker is priced -- Colossification stays off Bruna")
{
    BoardBs b;
    const int bruna   = b.Put("Bruna, Light of Alabaster");
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Colossification");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Colossification") == pilgrim);
    CHECK(SolveHostOf(b.s, "Colossification") == pilgrim);
    (void)bruna;
}

// Greaves' shroud is respected by the ranking's candidates: with Greaves on Mother, an Aura reaches
// Mother only in a plan that moves the Greaves away first; the shipped arm never names a shrouded host
// without that move, in the search or in Solve.
TEST_CASE("Aura host ranking: never a Greaves-shrouded host without the plan's own move")
{
    GreavesOnMother g;
    g.b.Put("Avacyn's Pilgrim");
    for (int k = 0; k < 7; ++k) { g.b.Put("Forest"); }
    g.b.Hand("Mythic Proportions");
    g.b.s.players[0].lands_played_this_turn = 1;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, true))
    {
        bool on_mother = false, move = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.enchant_target == g.mother) { on_mother = true; }
            if (a.kind == Action::Kind::Equip && a.sac_source_id == g.greaves && a.sac_victim_id != g.mother) { move = true; }
        }
        CHECK_MESSAGE(!(on_mother && !move), "Mythic Proportions names the shrouded Mother with no Greaves move");
    }
    {
        const TurnSolver::Plan plan = TurnSolver::Solve(g.b.s, /*is_pre_combat=*/true,
            TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
        bool on_mother = false, move = false;
        for (const Action& a : plan.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.enchant_target == g.mother) { on_mother = true; }
            if (a.kind == Action::Kind::Equip && a.sac_source_id == g.greaves && a.sac_victim_id != g.mother) { move = true; }
        }
        CHECK_MESSAGE(!(on_mother && !move), "Solve names the shrouded Mother with no Greaves move");
    }
    const long long before = g_enchant_retargets.load();
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, true))
    { GameState t = g.b.s; TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true); }
    CHECK(g_enchant_retargets.load() == before);
}

// The resolution-time fallback (an Aura with no usable target) uses the same key: Mother can attack,
// the sick Pilgrim cannot, so an orphaned Mythic Proportions lands on Mother -- not on the lowest card
// number (the Pilgrim), which the historical fallback picked.
TEST_CASE("Aura host ranking: the resolution fallback ranks with the same key")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    const int mother  = b.Put("Mother of Runes");
    b.s.phase = Phase::PreCombatMain;
    Permanent aura;
    aura.card             = CardBs("Mythic Proportions", 500);
    aura.controller_index = 0;
    aura.owner_index      = 0;
    b.s.battlefield.push_back(aura);
    const int slot = static_cast<int>(b.s.battlefield.size()) - 1;
    CHECK(ResolveEnchantTarget(b.s, 0, 0, false, slot) == mother);
    CHECK(ResolveEnchantTarget(b.s, 0, 0, false) == pilgrim);   // no slot: the historical scoring
}

// A creature-ONLY mana source (Somberwald Sage) cannot pay for an Aura, so the key must not "tap" it
// for one: Almost Perfect {4}{G}{W} off five lands needs one more mana, which only the Pilgrim can
// make -- the Pilgrim taps, the Sage stays up and is the host that attacks (proof run 1, gi52).
TEST_CASE("Aura host ranking: a creature-only mana source is never spent on an Aura")
{
    BoardBs b;
    const int sage    = b.Put("Somberwald Sage");
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    b.Put("Forest");
    b.Put("Forest");
    b.Put("Plains");
    b.Put("Forest");
    b.Put("Forest");
    b.Hand("Almost Perfect");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Almost Perfect") == sage);
    CHECK(SolveHostOf(b.s, "Almost Perfect") == sage);
    (void)pilgrim;
}

// NEXT turn's mana: a creature mana source the deck must tap next turn does not swing next turn.
// Colossification off seven lands; Eldrazi Conscription {8} stays in hand, so next turn the seven
// lands leave it one short and the fresh Birds of Paradise pays. Colossification on the Birds would
// score +20 next turn for a creature that will be tapped for mana; on Mother it swings for 21
// (proof run 5, d0 gi566).
TEST_CASE("Aura host ranking: a mana creature the deck needs next turn is not counted as next turn's attacker")
{
    BoardBs b;
    const int birds  = b.Put("Birds of Paradise", /*tapped=*/false, /*sick=*/true);
    const int mother = b.Put("Mother of Runes");
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Colossification");
    b.Hand("Eldrazi Conscription");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Colossification") == mother);
    CHECK(SolveHostOf(b.s, "Colossification") == mother);
    (void)birds;
}

// ---- GREEDY AURA-SWAP TIMING (MTG_SOLVE_COMBAT_SWAP, USER 2026-10-06) ------------------------------
// Bruna d0 smoke gi12 / gi885 / gi681: the greedy Solve swapped Arcanum Wings -> Colossification in the
// MAIN phase onto a creature that could attack; the ETB tap cost the attack (gi885: Mother with Eldrazi
// Conscription, opponent at 5, no attack T7). The same Aura brought in by the IN-COMBAT swap taps an
// attacker that stays in combat (CR 506.4). The control arm (slot forced 0) is the pre-fix policy.
namespace
{
struct SolveSwapArm
{
    std::int8_t prev;
    explicit SolveSwapArm(bool on) : prev(heurarm::t_arm[heurarm::SOLVE_COMBAT_SWAP])
    { heurarm::t_arm[heurarm::SOLVE_COMBAT_SWAP] = on ? 1 : 0; }
    ~SolveSwapArm() { heurarm::t_arm[heurarm::SOLVE_COMBAT_SWAP] = prev; }
};

// The greedy plan as the d0 runner / horizon leaf commits it: Solve, then the swap-timing pass that
// runs right before the combat pin.
bool SolveTakesSwap(const GameState& s)
{
    TurnSolver::Plan plan = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    TurnSolver::DeferAuraSwapToCombat(s, /*is_pre_combat=*/true, plan);
    for (const Action& a : plan.actions) { if (a.kind == Action::Kind::AuraSwap) { return true; } }
    return false;
}

BoardBs WingsOnMother(bool mother_sick)
{
    BoardBs b;
    const int mother = b.Put("Mother of Runes", /*tapped=*/false, /*sick=*/mother_sick);
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = mother;
    b.s.battlefield.push_back(w);
    // Exactly the swap's {2}{U}: Chancery {W}{U} + Forest.
    b.Put("Azorius Chancery");
    b.Put("Forest");
    b.Hand("Colossification");
    return b;
}
}   // namespace

TEST_CASE("Greedy Aura-swap timing: no pre-combat Colossification swap onto a would-be attacker")
{
    BoardBs b = WingsOnMother(/*mother_sick=*/false);
    { SolveSwapArm off(false); CHECK_MESSAGE(SolveTakesSwap(b.s), "control: the pre-fix greedy takes the main-phase swap"); }
    { SolveSwapArm on(true);   CHECK_FALSE(SolveTakesSwap(b.s)); }
    // The combat window still brings it in: the pin the d0 runner / rollout leaf set.
    TurnSolver::Plan none;
    TurnSolver::PinRolloutAuraSwap(b.s, /*is_pre_combat=*/true, none);
    CHECK(none.combat_aura_swap_choice == 70);
}

TEST_CASE("Greedy Aura-swap timing: a host that cannot attack keeps the main-phase swap")
{
    BoardBs b = WingsOnMother(/*mother_sick=*/true);
    SolveSwapArm on(true);
    CHECK(SolveTakesSwap(b.s));
}

// s9420000 gi965: the Wings host is Birds of Paradise and only the Birds' own mana completes the
// {2}{U}. An attacking Birds cannot tap for the combat swap, so the main-phase swap (paid in the ETB
// respond window) is the only way Colossification comes in this turn -- it is kept.
TEST_CASE("Greedy Aura-swap timing: a swap only a mana-creature host can pay stays in the main phase")
{
    BoardBs b;
    const int birds = b.Put("Birds of Paradise");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = birds;
    b.s.battlefield.push_back(w);
    b.Put("Azorius Chancery");   // {W}{U}: one short of {2}{U} without the Birds
    b.Hand("Colossification");
    SolveSwapArm on(true);
    CHECK(SolveTakesSwap(b.s));
}

// ---- HUMAN-PLAY AURA-SWAP TIMING (HumanCombatSwapOn, USER 2026-10-06) ------------------------------
// "My recommendation for the viewer is that it should be automatically applied in the attack phase
// rather than the 1st main." A human who commits `auraswap=Colossification` in main 1 onto a creature
// that could attack gets the swap in the COMBAT window instead (same rule + affordability as the
// greedy's MTG_SOLVE_COMBAT_SWAP), with the Aura they named and without being asked again.
namespace
{
TurnSolver::Plan HumanSwapPlan(const std::string& aura_in)
{
    TurnSolver::Plan p;
    Action a;
    a.kind          = Action::Kind::AuraSwap;
    a.card_name     = aura_in;
    a.hand_index    = -1;                                      // human: the NAMED hand Aura
    a.cost          = *DefBs("Arcanum Wings").params.aura_swap_cost;
    a.sac_source_id = 70;
    p.actions.push_back(a);
    return p;
}

struct LegacySwapScope
{
    bool prev;
    explicit LegacySwapScope(bool legacy) : prev(g_play_legacy_main_swap) { g_play_legacy_main_swap = legacy; }
    ~LegacySwapScope() { g_play_legacy_main_swap = prev; }
};

int BfIndex(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { return i; } }
    return -1;
}
}   // namespace

TEST_CASE("Human swap timing: a named Colossification swap onto a would-be attacker comes in after attackers, unasked")
{
    BoardBs b = WingsOnMother(/*mother_sick=*/false);
    const int mother = b.s.battlefield.front().card.m_number;
    const int colo   = b.s.players[0].hand.back().m_number;
    TurnSolver::Plan plan = HumanSwapPlan("Colossification");
    REQUIRE(TurnSolver::HumanSwapDefersToCombat(b.s, plan) == 0);
    {   // control: the pre-rule replay arm (--legacy-main-swap) keeps the main-phase swap
        LegacySwapScope legacy(true);
        CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, plan) == -1);
    }
    std::string label;
    REQUIRE(TurnSolver::DeferHumanAuraSwapToCombat(b.s, plan, &label));
    CHECK(plan.actions.empty());                                     // nothing left for main 1
    CHECK(b.s.scripted_combat_aura_swap == 70);
    CHECK(b.s.scripted_combat_aura_swap_in == colo);
    CHECK(label.find("Colossification") != std::string::npos);
    // Main 1 ends: still payable in combat, so the deferral stands.
    TurnSolver::SettleHumanDeferredSwap(b.s);
    CHECK(b.s.scripted_combat_aura_swap_in == colo);
    // Combat: Mother attacks; the window applies the NAMED Aura -- the human is not asked again.
    int asked = 0;
    DigChooser fail = [&](const GameState&, int, const std::string&, const std::vector<Card>&,
                          const std::vector<int>&, int) { ++asked; return -1; };
    DigChooser* prev = g_play_dig_chooser;
    g_play_dig_chooser = &fail;
    std::vector<int> atk = { BfIndex(b.s, mother) };
    ApplyCombatAuraSwap(b.s, 0, atk);
    g_play_dig_chooser = prev;
    CHECK(asked == 0);
    int colo_host = 0;
    for (const Permanent& p : b.s.battlefield) { if (p.card.m_number == colo) { colo_host = p.aura_attached_to; } }
    CHECK(colo_host == mother);
    CHECK(DefBs("Colossification").params.aura_power_bonus == 20);   // the +20 that swings this combat
    REQUIRE(atk.size() == 1);
    CHECK(b.s.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == mother);   // atk_idx repaired
    CHECK(b.s.scripted_combat_aura_swap == -1);
    CHECK(b.s.scripted_combat_aura_swap_in == -1);
}

TEST_CASE("Human swap timing: a swap only the mana-creature host can pay stays in the main phase")
{
    BoardBs b;
    const int birds = b.Put("Birds of Paradise");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = birds;
    b.s.battlefield.push_back(w);
    b.Put("Azorius Chancery");   // {W}{U}: one short of {2}{U} without the attacking Birds
    b.Hand("Colossification");
    TurnSolver::Plan plan = HumanSwapPlan("Colossification");
    CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, plan) == -1);
    CHECK_FALSE(TurnSolver::DeferHumanAuraSwapToCombat(b.s, plan));
    CHECK(plan.actions.size() == 1);
}

TEST_CASE("Human swap timing: an Aura without the ETB tap, or a host that cannot attack, keeps the main swap")
{
    {
        BoardBs b = WingsOnMother(/*mother_sick=*/false);
        b.Hand("Eldrazi Conscription");
        CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, HumanSwapPlan("Eldrazi Conscription")) == -1);
        CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, HumanSwapPlan("Colossification")) == 0);
    }
    {
        BoardBs b = WingsOnMother(/*mother_sick=*/true);
        CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, HumanSwapPlan("Colossification")) == -1);
    }
}

TEST_CASE("Human swap timing: mana spent later in main 1 moves the deferred swap back into the main phase")
{
    // Wings on Avacyn's Pilgrim; Chancery {W}{U} + Forest pay the {2}{U} without the attacking
    // Pilgrim, so the swap is deferred. A LATER line in the same main phase taps the Forest: the
    // combat window can no longer pay, but the main phase still can (Chancery + the Pilgrim itself)
    // -- so the swap happens there after all, as it would have before the rule.
    BoardBs b;
    const int birds = b.Put("Avacyn's Pilgrim");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = birds;
    b.s.battlefield.push_back(w);
    b.Put("Azorius Chancery");
    b.Put("Forest");
    const int colo = b.Hand("Colossification");
    TurnSolver::Plan plan = HumanSwapPlan("Colossification");
    REQUIRE(TurnSolver::DeferHumanAuraSwapToCombat(b.s, plan));
    for (Permanent& p : b.s.battlefield) { if (p.card.m_name.str() == "Forest") { p.tapped = true; } }
    TurnSolver::SettleHumanDeferredSwap(b.s);
    CHECK(b.s.scripted_combat_aura_swap_in == -1);
    int colo_host = 0;
    for (const Permanent& p : b.s.battlefield) { if (p.card.m_number == colo) { colo_host = p.aura_attached_to; } }
    CHECK(colo_host == birds);   // swapped in the main phase
}

TEST_CASE("Human swap timing: a deferred swap nobody can pay any more is dropped, not left to fail in combat")
{
    BoardBs b = WingsOnMother(/*mother_sick=*/false);
    const int colo = b.s.players[0].hand.back().m_number;
    TurnSolver::Plan plan = HumanSwapPlan("Colossification");
    REQUIRE(TurnSolver::DeferHumanAuraSwapToCombat(b.s, plan));
    // A later line tapped the Forest: Chancery alone is {W}{U}, and Mother is not a mana source.
    for (Permanent& p : b.s.battlefield) { if (p.card.m_name.str() == "Forest") { p.tapped = true; } }
    TurnSolver::SettleHumanDeferredSwap(b.s);
    CHECK(b.s.scripted_combat_aura_swap_in == -1);   // dropped, not left to fail in combat
    bool in_hand = false;
    for (const Card& c : b.s.players[0].hand) { if (c.m_number == colo) { in_hand = true; } }
    CHECK(in_hand);
}

// references/suboptimal/Bruna/claude_s3_gi2 T4 (the user's own game, won T5): Wings on Avacyn's Pilgrim
// (opponent at 18), Birds of Paradise the only blue source, Boseiju + two Remote Farms (a Plains
// here). The goldfish attack default sends the 0-power Birds too -- tapped at declaration (CR 508.1f),
// so its {U} could not pay the combat swap. With the swap pinned for combat the attack heuristic holds
// the Birds home (HoldForCombatAuraSwap): its {U} pays, Pilgrim swings for 1 + 20 = the T4 kill. Human
// play predicts that attack set, so the main-phase swap the user committed is deferred to combat.
TEST_CASE("Human swap timing: claude_s3_gi2 T4 -- the Birds stays home to pay, the swap moves to combat")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = pilgrim;
    b.s.battlefield.push_back(w);
    const int birds = b.Put("Birds of Paradise");
    b.Put("Boseiju, Who Endures");
    b.Put("Plains");                 // (the game's two Remote Farms, depletion counters aside)
    b.Hand("Colossification");
    b.s.players[1].life = 18;
    CHECK(DeclareAttackerIndices(b.s).size() == 2);           // no pin: Pilgrim AND Birds swing
    {
        GameState pinned = b.s;
        pinned.scripted_combat_aura_swap = 70;
        const std::vector<int> atk = DeclareAttackerIndices(pinned);
        REQUIRE(atk.size() == 1);                              // pinned: the Birds is held
        CHECK(pinned.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == pilgrim);
        CHECK(birds != pilgrim);
    }
    TurnSolver::Plan plan = HumanSwapPlan("Colossification");
    CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, plan) == 0);
    // The greedy's own timing rule (every creature that could attack is tapped) stays conservative.
    SolveSwapArm on(true);
    TurnSolver::Plan auton = plan;
    TurnSolver::DeferAuraSwapToCombat(b.s, /*is_pre_combat=*/true, auton);
    CHECK(auton.actions.size() == 1);
}

// CR 508.1f: attacking creatures are tapped as they are declared -- before the Aura-swap window. The
// engine taps them only at combat damage, so an attacking Birds of Paradise used to pay the combat
// swap's {U}. Pilgrim (host) and Birds attack; Boseiju + Plains cannot make {U}: no swap.
TEST_CASE("CR 508.1f: an attacking mana creature cannot pay the in-combat Aura swap")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = pilgrim;
    b.s.battlefield.push_back(w);
    const int birds = b.Put("Birds of Paradise");
    b.Put("Boseiju, Who Endures");
    b.Put("Plains");
    const int colo = b.Hand("Colossification");
    b.s.scripted_combat_aura_swap = 70;
    std::vector<int> atk = { BfIndex(b.s, pilgrim), BfIndex(b.s, birds) };
    ApplyCombatAuraSwap(b.s, 0, atk);
    bool in_hand = false;
    for (const Card& c : b.s.players[0].hand) { if (c.m_number == colo) { in_hand = true; } }
    CHECK(in_hand);                                       // no swap: the {U} source is attacking
    // Control: Birds stays home -> its {U} pays, the swap happens.
    BoardBs c;
    const int p2 = c.Put("Avacyn's Pilgrim");
    Permanent w2 = w; w2.aura_attached_to = p2;
    c.s.battlefield.push_back(w2);
    c.Put("Birds of Paradise");
    c.Put("Boseiju, Who Endures");
    c.Put("Plains");
    const int colo2 = c.Hand("Colossification");
    c.s.scripted_combat_aura_swap = 70;
    std::vector<int> atk2 = { BfIndex(c.s, p2) };
    ApplyCombatAuraSwap(c.s, 0, atk2);
    int host2 = 0;
    for (const Permanent& p : c.s.battlefield) { if (p.card.m_number == colo2) { host2 = p.aura_attached_to; } }
    CHECK(host2 == p2);
}

// The attack hold keeps mana home only for a swap that can HAPPEN: with no hand Aura to bring in, the
// window does nothing, so nothing is held (Bruna d0 s4004 gi360 T7: empty hand, a 40-power Birds held).
TEST_CASE("Attack hold: no hand Aura to swap in -- nothing is held")
{
    BoardBs b;
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = pilgrim;
    b.s.battlefield.push_back(w);
    b.Put("Birds of Paradise");
    b.Put("Boseiju, Who Endures");
    b.Put("Plains");
    GameState pinned = b.s;
    pinned.scripted_combat_aura_swap = 70;
    CHECK(DeclareAttackerIndices(pinned).size() == 2);   // empty hand: Birds swings
    b.Hand("Colossification");
    GameState with_aura = b.s;
    with_aura.scripted_combat_aura_swap = 70;
    CHECK(DeclareAttackerIndices(with_aura).size() == 1); // an Aura to swap in: Birds held for its {U}
}

// The hold keeps the CHEAPEST set of mana creatures that pays the swap, not every creature released on
// the way: two Forests pay the generic, the Birds (2 power under Unflinching Courage) is the only {U};
// the 1-power Pilgrim is not needed and swings.
TEST_CASE("Attack hold: the cheapest sufficient set -- an unneeded Pilgrim still attacks")
{
    BoardBs b;
    const int mother = b.Put("Mother of Runes");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = mother;
    b.s.battlefield.push_back(w);
    const int birds = b.Put("Birds of Paradise");
    Permanent uc;
    uc.card = CardBs("Unflinching Courage", 71); uc.controller_index = 0; uc.owner_index = 0;
    uc.aura_attached_to = birds;
    b.s.battlefield.push_back(uc);
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    b.Put("Forest");
    b.Put("Forest");
    b.Hand("Colossification");
    b.s.scripted_combat_aura_swap = 70;
    bool birds_attacks = false, pilgrim_attacks = false, mother_attacks = false;
    for (int i : DeclareAttackerIndices(b.s))
    {
        const int num = b.s.battlefield[static_cast<std::size_t>(i)].card.m_number;
        birds_attacks |= num == birds; pilgrim_attacks |= num == pilgrim; mother_attacks |= num == mother;
    }
    CHECK(mother_attacks);
    CHECK(pilgrim_attacks);
    CHECK_FALSE(birds_attacks);
}

// ---- THE DUPLICATE-COPY FOLD (USER 2026-10-07: "We should do the duplicate-copy fold") -------------------
// Two halves, both SOUND identity folds, default ON: identical hand copies of one creature Aura are one
// odometer class (MTG_AURA_COPY_FOLD), and identical creature HOSTS keep only the first k of their class
// (MTG_AURA_HOST_FOLD, k = creature-Aura hand slots). Neither may change the plan list the search sees:
// each case compares the folded enumeration with the unfolded one, plan for plan, and Solve's pick.
namespace
{
struct FoldArms
{
    std::int8_t pc, ph;
    explicit FoldArms(bool on) : pc(heurarm::t_arm[heurarm::AURA_COPY_FOLD]), ph(heurarm::t_arm[heurarm::AURA_HOST_FOLD])
    { heurarm::t_arm[heurarm::AURA_COPY_FOLD] = on ? 1 : 0; heurarm::t_arm[heurarm::AURA_HOST_FOLD] = on ? 1 : 0; }
    ~FoldArms() { heurarm::t_arm[heurarm::AURA_COPY_FOLD] = pc; heurarm::t_arm[heurarm::AURA_HOST_FOLD] = ph; }
};
// Every plan as "name>host" per cast, in order (the comparison the byte-identity claim makes).
std::vector<std::string> PlanShapes(const std::vector<TurnSolver::Plan>& plans)
{
    std::vector<std::string> out;
    for (const TurnSolver::Plan& p : plans)
    {
        std::string k;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            k += a.card_name.str() + ">" + std::to_string(a.enchant_target) + ";";
        }
        out.push_back(k);
    }
    return out;
}
}   // namespace

TEST_CASE("Duplicate-copy fold: two identical Aura copies over several hosts -- the same plans, folded or not")
{
    BoardBs b;
    b.Put("Mother of Runes");
    b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    b.Put("Birds of Paradise", /*tapped=*/true);
    for (int k = 0; k < 15; ++k) { b.Put("Forest"); }
    b.Hand("Colossification");
    b.Hand("Colossification");
    b.Hand("Mythic Proportions");
    b.s.players[0].lands_played_this_turn = 1;
    std::vector<std::string> on, off;
    int solve_on = 0, solve_off = 0;
    { FoldArms a(true);  on  = PlanShapes(TurnSolver::EnumerateMainPlans(b.s, true)); solve_on  = SolveHostOf(b.s, "Colossification"); }
    { FoldArms a(false); off = PlanShapes(TurnSolver::EnumerateMainPlans(b.s, true)); solve_off = SolveHostOf(b.s, "Colossification"); }
    CHECK(on.size() > 1);
    CHECK(on == off);
    CHECK(solve_on == solve_off);
}

TEST_CASE("Duplicate-copy fold: three identical hosts, one Aura -- the same plans, and a payer-tappable dork is never folded")
{
    // Three ready Mothers (identical, plain, not mana sources) and one Aura: one host class, k = 1.
    BoardBs b;
    b.Put("Mother of Runes");
    b.Put("Mother of Runes");
    b.Put("Mother of Runes");
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Mythic Proportions");
    b.s.players[0].lands_played_this_turn = 1;
    std::vector<std::string> on, off;
    { FoldArms a(true);  on  = PlanShapes(TurnSolver::EnumerateMainPlans(b.s, true)); }
    { FoldArms a(false); off = PlanShapes(TurnSolver::EnumerateMainPlans(b.s, true)); }
    CHECK(on == off);
    // Two untapped, ready Pilgrims: the real payer taps them by ORDER and never reads the Aura's target, so
    // "the Aura on the Pilgrim that paid" and "on the one that did not" are different lines -- both stay.
    BoardBs d;
    const int p1 = d.Put("Avacyn's Pilgrim");
    const int p2 = d.Put("Avacyn's Pilgrim");
    for (int k = 0; k < 6; ++k) { d.Put("Forest"); }
    d.Hand("Unflinching Courage");
    d.s.players[0].lands_played_this_turn = 1;
    FoldArms a(true);
    BranchArm br;   // every host its own plan, so the enumerated hosts are visible
    bool h1 = false, h2 = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(d.s, true))
    {
        for (const Action& x : p.actions)
        {
            if (x.kind != Action::Kind::CastFromHand || x.card_name.str() != "Unflinching Courage") { continue; }
            h1 = h1 || x.enchant_target == p1;
            h2 = h2 || x.enchant_target == p2;
        }
    }
    CHECK(h1);
    CHECK(h2);
}


// ---- THE USER's "Bruna gets all auras anyway when she attacks" (2026-10-07), as the shipped key prices it ----
// With Bruna ready to attack this turn, a FLAT power Aura is worth the same on any host -- her attack trigger
// gathers it -- so the key TIES across hosts and the choice costs no branching. It is NOT host-free for a
// host-TAPPING Aura (Colossification taps the creature it enters on, before Bruna's attack: never on Bruna) or for
// a BASE SETTER (Almost Perfect sets 9/4: +9 on a 1-power Mother who attacks beside Bruna, +4 on Bruna) -- which is
// why the key keeps the "gather all but the base-setters" variant.
TEST_CASE("Aura host ranking: with Bruna attacking, a flat Aura ties across hosts; a tapping Aura or a base setter does not")
{
    auto key_on = [](const std::string& aura, bool on_bruna)
    {
        BoardBs b;
        const int bruna  = b.Put("Bruna, Light of Alabaster");
        const int mother = b.Put("Mother of Runes");
        Permanent a;
        a.card = CardBs(aura, 77); a.controller_index = 0; a.owner_index = 0; a.entered_this_turn = true;
        a.aura_attached_to = on_bruna ? bruna : mother;
        b.s.battlefield.push_back(a);
        ResolveAuraEnterTapHost(b.s, static_cast<int>(b.s.battlefield.size()) - 1, /*respond_window=*/true);
        return AuraHostBoardKey(b.s, 0, /*attack_window=*/true);
    };
    {   // flat grant: identical either way (Bruna gathers it)
        const AuraHostKey kb = key_on("Mythic Proportions", true), km = key_on("Mythic Proportions", false);
        CHECK(kb.dmg == km.dmg);
        CHECK(kb.now == km.now);
        CHECK_FALSE(AuraHostKeyBetter(kb, km));
        CHECK_FALSE(AuraHostKeyBetter(km, kb));
    }
    {   // Colossification on Bruna taps her: she cannot attack -- the key strictly prefers Mother
        CHECK(AuraHostKeyBetter(key_on("Colossification", false), key_on("Colossification", true)));
    }
    {   // a base setter is worth more on the low-power creature that attacks beside Bruna
        CHECK(AuraHostKeyBetter(key_on("Almost Perfect", false), key_on("Almost Perfect", true)));
    }
}

// ---- ARCANUM WINGS' HOST: the USER's order as the key's last tie-break (MTG_WINGS_HOST_ORDER) -----------------
// Wings grants no power, so the damage key ties across hosts; the USER's order picks the LEAST USEFUL dork --
// Avacyn's Pilgrim ({W}) over Birds of Paradise (any colour) -- where enumeration order picked the Birds.
TEST_CASE("Aura host ranking: Arcanum Wings goes on the least useful dork (Pilgrim before Birds); =0 is the hatch")
{
    BoardBs b;
    const int birds   = b.Put("Birds of Paradise", /*tapped=*/true);
    const int pilgrim = b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    b.Put("Island");
    b.Put("Island");
    b.Put("Forest");
    b.Hand("Arcanum Wings");
    b.s.players[0].lands_played_this_turn = 1;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Arcanum Wings") == pilgrim);
    CHECK(SolveHostOf(b.s, "Arcanum Wings") == pilgrim);
    heurarm::t_arm[heurarm::WINGS_HOST_ORDER] = 0;
    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Arcanum Wings") == birds);
    heurarm::t_arm[heurarm::WINGS_HOST_ORDER] = -1;
}

// USER 2026-10-08: "a hardcast colossification can be put on something with summoning sickness." It cannot
// attack or tap for mana this turn anyway, so the ETB tap costs nothing: with a ready Mother of Runes on the
// board, the plan that casts a Somberwald Sage AND Colossification puts the Aura on the fresh Sage.
TEST_CASE("Aura host ranking: a hardcast Colossification goes on the creature cast this turn, not the ready attacker")
{
    BoardBs b;
    b.Put("Mother of Runes");
    for (int k = 0; k < 10; ++k) { b.Put("Forest"); }
    const int sage = b.Hand("Somberwald Sage");
    b.Hand("Colossification");
    b.s.players[0].lands_played_this_turn = 1;
    bool both = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, true))
    {
        if (!PlanCasts(p, "Somberwald Sage") || !PlanCasts(p, "Colossification")) { continue; }
        for (const Action& a : p.actions)
        { if (a.card_name.str() == "Colossification") { CHECK(a.enchant_target == sage); both = true; } }
    }
    CHECK(both);
}


// Bruna seed 11 T4 (USER 2026-10-08): "Creature fails to attack and win T4 because of the
// Colossification." Wings on an untapped Somberwald Sage (cast T2), Botanical Sanctum + ONE Razorverge
// Thicket on the battlefield, the second Thicket in hand: the human's line plays the Thicket and swaps
// Colossification in. The swap is payable in combat only WITH the plan's own land drop (Sanctum {U} +
// two Thickets; the Sage's mana is creature-only), so the timing check must count the land the plan
// plays first -- it judged the pre-land board, called the swap unpayable and kept it in main, where
// the ETB tap took the Sage out of the T4 kill.
TEST_CASE("Human swap timing: the plan's own land drop pays the combat swap (seed 11 T4)")
{
    BoardBs b;
    const int sage = b.Put("Somberwald Sage");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = sage;
    b.s.battlefield.push_back(w);
    b.Put("Botanical Sanctum");
    b.Put("Razorverge Thicket");
    b.Hand("Colossification");
    b.Hand("Razorverge Thicket");
    b.Hand("Lightning Greaves");
    TurnSolver::Plan with_land = HumanSwapPlan("Colossification");
    with_land.land_decided = true;
    with_land.land_to_play = "Razorverge Thicket";
    CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, with_land) == 0);
    // Control: the same swap with no land drop is NOT payable in combat (two lands + a creature-only
    // dork), so it stays in main.
    TurnSolver::Plan no_land = HumanSwapPlan("Colossification");
    no_land.land_decided = true;
    CHECK(TurnSolver::HumanSwapDefersToCombat(b.s, no_land) == -1);
    // The greedy timing pass (d0 / rollout) judges the same way.
    SolveSwapArm on(true);
    TurnSolver::Plan auton = with_land;
    TurnSolver::DeferAuraSwapToCombat(b.s, /*is_pre_combat=*/true, auton);
    CHECK(auton.actions.empty());
}

// ---- WIDTH (2026-10-10): dominated Greaves moves, combat-pin variants -------------------------
namespace
{
// Per-job lever override for one case (the heurarm slot the batch runner would set), restored on exit.
struct ArmOverride
{
    heurarm::Slot slot;
    ArmOverride(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmOverride() { heurarm::t_arm[slot] = -1; }
};

bool PlanEquipsOnto(const TurnSolver::Plan& p, int host)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::Equip && a.sac_victim_id == host) { return true; } }
    return false;
}

bool PlanTargets(const TurnSolver::Plan& p, int num)
{
    for (const Action& a : p.actions)
    {
        if (a.enchant_target == num) { return true; }
        if (a.kind == Action::Kind::Equip && a.sac_victim_id == num) { return true; }
    }
    return false;
}
}   // namespace

// Moving Greaves from Mother onto an Avacyn's Pilgrim that has been on the battlefield since the turn
// began grants nothing (CR 302.6: haste is moot), shrouds the Pilgrim and unshrouds Mother -- and with
// nothing in the plan targeting Mother, the no-move sibling dominates it. Not offered; the control arm
// (MTG_EQUIP_INERT_FOLD=0) offers it, so the case fails without the fold.
TEST_CASE("Width: an idle Greaves move onto a creature that is not summoning-sick is not offered")
{
    GreavesOnMother g;
    const int pilgrim = g.b.Put("Avacyn's Pilgrim");
    g.b.Put("Forest");
    g.b.Put("Razorverge Thicket");
    g.b.Hand("Glittering Wish");   // something to cast; no creature in hand (it would be the move's best host)
    g.b.s.players[0].lands_played_this_turn = 1;
    bool moved = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true))
    { if (PlanEquipsOnto(p, pilgrim)) { moved = true; } }
    CHECK_MESSAGE(!moved, "an idle Greaves -> Pilgrim move was enumerated");

    ArmOverride off(heurarm::EQUIP_INERT_FOLD, false);
    bool moved_ctrl = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true))
    { if (PlanEquipsOnto(p, pilgrim)) { moved_ctrl = true; } }
    CHECK_MESSAGE(moved_ctrl, "control arm: the move should be offered with the fold off");
}

// The same move is a RELEASE when the plan casts an Aura onto the creature it leaves (Arcanum Wings ->
// Mother needs Mother unshrouded). It must survive there -- and only there.
TEST_CASE("Width: a Greaves release move survives exactly beside the Aura it releases")
{
    GreavesOnMother g;
    const int pilgrim = g.b.Put("Avacyn's Pilgrim");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Razorverge Thicket");
    g.b.Put("Island");
    g.b.Hand("Arcanum Wings");
    g.b.s.players[0].lands_played_this_turn = 1;
    bool release = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true))
    {
        if (!PlanEquipsOnto(p, pilgrim)) { continue; }
        CHECK_MESSAGE(PlanTargets(p, g.mother), "a Greaves -> Pilgrim move with nothing targeting Mother");
        release = true;
    }
    CHECK_MESSAGE(release, "the release move (Greaves -> Pilgrim, Wings -> Mother) was not enumerated");
}

// A summoning-sick creature still gets the Greaves: haste is the whole point there.
TEST_CASE("Width: Greaves onto a summoning-sick creature is still offered")
{
    GreavesOnMother g;
    const int pilgrim = g.b.Put("Avacyn's Pilgrim", /*tapped=*/false, /*sick=*/true);
    g.b.Put("Forest");
    g.b.Put("Razorverge Thicket");
    g.b.s.players[0].lands_played_this_turn = 1;
    bool moved = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g.b.s, /*is_pre_combat=*/true))
    { if (PlanEquipsOnto(p, pilgrim)) { moved = true; } }
    CHECK(moved);
}

// The Arcanum Wings combat-swap pin is read only in THIS turn's combat, so a post-combat plan list
// carries no swap variant (MTG_COMBAT_PIN_PRECOMBAT; the control arm restores the old emission). The
// pre-combat list keeps it.
TEST_CASE("Width: combat-swap pin variants only on a pre-combat plan list")
{
    BoardBs b;
    const int mother = b.Put("Mother of Runes");
    Permanent w;
    w.card = CardBs("Arcanum Wings", 70); w.controller_index = 0; w.owner_index = 0;
    w.aura_attached_to = mother;
    b.s.battlefield.push_back(w);
    b.Put("Island");
    b.Put("Razorverge Thicket");
    b.Put("Forest");
    b.Hand("Colossification");
    b.s.players[0].lands_played_this_turn = 1;
    auto pinned = [&](bool pre) {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, pre))
        { if (p.combat_aura_swap_choice >= 0 || p.bruna_gather_choice >= 0) { ++n; } }
        return n;
    };
    CHECK(pinned(true) > 0);
    CHECK(pinned(false) == 0);
    ArmOverride off(heurarm::COMBAT_PIN_PRECOMBAT, false);
    CHECK(pinned(false) > 0);
}

// The post-apply dedup key must see a pending combat pin, exactly as it sees the other scripted_*
// pins: two states that differ only in it have different combats. MTG_DEDUP_KEY_COMBAT_PINS=0 is the
// old key-hole (the control).
TEST_CASE("Width: the dedup key folds the pending combat pins")
{
    BoardBs b;
    b.Put("Mother of Runes");
    GameState swap = b.s;   swap.scripted_combat_aura_swap = 70;
    GameState gath = b.s;   gath.scripted_bruna_gather = 1;
    CHECK(TurnSolver::DedupKeyOf(swap) != TurnSolver::DedupKeyOf(b.s));
    CHECK(TurnSolver::DedupKeyOf(gath) != TurnSolver::DedupKeyOf(b.s));
    ArmOverride off(heurarm::DEDUP_KEY_COMBAT_PINS, false);
    CHECK(TurnSolver::DedupKeyOf(swap) == TurnSolver::DedupKeyOf(b.s));
    CHECK(TurnSolver::DedupKeyOf(gath) == TurnSolver::DedupKeyOf(b.s));
}

// Bruna's gather pin is THIS combat's: consumed whether or not a gather fires (no Bruna attacked), so
// a pinned variant whose gather never happened leaves combat in its base plan's state.
TEST_CASE("Width: the Bruna gather pin is consumed by combat even when no gather fires")
{
    BoardBs b;
    b.Put("Mother of Runes");
    b.s.scripted_bruna_gather = 1;
    RolloutSimulateCombat(b.s);
    CHECK(b.s.scripted_bruna_gather == -1);
    CHECK(b.s.scripted_combat_aura_swap == -1);
}
