// Unit tests for Magnetic Theft -- {R} Instant, "Attach target Equipment to target creature. (Control
// of the Equipment doesn't change.)" (CardParams::attach_target_equipment).
//
// WHY THESE ARE UNIT TESTS. Every defect this card can have is silent: a Theft whose (Equipment,
// creature) pairs collapse in the plan dedup can only ever make the ONE pair the sort ranked first (a
// greedy pick inside the search window); a Theft cast before the Hammer it targets resolves to nothing;
// a Theft that sets equip_activated_this_turn spends Forge Anew's free first equip; a Theft that moves
// a creature Lizard Blades without turning it into a non-creature attacks for two the rules forbid.
// None of them crashes or diverges between worlds (they share the resolver), and none moves any other
// deck's digest.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <functional>
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

int Put(GameState& s, const std::string& name, int number, bool sick = false, int ctrl = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = ctrl;
    p.owner_index       = ctrl;
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

Permanent& ByNum(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountUntappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool InGraveyard(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool IsTheft(const Action& a)
{
    return a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Magnetic Theft";
}

// Every Theft action across the enumerated plans.
std::vector<Action> TheftActions(const std::vector<TurnSolver::Plan>& plans)
{
    std::vector<Action> out;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (IsTheft(a)) { out.push_back(a); } } }
    return out;
}

bool AnyTheftPair(const std::vector<TurnSolver::Plan>& plans, int eq, int host)
{
    for (const Action& a : TheftActions(plans))
    { if (a.attach_equip_id == eq && a.enchant_target == host) { return true; } }
    return false;
}

Action TheftAct(int eq, int host, const std::string& eq_hand = std::string(),
                const std::string& host_hand = std::string())
{
    Action a;
    a.kind              = Action::Kind::CastFromHand;
    a.card_name         = std::string("Magnetic Theft");
    a.def               = &Def("Magnetic Theft");
    a.hand_index        = -1;
    a.cost              = Def("Magnetic Theft").card.m_mana_cost;
    a.is_noncreature    = true;
    a.attach_equip_id   = eq;
    a.enchant_target    = host;
    if (!eq_hand.empty()) { a.attach_equip_hand = eq_hand; }
    a.trick_hand_target = host_hand;
    return a;
}

Action CastAct(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &Def(name);
    a.cost      = Def(name).card.m_mana_cost;
    a.is_noncreature = !Def(name).card.IsCreature();
    return a;
}

struct ScopedLoyaltyChooser
{
    LoyaltyTargetChooser fn;
    LoyaltyTargetChooser* saved;
    explicit ScopedLoyaltyChooser(LoyaltyTargetChooser f) : fn(std::move(f)), saved(g_play_loyalty_chooser)
    { g_play_loyalty_chooser = &fn; }
    ~ScopedLoyaltyChooser() { g_play_loyalty_chooser = saved; }
};

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

// ---- 1. the resolver: costs bypassed, not an equip ability ---------------------------------------

TEST_CASE("Magnetic Theft: resolves an unattached Hammer onto a Kor Duelist -- no equip cost, no equip activation")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int duelist = Put(s, "Kor Duelist", 2);
    Put(s, "Mountain", 50);
    REQUIRE(AttachTargetEquipmentLegal(s, 0, hammer, duelist));
    CHECK(ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), hammer, duelist));
    CHECK(ByNum(s, hammer).equipped_to == duelist);
    CHECK(CreatureHasDoubleStrike(ByNum(s, duelist), s));        // "as long as it is equipped"
    CHECK_FALSE(s.players[0].equip_activated_this_turn);         // not an equip ability (Forge Anew)
    CHECK(CountUntappedLands(s) == 1);                            // the {8} equip cost is bypassed
}

TEST_CASE("Magnetic Theft: an ATTACHED Equipment moves, and the old host loses it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int swift   = Put(s, "Swiftblade Vindicator", 2);
    const int duelist = Put(s, "Kor Duelist", 3);
    ApplyEquip(s, 0, hammer, swift);
    REQUIRE(ByNum(s, hammer).equipped_to == swift);
    CHECK(ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), hammer, duelist));
    CHECK(ByNum(s, hammer).equipped_to == duelist);
    CHECK(EquipBonusFor(ByNum(s, swift), s).first == 0);
}

// ---- 2. target legality ---------------------------------------------------------------------------

TEST_CASE("Magnetic Theft: illegal targets -- shroud host, itself, an opponent's creature, a non-Equipment")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int greaves = Put(s, "Lightning Greaves", 2);
    const int duelist = Put(s, "Kor Duelist", 3);
    const int swift   = Put(s, "Swiftblade Vindicator", 4);
    const int theirs  = Put(s, "Kor Duelist", 5, /*sick=*/false, /*ctrl=*/1);
    ApplyEquip(s, 0, greaves, duelist);                           // Greaves shrouds the Duelist
    CHECK_FALSE(AttachTargetEquipmentLegal(s, 0, hammer, duelist));   // CR 702.18b: it targets
    CHECK(AttachTargetEquipmentLegal(s, 0, hammer, swift));
    CHECK_FALSE(AttachTargetEquipmentLegal(s, 0, hammer, hammer));    // never itself (CR 301.5c)
    CHECK_FALSE(AttachTargetEquipmentLegal(s, 0, hammer, theirs));    // PROVISIONAL own-only fold
    CHECK_FALSE(AttachTargetEquipmentLegal(s, 0, swift, duelist));    // a creature is not Equipment
    // Greaves itself is a legal Equipment target (it can move off the shrouded Duelist).
    CHECK(AttachTargetEquipmentLegal(s, 0, greaves, swift));
}

TEST_CASE("Magnetic Theft: an O-Naginata onto a 1/1 is a legal TARGET pair whose attach does nothing")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int nag     = Put(s, "O-Naginata", 1);
    const int duelist = Put(s, "Kor Duelist", 2);
    CHECK(AttachTargetEquipmentLegal(s, 0, nag, duelist));        // min power is not targeting
    ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), nag, duelist);
    CHECK(ByNum(s, nag).equipped_to == 0);                        // CR 701.3b: the attach does nothing
}

TEST_CASE("Magnetic Theft: a declared target that is gone makes the spell do nothing (CR 608.2b)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer = Put(s, "Colossus Hammer", 1);
    Put(s, "Kor Duelist", 2);
    CHECK_FALSE(ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), hammer, 77));
    CHECK(ByNum(s, hammer).equipped_to == 0);
}

// ---- 3. Lizard Blades (reconfigure) ---------------------------------------------------------------

TEST_CASE("Magnetic Theft: attaching a creature Lizard Blades makes it a non-creature, and its Hammer falls off")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard  = Put(s, "Lizard Blades", 1);
    const int hammer  = Put(s, "Colossus Hammer", 2);
    const int duelist = Put(s, "Kor Duelist", 3);
    ApplyEquip(s, 0, hammer, lizard);                              // the Hammer rides the loose Lizard
    REQUIRE(ByNum(s, hammer).equipped_to == lizard);
    REQUIRE(ByNum(s, lizard).card.IsCreature());
    // A loose Lizard is a legal Equipment target although it is a creature: it has reconfigure.
    CHECK(AttachTargetEquipmentLegal(s, 0, lizard, duelist));
    CHECK(ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), lizard, duelist));
    CHECK(ByNum(s, lizard).equipped_to == duelist);
    CHECK_FALSE(ByNum(s, lizard).card.IsCreature());
    CHECK(ByNum(s, hammer).equipped_to == 0);                      // CR 704.5n: its gear fell off
    CHECK(CreatureHasDoubleStrike(ByNum(s, duelist), s));
    CHECK_FALSE(s.players[0].equip_activated_this_turn);
    // Attached, it is no longer a creature, so it is no longer a legal HOST.
    CHECK_FALSE(AttachTargetEquipmentLegal(s, 0, hammer, lizard));
}

// ---- 4. enumeration: the pair is a searched axis --------------------------------------------------

TEST_CASE("Magnetic Theft: every distinct (Equipment, creature) pair survives the plan dedup")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int warham  = Put(s, "Loxodon Warhammer", 2);
    const int duel_a  = Put(s, "Kor Duelist", 3);
    const int duel_b  = Put(s, "Kor Duelist", 4);   // identical to duel_a: folded (identity)
    const int swift   = Put(s, "Swiftblade Vindicator", 5);
    Hand(s, "Magnetic Theft", 60);
    Put(s, "Mountain", 50);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(AnyTheftPair(plans, hammer, duel_a) != AnyTheftPair(plans, hammer, duel_b));   // one of the twins
    CHECK(AnyTheftPair(plans, hammer, swift));
    CHECK((AnyTheftPair(plans, warham, duel_a) || AnyTheftPair(plans, warham, duel_b)));
    CHECK(AnyTheftPair(plans, warham, swift));
    // The pair is two DIFFERENT spells: one plan per pair, never a collapse onto one.
    int distinct = 0;
    std::vector<std::pair<int, int>> seen;
    for (const Action& a : TheftActions(plans))
    {
        const std::pair<int, int> k{ a.attach_equip_id, a.enchant_target };
        if (std::find(seen.begin(), seen.end(), k) == seen.end()) { seen.push_back(k); ++distinct; }
    }
    CHECK(distinct == 4);
}

TEST_CASE("Magnetic Theft: an equipped Duelist is NOT folded with a bare one (what it wears is identity)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int warham  = Put(s, "Loxodon Warhammer", 2);
    const int duel_a  = Put(s, "Kor Duelist", 3);
    const int duel_b  = Put(s, "Kor Duelist", 4);
    ApplyEquip(s, 0, warham, duel_b);
    Hand(s, "Magnetic Theft", 60);
    Put(s, "Mountain", 50);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(AnyTheftPair(plans, hammer, duel_a));
    CHECK(AnyTheftPair(plans, hammer, duel_b));
    // The already-attached pair (Warhammer on duel_b) is not offered (CR 701.3b no-op fold).
    CHECK_FALSE(AnyTheftPair(plans, warham, duel_b));
    CHECK(AnyTheftPair(plans, warham, duel_a));
}

TEST_CASE("Magnetic Theft: no Equipment, or no creature, means no castable Theft (CR 601.2c)")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Kor Duelist", 3);
        Hand(s, "Magnetic Theft", 60);
        Put(s, "Mountain", 50);
        CHECK(TheftActions(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
    {
        GameState s = Fresh();
        Put(s, "Colossus Hammer", 1);
        Hand(s, "Magnetic Theft", 60);
        Put(s, "Mountain", 50);
        CHECK(TheftActions(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
    {   // a shrouded creature is the only host: still no target
        GameState s = Fresh();
        const int greaves = Put(s, "Lightning Greaves", 2);
        const int duelist = Put(s, "Kor Duelist", 3);
        Put(s, "Colossus Hammer", 1);
        ApplyEquip(s, 0, greaves, duelist);
        Hand(s, "Magnetic Theft", 60);
        Put(s, "Mountain", 50);
        for (const Action& a : TheftActions(TurnSolver::EnumerateMainPlans(s, true)))
        { CHECK(a.enchant_target != duelist); }
    }
}

// ---- 5. the same-plan target: cast the Hammer, then Theft it -------------------------------------

TEST_CASE("Magnetic Theft: a hand Hammer is a target only in a plan that casts it, and the Theft resolves after it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int duelist = Put(s, "Kor Duelist", 3);
    Hand(s, "Magnetic Theft", 60);
    const int hand_hammer = Hand(s, "Colossus Hammer", 61);
    Put(s, "Mountain", 50);
    Put(s, "Mountain", 51);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool theft_on_hand = false, casts_hammer = false;
        for (const Action& a : p.actions)
        {
            if (IsTheft(a) && a.attach_equip_id == hand_hammer)
            {
                theft_on_hand = true;
                CHECK(a.attach_equip_hand.str() == "Colossus Hammer");
                CHECK(a.enchant_target == duelist);
            }
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Colossus Hammer") { casts_hammer = true; }
        }
        CHECK_MESSAGE(!(theft_on_hand && !casts_hammer), "a plan Thefts the hand Hammer without casting it");
        if (theft_on_hand && casts_hammer) { both = &p; }
    }
    REQUIRE(both != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *both, /*is_pre_combat=*/true);
    bool attached = false;
    for (const Permanent& p : t.battlefield)
    { if (p.card.m_name.str() == "Colossus Hammer" && p.equipped_to == duelist) { attached = true; } }
    CHECK(attached);
    CHECK(InGraveyard(t, "Magnetic Theft"));
}

TEST_CASE("Magnetic Theft: the dependency order holds under ANY rank -- a Theft listed first still follows its Hammer")
{
    EnsureCardsLoaded();
    // The structural sort itself.
    std::vector<Action> acts = { TheftAct(61, 3, "Colossus Hammer"), CastAct("Colossus Hammer"), CastAct("Kor Duelist") };
    std::vector<int> order = { 0, 1, 2 };
    ApplyAttachSpellDependencyOrder(acts, order);
    CHECK(order == std::vector<int>{ 1, 0, 2 });                  // right after the Hammer, stable otherwise
    CHECK(OrderingPlacesAttachSpellBeforeTarget(acts));
    // A host in hand too: the Theft follows BOTH casts.
    std::vector<Action> acts2 = { TheftAct(61, 62, "Colossus Hammer", "Kor Duelist"), CastAct("Kor Duelist"),
                                  CastAct("Colossus Hammer") };
    std::vector<int> order2 = { 0, 1, 2 };
    ApplyAttachSpellDependencyOrder(acts2, order2);
    CHECK(order2 == std::vector<int>{ 1, 2, 0 });
    // A battlefield-only Theft is never moved.
    std::vector<Action> acts3 = { TheftAct(1, 3), CastAct("Colossus Hammer") };
    std::vector<int> order3 = { 0, 1 };
    ApplyAttachSpellDependencyOrder(acts3, order3);
    CHECK(order3 == std::vector<int>{ 0, 1 });
    CHECK_FALSE(OrderingPlacesAttachSpellBeforeTarget(acts3));

    // ...and applied: a plan whose vector lists the Theft BEFORE the Hammer still attaches it.
    GameState s = Fresh();
    const int duelist = Put(s, "Kor Duelist", 3);
    Hand(s, "Magnetic Theft", 60);
    const int hand_hammer = Hand(s, "Colossus Hammer", 61);
    Put(s, "Mountain", 50);
    Put(s, "Mountain", 51);
    TurnSolver::Plan p;
    Action th = TheftAct(hand_hammer, duelist, "Colossus Hammer");
    th.hand_index = 0;
    Action hm = CastAct("Colossus Hammer");
    hm.hand_index = 1;
    p.actions = { th, hm };
    p.value = 1;
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    const Permanent* h = FindNum(t, hand_hammer);
    REQUIRE(h != nullptr);
    CHECK(h->equipped_to == duelist);
}

// ---- 6. the redundant-attach fold ------------------------------------------------------------------

TEST_CASE("Magnetic Theft: no plan both Thefts and Equips the same battlefield Equipment")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int warham = Put(s, "Loxodon Warhammer", 1);
    Put(s, "Kor Duelist", 3);
    Put(s, "Swiftblade Vindicator", 4);
    Hand(s, "Magnetic Theft", 60);
    for (int k = 0; k < 5; ++k) { Put(s, "Mountain", 50 + k); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool any_theft = false, any_equip = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool theft = false, equip = false;
        for (const Action& a : p.actions)
        {
            if (IsTheft(a) && a.attach_equip_id == warham) { theft = true; }
            if (a.kind == Action::Kind::Equip && a.sac_source_id == warham) { equip = true; }
        }
        any_theft = any_theft || theft;
        any_equip = any_equip || equip;
        CHECK_FALSE((theft && equip));
    }
    CHECK(any_theft);
    CHECK(any_equip);
}

TEST_CASE("Magnetic Theft: no plan Thefts onto a loose Lizard Blades that the same plan reconfigures away")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard  = Put(s, "Lizard Blades", 1);
    const int hammer  = Put(s, "Colossus Hammer", 2);
    Put(s, "Kor Duelist", 3);
    Hand(s, "Magnetic Theft", 60);
    for (int k = 0; k < 3; ++k) { Put(s, "Mountain", 50 + k); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool onto_lizard = false, reconf = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool t = false, r = false;
        for (const Action& a : p.actions)
        {
            if (IsTheft(a) && a.attach_equip_id == hammer && a.enchant_target == lizard) { t = true; }
            if (a.kind == Action::Kind::Equip && a.sac_source_id == lizard) { r = true; }
        }
        onto_lizard = onto_lizard || t;
        reconf = reconf || r;
        CHECK_FALSE((t && r));   // the Hammer would fall off the moment the Lizard attaches
    }
    CHECK(onto_lizard);          // Hammer onto the double-striking Lizard alone is a real line
    CHECK(reconf);
}

// ---- 7. lockstep: the executor realises the plan exactly as the rollout does ---------------------

TEST_CASE("Magnetic Theft: executor (AIEngine) and rollout (ApplyPlan) realise the same board")
{
    EnsureCardsLoaded();
    auto board = []()
    {
        GameState s = Fresh();
        Put(s, "Kor Duelist", 3);
        Put(s, "Swiftblade Vindicator", 4);
        Put(s, "Colossus Hammer", 1);
        Hand(s, "Magnetic Theft", 60);
        Hand(s, "Colossus Hammer", 61);
        Hand(s, "Magnetic Theft", 62);
        Put(s, "Mountain", 50);
        Put(s, "Mountain", 51);
        Put(s, "Plains", 52);
        return s;
    };
    const GameState s = board();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int checked = 0;
    for (const TurnSolver::Plan& line : plans)
    {
        int thefts = 0;
        for (const Action& a : line.actions) { if (IsTheft(a)) { ++thefts; } }
        if (thefts == 0) { continue; }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, line, true);
        GameState exec = s;
        eng.TakeTurn(exec, true, ResolveAll);
        INFO("plan with " << thefts << " Theft(s), " << line.actions.size() << " actions");
        for (const Permanent& p : roll.battlefield)
        {
            const Permanent* q = FindNum(exec, p.card.m_number);
            REQUIRE(q != nullptr);
            CHECK(q->equipped_to == p.equipped_to);
            CHECK(q->tapped == p.tapped);
        }
        CHECK(roll.battlefield.size() == exec.battlefield.size());
        CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
        CHECK(roll.players[0].graveyard.size() == exec.players[0].graveyard.size());
        CHECK_FALSE(exec.players[0].equip_activated_this_turn);
        ++checked;
        if (checked >= 12) { break; }
    }
    CHECK(checked > 0);
}

// ---- 8. human play: two board clicks, the searched pair preselected ------------------------------

TEST_CASE("Magnetic Theft (human): both targets are asked on the board, defaults = the searched pair")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int warham  = Put(s, "Loxodon Warhammer", 2);
    const int duelist = Put(s, "Kor Duelist", 3);
    const int swift   = Put(s, "Swiftblade Vindicator", 4);
    int asked = 0;
    std::vector<int> first_legal, second_legal;
    int first_default = -1, second_default = -1;
    {
        ScopedLoyaltyChooser sc([&](const GameState& g, int, const std::string&, const std::string&,
                                    const std::vector<int>& legal, int heur) -> int
        {
            ++asked;
            std::vector<int> nums;
            for (int i : legal) { nums.push_back(g.battlefield[static_cast<std::size_t>(i)].card.m_number); }
            if (asked == 1)
            {
                first_legal = nums; first_default = nums[static_cast<std::size_t>(heur)];
                return heur;                                              // keep the Hammer
            }
            second_legal = nums; second_default = nums[static_cast<std::size_t>(heur)];
            for (int k = 0; k < static_cast<int>(nums.size()); ++k) { if (nums[k] == swift) { return k; } }
            return heur;
        });
        ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), hammer, duelist);
    }
    CHECK(asked == 2);
    CHECK(first_default == hammer);
    CHECK(std::find(first_legal.begin(), first_legal.end(), warham) != first_legal.end());
    CHECK(second_default == duelist);
    CHECK(std::find(second_legal.begin(), second_legal.end(), swift) != second_legal.end());
    CHECK(std::find(second_legal.begin(), second_legal.end(), hammer) == second_legal.end());
    CHECK(ByNum(s, hammer).equipped_to == swift);                 // the human's pick, not the search's
}

TEST_CASE("Magnetic Theft (human): a pin that no longer resolves still lets the person choose from the board")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int duelist = Put(s, "Kor Duelist", 2);
    int asked = 0;
    {
        // One legal Equipment and one legal creature: both picks are FORCED and not prompted.
        ScopedLoyaltyChooser sc([&](const GameState&, int, const std::string&, const std::string&,
                                    const std::vector<int>&, int heur) -> int { ++asked; return heur; });
        // Pinned to a hand copy that was never cast (number 77): autonomously a no-effect, but a
        // person picks the targets from what is on the battlefield now.
        CHECK(ResolveAttachTargetEquipment(s, 0, Def("Magnetic Theft"), 77, 78));
    }
    CHECK(asked == 0);
    CHECK(ByNum(s, hammer).equipped_to == duelist);
}
