// Unit cover for the Bruna Stage 5d claude-play sweep findings (2026-10-05, seeds 77001-77020).
// Each case pins one confirmed defect from the sweep and is written so that it FAILS on the pre-fix
// tree (the control): the sweep ledger (docs/design/analysis-Bruna.md, "## Claude-play sweep") names
// the fix commit for each.
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
    { if (p.card.m_number == farm) { p.counters.push_back(Counter{ Counter::Type::Depletion, 0 }); } }
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
