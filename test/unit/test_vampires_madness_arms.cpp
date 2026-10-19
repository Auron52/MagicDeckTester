// Unit tests for the Vampires Stage 6a in-window defects D1 / D2 (2026-10-19;
// build/scratch/vampires/stage6a.md section 5): Markov Baron's MADNESS convoke arms.
//   D1. The madness fan priced the convoke arms on the PRE-PLAN board and pinned them by INDEX, so a body
//       the plan adds before the trigger (a Bloodline Keeper token tapped at trailing rank 0, a same-plan
//       cast) was invisible: the arm the line needs was mis-booked or missing, and an index pinned on the
//       pre-plan board meant a different arm (or none -> a decline) on the board at the trigger.
//   D2. The madness path never applied convoke CONTENTION, so the weak-dominance prune "drop the
//       no-convoke cast while a free body helps" also dropped it when a later steal in the same pass needs
//       that body untapped.
// Each line is enumerated by the search AND realised alike by the rollout's ApplyPlan and the executor.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

// A passive-opponent scheduled spawn (GameEngine's shape: no definition, colourless, no subtypes).
int Spawn(GameState& s, int pw, int tf, int number)
{
    Permanent p;
    p.card.m_name = std::to_string(pw) + "/" + std::to_string(tf) + " Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = pw;
    p.card.m_toughness = tf;
    p.card.m_number    = number;
    p.controller_index = 1;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
    return number;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int TappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0 && p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

int VampireTokens(const GameState& s, bool tapped)
{
    int n = 0;
    for (const Permanent& q : s.battlefield)
    {
        if (q.is_token && q.card.IsCreature() && CardHasSubtype(q.card, "Vampire") && q.tapped == tapped) { ++n; }
    }
    return n;
}

void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

bool PumpCastsBaron(const Action& a)
{
    return a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 4 && a.discard_name.str() == "Markov Baron"
        && a.madness_choice > 0;
}

// The rollout's ApplyPlan and the executor (the committed-line pop) must realise `line` alike. A breakpoint
// arm is pinned EMPTY (both worlds honour it) so the comparison is exactly the plan's own actions.
void CheckBothWorlds(const GameState& s, const TurnSolver::Plan& line0, bool pre,
                     const std::function<void(const GameState&, const char*)>& check)
{
    TurnSolver::Plan line = line0;
    if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, pre);
    check(roll, "rollout");
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    if (!pre) { eng.SetSearchPostCombat(true); }
    MtgTestSeam::CommitPhasePlan(eng, line, pre);
    GameState exec = s;
    eng.TakeTurn(exec, pre, ResolveAll);
    check(exec, "executor");
}

// The first enumerated plan whose ROLLOUT realisation satisfies `ok`.
const TurnSolver::Plan* FindRealised(const GameState& s, const std::vector<TurnSolver::Plan>& plans, bool pre,
                                     const std::function<bool(const TurnSolver::Plan&)>& shape,
                                     const std::function<bool(const GameState&)>& ok)
{
    for (const TurnSolver::Plan& p0 : plans)
    {
        if (!shape(p0)) { continue; }
        TurnSolver::Plan p = p0;
        if (p.bp_choice < 0) { p.bp_choice = TurnSolver::kBpEmptyChoice; p.bp_all = true; }
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, pre);
        if (ok(r)) { return &p0; }
    }
    return nullptr;
}

}   // namespace

// ---- D1: the bodies the plan adds before the trigger ----------------------------------------------------

TEST_CASE("D1: 'Keeper token; Condemned discards Baron, cast convoked by the token for two lands' is offered and realised")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 11);          // unsick: its token tap is live, so it is a COSTLY body
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51);
    Hand(s, "Markov Baron", 10);             // madness {2}{B}
    // Two lands. Pre-plan, the no-convoke cast ({2}{B}) is over the mana and only "tap the Keeper" fits.
    // With the token first, the Keeper is TAPPED at the trigger and the token (sick, black) is the free
    // body: "token pays {B}, two Swamps pay {2}" is the line. Before the fix the pinned arm (index 1 =
    // "tap the Keeper" on the pre-plan board) did not exist at the trigger and resolved as a decline.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    auto shape = [](const TurnSolver::Plan& p)
    {
        bool tok = false, mad = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::TapForTokenPay) { tok = true; }
            if (PumpCastsBaron(a)) { mad = true; }
        }
        return tok && mad;
    };
    auto ok = [](const GameState& r)
    {
        return FindNum(r, 10) != nullptr && VampireTokens(r, /*tapped=*/true) == 1 && TappedLands(r) == 2;
    };
    const TurnSolver::Plan* line = FindRealised(s, plans, true, shape, ok);
    REQUIRE_MESSAGE(line != nullptr, "'Keeper token, madness Baron convoked by the token' was not offered (D1)");
    for (const Action& a : line->actions)
    { if (PumpCastsBaron(a)) { CHECK(a.madness_mv == 2); } }   // booked at what it really costs: {2}
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        REQUIRE(FindNum(st, 10) != nullptr);              // Baron on the battlefield
        CHECK(FindNum(st, 11)->tapped);                   // Keeper tapped for its token
        CHECK(VampireTokens(st, /*tapped=*/true) == 1);   // the token convoked
        CHECK(TappedLands(st) == 2);
        CHECK(FindNum(st, 10)->temp_power_bonus == 1);    // it entered before the pump resolved
    });
}

TEST_CASE("D1: 'cast Dire Moon; Condemned discards Baron, convoked by the Dire Moon alone' -- Condemned keeps attacking")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);        // unsick, can attack: a COSTLY body
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    Hand(s, "Vampire of the Dire Moon", 12); // {B}: cast first, sick -> a FREE black body at the trigger
    // Three lands: Dire Moon {B} + Baron for {2} (the Dire Moon pays the {B}) = 3. Pre-plan the no-convoke
    // arm was booked at {2}{B} (1 + 3 > 3: never enumerated), and the arm that fit (index 1, "tap
    // Condemned") realised as "tap the Dire Moon AND Condemned for {1}" -- Condemned lost its attack.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    auto shape = [](const TurnSolver::Plan& p)
    {
        bool dm = false, mad = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Vampire of the Dire Moon") { dm = true; }
            if (PumpCastsBaron(a)) { mad = true; }
        }
        return dm && mad;
    };
    auto ok = [](const GameState& r)
    {
        return FindNum(r, 10) != nullptr && FindNum(r, 12) != nullptr && FindNum(r, 12)->tapped
            && !FindNum(r, 1)->tapped && TappedLands(r) == 3;
    };
    const TurnSolver::Plan* line = FindRealised(s, plans, true, shape, ok);
    REQUIRE_MESSAGE(line != nullptr, "'Dire Moon, madness Baron convoked by the Dire Moon only' was not offered (D1)");
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        REQUIRE(FindNum(st, 10) != nullptr);
        CHECK(FindNum(st, 12)->tapped);
        CHECK_FALSE(FindNum(st, 1)->tapped);
        CHECK(TappedLands(st) == 3);
    });
    // And every madness Baron plan books exactly what its realisation pays (the pin is board-independent).
    for (const TurnSolver::Plan& p0 : plans)
    {
        const Action* pump = nullptr;
        for (const Action& a : p0.actions) { if (PumpCastsBaron(a)) { pump = &a; } }
        if (pump == nullptr) { continue; }
        TurnSolver::Plan p = p0;
        if (p.bp_choice < 0) { p.bp_choice = TurnSolver::kBpEmptyChoice; p.bp_all = true; }
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        if (FindNum(r, 10) == nullptr) { continue; }   // a stranded pin resolves as the decline
        int lands = TappedLands(r);
        for (const Action& a : p0.actions)
        { if (a.kind == Action::Kind::CastFromHand) { lands -= a.cost.ManaValue(); } }
        CHECK(lands == pump->madness_mv);
    }
}

// ---- D2: contention -- a later claimant for the same bodies -------------------------------------------

TEST_CASE("D2: 'Condemned discards Baron, cast it with lands, then steal' -- the free body stays untapped for the steal")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Captivating Vampire", 1);
    Put(s, "Stromkirk Condemned", 2);
    Put(s, "Vampire of the Dire Moon", 3, /*sick=*/true);   // a FREE black body
    Put(s, "Falkenrath Gorger", 4);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    Spawn(s, 4, 4, 500000);
    // Four Vampires + the madness Baron = five untapped Vampires for the steal (count band 1, after the
    // pump's madness cast) -- but only if the Baron is paid with the three Swamps. The dominance prune
    // dropped that arm (a free body was left unused) because the madness path never asked about
    // contention, so the line was unexpressible within one main.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    auto shape = [](const TurnSolver::Plan& p)
    {
        bool st = false, mad = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapSubtypeSteal)
            { st = true; }
            if (PumpCastsBaron(a)) { mad = true; }
        }
        return st && mad;
    };
    auto ok = [](const GameState& r)
    {
        const Permanent* got = FindNum(r, 500000);
        return FindNum(r, 10) != nullptr && got != nullptr && got->controller_index == 0;
    };
    const TurnSolver::Plan* line = FindRealised(s, plans, true, shape, ok);
    REQUIRE_MESSAGE(line != nullptr, "'madness Baron with lands, then steal' was not offered (D2)");
    CheckBothWorlds(s, *line, /*pre=*/true, [](const GameState& st, const char* world)
    {
        INFO(world);
        REQUIRE(FindNum(st, 10) != nullptr);
        REQUIRE(FindNum(st, 500000) != nullptr);
        CHECK(FindNum(st, 500000)->controller_index == 0);
        CHECK(TappedLands(st) == 3);
    });
}

TEST_CASE("D2: with a claimant (a steal source or a second convoke card) the madness options keep the fewer-free arms")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 3, /*sick=*/true);   // free black body
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Card baron = Def("Markov Baron").card;
    baron.m_number = 10;
    const MadnessPending pend{ baron, 0, { Def("Markov Baron").params.madness_cost.value() }, "test" };
    auto has_plain = [&](const GameState& st)
    {
        for (const MadnessOption& o : MadnessOptionsNow(st, pend, MadnessWindow::PreCombatMain, nullptr))
        { if (o.conv_pip == 0 && o.conv_other == 0) { return true; } }
        return false;
    };
    CHECK_FALSE(has_plain(s));            // no claimant: the plain cast is dominated by the free body
    GameState steal = s;
    Put(steal, "Captivating Vampire", 1);
    CHECK(has_plain(steal));              // the steal wants that body untapped
    GameState two = s;
    Hand(two, "Markov Baron", 11);
    CHECK(has_plain(two));                // a second Baron may want it
}

// ---- Rule 10: the selection's own cover books ----------------------------------------------------------

namespace
{
Action BaronPump(int src, int cp, int cg)   // Condemned discarding Markov Baron, cast with cover (cp, cg)
{
    Action a;
    a.kind = Action::Kind::ActivatePump;
    a.card_name = std::string("Stromkirk Condemned");
    a.def = &Def("Stromkirk Condemned");
    a.sac_source_id = src;
    a.gy_exile_mode = 4;
    a.chosen_x = 1;
    a.discard_name = std::string("Markov Baron");
    a.madness_choice = 1;
    a.madness_arm = MadnessCoverPack(cp, cg);
    a.madness_mv = static_cast<std::int8_t>(3 - cp - cg);
    a.cost.generic = a.madness_mv;
    return a;
}
Action CastCard(const std::string& name, int hand_index)
{
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def = &Def(name);
    a.hand_index = hand_index;
    a.cost = Def(name).card.m_mana_cost;
    return a;
}
}   // namespace

TEST_CASE("Rule 10: a cover the trigger cannot pay is the decline twice; one a free arrival dominates is dropped")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);                        // black, can attack: costly
    Put(s, "Vampire of the Dire Moon", 3, /*sick=*/true);    // black, sick: FREE
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    Hand(s, "Falkenrath Gorger", 11);                         // red: cast first, sick -> a FREE other body
    const std::vector<Action> cands = {
        BaronPump(1, 0, 0),          // 0: pays {2}{B}, the free Dire Moon unused
        BaronPump(1, 1, 0),          // 1: the Dire Moon pays {B}
        BaronPump(1, 1, 1),          // 2: + Condemned pays {1}
        BaronPump(1, 1, 2),          // 3: three bodies
        CastCard("Falkenrath Gorger", 1),   // 4
    };
    auto rejects = [&](const GameState& st, std::vector<int> sel)
    { return TurnSolver::VampSweepSubsetRuleRejects(st, cands, sel, 10); };
    CHECK(rejects(s, { 0 }));              // 10b: dominated by cover (1,0)
    CHECK_FALSE(rejects(s, { 1 }));
    CHECK_FALSE(rejects(s, { 2 }));
    CHECK(rejects(s, { 3 }));              // 10a: two bodies on the board, three asked
    CHECK_FALSE(rejects(s, { 4, 3 }));     // ...the Gorger cast first is the third
    CHECK(rejects(s, { 4, 1 }));           // 10b: the Gorger arrives free and would pay {1}
    CHECK_FALSE(rejects(s, { 4, 2 }));     // the Dire Moon and the Gorger, Condemned keeps attacking
    // CONTENTION (D2): a second Baron in hand may want the Dire Moon -- the plain cast is a line again.
    GameState two = s;
    Hand(two, "Markov Baron", 12);
    CHECK_FALSE(rejects(two, { 0 }));
    CHECK(rejects(two, { 3 }));            // 10a holds whatever the contention
}

TEST_CASE("Madness covers: every enumerated cast pin is realised as a cast (no decline twins), alike in both worlds")
{
    EnsureCardsLoaded();
    std::vector<GameState> boards;
    {
        GameState s = Fresh();   // the D1 Keeper board
        Put(s, "Bloodline Keeper", 11); Put(s, "Stromkirk Condemned", 1);
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Markov Baron", 10);
        boards.push_back(s);
    }
    {
        GameState s = Fresh();   // the D1 same-plan cast board, three lands
        Put(s, "Stromkirk Condemned", 1);
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
        Hand(s, "Markov Baron", 10); Hand(s, "Vampire of the Dire Moon", 12); Hand(s, "Falkenrath Gorger", 13);
        boards.push_back(s);
    }
    {
        GameState s = Fresh();   // the D2 steal board
        Put(s, "Captivating Vampire", 1); Put(s, "Stromkirk Condemned", 2);
        Put(s, "Vampire of the Dire Moon", 3, true); Put(s, "Falkenrath Gorger", 4);
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
        Hand(s, "Markov Baron", 10);
        Spawn(s, 4, 4, 500000);
        boards.push_back(s);
    }
    for (std::size_t bi = 0; bi < boards.size(); ++bi)
    {
        const GameState& s = boards[bi];
        const long long unpinned_before = g_madness_unpinned_in_window.load();
        int casts = 0;
        for (const TurnSolver::Plan& p0 : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            const Action* pump = nullptr;
            for (const Action& a : p0.actions) { if (PumpCastsBaron(a)) { pump = &a; } }
            if (pump == nullptr) { continue; }
            ++casts;
            std::string desc;
            for (const Action& a : p0.actions)
            { desc += a.card_name.str() + "/M" + std::to_string(static_cast<int>(a.madness_choice)) + "."
                    + std::to_string(static_cast<int>(a.madness_arm)) + " "; }
            INFO("board " << bi << " [" << desc << "]");
            std::vector<GameState> worlds;
            CheckBothWorlds(s, p0, /*pre=*/true, [&](const GameState& st, const char* world)
            {
                INFO(world);
                CHECK(FindNum(st, 10) != nullptr);   // the pinned cover WAS paid (rule 10a / the mana gate)
                worlds.push_back(st);
            });
            REQUIRE(worlds.size() == 2);
            CHECK(worlds[0].battlefield.size() == worlds[1].battlefield.size());
            for (const Permanent& q : worlds[0].battlefield)
            {
                if (q.card.m_number >= 1000) { continue; }   // tokens: numbered per world
                const Permanent* e = FindNum(worlds[1], q.card.m_number);
                REQUIRE(e != nullptr);
                CHECK(e->tapped == q.tapped);
                CHECK(e->controller_index == q.controller_index);
            }
            CHECK(TappedLands(worlds[0]) == TappedLands(worlds[1]));
        }
        CHECK(casts > 0);
        CHECK(g_madness_unpinned_in_window.load() == unpinned_before);
    }
}
