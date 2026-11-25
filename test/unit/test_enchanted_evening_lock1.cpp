// Enchanted Evening lock cluster, STEP 1 (2026-11-26; docs/design/enchanted-evening-lock-design.md §4.1 / §6):
// the type statics.
//   * Enchanted Evening (static_all_permanents_enchantments): layer 4, every permanent is also an enchantment.
//   * Opalescence (static_animate_other_nonaura_enchantments_mv): layer 4 + 7b, every OTHER non-Aura enchantment is
//     also a creature with base P/T = its mana value.
// RefreshTypeStatics retypes each battlefield permanent's own Card copy (restore-then-reapply, bits on the card);
// TypeStaticsSbaCheckpoint kills the 0/0s (CR 704.5f) and sweeps the Auras that fell off them (CR 704.5m) at the
// shared SBA points, in both worlds.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
const char* const kEE    = "Enchanted Evening";
const char* const kOpal  = "Opalescence";
const char* const kPres  = "Enchantress's Presence";
const char* const kRing  = "Oblivion Ring";
const char* const kGrove = "Sterling Grove";

const CardDefinition& L1Def(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card L1Card(const std::string& name, int number)
{
    Card c = L1Def(name).card;
    c.m_number = number;
    return c;
}

int L1Put(GameState& s, const std::string& name, int number, int controller = 0, bool entered = false)
{
    Permanent p;
    p.card = L1Card(name, number);
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = entered;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int L1Spawn(GameState& s, int pw, int number)
{
    Card c;
    c.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power = pw;
    c.m_toughness = pw;
    c.m_number = number;
    Permanent p;
    p.card = c;
    p.controller_index = p.owner_index = 1;   // is_token stays false, as at both engine spawn sites
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState L1Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L1Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L1Card("Plains", 100900 + k)); }
    return s;
}

const Permanent* L1Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int L1Count(const GameState& s, const std::string& name, int controller = -1)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == name && (controller < 0 || p.controller_index == controller)) { ++n; } }
    return n;
}

int L1Lands(const GameState& s, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == controller && p.card.IsLand()) { ++n; } }
    return n;
}

bool L1InGraveyard(const GameState& s, int player, const std::string& name)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

const TurnSolver::Plan* L1FindCast(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name && a.tutor_target.str().empty())
        { return &p; }
    }
    return nullptr;
}

// Both worlds, the executor through the REAL GameEngine::ResolveStack (its post-resolution SBA pass and
// LegendRuleSbaCheckpoint are part of what is under test).
void L1CheckWorlds(const GameState& s, const TurnSolver::Plan& plan,
                   const std::function<void(const GameState&)>& check)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameEngine ge(eng);
    GameState exec = s;
    eng.TakeTurn(exec, true, [&ge](GameState& st) { MtgTestSeam::GeResolveStack(ge, st); });
    for (const GameState* g : { static_cast<const GameState*>(&roll), static_cast<const GameState*>(&exec) })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        check(*g);
    }
}
}   // namespace

TEST_CASE("Enchanted Evening / Opalescence: costs, types, params; routed to the deck's provider")
{
    const CardDefinition& ee = L1Def(kEE);
    CHECK(ee.card.m_mana_cost.ManaValue() == 5);
    CHECK(ee.card.m_mana_cost.hybrid_count == 2);
    CHECK(ee.card.IsEnchantment());
    CHECK_FALSE(ee.card.IsCreature());
    CHECK(ee.params.static_all_permanents_enchantments);
    CHECK_FALSE(ee.params.static_animate_other_nonaura_enchantments_mv);
    const CardDefinition& op = L1Def(kOpal);
    CHECK(op.card.m_mana_cost.ManaValue() == 4);
    CHECK(op.card.m_mana_cost.white == 2);
    CHECK(op.params.static_animate_other_nonaura_enchantments_mv);
    CHECK_FALSE(op.params.static_all_permanents_enchantments);
    // A list holding only the lock pieces still routes to the deck's provider (the OR in DetectDecisionProvider).
    Decklist d;
    d.mainboard.push_back(L1Card(kEE, 1));
    d.mainboard.push_back(L1Card(kOpal, 2));
    d.mainboard.push_back(L1Card("Forest", 3));
    CHECK(&DetectDecisionProvider(d) != &DetectDecisionProvider(Decklist{}));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "EnchantedEvening");
}

TEST_CASE("Enchanted Evening alone: every permanent of both sides is also an enchantment; nothing animates; undone when it leaves")
{
    GameState s = L1Board();
    L1Put(s, "Forest", 101);
    L1Put(s, "Plains", 100001, /*controller=*/1);
    L1Spawn(s, 2, 700001);
    L1Put(s, kPres, 50);
    L1Put(s, kEE, 51);
    CHECK(RefreshTypeStatics(s));
    for (const Permanent& p : s.battlefield)
    {
        CAPTURE(p.card.m_name.str());
        CHECK(p.card.IsEnchantment());
    }
    CHECK(L1Find(s, 101)->card.IsLand());
    CHECK_FALSE(L1Find(s, 101)->card.IsCreature());
    CHECK_FALSE(L1Find(s, 50)->card.IsCreature());
    CHECK(L1Find(s, 700001)->card.IsCreature());                        // the spawn keeps its own type and P/T
    CHECK(L1Find(s, 700001)->EffectivePower() == 2);
    CHECK(L1Find(s, 101)->card.m_ts_mask == Card::kTsAddedEnchantment);
    CHECK(L1Find(s, 50)->card.m_ts_mask == 0);                            // already an enchantment: nothing added
    // Idempotent: a second refresh changes nothing.
    const GameState again_before = s;
    RefreshTypeStatics(s);
    CHECK(s.battlefield == again_before.battlefield);
    // The checkpoint kills nothing (no Opalescence, so no 0/0).
    TypeStaticsSbaCheckpoint(s);
    CHECK(s.battlefield.size() == again_before.battlefield.size());
    // EE leaves: every added type is undone exactly.
    s.battlefield.erase(s.battlefield.begin() + 4);
    CHECK(RefreshTypeStatics(s));                                          // a card still carried bits
    CHECK_FALSE(L1Find(s, 101)->card.IsEnchantment());
    CHECK_FALSE(L1Find(s, 100001)->card.IsEnchantment());
    CHECK_FALSE(L1Find(s, 700001)->card.IsEnchantment());
    CHECK(L1Find(s, 50)->card.IsEnchantment());
    for (const Permanent& p : s.battlefield) { CHECK(p.card.m_ts_mask == 0); }
    CHECK_FALSE(RefreshTypeStatics(s));                                    // nothing left to do
    // Behind the stamp: a deck without a type static never retypes.
    GameState t = L1Board();
    t.deck_has_type_statics = false;
    L1Put(t, "Forest", 101);
    L1Put(t, kEE, 51);
    CHECK_FALSE(RefreshTypeStatics(t));
    CHECK_FALSE(L1Find(t, 101)->card.IsEnchantment());
}

TEST_CASE("Opalescence alone: other non-Aura enchantments are MV/MV creatures; never itself; Auras and lands untouched")
{
    GameState s = L1Board();
    const int forest = 101;
    L1Put(s, "Forest", forest);
    const int sprawl = L1Put(s, "Utopia Sprawl", 60);
    s.battlefield[static_cast<std::size_t>(sprawl)].aura_attached_to = forest;
    L1Put(s, kPres, 50);
    L1Put(s, kGrove, 52);
    L1Put(s, "Exploration", 53);
    L1Put(s, kOpal, 54);
    L1Spawn(s, 2, 700001);
    CHECK(RefreshTypeStatics(s));
    const Permanent* pres = L1Find(s, 50);
    CHECK(pres->card.IsCreature());
    CHECK(pres->EffectivePower() == 3);
    CHECK(pres->EffectiveToughness() == 3);
    CHECK(L1Find(s, 52)->EffectivePower() == 2);
    CHECK(L1Find(s, 53)->EffectiveToughness() == 1);
    CHECK_FALSE(L1Find(s, 54)->card.IsCreature());                        // "each OTHER": not itself
    CHECK_FALSE(L1Find(s, 60)->card.IsCreature());                        // an Aura
    CHECK_FALSE(L1Find(s, forest)->card.IsCreature());                    // not an enchantment without EE
    CHECK(L1Find(s, 700001)->EffectivePower() == 2);                      // the opponent has no enchantment
    // Counters (7d) and until-EOT pumps (7c) still apply on top of the 7b base.
    Permanent& pm = s.battlefield[2];
    REQUIRE(pm.card.m_number == 50);
    pm.temp_power_bonus = 2;
    CHECK(pm.EffectivePower() == 5);
    pm.temp_power_bonus = 0;
    TypeStaticsSbaCheckpoint(s);                                           // nothing is 0/0
    CHECK(s.battlefield.size() == 7);
    CHECK(L1Find(s, 60) != nullptr);
    // Opalescence leaves: Presence is a plain enchantment again with no P/T.
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { if (s.battlefield[i].card.m_number == 54) { s.battlefield.erase(s.battlefield.begin() + static_cast<std::ptrdiff_t>(i)); break; } }
    RefreshTypeStatics(s);
    CHECK_FALSE(L1Find(s, 50)->card.IsCreature());
    CHECK_FALSE(L1Find(s, 50)->card.m_power.has_value());
    CHECK_FALSE(L1Find(s, 50)->card.m_toughness.has_value());
}

TEST_CASE("Two Opalescences animate each other (4/4 each); with Enchanted Evening it is a 5/5")
{
    GameState s = L1Board();
    L1Put(s, kOpal, 54);
    L1Put(s, kOpal, 55);
    RefreshTypeStatics(s);
    CHECK(L1Find(s, 54)->card.IsCreature());
    CHECK(L1Find(s, 55)->card.IsCreature());
    CHECK(L1Find(s, 54)->EffectivePower() == 4);
    CHECK(L1Find(s, 55)->EffectiveToughness() == 4);
    L1Put(s, kEE, 51);
    RefreshTypeStatics(s);
    CHECK(L1Find(s, 51)->card.IsCreature());
    CHECK(L1Find(s, 51)->EffectivePower() == 5);
    CHECK(L1Find(s, 51)->EffectiveToughness() == 5);
    // A lone Opalescence with Enchanted Evening: EE 5/5, the Opalescence itself stays a non-creature.
    GameState t = L1Board();
    L1Put(t, kOpal, 54);
    L1Put(t, kEE, 51);
    RefreshTypeStatics(t);
    CHECK(L1Find(t, 51)->EffectivePower() == 5);
    CHECK_FALSE(L1Find(t, 54)->card.IsCreature());
}

TEST_CASE("Opalescence's new creatures: summoning sickness is control duration, not creature duration")
{
    GameState s = L1Board();
    L1Put(s, kPres, 50, 0, /*entered=*/false);                            // controlled since the turn began
    L1Put(s, kGrove, 52, 0, /*entered=*/true);                            // entered this turn
    L1Put(s, kOpal, 54, 0, /*entered=*/true);                             // Opalescence itself arrived this turn
    RefreshTypeStatics(s);
    CHECK(L1Find(s, 50)->CanAttack());
    CHECK(CanAttackFull(*L1Find(s, 50), s.battlefield, 0));
    CHECK_FALSE(L1Find(s, 52)->CanAttack());
    CHECK_FALSE(CanAttackFull(*L1Find(s, 52), s.battlefield, 0));
    CHECK_FALSE(L1Find(s, 54)->CanAttack());                              // not a creature at all
}

TEST_CASE("EE + Opalescence: every land of both sides and every MV-0 token dies at the checkpoint; Auras on lands follow")
{
    GameState s = L1Board();
    const int forest = 101;
    L1Put(s, "Forest", forest);
    L1Put(s, "Plains", 102);
    const int sprawl = L1Put(s, "Utopia Sprawl", 60);
    s.battlefield[static_cast<std::size_t>(sprawl)].aura_attached_to = forest;
    L1Put(s, "Plains", 100001, /*controller=*/1);
    L1Spawn(s, 3, 700001);
    L1Put(s, kEE, 51);
    L1Put(s, kOpal, 54);
    L1Put(s, kPres, 50);
    RefreshTypeStatics(s);
    // Retyped but not yet dead: the refresh never erases (callers may hold slot indices).
    CHECK(L1Find(s, forest)->card.IsCreature());
    CHECK(L1Find(s, forest)->EffectiveToughness() == 0);
    CHECK(L1Find(s, 700001)->EffectiveToughness() == 0);                  // the 3/3 spawn: base P/T = MV 0
    CHECK(s.battlefield.size() == 8);
    TypeStaticsSbaCheckpoint(s);
    CHECK(L1Lands(s, 0) == 0);
    CHECK(L1Lands(s, 1) == 0);
    CHECK(L1Find(s, 700001) == nullptr);
    CHECK(L1Find(s, 60) == nullptr);                                      // the Sprawl's land died (CR 704.5m)
    CHECK(L1InGraveyard(s, 0, "Forest"));
    CHECK(L1InGraveyard(s, 0, "Utopia Sprawl"));
    CHECK(L1InGraveyard(s, 1, "Plains"));                                  // the OWNER's graveyard
    CHECK(s.creature_died_this_turn);
    CHECK(L1Find(s, 51)->EffectivePower() == 5);
    CHECK(L1Find(s, 50)->EffectivePower() == 3);
    CHECK(L1Count(s, kOpal) == 1);
    // The same through LegendRuleSbaCheckpoint (the shared post-resolution / land-drop / end-step SBA point).
    GameState t = L1Board();
    L1Put(t, "Forest", 101);
    L1Put(t, kEE, 51);
    L1Put(t, kOpal, 54);
    LegendRuleSbaCheckpoint(t);
    CHECK(L1Lands(t, 0) == 0);
}

TEST_CASE("EE + Opalescence cast in BOTH worlds: Opalescence resolves, the lands it paid with die, the Sprawl follows")
{
    GameState s = L1Board();
    s.players[0].lands_played_this_turn = 1;
    const int forest = 101;
    L1Put(s, "Forest", forest);
    const int sprawl = L1Put(s, "Utopia Sprawl", 60);
    s.battlefield[static_cast<std::size_t>(sprawl)].aura_attached_to = forest;
    for (int k = 0; k < 4; ++k) { L1Put(s, "Plains", 110 + k); }
    L1Put(s, "Plains", 100001, /*controller=*/1);
    L1Spawn(s, 2, 700001);
    L1Put(s, kEE, 51);
    RefreshTypeStatics(s);
    s.players[0].hand.push_back(L1Card(kOpal, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = L1FindCast(plans, kOpal);
    REQUIRE(cast != nullptr);
    L1CheckWorlds(s, *cast, [](const GameState& g) {
        CHECK(L1Count(g, kOpal, 0) == 1);
        CHECK(L1Lands(g, 0) == 0);
        CHECK(L1Lands(g, 1) == 0);
        CHECK(L1Find(g, 700001) == nullptr);
        CHECK(L1Find(g, 60) == nullptr);
        CHECK(L1InGraveyard(g, 0, "Utopia Sprawl"));
        const Permanent* ee = L1Find(g, 51);
        REQUIRE(ee != nullptr);
        CHECK(ee->card.IsCreature());
        CHECK(ee->EffectivePower() == 5);
        CHECK(ee->CanAttack());                                            // controlled since the turn began
    });
}

TEST_CASE("Under EE + Opalescence our land drop dies as it enters; a fetchland is never cracked")
{
    for (const char* land : { "Forest", "Windswept Heath" })
    {
        CAPTURE(land);
        GameState s = L1Board();
        L1Put(s, kEE, 51);
        L1Put(s, kOpal, 54);
        s.players[0].hand.push_back(L1Card(land, 40));
        const std::size_t lib = s.players[0].library.size();
        LandPlayOptions o;
        REQUIRE(PlayLandFromHand(s, 0, L1Def(land), o));
        CHECK(s.players[0].lands_played_this_turn == 1);
        CHECK(L1Lands(s, 0) == 0);
        CHECK(L1InGraveyard(s, 0, land));
        CHECK(s.players[0].library.size() == lib);                       // no search
        CHECK(s.players[0].life == gamesetup::StartingLife());            // no fetch life paid
    }
    // Control: without Opalescence the land survives (held, or cracked for the Forest -- the provider's call).
    GameState t = L1Board();
    L1Put(t, kEE, 51);
    t.players[0].hand.push_back(L1Card("Windswept Heath", 40));
    t.players[0].library.push_back(L1Card("Forest", 950));
    LandPlayOptions o;
    REQUIRE(PlayLandFromHand(t, 0, L1Def("Windswept Heath"), o));
    CHECK(L1Lands(t, 0) == 1);
}

TEST_CASE("Under EE + Opalescence the opponent's land drop and its fetchland die; nothing is fetched")
{
    for (const char* land : { "Plains", "Windswept Heath" })
    {
        CAPTURE(land);
        GameState s = L1Board();
        s.opponent_plays_lands = true;
        L1Put(s, kEE, 51);
        L1Put(s, kOpal, 54);
        s.players[1].hand.push_back(L1Card(land, 100500));
        const std::size_t lib = s.players[1].library.size();
        const int life = s.players[1].life;
        ResolvePassiveOpponentLandTurn(s);
        CHECK(s.opponent_played_land_this_turn);
        CHECK(L1Lands(s, 1) == 0);
        CHECK(L1InGraveyard(s, 1, land));
        CHECK(s.players[1].library.size() == lib);
        CHECK(s.players[1].life == life);
        CHECK_FALSE(s.opponent_searched_library_this_turn);
    }
    // Control: with Enchanted Evening alone the opponent's Plains stays (an enchantment land, no creature).
    GameState t = L1Board();
    t.opponent_plays_lands = true;
    L1Put(t, kEE, 51);
    t.players[1].hand.push_back(L1Card("Plains", 100500));
    ResolvePassiveOpponentLandTurn(t);
    REQUIRE(L1Lands(t, 1) == 1);
    CHECK(L1Find(t, 100500)->card.IsEnchantment());
}

TEST_CASE("Under EE + Opalescence a scheduled opponent spawn (mana value 0) dies as it appears, in both worlds")
{
    static const std::vector<OpponentSpawn> sched{ OpponentSpawn{ 5, 3, 3 } };
    GameState base = L1Board();
    base.phase = Phase::Ending;
    base.opponent_spawns = &sched;
    L1Put(base, kEE, 51);
    L1Put(base, kOpal, 54);
    GameState roll = base;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
    GameState exec = base;
    exec.turn_number = 5;
    exec.phase = Phase::Beginning;
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    MtgTestSeam::GeUntapStep(ge, exec);
    for (const GameState* g : { static_cast<const GameState*>(&roll), static_cast<const GameState*>(&exec) })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        CHECK(g->turn_number == 5);
        CHECK(L1Count(*g, "3/3 Creature", 1) == 0);
        CHECK(g->next_opp_spawn_number != base.next_opp_spawn_number);   // it did appear
    }
    // Control: without Enchanted Evening the spawn is no enchantment and stays a 3/3.
    GameState c = base;
    for (std::size_t i = 0; i < c.battlefield.size(); ++i)
    { if (c.battlefield[i].card.m_number == 51) { c.battlefield.erase(c.battlefield.begin() + static_cast<std::ptrdiff_t>(i)); break; } }
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(c));
    CHECK(L1Count(c, "3/3 Creature", 1) == 1);
}

TEST_CASE("An Opalescence creature dying to the toughness SBA reaches its leave trigger: Oblivion Ring returns its card")
{
    GameState s = L1Board();
    L1Put(s, kOpal, 54);
    L1Spawn(s, 2, 700001);
    L1Put(s, kRing, 30);
    // Exile the spawn with the Ring through the shared ETB resolver (linked to this Ring).
    ResolveEtbExileNonlandUntilLeaves(s, 0, 30, kRing, ExileUntilLeavesPin(700001));
    REQUIRE(L1Find(s, 700001) == nullptr);
    RefreshTypeStatics(s);
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number != 30) { continue; }
        CHECK(p.card.IsCreature());
        CHECK(p.EffectiveToughness() == 3);
        p.temp_tough_bonus = -3;                                           // a -3/-3: 3/3 -> 0 toughness
    }
    TypeStaticsSbaCheckpoint(s);
    CHECK(L1Find(s, 30) == nullptr);
    CHECK(L1InGraveyard(s, 0, kRing));
    CHECK(L1Count(s, "2/2 Creature", 1) == 1);                             // returned under its owner
    CHECK(s.exile.empty());
}

TEST_CASE("Sterling Grove + Enchanted Evening: the shroud grant covers our lands (not the Grove, not the opponent's)")
{
    GameState s = L1Board();
    L1Put(s, kGrove, 52);
    L1Put(s, "Forest", 101);
    L1Put(s, "Plains", 100001, /*controller=*/1);
    RefreshTypeStatics(s);
    CHECK_FALSE(LandHasShroud(*L1Find(s, 101), s));                       // without EE a Forest is no enchantment
    L1Put(s, kEE, 51);
    RefreshTypeStatics(s);
    CHECK(PermanentIsEnchantmentNow(*L1Find(s, 101), s));
    CHECK(LandHasShroud(*L1Find(s, 101), s));
    CHECK(EnchantmentShroudFromGrant(*L1Find(s, 51), s));                 // EE itself
    CHECK_FALSE(EnchantmentShroudFromGrant(*L1Find(s, 52), s));           // a Grove does not shroud itself
    CHECK_FALSE(LandHasShroud(*L1Find(s, 100001), s));                    // "you control"
}

TEST_CASE("Type-statics bits ride the CARD: a copy that leaves and re-enters is undone and re-derived")
{
    GameState s = L1Board();
    L1Put(s, kPres, 50);
    L1Put(s, kOpal, 54);
    RefreshTypeStatics(s);
    Card dirty = L1Find(s, 50)->card;
    REQUIRE(dirty.m_ts_mask != 0);
    REQUIRE(dirty.IsCreature());
    // Opalescence leaves, then the dirty card re-enters (an O-Ring / Wave return builds the permanent from it).
    s.battlefield.clear();
    Permanent back;
    back.card = dirty;
    s.battlefield.push_back(back);
    RefreshTypeStatics(s);
    CHECK_FALSE(s.battlefield[0].card.IsCreature());
    CHECK_FALSE(s.battlefield[0].card.m_power.has_value());
    CHECK_FALSE(s.battlefield[0].card.m_toughness.has_value());
    CHECK(s.battlefield[0].card.m_type_mask == L1Def(kPres).card.m_type_mask);
    CHECK(s.battlefield[0].card.m_ts_mask == 0);
}
