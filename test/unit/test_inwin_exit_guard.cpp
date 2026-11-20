// Unit test for MTG_INWIN_EXIT_GUARD (TurnSolver.cpp, inwin::ExitGuardOn): the depth-free in-window proof cache must
// store only MINIMA. A node whose subtree took the first-verified-win horizon exit returned the first in-window win it
// met, which is the earliest only under the ID premise of the pass it ran in; served depth- and leaf-free to other
// passes and decisions it became a false "no win by T5" proof. The defect, found by IDG-2 and root-caused by DM-1
// (logs/maze/DM1-handoff.md): Martyr Lifegain s2151 gi149 wins T5 at d4 and d5 b0 but T6 at d6, d7 and d8 b0 -- a
// DEEPER unlimited search finding a worse line, which an earliest-win objective must never do. Under
// MTG_INWIN_CACHE_VERIFY=1 the d6 game shows 4 MATERIAL disagreements (cached "no win by 5", fresh 5).
// This plays the real regression game (martyr_regression_d5_s2002, gi 149) at d5 and d6, budget 0, and pins both T5.
#include <doctest/doctest.h>

#include "ai/MulliganProfileIO.h"
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

namespace
{
void EnsureCardsIwg()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

double PlayMartyrGi149(int depth)
{
    const Decklist deck = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Martyr Lifegain/Martyr Lifegain.cod"));
    const MulliganProfile prof = LoadDeckProfile(
        ResolveHeuristicDefaultsPath("decks/Martyr Lifegain/Martyr Lifegain.profile.json"));
    GoldFishRunner runner;
    // --seed 2151 --game-index 149 --games 1 --threads 1 --depth <d> --budget-ms 0 (the batch repro: seed = base + gi)
    const RunResult r = runner.Run(deck, 1, 2151, 8, prof, {}, 149, depth, /*timeout_ms=*/0, /*num_threads=*/1);
    REQUIRE(r.games_played == 1);
    return r.avg_turns;
}
}   // namespace

TEST_CASE("In-window cache exit guard: a deeper unlimited search keeps the T5 line (Martyr s2151 gi149)")
{
    EnsureCardsIwg();
    CHECK(PlayMartyrGi149(5) == doctest::Approx(5.0));
    // Before the guard: 6.0 -- the d6 lazy probe read a no-win proof a rollout pass's horizon exit had stored.
    CHECK(PlayMartyrGi149(6) == doctest::Approx(5.0));
}
