// Creature Burn: the PER-TURN Browbeat second main (MTG_CB_BROWBEAT_M2_RESTRICT, heurarm CB_BROWBEAT_M2_RESTRICT,
// DEFAULT OFF since 2026-11-21: measured NOT an identity, see STATUS at CreatureBurnProvider::RestrictedSecondMainLive).
//   * the deck plays the every-turn main 2 by default and with =0, and none with =1 (both halves read one reader);
//   * the post-combat main opens exactly while a Browbeat is in hand AND castable there (the pool pays it, or an open
//     land drop with a land in hand), and never on a deck that already plays a full main 2;
//   * a deck that holds a Browbeat but is NOT Creature Burn keeps the full main 2 (provider-scoped).
// The end-to-end "attack, then Browbeat" line is pinned by test/scenarios/creatureburn_browbeat_m2_attack_then_browbeat.json.
#include <doctest/doctest.h>
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include <cstdint>
#include <string>
#include <vector>

namespace
{
const CardDefinition& BbDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, name);
    return *d;
}
Card BbCard(const std::string& name, int number)
{
    Card c = BbDef(name).card;
    c.m_number = number;
    return c;
}
// Pins the lever for one scope and restores "use the env default" on exit.
struct BbArm
{
    heurarm::Arm saved;
    explicit BbArm(int v) : saved(heurarm::t_arm)
    { heurarm::t_arm[heurarm::CB_BROWBEAT_M2_RESTRICT] = static_cast<std::int8_t>(v); }
    ~BbArm() { heurarm::t_arm = saved; }
};
const CreatureBurnProvider& Cb()
{
    static const CreatureBurnProvider p;
    return p;
}
// The shipped list's mainboard, by name (DeckUsesSecondMain and the provider route read only the definitions).
Decklist CbDeck()
{
    const std::vector<std::pair<const char*, int>> list = {
        {"Browbeat", 1}, {"Rift Bolt", 2}, {"Flame Javelin", 1}, {"Fireblast", 3}, {"Lightning Bolt", 4},
        {"Magma Jet", 4}, {"Price of Progress", 2}, {"Searing Blaze", 2}, {"Ball Lightning", 4},
        {"Cinder Pyromancer", 2}, {"Figure of Destiny", 2}, {"Goblin Fireslinger", 4}, {"Hellspark Elemental", 2},
        {"Keldon Champion", 2}, {"Keldon Marauders", 1}, {"Spark Elemental", 2}, {"Vulshok Sorcerer", 2},
        {"Mountain", 18}, {"Teetering Peaks", 2}};
    Decklist deck;
    int n = 1;
    for (const auto& [name, k] : list)
    { for (int i = 0; i < k; ++i) { deck.mainboard.push_back(BbCard(name, n++)); } }
    return deck;
}
// A POST-COMBAT state: `untapped` + `tapped` Mountains, a non-sick Keldon Champion that attacked (tapped), the hand.
GameState PostCombat(int untapped, int tapped, const std::vector<std::string>& hand, int lands_played = 1)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PostCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 7;
    s.players[0].lands_played_this_turn = lands_played;
    s.m_provider          = &Cb();
    int num = 70;
    for (int k = 0; k < untapped + tapped; ++k)
    {
        Permanent p;
        p.card = BbCard("Mountain", num++);
        p.controller_index = p.owner_index = 0;
        p.tapped = k >= untapped;
        s.battlefield.push_back(p);
    }
    {
        Permanent p;
        p.card = BbCard("Keldon Champion", num++);
        p.controller_index = p.owner_index = 0;
        p.tapped = true;
        s.battlefield.push_back(p);
    }
    int h = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(BbCard(n, h++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(BbCard("Mountain", 40 + k)); }
    return s;
}
}   // namespace

TEST_CASE("Creature Burn Browbeat m2: the shipped list keeps its every-turn main 2 by default; =1 drops it")
{
    const Decklist deck = CbDeck();
    REQUIRE(dynamic_cast<const CreatureBurnProvider*>(&SelectDecisionProvider(deck)) != nullptr);
    {
        BbArm arm(-1);   // unset -> the env default (OFF; the suite never sets MTG_CB_BROWBEAT_M2_RESTRICT)
        CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
        CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(3, 0, {"Browbeat"})));
    }
    {
        BbArm arm(1);
        CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(deck));
    }
    {
        BbArm arm(0);
        CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
        // ...and the per-turn phase is closed, so the =0 arm is exactly the every-turn main 2.
        CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(3, 0, {"Browbeat"})));
    }
}

TEST_CASE("Creature Burn Browbeat m2: a non-Creature-Burn deck holding Browbeat keeps the full main 2")
{
    Decklist deck;
    deck.mainboard.push_back(BbCard("Browbeat", 1));
    deck.mainboard.push_back(BbCard("Mountain", 2));
    REQUIRE(dynamic_cast<const CreatureBurnProvider*>(&SelectDecisionProvider(deck)) == nullptr);
    BbArm arm(1);
    CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
}

TEST_CASE("Creature Burn Browbeat m2: opens only while a castable Browbeat is in hand after combat")
{
    BbArm arm(1);
    // Browbeat in hand, three untapped Mountains -> open.
    CHECK(Cb().RestrictedSecondMainLive(PostCombat(3, 2, {"Browbeat", "Lightning Bolt"})));
    // No Browbeat (burn only) -> closed: every main-2 cast had its main-1 twin.
    CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(3, 2, {"Lightning Bolt", "Ball Lightning"})));
    CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(3, 2, {})));
    // Browbeat in hand but only two untapped Mountains and the land drop used -> it cannot be cast here -> closed.
    CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(2, 3, {"Browbeat", "Mountain"}, /*lands_played=*/1)));
    // ...the same with the drop OPEN and a land in hand: the land drop makes the third mana -> open.
    CHECK(Cb().RestrictedSecondMainLive(PostCombat(2, 3, {"Browbeat", "Mountain"}, /*lands_played=*/0)));
    // ...but an open drop with no land in hand does not.
    CHECK_FALSE(Cb().RestrictedSecondMainLive(PostCombat(2, 3, {"Browbeat"}, /*lands_played=*/0)));
    // ProviderSecondMainLive is the engine's read and agrees; never on a deck already playing a full main 2.
    GameState s = PostCombat(3, 0, {"Browbeat"});
    CHECK(ProviderSecondMainLive(s));
    s.uses_second_main = true;
    CHECK_FALSE(ProviderSecondMainLive(s));
    GameState t = PostCombat(3, 0, {"Lightning Bolt"});
    CHECK_FALSE(ProviderSecondMainLive(t));
}

TEST_CASE("Creature Burn Browbeat m2: the open phase's menu is the FULL one (nothing is narrowed)")
{
    BbArm arm(1);
    // Five untapped Mountains: Browbeat {2}{R} + Lightning Bolt {R} + Ball Lightning {R}{R}{R} are all castable after
    // combat. The post-combat enumeration offers Browbeat, the Bolt AND the (dominated, but admitted) haste creature.
    const GameState s = PostCombat(5, 0, {"Browbeat", "Lightning Bolt", "Ball Lightning"});
    REQUIRE(ProviderSecondMainLive(s));
    bool browbeat = false, bolt = false, ball = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name == "Browbeat")       { browbeat = true; }
            if (a.card_name == "Lightning Bolt") { bolt = true; }
            if (a.card_name == "Ball Lightning") { ball = true; }
        }
    }
    CHECK(browbeat);
    CHECK(bolt);
    CHECK(ball);
}
