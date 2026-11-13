// Spirits onboarding, integrator C1 (2026-11-13): Millicent, Restless Revenant.
//   * Affinity for Spirits (affinity_for_subtype + subtypes_affected Spirit, Thrumming Hivepool's param) on a
//     CREATURE spell: EffectiveSpellCost, Spirit tokens and a Spirit-chosen Metallic Mimic counted, generic only.
//   * SAME-TURN affinity: the generic AFFINITY cast-order tier (25) puts her after every Spirit creature cast /
//     Vial put (10) and Lingering Souls (20), and SameTurnAffinityDiscountFor credits each of them (AffinityFeedCount:
//     Vial puts and token-making hand casts are new), so a Millicent made cheap by Spirits cast earlier in the plan
//     is enumerated and paid in BOTH worlds.
//   * "Whenever Millicent or another NONTOKEN Spirit you control dies or deals combat damage to a player, create a
//     1/1 white Spirit creature token with flying": combat_damage_tokens_per_trigger + combat_damage_watch_nontoken
//     in the shared FireCombatDamageTokens (ResolveCombatDamage, both worlds), dies_watch_nontoken in OnCreatureDies.
// test/scenarios/spirits_millicent_*.json drive the search end to end.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
const char* const kMill  = "Millicent, Restless Revenant";
const char* const kSouls = "Lingering Souls";

const CardDefinition& MlDef(const std::string& name)
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

Card MlZone(const std::string& name, int number)
{
    MlDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int MlPut(GameState& s, const std::string& name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card = MlDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const DecisionProvider& SpiritsProv()
{
    static const DecisionProvider& prov = [] () -> const DecisionProvider& {
        MlDef("Plains");
        return DetectDecisionProvider(DeckLoader::LoadFromFile(
            ResolveHeuristicDefaultsPath("decks/Spirits/Spirits.cod")));
    }();
    return prov;
}

// A Spirits board: the library holds Spirit creatures, so the deck's dominant creature type (what a Metallic
// Mimic names, DominantCreatureSubtypeId) is Spirit.
GameState MlBoard(const std::vector<std::string>& lands)
{
    GameState s;
    s.m_provider          = &SpiritsProv();
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(MlZone("Supreme Phantom", 900 + k)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(MlZone("Plains", 950 + k)); }
    int n = 100;
    for (const std::string& l : lands) { MlPut(s, l, ++n); }
    return s;
}

int MlCount(const std::vector<Action>& acts, Action::Kind k, const std::string& name)
{
    int n = 0;
    for (const Action& a : acts) { if (a.kind == k && a.card_name.str() == name) { ++n; } }
    return n;
}

int MlSpiritTokens(const GameState& s, int controller = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && p.is_token && CardHasSubtype(p.card, "Spirit")) { ++n; } }
    return n;
}

int MlTappedLands(const GameState& g)
{
    int n = 0;
    for (const Permanent& p : g.battlefield) { if (p.controller_index == 0 && p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

int MlIndex(const GameState& s, const std::string& name, int nth = 0)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_name.str() == name && nth-- == 0) { return i; } }
    return -1;
}

void MlResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState MlRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, MlResolveAll);
    return exec;
}

const TurnSolver::Plan* MlFindPlan(const std::vector<TurnSolver::Plan>& plans,
                                   const std::vector<std::pair<Action::Kind, std::string>>& want)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != want.size()) { continue; }
        bool ok = true;
        for (const auto& w : want)
        {
            int need = 0;
            for (const auto& w2 : want) { if (w2 == w) { ++need; } }
            if (MlCount(p.actions, w.first, w.second) != need) { ok = false; break; }
        }
        if (ok) { return &p; }
    }
    return nullptr;
}

// Both worlds' main-phase line for `plan`: Millicent resolved, the given token count, `lands` tapped.
void MlCheckBothWorlds(const GameState& s, const TurnSolver::Plan& plan, int spirit_tokens, int lands)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = MlRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(MlIndex(*g, kMill) >= 0);
        CHECK(MlSpiritTokens(*g) == spirit_tokens);
        CHECK(MlTappedLands(*g) == lands);
    }
}
}   // namespace

// ---- the card + affinity on a creature spell ---------------------------------------------------------

TEST_CASE("Millicent: the card -- {5}{W}{U} legendary 4/4 flying Spirit Soldier; params")
{
    const CardDefinition& d = MlDef(kMill);
    CHECK(d.card.m_mana_cost.ToString() == "{5}{W}{U}");
    CHECK(d.card.m_mana_cost.ManaValue() == 7);
    CHECK(d.card.HasSupertype(Supertype::Legendary));
    CHECK(d.card.IsCreature());
    CHECK(CardHasSubtype(d.card, "Spirit"));
    CHECK(CardHasSubtype(d.card, "Soldier"));
    CHECK(d.card.HasKeyword(Keyword::Flying));
    CHECK(d.card.m_power.value_or(0) == 4);
    CHECK(d.card.m_toughness.value_or(0) == 4);
    CHECK(d.params.affinity_for_subtype);
    CHECK(d.params.combat_damage_tokens_per_trigger == 1);
    CHECK(d.params.combat_damage_tokens_per_damage == 0);   // NOT Shroofus' "that many"
    CHECK(d.params.combat_damage_watch_nontoken);
    CHECK(d.params.dies_watch_nontoken);
    CHECK(d.params.dies_watch_includes_self);
    CHECK(std::string(SpiritsProv().Name()) == "Spirits");   // affinity on a Spirit is a spirits routing term
}

TEST_CASE("Millicent affinity on a CREATURE spell: {1} less per Spirit we control, tokens and a Spirit-named Mimic count")
{
    GameState s = MlBoard({});
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{5}{W}{U}");
    MlPut(s, "Supreme Phantom", 1);
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{4}{W}{U}");
    ApplyCastCreatesTokens(s, 0, MlDef(kSouls));   // two 1/1 Spirit tokens
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{2}{W}{U}");
    // A Metallic Mimic entering names the deck's type (Spirit) and becomes one.
    PutCardOntoBattlefield(s, 0, MlZone("Metallic Mimic", 2), "test");
    const int mi = MlIndex(s, "Metallic Mimic");
    REQUIRE(mi >= 0);
    CHECK(CardHasSubtype(s.battlefield[mi].card, "Spirit"));
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{1}{W}{U}");
    // Non-Spirits do not count; the opponent's Spirits do not count.
    MlPut(s, "Plains", 3);
    MlPut(s, "Supreme Phantom", 4, false, 1);
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{1}{W}{U}");
    // Generic only: the floor is {W}{U}.
    MlPut(s, "Supreme Phantom", 5);
    MlPut(s, "Supreme Phantom", 6);
    CHECK(EffectiveSpellCost(MlDef(kMill), s).ToString() == "{W}{U}");
}

TEST_CASE("Millicent casts in the AFFINITY tier: after a Spirit creature, Lingering Souls and a Mimic")
{
    GameState s = MlBoard({});
    const DecisionProvider& p = ResolveProvider(s);
    const int m = p.CastOrderRank(s, MlDef(kMill));
    CHECK(m == 25);
    CHECK(p.CastOrderRank(s, MlDef("Mausoleum Wanderer")) < m);
    CHECK(p.CastOrderRank(s, MlDef(kSouls)) < m);
    CHECK(p.CastOrderRank(s, MlDef("Metallic Mimic")) < m);
    CHECK(p.CastOrderRank(s, MlDef("Aether Vial")) < m);
}

// ---- same-turn affinity: findable and paid in BOTH worlds ---------------------------------------------

TEST_CASE("Millicent SAME TURN: Lingering Souls' two tokens discount her -- Souls + Millicent on FIVE lands, both worlds")
{
    // Three Spirits out. Souls {2}{W} then Millicent {5}{W}{U} - 5 = {W}{U}: 5 lands. Priced at the plan's start
    // she is {2}{W}{U}: 3 + 4 = 7 -> the line exists only with the token credit.
    GameState s = MlBoard({ "Hallowed Fountain", "Plains", "Plains", "Island", "Plains" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Mausoleum Wanderer", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].hand.push_back(MlZone(kSouls, 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = MlFindPlan(plans, {{Action::Kind::CastFromHand, kSouls},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(both != nullptr);
    MlCheckBothWorlds(s, *both, 2, 5);
}

TEST_CASE("Millicent SAME TURN: a Spirit CAST this main discounts her -- Wanderer {U} + Millicent on FOUR lands, both worlds")
{
    // Three Spirits out: Wanderer {U} then Millicent - 4 = {1}{W}{U}: 4 lands; board-only she is {2}{W}{U} (5).
    GameState s = MlBoard({ "Hallowed Fountain", "Plains", "Island", "Plains" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Drogskol Captain", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].hand.push_back(MlZone("Mausoleum Wanderer", 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = MlFindPlan(plans, {{Action::Kind::CastFromHand, "Mausoleum Wanderer"},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(both != nullptr);
    MlCheckBothWorlds(s, *both, 0, 4);
}

TEST_CASE("Millicent SAME TURN: an Aether Vial PUT discounts her -- Vial(2) Shacklegeist + Millicent on THREE lands")
{
    // Three Spirits out + the put Shacklegeist: Millicent {1}{W}{U} = 3 lands; board-only {2}{W}{U} (4).
    GameState s = MlBoard({ "Hallowed Fountain", "Plains", "Island" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Drogskol Captain", 2);
    MlPut(s, "Mausoleum Wanderer", 3);
    const int vi = MlPut(s, "Aether Vial", 7);
    s.battlefield[vi].charge_counters = 2;
    s.players[0].hand.push_back(MlZone("Shacklegeist", 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = MlFindPlan(plans, {{Action::Kind::ActivateVial, "Shacklegeist"},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(both != nullptr);
    MlCheckBothWorlds(s, *both, 0, 3);
}

TEST_CASE("Millicent SAME TURN: a Metallic Mimic cast first names Spirit and discounts her (Mimic {2} + Millicent on SIX lands)")
{
    // Two Spirits out: Mimic {2}, then Millicent - 3 = {2}{W}{U}: 6 lands; board-only {3}{W}{U} -> 2 + 5 = 7.
    GameState s = MlBoard({ "Hallowed Fountain", "Plains", "Island", "Plains", "Plains", "Island" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Drogskol Captain", 2);
    s.players[0].hand.push_back(MlZone("Metallic Mimic", 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = MlFindPlan(plans, {{Action::Kind::CastFromHand, "Metallic Mimic"},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(both != nullptr);
    MlCheckBothWorlds(s, *both, 0, 6);
}

TEST_CASE("Millicent same-turn credit is not a free lunch: FOUR lands cannot pay Souls + Millicent")
{
    GameState s = MlBoard({ "Hallowed Fountain", "Plains", "Island", "Plains" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Mausoleum Wanderer", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].hand.push_back(MlZone(kSouls, 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(MlFindPlan(plans, {{Action::Kind::CastFromHand, kSouls}, {Action::Kind::CastFromHand, kMill}}) == nullptr);
    CHECK(MlFindPlan(plans, {{Action::Kind::CastFromHand, kMill}}) != nullptr);   // {2}{W}{U} alone
}

// ---- the trigger: combat damage --------------------------------------------------------------------------

TEST_CASE("Millicent combat damage: ONE token per connecting NONTOKEN Spirit (not per damage; Souls tokens and the Angel excluded)")
{
    // ResolveCombatDamage is the ONE combat core GameEngine::CombatPhase and TurnSolver's rollout combat call.
    GameState s = MlBoard({});
    std::vector<int> atk;
    atk.push_back(MlPut(s, kMill, 1));                   // 4 power, a nontoken Spirit -> 1
    atk.push_back(MlPut(s, "Supreme Phantom", 2));       // 1 + Phantom-excl. -> 1
    atk.push_back(MlPut(s, "Shacklegeist", 3));          // 2 (+1 Phantom) -> 1
    atk.push_back(MlPut(s, "Metallic Mimic", 4));        // printed Shapeshifter (put, no ETB choice): not a Spirit -> 0
    ApplyCastCreatesTokens(s, 0, MlDef(kSouls));         // two Spirit TOKENS attack too -> 0
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].is_token) { atk.push_back(i); } }
    const int before_tokens = MlSpiritTokens(s);
    const int before_life   = s.players[1].life;
    ResolveCombatDamage(s, atk, 0, /*collect_descs=*/false);
    CHECK(before_life - s.players[1].life > 0);
    CHECK(MlSpiritTokens(s) - before_tokens == 3);
    // The new tokens: 1/1 white flying Spirits, summoning-sick (they did not join this combat).
    int fresh = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token || !p.entered_this_turn) { continue; }
        ++fresh;
        CHECK(CardHasSubtype(p.card, "Spirit"));
        CHECK(p.card.HasKeyword(Keyword::Flying));
        CHECK(p.card.m_power.value_or(0) == 1);
        CHECK(p.card.m_toughness.value_or(0) == 1);
    }
    CHECK(fresh >= 3);
}

TEST_CASE("Millicent combat damage: two Millicent-less boards make none; Millicent alone makes one")
{
    GameState s = MlBoard({});
    std::vector<int> atk{ MlPut(s, "Supreme Phantom", 2), MlPut(s, "Shacklegeist", 3) };
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(MlSpiritTokens(s) == 0);

    GameState m = MlBoard({});
    std::vector<int> a2{ MlPut(m, kMill, 1) };
    ResolveCombatDamage(m, a2, 0, false);
    CHECK(MlSpiritTokens(m) == 1);
    // The token arrives after damage: next turn it is a Spirit for the affinity count / lords.
    CHECK(EffectiveSpellCost(MlDef("Millicent, Restless Revenant"), m).generic == 5 - 2);
}

TEST_CASE("Millicent + Geist of Saint Traft: the Geist connects (one token); its Angel token is not a Spirit")
{
    GameState s = MlBoard({});
    MlPut(s, kMill, 1, true);   // sick: not attacking, still watching
    std::vector<int> atk{ MlPut(s, "Geist of Saint Traft", 2) };
    std::vector<int> eoc;
    const int tok_start = FireAttackCreateTokens(s, 0, &atk, &eoc);
    for (int i = tok_start; i < static_cast<int>(s.battlefield.size()); ++i) { atk.push_back(i); }
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(MlSpiritTokens(s) == 1);
}

// ---- the trigger: dies -----------------------------------------------------------------------------------

TEST_CASE("Millicent dies watch: the legend rule's second Geist makes a token; a Souls token dying does not")
{
    GameState s = MlBoard({});
    MlPut(s, kMill, 1);
    MlPut(s, "Geist of Saint Traft", 2);
    PutCardOntoBattlefield(s, 0, MlZone("Geist of Saint Traft", 3), "test");
    EnforceLegendRule(s, 0);
    int geists = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Geist of Saint Traft") { ++geists; } }
    CHECK(geists == 1);
    CHECK(MlSpiritTokens(s) == 1);

    // A Spirit TOKEN dying: no trigger ("nontoken").
    const int before = MlSpiritTokens(s);
    OnCreatureDies(s, 0, s.battlefield.back().card, /*dead_was_token=*/true, 0, nullptr, 0);
    CHECK(MlSpiritTokens(s) == before);
    // A nontoken Spirit dying: one token. Millicent herself dying: one token (the self block).
    OnCreatureDies(s, 0, MlDef("Supreme Phantom").card, false, 0, nullptr, 0);
    CHECK(MlSpiritTokens(s) == before + 1);
    const int mi = MlIndex(s, kMill);
    REQUIRE(mi >= 0);
    const Card mc = s.battlefield[mi].card;
    s.battlefield.erase(s.battlefield.begin() + mi);
    OnCreatureDies(s, 0, mc, false, 0, nullptr, 0);
    CHECK(MlSpiritTokens(s) == before + 2);
    // ...and with her gone, nothing watches any more.
    OnCreatureDies(s, 0, MlDef("Supreme Phantom").card, false, 0, nullptr, 0);
    CHECK(MlSpiritTokens(s) == before + 2);
}

TEST_CASE("Millicent DeathOfWouldPay: a nontoken Spirit victim pays, a Spirit token does not")
{
    GameState s = MlBoard({});
    MlPut(s, kMill, 1);
    const int ph = MlPut(s, "Supreme Phantom", 2);
    ApplyCastCreatesTokens(s, 0, MlDef(kSouls));
    CHECK(DeathOfWouldPay(s, 0, s.battlefield[ph]));
    CHECK_FALSE(DeathOfWouldPay(s, 0, s.battlefield.back()));
}

// ---- graveyard casts ride the cast sequence (integrator E2; graveyard-cast-search-holes-deferred.md section 3) ----

TEST_CASE("GyCastRidesCastSequence: a flashback Souls rides; a breakpoint class or a graveyard reader trails")
{
    GameState s = MlBoard({});
    auto gy = [&](const char* name) {
        Action a;
        a.kind      = Action::Kind::CastFromGraveyard;
        a.card_name = name;
        a.def       = &MlDef(name);
        return a;
    };
    CHECK(GyCastRidesCastSequence(s, gy(kSouls)));                // a token maker: rides at its rank
    CHECK_FALSE(GyCastRidesCastSequence(s, gy("Faithless Looting")));   // draws: a breakpoint class, trails
    CHECK_FALSE(GyCastRidesCastSequence(s, gy("Mystic Retrieval")));    // reads the graveyard: trails
    CHECK_FALSE(GyCastRidesCastSequence(s, gy("Throes of Chaos")));     // retrace + cascade: trails
    Action hand = gy(kSouls);
    hand.kind = Action::Kind::CastFromHand;
    CHECK_FALSE(GyCastRidesCastSequence(s, hand));                // only graveyard casts are asked
}

TEST_CASE("ApplyGraveyardCastDependencyOrder: a PENDING flashback follows the hand cast that puts its copy there")
{
    GameState s = MlBoard({});   // empty graveyard
    std::vector<Action> acts(3);
    acts[0].kind = Action::Kind::CastFromGraveyard; acts[0].card_name = kSouls; acts[0].def = &MlDef(kSouls);
    acts[1].kind = Action::Kind::CastFromHand;      acts[1].card_name = kMill;  acts[1].def = &MlDef(kMill);
    acts[2].kind = Action::Kind::CastFromHand;      acts[2].card_name = kSouls; acts[2].def = &MlDef(kSouls);
    std::vector<int> order{ 0, 1, 2 };
    ApplyGraveyardCastDependencyOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 1, 2, 0 });
    // A copy already in the graveyard: nothing moves.
    s.players[0].graveyard.push_back(MlZone(kSouls, 40));
    order = { 0, 1, 2 };
    ApplyGraveyardCastDependencyOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 0, 1, 2 });
}

TEST_CASE("Millicent SAME TURN: FLASHBACK Souls first discounts her -- flashback {1}{B} + Millicent {W}{U} on FOUR lands, both worlds")
{
    // Three Spirits out, Souls in the GRAVEYARD. Flashback {1}{B} (two Spirit tokens), then Millicent {5}{W}{U} - 5
    // = {W}{U}: four lands. Millicent first is {2}{W}{U} and leaves nothing for the flashback, so the pair exists
    // only with the graveyard cast at its rank (20, before the AFFINITY tier 25) in BOTH worlds and credited by
    // AffinityFeedCount. Before the fix both worlds cast it in the trailing pass and the enumerator did not
    // credit it: the pair was unenumerable at any budget.
    GameState s = MlBoard({ "Godless Shrine", "Hallowed Fountain", "Island", "Plains" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Mausoleum Wanderer", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].graveyard.push_back(MlZone(kSouls, 40));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = MlFindPlan(plans, {{Action::Kind::CastFromGraveyard, kSouls},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(both != nullptr);
    MlCheckBothWorlds(s, *both, 2, 4);
}

TEST_CASE("Millicent after a flashback is not a free lunch: THREE lands cannot pay flashback Souls + Millicent")
{
    GameState s = MlBoard({ "Godless Shrine", "Hallowed Fountain", "Island" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Mausoleum Wanderer", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].graveyard.push_back(MlZone(kSouls, 40));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(MlFindPlan(plans, {{Action::Kind::CastFromGraveyard, kSouls}, {Action::Kind::CastFromHand, kMill}}) == nullptr);
}

TEST_CASE("Millicent SAME TURN: hand Souls + its PENDING flashback + Millicent on SEVEN lands -- the flashback still follows its own hand cast")
{
    // Three Spirits out, Souls and Millicent in hand, empty graveyard. Souls {2}{W} (2 tokens), its flashback
    // {1}{B} (2 more), Millicent {5}{W}{U} - 7 = {W}{U}: 3 + 2 + 2 = 7 lands. The pending flashback ranks with
    // its own hand cast (20) and stays after it; both precede Millicent (25).
    GameState s = MlBoard({ "Godless Shrine", "Hallowed Fountain", "Island", "Plains", "Plains", "Plains", "Swamp" });
    MlPut(s, "Supreme Phantom", 1);
    MlPut(s, "Mausoleum Wanderer", 2);
    MlPut(s, "Shacklegeist", 3);
    s.players[0].hand.push_back(MlZone(kSouls, 30));
    s.players[0].hand.push_back(MlZone(kMill, 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* all3 = MlFindPlan(plans, {{Action::Kind::CastFromHand, kSouls},
                                                      {Action::Kind::CastFromGraveyard, kSouls},
                                                      {Action::Kind::CastFromHand, kMill}});
    REQUIRE(all3 != nullptr);
    MlCheckBothWorlds(s, *all3, 4, 7);
}
