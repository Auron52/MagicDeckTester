// Unit tests for Forge Anew -- {2}{W} Enchantment: "When this enchantment enters, return target
// Equipment card from your graveyard to the battlefield. / During your turn, you may activate equip
// abilities any time you could cast an instant. / You may pay {0} rather than pay the equip cost of the
// first equip ability you activate during each of your turns." (CardParams::etb_return_equipment_from_gy,
// first_equip_free; clause 2 is a PROVISIONAL deferral -- see the card's bracket note.)
//
// WHY THESE ARE UNIT TESTS. Every defect here is silent:
//   * the scalar mana bound charged a Colossus Hammer equip its printed {8} BEFORE the waiver credit
//     could price it, so without Puresteel the free Hammer equip was never offered -- to the search or
//     to the human (the viewer lists exactly the enumerated plans);
//   * a reconfigure (Lizard Blades) or a Magnetic Theft that took / spent the waiver would cost the
//     turn's Hammer equip {8};
//   * a rollout that ignored the cast's searched graveyard pin would score the provider's pick while
//     the real game returned the searched one;
//   * a returned piece with no breakpoint could not be equipped in the phase it returned, at any budget.
// None of them crashes or moves any other deck's digest.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/Dominance.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <set>
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

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

// A graveyard card is a NAME-ONLY placeholder (empty type masks -- DeckLoader::MakePlaceholder's
// shape), so every Equipment test on it must go through the definition (ZoneCard).
int Gy(GameState& s, const std::string& name, int number)
{
    Card c;
    c.m_name   = CardDatabase::Instance().CanonicalName(name);
    c.RehashName();
    c.m_number = number;
    s.players[0].graveyard.push_back(c);
    return number;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int IndexOf(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == number) { return i; } }
    return -1;
}

bool InGraveyard(const GameState& s, int number)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

int CountTappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
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

Action TheftAct(int eq, int host)
{
    Action a;
    a.kind            = Action::Kind::CastFromHand;
    a.card_name       = std::string("Magnetic Theft");
    a.def             = &Def("Magnetic Theft");
    a.hand_index      = -1;
    a.cost            = Def("Magnetic Theft").card.m_mana_cost;
    a.is_noncreature  = true;
    a.attach_equip_id = eq;
    a.enchant_target  = host;
    return a;
}

template <typename Pred>
bool AnyPlan(const std::vector<TurnSolver::Plan>& plans, Pred pred)
{
    for (const TurnSolver::Plan& p : plans) { if (pred(p)) { return true; } }
    return false;
}

bool HasAction(const TurnSolver::Plan& p, Action::Kind k, const std::string& name, int src = -1, int host = -1)
{
    for (const Action& a : p.actions)
    {
        if (a.kind != k || a.card_name.str() != name) { continue; }
        if (src >= 0 && a.sac_source_id != src)  { continue; }
        if (host >= 0 && a.sac_victim_id != host) { continue; }
        return true;
    }
    return false;
}

// GameEngine::MainPhase's shape: every cast resolves before the next.
void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

}   // namespace

// ---- 1-2. the ETB: pinned, unpinned, nothing legal, Lizard Blades ---------------------------------

TEST_CASE("Forge Anew ETB: a pinned return puts THAT Equipment onto the battlefield, unattached")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer = Gy(s, "Colossus Hammer", 10);
    const int lox    = Gy(s, "Loxodon Warhammer", 11);
    const int forge  = Put(s, "Forge Anew", 1, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, IndexOf(s, forge), "Loxodon Warhammer", -1);
    const Permanent* w = FindNum(s, lox);
    REQUIRE(w != nullptr);
    CHECK(w->card.m_name.str() == "Loxodon Warhammer");
    CHECK(w->equipped_to == 0);
    CHECK(w->entered_this_turn);
    CHECK(w->controller_index == 0);
    CHECK(InGraveyard(s, hammer));
    CHECK_FALSE(InGraveyard(s, lox));
}

TEST_CASE("Forge Anew ETB: unpinned takes the provider's pick; nothing legal moves nothing; Lizard Blades returns as a creature")
{
    EnsureCardsLoaded();
    {   // Unpinned: the largest power bonus -> the Hammer.
        GameState s = Fresh();
        Gy(s, "Loxodon Warhammer", 11);
        const int hammer = Gy(s, "Colossus Hammer", 10);
        const int forge  = Put(s, "Forge Anew", 1, true);
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
        CHECK(FindNum(s, hammer) != nullptr);
        CHECK(s.players[0].graveyard.size() == 1);
    }
    {   // No Equipment card in the graveyard (CR 603.3d): the trigger is removed, nothing moves.
        GameState s = Fresh();
        Gy(s, "Plains", 12);
        Gy(s, "Path to Exile", 13);
        const int forge = Put(s, "Forge Anew", 1, true);
        const std::size_t bf = s.battlefield.size();
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
        CHECK(s.battlefield.size() == bf);
        CHECK(s.players[0].graveyard.size() == 2);
    }
    {   // A pin naming a non-Equipment card is not a legal target: the provider's pick instead.
        GameState s = Fresh();
        Gy(s, "Plains", 12);
        const int lox   = Gy(s, "Loxodon Warhammer", 11);
        const int forge = Put(s, "Forge Anew", 1, true);
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), "Plains", -1);
        CHECK(FindNum(s, lox) != nullptr);
        CHECK(InGraveyard(s, 12));
    }
    {   // Lizard Blades is an Equipment CARD: it returns, unattached, as a (summoning-sick) creature.
        GameState s = Fresh();
        const int lizard = Gy(s, "Lizard Blades", 14);
        const int forge  = Put(s, "Forge Anew", 1, true);
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), "Lizard Blades", -1);
        const Permanent* lz = FindNum(s, lizard);
        REQUIRE(lz != nullptr);
        CHECK(lz->card.IsCreature());
        CHECK(lz->equipped_to == 0);
        CHECK(lz->entered_this_turn);
    }
}

// ---- 3. the full enter cascade: Sigarda's Aid attaches the returned piece --------------------------

TEST_CASE("Forge Anew ETB: with Sigarda's Aid out, the returned Hammer enters attached to the Kor Duelist")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Gy(s, "Colossus Hammer", 10);
    Put(s, "Sigarda's Aid", 2);
    const int duelist = Put(s, "Kor Duelist", 3);
    const int forge   = Put(s, "Forge Anew", 1, true);
    FireOwnEtbTriggers(s, 0, IndexOf(s, forge), "Colossus Hammer", -1);
    const Permanent* h = FindNum(s, hammer);
    REQUIRE(h != nullptr);
    CHECK(h->equipped_to == duelist);
    CHECK_FALSE(s.players[0].equip_activated_this_turn);   // an attach, not an equip activation
}

// ---- 4. the payment id: the shared predicate, max cost, reconfigure excluded -----------------------

TEST_CASE("Forge Anew waiver: the plan's most expensive equip is the free one, once per turn")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lox     = Put(s, "Loxodon Warhammer", 11);
    const int hammer  = Put(s, "Colossus Hammer", 10);
    const int dragon  = Put(s, "Dragonfire Blade", 12);
    const int duelist = Put(s, "Kor Duelist", 3);
    const int swift   = Put(s, "Swiftblade Vindicator", 4);
    const std::vector<Action> acts = { EquipAct("Loxodon Warhammer", lox, duelist, 3),
                                       EquipAct("Colossus Hammer", hammer, duelist, 8) };
    CHECK(TurnSolver::StoriedWaivedEquipId(s, acts) == 0);                 // no waiver out
    CHECK_FALSE(FirstEquipWaiverOnBattlefield(s, 0));
    Put(s, "Forge Anew", 1);
    CHECK(FirstEquipWaiverOnBattlefield(s, 0));
    CHECK(TurnSolver::StoriedWaivedEquipId(s, acts) == hammer);
    // StoriedWaivedCost: {0} for the waived id, the LIVE cost otherwise (Dragonfire Blade's per-colour
    // reduction onto the two-colour Swiftblade: {4} - 2 = {2}).
    CHECK(StoriedWaivedCost(s, 0, hammer, ManaCost{}, duelist, hammer).ManaValue() == 0);
    CHECK(StoriedWaivedCost(s, 0, dragon, ManaCost{}, swift, hammer).ManaValue() == 2);
    s.players[0].equip_activated_this_turn = true;                          // the first is spent
    CHECK_FALSE(FirstEquipWaiverOnBattlefield(s, 0));
    CHECK(TurnSolver::StoriedWaivedEquipId(s, acts) == 0);
    // A second waiver source (Kili with the story) still gives no second free equip.
    Put(s, "Kíli the Resourceful", 5);
    s.players[0].has_enduring_story = true;
    CHECK(TurnSolver::StoriedWaivedEquipId(s, acts) == 0);
}

TEST_CASE("Forge Anew waiver: a reconfigure is neither waived nor spends it; a real equip after it is free")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 1);
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 2);
    const int hammer = Put(s, "Colossus Hammer", 4);
    Put(s, "Forge Anew", 3);
    const Action reconf = EquipAct("Lizard Blades", lizard, ardenn, 2);
    const Action ham    = EquipAct("Colossus Hammer", hammer, ardenn, 8);
    CHECK(TurnSolver::StoriedWaivedEquipId(s, { reconf }) == 0);
    CHECK(TurnSolver::StoriedWaivedEquipId(s, { reconf, ham }) == hammer);
    Put(s, "Mountain", 50);
    Put(s, "Mountain", 51);
    // Reconfigure alone: pays its {2}, leaves the turn's first equip unspent.
    {
        TurnSolver::Plan p;
        p.actions = { reconf };
        p.value   = 1;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK(FindNum(t, lizard)->equipped_to == ardenn);
        CHECK_FALSE(t.players[0].equip_activated_this_turn);
        CHECK(CountTappedLands(t) == 2);
        CHECK(FirstEquipWaiverOnBattlefield(t, 0));
    }
    // Reconfigure + the Hammer equip: {2} + {0} on two Mountains -- the Hammer took the waiver.
    {
        TurnSolver::Plan p;
        p.actions = { reconf, ham };
        p.value   = 1;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK(FindNum(t, lizard)->equipped_to == ardenn);
        CHECK(FindNum(t, hammer)->equipped_to == ardenn);
        CHECK(t.players[0].equip_activated_this_turn);
        CHECK(CountTappedLands(t) == 2);
    }
}

TEST_CASE("Forge Anew waiver: a Magnetic Theft attach does not spend it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 10);
    const int duelist = Put(s, "Kor Duelist", 3);
    Put(s, "Forge Anew", 1);
    Put(s, "Mountain", 50);
    Hand(s, "Magnetic Theft", 60);
    TurnSolver::Plan p;
    p.actions = { TheftAct(hammer, duelist) };
    p.value   = 1;
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
    CHECK(FindNum(s, hammer)->equipped_to == duelist);
    CHECK_FALSE(s.players[0].equip_activated_this_turn);
    CHECK(FirstEquipWaiverOnBattlefield(s, 0));
}

// ---- 5. enumeration: the bound addend (fix F) -------------------------------------------------------

TEST_CASE("Forge Anew waiver: on THREE lands the free Hammer equip is enumerated (the bound no longer cuts it)")
{
    EnsureCardsLoaded();
    auto board = [](bool forge, bool kili)
    {
        GameState s = Fresh();
        Put(s, "Colossus Hammer", 10);
        Put(s, "Kor Duelist", 3);
        if (forge) { Put(s, "Forge Anew", 1); }
        if (kili)  { Put(s, "Kíli the Resourceful", 5); s.players[0].has_enduring_story = true; }
        for (int k = 0; k < 3; ++k) { Put(s, "Plains", 50 + k); }
        return s;
    };
    auto equips_hammer = [](const TurnSolver::Plan& p)
    { return HasAction(p, Action::Kind::Equip, "Colossus Hammer", 10, 3); };
    // Control: no waiver -> {8} on three lands is unpayable.
    CHECK_FALSE(AnyPlan(TurnSolver::EnumerateMainPlans(board(false, false), true), equips_hammer));
    // Forge Anew: offered.
    CHECK(AnyPlan(TurnSolver::EnumerateMainPlans(board(true, false), true), equips_hammer));
    // Kili with the story: the same hole, fixed by the same addend.
    CHECK(AnyPlan(TurnSolver::EnumerateMainPlans(board(false, true), true), equips_hammer));
    // ...and the GREEDY LEAF (Solve, the beyond-horizon playout) sees the free equip too -- its own
    // battlefield-only credit + bound addend; without them every playout priced the Hammer at {8}.
    const TurnSolver::GreedyPermit leaf(TurnSolver::GreedySite::HorizonLeaf, 0);
    CHECK(equips_hammer(TurnSolver::Solve(board(true, false), /*is_pre_combat=*/true, leaf)));
    CHECK_FALSE(equips_hammer(TurnSolver::Solve(board(false, false), /*is_pre_combat=*/true, leaf)));
}

// ---- 6. the same-turn line: cast Forge Anew, then the free Hammer equip ----------------------------

TEST_CASE("Forge Anew waiver: a SAME-PLAN Forge Anew cast makes the Hammer equip free (4 lands: cast + equip)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 10);
    const int duelist = Put(s, "Kor Duelist", 3);
    Hand(s, "Forge Anew", 1);
    for (int k = 0; k < 4; ++k) { Put(s, "Plains", 50 + k); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (HasAction(p, Action::Kind::CastFromHand, "Forge Anew")
            && HasAction(p, Action::Kind::Equip, "Colossus Hammer", hammer, duelist)) { line = &p; break; }
    }
    REQUIRE(line != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *line, /*is_pre_combat=*/true);
    CHECK(FindNum(t, hammer)->equipped_to == duelist);
    CHECK(CountTappedLands(t) == 3);                       // {2}{W} for Forge, {0} for the Hammer
    CHECK(t.players[0].equip_activated_this_turn);
    // The plan JSON's display helper prices it the same way (credit_plan_casts).
    CHECK(TurnSolver::StoriedWaivedEquipId(s, line->actions, /*credit_plan_casts=*/true) == hammer);
    CHECK(TurnSolver::StoriedWaivedEquipId(s, line->actions) == 0);   // payment sites: not yet cast
}

// ---- 7-8. the searched graveyard pick: cast variants, and the rollout carries the pin (fix N) -------

TEST_CASE("Forge Anew cast: one variant per distinct graveyard EQUIPMENT name, plus the unpinned base")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Hand(s, "Forge Anew", 1);
    Gy(s, "Colossus Hammer", 10);
    Gy(s, "Colossus Hammer", 11);
    Gy(s, "Loxodon Warhammer", 12);
    Gy(s, "Plains", 13);
    for (int k = 0; k < 3; ++k) { Put(s, "Plains", 50 + k); }
    std::set<std::string> pins;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        { if (a.card_name.str() == "Forge Anew") { pins.insert(a.tutor_target.str()); } }
    }
    CHECK(pins == std::set<std::string>{ "", "Colossus Hammer", "Loxodon Warhammer" });
}

TEST_CASE("Forge Anew cast: the rollout and the executor both return the PINNED Equipment, not the provider's")
{
    EnsureCardsLoaded();
    auto board = []()
    {
        GameState s = Fresh();
        Hand(s, "Forge Anew", 1);
        Gy(s, "Colossus Hammer", 10);
        Gy(s, "Loxodon Warhammer", 12);
        for (int k = 0; k < 3; ++k) { Put(s, "Plains", 50 + k); }
        return s;
    };
    const GameState s = board();
    const TurnSolver::Plan* line = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.card_name.str() == "Forge Anew" && a.tutor_target.str() == "Loxodon Warhammer") { line = &p; break; }
    }
    REQUIRE(line != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *line, /*is_pre_combat=*/true);
    CHECK(FindNum(roll, 12) != nullptr);     // the Warhammer -- the provider would take the Hammer
    CHECK(FindNum(roll, 10) == nullptr);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    CHECK(FindNum(exec, 12) != nullptr);
    CHECK(FindNum(exec, 10) == nullptr);
    CHECK(roll.players[0].graveyard.size() == exec.players[0].graveyard.size());
}

// ---- 9. the graveyard reader: stamped, and folded into the dominance key (fix O) --------------------

TEST_CASE("Forge Anew graveyard reader: a Forge Anew deck stamps GyR_EquipmentNames, and it separates graveyards")
{
    EnsureCardsLoaded();
    Decklist with_card;
    with_card.mainboard.push_back(Def("Forge Anew").card);
    with_card.mainboard.push_back(Def("Colossus Hammer").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(with_card) & GyR_EquipmentNames) != 0);
    Decklist without;
    without.mainboard.push_back(Def("Colossus Hammer").card);
    without.mainboard.push_back(Def("Kor Duelist").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(without) & GyR_EquipmentNames) == 0);

    auto snap = [](const std::string& gy_card, std::uint32_t readers)
    {
        GameState s = Fresh();
        s.step  = Step::Untap;
        s.deck_gy_readers = readers;
        Gy(s, gy_card, 10);
        return dominance::Build(s, DefaultProvider());
    };
    const dominance::DomSnap a = snap("Colossus Hammer", GyR_EquipmentNames);
    const dominance::DomSnap b = snap("Loxodon Warhammer", GyR_EquipmentNames);
    REQUIRE(a.comparable);
    REQUIRE(b.comparable);
    CHECK(a.exact != b.exact);
    // ...and with no reader the graveyard is invisible to the comparison (documents the gate).
    CHECK(snap("Colossus Hammer", GyR_None).exact == snap("Loxodon Warhammer", GyR_None).exact);
}

// ---- 10. the breakpoint (site 10): the returned piece is equipped in the same phase ----------------

TEST_CASE("Forge Anew breakpoint: the graveyard-Equipment window and its deck stamp")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Gy(s, "Colossus Hammer", 10);
    Gy(s, "Plains", 13);
    const std::vector<int> snap = TurnSolver::GraveyardEquipmentNumbers(s);
    CHECK(snap == std::vector<int>{ 10 });
    CHECK_FALSE(TurnSolver::EquipmentReturnedFromGraveyard(snap, s));
    const int forge = Put(s, "Forge Anew", 1, true);
    FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
    CHECK(TurnSolver::EquipmentReturnedFromGraveyard(snap, s));
    // A deck the stamp proves has no graveyard-Equipment return takes no snapshot at all.
    GameState t = Fresh();
    Gy(t, "Colossus Hammer", 10);
    Decklist none;
    none.mainboard.push_back(Def("Colossus Hammer").card);
    GoldFishRunner::StampDeckTraits(t, none);
    CHECK_FALSE(t.deck_has_gy_equipment_return);
    CHECK_FALSE(t.deck_has_first_equip_waiver);
    CHECK(TurnSolver::GraveyardEquipmentNumbers(t).empty());
    Decklist with_card;
    with_card.mainboard.push_back(Def("Forge Anew").card);
    GameState u = Fresh();
    GoldFishRunner::StampDeckTraits(u, with_card);
    CHECK(u.deck_has_gy_equipment_return);
    CHECK(u.deck_has_first_equip_waiver);
    // The waiver walk is gated by its stamp: a deck proven to hold no waiver card never reads one.
    Put(u, "Forge Anew", 1);
    CHECK(FirstEquipWaiverOnBattlefield(u, 0));
    u.deck_has_first_equip_waiver = false;
    CHECK_FALSE(FirstEquipWaiverOnBattlefield(u, 0));
}

TEST_CASE("Forge Anew breakpoint: cast Forge Anew, return the Hammer, equip it for {0} in the SAME phase")
{
    EnsureCardsLoaded();
    auto board = []()
    {
        GameState s = Fresh();
        Gy(s, "Colossus Hammer", 10);
        Put(s, "Kor Duelist", 3);
        Hand(s, "Forge Anew", 1);
        for (int k = 0; k < 3; ++k) { Put(s, "Plains", 50 + k); }   // exactly {2}{W}: nothing left over
        return s;
    };
    const int hammer = 10, duelist = 3;
    // The cast is the only thing castable. Its apply returns the Hammer and site 10 arms on it.
    const GameState s0 = board();
    const TurnSolver::Plan base = TurnSolver::Solve(s0, /*is_pre_combat=*/true,
                                                   TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    REQUIRE(HasAction(base, Action::Kind::CastFromHand, "Forge Anew"));
    // (a) The SEARCHED continuation (the variant the wave walker opens for a plan PlanOpensBreakpoint
    //     marks): candidate 0 of the post-return list, enumerated on the board where the Hammer IS an
    //     equip candidate and Forge Anew's waiver is live, equips it for {0}.
    {
        GameState s = board();
        TurnSolver::Plan v = base;
        v.bp_choice = 0;
        v.bp_at     = 0;
        TurnSolver::ApplyPlan(s, v, /*is_pre_combat=*/true);
        const Permanent* h = FindNum(s, hammer);
        REQUIRE(h != nullptr);
        CHECK(h->equipped_to == duelist);
        CHECK(s.players[0].equip_activated_this_turn);
        CHECK(CountTappedLands(s) == 3);
    }
    // (b) The SEARCH finds that line on its own: a depth-2 search of this phase commits a plan whose
    //     apply ends with the Hammer on the Duelist. Before PlanOpensBreakpoint's graveyard-Equipment
    //     clause the base plan opened no site-10 variant, so its only continuation was the unbranched
    //     canon, which applies casts only -- the returned Hammer sat unequipped at any budget.
    {
        GameState s = board();
        const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2);
        TurnSolver::ApplyPlan(s, pick, /*is_pre_combat=*/true);
        const Permanent* h = FindNum(s, hammer);
        REQUIRE(h != nullptr);
        CHECK(h->equipped_to == duelist);
    }
    // (c) ...and the EXECUTOR realises it: the real turn (AIEngine searching at depth 2, then
    //     replaying its committed continuation at the same site-10 occurrence) ends the same way.
    {
        GameState s = board();
        AIEngine eng(MulliganProfile::DefaultProfile(), 2, 20);
        eng.TakeTurn(s, /*is_pre_combat_main=*/true, ResolveAll);
        const Permanent* h = FindNum(s, hammer);
        REQUIRE(h != nullptr);
        CHECK(h->equipped_to == duelist);
        CHECK(s.players[0].equip_activated_this_turn);
    }
}

// ---- 11. human play: the dig chooser over the graveyard, only Equipment legal, mandatory ---------

TEST_CASE("Forge Anew (human): the dig chooser shows the graveyard, only its Equipment legal; one legal card is not asked")
{
    EnsureCardsLoaded();
    struct Scoped
    {
        DigChooser fn; DigChooser* saved;
        explicit Scoped(DigChooser f) : fn(std::move(f)), saved(g_play_dig_chooser) { g_play_dig_chooser = &fn; }
        ~Scoped() { g_play_dig_chooser = saved; }
    };
    {
        GameState s = Fresh();
        Gy(s, "Plains", 13);
        Gy(s, "Colossus Hammer", 10);
        const int lox   = Gy(s, "Loxodon Warhammer", 11);
        const int forge = Put(s, "Forge Anew", 1, true);
        int asked = 0, seen_default = -2;
        std::vector<int> seen_legal;
        std::size_t seen_examined = 0;
        Scoped sc([&](const GameState&, int, const std::string& src, const std::vector<Card>& examined,
                      const std::vector<int>& legal, int def)
        {
            ++asked;
            CHECK(src.find("(return Equipment from graveyard)") != std::string::npos);
            seen_examined = examined.size();
            seen_legal    = legal;
            seen_default  = def;
            return 2;   // the Warhammer (graveyard index 2)
        });
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
        CHECK(asked == 1);
        CHECK(seen_examined == 3);                          // the whole graveyard is shown
        CHECK(seen_legal == std::vector<int>{ 1, 2 });       // only the Equipment cards are takeable
        CHECK(seen_default == 1);                            // the provider's pick: the Hammer
        CHECK(FindNum(s, lox) != nullptr);                   // the person's pick is honoured
    }
    {   // A decline (-1) or an illegal reply takes the default: the return is mandatory.
        GameState s = Fresh();
        Gy(s, "Plains", 13);
        const int hammer = Gy(s, "Colossus Hammer", 10);
        Gy(s, "Loxodon Warhammer", 11);
        const int forge = Put(s, "Forge Anew", 1, true);
        Scoped sc([&](const GameState&, int, const std::string&, const std::vector<Card>&,
                      const std::vector<int>&, int) { return 0; });   // the Plains: not legal
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
        CHECK(FindNum(s, hammer) != nullptr);
        CHECK(InGraveyard(s, 13));
    }
    {   // A single legal Equipment is not a choice: no prompt.
        GameState s = Fresh();
        Gy(s, "Plains", 13);
        const int hammer = Gy(s, "Colossus Hammer", 10);
        const int forge  = Put(s, "Forge Anew", 1, true);
        int asked = 0;
        Scoped sc([&](const GameState&, int, const std::string&, const std::vector<Card>&,
                      const std::vector<int>&, int) { ++asked; return -1; });
        FireOwnEtbTriggers(s, 0, IndexOf(s, forge), std::string(), -1);
        CHECK(asked == 0);
        CHECK(FindNum(s, hammer) != nullptr);
    }
}
