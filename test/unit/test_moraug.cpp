// Unit tests for Moraug, Fury of Akoum (2026-10-23, a Landfall screening candidate) and the two generic params
// it introduced -- CardParams::attack_count_team_power ("each creature you control gets +1/+0 for each time it
// has attacked this turn") and landfall_additional_combat ("if it's your main phase, there's an additional
// combat phase after this phase; at the beginning of that combat, untap all creatures you control").
//
//  1. Printed data and params as Scryfall says; a landfall watcher.
//  2. THE STATIC: +1/+0 per declared attack this turn, per Moraug we control; nothing without Moraug.
//  3. THE TRIGGER queues a combat only in OUR MAIN PHASE (not in combat, not in the upkeep) and only for a
//     deck stamped with the card.
//  4. MAIN-1 QUEUE: the additional combat runs BEFORE the regular one and untaps; the regular one does not,
//     so the creature attacks once (+1) -- a main-1 land adds no attack.
//  5. MAIN-2 QUEUE: after the regular combat the additional one untaps and attacks again (+2).
//  6. THE SECOND MAIN is live while Moraug is ours (the held land can be played there), and the search finds
//     "hold the land for main 2" when only that line kills this turn.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <cstddef>
#include <string>
#include <vector>

namespace
{

constexpr const char* kMoraug = "Moraug, Fury of Akoum";

void LoadMo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefMo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshMo()
{
    GameState s;
    s.active_player_index   = 0;
    s.turn_number           = 6;
    s.players[0].life       = 20;
    s.players[1].life       = 20;
    s.deck_has_extra_combat = true;    // StampDeckTraits' answer for a list holding Moraug
    s.phase                 = Phase::PreCombatMain;
    return s;
}

int PutMo(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefMo(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card CardMo(const std::string& name, int number)
{
    Card c = DefMo(name).card;
    c.m_number = number;
    return c;
}

const Permanent* ByNumberMo(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int PowerOf(const GameState& s, int number)
{
    const Permanent* p = ByNumberMo(s, number);
    REQUIRE(p != nullptr);
    return CombatPowerOf(*p, s);   // EffectivePower + ComputeLordBonus (+ Auras / Equipment)
}

void Lands(GameState& s, int n, int first_number)
{
    for (int i = 0; i < n; ++i) { PutMo(s, "Mountain", first_number + i); }
}

}   // namespace

TEST_CASE("Moraug: printed data and params as Scryfall says")
{
    LoadMo();
    const CardDefinition& m = DefMo(kMoraug);
    CHECK(m.card.m_mana_cost.generic == 4);
    CHECK(m.card.m_mana_cost.red == 2);
    CHECK(m.card.m_mana_cost.ManaValue() == 6);
    CHECK(m.card.m_power == 6);
    CHECK(m.card.m_toughness == 6);
    CHECK(m.card.HasSupertype(Supertype::Legendary));
    CHECK(CardHasSubtype(m.card, "Minotaur"));
    CHECK(CardHasSubtype(m.card, "Warrior"));
    CHECK(m.params.attack_count_team_power);
    CHECK(m.params.landfall_additional_combat);
    CHECK(m.landfall_watcher);
    CHECK(DefHasLandfallTrigger(m.params));
}

TEST_CASE("Moraug: +1/+0 per declared attack this turn -- only while a Moraug is ours")
{
    LoadMo();
    GameState s = FreshMo();
    const int bear = 2;
    PutMo(s, "Rampaging Baloths", bear);                    // a 6/6
    CHECK(PowerOf(s, bear) == 6);
    s.battlefield[0].SetAttacksThisTurn(2);
    CHECK(PowerOf(s, bear) == 6);                           // no Moraug: the count reads nothing
    PutMo(s, kMoraug, 1);
    CHECK(PowerOf(s, bear) == 8);                           // +2 for two attacks
    CHECK(PowerOf(s, 1) == 6);                              // Moraug itself has not attacked
    // The count saturates at 7 (three bits).
    s.battlefield[0].SetAttacksThisTurn(9);
    CHECK(s.battlefield[0].AttacksThisTurn() == 7);
    s.battlefield[0].NoteAttackDeclared();
    CHECK(s.battlefield[0].AttacksThisTurn() == 7);
}

TEST_CASE("Moraug: a land queues an additional combat only in our MAIN phase, and only for a stamped deck")
{
    LoadMo();
    GameState s = FreshMo();
    PutMo(s, kMoraug, 1);
    Lands(s, 5, 10);
    EnterLand(s, DefMo("Mountain"), 20);                    // main 1
    CHECK(s.pending_extra_combats == 1);
    s.phase = Phase::PostCombatMain;
    EnterLand(s, DefMo("Mountain"), 21);                    // main 2
    CHECK(s.pending_extra_combats == 2);
    s.phase = Phase::Combat;                                // Primeval Titan's attack fetch
    EnterLand(s, DefMo("Mountain"), 22);
    CHECK(s.pending_extra_combats == 2);
    s.phase = Phase::Beginning;                             // an upkeep Search for Tomorrow
    EnterLand(s, DefMo("Mountain"), 23);
    CHECK(s.pending_extra_combats == 2);
    s.phase = Phase::PreCombatMain;
    s.deck_has_extra_combat = false;                        // an unstamped state queues nothing
    EnterLand(s, DefMo("Mountain"), 24);
    CHECK(s.pending_extra_combats == 2);
}

TEST_CASE("Moraug: main 1's additional combat runs BEFORE the regular one -- one attack, +1")
{
    LoadMo();
    GameState s = FreshMo();
    PutMo(s, kMoraug, 1, /*sick=*/true);                    // cast this turn: cannot attack
    const int beast = 2;
    PutMo(s, "Rampaging Baloths", beast);                   // a 6/6 that can
    s.pending_extra_combats = 1;                            // a main-1 land under Moraug
    RolloutSimulateCombat(s);
    // Additional combat: untap, attack, 6 + 1. Regular combat: the Baloths is tapped and does not untap.
    CHECK(s.players[1].life == 20 - 7);
    CHECK(s.pending_extra_combats == 0);
    CHECK(ByNumberMo(s, beast)->AttacksThisTurn() == 1);
    CHECK(s.phase == Phase::PostCombatMain);
}

TEST_CASE("Moraug: main 2's additional combat untaps and attacks again -- +2 on the second swing")
{
    LoadMo();
    GameState s = FreshMo();
    PutMo(s, kMoraug, 1, /*sick=*/true);
    const int beast = 2;
    PutMo(s, "Rampaging Baloths", beast);
    RolloutSimulateCombat(s);                               // the regular combat: 6 + 1
    CHECK(s.players[1].life == 13);
    CHECK(ByNumberMo(s, beast)->tapped);
    Lands(s, 5, 10);
    EnterLand(s, DefMo("Mountain"), 20);                    // main 2: queues one
    CHECK(s.pending_extra_combats == 1);
    CHECK_FALSE(RolloutRunPostMain2ExtraCombats(s));        // 13 - (6 + 2) = 5: not yet lethal
    CHECK(s.players[1].life == 5);
    CHECK(ByNumberMo(s, beast)->AttacksThisTurn() == 2);
    CHECK(s.pending_extra_combats == 0);
}

TEST_CASE("Moraug: the post-combat main is live while Moraug is ours, and the held land may be played there")
{
    LoadMo();
    GameState s = FreshMo();
    CHECK_FALSE(ExtraCombatSecondMainLive(s));
    PutMo(s, kMoraug, 1);
    CHECK(ExtraCombatSecondMainLive(s));
    CHECK(ProviderSecondMainLive(s));
    s.deck_has_extra_combat = false;
    CHECK_FALSE(ExtraCombatSecondMainLive(s));
}

TEST_CASE("Moraug: the search holds the land for main 2 when only that line kills this turn")
{
    LoadMo();
    GameState s = FreshMo();
    PutMo(s, kMoraug, 1);                                   // an old Moraug: it attacks
    Lands(s, 6, 10);
    for (Permanent& p : s.battlefield) { if (p.card.IsLand()) { p.tapped = true; } }   // no mana: lands only
    s.players[0].hand.push_back(CardMo("Mountain", 30));
    for (int i = 0; i < 10; ++i) { s.players[0].library.push_back(CardMo("Mountain", 40 + i)); }
    // Opponent at 15: land in main 1 -> additional combat 6+1 then a tapped regular combat = 7. Land held for
    // main 2 -> regular combat 6+1 = 7, then main 2's additional combat untaps and swings 6+2 = 8 -> 15.
    s.players[1].life = 15;
    const TurnSolver::Plan m1 = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2);
    CHECK(m1.land_to_play.empty());
    GameState t = s;
    TurnSolver::ApplyPlan(t, m1, /*is_pre_combat=*/true);
    RolloutSimulateCombat(t);
    CHECK(t.players[1].life == 8);
    REQUIRE(ProviderSecondMainLive(t));
    const TurnSolver::Plan m2 = TurnSolver::SolveWithLookahead(t, /*is_pre_combat=*/false, /*depth=*/2);
    CHECK(m2.land_to_play == "Mountain");
    TurnSolver::ApplyPlan(t, m2, /*is_pre_combat=*/false);
    CHECK(t.pending_extra_combats == 1);
    CHECK(RolloutRunPostMain2ExtraCombats(t));
    CHECK(t.players[1].life <= 0);
}
