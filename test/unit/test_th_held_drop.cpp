// treasure_hunt's depth-0 "Treasure Hunt before the land drop" rule (DecisionProvider::LandDropAfterDrawEngine,
// moved out of AIEngine::TakeTurn into TreasureHuntProvider 2026-10-14) and the drop it used to LOSE
// (MTG_DRAW_HELD_DROP_REPLAY; docs/design/treasure-hunt-land-rule-provider.md).
//
// The board is the cast-order probe A2 as the T4 pre-combat main sees it: four lands (Island, Steam Vents
// x2, Thundering Falls), hand Land's Edge + Treasure Hunt + four Islands (one just drawn), the opponent on
// 18. Treasure Hunt is castable without the drop and no outlet is out, so the rule holds the drop. The d0
// greedy then casts Land's Edge ALONE (3 of the 4 mana), and the held drop was only ever replayed by the
// draw engine's second TakeTurn pass -- which only a cast draw engine requests. So no land was played on
// T4 and the game won T5; d3, d5 and d8 win T4 (land, Land's Edge, Treasure Hunt).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsThd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefThd(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutThd(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = DefThd(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = false;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

Card CardThd(const std::string& name, int number)
{
    Card c     = DefThd(name).card;
    c.m_number = number;
    return c;
}

int InHandThd(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

bool OnBattlefieldThd(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

const TreasureHuntProvider& ThProv()
{
    static const TreasureHuntProvider p;
    return p;
}

// The lever, forced for this thread and restored on scope exit.
struct ReplayArm
{
    explicit ReplayArm(bool on)
    { heurarm::t_arm[heurarm::DRAW_HELD_DROP_REPLAY] = on ? 1 : 0; }
    ~ReplayArm() { heurarm::t_arm[heurarm::DRAW_HELD_DROP_REPLAY] = -1; }
};

const char* kTH = "Treasure Hunt";
const char* kLE = "Land's Edge";

// Probe A2, pre-combat main of T4 with the drop open. `library_top` goes on top (index 0 = next draw),
// then Gideon filler (a nonland that stops Treasure Hunt). `extra_battlefield` adds permanents.
GameState BoardA2(int opp_life, const std::vector<std::string>& library_top,
                  const std::vector<std::string>& extra_battlefield = {})
{
    EnsureCardsThd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 0;
    s.m_provider = &ThProv();
    int num = 1;
    for (const char* n : { "Island", "Steam Vents", "Steam Vents", "Thundering Falls" }) { PutThd(s, n, num++); }
    for (const std::string& n : extra_battlefield) { PutThd(s, n, num++); }
    for (const char* n : { kLE, kTH, "Island", "Island", "Island", "Island" }) { s.players[0].hand.push_back(CardThd(n, num++)); }
    for (const std::string& n : library_top) { s.players[0].library.push_back(CardThd(n, num++)); }
    for (int k = 0; k < 12; ++k) { s.players[0].library.push_back(CardThd("Gideon, Ally of Zendikar", num++)); }
    s.deck_feeds_combat = false;   // GoldFishRunner::StampDeckTraits for this list (no creatures)
    s.uses_second_main  = false;
    s.phase = Phase::PreCombatMain;
    s.step  = Step::MainPhase;
    s.StampMain1Hand();
    return s;
}

// GameEngine::MainPhase's pre-combat shape at depth 0: one TakeTurn, then the second pass if requested.
// Returns pass 1's request.
bool PlayMain1(AIEngine& eng, GameState& s)
{
    auto resolver = [](GameState& st)
    {
        while (!st.stack.empty())
        {
            const StackEntry e = st.stack.back();
            st.stack.pop_back();
            const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
            if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
        }
    };
    const bool second_pass = eng.TakeTurn(s, /*is_pre_combat_main=*/true, resolver);
    if (second_pass) { eng.TakeTurn(s, true, resolver); }
    return second_pass;
}

std::vector<std::string> Names(const GameState& s)
{
    std::vector<std::string> v;
    for (const Permanent& p : s.battlefield) { v.push_back("bf:" + p.card.m_name.str()); }
    for (const Card& c : s.players[0].hand) { v.push_back("hand:" + c.m_name.str()); }
    return v;
}

const std::vector<std::string> kSixIslands(6, "Island");

}   // namespace

TEST_CASE("LandDropAfterDrawEngine: TreasureHuntProvider holds the drop for a castable Treasure Hunt with no outlet out")
{
    {
        const GameState s = BoardA2(18, kSixIslands);
        CHECK(ThProv().LandDropAfterDrawEngine(s, 0));
        static const GenericProvider generic;
        CHECK_FALSE(generic.LandDropAfterDrawEngine(s, 0));   // base: every other deck plays its drop first
    }
    {   // a Land's Edge already out: the draw is safe, the drop goes first
        const GameState s = BoardA2(18, kSixIslands, { kLE });
        CHECK_FALSE(ThProv().LandDropAfterDrawEngine(s, 0));
    }
    {   // a Reliquary Tower already out: likewise
        const GameState s = BoardA2(18, kSixIslands, { "Reliquary Tower" });
        CHECK_FALSE(ThProv().LandDropAfterDrawEngine(s, 0));
    }
    {   // Treasure Hunt not castable without the drop (every source tapped)
        GameState s = BoardA2(18, kSixIslands);
        for (Permanent& p : s.battlefield) { p.tapped = true; }
        CHECK_FALSE(ThProv().LandDropAfterDrawEngine(s, 0));
    }
}

TEST_CASE("d0 executor: the drop held for Treasure Hunt is played that main when Treasure Hunt is not cast (probe A2)")
{
    {   // the fix (default): the held Island is played after Land's Edge, and the second pass it
        // requests casts Treasure Hunt off it -- the d3/d5/d8 line, on T4
        ReplayArm arm(true);
        GameState s = BoardA2(18, kSixIslands);
        AIEngine  eng;   // depth 0: the d0 runner
        CHECK(PlayMain1(eng, s));
        CHECK(OnBattlefieldThd(s, kLE));
        CHECK(s.players[0].lands_played_this_turn == 1);   // the held drop is not lost
        CHECK(InHandThd(s, kTH) == 0);                     // ...and its mana casts Treasure Hunt on T4
    }
    {   // =0: the defect, as it was -- Land's Edge alone, no second pass, no land, Treasure Hunt kept
        ReplayArm arm(false);
        GameState s = BoardA2(18, kSixIslands);
        AIEngine  eng;
        CHECK_FALSE(PlayMain1(eng, s));
        CHECK(OnBattlefieldThd(s, kLE));
        CHECK(s.players[0].lands_played_this_turn == 0);
        CHECK(InHandThd(s, kTH) == 1);
    }
}

TEST_CASE("d0 executor: a held drop that is the marginal Land's Edge ammo for lethal stays held (HoldDeferredDropForLethal)")
{
    // Opponent on 8: after Land's Edge the hand holds exactly four lands = 8 damage at 2 a land, so
    // playing one would leave the turn a land short of lethal. The provider's hold wins over the replay.
    ReplayArm arm(true);
    GameState s = BoardA2(8, { "Gideon, Ally of Zendikar" });
    AIEngine  eng;
    PlayMain1(eng, s);
    REQUIRE(OnBattlefieldThd(s, kLE));   // the shape under test: Land's Edge cast, Treasure Hunt not
    REQUIRE(InHandThd(s, kTH) == 1);
    CHECK(s.players[0].lands_played_this_turn == 0);
    CHECK(InHandThd(s, "Island") == 4);
}

TEST_CASE("d0 executor: a main that DOES cast Treasure Hunt keeps today's drop route (both arms identical)")
{
    // Five lands: Land's Edge AND Treasure Hunt are both cast, so the held drop is the breakpoint
    // continuation's / second pass's to play with the draw known -- the replay never fires.
    std::vector<std::vector<std::string>> out;
    for (const bool on : { false, true })
    {
        CAPTURE(on);
        ReplayArm arm(on);
        GameState s = BoardA2(18, kSixIslands, { "Island" });
        AIEngine  eng;
        PlayMain1(eng, s);
        CHECK(OnBattlefieldThd(s, kLE));
        CHECK(InHandThd(s, kTH) == 0);
        CHECK(s.players[0].lands_played_this_turn == 1);
        out.push_back(Names(s));
    }
    CHECK(out[0] == out[1]);
}
