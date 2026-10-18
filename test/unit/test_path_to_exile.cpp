// Path to Exile (Colossus Hammer onboarding, 2026-10-17):
//   "Exile target creature. Its controller may search their library for a basic land card, put that
//    card onto the battlefield tapped, then shuffle."
//
// The removal template's exile branch + land_search_for_removal_controller, resolved in BOTH worlds
// by the shared ResolveCreatureRemovalAt (SpellEffects.h). These pin what a game log cannot show:
//   1. a self-target Path exiles our creature and fetches a TAPPED basic (search_count +1, the land
//      drop untouched); the basic is the searched land_search_pins axis (Plains vs Mountain: width 2);
//      the rollout (ApplyPlan) and the executor (AIEngine through the committed-line seam) realise
//      byte-identical boards, libraries and zones;
//   2. an opponent-target Path exiles the creature and nothing else: no land, no shuffle, no pin;
//      the ColossusHammer provider refuses that cast, the base provider offers it;
//   3. the exile branch now DETACHES (the named bug fix): an exiled Colossus Hammer host leaves the
//      Hammer on the battlefield with equipped_to == 0 -- in both worlds, and for Swords too;
//   4. the self-target cast is one variant per own-creature IDENTITY class (two bare Duelists fold;
//      an equipped one does not), and the second Path names the class's second member, so "Path both
//      Duelists" is one plan; no creature anywhere -> no Path cast;
//   5. a stale self-target (the creature left before the cast) is not cast in either world;
//   6. human play: the board click defaults to our lowest-power creature, -1 at the tutor_etb prompt
//      declines (no land, no shuffle), picking the opponent's creature asks no land prompt.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr const char* kPath = "Path to Exile";

void EnsureCardsPe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefPe(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const ColossusHammerProvider& HammerPe()
{
    static const ColossusHammerProvider p;
    return p;
}

// Path to Exile is never cast on this deck by default (USER 2026-10-18, MTG_COLOSSUS_NO_PATH --
// ColossusHammerProvider::NeverCast). These tests exercise the Path machinery itself (the cast, its
// lockstep, the self-target classes), so they run with the exclusion OFF; the last test pins the default.
struct PathOfferedPe
{
    std::int8_t saved;
    PathOfferedPe() : saved(heurarm::t_arm[heurarm::COLOSSUS_NO_PATH]) { heurarm::t_arm[heurarm::COLOSSUS_NO_PATH] = 0; }
    ~PathOfferedPe() { heurarm::t_arm[heurarm::COLOSSUS_NO_PATH] = saved; }
};

int PutPe(GameState& s, const std::string& name, int number, int controller = 0, bool tapped = false,
          bool sick = false)
{
    Permanent p;
    p.card              = DefPe(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// An opponent 1/1 creature token (the passive opponent's spawn shape); returns its number.
int SpawnPe(GameState& s)
{
    CreateToken(s, 1, 1, 1, { "Spirit" }, "", {});
    return s.battlefield.back().card.m_number;
}

// Turn 4, ColossusHammer provider (nullptr = the base provider), `plains` untapped Plains (1..), the
// hand `hand` (numbered 20..), a library of 6 Plains then 4 Mountains (100..) unless `mountains` is 0.
GameState BoardPe(int plains, const std::vector<std::string>& hand, bool hammer_provider = true,
                  int mountains = 4)
{
    EnsureCardsPe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 777;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = hammer_provider ? &HammerPe() : nullptr;
    for (int k = 0; k < plains; ++k) { PutPe(s, "Plains", 1 + k); }
    int h = 20;
    for (const std::string& n : hand)
    {
        Card c = DefPe(n).card;
        c.m_number = h++;
        s.players[0].hand.push_back(c);
    }
    int l = 100;
    for (int k = 0; k < 6; ++k)         { Card c = DefPe("Plains").card;   c.m_number = l++; s.players[0].library.push_back(c); }
    for (int k = 0; k < mountains; ++k) { Card c = DefPe("Mountain").card; c.m_number = l++; s.players[0].library.push_back(c); }
    return s;
}

const Permanent* FindPe(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountBf(const GameState& s, const std::string& name, bool tapped)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name && p.tapped == tapped) { ++n; } }
    return n;
}

bool InZone(const std::vector<Card>& z, int number)
{
    for (const Card& c : z) { if (c.m_number == number) { return true; } }
    return false;
}

bool IsPathCast(const Action& a) { return a.kind == Action::Kind::CastFromHand && a.card_name == kPath; }

// Plans whose ONLY action is one Path cast naming `target` (no land).
std::vector<const TurnSolver::Plan*> SolePathPlans(const std::vector<TurnSolver::Plan>& plans, int target)
{
    std::vector<const TurnSolver::Plan*> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && IsPathCast(p.actions[0]) && p.actions[0].enchant_target == target
            && p.land_to_play.empty())
        { out.push_back(&p); }
    }
    return out;
}

// The result points INTO `plans`, so a temporary list would leave every pointer dangling (a Plan's
// freed storage then reads as garbage -- a non-cast Action::Kind -- and ApplyPlan silently casts
// nothing). Bind the list to a named variable first; the rvalue overload makes the slip a compile error.
std::vector<const TurnSolver::Plan*> SolePathPlans(std::vector<TurnSolver::Plan>&& plans, int target) = delete;

// Every enchant_target a Path cast takes across the plans.
std::set<int> PathTargets(const std::vector<TurnSolver::Plan>& plans)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (IsPathCast(a)) { out.insert(a.enchant_target); } } }
    return out;
}

void ResolveStackPe(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackPe);
    return exec;
}

std::vector<int> Numbers(const std::vector<Card>& z)
{
    std::vector<int> v;
    for (const Card& c : z) { v.push_back(c.m_number); }
    return v;
}
std::vector<std::string> Names(const std::vector<Card>& z)
{
    std::vector<std::string> v;
    for (const Card& c : z) { v.push_back(c.m_name.str()); }
    return v;
}
bool NamedIn(const std::vector<Card>& z, const std::string& name)
{
    for (const Card& c : z) { if (c.m_name.str() == name) { return true; } }
    return false;
}
std::string SetStr(const std::set<int>& t)
{
    std::string o = "{";
    for (int x : t) { o += std::to_string(x) + ","; }
    return o + "}";
}
std::vector<int> LibNumbers(const GameState& s)
{
    std::vector<int> v;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { v.push_back(s.players[0].library[i].m_number); }
    return v;
}
std::vector<std::string> BoardSig(const GameState& s)
{
    std::vector<std::string> v;
    for (const Permanent& p : s.battlefield)
    {
        v.push_back(p.card.m_name.str() + "#" + std::to_string(p.card.m_number) + (p.tapped ? "/t" : "/u")
                    + "/e" + std::to_string(p.equipped_to) + "/c" + std::to_string(p.controller_index));
    }
    return v;
}

void CheckSameWorld(const GameState& roll, const GameState& exec)
{
    CHECK(BoardSig(roll) == BoardSig(exec));
    CHECK(LibNumbers(roll) == LibNumbers(exec));
    CHECK(Numbers(roll.exile) == Numbers(exec.exile));
    // By NAME: the rollout files the spell's definition card (m_number 0) where the executor files
    // the cast copy -- a pre-existing, Path-independent difference nothing reads.
    CHECK(Names(roll.players[0].graveyard) == Names(exec.players[0].graveyard));
    CHECK(Numbers(roll.players[0].hand) == Numbers(exec.players[0].hand));
    CHECK(roll.search_count == exec.search_count);
    CHECK(roll.players[0].life == exec.players[0].life);
}

}   // namespace

TEST_CASE_FIXTURE(PathOfferedPe, "Path: a self-target cast exiles our creature and fetches a TAPPED basic -- searched, in lockstep")
{
    GameState s = BoardPe(1, { kPath });
    PutPe(s, "Kor Duelist", 50);
    const int sc0 = s.search_count;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> self = SolePathPlans(plans, 50);
    // The basic is the land_search_pins axis: Plains and Mountain are the two picks -> two variants.
    REQUIRE(self.size() == 2);
    std::set<std::string> fetched;
    for (const TurnSolver::Plan* p : self)
    {
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *p, true);
        GameState exec = RunExecutor(s, *p);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
            CHECK(FindPe(*g, 50) == nullptr);                 // the Duelist is gone...
            CHECK(InZone(g->exile, 50));                       // ...into exile
            CHECK(NamedIn(g->players[0].graveyard, kPath));    // Path resolved
            CHECK(g->search_count == sc0 + 1);                 // ONE shuffle
            CHECK(g->players[0].lands_played_this_turn == 0);  // never the land drop
            CHECK(g->battlefield.size() == 2);                 // the Plains + the fetched basic
            CHECK(CountBf(*g, "Plains", true) + CountBf(*g, "Mountain", true) == 2);   // both tapped
            CHECK(g->players[0].library.size() == 9);
        }
        CheckSameWorld(roll, exec);
        fetched.insert(CountBf(roll, "Mountain", true) == 1 ? "Mountain" : "Plains");
    }
    CHECK(fetched == std::set<std::string>{ "Mountain", "Plains" });
    // One basic left in the library -> width 1: the self-target cast has no pin fan.
    GameState one = BoardPe(1, { kPath }, true, /*mountains=*/0);
    PutPe(one, "Kor Duelist", 50);
    const std::vector<TurnSolver::Plan> one_plans = TurnSolver::EnumerateMainPlans(one, true);
    CHECK(SolePathPlans(one_plans, 50).size() == 1);
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: an opponent target exiles it and nothing else; the ColossusHammer provider refuses that cast")
{
    // ColossusHammer: only the self-target variant; no opponent-target Path at all.
    {
        GameState s = BoardPe(1, { kPath });
        PutPe(s, "Kor Duelist", 50);
        SpawnPe(s);
        const std::set<int> t = PathTargets(TurnSolver::EnumerateMainPlans(s, true));
        CHECK(t.count(50) == 1);
        CHECK(t.count(0) == 0);
        // With only the opponent's creature on board there is nothing to cast.
        GameState o = BoardPe(1, { kPath });
        SpawnPe(o);
        CHECK(PathTargets(TurnSolver::EnumerateMainPlans(o, true)).empty());
    }
    // The base provider offers both; the opponent-target cast never fans a land pick.
    GameState s = BoardPe(1, { kPath }, /*hammer_provider=*/false);
    PutPe(s, "Kor Duelist", 50);
    const int spawn = SpawnPe(s);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    { const std::set<int> pt_ = PathTargets(plans); const std::set<int> want_ = std::set<int>{ 0, 50 }; CHECK_MESSAGE(pt_ == want_, SetStr(pt_)); }
    const std::vector<const TurnSolver::Plan*> opp = SolePathPlans(plans, 0);
    REQUIRE(opp.size() == 1);
    CHECK(opp[0]->land_search_pins.empty());
    const int sc0 = s.search_count;
    const int ls0 = g_in_plan_ls_count;
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *opp[0], true);
    CHECK(g_in_plan_ls_count == ls0);   // no land-search ordinal consumed
    GameState exec = RunExecutor(s, *opp[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        CHECK(FindPe(*g, spawn) == nullptr);
        CHECK(FindPe(*g, 50) != nullptr);
        CHECK(g->search_count == sc0);              // no search, no shuffle
        CHECK(LibNumbers(*g) == LibNumbers(s));     // library untouched
        CHECK(CountBf(*g, "Mountain", true) == 0);
        CHECK(CountBf(*g, "Plains", true) == 1);    // the {W}
    }
    CheckSameWorld(roll, exec);
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: exiling an equipped host DETACHES the Equipment, in both worlds (the exile-branch fix)")
{
    GameState s = BoardPe(1, { kPath });
    PutPe(s, "Kor Duelist", 50);
    PutPe(s, "Colossus Hammer", 60);
    s.battlefield.back().equipped_to = 50;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> self = SolePathPlans(plans, 50);
    REQUIRE_FALSE(self.empty());
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *self[0], true);
    GameState exec = RunExecutor(s, *self[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        REQUIRE(FindPe(*g, 60) != nullptr);
        CHECK(FindPe(*g, 60)->equipped_to == 0);   // the Hammer stays, unattached (CR 301.5c)
        CHECK(FindPe(*g, 50) == nullptr);
    }
    CheckSameWorld(roll, exec);

    // The same branch serves Swords to Plowshares: its (human-only) self-target used to leave the
    // Hammer "attached" to an exiled creature.
    GameState w = BoardPe(0, {});
    const int host = PutPe(w, "Kor Duelist", 50);
    PutPe(w, "Colossus Hammer", 60);
    w.battlefield.back().equipped_to = 50;
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = DefPe("Swords to Plowshares").card;
    e.source.m_number  = 30;
    e.controller_index = 0;
    Target t;
    t.type            = Target::Type::Permanent;
    t.permanent_index = host;
    e.targets.push_back(t);
    EffectHandler::Resolve(w, e, DefPe("Swords to Plowshares"));
    REQUIRE(FindPe(w, 60) != nullptr);
    CHECK(FindPe(w, 60)->equipped_to == 0);
    CHECK(FindPe(w, 50) == nullptr);
    CHECK(w.players[0].life == 21);   // our Duelist's power, to us
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: exiling the host of an attached Lizard Blades makes the Lizard a creature again, in both worlds")
{
    // Reconfigure (CR 702.151b): attached, Lizard Blades is not a creature; once its host leaves it is
    // unattached and a creature again -- not summoning sick, because it has been under our control
    // continuously (CR 302.6). ResolveCreatureRemovalAt's detach is followed by the shared refresh.
    GameState s = BoardPe(1, { kPath });
    PutPe(s, "Kor Duelist", 50);
    PutPe(s, "Lizard Blades", 70);
    ApplyEquip(s, 0, 70, 50);
    REQUIRE(FindPe(s, 70)->equipped_to == 50);
    REQUIRE_FALSE(FindPe(s, 70)->card.IsCreature());
    // The plan list must OUTLIVE the pointers into it (SolePathPlans' rvalue overload is deleted).
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> self = SolePathPlans(plans, 50);
    REQUIRE_FALSE(self.empty());
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *self[0], true);
    GameState exec = RunExecutor(s, *self[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        INFO("Path in gy " << NamedIn(g->players[0].graveyard, kPath) << ", in hand "
             << NamedIn(g->players[0].hand, kPath) << ", Plains tapped " << CountBf(*g, "Plains", true));
        CHECK(FindPe(*g, 50) == nullptr);
        const Permanent* lz = FindPe(*g, 70);
        REQUIRE(lz != nullptr);
        CHECK(lz->equipped_to == 0);
        CHECK(lz->card.IsCreature());
        CHECK_FALSE(lz->entered_this_turn);
    }
    CheckSameWorld(roll, exec);
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: one cast variant per own-creature identity class; the second Path names the second member")
{
    // Two bare Duelists fold, Swiftblade is its own class.
    {
        GameState s = BoardPe(1, { kPath });
        PutPe(s, "Kor Duelist", 50);
        PutPe(s, "Kor Duelist", 51);
        PutPe(s, "Swiftblade Vindicator", 52);
        SpawnPe(s);
        SpawnPe(s);
        { const std::set<int> pt_ = PathTargets(TurnSolver::EnumerateMainPlans(s, true)); const std::set<int> want_ = std::set<int>{ 50, 52 }; CHECK_MESSAGE(pt_ == want_, SetStr(pt_)); }
        s.m_provider = nullptr;   // the base provider adds ONE opponent variant for both spawns
        { const std::set<int> pt_ = PathTargets(TurnSolver::EnumerateMainPlans(s, true)); const std::set<int> want_ = std::set<int>{ 0, 50, 52 }; CHECK_MESSAGE(pt_ == want_, SetStr(pt_)); }
    }
    // An equipped Duelist is not the bare one's twin (the Hammer falls off one and not the other).
    {
        GameState s = BoardPe(1, { kPath });
        PutPe(s, "Kor Duelist", 50);
        PutPe(s, "Kor Duelist", 51);
        PutPe(s, "Colossus Hammer", 60);
        s.battlefield.back().equipped_to = 51;
        { const std::set<int> pt_ = PathTargets(TurnSolver::EnumerateMainPlans(s, true)); const std::set<int> want_ = std::set<int>{ 50, 51 }; CHECK_MESSAGE(pt_ == want_, SetStr(pt_)); }
        CHECK(RemovalSelfTargetFoldKey(s, *FindPe(s, 50)) != RemovalSelfTargetFoldKey(s, *FindPe(s, 51)));
    }
    // ...nor is a summoning-sick one, nor one with a counter.
    {
        GameState s = BoardPe(0, {});
        PutPe(s, "Kor Duelist", 50);
        PutPe(s, "Kor Duelist", 51, 0, false, /*sick=*/true);
        PutPe(s, "Kor Duelist", 52);
        PutPe(s, "Kor Duelist", 53);
        s.battlefield.back().counters.Add(Counter::Type::PlusOnePlusOne, 1);
        CHECK(RemovalSelfTargetFoldKey(s, *FindPe(s, 50)) != RemovalSelfTargetFoldKey(s, *FindPe(s, 51)));
        CHECK(RemovalSelfTargetFoldKey(s, *FindPe(s, 50)) == RemovalSelfTargetFoldKey(s, *FindPe(s, 52)));
        CHECK(RemovalSelfTargetFoldKey(s, *FindPe(s, 50)) != RemovalSelfTargetFoldKey(s, *FindPe(s, 53)));
    }
    // Two Paths, two identical Duelists: the plan that exiles BOTH exists, and resolves to two basics.
    {
        GameState s = BoardPe(2, { kPath, kPath });
        PutPe(s, "Kor Duelist", 50);
        PutPe(s, "Kor Duelist", 51);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* both = nullptr;
        for (const TurnSolver::Plan& p : plans)
        {
            std::set<int> t;
            int casts = 0;
            for (const Action& a : p.actions) { if (IsPathCast(a)) { ++casts; t.insert(a.enchant_target); } }
            if (casts == 2 && t == std::set<int>{ 50, 51 } && p.land_search_pins.empty()) { both = &p; break; }
        }
        REQUIRE_MESSAGE(both != nullptr, "the two-Path line was not enumerated");
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *both, true);
        CHECK(FindPe(roll, 50) == nullptr);
        CHECK(FindPe(roll, 51) == nullptr);
        CHECK(roll.search_count == s.search_count + 2);
        GameState exec = RunExecutor(s, *both);
        CheckSameWorld(roll, exec);
    }
    // No creature on either side: no legal target, no Path cast.
    {
        GameState s = BoardPe(1, { kPath }, /*hammer_provider=*/false);
        CHECK(PathTargets(TurnSolver::EnumerateMainPlans(s, true)).empty());
    }
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: a stale self-target (the creature left before the cast) is not cast in either world")
{
    GameState s = BoardPe(1, { kPath });
    PutPe(s, "Kor Duelist", 50);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> self = SolePathPlans(plans, 50);
    REQUIRE_FALSE(self.empty());
    GameState gone = s;
    gone.battlefield.erase(gone.battlefield.begin() + 1);   // the Duelist
    REQUIRE(FindPe(gone, 50) == nullptr);
    GameState roll = gone;
    TurnSolver::ApplyPlan(roll, *self[0], true);
    GameState exec = RunExecutor(gone, *self[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        CHECK(InZone(g->players[0].hand, 20));          // still in hand
        CHECK(CountBf(*g, "Plains", false) == 1);       // nothing paid
        CHECK(g->search_count == s.search_count);
    }
    CheckSameWorld(roll, exec);
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: human play -- the board click defaults to our lowest-power creature; -1 declines the search")
{
    // The plan carries no own target (the human's one cast); an opponent creature is on board too.
    GameState s = BoardPe(1, { kPath }, /*hammer_provider=*/false);
    PutPe(s, "Kor Duelist", 50);
    const int spawn = SpawnPe(s);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> opp = SolePathPlans(plans, 0);
    REQUIRE(opp.size() == 1);

    int pick_spawn = 0;            // 0 = take the default, 1 = answer the spawn
    int tutor_answer = -1;         // -1 = decline, -2 = take the default
    int target_default = -1, tutor_calls = 0;
    TargetChooser tc = [&](const GameState& g, const CardDefinition&, int, int, int,
                           const std::vector<ChosenTarget>& heur) -> std::vector<ChosenTarget>
    {
        REQUIRE(heur.size() == 1);
        target_default = g.battlefield[static_cast<std::size_t>(heur[0].index)].card.m_number;
        if (pick_spawn)
        {
            for (int i = 0; i < static_cast<int>(g.battlefield.size()); ++i)
            { if (g.battlefield[static_cast<std::size_t>(i)].card.m_number == spawn) { return { { 1, i, 0 } }; } }
        }
        return heur;
    };
    TutorChooser tu = [&](const GameState&, int, const std::string&, const std::vector<std::string>&, int def,
                          const std::vector<int>&) -> int
    {
        ++tutor_calls;
        return tutor_answer == -2 ? def : tutor_answer;
    };
    TargetChooser* saved_t = g_play_target_chooser;
    TutorChooser*  saved_u = g_play_tutor_chooser;
    g_play_target_chooser = &tc;
    g_play_tutor_chooser  = &tu;

    // 1. Default = our Duelist (not the spawn the plan's variant named); -1 declines: no land, no shuffle.
    GameState a = s;
    TurnSolver::ApplyPlan(a, *opp[0], true);
    CHECK(target_default == 50);
    CHECK(FindPe(a, 50) == nullptr);
    CHECK(FindPe(a, spawn) != nullptr);
    CHECK(tutor_calls == 1);
    CHECK(a.search_count == s.search_count);
    CHECK(LibNumbers(a) == LibNumbers(s));

    // 2. Taking the prompt's default fetches a tapped basic and shuffles once.
    tutor_answer = -2; tutor_calls = 0;
    GameState b = s;
    TurnSolver::ApplyPlan(b, *opp[0], true);
    CHECK(tutor_calls == 1);
    CHECK(b.search_count == s.search_count + 1);
    CHECK(CountBf(b, "Plains", true) + CountBf(b, "Mountain", true) == 2);

    // 3. Answering the spawn exiles it and asks no land prompt (the opponent's search is a no-op).
    pick_spawn = 1; tutor_calls = 0;
    GameState c = s;
    TurnSolver::ApplyPlan(c, *opp[0], true);
    CHECK(FindPe(c, spawn) == nullptr);
    CHECK(FindPe(c, 50) != nullptr);
    CHECK(tutor_calls == 0);
    CHECK(c.search_count == s.search_count);

    g_play_target_chooser = saved_t;
    g_play_tutor_chooser  = saved_u;
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: human play -- the default ranks by REAL power, so a Colossus Hammer carrier is never the 'cheapest body'")
{
    // Claude-play sweep, Colossus Hammer seeds 2100005 / 2100017: the target decision listed
    // "Lizard Blades (13/13)" with heuristic_default 0 -- the default ranked by EffectivePower(), which
    // omits Equipment, so the 11/11 Hammer carrier tied the bare 1/1 and won on battlefield order.
    GameState s = BoardPe(1, { kPath }, /*hammer_provider=*/false);
    const int carrier_idx = PutPe(s, "Kor Duelist", 50);           // FIRST in battlefield order
    PutPe(s, "Colossus Hammer", 51);
    ApplyEquip(s, 0, 51, 50);
    const int bare_idx = PutPe(s, "Swiftblade Vindicator", 52);     // a printed 1/1, nothing attached
    const int spawn = SpawnPe(s);
    REQUIRE(s.battlefield[static_cast<std::size_t>(carrier_idx)].EffectivePower()
            == s.battlefield[static_cast<std::size_t>(bare_idx)].EffectivePower());   // the old tie
    CHECK(EquipGatePowerOf(s.battlefield[static_cast<std::size_t>(carrier_idx)], s) == 11);
    CHECK(RemovalHumanDefaultTarget(s, 0) == bare_idx);

    // ...and it is the board click's PRESELECTED default (the rollout / human apply's chooser path).
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> opp = SolePathPlans(plans, 0);
    REQUIRE(opp.size() == 1);
    int target_default = -1;
    TargetChooser tc = [&](const GameState& g, const CardDefinition&, int, int, int,
                           const std::vector<ChosenTarget>& heur) -> std::vector<ChosenTarget>
    {
        REQUIRE(heur.size() == 1);
        target_default = g.battlefield[static_cast<std::size_t>(heur[0].index)].card.m_number;
        return heur;
    };
    TutorChooser tu = [&](const GameState&, int, const std::string&, const std::vector<std::string>&, int,
                          const std::vector<int>&) -> int { return -1; };
    TargetChooser* saved_t = g_play_target_chooser;
    TutorChooser*  saved_u = g_play_tutor_chooser;
    g_play_target_chooser = &tc;
    g_play_tutor_chooser  = &tu;
    GameState a = s;
    TurnSolver::ApplyPlan(a, *opp[0], true);
    g_play_target_chooser = saved_t;
    g_play_tutor_chooser  = saved_u;
    CHECK(target_default == 52);
    CHECK(FindPe(a, 52) == nullptr);                                // the bare body went
    CHECK(FindPe(a, 50) != nullptr);                                // the lethal attacker stayed
    CHECK(FindPe(a, spawn) != nullptr);
}

TEST_CASE_FIXTURE(PathOfferedPe, "Path: the attack projection drops the creature a self-target Path exiles (and its Equipment)")
{
    // Kor Duelist + Colossus Hammer = 11 power, double strike while equipped -> 22; Swiftblade 1/1
    // double strike -> 2. Exiling the Duelist leaves 2 of attack, exiling Swiftblade leaves 22.
    auto board = [](int opp_life)
    {
        GameState s = BoardPe(1, { kPath });
        s.players[1].life = opp_life;
        PutPe(s, "Kor Duelist", 50);
        PutPe(s, "Colossus Hammer", 60);
        s.battlefield.back().equipped_to = 50;
        PutPe(s, "Swiftblade Vindicator", 52);
        return s;
    };
    auto max_win_life = [&](int target)
    {
        int best = 0;
        for (int life = 1; life <= 30; ++life)
        {
            for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(board(life), true))
            {
                if (p.actions.size() == 1 && IsPathCast(p.actions[0]) && p.actions[0].enchant_target == target
                    && p.wins_this_turn)
                { best = life; }
            }
        }
        return best;
    };
    CHECK(max_win_life(50) == 2);
    CHECK(max_win_life(52) == 22);
}

TEST_CASE("Path: Colossus Hammer never casts it by default (USER 2026-10-18: excluded for goldfishing)")
{
    // The default arm: no Path plan at all, own creature or opponent's on the board.
    GameState s = BoardPe(2, { kPath });
    PutPe(s, "Kor Duelist", 50);
    SpawnPe(s);
    CHECK(PathTargets(TurnSolver::EnumerateMainPlans(s, true)).empty());
    CHECK(HammerPe().NeverCast(DefPe(kPath)));
    CHECK_FALSE(HammerPe().NeverCast(DefPe("Colossus Hammer")));
    // =0 brings the self-target back (and only it). A different board (one more Plains), so no
    // enumeration memo entry made under the default can answer for it.
    PathOfferedPe off;
    CHECK_FALSE(HammerPe().NeverCast(DefPe(kPath)));
    GameState t = BoardPe(3, { kPath });
    PutPe(t, "Kor Duelist", 50);
    SpawnPe(t);
    { const std::set<int> pt_ = PathTargets(TurnSolver::EnumerateMainPlans(t, true)); CHECK_MESSAGE(pt_ == std::set<int>{ 50 }, SetStr(pt_)); }
}
