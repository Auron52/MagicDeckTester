// MAIN 1 IS NOT THE END OF A SPLIT TURN (MTG_DIG_DEFER_MAIN2; TurnSolver::Main1DigDefersToMain2).
//
// FiveColour d8/b0 gi1106 (seed 6211107), turn 2, after the Wooded Foothills -> Blood Crypt drop:
// Stomping Ground, Blood Crypt and a Birds of Paradise cast last turn are untapped, and the hand holds
// Faeburrow Elder ({1}{G}{W}), Zagoth Triome (cycling {3}), Island, Unite the Coalition and Maelstrom
// Archangel. The USER phase rule classifies Faeburrow Main2 (no Greaves), so the pre-combat filter
// withholds it and main 1 casts nothing. The only good play is the post-combat Faeburrow -- it is the
// T4 kill's engine (T3 Archangel off its five colours, T4 Unite + the Archangel's free Unite).
//
// The defect: main 1's apply then ran the provider's heuristic "dig when stuck", which counts LANDS (2)
// against Faeburrow's MV 3, judged the hand dead, and cycled the Triome off all three sources -- so
// the unfiltered post-combat enumeration had no mana to emit Faeburrow with, and the Main2 class
// DELETED the line it exists to move (the game won T5; PHASE=0 and MTG_GENERIC_DIG=0 win T4).
// With the lever the dig waits for main 2: main 2 is offered Faeburrow, and its "pass" still digs.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

Card NumberedCard(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = NumberedCard(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = false;
    s.battlefield.push_back(p);
}

// The phase filter is searched-play only (MainPhaseFilterActive: SearchedPlayActive), as in a d8 game.
struct SearchedPlayScope
{
    bool saved;
    SearchedPlayScope() : saved(g_searched_play) { TurnSolver::SetSearchedPlay(true); }
    ~SearchedPlayScope() { TurnSolver::SetSearchedPlay(saved); }
};

struct ArmScope
{
    explicit ArmScope(heurarm::Slot slot, int on) { heurarm::t_arm[slot] = static_cast<std::int8_t>(on); }
    ~ArmScope() { heurarm::Clear(); }
};

// gi1106's T2 main 1, the drop already made. `salt` keeps one arm's memo entries from serving another.
GameState Turn2(int salt)
{
    static const FiveColourProvider fc;
    GameState s;
    s.m_provider          = &fc;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.uses_second_main    = true;    // SetupGame's DeckUsesSecondMain stamp (FiveColour plays a main 2)
    s.players[0].life     = 14 + salt;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    Put(s, "Stomping Ground", 46);
    Put(s, "Birds of Paradise", 2);
    Put(s, "Blood Crypt", 47);
    int num = 100;
    for (const char* n : { "Unite the Coalition", "Faeburrow Elder", "Maelstrom Archangel",
                           "Maelstrom Archangel", "Island", "Zagoth Triome" })
    { s.players[0].hand.push_back(NumberedCard(n, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(NumberedCard("Forest", 900 + k)); }
    return s;
}

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

int UntappedSources(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (!p.tapped) { ++n; } }
    return n;
}

bool OnBattlefield(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

const TurnSolver::Plan* FindCast(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &p; } }
    }
    return nullptr;
}

// Main 1 as the search plays it: the drop is made, the filter leaves nothing to cast, so the plan is
// empty. land_decided keeps the apply from making a greedy drop of its own.
GameState AfterMain1(int salt)
{
    GameState s = Turn2(salt);
    TurnSolver::Plan m1;
    m1.land_decided = true;
    TurnSolver::ApplyPlan(s, m1, /*is_pre_combat=*/true);
    return s;
}

}   // namespace

TEST_CASE("Main-1 dig waits for main 2: the withheld Faeburrow is offered post-combat")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    SearchedPlayScope searched;
    {
        const GameState pre = Turn2(0);
        REQUIRE(TurnSolver::CollapsedMainActive(pre));
        // The filter withholds Faeburrow from main 1 (USER phase rule: Main2 without Greaves)...
        CHECK(FindCast(TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true), "Faeburrow Elder") == nullptr);
        // ...and it is payable (Stomping {G}, Birds {W}, Crypt {1}), so main 1's dig waits.
        CHECK(TurnSolver::Main1DigDefersToMain2(pre));
    }
    const GameState m2 = AfterMain1(0);
    CHECK(InHand(m2, "Zagoth Triome"));          // not cycled in main 1
    CHECK(UntappedSources(m2) == 3);
    const std::vector<TurnSolver::Plan> post = TurnSolver::EnumerateMainPlans(m2, /*is_pre_combat=*/false);
    const TurnSolver::Plan* fae = FindCast(post, "Faeburrow Elder");
    REQUIRE(fae != nullptr);                      // the moved line exists in main 2
    {
        GameState s = m2;
        TurnSolver::ApplyPlan(s, *fae, /*is_pre_combat=*/false);
        CHECK(OnBattlefield(s, "Faeburrow Elder"));
        CHECK(InHand(s, "Zagoth Triome"));       // tapped out: nothing left to dig with
    }
    {   // MOVED, NOT CANCELLED: a main 2 that passes still runs the heuristic dig.
        GameState s = m2;
        TurnSolver::Plan pass;
        pass.land_decided = true;
        TurnSolver::ApplyPlan(s, pass, /*is_pre_combat=*/false);
        CHECK_FALSE(InHand(s, "Zagoth Triome"));
        CHECK(UntappedSources(s) == 0);
    }
}

TEST_CASE("Main-1 dig waits for main 2: MTG_DIG_DEFER_MAIN2=0 digs in main 1 and deletes the cast")
{
    EnsureCardsLoaded();
    ArmScope arm(heurarm::DIG_DEFER_MAIN2, 0);
    SearchedPlayScope searched;
    CHECK_FALSE(TurnSolver::Main1DigDefersToMain2(Turn2(1)));
    const GameState m2 = AfterMain1(1);
    CHECK_FALSE(InHand(m2, "Zagoth Triome"));    // cycled for {3} at the end of main 1
    CHECK(UntappedSources(m2) == 0);
    CHECK(FindCast(TurnSolver::EnumerateMainPlans(m2, /*is_pre_combat=*/false), "Faeburrow Elder") == nullptr);
}

TEST_CASE("Main-1 dig waits for main 2: inert without the filter or without a payable withheld cast")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    {   // Depth 0 (unsearched play): the filter never withholds, so main 1 is the whole turn.
        CHECK_FALSE(TurnSolver::Main1DigDefersToMain2(Turn2(2)));
    }
    SearchedPlayScope searched;
    {   // A single-main deck: nothing is deferred, nothing waits.
        GameState s = Turn2(3);
        s.uses_second_main = false;
        CHECK_FALSE(TurnSolver::Main1DigDefersToMain2(s));
    }
    {   // Withheld but unpayable (Birds tapped: two sources for a three-mana cast) -> the dig is main 1's.
        GameState s = Turn2(4);
        for (Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Birds of Paradise") { p.tapped = true; } }
        CHECK_FALSE(TurnSolver::Main1DigDefersToMain2(s));
    }
    {   // Faeburrow cast already: nothing withheld remains (Archangel / Unite are not payable).
        GameState s = Turn2(5);
        std::vector<Card>& h = s.players[0].hand;
        for (auto it = h.begin(); it != h.end(); ++it) { if (it->m_name.str() == "Faeburrow Elder") { h.erase(it); break; } }
        CHECK_FALSE(TurnSolver::Main1DigDefersToMain2(s));
    }
}
