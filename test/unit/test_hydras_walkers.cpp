// Unit tests for the Hydras I3 group (2026-11-11): Kalonian Hydra's attack trigger and its projection
// addend, Nissa, Who Shakes the World's loyalty abilities, Garruk, Primal Hunter, and the deck's second
// main (USER: "Second main -3 makes sense.").
//
//  1. Card data: the three entries carry the right params; Kalonian's Scryfall "Double" is no tag.
//  2. Kalonian: a lone attacker deals 8 in the rollout combat, every creature we control is doubled
//     (summoning-sick and animated-land included, the opponent's untouched), two copies x4, a counter
//     doubler applies to the doubling, and the projection equals the realised damage in every case.
//  3. ApplyAttackDoubleTeamCounters counts DECLARED copies only.
//  4. Nissa +1: tapped / untapped (pre-tap fold) targets, the identity-folded enumeration, the zero target.
//  5. Nissa -8: the searched count, tapped entry, shuffle, Nissa binned; every count enumerated.
//  6. Garruk: +1 Beast, -3 greatest-power draw (Garruk binned), -6 one Wurm per land.
//  7. DeckUsesSecondMain is keyed on Garruk's -3; a main-2 enumeration offers the -3.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void LoadHw()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHw(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardHw(const std::string& name, int number)
{
    Card c = DefHw(name).card;
    c.m_number = number;
    return c;
}

GameState FreshHw()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 40;
    s.game_seed           = 777;
    s.players[0].lands_played_this_turn = 1;
    return s;
}

Permanent& PutHw(GameState& s, const std::string& name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card              = CardHw(name, number);
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    const CardDefinition& d = DefHw(name);
    if (d.params.loyalty_start > 0)
    {
        p.loyalty = d.params.loyalty_start;
        p.counters.Add(Counter::Type::Loyalty, p.loyalty);
    }
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Permanent* ByNumHw(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int OppLifeDropHw(GameState s)
{
    const int before = s.players[1].life;
    RolloutSimulateCombat(s);
    return before - s.players[1].life;
}

void LibraryHw(GameState& s, const std::string& name, int n, int first)
{
    for (int i = 0; i < n; ++i) { s.players[0].library.push_back(CardHw(name, first + i)); }
}

int AbilityIndex(const CardDefinition& d, const std::string& effect)
{
    for (int i = 0; i < static_cast<int>(d.params.loyalty_abilities.size()); ++i)
    { if (d.params.loyalty_abilities[static_cast<std::size_t>(i)].effect == effect) { return i; } }
    return -1;
}

std::vector<Action> LoyaltyActions(const std::vector<Action>& acts, int walker, int ability)
{
    std::vector<Action> out;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivateLoyalty && a.sac_source_id == walker && a.loyalty_ability == ability)
        { out.push_back(a); }
    }
    return out;
}

}   // namespace

TEST_CASE("Hydras I3: card data -- Kalonian, Nissa's loyalty, Garruk")
{
    LoadHw();
    const CardDefinition& k = DefHw("Kalonian Hydra");
    CHECK(k.card.m_mana_cost.ManaValue() == 5);
    CHECK(k.card.m_mana_cost.green == 2);
    CHECK(k.card.m_power.value_or(-1) == 0);
    CHECK(k.card.m_toughness.value_or(-1) == 0);
    CHECK(k.card.HasKeyword(Keyword::Trample));
    CHECK(k.params.enters_with_plus_counters == 4);
    CHECK(k.params.attack_double_team_counters);
    const CardDefinition& n = DefHw("Nissa, Who Shakes the World");
    CHECK(n.params.loyalty_start == 5);
    REQUIRE(n.params.loyalty_abilities.size() == 2);
    CHECK(n.params.loyalty_abilities[0].delta == 1);
    CHECK(n.params.loyalty_abilities[0].effect == "untap_land_animate");
    CHECK(n.params.loyalty_abilities[0].amount == 3);
    CHECK(n.params.loyalty_abilities[1].delta == -8);
    CHECK(n.params.loyalty_abilities[1].effect == "fetch_forests_tapped");
    CHECK(n.params.land_tap_bonus == 1);
    const CardDefinition& g = DefHw("Garruk, Primal Hunter");
    CHECK(g.card.m_mana_cost.ManaValue() == 5);
    CHECK(g.card.m_mana_cost.green == 3);
    CHECK(g.params.loyalty_start == 3);
    REQUIRE(g.params.loyalty_abilities.size() == 3);
    CHECK(g.params.loyalty_abilities[0].effect == "beast_token_33");
    CHECK(g.params.loyalty_abilities[1].delta == -3);
    CHECK(g.params.loyalty_abilities[1].effect == "draw_greatest_power");
    CHECK(g.params.loyalty_abilities[2].delta == -6);
    CHECK(g.params.loyalty_abilities[2].effect == "wurm_tokens_per_land");
    CHECK(g.params.loyalty_abilities[2].amount == 6);
}

TEST_CASE("Kalonian Hydra: a lone attacker swings for 8 and the projection reads 8")
{
    LoadHw();
    GameState s = FreshHw();
    Permanent& k = PutHw(s, "Kalonian Hydra", 1);
    k.counters.Add(Counter::Type::PlusOnePlusOne, 4);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 8);
    GameState t = s;
    RolloutSimulateCombat(t);
    CHECK(s.players[1].life - t.players[1].life == 8);
    CHECK(PlusCountersOn(*ByNumHw(t, 1)) == 8);
}

TEST_CASE("Kalonian Hydra: EVERY creature we control is doubled; the opponent's are not; projection == realised")
{
    LoadHw();
    GameState s = FreshHw();
    PutHw(s, "Kalonian Hydra", 1).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    // A summoning-sick Primordial (cannot attack) is doubled too.
    PutHw(s, "Primordial Hydra", 2, 0, /*sick=*/true).counters.Add(Counter::Type::PlusOnePlusOne, 3);
    // A Nissa-animated Forest (haste): attacks, and its 3 counters double to 6.
    PutHw(s, "Forest", 3);
    ResolveUntapLandAnimate(s, 0, 3, 3);
    // An opponent creature with a counter is untouched.
    PutHw(s, "Primordial Hydra", 4, /*controller=*/1).counters.Add(Counter::Type::PlusOnePlusOne, 5);
    const int proj = TurnSolver::ProjectedAttackDamageForTest(s);
    GameState t = s;
    RolloutSimulateCombat(t);
    CHECK(s.players[1].life - t.players[1].life == 14);   // 8 + 6
    CHECK(proj == 14);
    CHECK(PlusCountersOn(*ByNumHw(t, 2)) == 6);
    CHECK(PlusCountersOn(*ByNumHw(t, 3)) == 6);
    CHECK(PlusCountersOn(*ByNumHw(t, 4)) == 5);
}

TEST_CASE("Kalonian Hydra: two copies double twice (x4); a counter doubler applies to each doubling")
{
    LoadHw();
    GameState s = FreshHw();
    PutHw(s, "Kalonian Hydra", 1).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    PutHw(s, "Kalonian Hydra", 2).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 32);
    CHECK(OppLifeDropHw(s) == 32);

    GameState v = FreshHw();
    PutHw(v, "Kalonian Hydra", 1).counters.Add(Counter::Type::PlusOnePlusOne, 8);
    PutHw(v, "Primal Vigor", 2);
    // Under Primal Vigor a doubling of 8 puts 2 x 8: 8 -> 24.
    CHECK(TurnSolver::ProjectedAttackDamageForTest(v) == OppLifeDropHw(v));
    GameState w = v;
    RolloutSimulateCombat(w);
    CHECK(PlusCountersOn(*ByNumHw(w, 1)) == 24);
}

TEST_CASE("Kalonian Hydra: -1/-1 counters annihilate first; a Kalonian that does not attack does not trigger")
{
    LoadHw();
    GameState s = FreshHw();
    Permanent& k = PutHw(s, "Kalonian Hydra", 1);
    k.counters.Add(Counter::Type::PlusOnePlusOne, 5);
    k.counters.Add(Counter::Type::MinusOneMinusOne, 1);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 8);   // net 4 -> 8
    CHECK(OppLifeDropHw(s) == 8);

    GameState sick = FreshHw();
    PutHw(sick, "Kalonian Hydra", 1, 0, /*sick=*/true).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    PutHw(sick, "Primordial Hydra", 2).counters.Add(Counter::Type::PlusOnePlusOne, 3);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(sick) == 3);
    GameState t = sick;
    RolloutSimulateCombat(t);
    CHECK(sick.players[1].life - t.players[1].life == 3);
    CHECK(PlusCountersOn(*ByNumHw(t, 1)) == 4);
}

TEST_CASE("ApplyAttackDoubleTeamCounters: only the DECLARED attackers trigger (CR 508.4)")
{
    LoadHw();
    GameState s = FreshHw();
    PutHw(s, "Kalonian Hydra", 1).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    PutHw(s, "Kalonian Hydra", 2).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    const std::vector<int> atk{ 0, 1 };
    GameState a = s;
    CHECK(ApplyAttackDoubleTeamCounters(a, 0, atk, /*declared_n=*/1) == 1);   // index 1 joined later
    CHECK(PlusCountersOn(a.battlefield[0]) == 8);
    CHECK(PlusCountersOn(a.battlefield[1]) == 8);
    GameState b = s;
    CHECK(ApplyAttackDoubleTeamCounters(b, 0, atk, 2) == 2);
    CHECK(PlusCountersOn(b.battlefield[0]) == 16);
}

TEST_CASE("Nissa +1: a TAPPED Forest is untapped, becomes a 3/3 Elemental land with vigilance and haste")
{
    LoadHw();
    GameState s = FreshHw();
    const CardDefinition& nd = DefHw("Nissa, Who Shakes the World");
    PutHw(s, "Nissa, Who Shakes the World", 1);
    PutHw(s, "Forest", 10).tapped = true;
    const int plus = AbilityIndex(nd, "untap_land_animate");
    ApplyLoyaltyAbility(s, 0, 1, plus, /*elk_target=*/10);
    const Permanent* f = ByNumHw(s, 10);
    REQUIRE(f != nullptr);
    CHECK_FALSE(f->tapped);
    CHECK(f->card.IsLand());
    CHECK(f->card.IsCreature());
    CHECK(CardHasSubtype(f->card, "Forest"));
    CHECK(CardHasSubtype(f->card, "Elemental"));
    CHECK(f->card.HasKeyword(Keyword::Haste));
    CHECK(f->card.HasKeyword(Keyword::Vigilance));
    CHECK(PlusCountersOn(*f) == 3);
    CHECK(f->EffectivePower() == 3);
    CHECK(s.floating_mana.Total() == 0);   // it was tapped: nothing to pre-tap
    CHECK(ByNumHw(s, 1)->loyalty == 6);
    // One loyalty ability per turn: a second activation is a no-op.
    PutHw(s, "Forest", 11).tapped = true;
    ApplyLoyaltyAbility(s, 0, 1, plus, 11);
    CHECK(ByNumHw(s, 11)->tapped);
    CHECK(ByNumHw(s, 1)->loyalty == 6);
    // An animated land is a creature: no longer a legal target.
    CHECK_FALSE(NissaAnimateTargetLegal(s, 0, *ByNumHw(s, 10)));
}

TEST_CASE("Nissa +1: an UNTAPPED basic target is tapped for mana first (pre-tap fold), then untapped")
{
    LoadHw();
    GameState s = FreshHw();
    s.deck_has_land_tap_bonus = true;
    const CardDefinition& nd = DefHw("Nissa, Who Shakes the World");
    PutHw(s, "Nissa, Who Shakes the World", 1);
    PutHw(s, "Forest", 10);
    ApplyLoyaltyAbility(s, 0, 1, AbilityIndex(nd, "untap_land_animate"), 10);
    CHECK_FALSE(ByNumHw(s, 10)->tapped);
    CHECK(ByNumHw(s, 10)->card.IsCreature());
    // Forest {G} + Nissa's own static {G}.
    CHECK(s.floating_mana.green == 2);
    // The zero-target choice: loyalty only.
    GameState z = FreshHw();
    PutHw(z, "Nissa, Who Shakes the World", 1);
    PutHw(z, "Forest", 10);
    ApplyLoyaltyAbility(z, 0, 1, AbilityIndex(nd, "untap_land_animate"), 0);
    CHECK(ByNumHw(z, 1)->loyalty == 6);
    CHECK_FALSE(ByNumHw(z, 10)->card.IsCreature());
    CHECK(z.floating_mana.Total() == 0);
}

TEST_CASE("Nissa +1 enumeration: plain Forests fold to ONE target (a tapped one) plus the zero target")
{
    LoadHw();
    GameState s = FreshHw();
    const CardDefinition& nd = DefHw("Nissa, Who Shakes the World");
    PutHw(s, "Nissa, Who Shakes the World", 1);
    PutHw(s, "Forest", 10);
    PutHw(s, "Forest", 11).tapped = true;
    PutHw(s, "Forest", 12).tapped = true;
    PutHw(s, "Forest", 13);
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const std::vector<Action> plus = LoyaltyActions(acts, 1, AbilityIndex(nd, "untap_land_animate"));
    REQUIRE(plus.size() == 2);
    std::vector<int> targets;
    for (const Action& a : plus) { targets.push_back(a.enchant_target); }
    std::sort(targets.begin(), targets.end());
    CHECK(targets[0] == 0);
    CHECK(targets[1] == 11);   // the first TAPPED member
    // -8 is unaffordable at loyalty 5.
    CHECK(LoyaltyActions(acts, 1, AbilityIndex(nd, "fetch_forests_tapped")).empty());
    // All untapped: the class representative is the first untapped one.
    GameState u = FreshHw();
    PutHw(u, "Nissa, Who Shakes the World", 1);
    PutHw(u, "Forest", 10);
    PutHw(u, "Forest", 13);
    const std::vector<Action> up = LoyaltyActions(TurnSolver::CollectActionsForTest(u, true), 1,
                                                  AbilityIndex(nd, "untap_land_animate"));
    REQUIRE(up.size() == 2);
    CHECK((up[0].enchant_target == 10 || up[1].enchant_target == 10));
}

TEST_CASE("Nissa -8: the searched count of Forests enters tapped, the library is shuffled, Nissa is binned")
{
    LoadHw();
    const CardDefinition& nd = DefHw("Nissa, Who Shakes the World");
    const int minus = AbilityIndex(nd, "fetch_forests_tapped");
    GameState s = FreshHw();
    Permanent& n = PutHw(s, "Nissa, Who Shakes the World", 1);
    n.loyalty = 8;
    LibraryHw(s, "Forest", 5, 200);
    LibraryHw(s, "Kalonian Hydra", 3, 300);
    // Every count 0..5 is enumerated (chosen_x = count + 1).
    const std::vector<Action> acts = LoyaltyActions(TurnSolver::CollectActionsForTest(s, true), 1, minus);
    std::vector<int> xs;
    for (const Action& a : acts) { xs.push_back(a.chosen_x); }
    std::sort(xs.begin(), xs.end());
    CHECK(xs == std::vector<int>{ 1, 2, 3, 4, 5, 6 });

    GameState t = s;
    const unsigned long long sc = static_cast<unsigned long long>(t.search_count);
    ApplyLoyaltyAbility(t, 0, 1, minus, 0, std::string(), false, /*count=*/2);
    int forests = 0;
    for (const Permanent& p : t.battlefield)
    {
        if (p.card.m_name.str() == "Forest") { ++forests; CHECK(p.tapped); }
    }
    CHECK(forests == 2);
    CHECK(t.players[0].library.size() == 6);
    CHECK(ByNumHw(t, 1) == nullptr);   // 8 - 8 = 0: binned
    CHECK(static_cast<unsigned long long>(t.search_count) > sc);

    GameState all = s;
    ApplyLoyaltyAbility(all, 0, 1, minus);   // unpinned = all
    CHECK(all.players[0].library.size() == 3);
}

TEST_CASE("Garruk: +1 Beast, -3 draws the greatest power (and bins him), -6 one Wurm per land")
{
    LoadHw();
    const CardDefinition& gd = DefHw("Garruk, Primal Hunter");
    GameState s = FreshHw();
    PutHw(s, "Garruk, Primal Hunter", 1);
    PutHw(s, "Primordial Hydra", 2).counters.Add(Counter::Type::PlusOnePlusOne, 5);
    PutHw(s, "Kalonian Hydra", 3).counters.Add(Counter::Type::PlusOnePlusOne, 4);
    LibraryHw(s, "Forest", 10, 200);

    GameState b = s;
    ApplyLoyaltyAbility(b, 0, 1, AbilityIndex(gd, "beast_token_33"));
    const Permanent& beast = b.battlefield.back();
    CHECK(beast.is_token);
    CHECK(CardHasSubtype(beast.card, "Beast"));
    CHECK(beast.card.HasColor(Color::Green));
    CHECK(beast.EffectivePower() == 3);
    CHECK(ByNumHw(b, 1)->loyalty == 4);

    GameState d = s;
    ApplyLoyaltyAbility(d, 0, 1, AbilityIndex(gd, "draw_greatest_power"));
    CHECK(d.players[0].hand.size() == 5);
    CHECK(d.players[0].cards_drawn_this_turn == 5);
    CHECK(ByNumHw(d, 1) == nullptr);   // 3 - 3 = 0

    GameState w = s;
    PutHw(w, "Forest", 20);
    PutHw(w, "Forest", 21);
    PutHw(w, "Forest", 22);
    ResolveUntapLandAnimate(w, 0, 22, 3);   // an animated Forest is still a land
    ByNumHw(w, 1)->loyalty = 7;
    ApplyLoyaltyAbility(w, 0, 1, AbilityIndex(gd, "wurm_tokens_per_land"));
    int wurms = 0;
    for (const Permanent& p : w.battlefield)
    {
        if (p.is_token && CardHasSubtype(p.card, "Wurm"))
        { ++wurms; CHECK(p.EffectivePower() == 6); CHECK(p.card.HasColor(Color::Green)); }
    }
    CHECK(wurms == 3);
    CHECK(ByNumHw(w, 1)->loyalty == 1);
}

TEST_CASE("Garruk -3 reads the board AT RESOLUTION and is never gated on the pre-plan board")
{
    LoadHw();
    const CardDefinition& gd = DefHw("Garruk, Primal Hunter");
    GameState s = FreshHw();
    PutHw(s, "Garruk, Primal Hunter", 1);   // no creature yet: a same-plan cast would supply one
    const std::vector<Action> acts = LoyaltyActions(TurnSolver::CollectActionsForTest(s, true), 1,
                                                    AbilityIndex(gd, "draw_greatest_power"));
    CHECK(acts.size() == 1);
    CHECK(GreatestPowerAmongOwnCreatures(s, 0) == 0);
}

TEST_CASE("Second main: keyed on Garruk's -3; the post-combat enumeration offers the -3")
{
    LoadHw();
    Decklist with;
    with.mainboard.push_back(CardHw("Garruk, Primal Hunter", 1));
    with.mainboard.push_back(CardHw("Kalonian Hydra", 2));
    CHECK(GoldFishRunner::DeckUsesSecondMain(with));
    Decklist without;
    without.mainboard.push_back(CardHw("Kalonian Hydra", 2));
    without.mainboard.push_back(CardHw("Nissa, Who Shakes the World", 3));
    CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(without));

    const CardDefinition& gd = DefHw("Garruk, Primal Hunter");
    GameState s = FreshHw();
    s.uses_second_main = true;
    PutHw(s, "Garruk, Primal Hunter", 1);
    PutHw(s, "Kalonian Hydra", 3).counters.Add(Counter::Type::PlusOnePlusOne, 8);
    CHECK(LoyaltyActions(TurnSolver::CollectActionsForTest(s, false), 1,
                         AbilityIndex(gd, "draw_greatest_power")).size() == 1);
}
