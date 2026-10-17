// Unit tests for the Galadriel deck's two new mechanics (2026-10-17):
//
//  1. GALADRIEL OF LOTHLORIEN's scry watcher (CardParams::scry_reveal_land_to_battlefield; the SCRY
//     WATCHERS block in core/SpellEffects.h). A land entering with its own ETB scry starts a reveal
//     CHAIN: every revealed land enters tapped and scries again. The chain is one searched policy --
//     a skip count k over the STOP cards (nonlands, scry-less lands) -- indexed by the land's scry pin.
//     Guarded: the chain runs to the first kept stop card; a pinned k bottoms that many stop cards; a
//     scry-less land taken ends the chain; nothing happens without a watcher or with the per-game
//     gate clear.
//  2. THE GREY HAVENS' graveyard-legend colours (CardParams::mana_colors_of_graveyard_legends):
//     EffectiveProduces is {C} until a legendary creature card is in our graveyard, then adds its
//     colours.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoadedGal()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefGal(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

constexpr const char* kGaladriel = "Galadriel of Lothl\xC3\xB3rien";
constexpr const char* kTemple    = "Temple of Mystery";
constexpr const char* kSkerry    = "Saprazzan Skerry";
constexpr const char* kCrackle   = "Crackle with Power";
constexpr const char* kHavens    = "The Grey Havens";

GameState FreshGal()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

void PutGal(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefGal(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void LibGal(GameState& s, const std::vector<std::string>& names, int first_number)
{
    int n = first_number;
    for (const std::string& name : names)
    {
        Card c = DefGal(name).card;
        c.m_number = n++;
        s.players[0].library.push_back(c);
    }
}

int CountNamedGal(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Galadriel deck cards: printed data and params")
{
    EnsureCardsLoadedGal();
    const CardDefinition& g = DefGal(kGaladriel);
    CHECK(g.card.m_mana_cost.generic == 1);
    CHECK(g.card.m_mana_cost.green == 1);
    CHECK(g.card.m_mana_cost.blue == 1);
    CHECK(g.card.m_power == 3);
    CHECK(g.card.m_toughness == 3);
    CHECK(g.card.HasSupertype(Supertype::Legendary));
    CHECK(g.params.scry_reveal_land_to_battlefield);

    const CardDefinition& h = DefGal(kHavens);
    CHECK(h.card.HasSupertype(Supertype::Legendary));
    CHECK(h.params.mana_colors_of_graveyard_legends);
    CHECK(h.params.etb_scry == 1);
    CHECK_FALSE(h.params.enters_tapped);

    const CardDefinition& t = DefGal(kTemple);
    CHECK(t.params.enters_tapped);
    CHECK(t.params.etb_scry == 1);
}

TEST_CASE("Galadriel: a land's ETB scry chains through scry-lands and stops at the first nonland")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, kGaladriel, 1);
    LibGal(s, { kTemple, kTemple, kCrackle, kTemple }, 10);

    // A Temple enters (as from a land drop): its scry sees a Temple -> kept and revealed -> enters ->
    // scries the next Temple -> enters -> scries Crackle (a stop card, the default keeps it) -> the
    // reveal shows a nonland and the chain ends with Crackle on top.
    EnterLand(s, DefGal(kTemple), 2, /*resolve_full_etb=*/true, /*fire_landfall=*/true,
              LandEntryTap::ForcedTapped);
    CHECK(CountNamedGal(s, kTemple) == 3);
    REQUIRE(s.players[0].library.size() == 2);
    CHECK(s.players[0].library[0].m_name.str() == kCrackle);
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == kTemple) { CHECK(p.tapped); } }
    CHECK(s.players[0].lands_played_this_turn == 0);   // a put, not a land drop
}

TEST_CASE("Galadriel: a pinned skip count bottoms that many stop cards")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, kGaladriel, 1);
    LibGal(s, { kCrackle, kTemple, kTemple }, 10);

    // Candidates are {default 0, then 1, 2}: index 1 = skip one stop card.
    const std::vector<int> cands = ScryChainCandidates(s);
    REQUIRE(cands.size() == 2);   // one stop card: k = 0 or 1
    CHECK(cands[0] == 0);
    {
        ScriptedTopChoice pin(1);
        EnterLand(s, DefGal(kTemple), 2, true, true, LandEntryTap::ForcedTapped);
    }
    // Crackle bottomed, both Temples revealed in, and the chain's last look sees Crackle again with no
    // skips left: kept, revealed, stays.
    CHECK(CountNamedGal(s, kTemple) == 3);
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library[0].m_name.str() == kCrackle);
}

TEST_CASE("Galadriel: a scry-less land taken ends the chain; skipping it continues")
{
    EnsureCardsLoadedGal();
    {
        GameState s = FreshGal();
        PutGal(s, kGaladriel, 1);
        LibGal(s, { kSkerry, kTemple }, 10);
        // k = 0: keep the Skerry, reveal it -> it enters with its depletion counters; no scry, so the
        // chain ends and the Temple is still in the library.
        const std::vector<int> cands = ScryChainCandidates(s);
        const int idx0 = static_cast<int>(std::find(cands.begin(), cands.end(), 0) - cands.begin());
        {
            ScriptedTopChoice pin(idx0);
            EnterLand(s, DefGal(kTemple), 2, true, true, LandEntryTap::ForcedTapped);
        }
        CHECK(CountNamedGal(s, kSkerry) == 1);
        CHECK(CountNamedGal(s, kTemple) == 1);
        CHECK(s.players[0].library.size() == 1);
    }
    {
        GameState s = FreshGal();
        PutGal(s, kGaladriel, 1);
        LibGal(s, { kSkerry, kTemple }, 10);
        const std::vector<int> cands = ScryChainCandidates(s);
        const int idx1 = static_cast<int>(std::find(cands.begin(), cands.end(), 1) - cands.begin());
        {
            ScriptedTopChoice pin(idx1);
            EnterLand(s, DefGal(kTemple), 2, true, true, LandEntryTap::ForcedTapped);
        }
        // k = 1: the Skerry is bottomed, the Temple revealed in; its scry sees the Skerry again with
        // no skips left -> kept, revealed, enters.
        CHECK(CountNamedGal(s, kTemple) == 2);
        CHECK(CountNamedGal(s, kSkerry) == 1);
        CHECK(s.players[0].library.size() == 0);
    }
}

TEST_CASE("Galadriel: no watcher, or the per-game gate clear -> a scry reveals nothing")
{
    EnsureCardsLoadedGal();
    {
        GameState s = FreshGal();
        LibGal(s, { kTemple, kTemple }, 10);
        EnterLand(s, DefGal(kTemple), 2, true, true, LandEntryTap::ForcedTapped);
        CHECK(CountNamedGal(s, kTemple) == 1);
        CHECK(s.players[0].library.size() == 2);
    }
    {
        GameState s = FreshGal();
        s.deck_has_scry_watcher = false;
        PutGal(s, kGaladriel, 1);
        LibGal(s, { kTemple, kTemple }, 10);
        CHECK(ScryWatcherCopies(s, 0) == 0);
        CHECK_FALSE(s.deck_has_scry_watcher);
        EnterLand(s, DefGal(kTemple), 2, true, true, LandEntryTap::ForcedTapped);
        CHECK(CountNamedGal(s, kTemple) == 1);
        CHECK(s.players[0].library.size() == 2);
    }
}

TEST_CASE("The Grey Havens: {C} until a legendary creature card is in the graveyard")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    const CardDefinition& h = DefGal(kHavens);
    {
        const std::vector<Color>& c = EffectiveProduces(s, 0, h);
        REQUIRE(c.size() == 1);
        CHECK(c[0] == Color::Colorless);
    }
    Card gc = DefGal(kGaladriel).card;
    gc.m_number = 30;
    s.players[0].graveyard.push_back(gc);
    {
        const std::vector<Color> c = EffectiveProduces(s, 0, h);
        CHECK(std::find(c.begin(), c.end(), Color::Green)     != c.end());
        CHECK(std::find(c.begin(), c.end(), Color::Blue)      != c.end());
        CHECK(std::find(c.begin(), c.end(), Color::Colorless) != c.end());
        CHECK(std::find(c.begin(), c.end(), Color::Red)       == c.end());
    }
    // A nonlegendary / noncreature card in the graveyard adds nothing.
    GameState s2 = FreshGal();
    Card cc = DefGal(kCrackle).card;
    cc.m_number = 31;
    s2.players[0].graveyard.push_back(cc);
    CHECK(EffectiveProduces(s2, 0, h).size() == 1);
}

// ---- THE PAYMENT BLOW-UP (MTG_ACF_EXACT / MTG_ACF_INTERIOR) ------------------------------------
// The board the d5 search kept testing Crackle with Power X=5 against: 15 lands + Sol Ring = exactly
// 17 mana, one native red source, three any-colour filters. {15}{R}{R} is UNPAYABLE -- the second {R}
// needs a filter whose {1} feed leaves 16 -- and before the exact filter check the backtracker took
// 8-14 M nodes to prove it. Correctness is pinned here (false, and nothing left tapped); one more red
// land makes the same cost payable (true), so the check is not simply refusing filter boards.
namespace
{
GameState CrackleBoardGal(bool extra_red)
{
    GameState s = FreshGal();
    int n = 100;
    auto put = [&](const char* name, int count) { for (int i = 0; i < count; ++i) { PutGal(s, name, n++); } };
    put("Temple of Enlightenment", 3);
    put(kTemple, 2);                       // Temple of Mystery
    put("Temple of Malady", 2);
    put("Temple of Deceit", 2);
    put("Rumble Arena", 2);
    put("Zhalfirin Void", 1);
    put(kHavens, 1);
    put("Temple of Triumph", extra_red ? 2 : 1);
    put("Crystal Grotto", 1);
    put("Sol Ring", 1);
    return s;
}
ManaCost CrackleX5Gal() { ManaCost c; c.generic = 15; c.red = 2; return c; }
}   // namespace

TEST_CASE("Mana payment: a filter's feed is priced -- {15}{R}{R} off 17 mana with one red land is unpayable")
{
    EnsureCardsLoadedGal();
    GameState s = CrackleBoardGal(/*extra_red=*/false);
    CHECK_FALSE(TapForCostBacktrack(s, CrackleX5Gal(), /*for_creature=*/false, ManaPool{}));
    for (const Permanent& p : s.battlefield) { CHECK_FALSE(p.tapped); }   // a failure leaves no taps
}

TEST_CASE("Mana payment: the same board with a second red land pays {14}{R}{R} through a filter")
{
    EnsureCardsLoadedGal();
    // 16 lands + Sol Ring = 18; two native reds pay {R}{R}, the rest pays 14 generic with 2 to spare.
    GameState s = CrackleBoardGal(/*extra_red=*/true);
    ManaCost c; c.generic = 14; c.red = 2;
    CHECK(TapForCostBacktrack(s, c, false, ManaPool{}));
    // And one red land + a fed filter: {13}{R}{R} = 15 of the 17 -- payable only through a conversion.
    GameState s1 = CrackleBoardGal(false);
    ManaCost c1; c1.generic = 13; c1.red = 2;
    CHECK(TapForCostBacktrack(s1, c1, false, ManaPool{}));
}
