// Unit tests for the CAST RIDERS -- the per-cast values a plan action hands to ONE cast that are not
// positional parameters of the executor's cast_by_name or the rollout's apply_one: Magnetic Theft's
// Equipment target (Action::attach_equip_id), Mycoloth's devour count, Beseech's coloured twobrid
// pips, Search for Tomorrow's land-put credit and a walker's same-cast loyalty activation.
//
// THE CONTRACT: consume-once. A plan-execution site arms them from the Action immediately before the
// cast, and the cast entry TAKES them (copies them out and resets them) as its first statement, before
// any early return -- so a cast that is skipped (card not in hand, held for a karoo, truncated by a
// breakpoint, unknown, over the Irencrag budget, unpayable, illegal) can never hand a value to a later
// cast. Before CastRiders the executor reset only some of them on a cast_by_name miss: a skipped Theft
// left its Equipment target armed for the next cast that did not arm its own (cast_alt, the dig
// rebuy, the Aura recast, the held-Aura release), and a skipped Mycoloth its devour count.
//
// WHY UNIT TESTS. Every executor cast that DOES arm overwrites a stale value, and every one that does
// not casts a card whose stamp is param-gated off, so no current deck shows the leak in a game: the
// cases below drive the executor's own hand cast (MtgTestSeam::CastSpellFromHand, the shape of
// cast_alt / the dig rebuy) right after a skipped cast, and check what it puts on the stack.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

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

// `top` goes on the library first (the top: Library::DrawTop reads the front), then 20 Plains.
GameState Fresh(const std::vector<std::string>& top = {})
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (std::size_t k = 0; k < top.size(); ++k)
    {
        Card c = Def(top[k]).card;
        c.m_number = 800 + static_cast<int>(k);
        s.players[0].library.push_back(c);
    }
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
    if (Def(name).params.loyalty_start > 0) { p.loyalty = Def(name).params.loyalty_start; }
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

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

Action CastAct(const std::string& name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &Def(name);
    a.hand_index     = -1;
    a.cost           = Def(name).card.m_mana_cost;
    a.is_noncreature = !Def(name).card.IsCreature();
    return a;
}

Action TheftAct(int eq, int host)
{
    Action a = CastAct("Magnetic Theft");
    a.attach_equip_id = eq;
    a.enchant_target  = host;
    return a;
}

TurnSolver::Plan PlanOf(const std::vector<Action>& acts)
{
    TurnSolver::Plan p;
    p.actions = acts;
    return p;
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

// The executor's hand cast with NOTHING armed (cast_alt / the dig rebuy / the Aura recast shape):
// cast `name` (just put into hand) and return the stack entry it pushed.
int UntappedLands(const GameState& s)
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

StackEntry CastUnarmed(AIEngine& eng, GameState& s, const std::string& name, int number)
{
    Hand(s, name, number);
    ManaPool avail = AvailableManaPool(s);
    const std::size_t before = s.stack.size();
    MtgTestSeam::CastSpellFromHand(eng, s, s.players[0].hand.back(), avail, 0);
    REQUIRE(s.stack.size() == before + 1);
    return s.stack.back();
}

}   // namespace

TEST_CASE("Cast riders: a SKIPPED Magnetic Theft does not hand its Equipment target to the next cast")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Kor Duelist", 3);
    Put(s, "Swiftblade Vindicator", 4);
    Put(s, "Colossus Hammer", 1);
    Put(s, "Dragonfire Blade", 2);
    for (int k = 0; k < 6; ++k) { Put(s, "Mountain", 50 + k); }
    Hand(s, "Magnetic Theft", 60);   // ONE Theft in hand, a plan with TWO: the second has no card
    const TurnSolver::Plan line = PlanOf({ TheftAct(1, 3), TheftAct(2, 4) });

    // The rollout: the first Theft moves the Hammer, the second finds no card and does nothing.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    REQUIRE(FindNum(roll, 1) != nullptr);
    CHECK(FindNum(roll, 1)->equipped_to == 3);
    CHECK(FindNum(roll, 2)->equipped_to == 0);

    // The executor realises the same board...
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    REQUIRE(FindNum(exec, 1) != nullptr);
    CHECK(FindNum(exec, 1)->equipped_to == 3);
    CHECK(FindNum(exec, 2)->equipped_to == 0);

    // ...and the skipped Theft's Equipment (the Dragonfire Blade, #2) is NOT left armed: the next
    // Theft cast that arms nothing of its own carries no Equipment (it used to carry #2).
    const StackEntry next = CastUnarmed(eng, exec, "Magnetic Theft", 61);
    CHECK(next.attach_equip_id == 0);
}

TEST_CASE("Cast riders: a SKIPPED Mycoloth does not hand its devour count to the next cast")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    for (int k = 0; k < 10; ++k) { Put(s, "Forest", 50 + k); }
    Put(s, "Kor Duelist", 3);
    Put(s, "Swiftblade Vindicator", 4);
    Action myc = CastAct("Mycoloth");   // not in hand: the cast is skipped
    myc.devour_count = 2;
    const TurnSolver::Plan line = PlanOf({ myc });
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    CHECK(FindNum(exec, 3) != nullptr);   // nothing was devoured
    CHECK(FindNum(exec, 4) != nullptr);
    const StackEntry next = CastUnarmed(eng, exec, "Mycoloth", 70);
    CHECK_FALSE(next.devour_count.has_value());
}

TEST_CASE("Cast riders: an UNPAID walker cast does not fire its loyalty activation (executor == rollout)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int old_ajani = Put(s, "Ajani, Strength of the Pride", 30);   // on the battlefield, unactivated
    Hand(s, "Ajani, Strength of the Pride", 31);                          // no mana: the cast cannot be paid
    Action cast = CastAct("Ajani, Strength of the Pride");
    cast.loyalty_ability = 0;   // "cast it, then +1 this turn"
    const TurnSolver::Plan line = PlanOf({ cast });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    REQUIRE(FindNum(roll, old_ajani) != nullptr);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    REQUIRE(FindNum(exec, old_ajani) != nullptr);

    // The rollout casts nothing and activates nothing; the executor used to activate the OLD Ajani.
    CHECK_FALSE(FindNum(roll, old_ajani)->loyalty_activated_this_turn);
    CHECK(FindNum(exec, old_ajani)->loyalty_activated_this_turn
          == FindNum(roll, old_ajani)->loyalty_activated_this_turn);
    CHECK(FindNum(exec, old_ajani)->loyalty == FindNum(roll, old_ajani)->loyalty);
    CHECK(exec.players[0].hand.size() == roll.players[0].hand.size());
}

// ---- the FREE-CAST one-shot (Maelstrom Archangel's banked charge; a cascade hit) -----------------
//
// The rollout used to arm its free-cast flag (cascade_free) and SPEND the bank before apply_one, and
// to consume the flag only after five early returns; a skipped free cast therefore made the NEXT cast
// free. The executor spends the bank at payment and charges the next cast. Now apply_one takes the
// flag (and the action's free_cast rider) at its head and spends the bank at payment, as the executor.

TEST_CASE("Free cast: a SKIPPED Archangel free cast does not make the next cast free (rollout == executor)")
{
    EnsureCardsLoaded();
    // No mana at all: the Duelist is castable only for free. One banked Maelstrom Archangel charge, post-
    // combat main; the plan's free cast names a creature that is not in hand, so it is skipped. (A
    // creature, so the generic cast order -- creatures first, ties by plan order -- keeps it ahead of
    // the Duelist.)
    GameState s = Fresh();
    Hand(s, "Kor Duelist", 31);
    s.free_casts_available = 1;
    Action skipped = CastAct("Swiftblade Vindicator");
    skipped.free_cast = true;
    skipped.cost      = ManaCost{};
    const TurnSolver::Plan line = PlanOf({ skipped, CastAct("Kor Duelist") });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/false);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    eng.SetSearchPostCombat(true);   // the executor plays this post-combat main (FiveColour does)
    MtgTestSeam::CommitPhasePlan(eng, line, false);
    GameState exec = s;
    eng.TakeTurn(exec, false, ResolveAll);

    // The executor cannot pay {W} and keeps the charge; the rollout used to spend the charge on the
    // skipped cast AND cast the unpayable Duelist for free.
    CHECK(InHand(exec, "Kor Duelist"));
    CHECK(exec.free_casts_available == 1);
    CHECK(InHand(roll, "Kor Duelist") == InHand(exec, "Kor Duelist"));
    CHECK(roll.free_casts_available == exec.free_casts_available);
}

TEST_CASE("Free cast: a REFUSED cascade hit does not make the next cast free (rollout)")
{
    EnsureCardsLoaded();
    // Throes of Chaos cascades into Path to Exile, which has no legal target (no creature anywhere) and
    // is refused at apply_one's Path guard -- after the cascade walk armed the free-cast one-shot. Four
    // Mountains pay for Throes; nothing pays the Duelist's {W}, so only a stray free cast could cast it.
    GameState s = Fresh({ "Path to Exile" });
    for (int k = 0; k < 4; ++k) { Put(s, "Mountain", 50 + k); }
    Hand(s, "Throes of Chaos", 30);
    Hand(s, "Kor Duelist", 31);
    const TurnSolver::Plan line = PlanOf({ CastAct("Throes of Chaos"), CastAct("Kor Duelist") });
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    REQUIRE_FALSE(InHand(roll, "Throes of Chaos"));
    CHECK(InHand(roll, "Kor Duelist"));
}
