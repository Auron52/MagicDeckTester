// Sea Creatures I5 (2026-11-17): Sin, Unending Cataclysm (logs/sea-research/sin.md).
//   * "As Sin enters, remove all counters from any number of artifacts, creatures, and enchantments. Sin enters
//     with X +1/+1 counters on it, where X is twice the number of counters removed this way." -- an as-enters
//     replacement at the universal enter cascade (ApplyCounterHarvestAsEnters), one body for both worlds.
//   * The choice is SEARCHED: the cast's "@H?" sentinel is fanned by AppendSubdecisionAxes from a live probe of
//     the board Sin meets ("@H<n>,<n>", identical permanents folded k-of-m); the pin is read only by Sin's entry.
//   * Unpinned entries take DecisionProvider::CounterHarvestDefault (base rule; Sea Creatures: the Sagas only).
//   * USER 2026-11-17: "We should search in the engine for sin, but heuristically prune so that it strips sagas. I
//     don't think there are any other counters in the deck, but if anything else shows up I guess we can leave
//     it?" -- SeaCreaturesProvider::CounterHarvestSearchSubsets, MTG_SEA_SIN_STRIP_SAGAS (default ON, =0 searched).
//   * The dies trigger is a PROVISIONAL deferral (Sin cannot die in this list).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/HeuristicArm.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kSin   = "Sin, Unending Cataclysm";
const char* const kKiora = "Kiora Bests the Sea God";
const char* const kLev   = "Summon: Leviathan";

const CardDefinition& SnDef(const std::string& name)
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

const SeaCreaturesProvider& SnProv()
{
    static const SeaCreaturesProvider p;
    return p;
}

const GenericProvider& SnGeneric()
{
    static const GenericProvider p;
    return p;
}

// MTG_SEA_SIN_STRIP_SAGAS for the scope (this thread's batch arm slot).
struct SnStripArm
{
    explicit SnStripArm(bool on) { heurarm::t_arm[heurarm::SEA_SIN_STRIP_SAGAS] = on ? 1 : 0; }
    ~SnStripArm() { heurarm::t_arm[heurarm::SEA_SIN_STRIP_SAGAS] = -1; }
};

GameState SnBoard(int turn = 7, const DecisionProvider* prov = nullptr)
{
    GameState s;
    s.m_provider          = prov != nullptr ? prov : &SnProv();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.game_seed           = 4242;
    s.players[0].lands_played_this_turn = 1;
    return s;
}

int SnPut(GameState& s, const std::string& name, int number, int controller = 0, int lore = 0)
{
    Permanent p;
    p.card             = SnDef(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    p.lore_counters    = lore;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void SnLands(GameState& s, int n)
{
    // Breeding Pool taps for G or U: n of them pay Sin's {5}{G}{U}.
    for (int i = 0; i < n; ++i) { SnPut(s, "Breeding Pool", 100 + i, 0); }
}

const Permanent* SnFind(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

// The executor's resolution of a Sin cast carrying `pin` (StackEntry::tutor_target).
void SnResolveSin(GameState& s, const std::string& pin, int number = 4242)
{
    const CardDefinition& def = SnDef(kSin);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = number;
    e.controller_index = 0;
    e.tutor_target = pin;
    EffectHandler::Resolve(s, e, def);
}

// Every distinct harvest pin the search offers on Sin's cast from this board.
std::set<std::string> SnPins(const GameState& s)
{
    std::set<std::string> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name == kSin) { out.insert(a.tutor_target.str()); }
        }
    }
    return out;
}
}  // namespace

TEST_CASE("Sea I5: Sin's card data")
{
    const CardDefinition& d = SnDef(kSin);
    CHECK(d.card.m_mana_cost.ManaValue() == 7);
    CHECK(d.card.IsCreature());
    CHECK(d.card.HasSupertype(Supertype::Legendary));
    CHECK(CardHasSubtype(d.card, "Leviathan"));
    CHECK(CardHasSubtype(d.card, "Avatar"));
    CHECK(d.card.HasKeyword(Keyword::Flying));
    CHECK(d.card.HasKeyword(Keyword::Trample));
    CHECK(d.card.m_power.value_or(-1) == 5);
    CHECK(d.card.m_toughness.value_or(-1) == 5);
    CHECK(d.params.as_enters_harvest_counters == 2);
}

TEST_CASE("Sea I5: RemoveAllCounters is the exact twin of TotalCountersOn")
{
    Permanent p;
    p.counters.Add(Counter::Type::PlusOnePlusOne, 3);
    p.counters.Add(Counter::Type::MinusOneMinusOne, 1);
    p.counters.Add(Counter::Type::Loyalty, 4);   // the display mirror of p.loyalty
    p.loyalty = 4;
    int expected = 3 + 1 + 4;
    int k = 1;
    ForEachDedicatedCounterField(p, [&](const char*, int& v) { v = k; expected += k; ++k; });
    CHECK(TotalCountersOn(p) == expected);
    RemoveAllCounters(p);
    CHECK(TotalCountersOn(p) == 0);
    CHECK(p.counters.empty());
    CHECK(p.loyalty == 0);
    ForEachDedicatedCounterField(p, [](const char*, const int& v) { CHECK(v == 0); });
}

TEST_CASE("Sea I5: executor -- a pinned harvest strips exactly the named Sagas (ward is not consulted)")
{
    GameState s = SnBoard();
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnPut(s, kLev, 502, 0, /*lore=*/1);
    SUBCASE("both")
    {
        SnResolveSin(s, "@H501,502");
        REQUIRE(SnFind(s, 4242) != nullptr);
        CHECK(PlusCountersOn(*SnFind(s, 4242)) == 6);
        CHECK(SnFind(s, 4242)->EffectivePower() == 11);
        CHECK(SnFind(s, 501)->lore_counters == 0);
        CHECK(SnFind(s, 502)->lore_counters == 0);
    }
    SUBCASE("only Summon: Leviathan (ward {2} is never paid: the clause is not targeted)")
    {
        SnResolveSin(s, "@H502");
        CHECK(PlusCountersOn(*SnFind(s, 4242)) == 2);
        CHECK(SnFind(s, 501)->lore_counters == 2);
        CHECK(SnFind(s, 502)->lore_counters == 0);
    }
    SUBCASE("none")
    {
        SnResolveSin(s, "@H");
        CHECK(PlusCountersOn(*SnFind(s, 4242)) == 0);
        CHECK(SnFind(s, 4242)->EffectivePower() == 5);
        CHECK(SnFind(s, 501)->lore_counters == 2);
        CHECK(SnFind(s, 502)->lore_counters == 1);
    }
    SUBCASE("a named permanent that left the battlefield (bounced first) is skipped")
    {
        s.battlefield.erase(s.battlefield.begin());   // Kiora gone
        SnResolveSin(s, "@H501,502");
        CHECK(PlusCountersOn(*SnFind(s, 4242)) == 2);
        CHECK(SnFind(s, 502)->lore_counters == 0);
    }
}

TEST_CASE("Sea I5: unpinned, base rule -- every Saga and the opponent's counters")
{
    GameState s = SnBoard(7, &SnGeneric());
    SnPut(s, kKiora, 501, 0, /*lore=*/1);
    SnPut(s, kLev, 502, 0, /*lore=*/2);
    const int opp = SnPut(s, "Walking Ballista", 503, 1);
    s.battlefield[static_cast<std::size_t>(opp)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    SnResolveSin(s, "");   // a cast with no pin (the playout beyond the horizon)
    REQUIRE(SnFind(s, 4242) != nullptr);
    CHECK(PlusCountersOn(*SnFind(s, 4242)) == 2 * (1 + 2 + 2));
    CHECK(SnFind(s, 501)->lore_counters == 0);
    CHECK(SnFind(s, 502)->lore_counters == 0);
    // The stripped 0/0 Ballista dies to the toughness SBA through the doomed queue (drained by Resolve).
    CHECK(SnFind(s, 503) == nullptr);
}

TEST_CASE("Sea I5: unpinned, Sea Creatures -- strip the Sagas, leave anything else (USER 2026-11-17)")
{
    GameState s = SnBoard();
    SnPut(s, kKiora, 501, 0, /*lore=*/1);
    SnPut(s, kLev, 502, 0, /*lore=*/2);
    const int opp = SnPut(s, "Walking Ballista", 503, 1);
    s.battlefield[static_cast<std::size_t>(opp)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    CHECK(ResolveProvider(s).CounterHarvestDefault(s, 0, CounterHarvestCandidates(s, 4242)).size() == 2);
    SnResolveSin(s, "");
    REQUIRE(SnFind(s, 4242) != nullptr);
    CHECK(PlusCountersOn(*SnFind(s, 4242)) == 2 * (1 + 2));
    CHECK(SnFind(s, 501)->lore_counters == 0);
    CHECK(SnFind(s, 502)->lore_counters == 0);
    REQUIRE(SnFind(s, 503) != nullptr);
    CHECK(PlusCountersOn(*SnFind(s, 503)) == 2);
}

TEST_CASE("Sea I5: the unresolved sentinel also takes the default; the pin is read only by Sin's own entry")
{
    GameState s = SnBoard();
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnResolveSin(s, "@H?");
    CHECK(PlusCountersOn(*SnFind(s, 4242)) == 4);

    GameState t = SnBoard();
    SnPut(t, kKiora, 501, 0, /*lore=*/2);
    Permanent sin;
    sin.card = SnDef(kSin).card;
    sin.card.m_number = 4343;
    sin.controller_index = 0;
    sin.owner_index = 0;
    t.battlefield.push_back(sin);
    const std::string none = "@H";
    {
        CounterHarvestPinScope other(9999, &none);   // another entrant's pin: not Sin's
        FireEtbWatchers(t, 0, static_cast<int>(t.battlefield.size()) - 1);
    }
    CHECK(PlusCountersOn(*SnFind(t, 4343)) == 4);   // default, not "none"
    CHECK(SnFind(t, 501)->lore_counters == 0);
}

TEST_CASE("Sea I5: a harvested Saga re-fires chapter I (another Kiora Kraken)")
{
    GameState s = SnBoard();
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnResolveSin(s, "@H501");
    REQUIRE(SnFind(s, 501)->lore_counters == 0);
    int krakens = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { ++krakens; } }
    AdvanceSagas(s);
    int after = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { ++after; } }
    CHECK(after == krakens + 1);
    CHECK(SnFind(s, 501)->lore_counters == 1);
}

TEST_CASE("Sea I5: the Sea prune -- ONE searched subset: every Saga, nothing else")
{
    GameState s = SnBoard();
    SnLands(s, 7);
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnPut(s, kLev, 502, 0, /*lore=*/1);
    const int opp = SnPut(s, "Walking Ballista", 503, 1);
    s.battlefield[static_cast<std::size_t>(opp)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    s.players[0].hand.push_back(SnDef(kSin).card);
    s.players[0].hand.back().m_number = 4242;
    {
        SnStripArm on(true);
        CHECK(SnPins(s) == std::set<std::string>{ "@H501,502" });
    }
    {
        SnStripArm off(false);   // MTG_SEA_SIN_STRIP_SAGAS=0: the engine's full menu
        CHECK(SnPins(s).size() == 8);
    }
}

TEST_CASE("Sea I5: search (MTG_SEA_SIN_STRIP_SAGAS=0) -- every subset is a plan; identical Sagas fold to k-of-m")
{
    SnStripArm off(false);
    SUBCASE("two different Sagas: 4 subsets")
    {
        GameState s = SnBoard();
        SnLands(s, 7);
        SnPut(s, kKiora, 501, 0, /*lore=*/2);
        SnPut(s, kLev, 502, 0, /*lore=*/1);
        s.players[0].hand.push_back(SnDef(kSin).card);
        s.players[0].hand.back().m_number = 4242;
        const std::set<std::string> pins = SnPins(s);
        CHECK(pins == std::set<std::string>{ "@H", "@H501", "@H502", "@H501,502" });
    }
    SUBCASE("two identical lore-1 Leviathans: 3 subsets, not 4")
    {
        GameState s = SnBoard();
        SnLands(s, 7);
        SnPut(s, kLev, 502, 0, /*lore=*/1);
        SnPut(s, kLev, 503, 0, /*lore=*/1);
        s.players[0].hand.push_back(SnDef(kSin).card);
        s.players[0].hand.back().m_number = 4242;
        const std::set<std::string> pins = SnPins(s);
        CHECK(pins == std::set<std::string>{ "@H", "@H502", "@H502,503" });
    }
    SUBCASE("no counters anywhere: the single empty pin, no sentinel escapes")
    {
        GameState s = SnBoard();
        SnLands(s, 7);
        s.players[0].hand.push_back(SnDef(kSin).card);
        s.players[0].hand.back().m_number = 4242;
        const std::set<std::string> pins = SnPins(s);
        CHECK(pins == std::set<std::string>{ "@H" });
    }
}

TEST_CASE("Sea I5: the rollout's apply strips what the pin names (lockstep with the executor)")
{
    SnStripArm off(false);
    GameState s = SnBoard();
    SnLands(s, 7);
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnPut(s, kLev, 502, 0, /*lore=*/1);
    s.players[0].hand.push_back(SnDef(kSin).card);
    s.players[0].hand.back().m_number = 4242;
    int checked = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        std::string pin;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == kSin) { pin = a.tutor_target.str(); } }
        if (pin.empty()) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        REQUIRE(SnFind(t, 4242) != nullptr);
        const bool k = pin.find("501") != std::string::npos, l = pin.find("502") != std::string::npos;
        CHECK(PlusCountersOn(*SnFind(t, 4242)) == 2 * ((k ? 2 : 0) + (l ? 1 : 0)));
        CHECK(SnFind(t, 501)->lore_counters == (k ? 0 : 2));
        CHECK(SnFind(t, 502)->lore_counters == (l ? 0 : 1));
        ++checked;
    }
    CHECK(checked >= 4);
}

TEST_CASE("Sea I5: human play -- the dragon multi-pick, every candidate, default preselected; an empty reply stands")
{
    GameState s = SnBoard();
    SnPut(s, kKiora, 501, 0, /*lore=*/2);
    SnPut(s, kLev, 502, 0, /*lore=*/1);
    const int opp = SnPut(s, "Walking Ballista", 503, 1);   // not a Saga: offered, not preselected
    s.battlefield[static_cast<std::size_t>(opp)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    std::vector<int> seen_heur;
    std::size_t seen_n = 0;
    DragonChooser chooser = [&](const GameState&, int, const std::string&, const std::vector<Card>& cands,
                                int max_puts, const std::vector<int>& heur)
    {
        seen_n = cands.size();
        CHECK(max_puts == static_cast<int>(cands.size()));
        seen_heur = heur;
        return std::vector<int>{};
    };
    DragonChooser* saved = g_play_dragon_chooser;
    g_play_dragon_chooser = &chooser;
    SnResolveSin(s, "");
    g_play_dragon_chooser = saved;
    CHECK(seen_n == 3);
    CHECK(seen_heur == std::vector<int>{ 0, 1 });   // the default: both Sagas
    CHECK(PlusCountersOn(*SnFind(s, 4242)) == 0);   // the empty reply stood
    CHECK(SnFind(s, 501)->lore_counters == 2);
}
