// Sea Creatures I6 (2026-11-17): Trench Gorger (logs/sea-research/gorger.md).
//   * "When this creature enters, you may search your library for any number of land cards, exile them, then
//     shuffle. If you do, this creature has base power and base toughness each equal to the number of cards exiled
//     this way." -- PerformExileLibraryLands (core/LandSearch.h), from the universal own-ETB cascade, both worlds.
//   * The choice is the in-plan land-search pin over ExileLibraryLandsMenu (kLandSearchPinMask = the decline).
//   * USER 2026-11-16: "Trench Gorger is likely a "take all lands" type of effect since it is at the top of the
//     curve, but we'll have to do some testing." / "Actually I guess less than 6." -> SeaCreaturesProvider::
//     RestrictExileLibraryLandsMenu: >= 6 library lands -> exile all; < 6 -> decline. MTG_SEA_GORGER_ALL (=0 searched).
//   * Ledger Q11 (agent default): a bounced Gorger drops its base P/T override (RevealedCard, CR 400.7).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kGorger = "Trench Gorger";
constexpr int kGorgerNum = 4343;

const CardDefinition& GgDef(const std::string& name)
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

Card GgCard(const std::string& name, int number)
{
    Card c     = GgDef(name).card;
    c.m_number = number;
    return c;
}

const SeaCreaturesProvider& GgSea()
{
    static const SeaCreaturesProvider p;
    return p;
}

const GenericProvider& GgGeneric()
{
    static const GenericProvider p;
    return p;
}

// MTG_SEA_GORGER_ALL for the scope (this thread's batch arm slot).
struct GgAllArm
{
    explicit GgAllArm(bool on) { heurarm::t_arm[heurarm::SEA_GORGER_ALL] = on ? 1 : 0; }
    ~GgAllArm() { heurarm::t_arm[heurarm::SEA_GORGER_ALL] = -1; }
};

// A main-phase board with eight untapped Breeding Pools (Gorger's {6}{U}{U}) and a library of `lands` (cycled from
// a fixed list) interleaved with nonland cards.
GameState GgBoard(int lands, const DecisionProvider* prov = nullptr)
{
    GameState s;
    s.m_provider          = prov != nullptr ? prov : &GgSea();
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.game_seed           = 777;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 8; ++i)
    {
        Permanent p;
        p.card             = GgCard("Breeding Pool", 100 + i);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    static const char* const kLandCycle[] = { "Forest", "Island", "Forest", "Misty Rainforest", "Hedge Maze",
                                              "Forest", "Island", "Breeding Pool", "Forest", "Misty Rainforest" };
    int n = 200;
    for (int i = 0; i < lands; ++i)
    {
        s.players[0].library.push_back(GgCard("Spawning Kraken", n++));
        s.players[0].library.push_back(GgCard(kLandCycle[i % 10], n++));
    }
    s.players[0].library.push_back(GgCard("Spawning Kraken", n++));
    return s;
}

int GgLibLands(const GameState& s)
{
    int k = 0;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(s.players[0].library[i]);
        if (d != nullptr && d->card.IsLand()) { ++k; }
    }
    return k;
}

std::vector<int> GgLibNumbers(const GameState& s)
{
    std::vector<int> v;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { v.push_back(s.players[0].library[i].m_number); }
    return v;
}

std::vector<int> GgExileNumbers(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.exile) { v.push_back(c.m_number); }
    return v;
}

const Permanent* GgFind(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == kGorgerNum) { return &p; } }
    return nullptr;
}

// The executor's resolution of a Gorger cast under the in-plan pin list `pins`.
void GgResolve(GameState& s, const std::vector<int>& pins)
{
    const CardDefinition& def = GgDef(kGorger);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = kGorgerNum;
    e.controller_index = 0;
    ScriptedLandSearch sls(pins);
    EffectHandler::Resolve(s, e, def);
    DrainPendingZeroToughness(s);
}

void GgResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// Every plan that is exactly "cast Trench Gorger".
std::vector<TurnSolver::Plan> GgCastPlans(const GameState& s)
{
    std::vector<TurnSolver::Plan> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.actions.size() == 1 && p.actions.front().kind == Action::Kind::CastFromHand
            && p.actions.front().card_name == kGorger) { out.push_back(p); }
    }
    return out;
}
}  // namespace

TEST_CASE("Sea I6: Trench Gorger's card data")
{
    const CardDefinition& d = GgDef(kGorger);
    CHECK(d.card.m_mana_cost.ManaValue() == 8);
    CHECK(d.card.IsCreature());
    CHECK(CardHasSubtype(d.card, "Leviathan"));
    CHECK(d.card.HasKeyword(Keyword::Trample));
    CHECK(d.card.m_power == 6);
    CHECK(d.card.m_toughness == 6);
    CHECK(d.params.etb_exile_library_lands_set_base_pt);
    CHECK(d.params.land_search_count == 0);
}

TEST_CASE("Sea I6: the engine menu -- exile all first, one keep-one per class, every keep-two, nested keep-j, keep-all last")
{
    GameState s = GgBoard(10, &GgGeneric());   // 4 Forest, 2 Island, 2 Misty, 1 Hedge Maze, 1 Breeding Pool
    const ExileLandsMenu m = ExileLibraryLandsMenuEngine(s, 0);
    REQUIRE(m.cls.size() == 5);
    int L = 0;
    for (int c : m.copies) { L += c; }
    CHECK(L == 10);
    auto sum = [](const std::vector<int>& v) { int t = 0; for (int x : v) { t += x; } return t; };
    REQUIRE(!m.keep.empty());
    CHECK(sum(m.keep.front()) == 0);                 // the front: exile all
    CHECK(m.keep.back() == m.copies);                // the 0/0 entry, last
    std::set<std::vector<int>> seen;
    int ones = 0, twos = 0, more = 0;
    for (const std::vector<int>& v : m.keep)
    {
        CHECK(seen.insert(v).second);                // duplicate-free
        for (std::size_t c = 0; c < v.size(); ++c) { CHECK(v[c] <= m.copies[c]); }
        const int k = sum(v);
        if (k == 1) { ++ones; } else if (k == 2) { ++twos; } else if (k > 2 && k < L) { ++more; }
    }
    CHECK(ones == 5);                                // one per class
    CHECK(twos == 10 + 3);                           // C(5,2) pairs + doubles (Forest, Island, Misty)
    CHECK(more == L - 3);                            // keep-3 .. keep-(L-1), one each
    CHECK(static_cast<int>(m.keep.size()) <= kExileLandsMenuCap);
    CHECK(m.truncated == 0);
    // The root prune drops only the 0/0 entry; the decline stays a searched option.
    const ExileLandsMenu r = ExileLibraryLandsMenu(s, 0);
    CHECK(r.keep.size() == m.keep.size() - 1);
    CHECK(r.decline_offered);
    // Independent of library order (the classes are name-sorted, the order is the provider's).
    GameState t = s;
    std::vector<Card> cards;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { cards.push_back(s.players[0].library[i]); }
    while (t.players[0].library.size() > 0) { t.players[0].library.erase(t.players[0].library.begin()); }
    for (std::size_t i = cards.size(); i-- > 0; ) { t.players[0].library.push_back(cards[i]); }
    CHECK(ExileLibraryLandsMenuEngine(t, 0).keep == m.keep);
}

TEST_CASE("Sea I6: exile all sets the base P/T and shuffles (executor)")
{
    GameState s = GgBoard(10, &GgGeneric());
    const int searches0 = s.search_count;
    GgResolve(s, {});                                // unpinned: the front, exile all
    const Permanent* g = GgFind(s);
    REQUIRE(g != nullptr);
    CHECK(g->card.m_power == 10);
    CHECK(g->card.m_toughness == 10);
    CHECK(g->EffectivePower() == 10);
    CHECK(GgLibLands(s) == 0);
    CHECK(s.exile.size() == 10);
    CHECK(s.players[0].library.size() == 11);       // the Krakens stay
    CHECK(s.search_count == searches0 + 1);          // shuffled once
}

TEST_CASE("Sea I6: decline stays 6/6 and does not shuffle")
{
    GameState s = GgBoard(10, &GgGeneric());
    const std::vector<int> lib0 = GgLibNumbers(s);
    const int searches0 = s.search_count;
    GgResolve(s, { kLandSearchPinMask });
    const Permanent* g = GgFind(s);
    REQUIRE(g != nullptr);
    CHECK(g->card.m_power == 6);
    CHECK(g->card.m_toughness == 6);
    CHECK(GgLibNumbers(s) == lib0);                  // the library is untouched, in order
    CHECK(s.exile.empty());
    CHECK(s.search_count == searches0);
}

TEST_CASE("Sea I6: the Sea prune -- >= 6 library lands: exile all; < 6: decline (USER 2026-11-16)")
{
    GgAllArm on(true);
    SUBCASE("6 lands: one option, exile all")
    {
        GameState s = GgBoard(6);
        const ExileLandsMenu m = ExileLibraryLandsMenu(s, 0);
        REQUIRE(m.keep.size() == 1);
        CHECK(std::all_of(m.keep.front().begin(), m.keep.front().end(), [](int x) { return x == 0; }));
        CHECK_FALSE(m.decline_offered);
        CHECK(ExileLibraryLandsPinValues(s, 0) == std::vector<int>{ 0 });
        GgResolve(s, {});
        REQUIRE(GgFind(s) != nullptr);
        CHECK(GgFind(s)->card.m_power == 6);
        CHECK(GgLibLands(s) == 0);
        CHECK(s.exile.size() == 6);
    }
    SUBCASE("5 lands: one option, decline")
    {
        GameState s = GgBoard(5);
        const std::vector<int> lib0 = GgLibNumbers(s);
        const ExileLandsMenu m = ExileLibraryLandsMenu(s, 0);
        CHECK(m.keep.empty());                       // the decline is the front
        CHECK(ExileLibraryLandsPinValues(s, 0) == std::vector<int>{ 0 });
        GgResolve(s, {});
        REQUIRE(GgFind(s) != nullptr);
        CHECK(GgFind(s)->card.m_power == 6);
        CHECK(GgLibNumbers(s) == lib0);
        CHECK(s.exile.empty());
    }
    SUBCASE("the search sees no variant under the prune")
    {
        GameState s = GgBoard(10);
        s.players[0].hand.push_back(GgCard(kGorger, kGorgerNum));
        for (const TurnSolver::Plan& p : GgCastPlans(s)) { CHECK(p.land_search_pins.empty()); }
    }
}

TEST_CASE("Sea I6: MTG_SEA_GORGER_ALL=0 offers the full searched menu (decline included)")
{
    GgAllArm off(false);
    GameState s = GgBoard(10);
    const ExileLandsMenu engine = ExileLibraryLandsMenuEngine(s, 0);
    const std::vector<int> vals = ExileLibraryLandsPinValues(s, 0);
    CHECK(vals.size() == engine.keep.size());        // every entry but the 0/0 one, + the decline
    CHECK(vals.back() == kLandSearchPinMask);
    s.players[0].hand.push_back(GgCard(kGorger, kGorgerNum));
    std::set<int> pins;
    for (const TurnSolver::Plan& p : GgCastPlans(s))
    {
        if (p.land_search_pins.empty()) { pins.insert(0); continue; }
        REQUIRE(p.land_search_pins.size() == 1);
        pins.insert(p.land_search_pins.front());
    }
    CHECK(pins == std::set<int>(vals.begin(), vals.end()));
}

TEST_CASE("Sea I6: executor and rollout agree on every searched entry (lockstep)")
{
    GgAllArm off(false);
    GameState s = GgBoard(8);
    s.players[0].hand.push_back(GgCard(kGorger, kGorgerNum));
    const std::vector<TurnSolver::Plan> plans = GgCastPlans(s);
    REQUIRE(plans.size() > 10);
    std::set<int> bodies;
    for (const TurnSolver::Plan& p : plans)
    {
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState exec = s;
        eng.TakeTurn(exec, true, GgResolveAll);
        const Permanent* gr = GgFind(roll);
        const Permanent* ge = GgFind(exec);
        REQUIRE(gr != nullptr);
        REQUIRE(ge != nullptr);
        CHECK(gr->card.m_power == ge->card.m_power);
        CHECK(gr->card.m_toughness == ge->card.m_toughness);
        CHECK(GgLibNumbers(roll) == GgLibNumbers(exec));
        CHECK(GgExileNumbers(roll) == GgExileNumbers(exec));
        CHECK(roll.search_count == exec.search_count);
        CHECK(gr->card.m_power.value_or(0) + GgLibLands(roll)
              == (p.land_search_pins == std::vector<int>{ kLandSearchPinMask } ? 6 + 8 : 8));
        bodies.insert(gr->card.m_power.value_or(0));
    }
    CHECK(bodies.count(8) == 1);                     // exile all
    CHECK(bodies.count(6) == 1);                     // the decline (and keep-2)
    CHECK(bodies.count(0) == 0);                     // the 0/0 entry is never searched
}

TEST_CASE("Sea I6: human play -- dig yes/no then the dragon multi-pick over every library land")
{
    GgAllArm on(true);
    GameState s = GgBoard(5);   // the AI would decline; the human may still search
    int dig_default = 99;
    DigChooser dig = [&](const GameState&, int, const std::string& src, const std::vector<Card>& ex,
                         const std::vector<int>& legal, int heur)
    {
        CHECK(src.find("(search lands?)") != std::string::npos);
        CHECK(ex.size() == 1);
        CHECK(legal == std::vector<int>{ 0 });
        dig_default = heur;
        return 0;   // search
    };
    std::vector<std::string> names;
    std::vector<int> seen_heur;
    std::vector<int> reply;
    DragonChooser drag = [&](const GameState&, int, const std::string& src, const std::vector<Card>& cands,
                             int max_puts, const std::vector<int>& heur)
    {
        CHECK(src.find("(exile lands)") != std::string::npos);
        CHECK(max_puts == static_cast<int>(cands.size()));
        names.clear();
        for (const Card& c : cands) { names.push_back(c.m_name.str()); }
        seen_heur = heur;
        return reply;
    };
    DigChooser* sd = g_play_dig_chooser;
    DragonChooser* sg = g_play_dragon_chooser;
    g_play_dig_chooser = &dig;
    g_play_dragon_chooser = &drag;
    SUBCASE("two of five exiled: a 2/2")
    {
        reply = { 0, 3 };
        GgResolve(s, {});
        CHECK(dig_default == -1);                    // the Sea prune's front at 5 lands: decline
        CHECK(names.size() == 5);                    // every library land
        CHECK(std::is_sorted(names.begin(), names.end()));
        CHECK(seen_heur.size() == 5);                // the AI default: all
        REQUIRE(GgFind(s) != nullptr);
        CHECK(GgFind(s)->card.m_power == 2);
        CHECK(s.exile.size() == 2);
        CHECK(GgLibLands(s) == 3);
    }
    SUBCASE("an empty reply stands: 0/0, it dies")
    {
        reply = {};
        const int searches0 = s.search_count;
        GgResolve(s, {});
        CHECK(GgFind(s) == nullptr);                 // the toughness SBA
        CHECK(s.search_count == searches0 + 1);      // the search still shuffled
        CHECK(s.exile.empty());
    }
    g_play_dig_chooser = sd;
    g_play_dragon_chooser = sg;
}

TEST_CASE("Sea I6: a bounced Gorger drops its base P/T override (CR 400.7, ledger Q11)")
{
    GameState s = GgBoard(10, &GgGeneric());
    GgResolve(s, {});
    REQUIRE(GgFind(s) != nullptr);
    REQUIRE(GgFind(s)->card.m_power == 10);
    int bi = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == kGorgerNum) { bi = i; } }
    REQUIRE(ReturnPermanentToOwnersHand(s, bi, ""));
    REQUIRE(!s.players[0].hand.empty());
    const Card& back = s.players[0].hand.back();
    CHECK(back.m_number == kGorgerNum);
    CHECK(back.m_power == 6);
    CHECK(back.m_toughness == 6);
    // ...and a recast that declines is a 6/6 again.
    s.players[0].hand.pop_back();
    GgResolve(s, { kLandSearchPinMask });
    REQUIRE(GgFind(s) != nullptr);
    CHECK(GgFind(s)->card.m_power == 6);
}
