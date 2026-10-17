// Unit tests for Lizard Blades -- "Double strike / Equipped creature has double strike / Reconfigure
// {2}" (CR 702.151) -- and for the shared attach primitive it forced: ApplyEquip now ends in
// RefreshReconfigureCreatures, so EVERY attach (Equip / reconfigure, Sigarda's Aid, Ardenn, and the
// Magnetic Theft / Raubahn attaches that land after this card) turns a reconfigure piece into a
// non-creature, and every detach (unattach, host gone) turns it back.
//
// WHY THESE ARE UNIT TESTS. Each defect here is silent: a Lizard that stays a creature while attached
// attacks for 2 the rules say it cannot; a Hammer that stays on an attached Lizard is +10 on a body
// that is not even a creature; a reconfigure priced at the default equip_cost_generic 0 is a FREE
// double-strike grant the card does not have; a reconfigure that spends Kili's / Forge Anew's
// first-equip waiver costs a Hammer {8}. None of them crashes, diverges between the two worlds (they
// share the bodies), or moves any other deck's digest.
#include <doctest/doctest.h>

#include "ai/Combat.h"
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

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

Permanent& ByNum(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int IndexOf(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == number) { return i; } }
    return -1;
}

int SwingFor(GameState& s, const std::vector<int>& atk_idx)
{
    return ResolveCombatDamage(s, atk_idx, /*exalted_bonus=*/0, /*collect_descs=*/false).total_damage;
}

bool HasSubtype(const Permanent& p, const std::string& st)
{
    for (const std::string& x : p.card.m_subtypes) { if (x == st) { return true; } }
    return false;
}

Action EquipAct(const std::string& name, int src, int host, int generic)
{
    Action a;
    a.kind          = Action::Kind::Equip;
    a.card_name     = name;
    a.def           = &Def(name);
    a.sac_source_id = src;
    a.sac_victim_id = host;
    a.cost          = ManaCost{};
    a.cost.generic  = generic;
    return a;
}

// Does any enumerated plan carry an action matching `pred`?
template <typename Pred>
bool AnyPlanAction(const std::vector<TurnSolver::Plan>& plans, Pred pred)
{
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (pred(a)) { return true; } } }
    return false;
}

}   // namespace

// ---- 1-2. a creature, then an Equipment that is not a creature -----------------------------------

TEST_CASE("Lizard Blades: a loose Lizard is a 1/1 double-striking creature that attacks")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    REQUIRE(ByNum(s, lizard).card.IsCreature());
    CHECK(CreatureHasDoubleStrike(ByNum(s, lizard), s));
    CHECK(CanAttackFull(ByNum(s, lizard), s.battlefield, 0));
    CHECK(SwingFor(s, { IndexOf(s, lizard), IndexOf(s, ardenn) }) == 2 + 2);
}

TEST_CASE("Lizard Blades: attached (through ApplyEquip) it is NOT a creature, loses Lizard, and grants double strike")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    REQUIRE(CanAttachEquip(s, 0, lizard, ardenn));
    ApplyEquip(s, 0, lizard, ardenn);
    const Permanent& lz = ByNum(s, lizard);
    CHECK(lz.equipped_to == ardenn);
    CHECK_FALSE(lz.card.IsCreature());
    CHECK(lz.card.HasType(CardType::Artifact));
    CHECK(HasSubtype(lz, "Equipment"));
    CHECK_FALSE(HasSubtype(lz, "Lizard"));           // "loses any creature subtypes it had"
    CHECK_FALSE(CanAttackFull(lz, s.battlefield, 0));
    CHECK(CreatureHasDoubleStrike(ByNum(s, ardenn), s));
    // The shared attack declaration leaves the piece out; Ardenn swings 2 x 2.
    const std::vector<int> atk = DeclareAttackerIndices(s);
    for (int i : atk) { CHECK(s.battlefield[static_cast<std::size_t>(i)].card.m_number != lizard); }
    CHECK(SwingFor(s, { IndexOf(s, ardenn) }) == 4);
}

// ---- 3. never onto itself ---------------------------------------------------------------------

TEST_CASE("Lizard Blades: an attach onto itself is refused and changes nothing (CR 301.5c)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    CHECK_FALSE(CanAttachEquip(s, 0, lizard, lizard));
    ApplyEquip(s, 0, lizard, lizard);
    CHECK(ByNum(s, lizard).equipped_to == 0);
    CHECK(ByNum(s, lizard).card.IsCreature());
}

// ---- 4. Equipment on the Lizard falls off when it stops being a creature (CR 704.5n) -------------

TEST_CASE("Lizard Blades: Hammer on a creature Lizard is 22; reconfiguring the Lizard drops the Hammer")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    const int hammer = Put(s, "Colossus Hammer", 3);
    ApplyEquip(s, 0, hammer, lizard);
    REQUIRE(ByNum(s, hammer).equipped_to == lizard);
    CHECK(SwingFor(s, { IndexOf(s, lizard) }) == (1 + 10) * 2);
    ApplyEquip(s, 0, lizard, ardenn);
    CHECK(ByNum(s, hammer).equipped_to == 0);
    CHECK_FALSE(ByNum(s, lizard).card.IsCreature());
    // ...and the Hammer can now go on the double-striking Ardenn: (2 + 10) x 2.
    ApplyEquip(s, 0, hammer, ardenn);
    CHECK(SwingFor(s, { IndexOf(s, ardenn) }) == (2 + 10) * 2);
}

// ---- 5. host gone -> a creature again, sick only if it came under our control this turn ----------

TEST_CASE("Lizard Blades: when the host dies it is a creature again, and attacks unless it entered this turn")
{
    EnsureCardsLoaded();
    for (const bool sick : { false, true })
    {
        CAPTURE(sick);
        GameState s = Fresh();
        const int lizard = Put(s, "Lizard Blades", 1, sick);
        const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        ApplyEquip(s, 0, lizard, ardenn);
        REQUIRE_FALSE(ByNum(s, lizard).card.IsCreature());
        SacrificePermanentAt(s, 0, IndexOf(s, ardenn));   // the real death path
        const Permanent& lz = ByNum(s, lizard);
        CHECK(lz.equipped_to == 0);
        CHECK(lz.card.IsCreature());
        CHECK(HasSubtype(lz, "Lizard"));
        CHECK(CanAttackFull(lz, s.battlefield, 0) == !sick);
    }
}

TEST_CASE("Lizard Blades: a host that left by a path that forgot to unattach is caught by the refresh")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    ApplyEquip(s, 0, lizard, ardenn);
    s.battlefield.erase(s.battlefield.begin() + IndexOf(s, ardenn));   // a raw removal
    RefreshReconfigureCreatures(s);
    CHECK(ByNum(s, lizard).equipped_to == 0);
    CHECK(ByNum(s, lizard).card.IsCreature());
}

// ---- 6. Kor Duelist reads it as Equipment --------------------------------------------------------

TEST_CASE("Lizard Blades: Kor Duelist wearing only the Lizard double-strikes")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard  = Put(s, "Lizard Blades", 1);
    const int duelist = Put(s, "Kor Duelist", 2);
    ApplyEquip(s, 0, lizard, duelist);
    CHECK(HasDoubleStrikeFromEquipment(ByNum(s, duelist), s));
    CHECK(SwingFor(s, { IndexOf(s, duelist) }) == 2);
}

TEST_CASE("Lizard Blades: the double-strike grant walk is gated by the deck stamp")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    ApplyEquip(s, 0, lizard, ardenn);
    REQUIRE(EquipmentGrantsDoubleStrikeTo(ByNum(s, ardenn), s));
    s.deck_has_equip_ds_grant = false;    // what StampDeckTraits writes for every other deck
    CHECK_FALSE(EquipmentGrantsDoubleStrikeTo(ByNum(s, ardenn), s));
}

// ---- 7. the cost, and "not an equip ability" -----------------------------------------------------

TEST_CASE("Lizard Blades: reconfigure is priced {2} -- never the phantom free equip -- and Puresteel's equip {0} applies")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    CHECK(EquipCostGenericNow(s, 0, Def("Lizard Blades"), ardenn) == 2);
    CHECK(IsReconfigureAttach(s, 0, lizard));
    // Metalcraft live (Puresteel + 3 artifacts: the Lizard, two Bone Saws): the piece GAINS a real
    // equip {0} ability (CR 301.5c lets a reconfigure Equipment creature equip), so the attach is
    // that equip ability -- free, and an equip activation.
    Put(s, "Puresteel Paladin", 3);
    Put(s, "Bone Saw", 4);
    Put(s, "Bone Saw", 5);
    REQUIRE(MetalcraftEquipZeroLive(s, 0));
    CHECK(EquipCostGenericNow(s, 0, Def("Lizard Blades"), ardenn) == 0);
    CHECK_FALSE(IsReconfigureAttach(s, 0, lizard));
}

TEST_CASE("Lizard Blades: Kili's first-equip waiver is neither given to nor spent by a reconfigure")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    Put(s, "Kíli the Resourceful", 3);
    const int hammer = Put(s, "Colossus Hammer", 4);
    s.players[0].has_enduring_story = true;
    const Action reconf = EquipAct("Lizard Blades", lizard, ardenn, 2);
    const Action ham    = EquipAct("Colossus Hammer", hammer, ardenn, 8);
    CHECK(TurnSolver::StoriedWaivedEquipId(s, { reconf }) == 0);
    CHECK(TurnSolver::StoriedWaivedEquipId(s, { reconf, ham }) == hammer);

    // Applied through the rollout's plan apply (the path claude-play and the viewer use too): the
    // reconfigure pays {2} and leaves the turn's first equip unspent; a real equip spends it.
    for (int k = 0; k < 2; ++k) { Put(s, "Mountain", 50 + k); }
    TurnSolver::Plan p;
    p.actions = { reconf };
    p.value   = 1;
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    CHECK(ByNum(t, lizard).equipped_to == ardenn);
    CHECK_FALSE(t.players[0].equip_activated_this_turn);
    CHECK(ByNum(t, 50).tapped);
    CHECK(ByNum(t, 51).tapped);
}

// ---- 8. the unattach half ------------------------------------------------------------------------

TEST_CASE("Lizard Blades: unattach is live only while attached, and makes it a creature again")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, lizard, PermAbilityMode::ReconfigureUnattach));
    ApplyEquip(s, 0, lizard, ardenn);
    CHECK(PermAbilitySourceLive(s, 0, lizard, PermAbilityMode::ReconfigureUnattach));
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::ReconfigureUnattach));
    ApplyPermAbility(s, 0, lizard, PermAbilityMode::ReconfigureUnattach);
    CHECK(ByNum(s, lizard).equipped_to == 0);
    CHECK(ByNum(s, lizard).card.IsCreature());
    CHECK_FALSE(CreatureHasDoubleStrike(ByNum(s, ardenn), s));
}

TEST_CASE("Lizard Blades: the enumerator offers reconfigure at {2} (not free) and unattach only while attached")
{
    EnsureCardsLoaded();
    auto is_reconf = [](int lizard, int host)
    {
        return [=](const Action& a)
        { return a.kind == Action::Kind::Equip && a.sac_source_id == lizard && a.sac_victim_id == host; };
    };
    auto is_unattach = [](int lizard)
    {
        return [=](const Action& a)
        {
            return a.kind == Action::Kind::ActivatePermAbility && a.sac_source_id == lizard
                && a.ability_mode == Action::AbilityMode::ReconfigureUnattach;
        };
    };
    // One Mountain: {2} is unaffordable, so no plan reconfigures (the default equip_cost_generic 0
    // would have offered it for free).
    {
        GameState s = Fresh();
        const int lizard = Put(s, "Lizard Blades", 1);
        const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        Put(s, "Mountain", 50);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK_FALSE(AnyPlanAction(plans, is_reconf(lizard, ardenn)));
        CHECK_FALSE(AnyPlanAction(plans, is_unattach(lizard)));
    }
    // Two Mountains: offered, at {2}; still no unattach (it is not attached).
    {
        GameState s = Fresh();
        const int lizard = Put(s, "Lizard Blades", 1);
        const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        Put(s, "Mountain", 50);
        Put(s, "Mountain", 51);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(AnyPlanAction(plans, [&](const Action& a)
              { return is_reconf(lizard, ardenn)(a) && a.cost.generic == 2; }));
        CHECK_FALSE(AnyPlanAction(plans, is_unattach(lizard)));
    }
    // Attached to Ardenn, a Kor Duelist beside it, two Mountains: unattach AND the move are offered,
    // and never together in one plan (one activation family).
    {
        GameState s = Fresh();
        const int lizard  = Put(s, "Lizard Blades", 1);
        const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        const int duelist = Put(s, "Kor Duelist", 3);
        Put(s, "Mountain", 50);
        Put(s, "Mountain", 51);
        ApplyEquip(s, 0, lizard, ardenn);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(AnyPlanAction(plans, is_unattach(lizard)));
        CHECK(AnyPlanAction(plans, is_reconf(lizard, duelist)));
        for (const TurnSolver::Plan& p : plans)
        {
            int n = 0;
            for (const Action& a : p.actions) { if (a.sac_source_id == lizard) { ++n; } }
            CHECK(n <= 1);
        }
    }
}

// ---- 9. the begin-combat planner (Ardenn) and Sigarda's Aid --------------------------------------

TEST_CASE("Lizard Blades: Ardenn's begin-combat attach never self-attaches and matches its projection")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    Put(s, "Ardenn, Intrepid Archaeologist", 2);
    const int hammer = Put(s, "Colossus Hammer", 3);
    // Hammer onto the double-striking Lizard (1+10)x2 + Ardenn 2 = 24, or Hammer + Lizard onto Ardenn
    // (2+10)x2 = 24 with the Lizard no longer attacking: a tie, which the planner breaks to FEWER
    // moves. Either way the Lizard's own attack term is priced, and it is never attached to itself.
    REQUIRE(BeginCombatProjectedDamage(s, 0) == 2 + 2);
    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(ByNum(s, lizard).equipped_to != lizard);
    CHECK(ByNum(s, hammer).equipped_to != 0);
    CHECK(BeginCombatProjectedDamage(s, 0) == 24);
}

TEST_CASE("Lizard Blades: Ardenn does not move the Lizard onto a host it adds nothing to")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    Put(s, "Ardenn, Intrepid Archaeologist", 2);
    Put(s, "Swiftblade Vindicator", 3);
    // Swiftblade already double-strikes: Lizard onto it is +0 for the host and -2 for the Lizard.
    // Lizard onto Ardenn is +2 -2 = 0. Neither move gains, so nothing moves.
    const int before = BeginCombatProjectedDamage(s, 0);
    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(ByNum(s, lizard).equipped_to == 0);
    CHECK(ByNum(s, lizard).card.IsCreature());
    CHECK(BeginCombatProjectedDamage(s, 0) == before);
}

TEST_CASE("Lizard Blades: Sigarda's Aid declines a host that gains nothing, and attaches to one that does")
{
    EnsureCardsLoaded();
    const DecisionProvider& prov = DefaultProvider();
    {
        GameState s = Fresh();
        Put(s, "Lizard Blades", 1, /*sick=*/true);
        Put(s, "Swiftblade Vindicator", 3);
        const Card lz = ByNum(s, 1).card;
        CHECK(prov.EquipmentEtbAttachHost(s, 0, lz, { IndexOf(s, 3) }) == 0);   // decline: keep the body
    }
    {
        GameState s = Fresh();
        Put(s, "Lizard Blades", 1, /*sick=*/true);
        Put(s, "Ardenn, Intrepid Archaeologist", 2);
        const Card lz = ByNum(s, 1).card;
        CHECK(prov.EquipmentEtbAttachHost(s, 0, lz, { IndexOf(s, 2) }) == 2);
    }
    // A non-reconfigure Equipment keeps the old always-attach default (byte-identical elsewhere).
    {
        GameState s = Fresh();
        Put(s, "Bone Saw", 1);
        Put(s, "Swiftblade Vindicator", 3);
        const Card saw = ByNum(s, 1).card;
        CHECK(prov.EquipmentEtbAttachHost(s, 0, saw, { IndexOf(s, 3) }) == 3);
    }
}
