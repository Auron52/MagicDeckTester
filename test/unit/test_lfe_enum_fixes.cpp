// Unit tests for the LandfallElementals claude-play sweep's enumeration defects (2026-10-19):
//
//  1. CRACK POSITION vs A LIBRARY-TOP READER (s61002 / s61016): a land-search crack SHUFFLES, so it
//     must be orderable BEFORE a Risen Reef look / a Flamekin Harbinger top tutor in one plan -- the
//     Plan::land_search_act_slot twins now fire for readers, and a PRE-CAST SUBSET (slot <= -2) puts
//     some cracks before the casts and the rest after.
//  2. LAND-SLOT SUFFIX UNPAYABLE (s61005): a positioned land whose casts AFTER the land drop is not
//     emitted (SlotPrefixProbe::drops).
//  3. FOUNDATION BREAKER EVOKED FOR NOTHING (s61001 / s61007): no plan evokes it while nothing can read
//     its entry (PlanHasDeadOptionalEvoke).
//  4. The landfall self-pump event reports the FULL P/T (lord bonus included).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLfx()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardLfx(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

GameState FreshLfx()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 61002;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    return s;
}

Permanent& PutLfx(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = CardLfx(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void LibLfx(GameState& s, const std::vector<std::string>& names, int first_number)
{
    int n = first_number;
    for (const std::string& name : names) { s.players[0].library.push_back(CardLfx(name, n++)); }
}

bool HasAction(const TurnSolver::Plan& p, const std::string& card, Action::Kind kind)
{
    for (const Action& a : p.actions) { if (a.card_name.str() == card && a.kind == kind) { return true; } }
    return false;
}

int CountLandSearchActs(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (TurnSolver::IsLandSearchActivation(a)) { ++n; } }
    return n;
}

int CountNamedLfx(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool InHandLfx(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Crack position: 'crack Terramorphic, THEN Harbinger' keeps the tutored card on top")
{
    EnsureCardsLfx();
    GameState s = FreshLfx();
    PutLfx(s, "Mountain", 20);
    PutLfx(s, "Terramorphic Expanse", 21);
    s.players[0].hand.push_back(CardLfx("Flamekin Harbinger", 1));
    LibLfx(s, { "Swords to Plowshares", "Fulminator Mage", "Forest", "Swords to Plowshares",
                "Swords to Plowshares", "Swords to Plowshares", "Brushfire Elemental" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* base = nullptr;
    const TurnSolver::Plan* first = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 2 || p.tutor_choice != -1 || !p.look_top_pins.empty()) { continue; }
        if (!HasAction(p, "Flamekin Harbinger", Action::Kind::CastFromHand) || CountLandSearchActs(p) != 1) { continue; }
        if (p.land_search_act_slot == -1 && base == nullptr) { base = &p; }
        if (p.land_search_act_slot == 0 && first == nullptr) { first = &p; }
    }
    REQUIRE(base != nullptr);
    REQUIRE(first != nullptr);   // the crack-FIRST twin exists: a top tutor is a library reader
    GameState t = s;
    TurnSolver::ApplyPlan(t, *first, true);
    REQUIRE(!t.players[0].library.empty());
    const CardDefinition* top = CardDatabase::Instance().LookupCached(t.players[0].library[0]);
    REQUIRE(top != nullptr);
    CHECK(CardHasSubtype(top->card, "Elemental"));   // the found card survived: the shuffle came first
    CHECK(CountNamedLfx(t, "Forest") == 1);
}

TEST_CASE("Crack position: two cracks around a Vial-put Risen Reef -- the PRE-CAST SUBSET twin")
{
    EnsureCardsLfx();
    GameState s = FreshLfx();
    for (int i = 0; i < 3; ++i) { PutLfx(s, "Mountain", 20 + i); }
    PutLfx(s, "Myriad Landscape", 25);
    PutLfx(s, "Terramorphic Expanse", 26);
    Permanent& vial = PutLfx(s, "Aether Vial", 27);
    vial.charge_counters = 3;
    s.players[0].hand.push_back(CardLfx("Risen Reef", 1));
    LibLfx(s, { "Swords to Plowshares", "Forest", "Forest", "Island", "Swords to Plowshares",
                "Waterlogged Grove", "Swords to Plowshares", "Forest" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* myriad_first = nullptr;
    int subsets = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!HasAction(p, "Risen Reef", Action::Kind::ActivateVial) || CountLandSearchActs(p) != 2) { continue; }
        const int mask = TurnSolver::LandSearchActFirstMask(p.land_search_act_slot);
        // the default-pick twin (land-search pin variants pick other basics -- or none -- and so
        // legitimately change the card the look sees)
        if (mask == 0 || !p.look_top_pins.empty() || !p.land_search_pins.empty() || p.bp_choice >= 0) { continue; }
        ++subsets;
        const std::vector<Action> pre = TurnSolver::LandSearchActsMasked(p.actions, mask, true);
        if (pre.size() == 1 && pre[0].card_name.str() == "Myriad Landscape" && myriad_first == nullptr) { myriad_first = &p; }
    }
    CHECK(subsets >= 2);   // {Myriad} first, {Expanse} first
    REQUIRE(myriad_first != nullptr);
    // The Reef's look must see the top AFTER the Myriad crack's shuffle: crack a copy alone to learn it.
    GameState probe = s;
    TurnSolver::ApplyLandSearchActs(probe, TurnSolver::LandSearchActsMasked(myriad_first->actions,
        TurnSolver::LandSearchActFirstMask(myriad_first->land_search_act_slot), true));
    REQUIRE(!probe.players[0].library.empty());
    const std::string seen = probe.players[0].library[0].m_name.str();
    GameState t = s;
    TurnSolver::ApplyPlan(t, *myriad_first, true);
    CHECK(CountNamedLfx(t, "Myriad Landscape") == 0);
    CHECK(CountNamedLfx(t, "Terramorphic Expanse") == 0);   // the rest cracked in the trailing pass
    CHECK(CountNamedLfx(t, "Risen Reef") == 1);
    const CardDefinition* sd = CardDatabase::Instance().Lookup(seen);
    REQUIRE(sd != nullptr);
    if (sd->card.IsLand()) { CHECK(CountNamedLfx(t, seen) >= 1); }
    else                   { CHECK(InHandLfx(t, seen)); }
}

TEST_CASE("Land slot: a position whose casts AFTER the land drop is not emitted (s61005 T4)")
{
    EnsureCardsLfx();
    GameState s = FreshLfx();
    s.players[0].lands_played_this_turn = 0;
    PutLfx(s, "Akoum Hellhound", 1);
    PutLfx(s, "Akoum Hellhound", 2);
    PutLfx(s, "Brushfire Elemental", 3);
    PutLfx(s, "Flamekin Harbinger", 4);
    PutLfx(s, "Island", 20);
    PutLfx(s, "Secluded Courtyard", 21);
    PutLfx(s, "Unclaimed Territory", 22);
    s.players[0].hand.push_back(CardLfx("Brushfire Elemental", 5));
    s.players[0].hand.push_back(CardLfx("Mossborn Hydra", 6));
    s.players[0].hand.push_back(CardLfx("Smokebraider", 7));
    s.players[0].hand.push_back(CardLfx("Waterlogged Grove", 8));
    LibLfx(s, { "Swords to Plowshares", "Swords to Plowshares" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool realised = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_slot == 0) { continue; }
        // No positioned plan may drop one of its casts when applied.
        std::vector<std::string> dropped;
        std::vector<std::string>* saved = g_play_dropped_cast_sink;
        g_play_dropped_cast_sink = &dropped;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        g_play_dropped_cast_sink = saved;
        CHECK_MESSAGE(dropped.empty(), "positioned plan drops ", dropped.size(), " cast(s); land ", p.land_to_play);
        if (p.land_to_play == "Waterlogged Grove" && p.actions.size() == 1
            && HasAction(p, "Brushfire Elemental", Action::Kind::CastFromHand)) { realised = true; }
    }
    CHECK(realised);   // the line the degenerate clone realised ("Brushfire, then the Grove") stays
}

TEST_CASE("Foundation Breaker: no evoke while nothing can read its entry")
{
    EnsureCardsLfx();
    auto evokes = [](const std::vector<TurnSolver::Plan>& plans, bool* with_reef_cast) -> int
    {
        int n = 0;
        if (with_reef_cast != nullptr) { *with_reef_cast = true; }
        for (const TurnSolver::Plan& p : plans)
        {
            bool ev = false, reef = false;
            for (const Action& a : p.actions)
            {
                if (a.card_name.str() == "Foundation Breaker" && a.evoke) { ev = true; }
                if (a.card_name.str() == "Risen Reef") { reef = true; }
            }
            if (!ev) { continue; }
            ++n;
            if (with_reef_cast != nullptr && !reef) { *with_reef_cast = false; }
        }
        return n;
    };
    {
        // Nothing anywhere: the evoke is two mana and a card for nothing.
        GameState s = FreshLfx();
        PutLfx(s, "Forest", 20);
        PutLfx(s, "Forest", 21);
        s.players[0].hand.push_back(CardLfx("Foundation Breaker", 1));
        LibLfx(s, { "Swords to Plowshares" }, 10);
        CHECK(evokes(TurnSolver::EnumerateMainPlans(s, true), nullptr) == 0);
    }
    {
        // A Reef on the battlefield reads the entry: offered.
        GameState s = FreshLfx();
        PutLfx(s, "Forest", 20);
        PutLfx(s, "Forest", 21);
        PutLfx(s, "Risen Reef", 2);
        s.players[0].hand.push_back(CardLfx("Foundation Breaker", 1));
        LibLfx(s, { "Swords to Plowshares" }, 10);
        CHECK(evokes(TurnSolver::EnumerateMainPlans(s, true), nullptr) > 0);
    }
    {
        // A Reef in HAND: the evoke is offered only together with the Reef's cast.
        GameState s = FreshLfx();
        PutLfx(s, "Forest", 20);
        PutLfx(s, "Forest", 21);
        PutLfx(s, "Forest", 22);
        PutLfx(s, "Island", 23);
        PutLfx(s, "Forest", 24);
        s.players[0].hand.push_back(CardLfx("Foundation Breaker", 1));
        s.players[0].hand.push_back(CardLfx("Risen Reef", 2));
        LibLfx(s, { "Swords to Plowshares" }, 10);
        bool all_with_reef = false;
        CHECK(evokes(TurnSolver::EnumerateMainPlans(s, true), &all_with_reef) > 0);
        CHECK(all_with_reef);
    }
}

TEST_CASE("Landfall self-pump event reports the FULL P/T (Incandescent Soulstoke's lord bonus included)")
{
    EnsureCardsLfx();
    GameState s = FreshLfx();
    PutLfx(s, "Akoum Hellhound", 1);           // 0/1, +2/+2 per land
    PutLfx(s, "Incandescent Soulstoke", 2);    // other Elementals get +1/+1
    std::vector<PlayEvent> events;
    std::vector<PlayEvent>* saved = g_play_event_sink;
    g_play_event_sink = &events;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    EnterLand(s, *forest, 30, /*resolve_full_etb=*/true, /*fire_landfall=*/true, LandEntryTap::ForcedTapped);
    g_play_event_sink = saved;
    bool found = false;
    for (const PlayEvent& e : events)
    {
        if (e.text.find("Akoum Hellhound") == std::string::npos) { continue; }
        found = true;
        CHECK(e.text.find("now 3/4") != std::string::npos);   // 0/1 + 2/2 + the lord's 1/1
    }
    CHECK(found);
}
