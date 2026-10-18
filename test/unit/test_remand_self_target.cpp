// Unit tests for REMAND aimed at our OWN spell (CardParams::counters_target_spell_to_hand, the
// Action::remand_by rider). Remand's only legal target against a passive opponent is one of our own
// spells on the stack (CR 115.1), so it is never a cast of its own: it rides the cast of the spell it
// targets as ONE combined plan action -- X's cost plus the Remand's, X's cast-time block, the
// Remand's cast-time block, then the Remand resolves first: X back to its owner's hand (unless it
// can't be countered), Remand draws a card, Remand to the graveyard.
//
// What these pin:
//   * the enumeration: Remand is never offered alone; every castable target gets its own variant
//     (no collapse onto one target); the variant and X's own plain cast never share a plan; an
//     unaffordable combined bill is not offered; Hinata's per-target discount applies to Remand;
//   * the realisation: the rollout's apply (TurnSolver::ApplyPlan) and the executor (AIEngine's
//     committed-plan TakeTurn) realise the SAME board -- hand, graveyard, battlefield, storm count,
//     draws (the executor/rollout lockstep);
//   * the can't-be-countered case: the spell resolves AND Remand draws;
//   * a void rider (no cast left in the Irencrag budget for the Remand) casts X plainly in both worlds.
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

#include <algorithm>
#include <string>
#include <vector>

namespace
{

const char* kRemand = "Remand";
const char* kUncounterable = "Remand Test Uncounterable Bear";   // registered below

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        // A can't-be-countered creature for the CR 701.5a case (Niv-Mizzet, Parun is added by the
        // niv-core branch; the param is what matters): a Monastery Swiftspear that can't be countered.
        const CardDefinition* base = CardDatabase::Instance().Lookup("Monastery Swiftspear");
        if (base != nullptr)
        {
            const CardDefinition copy = *base;
            CardDatabase::Instance().Register(kUncounterable, [copy]()
            {
                CardDefinition d = copy;
                d.card.m_name = InternedName(std::string(kUncounterable));
                d.params.cant_be_countered = true;
                return d;
            });
        }
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

// The land drop is spent (so no plan plays a land), and the library is `top` then 20 Plains.
GameState Fresh(const std::vector<std::string>& top = {})
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
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

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

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

std::vector<std::string> Names(const std::vector<Card>& cards)
{
    std::vector<std::string> v;
    for (const Card& c : cards) { v.push_back(c.m_name.str()); }
    std::sort(v.begin(), v.end());
    return v;
}

int TappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

bool OnBattlefield(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

// Does a plan contain a cast of `x` answered by `by` (empty = a plain cast of x)?
bool HasCast(const TurnSolver::Plan& p, const std::string& x, const std::string& by)
{
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == x && a.remand_by.str() == by)
        { return true; }
    }
    return false;
}

// The enumerated plan that does EXACTLY "cast x, Remand it" and nothing else, or nullptr.
const TurnSolver::Plan* RemandOnly(const std::vector<TurnSolver::Plan>& plans, const std::string& x)
{
    for (const TurnSolver::Plan& p : plans)
    {
        int casts = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++casts; } }
        if (casts == 1 && HasCast(p, x, kRemand)) { return &p; }
    }
    return nullptr;
}

struct Outcome
{
    std::vector<std::string> hand, graveyard;
    int tapped = 0, spells_cast = 0, drawn = 0;
    bool x_on_battlefield = false;
};

Outcome Of(const GameState& s, const std::string& x)
{
    Outcome o;
    o.hand             = Names(s.players[0].hand);
    o.graveyard        = Names(s.players[0].graveyard);
    o.tapped           = TappedLands(s);
    o.spells_cast      = s.spells_cast_this_turn;
    o.drawn            = s.players[0].cards_drawn_this_turn;
    o.x_on_battlefield = OnBattlefield(s, x);
    return o;
}

// Realise `plan` from `s` in BOTH worlds and return (rollout, executor).
std::pair<Outcome, Outcome> BothWorlds(const GameState& s, const TurnSolver::Plan& plan,
                                       const std::string& x)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAll);
    return { Of(roll, x), Of(exec, x) };
}

void CheckSame(const Outcome& r, const Outcome& e)
{
    CHECK(r.hand == e.hand);
    CHECK(r.graveyard == e.graveyard);
    CHECK(r.tapped == e.tapped);
    CHECK(r.spells_cast == e.spells_cast);
    CHECK(r.drawn == e.drawn);
    CHECK(r.x_on_battlefield == e.x_on_battlefield);
}

}   // namespace

TEST_CASE("Remand: never offered alone; every target is its own variant; X and its remand never share a plan")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    for (int k = 0; k < 4; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Ponder", 11);
    Hand(s, "Bone Saw", 12);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    REQUIRE_FALSE(plans.empty());
    bool remand_alone = false, on_ponder = false, on_saw = false, twice = false;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kRemand) { remand_alone = true; }
        }
        if (HasCast(p, "Ponder", kRemand))   { on_ponder = true; }
        if (HasCast(p, "Bone Saw", kRemand)) { on_saw = true; }
        // ONE Ponder in hand: its plain cast and its remand are the same physical card twice.
        if (HasCast(p, "Ponder", kRemand) && HasCast(p, "Ponder", "")) { twice = true; }
        // ONE Remand in hand: at most one remand per plan.
        int remands = 0;
        for (const Action& a : p.actions) { if (!a.remand_by.empty()) { ++remands; } }
        CHECK(remands <= 1);
    }
    CHECK_FALSE(remand_alone);
    CHECK(on_ponder);   // both targets reach the plan list -- the dedup keeps them distinct
    CHECK(on_saw);
    CHECK_FALSE(twice);
    // "Remand Bone Saw, then cast Ponder" is one legal plan (two different spells).
    bool mixed = false;
    for (const TurnSolver::Plan& p : plans)
    { if (HasCast(p, "Bone Saw", kRemand) && HasCast(p, "Ponder", "")) { mixed = true; } }
    CHECK(mixed);
}

TEST_CASE("Remand: the combined bill is X's cost plus {1}{U}, and an unaffordable bill is not offered")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Island", 50);   // ONE Island: {1}{U} is not payable
        Hand(s, kRemand, 10);
        Hand(s, "Bone Saw", 12);
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        { CHECK_FALSE(HasCast(p, "Bone Saw", kRemand)); }
    }
    GameState s = Fresh();
    for (int k = 0; k < 3; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Ponder", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Ponder");
    REQUIRE(p != nullptr);
    for (const Action& a : p->actions)
    {
        if (a.remand_by.empty()) { continue; }
        CHECK(a.cost.ManaValue() == 3);   // {U} + {1}{U}
        CHECK(a.cost.blue == 2);
        CHECK(a.direct_damage == 0);      // a countered X projects none of its resolution
        CHECK(a.is_draw);
    }
}

TEST_CASE("Remand: Hinata's per-target discount applies to the Remand itself")
{
    EnsureCardsLoaded();
    // Sol Ring {1} + Remand {1}{U} = 3 mana; Hinata takes {1} off the Remand (one
    // target), so two Islands pay it -- and without her they do not.
    GameState s = Fresh({ "Mountain" });
    for (int k = 0; k < 2; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Sol Ring", 12);
    CHECK(RemandOnly(TurnSolver::EnumerateMainPlans(s, true), "Sol Ring") == nullptr);
    Put(s, "Hinata, Dawn-Crowned", 40);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Sol Ring");
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorlds(s, *p, "Sol Ring");
    CHECK(r.tapped == 2);
    CHECK_FALSE(r.x_on_battlefield);
    CHECK(r.hand == std::vector<std::string>{ "Mountain", "Sol Ring" });
    CheckSame(r, e);
}

TEST_CASE("Remand on a mana rock: X back in hand, a card drawn, both casts counted (rollout == executor)")
{
    EnsureCardsLoaded();
    GameState s = Fresh({ "Mountain" });   // the Remand's draw
    for (int k = 0; k < 3; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Sol Ring", 12);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Sol Ring");
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorlds(s, *p, "Sol Ring");
    // The rock never resolved -- it is in hand, not on the battlefield, and its mana was never
    // credited; Remand is in the
    // graveyard; the Remand drew the Mountain; three Islands paid {1} + {1}{U}; two spells
    // cast.
    CHECK(r.hand == std::vector<std::string>{ "Mountain", "Sol Ring" });
    CHECK(r.graveyard == std::vector<std::string>{ kRemand });
    CHECK_FALSE(r.x_on_battlefield);
    CHECK(r.tapped == 3);
    CHECK(r.spells_cast == 2);
    CHECK(r.drawn == 1);
    CheckSame(r, e);
}

TEST_CASE("Remand, then RECAST: the Remand's cantrip continuation casts the returned spell again (rollout)")
{
    EnsureCardsLoaded();
    // Bone Saw costs {0}: after "Bone Saw + Remand on it" the Saw is back in hand and the Remand's
    // draw opens the plain-cantrip breakpoint, whose continuation casts it again -- "Probe, Remand,
    // recast Probe" with no machinery of its own. (The executor replays the continuation the search
    // RECORDS for a committed line; this harness commits a bare plan, so only the rollout is read.)
    GameState s = Fresh({ "Mountain" });
    for (int k = 0; k < 2; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Bone Saw", 12);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Bone Saw");
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, /*is_pre_combat=*/true);
    const Outcome r = Of(roll, "Bone Saw");
    CHECK(r.x_on_battlefield);                    // recast by the continuation
    CHECK(r.spells_cast == 3);                    // Bone Saw, Remand, Bone Saw
    CHECK(r.drawn == 1);
    CHECK(r.hand == std::vector<std::string>{ "Mountain" });
    CHECK(r.graveyard == std::vector<std::string>{ kRemand });
}

TEST_CASE("Remand on a cantrip: the cantrip does NOT resolve (no Ponder draw); only Remand draws")
{
    EnsureCardsLoaded();
    GameState s = Fresh({ "Mountain", "Forest" });
    for (int k = 0; k < 3; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Ponder", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Ponder");
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorlds(s, *p, "Ponder");
    CHECK(r.drawn == 1);
    CHECK(r.hand == std::vector<std::string>{ "Mountain", "Ponder" });
    CHECK(r.graveyard == std::vector<std::string>{ kRemand });
    CheckSame(r, e);
}

TEST_CASE("Remand on a spell that can't be countered: the spell RESOLVES and Remand still draws")
{
    EnsureCardsLoaded();
    REQUIRE(CardDatabase::Instance().Lookup(kUncounterable) != nullptr);
    GameState s = Fresh({ "Forest" });
    Put(s, "Mountain", 50);
    for (int k = 0; k < 2; ++k) { Put(s, "Island", 51 + k); }
    Hand(s, kRemand, 10);
    Hand(s, kUncounterable, 13);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, kUncounterable);
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorlds(s, *p, kUncounterable);
    CHECK(r.x_on_battlefield);
    CHECK(r.hand == std::vector<std::string>{ "Forest" });
    CHECK(r.graveyard == std::vector<std::string>{ kRemand });
    CHECK(r.drawn == 1);
    CHECK(r.spells_cast == 2);
    CheckSame(r, e);
}

TEST_CASE("Remand rider is VOID with no cast left for it (Irencrag budget): X is cast plainly, in both worlds")
{
    EnsureCardsLoaded();
    GameState s = Fresh({ "Mountain", "Forest" });
    for (int k = 0; k < 3; ++k) { Put(s, "Island", 50 + k); }
    Hand(s, kRemand, 10);
    Hand(s, "Ponder", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = RemandOnly(plans, "Ponder");
    REQUIRE(p != nullptr);
    GameState one_left = s;
    one_left.casts_remaining_this_turn = 1;   // "you can cast only one more spell this turn"
    const auto [r, e] = BothWorlds(one_left, *p, "Ponder");
    CHECK(r.spells_cast == 1);
    CHECK(std::find(r.graveyard.begin(), r.graveyard.end(), "Ponder") != r.graveyard.end());
    CHECK(std::find(r.hand.begin(), r.hand.end(), std::string(kRemand)) != r.hand.end());
    CheckSame(r, e);
}

TEST_CASE("Remand: the shared predicates -- never castable on its own, never a free-cast hit")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    CHECK(CastableOnItsOwn(Def("Bone Saw")));
    CHECK_FALSE(CastableOnItsOwn(Def(kRemand)));
    CHECK_FALSE(FreeCastHasLegalTargets(s, 0, Def(kRemand)));
    CHECK(Def(kRemand).params.counters_target_spell_to_hand);
    CHECK(Def(kRemand).params.draw == 1);
}

