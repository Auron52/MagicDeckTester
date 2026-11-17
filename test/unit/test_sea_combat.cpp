// Sea Creatures I2 (2026-11-17): the combat draft (logs/sea-research/combat.md) + the shared bounce helper.
//   * Spawning Kraken: combat_damage_tokens_per_trigger 1 + combat_damage_watch_subtypes Kraken/Leviathan/Octopus/
//     Serpent in the shared FireCombatDamageTokens (ResolveCombatDamage -- the ONE combat core GameEngine::CombatPhase
//     and the rollout call), one 9/9 blue Kraken per (Kraken, connecting sea creature) pair.
//   * Koma, World-Eater: combat_damage_watch_self (only Koma's own connection) + combat_damage_token_name "Koma's
//     Coil" (BuildTokenProto / CreateTokens' printed name).
//   * Serpent of Yawning Depths (Enchantment Creature, evasion inert), Lorthos (recorded {8} param, always declined).
//   * Whelming Wave: PerformBounceAllCreaturesExcept over ReturnPermanentToOwnersHand (owner's hand, a token / spawn
//     ceases, Auras swept, equipment unattached) -- executor (EffectHandler) and rollout (ApplyPlan) identical; the
//     Sea provider never casts it (MTG_SEA_WAVE_NEVER, USER 2026-11-16).
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
const char* const kKraken  = "Spawning Kraken";
const char* const kKoma    = "Koma, World-Eater";
const char* const kSerpent = "Serpent of Yawning Depths";
const char* const kLorthos = "Lorthos, the Tidemaker";
const char* const kWave    = "Whelming Wave";

const CardDefinition& ScDef(const std::string& name)
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

const SeaCreaturesProvider& ScProv()
{
    static const SeaCreaturesProvider p;
    return p;
}

GameState ScBoard(int turn = 5)
{
    GameState s;
    s.m_provider          = &ScProv();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.game_seed           = 777;
    return s;
}

int ScPut(GameState& s, const std::string& name, int number, int controller = 0, int owner = -1)
{
    Permanent p;
    p.card              = ScDef(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = owner < 0 ? controller : owner;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A scheduled opponent spawn: a "N/N Creature" card with no subtypes, numbered from kFirstOppSpawnNumber, is_token
// NOT set (GameEngine's spawn shape).
int ScSpawn(GameState& s, int k, int controller = 1)
{
    Permanent p;
    p.card.m_name = "3/3 Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = 3;
    p.card.m_toughness = 3;
    p.card.m_number    = GameState::kFirstOppSpawnNumber + k;
    p.controller_index = controller;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int ScCount(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool ScInHand(const GameState& s, int player, int number)
{
    for (const Card& c : s.players[player].hand) { if (c.m_number == number) { return true; } }
    return false;
}

bool ScOnBf(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}

struct ScArm
{
    int slot;
    std::int8_t prev;
    ScArm(int sl, int v) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ScArm() { heurarm::t_arm[slot] = prev; }
};
}  // namespace

TEST_CASE("Sea I2: the cards -- costs, types, params")
{
    const CardDefinition& kr = ScDef(kKraken);
    CHECK(kr.card.m_mana_cost.ManaValue() == 6);
    CHECK(CardHasSubtype(kr.card, "Kraken"));
    CHECK(kr.params.combat_damage_tokens_per_trigger == 1);
    CHECK(kr.params.combat_damage_watch_subtypes.size() == 4);
    CHECK(!kr.params.combat_damage_watch_self);

    const CardDefinition& ko = ScDef(kKoma);
    CHECK(ko.card.m_mana_cost.ManaValue() == 7);
    CHECK(ko.params.combat_damage_watch_self);
    CHECK(ko.params.combat_damage_tokens_per_trigger == 4);
    CHECK(ko.params.combat_damage_token_name == "Koma's Coil");
    CHECK(ko.params.cant_be_countered);

    const CardDefinition& se = ScDef(kSerpent);
    CHECK(se.card.IsCreature());
    CHECK(se.card.HasType(CardType::Enchantment));
    CHECK(CardHasSubtype(se.card, "Serpent"));

    const CardDefinition& lo = ScDef(kLorthos);
    CHECK(lo.card.m_mana_cost.ManaValue() == 8);
    CHECK(CardHasSubtype(lo.card, "Octopus"));
    CHECK(lo.params.attack_may_pay_tap_permanents == 8);

    const CardDefinition& wv = ScDef(kWave);
    CHECK(wv.card.m_mana_cost.ManaValue() == 4);
    CHECK(wv.params.bounce_all_creatures);
    CHECK(wv.params.bounce_all_creatures_except_subtypes.size() == 4);
}

TEST_CASE("Sea I2: Spawning Kraken alone connecting makes ONE 9/9 blue Kraken, untapped and sick")
{
    GameState s = ScBoard();
    std::vector<int> atk{ ScPut(s, kKraken, 1) };
    ResolveCombatDamage(s, atk, 0, /*collect_descs=*/false);
    CHECK(s.players[1].life == 30 - 6);
    REQUIRE(ScCount(s, "9/9 Kraken Token") == 1);
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK(p.entered_this_turn);
        CHECK(!p.tapped);
        CHECK(CardHasSubtype(p.card, "Kraken"));
        CHECK(p.card.HasColor(Color::Blue));
        CHECK(p.card.m_power.value_or(0) == 9);
    }
}

TEST_CASE("Sea I2: two Krakens + Koma + Serpent connect -> 8 Krakens and exactly 4 Koma's Coil")
{
    GameState s = ScBoard();
    std::vector<int> atk{ ScPut(s, kKraken, 1), ScPut(s, kKraken, 2), ScPut(s, kKoma, 3), ScPut(s, kSerpent, 4) };
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(s.players[1].life == 30 - (6 + 6 + 8 + 6));
    CHECK(ScCount(s, "9/9 Kraken Token") == 8);   // 2 watchers x 4 connecting sea creatures
    CHECK(ScCount(s, "Koma's Coil") == 4);
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_name.str() != "Koma's Coil") { continue; }
        CHECK(p.is_token);
        CHECK(CardHasSubtype(p.card, "Serpent"));
        CHECK(p.card.HasColor(Color::Blue));
        CHECK(p.card.m_power.value_or(0) == 3);
        CHECK(p.card.m_toughness.value_or(0) == 3);
        CHECK(p.def_absent);
    }
}

TEST_CASE("Sea I2: Koma is SELF-scoped -- a connecting Coil makes no Coils but feeds every Spawning Kraken")
{
    GameState s = ScBoard();
    ScPut(s, kKoma, 3);                                                  // on the battlefield, NOT attacking
    ScPut(s, kKraken, 1);                                                // not attacking either
    CreateTokens(s, 0, 1, 3, 3, { "Serpent" }, "U", {}, "Koma's Coil");  // one Coil from an earlier turn
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].is_token) { s.battlefield[i].entered_this_turn = false; atk.push_back(i); } }
    REQUIRE(atk.size() == 1);
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(ScCount(s, "Koma's Coil") == 1);         // no new Coils
    CHECK(ScCount(s, "9/9 Kraken Token") == 1);    // the Coil is a Serpent: +1 Kraken

    // A stolen opponent spawn (no subtypes) connecting with a Spawning Kraken out makes nothing.
    GameState t = ScBoard();
    ScPut(t, kKraken, 1);
    std::vector<int> a2{ ScSpawn(t, 0, /*controller=*/0) };
    ResolveCombatDamage(t, a2, 0, false);
    CHECK(ScCount(t, "9/9 Kraken Token") == 0);
}

TEST_CASE("Sea I2: Lorthos attacks -- the {8} trigger is declined, nothing is tapped")
{
    GameState s = ScBoard();
    for (int i = 0; i < 8; ++i) { ScPut(s, "Island", 10 + i); }
    ScSpawn(s, 0);
    std::vector<int> atk{ ScPut(s, kLorthos, 1) };
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(s.players[1].life == 30 - 8);
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_name.str() == "Island" || p.controller_index == 1) { CHECK(!p.tapped); }
    }
}

TEST_CASE("Sea I2: CreateTokens' printed name is the only difference from the generated token")
{
    GameState s = ScBoard();
    CreateTokens(s, 0, 1, 3, 3, { "Serpent" }, "U");
    CreateTokens(s, 0, 1, 3, 3, { "Serpent" }, "U", {}, "Koma's Coil");
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.battlefield[0].card.m_name.str() == "3/3 Serpent Token");
    CHECK(s.battlefield[1].card.m_name.str() == "Koma's Coil");
    CHECK(CardHasSubtype(s.battlefield[1].card, "Serpent"));
}

TEST_CASE("Sea I2: ReturnPermanentToOwnersHand -- owner's hand, a token / spawn ceases, revolt, detach")
{
    GameState s = ScBoard();
    const int elves = ScPut(s, "Llanowar Elves", 41);                 // ours
    const int stolen = ScPut(s, "Llanowar Elves", 42, 0, /*owner=*/1);  // ours to control, theirs to own
    ScSpawn(s, 0);
    Permanent& gr = s.battlefield[static_cast<std::size_t>(ScPut(s, "Lightning Greaves", 43))];
    gr.equipped_to = 41;
    (void)elves; (void)stolen;
    CHECK(!s.permanent_left_under_control_this_turn);
    REQUIRE(ReturnPermanentToOwnersHand(s, 0, "Test"));
    CHECK(ScInHand(s, 0, 41));
    CHECK(s.permanent_left_under_control_this_turn);
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 43) { CHECK(p.equipped_to == 0); } }
    REQUIRE(ReturnPermanentToOwnersHand(s, 0, "Test"));   // the stolen Elves
    CHECK(ScInHand(s, 1, 42));
    CHECK(!ScInHand(s, 0, 42));
    const std::size_t opp_hand = s.players[1].hand.size();
    REQUIRE(ReturnPermanentToOwnersHand(s, 0, "Test"));   // the spawn: a token, it ceases to exist
    CHECK(s.players[1].hand.size() == opp_hand);
    CHECK(!ScOnBf(s, GameState::kFirstOppSpawnNumber));
    CHECK(!ReturnPermanentToOwnersHand(s, 99, "Test"));
}

// The Wave board: our sea creatures + Koma's Coil stay; a non-sea creature of ours (with an Aura) returns to our
// hand; a stolen spawn and two opponent spawns cease; a stolen opponent CARD returns to the opponent's hand.
static GameState WaveBoard()
{
    GameState s = ScBoard();
    for (int i = 0; i < 4; ++i) { ScPut(s, "Island", 10 + i); }
    ScPut(s, kKraken, 1);
    ScPut(s, kSerpent, 2);
    CreateTokens(s, 0, 1, 3, 3, { "Serpent" }, "U", {}, "Koma's Coil");
    ScPut(s, "Llanowar Elves", 41);
    Permanent& aura = s.battlefield[static_cast<std::size_t>(ScPut(s, "Rancor", 44))];
    aura.aura_attached_to = 41;
    ScPut(s, "Llanowar Elves", 42, 0, /*owner=*/1);
    ScSpawn(s, 0);
    ScSpawn(s, 1);
    ScSpawn(s, 2, /*controller=*/0);
    return s;
}

static void CheckWaveResult(const GameState& s)
{
    CHECK(ScOnBf(s, 1));
    CHECK(ScOnBf(s, 2));
    CHECK(ScCount(s, "Koma's Coil") == 1);
    CHECK(!ScOnBf(s, 41));
    CHECK(ScInHand(s, 0, 41));
    CHECK(!ScOnBf(s, 44));                                         // Rancor fell off (CR 704.5m)
    CHECK(!ScOnBf(s, 42));
    CHECK(ScInHand(s, 1, 42));                                      // owner's hand
    CHECK(s.players[1].hand.size() == 1);                           // spawns cease, never reach a hand
    for (int k = 0; k < 3; ++k) { CHECK(!ScOnBf(s, GameState::kFirstOppSpawnNumber + k)); }
    CHECK(ScCount(s, "Island") == 4);
}

TEST_CASE("Sea I2: Whelming Wave -- the shared helper and the executor's resolution")
{
    GameState h = WaveBoard();
    CHECK(PerformBounceAllCreaturesExcept(h, kWave, ScDef(kWave).params.bounce_all_creatures_except_subtypes) == 5);
    CheckWaveResult(h);

    GameState ex = WaveBoard();
    const CardDefinition& def = ScDef(kWave);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = 4343;
    e.controller_index = 0;
    EffectHandler::Resolve(ex, e, def);
    CheckWaveResult(ex);
}

TEST_CASE("Sea I2: Whelming Wave -- never enumerated under the Sea provider; =0 searches it; the rollout matches")
{
    GameState s = WaveBoard();
    s.players[0].hand.push_back(ScDef(kWave).card);
    s.players[0].hand.back().m_number = 4343;
    const auto casts_wave = [](const TurnSolver::Plan& p) {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == kWave) { return true; } }
        return false;
    };
    {
        ScArm on(heurarm::SEA_WAVE_NEVER, 1);
        bool any = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { any = any || casts_wave(p); }
        // MTG_HUMAN_PLAY / MTG_UNPRUNED are process-static: assert the contract in whichever process runs this.
        if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::SeaWave)) { CHECK(any); }
        else { CHECK(!any); }
    }
    {
        ScArm off(heurarm::SEA_WAVE_NEVER, 0);
        bool any = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (!casts_wave(p)) { continue; }
            any = true;
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            CheckWaveResult(t);
            break;
        }
        CHECK(any);
    }
}
