// Unit cover for the ONE rule that mass death has to get right: a creature at 0 toughness is put
// into the graveyard by a STATE-BASED ACTION, and under CR 704.3 state-based actions are performed
// BEFORE triggered abilities are even put on the stack. So a +1/+1 counter arriving from a trigger
// is always too late to save it -- only counters it entered with, or received while still above 0,
// can do that.
//
// WHY THIS FILE EXISTS. The rollout's fade sweep used to decide who dies LAZILY, testing each body's
// toughness as it walked the battlefield downward -- i.e. AFTER the higher-indexed deaths had
// already fired their triggers. So a death trigger that gained life, with an "whenever you gain
// life, put a +1/+1 counter on each creature you control" permanent out, pumped the 0/0s the sweep
// had not reached yet and they SURVIVED. The executor
// (GameEngine::CheckStateBasedActions) never had this bug: it collects the dead during its erase
// loop and fires OnCreatureDies only afterwards. So the two worlds disagreed, which makes this an
// fd-diverge as well as a rules violation.
//
// USER caught the rules error in a code comment that had asserted the opposite:
//   "the 0/0s would die before they received the counter. The only case where that is true is when
//    they enter with the counters already on them or receive the counters before becoming 0/0."
//
// Every gate was green at the time, because the engine agreed with itself. Only the rules caught it,
// which is exactly the class of bug these cases have to carry.
// See docs/design/sba-precedes-triggers-in-mass-death.md.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

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

int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A vanilla 0/0 token "created by" `source_number` -- what a Saproling Burst's bodies become when
// its last fade counter is removed. def_absent is the real article: a vanilla token has no
// CardDefinition, which is what every watcher loop's LookupCached returns null for.
void PutDyingToken(GameState& s, int controller, int number, int source_number)
{
    Permanent p;
    p.card                  = Card{};
    p.card.m_name           = InternedName{ "Saproling" };
    p.card.m_number         = number;
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes       = { "Saproling" };
    p.card.m_power          = 0;
    p.card.m_toughness      = 0;
    p.controller_index      = controller;
    p.owner_index           = controller;
    p.is_token              = true;
    p.def_absent            = true;
    p.created_by_number     = source_number;
    s.battlefield.push_back(p);
}

int CountTokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { ++n; } }
    return n;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

constexpr int kBurst = 900;   // the fading source the tokens are keyed to

}  // namespace

TEST_CASE("mass death: a 0/0 is not saved by a +1/+1 counter from a death TRIGGER (CR 704.3)")
{
    EnsureCardsLoaded();

    // THE DIVERGENT BOARD, and the whole point of the case:
    //   Daxos, Blessed by the Sun -- own_creature_dies_lifegain 1, so every death GAINS LIFE
    //   Archangel of Thune        -- lifegain_each_own_creature_counters 1, so every life-gain
    //                                EVENT puts a +1/+1 counter on each creature we control
    // With six 0/0 bodies dying together, the first death's trigger chain would pump the other
    // five to 1/1 if the sweep were still deciding who dies one body at a time.
    GameState s = Fresh();
    Put(s, "Daxos, Blessed by the Sun", 0, 1);
    Put(s, "Archangel of Thune",        0, 2);
    for (int i = 0; i < 6; ++i) { PutDyingToken(s, 0, 1000 + i, kBurst); }
    REQUIRE(CountTokens(s) == 6);

    SweepDeadFadeTokens(s, kBurst);

    // ALL SIX. Under the old lazy sweep only the first died and five walked away as 1/1s.
    CHECK(CountTokens(s) == 0);
    for (const Permanent& p : s.battlefield)
    { CHECK_MESSAGE(!p.is_token, "a 0/0 token survived the sweep: ", p.card.m_name.str()); }

    // The triggers themselves must still have FIRED -- the fix is about ordering, not suppression.
    // Six deaths through Daxos is six separate life-gain events (CR 119.10, one per event).
    CHECK(s.players[0].life == 26);
}

TEST_CASE("mass death: DestroyTokensCreatedBy kills every body on the same divergent board")
{
    EnsureCardsLoaded();
    // The Burst's LTB ("destroy all tokens created with this enchantment") is the other sweep.
    // It is NOT subject to the ordering bug -- measured, not assumed: this case still passes with
    // the old guard restored, because the sweep has no toughness VERDICT for a counter to corrupt
    // (it destroys by `created_by_number`, unconditionally). So this case pins the bulk REMOVAL and
    // the source keying, which is what could break here, and the case above is the one that
    // actually detects the SBA-ordering regression.
    GameState s = Fresh();
    Put(s, "Daxos, Blessed by the Sun", 0, 1);
    Put(s, "Archangel of Thune",        0, 2);
    for (int i = 0; i < 5; ++i) { PutDyingToken(s, 0, 1000 + i, kBurst); }
    // A token from a DIFFERENT source must be left alone -- the sweep is keyed to one enchantment.
    PutDyingToken(s, 0, 2000, /*source_number=*/kBurst + 1);
    REQUIRE(CountTokens(s) == 6);

    DestroyTokensCreatedBy(s, kBurst);

    CHECK(CountTokens(s) == 1);
    CHECK(s.battlefield.back().created_by_number == kBurst + 1);
    CHECK(s.players[0].life == 25);
}

TEST_CASE("mass death: a body ABOVE 0 toughness is untouched, counters or not")
{
    EnsureCardsLoaded();
    // The guard must not overreach: SweepDeadFadeTokens kills only what is actually at 0. A body
    // that entered WITH a +1/+1 counter is a 1/1 and survives -- which is precisely the case the
    // user named as the one where counters legitimately do save a creature.
    GameState s = Fresh();
    Put(s, "Daxos, Blessed by the Sun", 0, 1);
    PutDyingToken(s, 0, 1000, kBurst);
    PutDyingToken(s, 0, 1001, kBurst);
    AddPlusCounters(s.battlefield.back(), 1);        // entered with a counter -> 1/1, lives
    REQUIRE(CountTokens(s) == 2);

    SweepDeadFadeTokens(s, kBurst);

    CHECK(CountTokens(s) == 1);
    CHECK(s.battlefield.back().card.m_number == 1001);
    CHECK(s.players[0].life == 21);                  // exactly one death fired
}

TEST_CASE("mass death: the sweep is a no-op when nothing is at 0")
{
    EnsureCardsLoaded();
    // Both sweeps are called on every upkeep of a fading permanent, so the common case is that they
    // do nothing at all. A bulk path that fired here would be a correctness problem, not a perf one.
    GameState s = Fresh();
    Put(s, "Daxos, Blessed by the Sun", 0, 1);
    PutDyingToken(s, 0, 1000, kBurst);
    AddPlusCounters(s.battlefield.back(), 2);
    const std::size_t before = s.battlefield.size();

    SweepDeadFadeTokens(s, kBurst);

    CHECK(s.battlefield.size() == before);
    CHECK(s.players[0].life == 20);                  // no death, no trigger
}
