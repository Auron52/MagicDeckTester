// Unit cover for the Bruna Stage 5d claude-play sweep findings (2026-10-05, seeds 77001-77020).
// Each case pins one confirmed defect from the sweep and is written so that it FAILS on the pre-fix
// tree (the control): the sweep ledger (docs/design/analysis-Bruna.md, "## Claude-play sweep") names
// the fix commit for each.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
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

// The autonomous plan dedup used to key a creature Aura by NAME only, so the search saw ONE host per
// Aura (the first enumerated) -- which creature carries Mythic Proportions was never searched.
TEST_CASE("Bruna sweep B: a creature Aura's host is a searched axis (one plan per legal host)")
{
    BoardBs b;
    const int mother  = b.Put("Mother of Runes");
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    for (int k = 0; k < 7; ++k) { b.Put("Forest"); }
    b.Hand("Mythic Proportions");
    b.s.players[0].lands_played_this_turn = 1;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    bool on_mother = false, on_pilgrim = false;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.enchant_target == mother)  { on_mother = true; }
            if (a.enchant_target == pilgrim) { on_pilgrim = true; }
        }
    }
    CHECK(on_mother);
    CHECK(on_pilgrim);
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
