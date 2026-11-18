// Sea Creatures I3 (2026-11-17): the Sagas draft (logs/sea-research/sagas.md) on the EXISTING Saga engine
// (World War Hulk; docs/design/saga-world-war-hulk.md).
//   * Kiora Bests the Sea God: I = an 8/8 blue Kraken token with hexproof (CreateToken); II = tap the opponent's
//     nonland permanents (the don't-untap rider deferred: skip_next_untap is NOT set); III = gain control of a
//     target opponent permanent and untap it (searched rank pin over a provider-restricted list; sound folds).
//   * Summon: Leviathan: I = the shared PerformBounceAllCreaturesExcept (Merfolk added); II, III = an until-EOT
//     attack-draw INSTANCE (GameState::eot_attack_draw_n) read by the shared ApplyAttackDrawTriggers; a creature
//     Saga's sacrifice goes through SacrificePermanentAt; the per-turn post-combat main
//     (SeaCreaturesProvider::RestrictedSecondMainLive).
//   * Lore may be 0 (Sin): the next advance re-fires chapter I and does not sacrifice.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kKiora = "Kiora Bests the Sea God";
const char* const kLev   = "Summon: Leviathan";
const char* const kSK    = "Spawning Kraken";
const char* const kHulk  = "World War Hulk";
const char* const kBody  = "Venerable Knight";   // a plain non-sea creature card (no params, no keywords)

const CardDefinition& SgDef(const std::string& name)
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

const SeaCreaturesProvider& SgProv()
{
    static const SeaCreaturesProvider p;
    return p;
}

GameState SgBoard(int turn = 5)
{
    GameState s;
    s.m_provider          = &SgProv();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.game_seed           = 777;
    return s;
}

int SgPut(GameState& s, const std::string& name, int number, int controller = 0, int owner = -1, int lore = 0)
{
    Permanent p;
    p.card             = SgDef(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = owner < 0 ? controller : owner;
    p.lore_counters    = lore;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int SgSpawn(GameState& s, int k, int power, int toughness)
{
    Permanent p;
    p.card.m_name = std::to_string(power) + "/" + std::to_string(toughness) + " Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = power;
    p.card.m_toughness = toughness;
    p.card.m_number    = GameState::kFirstOppSpawnNumber + k;
    p.controller_index = 1;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void SgLibrary(GameState& s, int n)
{
    for (int i = 0; i < n; ++i)
    {
        Card c = SgDef("Island").card;
        c.m_number = 9000 + i;
        s.players[0].library.push_back(c);
    }
}

const Permanent* SgFind(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int SgTokens(const GameState& s, const std::string& sub)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && CardHasSubtype(p.card, sub)) { ++n; } }
    return n;
}

bool SgInGy(const GameState& s, int player, int number)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

bool SgFoldsLive() { return !HumanPlayActive() && !DecisionUnpruned(UnprunedGate::SagaSteal); }
}  // namespace

TEST_CASE("Sea I3: the cards -- costs, types, params")
{
    const CardDefinition& k = SgDef(kKiora);
    CHECK(k.card.m_mana_cost.ManaValue() == 7);
    CHECK(!k.card.IsCreature());
    CHECK(CardHasSubtype(k.card, "Saga"));
    CHECK(k.params.saga_chapters == 3);
    CHECK(k.params.saga_ch1_token_power == 8);
    CHECK(k.params.saga_ch1_token_toughness == 8);
    CHECK(k.params.saga_ch2_tap_opp_nonland);
    CHECK(k.params.saga_ch3_gain_control_opp_permanent);
    CHECK(k.params.saga_eot_attack_draw_chapter_mask == 0);

    const CardDefinition& l = SgDef(kLev);
    CHECK(l.card.m_mana_cost.ManaValue() == 6);
    CHECK(l.card.IsCreature());
    CHECK(CardHasSubtype(l.card, "Saga"));
    CHECK(CardHasSubtype(l.card, "Leviathan"));
    CHECK(l.card.HasKeyword(Keyword::Ward));
    CHECK(l.params.saga_chapters == 3);
    CHECK(l.params.saga_eot_attack_draw_chapter_mask == ((1u << 2) | (1u << 3)));
    CHECK(l.params.saga_ch1_bounce_creatures_except_subtypes.size() == 5);
    CHECK(std::find(l.params.saga_ch1_bounce_creatures_except_subtypes.begin(),
                    l.params.saga_ch1_bounce_creatures_except_subtypes.end(), "Merfolk")
          != l.params.saga_ch1_bounce_creatures_except_subtypes.end());
}

TEST_CASE("Sea I3: Kiora I on the executor's cast -- an 8/8 blue hexproof Kraken token, lore 1")
{
    GameState s = SgBoard();
    const CardDefinition& def = SgDef(kKiora);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = 4242;
    e.controller_index = 0;
    EffectHandler::Resolve(s, e, def);
    const Permanent* kiora = SgFind(s, 4242);
    REQUIRE(kiora != nullptr);
    CHECK(kiora->lore_counters == 1);
    REQUIRE(SgTokens(s, "Kraken") == 1);
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK(p.EffectivePower() == 8);
        CHECK(p.EffectiveToughness() == 8);
        CHECK(p.card.HasColor(Color::Blue));
        CHECK(p.card.HasKeyword(Keyword::Hexproof));
        CHECK(p.controller_index == 0);
    }
}

TEST_CASE("Sea I3: Kiora II taps the opponent's nonland permanents and never sets skip_next_untap")
{
    GameState s = SgBoard();
    SgPut(s, kKiora, 4242, 0, -1, /*lore=*/1);
    SgSpawn(s, 0, 4, 4);
    SgSpawn(s, 1, 2, 2);
    const int land = SgPut(s, "Island", 77, 1);
    AdvanceSagas(s);
    CHECK(SgFind(s, 4242)->lore_counters == 2);
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != 1) { continue; }
        CHECK(p.tapped == !p.card.IsLand());
        CHECK_FALSE(p.skip_next_untap);
    }
    CHECK_FALSE(s.battlefield[static_cast<std::size_t>(land)].tapped);
}

TEST_CASE("Sea I3: Kiora III steals the biggest permanent, untaps it, owner kept; Kiora is sacrificed")
{
    GameState s = SgBoard();
    SgPut(s, kKiora, 4242, 0, -1, /*lore=*/2);
    SgSpawn(s, 0, 2, 2);
    SgSpawn(s, 1, 4, 4);
    for (Permanent& p : s.battlefield) { if (p.controller_index == 1) { p.tapped = true; } }
    AdvanceSagas(s);
    const Permanent* got = SgFind(s, GameState::kFirstOppSpawnNumber + 1);
    REQUIRE(got != nullptr);
    CHECK(got->controller_index == 0);
    CHECK(got->owner_index == 1);
    CHECK_FALSE(got->tapped);
    CHECK(got->gained_control_this_turn);
    CHECK(SgFind(s, GameState::kFirstOppSpawnNumber)->controller_index == 1);
    CHECK(SgFind(s, 4242) == nullptr);
    CHECK(SgInGy(s, 0, 4242));
}

TEST_CASE("Sea I3: Kiora III with no legal target does nothing; Kiora is still sacrificed")
{
    GameState s = SgBoard();
    SgPut(s, kKiora, 4242, 0, -1, /*lore=*/2);
    AdvanceSagas(s);
    CHECK(SgFind(s, 4242) == nullptr);
    CHECK(SgInGy(s, 0, 4242));
}

TEST_CASE("Sea I3: Kiora III's steal list -- engine order, sound folds, the rank pin, human list unrestricted")
{
    GameState s = SgBoard();
    SgSpawn(s, 0, 1, 1);
    SgSpawn(s, 1, 6, 6);
    SgSpawn(s, 2, 6, 6);
    SgSpawn(s, 3, 2, 7);   // not dominated by the 6/6s (toughness)
    const std::vector<int> all = SagaStealCandidates(s, 0);
    REQUIRE(all.size() == 4);
    CHECK(s.battlefield[static_cast<std::size_t>(all[0])].EffectivePower() == 6);
    CHECK(s.battlefield[static_cast<std::size_t>(all[0])].card.m_number == GameState::kFirstOppSpawnNumber + 1);
    const std::vector<int> srch = SagaStealSearchCandidates(s, 0);
    if (SgFoldsLive())
    {
        REQUIRE(srch.size() == 2);   // one 6/6 (identity), the 2/7; the 1/1 is dominated
        CHECK(srch[0] == all[0]);
        CHECK(s.battlefield[static_cast<std::size_t>(srch[1])].EffectiveToughness() == 7);
    }
    else { CHECK(srch.size() == 4); }

    // The pin indexes the restricted list and clamps (duplicate-not-whiff): rank 9 -> the last candidate.
    SgPut(s, kKiora, 4242, 0, -1, /*lore=*/2);
    s.scripted_saga_target = 9;
    const int last = srch.back() < 0 ? -1 : s.battlefield[static_cast<std::size_t>(srch.back())].card.m_number;
    AdvanceSagas(s);
    CHECK(s.scripted_saga_target == -1);
    CHECK(SgFind(s, last)->controller_index == 0);
}

TEST_CASE("Sea I3: a lore-0 Saga (Sin) re-fires chapter I and is not sacrificed")
{
    GameState s = SgBoard();
    SgPut(s, kKiora, 4242, 0, -1, /*lore=*/0);
    AdvanceSagas(s);
    REQUIRE(SgFind(s, 4242) != nullptr);
    CHECK(SgFind(s, 4242)->lore_counters == 1);
    CHECK(SgTokens(s, "Kraken") == 1);
}

TEST_CASE("Sea I3: Summon: Leviathan I bounces non-sea creatures to their OWNERS (a stolen one goes back)")
{
    GameState s = SgBoard();
    SgSpawn(s, 0, 3, 3);
    SgPut(s, kSK, 501, 0);
    SgPut(s, kBody, 502, 0, /*owner=*/1);   // a creature we stole
    const CardDefinition& def = SgDef(kLev);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = 4343;
    e.controller_index = 0;
    EffectHandler::Resolve(s, e, def);
    REQUIRE(SgFind(s, 4343) != nullptr);
    CHECK(SgFind(s, 4343)->lore_counters == 1);
    CHECK(SgFind(s, 501) != nullptr);                              // a Kraken stays
    CHECK(SgFind(s, GameState::kFirstOppSpawnNumber) == nullptr);   // a spawn ceases
    CHECK(SgFind(s, 502) == nullptr);
    bool in_opp_hand = false;
    for (const Card& c : s.players[1].hand) { in_opp_hand = in_opp_hand || c.m_number == 502; }
    CHECK(in_opp_hand);
    CHECK(s.players[0].hand.empty());
}

TEST_CASE("Sea I3: Summon: Leviathan II/III -- per-instance draws for declared sea attackers; sacrificed at III")
{
    GameState s = SgBoard();
    SgLibrary(s, 10);
    const int lev = SgPut(s, kLev, 4343, 0, -1, /*lore=*/1);
    (void)lev;
    SgPut(s, kSK, 501, 0);
    SgPut(s, kBody, 502, 0);
    AdvanceSagas(s);   // chapter II
    CHECK(s.eot_attack_draw_n == 1);
    CHECK(SgProv().RestrictedSecondMainLive(s));
    CHECK(ProviderSecondMainLive(s));
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { atk.push_back(i); }
    const std::size_t h0 = s.players[0].hand.size();
    ApplyAttackDrawTriggers(s, 0, atk);
    CHECK(s.players[0].hand.size() == h0 + 2);   // Leviathan + Spawning Kraken; the Knight is not a sea creature

    // A creature put onto the battlefield attacking (past declared_n) did not attack: no draw (CR 508.4).
    const std::size_t h1 = s.players[0].hand.size();
    ApplyAttackDrawTriggers(s, 0, atk, /*declared_n=*/1);
    CHECK(s.players[0].hand.size() == h1 + 1);

    // Next turn: chapter III adds an instance and sacrifices the creature Saga through the death path.
    s.eot_attack_draw_n = 0;   // the untap reset (both worlds)
    s.creature_died_this_turn = false;
    AdvanceSagas(s);
    CHECK(s.eot_attack_draw_n == 1);
    CHECK(SgFind(s, 4343) == nullptr);
    CHECK(SgInGy(s, 0, 4343));
    CHECK(s.creature_died_this_turn);
    // The instance outlives the Saga: the Spawning Kraken still draws.
    std::vector<int> atk2;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { atk2.push_back(i); }
    const std::size_t h2 = s.players[0].hand.size();
    ApplyAttackDrawTriggers(s, 0, atk2);
    CHECK(s.players[0].hand.size() == h2 + 1);
}

TEST_CASE("Sea I3: two live instances draw two per attacker; an empty library loses")
{
    GameState s = SgBoard();
    SgLibrary(s, 3);
    SgPut(s, kSK, 501, 0);
    SgPut(s, kSK, 502, 0);
    s.eot_attack_draw_n = 2;
    ApplyAttackDrawTriggers(s, 0, { 0, 1 });
    CHECK(s.players[0].hand.size() == 3);
    CHECK(s.player_lost_on_draw);
}

TEST_CASE("Sea I3: no instance, no draw and no conditional second main")
{
    GameState s = SgBoard();
    SgLibrary(s, 3);
    SgPut(s, kSK, 501, 0);
    ApplyAttackDrawTriggers(s, 0, { 0 });
    CHECK(s.players[0].hand.empty());
    CHECK_FALSE(SgProv().RestrictedSecondMainLive(s));
    s.eot_attack_draw_n = 1;
    s.uses_second_main = true;   // a deck already playing a full main 2 is never re-gated
    CHECK_FALSE(ProviderSecondMainLive(s));
}

TEST_CASE("Sea I3: the rollout's cast drains chapter I like the executor (Kiora, Leviathan)")
{
    for (const char* name : { kKiora, kLev })
    {
        GameState s = SgBoard();
        for (int i = 0; i < 7; ++i) { SgPut(s, "Island", 100 + i, 0); }
        SgSpawn(s, 0, 3, 3);
        s.players[0].hand.push_back(SgDef(name).card);
        s.players[0].hand.back().m_number = 4444;
        bool cast = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            bool has = false;
            for (const Action& a : p.actions)
            { has = has || (a.kind == Action::Kind::CastFromHand && a.card_name == name); }
            if (!has) { continue; }
            cast = true;
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            REQUIRE(SgFind(t, 4444) != nullptr);
            CHECK(SgFind(t, 4444)->lore_counters == 1);
            if (std::string(name) == kKiora) { CHECK(SgTokens(t, "Kraken") == 1); }
            else { CHECK(SgFind(t, GameState::kFirstOppSpawnNumber) == nullptr); }
            break;
        }
        CHECK_MESSAGE(cast, name);
    }
}

TEST_CASE("Sea I3: World War Hulk's chapters are unchanged by the restructure")
{
    GameState s = SgBoard();
    SgPut(s, kHulk, 4545, 0, -1, /*lore=*/1);
    SgPut(s, kSK, 501, 0);
    AdvanceSagas(s);
    const Permanent* sk = SgFind(s, 501);
    REQUIRE(sk != nullptr);
    CHECK(sk->EffectivePower() == SgDef(kSK).card.m_power.value_or(0) + SgDef(kHulk).params.saga_ch2_counters_on_target);
    CHECK(s.eot_attack_draw_n == 0);
}

TEST_CASE("Sea Kiora III steal (MTG_SEA_STEAL_CREATURE): a creature at 8+ mana; lands stay searched below 8 or beside a 1/1")
{
    auto board = [](int forests, int spawn_power)
    {
        GameState s = SgBoard();
        for (int k = 0; k < forests; ++k) { SgPut(s, "Forest", 100 + k); }
        Permanent land;
        land.card = SgDef("Opponent Basic Land").card;
        land.card.m_number = 600000;
        land.controller_index = 1;
        land.owner_index = 1;
        s.battlefield.push_back(land);
        if (spawn_power > 0) { SgSpawn(s, 0, spawn_power, spawn_power); }
        return s;
    };
    auto has_land = [](const GameState& s, const std::vector<int>& c)
    {
        for (const int i : c) { if (s.battlefield[static_cast<std::size_t>(i)].card.IsLand()) { return true; } }
        return false;
    };
    if (!SgFoldsLive()) { return; }   // MTG_UNPRUNE=sagasteal: the full list by design
    heurarm::Arm saved = heurarm::t_arm;
    heurarm::t_arm[static_cast<int>(heurarm::SEA_STEAL_CREATURE)] = 1;
    {
        const GameState s = board(8, 2);
        const std::vector<int> c = SagaStealSearchCandidates(s, 0);
        CHECK(c.size() == 1);
        CHECK_FALSE(has_land(s, c));
    }
    { const GameState s = board(7, 2); CHECK(has_land(s, SagaStealSearchCandidates(s, 0))); }   // below 8 mana
    { const GameState s = board(8, 1); CHECK(SagaStealSearchCandidates(s, 0).size() == 2); }    // a 1/1: arguable
    { const GameState s = board(8, 0); CHECK(has_land(s, SagaStealSearchCandidates(s, 0))); }   // no creature
    heurarm::t_arm[static_cast<int>(heurarm::SEA_STEAL_CREATURE)] = 0;
    { const GameState s = board(8, 2); CHECK(SagaStealSearchCandidates(s, 0).size() == 2); }    // =0: root list
    heurarm::t_arm = saved;
}
