// The land-ETB scry fan (Plan::scry_choice, EnumeratePlansWithLand) in the NON-CLAIRVOYANT search.
//
// The clairvoyant engine sizes the fan off the cards now on top: TopDispositionCandidates with the provider's firm
// ScryVerdict prune, so a top card the provider always keeps leaves no alternative. That is clairvoyance by design
// (USER 2026-11-18). MTG_NC_SEARCH's ReshuffleAvgChoosePlan, however, builds its candidate set on the REAL state and
// only then values plans over sampled futures -- so the same sizing let the undrawn real top decide which lines the
// non-clairvoyant search could choose. USER 2026-11-18: "With the clairvoyance case, it would be a different story
// if it slipped through to non-clairvoyant search. In that case it is a real bug to fix." The NC candidate set now
// sizes an unseen look off its COUNT alone (the step-9d Loran's Escape shape); a KNOWN top (placed by the player)
// is still read. The first case failed before the fix.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
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

// A provider with a FIRM scry verdict: always keep a land on top; no verdict on anything else. Its default
// disposition (ScryKeepOnTop) agrees with the verdict, so for a land on top the default IS the only legal
// disposition and the verdict prunes the alternative.
struct LandKeepVerdictProvider final : GenericProvider
{
    bool ScryKeepOnTop(const GameState&, const Card& c) const override
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        return d != nullptr && d->card.IsLand();
    }
    int ScryVerdict(const GameState&, const Card& c) const override
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        return (d != nullptr && d->card.IsLand()) ? 1 : -1;
    }
};
const LandKeepVerdictProvider& Prov() { static const LandKeepVerdictProvider p; return p; }

Card Numbered(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

// T3 main 1, no land played yet: a Temple of Epiphany (etb scry 1) in hand, two Islands out. `top` is the card
// on top of the library (unseen unless `known`), 20 Mountains below it.
GameState Board(const std::string& top, bool known)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.m_provider          = &Prov();
    for (int k = 0; k < 2; ++k)
    {
        Permanent p;
        p.card = Numbered("Island", 1 + k);
        s.battlefield.push_back(p);
    }
    s.players[0].hand.push_back(Numbered("Temple of Epiphany", 10));
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(Numbered("Mountain", 900 + k)); }
    if (known) { s.players[0].library.insert(s.players[0].library.begin(), Numbered(top, 800)); }   // placed: known
    else
    {
        // Dealt, not placed: rebuild the library with `top` first and no knowledge recorded.
        std::vector<Card> cards{ Numbered(top, 800) };
        for (int k = 0; k < 20; ++k) { cards.push_back(Numbered("Mountain", 900 + k)); }
        s.players[0].library.assign(cards.begin(), cards.end());
    }
    return s;
}

// How many plans play the Temple with a NON-default scry disposition pinned.
int ScryVariants(const std::vector<TurnSolver::Plan>& plans)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans)
    { if (p.land_to_play == "Temple of Epiphany" && p.scry_choice >= 1) { ++n; } }
    return n;
}

struct SearchedPlay
{
    bool saved = g_searched_play;
    SearchedPlay()  { TurnSolver::SetSearchedPlay(true); }
    ~SearchedPlay() { TurnSolver::SetSearchedPlay(saved); }
};

}   // namespace

TEST_CASE("NC scry: the non-clairvoyant candidate set does not depend on the UNSEEN top card")
{
    EnsureCardsLoaded();
    SearchedPlay _sp;
    const GameState land_top  = Board("Island", /*known=*/false);          // firm keep verdict
    const GameState spell_top = Board("Lightning Bolt", /*known=*/false);  // no verdict
    REQUIRE_FALSE(land_top.players[0].library.KnownAt(0));

    // The clairvoyant engine reads the top (by design): the firm verdict leaves no alternative.
    CHECK(ScryVariants(TurnSolver::EnumerateMainPlans(land_top, true)) == 0);
    CHECK(ScryVariants(TurnSolver::EnumerateMainPlans(spell_top, true)) == 1);

    // The non-clairvoyant search's candidate set: one alternative either way, sized off the look count. Before
    // the fix the land top gave 0 and the spell top 1 -- the undrawn card picked the menu.
    const int nc_land  = ScryVariants(TurnSolver::NcRootPlansForTest(land_top, true));
    const int nc_spell = ScryVariants(TurnSolver::NcRootPlansForTest(spell_top, true));
    CHECK(nc_land == 1);
    CHECK(nc_spell == 1);
    CHECK(TurnSolver::NcRootPlansForTest(land_top, true).size()
          == TurnSolver::NcRootPlansForTest(spell_top, true).size());
}

TEST_CASE("NC scry: a KNOWN top (placed by the player) is still read by the non-clairvoyant candidate set")
{
    EnsureCardsLoaded();
    SearchedPlay _sp;
    const GameState land_top = Board("Island", /*known=*/true);
    REQUIRE(land_top.players[0].library.KnownAt(0));
    CHECK(ScryVariants(TurnSolver::NcRootPlansForTest(land_top, true)) == 0);   // the verdict prunes, as it should
}
