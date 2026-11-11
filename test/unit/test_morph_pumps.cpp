// Unit tests for the Morph deck's pump family (INT-1, 2026-11-11; ledger docs/design/analysis-Morph.md):
// Wild Defiance's target watcher, Berserk / Legion Leadership power doubling, Temur Battle Rage's double
// strike, Might of Old Krosa's main-phase "instead", kicked Vines fanned per target, Berserk's delayed
// destroy-if-attacked, Legion Stronghold entering tapped, the Spellskite redirect, and MorphProvider's
// routing + cast-order rank. Every payload runs through the ONE shared resolver (ResolveSoloTargetTrick ->
// ApplyTrickPayload) that the executor and the rollout both call, so a resolver test covers both worlds.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <string>

namespace
{

void EnsureCardsMorph()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefM(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardM(const std::string& name, int number)
{
    Card c     = DefM(name).card;
    c.m_number = number;
    return c;
}

// A ready (not summoning-sick) permanent of ours.
int PutM(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardM(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A vanilla 2/2 body (deck-agnostic, so the test does not depend on any creature entry).
int PutBear(GameState& s, int number)
{
    Card c;
    c.m_name = "Test Bear";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power     = 2;
    c.m_toughness = 2;
    c.m_number    = number;
    Permanent p;
    p.card             = c;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState BaseM()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int Power(const GameState& s, int i) { return CombatPowerOf(s.battlefield[static_cast<std::size_t>(i)], s); }

void Cast(GameState& s, const std::string& spell, int target_number, int redirect = 0)
{
    REQUIRE(ResolveSoloTargetTrick(s, 0, DefM(spell), target_number, 0, 0, redirect));
}

}  // namespace

TEST_CASE("morph pumps: Wild Defiance triggers once per copy, before the spell's own pump")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    PutM(s, "Wild Defiance", 20);
    PutM(s, "Wild Defiance", 21);
    Cast(s, "Might of Old Krosa", 10);
    // 2 + 3 + 3 (two Defiances) + 4 (main phase) = 12.
    CHECK(Power(s, b) == 12);
    CHECK(s.battlefield[static_cast<std::size_t>(b)].temp_tough_bonus == 10);
}

TEST_CASE("morph pumps: Might of Old Krosa is +4/+4 in a main phase, +2/+2 otherwise")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    Cast(s, "Might of Old Krosa", 10);
    CHECK(Power(s, b) == 6);
    GameState c = BaseM();
    c.phase = Phase::Combat;
    const int cb = PutBear(c, 10);
    Cast(c, "Might of Old Krosa", 10);
    CHECK(Power(c, cb) == 4);
}

TEST_CASE("morph pumps: doublers double the Defiance trigger and earlier pumps, not later ones")
{
    EnsureCardsMorph();
    {
        // Defiance + Berserk on a 2/2: the trigger resolves first, so X = 5 -> 10.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        PutM(s, "Wild Defiance", 20);
        Cast(s, "Berserk", 10);
        CHECK(Power(s, b) == 10);
    }
    {
        // Pump then double: (2 + 4) x 2 = 12.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Might of Old Krosa", 10);
        Cast(s, "Legion Leadership", 10);
        CHECK(Power(s, b) == 12);
    }
    {
        // Double then pump: 2 x 2 + 4 = 8 -- why the cast-order rank puts additive pumps first.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        Cast(s, "Might of Old Krosa", 10);
        CHECK(Power(s, b) == 8);
    }
    {
        // Two doublers commute: 2 -> 4 -> 8 either way.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        Cast(s, "Legion Leadership", 10);
        CHECK(Power(s, b) == 8);
    }
}

TEST_CASE("morph pumps: Temur Battle Rage grants double strike until end of turn")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    CHECK_FALSE(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(b)], s));
    Cast(s, "Temur Battle Rage", 10);
    CHECK(s.battlefield[static_cast<std::size_t>(b)].temp_double_strike);
    CHECK(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(b)], s));
}

TEST_CASE("morph pumps: Berserk destroys its target at the end step ONLY if it attacked")
{
    EnsureCardsMorph();
    {
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(b)].destroy_at_end_if_attacked));
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        CHECK(s.battlefield[static_cast<std::size_t>(b)].AttacksThisTurn() == 1);
        DestroyAttackedAtEndStep(s);
        CHECK(s.battlefield.empty());
        REQUIRE(s.players[0].graveyard.size() == 1);
        CHECK(s.players[0].graveyard[0].m_name.str() == "Test Bear");
    }
    {
        // Did not attack: survives, and the delayed trigger is spent.
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        DestroyAttackedAtEndStep(s);
        REQUIRE(s.battlefield.size() == 1);
        CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(b)].destroy_at_end_if_attacked));
    }
    {
        // Indestructible: attacked, but "destroy" does nothing.
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        s.battlefield[static_cast<std::size_t>(b)].temp_indestructible = true;
        Cast(s, "Berserk", 10);
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        DestroyAttackedAtEndStep(s);
        CHECK(s.battlefield.size() == 1);
    }
    {
        // Without the deck stamp the attack is not counted (every other deck byte-identical).
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        CHECK(s.battlefield[static_cast<std::size_t>(b)].AttacksThisTurn() == 0);
    }
}

TEST_CASE("morph pumps: Vines of Vastwood -- unkicked only targets, kicked is +4/+4")
{
    EnsureCardsMorph();
    const CardDefinition& k = DefM("Vines of Vastwood (Kicked)");
    CHECK(k.params.solo_target_trick);
    CHECK(k.params.power_bonus == 4);
    CHECK(k.params.tough_bonus == 4);
    CHECK_FALSE(k.params.kicker_cost.has_value());
    CHECK(k.card.m_mana_cost.green == 2);
    {
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Vines of Vastwood", 10);
        CHECK(Power(s, b) == 2);                     // no Defiance: unkicked does nothing here
        PutM(s, "Wild Defiance", 20);
        Cast(s, "Vines of Vastwood", 10);
        CHECK(Power(s, b) == 5);                     // ...but it TARGETS, so Defiance fires
        Cast(s, "Vines of Vastwood (Kicked)", 10);
        CHECK(Power(s, b) == 12);                    // +3 Defiance +4 kicked
    }
}

TEST_CASE("morph pumps: kicked Vines is enumerated once PER TARGET (the generic kicker gap)")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Vines of Vastwood", 101));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    int kicked = 0, unkicked = 0, untargeted_kicked = 0;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Vines of Vastwood") { continue; }
        if (a.kicked) { ++kicked; if (a.enchant_target == 0) { ++untargeted_kicked; } }
        else          { ++unkicked; }
    }
    CHECK(kicked == 2);
    CHECK(unkicked == 2);
    CHECK(untargeted_kicked == 0);
}

TEST_CASE("morph pumps: Legion Stronghold always enters tapped and taps for R or W")
{
    EnsureCardsMorph();
    const CardDefinition& back = DefM("Legion Stronghold");
    CHECK(back.card.IsLand());
    CHECK(back.params.enters_tapped);
    REQUIRE(back.params.produces.size() == 2);
    CHECK(back.params.produces[0] == Color::Red);
    CHECK(back.params.produces[1] == Color::White);
    CHECK(back.params.etb_pay_life_to_untap == 0);
}

TEST_CASE("morph pumps: Spellskite redirect -- Defiance fires for BOTH targets, the payload lands on Spellskite")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b  = PutBear(s, 10);
    const int sk = PutM(s, "Spellskite", 11);
    PutM(s, "Wild Defiance", 20);
    Cast(s, "Might of Old Krosa", 10, /*redirect=*/11);
    CHECK(Power(s, b) == 5);                         // the bear became the target: +3 only
    CHECK(Power(s, sk) == 7);                        // 0 + 3 (redirect) + 4 (Might)
}

TEST_CASE("morph pumps: the redirect twin is offered only with a watcher, and carries a {U/P} pip")
{
    EnsureCardsMorph();
    auto count_redirects = [](const GameState& s, int& life_twins)
    {
        int n = 0;
        life_twins = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind != Action::Kind::CastFromHand || a.trick_redirect == 0) { continue; }
            ++n;
            if (a.phyrexian_life == 2) { ++life_twins; CHECK(a.cost.blue == 0); }
            else                       { CHECK(a.cost.blue == 1); CHECK(a.cost.phyrexian_count == 1); }
        }
        return n;
    };
    GameState s = BaseM();
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    int life = 0;
    CHECK(count_redirects(s, life) == 0);            // no watcher: the redirect is dominated
    PutM(s, "Wild Defiance", 20);
    const int n = count_redirects(s, life);
    CHECK(n >= 1);                                   // bear -> Spellskite (mana form)
    CHECK(life >= 1);                                // ...and its pay-2-life twin
}

TEST_CASE("morph pumps: the Morph list routes to MorphProvider, whose rank orders permanents < pumps < doublers")
{
    EnsureCardsMorph();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Morph/Morph.cod"));
    const DecisionProvider& p = DetectDecisionProvider(deck);
    CHECK(std::string(p.Name()) == "Morph");
    CHECK(p.OrderOpaqueCastsByRank());
    GameState s = BaseM();
    const int defiance = p.CastOrderRank(s, DefM("Wild Defiance"));
    const int might    = p.CastOrderRank(s, DefM("Might of Old Krosa"));
    const int vines    = p.CastOrderRank(s, DefM("Vines of Vastwood (Kicked)"));
    const int berserk  = p.CastOrderRank(s, DefM("Berserk"));
    const int legion   = p.CastOrderRank(s, DefM("Legion Leadership"));
    const int tbr      = p.CastOrderRank(s, DefM("Temur Battle Rage"));
    const int skite    = p.CastOrderRank(s, DefM("Spellskite"));
    CHECK(skite < might);
    CHECK(defiance < might);
    CHECK(might == vines);
    CHECK(might < berserk);
    CHECK(berserk == legion);
    CHECK(berserk == tbr);
}
