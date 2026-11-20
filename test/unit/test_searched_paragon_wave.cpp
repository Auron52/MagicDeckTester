// The SEARCHED engine defaults for Nykthos Paragon's spend and Genesis Wave's put subset (2026-11-20,
// docs/design/provider-heuristic-required.md). USER 2026-11-20: "The default should be searched and probably
// the analysis should be given a warning if we don't override the policy."
//
//  1. Resolution: a Paragon spend list pins each eligible gain event's count; past the list = 0 (decline).
//  2. Resolution: the pins are bound to ONE state object -- a copy (a nested re-solve's candidate) never
//     consumes them and takes the generic default.
//  3. Resolution: a Wave skip mask puts exactly the unmasked legal reveals.
//  4. Enumeration: with no deck policy the fan-out emits the spend vectors / per-name subsets, sized off the
//     plan's own probe; the measurement arm MTG_REQHEUR_OLD_DEFAULT=1 emits none.
//  5. Lockstep: the real executor realises a pinned variant exactly as the rollout's apply scores it.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void LoadCardsPw()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DPw(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutPw(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = DPw(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandPw(GameState& s, const std::string& name, int number)
{
    Card c = DPw(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

void LibPw(GameState& s, const std::string& name, int number)
{
    Card c = DPw(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

GameState FreshPw(int turn = 6)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.game_seed           = 4243;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int PlusPw(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_number != number) { continue; }
        int n = 0;
        for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
        return n;
    }
    return -1;
}

int CountPw(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool CastsPw(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

struct SearchedPlayOn
{
    bool saved;
    SearchedPlayOn() : saved(g_searched_play) { TurnSolver::SetSearchedPlay(true); }
    ~SearchedPlayOn() { TurnSolver::SetSearchedPlay(saved); }
};

struct ArmPw
{
    explicit ArmPw(heurarm::Slot s, int v) { heurarm::t_arm[static_cast<int>(s)] = static_cast<std::int8_t>(v); }
    ~ArmPw() { heurarm::Clear(); }
};

// One Paragon (unused), one Elf, four lands for Feed the Clan {1}{G} + Heroes' Reunion {G}{W}.
GameState ParagonBoard()
{
    GameState s = FreshPw();
    s.players[0].lands_played_this_turn = 1;
    PutPw(s, "Nykthos Paragon", 1);
    PutPw(s, "Llanowar Elves", 2);
    PutPw(s, "Forest", 10); PutPw(s, "Forest", 11); PutPw(s, "Plains", 12); PutPw(s, "Plains", 13);
    HandPw(s, "Feed the Clan", 20);
    HandPw(s, "Heroes' Reunion", 21);
    for (int i = 0; i < 10; ++i) { LibPw(s, "Forest", 60 + i); }
    return s;
}

}   // namespace

TEST_CASE("searched Paragon: the spend list pins each gain event; past the list declines")
{
    LoadCardsPw();
    GameState s = FreshPw();
    PutPw(s, "Nykthos Paragon", 1);
    PutPw(s, "Llanowar Elves", 2);
    const std::vector<int> pins = { 0, 1 };   // decline the 2, spend on the 5
    {
        ScriptedParagonSpend sp(pins, static_cast<const void*>(&s));
        GainLife(s, 0, 2);
        CHECK(PlusPw(s, 2) == 0);
        GainLife(s, 0, 5);
        CHECK(PlusPw(s, 2) == 5);
    }
    GameState t = FreshPw();
    PutPw(t, "Nykthos Paragon", 1);
    PutPw(t, "Llanowar Elves", 2);
    const std::vector<int> none = { 0 };      // one event listed (0), the second past the end -> 0
    {
        ScriptedParagonSpend sp(none, static_cast<const void*>(&t));
        GainLife(t, 0, 2);
        GainLife(t, 0, 5);
    }
    CHECK(PlusPw(t, 2) == 0);
    const bool used0 = t.battlefield[0].lifegain_counters_used_this_turn;
    CHECK_FALSE(used0);
}

TEST_CASE("searched Paragon: a COPY of the pinned state never consumes the list (the generic greedy runs)")
{
    LoadCardsPw();
    GameState s = FreshPw();
    PutPw(s, "Nykthos Paragon", 1);
    PutPw(s, "Llanowar Elves", 2);
    const std::vector<int> pins = { 0, 1 };
    ScriptedParagonSpend sp(pins, static_cast<const void*>(&s));
    GameState copy = s;
    GainLife(copy, 0, 2);                       // greedy on the copy: spends on the first gain
    CHECK(PlusPw(copy, 2) == 2);
    CHECK(g_paragon_cursor == 0);               // the pinned state's list is untouched
    GainLife(s, 0, 2);
    GainLife(s, 0, 5);
    CHECK(PlusPw(s, 2) == 5);
}

TEST_CASE("searched Wave: a skip mask puts exactly the unmasked legal reveals")
{
    LoadCardsPw();
    GameState s = FreshPw();
    LibPw(s, "Forest", 50); LibPw(s, "Llanowar Elves", 51); LibPw(s, "Forest", 52);
    {
        ScriptedWaveSkip ws(/*mask=*/2, static_cast<const void*>(&s));   // skip candidate 1 (the Elf)
        PerformGenesisWave(s, 0, 3, "Genesis Wave");
    }
    CHECK(CountPw(s, "Forest") == 2);
    CHECK(CountPw(s, "Llanowar Elves") == 0);
    GameState t = FreshPw();
    LibPw(t, "Forest", 50); LibPw(t, "Llanowar Elves", 51); LibPw(t, "Forest", 52);
    {
        ScriptedWaveSkip ws(2, static_cast<const void*>(&s));   // bound to ANOTHER object: inert on t
        PerformGenesisWave(t, 0, 3, "Genesis Wave");
    }
    CHECK(CountPw(t, "Forest") == 2);
    CHECK(CountPw(t, "Llanowar Elves") == 1);
}

TEST_CASE("searched Paragon: the fan-out emits the spend vectors, and the executor realises one exactly")
{
    LoadCardsPw();
    SearchedPlayOn sp;
    const GameState s = ParagonBoard();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* base = nullptr;
    std::vector<const TurnSolver::Plan*> vars;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!CastsPw(p, "Feed the Clan") || !CastsPw(p, "Heroes' Reunion")) { continue; }
        if (p.paragon_spend_pins.empty()) { if (base == nullptr) { base = &p; } }
        else { vars.push_back(&p); }
    }
    REQUIRE_MESSAGE(base != nullptr, "no base plan casting both gains");
    // Two events, one copy: [0,0] and [0,1] (the base's own [1,0] is not re-emitted).
    bool decline_all = false, second = false, first = false;
    for (const TurnSolver::Plan* v : vars)
    {
        if (v->actions.size() != base->actions.size()) { continue; }
        if (v->paragon_spend_pins == std::vector<int>{ 0, 0 }) { decline_all = true; }
        if (v->paragon_spend_pins == std::vector<int>{ 0, 1 }) { second = true; }
        if (v->paragon_spend_pins == std::vector<int>{ 1, 0 }) { first = true; }
    }
    CHECK(decline_all);
    CHECK(second);
    CHECK_FALSE(first);

    TurnSolver::Plan pinned = *base;
    pinned.paragon_spend_pins = { 0, 1 };
    GameState rb = s, rv = s;
    TurnSolver::ApplyPlan(rb, *base, true);
    TurnSolver::ApplyPlan(rv, pinned, true);
    CHECK(PlusPw(rb, 2) != PlusPw(rv, 2));      // the spend moved to the other gain

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, pinned, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, [](GameState&) {});
    CHECK(PlusPw(exec, 2) == PlusPw(rv, 2));
    CHECK(PlusPw(exec, 1) == PlusPw(rv, 1));
}

TEST_CASE("searched Paragon / Wave: the measurement arm MTG_REQHEUR_OLD_DEFAULT=1 emits no variant")
{
    LoadCardsPw();
    SearchedPlayOn sp;
    ArmPw arm(heurarm::REQHEUR_OLD_DEFAULT, 1);
    const GameState s = ParagonBoard();
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        CHECK(p.paragon_spend_pins.empty());
        CHECK(p.wave_skip_choice == -1);
    }
}

TEST_CASE("searched Wave: the fan-out emits every per-name subset of the probe's reveals; the executor realises one")
{
    LoadCardsPw();
    SearchedPlayOn sp;
    GameState s = FreshPw();
    s.players[0].lands_played_this_turn = 1;
    for (int n = 1; n <= 6; ++n) { PutPw(s, "Forest", n); }
    HandPw(s, "Genesis Wave", 20);
    LibPw(s, "Forest", 50); LibPw(s, "Llanowar Elves", 51); LibPw(s, "Forest", 52);
    for (int i = 0; i < 8; ++i) { LibPw(s, "Plains", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* base = nullptr;
    std::vector<int> masks;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!CastsPw(p, "Genesis Wave") || p.actions.size() != 1 || p.actions[0].chosen_x != 3) { continue; }
        if (p.wave_skip_choice < 0) { if (base == nullptr) { base = &p; } }
        else { masks.push_back(p.wave_skip_choice); }
    }
    REQUIRE_MESSAGE(base != nullptr, "no X=3 Genesis Wave plan");
    // Forest x2 (positions 0, 2) and Elves x1 (position 1): (2+1) * (1+1) = 6 per-name subsets.
    std::sort(masks.begin(), masks.end());
    masks.erase(std::unique(masks.begin(), masks.end()), masks.end());
    CHECK(masks == std::vector<int>{ 0, 2, 4, 5, 6, 7 });

    TurnSolver::Plan pinned = *base;
    pinned.wave_skip_choice = 2;                 // put both Forests, mill the Elf
    GameState rv = s;
    TurnSolver::ApplyPlan(rv, pinned, true);
    CHECK(CountPw(rv, "Llanowar Elves") == 0);
    CHECK(CountPw(rv, "Forest") == 8);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, pinned, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, [](GameState&) {});
    CHECK(CountPw(exec, "Llanowar Elves") == 0);
    CHECK(CountPw(exec, "Forest") == 8);
}
