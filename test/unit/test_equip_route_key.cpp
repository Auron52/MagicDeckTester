// The equip route's verdict memo is keyed on EquipRouteStateKey. Two boards whose route verdict can
// differ must not share a key, because a memoised verdict is REPLAYED by the executor: a LETHAL
// proven on one board and replayed on the other is "APPLY replay found no kill on a board the trial
// proved", and the mirror image -- a "no" from the weaker board suppressing the kill the stronger
// one had -- is silent. 2026-10-05: the key folded the generic counter vector and none of the
// dedicated counter ints, so a Puresteel wearing an Umezawa's Jitte with two charge counters (+4/+4
// at combat) and the same board with an empty Jitte shared one key; the swap screen's card-scores
// run printed 2,000+ of the loud kind. These cases pin every dedicated counter and the phase.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
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

Permanent& Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return s.battlefield.back();
}

Permanent& Find(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    REQUIRE_MESSAGE(false, "permanent not on the battlefield: ", number);
    return s.battlefield.front();
}

GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 18;
    Put(s, "Puresteel Paladin", 54);
    Put(s, "Umezawa's Jitte", 77);
    Find(s, 77).equipped_to = 54;
    Put(s, "Spidersilk Net", 70);
    Put(s, "Accorder's Shield", 3);
    Put(s, "Ancient Den", 8);
    s.players[0].hand.push_back(Def("Colossus Hammer").card);
    s.players[0].hand.push_back(Def("Bonesplitter").card);
    return s;
}

}   // namespace

TEST_CASE("the route key separates a Jitte with charge counters from an empty one (the 2026-10-05 collision)")
{
    EnsureCardsLoaded();
    GameState a = Board();
    GameState b = Board();
    CHECK(EquipRouteStateKey(a, 0) == EquipRouteStateKey(b, 0));   // identical boards, identical key
    Find(b, 77).charge_counters = 2;
    CHECK(EquipRouteStateKey(a, 0) != EquipRouteStateKey(b, 0));
    Find(a, 77).charge_counters = 2;
    CHECK(EquipRouteStateKey(a, 0) == EquipRouteStateKey(b, 0));   // and back
}

TEST_CASE("every dedicated counter int on a permanent splits the route key")
{
    EnsureCardsLoaded();
    const GameState base = Board();
    const std::uint64_t k0 = EquipRouteStateKey(base, 0);
    auto with = [&](auto&& mutate)
    {
        GameState s = base;
        mutate(Find(s, 70));                 // the Net: an Equipment nothing else reads, so only the key moves
        return EquipRouteStateKey(s, 0);
    };
    CHECK(with([](Permanent& p) { p.hone_counters    = 1; }) != k0);   // Dwalin: +1/+0 per Equipment
    CHECK(with([](Permanent& p) { p.quest_counters   = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.storage_counters = 1; }) != k0);   // a Remote Farm's battery
    CHECK(with([](Permanent& p) { p.spore_counters   = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.fade_counters    = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.age_counters     = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.ice_counters     = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.lore_counters    = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.verse_counters   = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.loyalty          = 1; }) != k0);
    CHECK(with([](Permanent& p) { p.charge_counters  = 1; }) != k0);
    // The generic vector was already keyed; it still is.
    CHECK(with([](Permanent& p) { p.counters.Add(Counter::Type::PlusOnePlusOne, 1); }) != k0);
}

TEST_CASE("the route key separates the same board before and after combat")
{
    EnsureCardsLoaded();
    GameState a = Board();
    GameState b = Board();
    a.phase = Phase::PreCombatMain;
    b.phase = Phase::PostCombatMain;
    CHECK(EquipRouteStateKey(a, 0) != EquipRouteStateKey(b, 0));
}

// The equip route's walk returned "no win" on a FINISHED chain whose board already kills: with nothing
// left to cast the enumeration is empty, and the only plan that carries "attack now" comes from the
// board-lethal short-circuit, which MTG_UNPRUNED (every claude-play / viewer session) turns off.
// KittyEquipment v2 claude_s6_gi5 t3, 2026-10-27: the viewer never offered COMBO OFF. The walk now asks
// BoardLethalAsIs, a VERIFIED kill (the idle plan applied on a copy and the combat simulated).
TEST_CASE("BoardLethalAsIs: a finished equip chain kills as it stands; a short one does not")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    Put(s, "Puresteel Paladin", 40);
    Put(s, "Shadowspear", 49);
    Find(s, 49).equipped_to = 40;
    Put(s, "Colossus Hammer", 20);
    Find(s, 20).equipped_to = 40;
    Put(s, "Colossus Hammer", 21);
    Find(s, 21).equipped_to = 40;
    s.players[1].life = 20;
    CHECK(TurnSolver::BoardLethalAsIs(s, /*is_pre_combat=*/true));     // 2 + 10 + 10 + 1 = 23
    CHECK_FALSE(TurnSolver::BoardLethalAsIs(s, /*is_pre_combat=*/false));
    s.players[1].life = 24;
    CHECK_FALSE(TurnSolver::BoardLethalAsIs(s, /*is_pre_combat=*/true));
    Find(s, 40).entered_this_turn = true;                              // summoning sick: no attack
    s.players[1].life = 20;
    CHECK_FALSE(TurnSolver::BoardLethalAsIs(s, /*is_pre_combat=*/true));
}
