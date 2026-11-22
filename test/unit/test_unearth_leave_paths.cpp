// Unearth's leave-the-battlefield replacement (CR 702.84a: "If it would leave the battlefield, exile it
// instead of putting it anywhere else"), routed through EVERY leave path (USER 2026-11-22: "It's arguable,
// but I think its best to just fully implement this."). The sacrifice chokepoint and the end-step exile
// are covered in test_red_elementals.cpp; this file covers the rest, one leave path per case, in the
// shared resolvers both worlds call (and the executor-only SBA, GameEngine::CheckStateBasedActions, whose
// rollout twin is SbaKillDeadCreatures / CounterPingKillAt):
//
//   destroy (Fatal Push's ResolveCreatureRemovalAt, the cast-trigger destroy), lethal damage (both SBA
//   worlds + the pinger kill), toughness 0 (both SBA worlds + an until-end-of-turn shrink), the legend
//   rule, a symmetric sweep (Pyroclasm), a bounce (to hand) -- and in every case: the card is in EXILE,
//   never the graveyard / hand; it did NOT die (morbid unset); its leave triggers fire (Keldon Marauders'
//   LTB ping, put unearthed by hand); revolt sees it leave. A NON-unearthed copy still dies normally.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsUe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DUe(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState BoardUe()
{
    EnsureCardsUe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    static const RedElementalsProvider prov;
    s.m_provider          = &prov;
    return s;
}

int g_nue = 7600;

// A Hellspark Elemental unearthed from our graveyard (the real ApplyUnearth route). `legendary` stamps the
// Legendary supertype onto the graveyard card first (no unearth card in the pool is legendary; the legend
// rule reads the supertype off the object). Returns its battlefield index.
int UnearthHellspark(GameState& s, bool legendary = false)
{
    Card c = DUe("Hellspark Elemental").card;
    c.m_number = g_nue++;
    if (legendary) { c.AddSupertype(Supertype::Legendary); }
    s.players[0].graveyard.push_back(c);
    REQUIRE(ApplyUnearth(s, 0, "Hellspark Elemental", c.m_number));
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        if (s.battlefield[i].card.m_number != c.m_number) { continue; }
        REQUIRE(bool(s.battlefield[i].unearthed));
        if (legendary) { s.battlefield[i].card.AddSupertype(Supertype::Legendary); }
        return i;
    }
    FAIL("unearthed Hellspark not on the battlefield");
    return -1;
}

// A plain (hard-cast) permanent; `unearthed` sets the bit directly (Keldon Marauders, for its LTB ping).
int PutUe(GameState& s, const std::string& name, bool unearthed = false, bool legendary = false)
{
    Permanent p;
    p.card              = DUe(name).card;
    p.card.m_number     = g_nue++;
    if (legendary) { p.card.AddSupertype(Supertype::Legendary); }
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.unearthed         = unearthed;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

template <class Zone>
int CountUe(const Zone& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

int BfCount(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// The replacement's whole contract for one unearthed Hellspark that just left.
void CheckExiledNotDied(const GameState& s, int exiled = 1, bool another_creature_died = false)
{
    CHECK(BfCount(s, "Hellspark Elemental") == 0);
    CHECK(CountUe(s.exile, "Hellspark Elemental") == exiled);
    CHECK(CountUe(s.players[0].graveyard, "Hellspark Elemental") == 0);
    CHECK(CountUe(s.players[0].hand, "Hellspark Elemental") == 0);
    CHECK(s.creature_died_this_turn == another_creature_died);  // unearth itself did not trigger morbid
    CHECK(s.permanent_left_under_control_this_turn);     // it did LEAVE (revolt)
}

}   // namespace

TEST_CASE("Unearth replacement: a DESTROY effect exiles the unearthed creature (Fatal Push, cast-trigger destroy)")
{
    {
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        ResolveCreatureRemovalAt(s, i, DUe("Fatal Push"), 0, -1);
        CheckExiledNotDied(s);
    }
    {
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        const int num = s.battlefield[static_cast<std::size_t>(i)].card.m_number;
        ResolveCastTriggerTargets(s, 0, "test destroy", { num }, /*destroy=*/true);
        CheckExiledNotDied(s);
    }
    // Control: a hard-cast (not unearthed) Hellspark destroyed goes to the graveyard.
    {
        GameState s = BoardUe();
        const int i = PutUe(s, "Hellspark Elemental");
        ResolveCreatureRemovalAt(s, i, DUe("Fatal Push"), 0, -1);
        CHECK(CountUe(s.players[0].graveyard, "Hellspark Elemental") == 1);
        CHECK(CountUe(s.exile, "Hellspark Elemental") == 0);
    }
}

TEST_CASE("Unearth replacement: an ETB destroy that bypasses the removal resolver also exiles it")
{
    GameState s = BoardUe();
    UnearthHellspark(s);
    DestroyLargestOppCreature(s, 1);  // Ravenous Chupacabra's separate destroy path
    CheckExiledNotDied(s);
}

TEST_CASE("Unearth replacement: a morbid end-step destroy exiles without another death")
{
    GameState s = BoardUe();
    PutUe(s, "Reaper from the Abyss");
    UnearthHellspark(s);
    s.creature_died_this_turn = true;  // the Reaper trigger was already put on the stack
    PerformEndStepMorbidDestroy(s, 1);
    CHECK(BfCount(s, "Hellspark Elemental") == 0);
    CHECK(CountUe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CountUe(s.players[0].graveyard, "Hellspark Elemental") == 0);
}

TEST_CASE("Unearth replacement: LETHAL DAMAGE (executor SBA, rollout SBA, pinger kill) exiles it")
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    {   // executor: GameEngine::CheckStateBasedActions
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        s.battlefield[static_cast<std::size_t>(i)].damage = 1;   // a 3/1
        MtgTestSeam::GeCheckSba(ge, s);
        CheckExiledNotDied(s);
    }
    {   // rollout twin: SbaKillDeadCreatures
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        PutUe(s, "Spark Elemental");                              // a non-unearthed body dying alongside
        s.battlefield[static_cast<std::size_t>(i)].damage = 1;
        s.battlefield.back().damage = 1;
        CHECK(SbaKillDeadCreatures(s, "lethal damage") == 2);
        CheckExiledNotDied(s, 1, /*another_creature_died=*/true);
        CHECK(CountUe(s.players[0].graveyard, "Spark Elemental") == 1);   // the other one died
    }
    {   // a ping's kill: CounterPingKillAt
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        s.battlefield[static_cast<std::size_t>(i)].damage = 1;
        CounterPingKillAt(s, static_cast<std::size_t>(i), "lethal ability damage");
        CheckExiledNotDied(s);
    }
}

TEST_CASE("Unearth replacement: TOUGHNESS 0 (both SBA worlds, an until-end-of-turn shrink) exiles it")
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    {
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        s.battlefield[static_cast<std::size_t>(i)].temp_tough_bonus = -1;
        MtgTestSeam::GeCheckSba(ge, s);
        CheckExiledNotDied(s);
    }
    {
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        s.battlefield[static_cast<std::size_t>(i)].temp_tough_bonus = -1;
        SbaKillDeadCreatures(s, "0 toughness");
        CheckExiledNotDied(s);
    }
    {
        GameState s = BoardUe();
        const int i = UnearthHellspark(s);
        ShrinkCreatureUntilEot(s, s.battlefield[static_cast<std::size_t>(i)].card.m_number, -1, -1);
        CheckExiledNotDied(s);
    }
}

TEST_CASE("Unearth replacement: the LEGEND RULE puts the unearthed copy into exile")
{
    GameState s = BoardUe();
    PutUe(s, "Hellspark Elemental", false, /*legendary=*/true);   // the copy we keep (oldest)
    UnearthHellspark(s, /*legendary=*/true);
    REQUIRE(BfCount(s, "Hellspark Elemental") == 2);
    EnforceLegendRule(s, 0);
    REQUIRE(BfCount(s, "Hellspark Elemental") == 1);
    // Whichever copy the rule kept, the unearthed one is never in the graveyard: either it was kept, or it
    // went to exile. And a death is recorded only when the NON-unearthed copy was the one put away.
    const bool kept_unearthed = s.battlefield[0].unearthed;
    CHECK(CountUe(s.exile, "Hellspark Elemental") == (kept_unearthed ? 0 : 1));
    CHECK(CountUe(s.players[0].graveyard, "Hellspark Elemental") == (kept_unearthed ? 1 : 0));
}

TEST_CASE("Unearth replacement: a SYMMETRIC SWEEP (Pyroclasm) exiles it; the hard-cast body dies")
{
    GameState s = BoardUe();
    UnearthHellspark(s);
    PutUe(s, "Spark Elemental");
    PerformDamageAllCreatures(s, 0, DUe("Pyroclasm"), 2);
    CHECK(BfCount(s, "Hellspark Elemental") == 0);
    CHECK(CountUe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CountUe(s.players[0].graveyard, "Hellspark Elemental") == 0);
    CHECK(CountUe(s.players[0].graveyard, "Spark Elemental") == 1);
}

TEST_CASE("Unearth replacement: a BOUNCE exiles it instead of returning it to hand")
{
    GameState s = BoardUe();
    const int i = UnearthHellspark(s);
    CHECK(ReturnPermanentToOwnersHand(s, i, "test bounce"));
    CheckExiledNotDied(s);
}

TEST_CASE("Unearth replacement: it LEFT -- leave triggers fire (Keldon Marauders' LTB ping), dies triggers do not")
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    GameState s = BoardUe();
    const int i = PutUe(s, "Keldon Marauders", /*unearthed=*/true);
    const int life0 = s.players[1].life;
    s.battlefield[static_cast<std::size_t>(i)].damage = 3;   // a 3/3
    MtgTestSeam::GeCheckSba(ge, s);
    CHECK(BfCount(s, "Keldon Marauders") == 0);
    CHECK(CountUe(s.exile, "Keldon Marauders") == 1);
    CHECK(CountUe(s.players[0].graveyard, "Keldon Marauders") == 0);
    CHECK(s.players[1].life == life0 - 1);   // "When this creature ... leaves the battlefield, 1 damage"
    CHECK_FALSE(s.creature_died_this_turn);
}
