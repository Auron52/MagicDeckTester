// Martyr Lifegain card integration (2026-11-10): Thraben Inspector, Squadron Hawk, Curious Farm
// Animals, Kami of False Hope, Enduring Innocence. Each test drives the SHARED resolution helpers
// (FireOwnEtbTriggers, ApplySacCreatureOutlet, OnCreatureDies, ResolveCombatDamage) that both the
// executor and the rollout call, so a pass here is a pass in both worlds.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/Combat.h"
#include "ai/TurnSolver.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& MlDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card MlZone(const char* name, int number)
{
    MlDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int MlAdd(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = MlDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int MlCount(const GameState& s, const char* name, int controller = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && p.card.m_name.str() == name) { ++n; } }
    return n;
}

int MlHandCount(const GameState& s, const char* name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

void MlSetLib(GameState& s, const std::vector<Card>& cards)
{ s.players[0].library.assign(cards.begin(), cards.end()); }

GameState MlState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    return s;
}
}   // namespace

// ---- Thraben Inspector ---------------------------------------------------------------------------

TEST_CASE("Thraben Inspector: its ETB investigates -- one crackable Clue artifact token")
{
    GameState s = MlState();
    const int idx = MlAdd(s, "Thraben Inspector", 11, 0, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    REQUIRE(MlCount(s, "Clue Token") == 1);
    const Permanent& clue = s.battlefield.back();
    CHECK(clue.is_token);
    CHECK(clue.card.HasType(CardType::Artifact));
    CHECK(!clue.card.IsCreature());
    CHECK(clue.card.m_number >= GameState::kFirstTokenNumber);
    const CardDefinition* cd = CardDatabase::Instance().LookupCached(clue.card);
    REQUIRE(cd != nullptr);
    CHECK(cd->params.sac_draw_cost.has_value());   // the {2}-sac-draw lives on the Clue Token def
}

TEST_CASE("Thraben Inspector: a graveyard RETURN (Abiding Grace's route) investigates too")
{
    GameState s = MlState();
    const Card back = MlZone("Thraben Inspector", 12);
    PutCardOntoBattlefield(s, 0, back, "Abiding Grace", 0);
    CHECK(MlCount(s, "Thraben Inspector") == 1);
    CHECK(MlCount(s, "Clue Token") == 1);
}

TEST_CASE("Thraben Inspector: a creature without the param mints nothing (byte-identity guard)")
{
    GameState s = MlState();
    const int idx = MlAdd(s, "Kami of False Hope", 13, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlCount(s, "Clue Token") == 0);
}

// ---- Squadron Hawk -------------------------------------------------------------------------------

TEST_CASE("Squadron Hawk: fetches up to three Hawks to hand, never another creature, one shuffle")
{
    GameState s = MlState();
    MlSetLib(s, { MlZone("Martyr of Sands", 30), MlZone("Squadron Hawk", 31),
                             MlZone("Squadron Hawk", 32), MlZone("Plains", 33),
                             MlZone("Squadron Hawk", 34), MlZone("Squadron Hawk", 35),
                             MlZone("Serra Ascendant", 36) });
    const uint64_t searches = s.search_count;
    const int idx = MlAdd(s, "Squadron Hawk", 20, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlHandCount(s, "Squadron Hawk") == 3);
    CHECK(s.players[0].hand.size() == 3);
    CHECK(s.players[0].library.size() == 4);   // the 4th Hawk + Martyr + Plains + Serra stay
    CHECK(s.search_count == searches + 1);     // exactly ONE shuffle for the whole multi-fetch
}

TEST_CASE("Squadron Hawk: one Hawk left -> one fetched; none left -> nothing, still shuffles")
{
    GameState s = MlState();
    MlSetLib(s, { MlZone("Martyr of Sands", 30), MlZone("Squadron Hawk", 31),
                             MlZone("Plains", 33) });
    const int idx = MlAdd(s, "Squadron Hawk", 20, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlHandCount(s, "Squadron Hawk") == 1);
    CHECK(s.players[0].hand.size() == 1);

    GameState t = MlState();
    MlSetLib(t, { MlZone("Martyr of Sands", 30), MlZone("Plains", 33) });
    const uint64_t searches = t.search_count;
    const int tidx = MlAdd(t, "Squadron Hawk", 21, 0, true);
    FireOwnEtbTriggers(t, 0, tidx, std::string(), -1);
    CHECK(t.players[0].hand.empty());
    CHECK(t.players[0].library.size() == 2);
    CHECK(t.search_count == searches + 1);
}

// ---- Curious Farm Animals ------------------------------------------------------------------------

TEST_CASE("Curious Farm Animals: its own read dies-gain opens the self-only gate; Ranger-Captain's does not")
{
    GameState s = MlState();
    s.deck_reads_life_total = true;   // Serra Ascendant / Speaker of the Heavens read the life total
    MlAdd(s, "Curious Farm Animals", 40);
    MlAdd(s, "Ranger-Captain of Eos", 41);
    CHECK(SelfSacOutletUseful(s, 0, 40));
    CHECK(!SelfSacOutletUseful(s, 0, 41));   // no payload, no death payoff: still dominated

    // Nothing reads the gain: the activation is a dominated no-op again.
    GameState t = MlState();
    t.deck_reads_life_total = false;
    MlAdd(t, "Curious Farm Animals", 40);
    CHECK(!SelfSacOutletUseful(t, 0, 40));
}

TEST_CASE("Curious Farm Animals: the sacrifice gains 3 (one event) and autonomously destroys nothing of ours")
{
    GameState s = MlState();
    s.players[0].life = 20;
    MlAdd(s, "Curious Farm Animals", 40);
    MlAdd(s, "Umezawa's Jitte", 42);
    MlAdd(s, "Abiding Grace", 43);
    CreateClueTokens(s, 0, 1);
    ApplySacCreatureOutlet(s, 0, 40, 40);
    CHECK(s.players[0].life == 23);
    CHECK(s.players[0].life_gained_this_turn == 3);
    CHECK(MlCount(s, "Curious Farm Animals") == 0);
    CHECK(MlCount(s, "Umezawa's Jitte") == 1);   // dominance fold: no opponent target -> none
    CHECK(MlCount(s, "Abiding Grace") == 1);
    CHECK(MlCount(s, "Clue Token") == 1);
    REQUIRE(!s.players[0].graveyard.empty());
    CHECK(s.players[0].graveyard.back().m_name.str() == "Curious Farm Animals");
}

TEST_CASE("Curious Farm Animals: an opponent's artifact IS destroyed (the fold's purposeful target)")
{
    GameState s = MlState();
    MlAdd(s, "Curious Farm Animals", 40);
    MlAdd(s, "Umezawa's Jitte", 44, /*controller=*/1);
    ApplySacCreatureOutlet(s, 0, 40, 40);
    CHECK(MlCount(s, "Umezawa's Jitte", 1) == 0);
}

TEST_CASE("Curious Farm Animals: any death (not just the sacrifice) gains 3")
{
    GameState s = MlState();
    s.players[0].life = 20;
    const Card dead = MlDef("Curious Farm Animals").card;
    OnCreatureDies(s, 0, dead, false, 0, nullptr, 0);
    CHECK(s.players[0].life == 23);
}

// ---- Kami of False Hope --------------------------------------------------------------------------

TEST_CASE("Kami of False Hope: a sacrifice prevents this turn's combat damage, in the core and the projection")
{
    auto board = [](GameState& s)
    {
        s.players[0].life = 20;
        s.players[1].life = 20;
        MlAdd(s, "Kami of False Hope", 50);
        MlAdd(s, "Serra Ascendant", 51);   // 1/1 lifelink, not summoning sick
        const int j = MlAdd(s, "Umezawa's Jitte", 52);
        s.battlefield[static_cast<std::size_t>(j)].equipped_to = 51;
    };
    auto serra_idx = [](const GameState& s)
    {
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == 51) { return i; } }
        return -1;
    };
    auto jitte = [](const GameState& s)
    {
        for (const Permanent& p : s.battlefield) { if (p.card.m_number == 52) { return p.charge_counters; } }
        return -1;
    };

    // Control: no sacrifice -> Serra connects for 1, gains 1, Jitte earns two counters.
    GameState c = MlState();
    board(c);
    CHECK(TurnSolver::ProjectedAttackDamage(c) > 0);
    ResolveCombatDamage(c, { serra_idx(c) }, 0, false);
    CHECK(c.players[1].life < 20);
    CHECK(c.players[0].life > 20);
    CHECK(jitte(c) == 2);

    // Sacrificed: nothing is dealt, nothing is gained, Jitte earns nothing.
    GameState s = MlState();
    board(s);
    ApplySacCreatureOutlet(s, 0, 50, 50);
    CHECK(s.combat_damage_prevented_this_turn);
    CHECK(MlCount(s, "Kami of False Hope") == 0);
    CHECK(TurnSolver::ProjectedAttackDamage(s) == 0);
    ResolveCombatDamage(s, { serra_idx(s) }, 0, false);
    CHECK(s.players[1].life == 20);
    CHECK(s.players[0].life == 20);
    CHECK(jitte(s) == 0);
}

TEST_CASE("Kami of False Hope: the self-only gate opens only with a death payoff (Abiding Grace's MV-1 return)")
{
    GameState s = MlState();
    s.deck_has_endstep_reanimate = true;
    MlAdd(s, "Kami of False Hope", 50);
    CHECK(!SelfSacOutletUseful(s, 0, 50));
    MlAdd(s, "Abiding Grace", 53);
    CHECK(SelfSacOutletUseful(s, 0, 50));
}


// ---- Enduring Innocence --------------------------------------------------------------------------

namespace
{
GameState InnoState()
{
    GameState s = MlState();
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(MlZone("Plains", 200 + k)); }
    return s;
}
// A creature ENTERS (both enter cascades, as every real entry route runs them).
void Enter(GameState& s, const char* name, int number, int controller = 0)
{
    const int idx = MlAdd(s, name, number, controller, /*sick=*/true);
    FireEtbWatchers(s, controller, idx);
    FireOwnEtbTriggers(s, controller, idx, std::string(), -1);
}
}   // namespace

TEST_CASE("Enduring Innocence: draws once each turn, for another small creature of ours")
{
    GameState s = InnoState();
    MlAdd(s, "Enduring Innocence", 60);
    Enter(s, "Mother of Runes", 61);
    CHECK(s.players[0].hand.size() == 1);
    Enter(s, "Kami of False Hope", 62);
    CHECK(s.players[0].hand.size() == 1);   // once each turn
    Enter(s, "Kami of False Hope", 63, /*controller=*/1);
    CHECK(s.players[0].hand.size() == 1);   // not ours
}

TEST_CASE("Enduring Innocence: the power filter reads the entrant's LIVE power")
{
    {   // Ranger of Eos (3/2) does not trigger it.
        GameState s = InnoState();
        MlAdd(s, "Enduring Innocence", 60);
        Enter(s, "Ranger of Eos", 64);
        int drawn = 0;
        for (const Card& c : s.players[0].hand) { if (c.m_name.str() == "Plains") { ++drawn; } }
        CHECK(drawn == 0);
    }
    {   // Serra Ascendant at 30 life is a 6/6: no draw. Below 30 she is a 1/1: draw.
        GameState s = InnoState();
        s.players[0].life = 30;
        MlAdd(s, "Enduring Innocence", 60);
        Enter(s, "Serra Ascendant", 65);
        CHECK(s.players[0].hand.empty());
        GameState t = InnoState();
        t.players[0].life = 20;
        MlAdd(t, "Enduring Innocence", 60);
        Enter(t, "Serra Ascendant", 65);
        CHECK(t.players[0].hand.size() == 1);
    }
    {   // A second Innocence (2/1) entering: the first draws; the newcomer is not "other" to itself.
        GameState s = InnoState();
        MlAdd(s, "Enduring Innocence", 60);
        Enter(s, "Enduring Innocence", 66);
        CHECK(s.players[0].hand.size() == 1);
    }
}

TEST_CASE("Enduring Innocence: two copies each draw once; a graveyard return counts as an entry")
{
    GameState s = InnoState();
    MlAdd(s, "Enduring Innocence", 60);
    MlAdd(s, "Enduring Innocence", 67);
    PutCardOntoBattlefield(s, 0, MlZone("Kami of False Hope", 68), "Abiding Grace", 0);
    CHECK(s.players[0].hand.size() == 2);
    Enter(s, "Mother of Runes", 61);
    CHECK(s.players[0].hand.size() == 2);
}

TEST_CASE("Enduring Innocence: dies as a creature -> returns as a noncreature enchantment, once")
{
    GameState s = InnoState();
    MlAdd(s, "Enduring Innocence", 70);   // the watcher that must NOT draw for the return
    const Card dying = MlDef("Enduring Innocence").card;
    Card dead = dying;
    dead.m_number = 71;
    s.players[0].graveyard.push_back(dead);   // the death site puts it there first
    OnCreatureDies(s, 0, dead, /*was_token=*/false, 0, nullptr, 0);
    const Permanent* back = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 71) { back = &p; } }
    REQUIRE(back != nullptr);
    CHECK(!back->card.IsCreature());
    CHECK(back->card.IsEnchantment());
    CHECK(back->enduring_noncreature);
    CHECK(s.players[0].graveyard.empty());
    CHECK(s.players[0].hand.empty());   // not a creature entering: Innocence #70 does not draw

    // The returned enchantment keeps its draw trigger.
    Enter(s, "Kami of False Hope", 72);
    CHECK(s.players[0].hand.size() == 2);   // #70 and the returned #71 each trigger once

    // Leaving again as a NONcreature does not return it (the intervening if).
    GameState t = InnoState();
    Card enc = dying;
    enc.m_number = 73;
    enc.RemoveType(CardType::Creature);
    t.players[0].graveyard.push_back(enc);
    OnCreatureDies(t, 0, enc, false, 0, nullptr, 0);
    CHECK(MlCount(t, "Enduring Innocence") == 0);
    CHECK(t.players[0].graveyard.size() == 1);

    // A token copy ceases to exist.
    GameState u = InnoState();
    Card tok = dying;
    tok.m_number = 1500;
    OnCreatureDies(u, 0, tok, /*was_token=*/true, 0, nullptr, 0);
    CHECK(MlCount(u, "Enduring Innocence") == 0);
}
