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

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
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

    // Candidates are {live default (-1), then k = 0, 1}: index 2 = skip one stop card.
    const std::vector<int> cands = ScryChainCandidates(s);
    REQUIRE(cands.size() == 3);   // one stop card: the default, k = 0, k = 1
    CHECK(cands[0] == -1);
    {
        ScriptedTopChoice pin(2);
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

// ---- GaladrielProvider::ScryKeepOnTop before Galadriel (USER 2026-10-17): "Without Galadriel, you
// want to look for colors, Crackle and acceleration. Anything else is thrown unless you are short on
// lands and when you are really in bad shape for colours you might even throw Crackle or acceleration
// if they don't help solve the problem."
namespace
{
void HandGal(GameState& s, const std::string& name, int number)
{
    Card c = DefGal(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}
bool KeepsGal(const GameState& s, const std::string& name)
{
    static const GaladrielProvider gp;
    Card c = DefGal(name).card;
    c.m_number = 900;
    return gp.ScryKeepOnTop(s, c);
}
}   // namespace

TEST_CASE("Galadriel scry before Galadriel: missing G/U -> only colour fixers are kept")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, "Temple of Malice", 1);       // B/R
    PutGal(s, "Zhalfirin Void", 2);         // C
    HandGal(s, "Temple of Triumph", 3);     // R/W: three lands, not short
    HandGal(s, kGaladriel, 4);
    CHECK(KeepsGal(s, kTemple));            // G/U
    CHECK(KeepsGal(s, "Temple of Deceit")); // U
    CHECK(KeepsGal(s, kSkerry));            // U: acceleration that solves the problem
    s.turn_number = 3;
    CHECK_FALSE(KeepsGal(s, "Crystal Grotto")); // early with 2 mana out, a filter is not real G/U (USER)
    PutGal(s, "Temple of Triumph", 9);          // 3 mana out: the filter is drawn as a 4th source (USER)
    CHECK(KeepsGal(s, "Crystal Grotto"));
    s.battlefield.pop_back();
    s.turn_number = 4;
    CHECK(KeepsGal(s, "Crystal Grotto"));       // far enough along, it is sufficient (USER)
    CHECK_FALSE(KeepsGal(s, kCrackle));     // bad shape: Crackle does not help
    CHECK_FALSE(KeepsGal(s, "Sol Ring"));   // ...nor colourless acceleration
    CHECK_FALSE(KeepsGal(s, kHavens));
    CHECK_FALSE(KeepsGal(s, kGaladriel));   // a second one
}

TEST_CASE("Galadriel scry before Galadriel: colours in, a scry land in hand -> Crackle kept, the rest thrown")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, kTemple, 1);                  // G/U
    PutGal(s, "Temple of Malice", 2);       // B/R
    PutGal(s, "Zhalfirin Void", 3);
    HandGal(s, kGaladriel, 4);
    HandGal(s, "Temple of Triumph", 5);     // the land that follows her
    CHECK(KeepsGal(s, kCrackle));
    // Three lands out: acceleration is no longer useful (USER 2026-10-18, the LAND TIMING rule; it is
    // kept below two lands -- see the LAND TIMING test).
    CHECK_FALSE(KeepsGal(s, "Sol Ring"));
    CHECK_FALSE(KeepsGal(s, kSkerry));
    CHECK_FALSE(KeepsGal(s, "Temple of Plenty"));
    CHECK_FALSE(KeepsGal(s, kHavens));
    CHECK_FALSE(KeepsGal(s, kGaladriel));
}

TEST_CASE("Galadriel scry before Galadriel: colours in, no scry land in hand -> keep a scry land only (USER, seed 14)")
{
    EnsureCardsLoadedGal();
    // The seed 14 reference, turn 3: Epiphany + Mystery + Void out, Galadriel + Crackle in hand.
    GameState s = FreshGal();
    s.turn_number = 3;
    PutGal(s, "Temple of Epiphany", 1);
    PutGal(s, kTemple, 2);                  // G/U
    PutGal(s, "Zhalfirin Void", 3);
    HandGal(s, kGaladriel, 4);
    HandGal(s, kCrackle, 5);
    CHECK(KeepsGal(s, "Temple of Plenty"));
    CHECK(KeepsGal(s, "Temple of Triumph"));
    CHECK_FALSE(KeepsGal(s, kCrackle));
    CHECK_FALSE(KeepsGal(s, "Sol Ring"));
    CHECK_FALSE(KeepsGal(s, kSkerry));
    CHECK(KeepsGal(s, kHavens));            // a scry land too (every land but the Skerry is)
    CHECK_FALSE(KeepsGal(s, kGaladriel));
}

TEST_CASE("Galadriel scry before Galadriel: short on lands -> any land, but only once G and U are in")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, "Temple of Malice", 1);       // B/R: no G, no U -- colours first even though short
    HandGal(s, kGaladriel, 2);
    CHECK_FALSE(KeepsGal(s, kHavens));
    CHECK_FALSE(KeepsGal(s, "Temple of Triumph"));
    CHECK(KeepsGal(s, "Temple of Plenty")); // G
    s.turn_number = 2;
    PutGal(s, "Rumble Arena", 3);           // early, a filter does not cover G/U
    CHECK_FALSE(KeepsGal(s, kHavens));
    s.battlefield.pop_back();
    PutGal(s, kTemple, 3);                  // G/U covered (no red needed), two lands: short
    CHECK(KeepsGal(s, kHavens));
    CHECK(KeepsGal(s, "Temple of Triumph"));
}

TEST_CASE("Galadriel scry with Galadriel out: scry-lands kept, a first Crackle only when next turn is lethal")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, kGaladriel, 1);               // a 3/3
    PutGal(s, "Temple of Malice", 2);       // R
    PutGal(s, "Temple of Abandon", 3);      // R
    PutGal(s, kTemple, 4);
    PutGal(s, "Zhalfirin Void", 5);
    HandGal(s, "Temple of Plenty", 6);      // next turn: 5 mana
    s.players[1].life = 5;                  // 5 - 3 power = 2 -> X = 1: {3}{R}{R}, payable
    CHECK(KeepsGal(s, kCrackle));
    CHECK(KeepsGal(s, "Temple of Deceit"));
    CHECK_FALSE(KeepsGal(s, "Sol Ring"));
    CHECK_FALSE(KeepsGal(s, kSkerry));
    s.players[1].life = 20;                 // X = 4: 14 mana needed
    CHECK_FALSE(KeepsGal(s, kCrackle));
}

// ---- Fed any-colour filters cost their feed in the POOL too (claude-play sweep, seed 90000 T4) ----
// Crystal Grotto + Rumble Arena + Saprazzan Skerry (UU) + Zhalfirin Void: five raw mana, but both {R}
// must come through the filters, each eating a {1} -- so {3}{R}{R} (Crackle X=1) is unpayable while
// {1}{G}{U} (Galadriel: Skerry U + U feeding a filter for G + a {C}) is payable.
TEST_CASE("Mana pool: a fed filter's colour costs a feed -- {3}{R}{R} off two filters + Skerry + Void is unpayable")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, "Crystal Grotto", 1);
    PutGal(s, "Rumble Arena", 2);
    PutGal(s, kSkerry, 3);
    s.battlefield.back().counters.Add(Counter::Type::Depletion, 2);
    PutGal(s, "Zhalfirin Void", 4);
    const ManaPool pool = AvailableManaPool(s);
    CHECK(pool.Total() == 5);
    ManaCost crackle1; crackle1.generic = 3; crackle1.red = 2;
    ManaCost gal;      gal.generic = 1; gal.green = 1; gal.blue = 1;
    ManaCost three;    three.generic = 3;
    CHECK_FALSE(pool.CanPay(crackle1));
    CHECK(pool.CanPay(gal));
    CHECK(pool.CanPay(three));
}

TEST_CASE("Galadriel scry with Galadriel out: the last reachable Crackle is kept behind a stop card in the known pile (USER)")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, kGaladriel, 1);
    PutGal(s, kTemple, 2);
    auto lib_card = [](const std::string& n, int num) { Card c = DefGal(n).card; c.m_number = num; return c; };
    Library& lib = s.players[0].library;
    lib.push_back(lib_card("Temple of Plenty", 10));      // unknown upper library, no other Crackle
    CHECK_FALSE(KeepsGal(s, kCrackle));                   // nothing bottomed: it may go
    lib.push_back(lib_card("Temple of Deceit", 12)); lib.NoteBottomed();
    CHECK_FALSE(KeepsGal(s, kCrackle));                   // only a scry-land ahead: it chains in for free
    lib.push_back(lib_card("Sol Ring", 13)); lib.NoteBottomed();
    CHECK(KeepsGal(s, kCrackle));                         // a stop card ahead: a turn's wait per card
    HandGal(s, kCrackle, 14);
    CHECK_FALSE(KeepsGal(s, kCrackle));                   // one in hand already: still thrown
    s.players[0].hand.clear();
    {
        GameState s2 = s;                                 // the other Crackle still above the pile:
        Library& l2 = s2.players[0].library;              // the chain can run on and meet it
        l2 = Library{};
        l2.push_back(lib_card(kCrackle, 11));
        l2.push_back(lib_card("Sol Ring", 13)); l2.NoteBottomed();
        CHECK_FALSE(KeepsGal(s2, kCrackle));
    }
    {
        GameState s2 = s;                                 // a Crackle FIRST in the pile: this one may go
        Library& l2 = s2.players[0].library;
        l2 = Library{};
        l2.push_back(lib_card("Temple of Plenty", 10));
        l2.push_back(lib_card(kCrackle, 11)); l2.NoteBottomed();
        l2.push_back(lib_card("Sol Ring", 13)); l2.NoteBottomed();
        CHECK_FALSE(KeepsGal(s2, kCrackle));
    }
    lib.ShuffleByKey(7);
    CHECK(lib.KnownBottom() == 0);                        // a shuffle forgets the pile
    CHECK_FALSE(KeepsGal(s, kCrackle));
}

TEST_CASE("Galadriel scry with no Galadriel anywhere: keep only Galadriel (USER)")
{
    EnsureCardsLoadedGal();
    GameState s = FreshGal();
    PutGal(s, "Temple of Malice", 1);       // one land: short on lands, missing G and U
    HandGal(s, kCrackle, 2);
    CHECK(KeepsGal(s, kGaladriel));
    CHECK_FALSE(KeepsGal(s, kTemple));      // even the G/U land the rule above would keep
    CHECK_FALSE(KeepsGal(s, "Crystal Grotto"));
    CHECK_FALSE(KeepsGal(s, kCrackle));
    CHECK_FALSE(KeepsGal(s, "Sol Ring"));
    CHECK_FALSE(KeepsGal(s, kSkerry));
}

TEST_CASE("Galadriel scry rule is a FIRM verdict: a land-drop scry offers the search ONE candidate (USER)")
{
    // USER 2026-10-18: "No greedy in the search doesn't meant the heuristics in the provider cannot prune
    // the tree. They can and should." The seed 14 reference state: Galadriel + Crackle in hand, G/U out.
    EnsureCardsLoadedGal();
    static const GaladrielProvider gp;
    GameState s = FreshGal();
    s.m_provider = &gp;
    s.turn_number = 3;
    PutGal(s, "Temple of Epiphany", 1);
    PutGal(s, kTemple, 2);
    PutGal(s, "Zhalfirin Void", 3);
    HandGal(s, kGaladriel, 4);
    HandGal(s, kCrackle, 5);
    auto one = [](const std::string& n) { Card c = DefGal(n).card; c.m_number = 99; return std::vector<Card>{ c }; };
    const auto keep = TopDispositionCandidates(s, one("Temple of Plenty"), LookKind::Scry);
    REQUIRE(keep.size() == 1);
    CHECK(keep[0].top_order.size() == 1);                  // kept on top
    const auto toss = TopDispositionCandidates(s, one("Sol Ring"), LookKind::Scry);
    REQUIRE(toss.size() == 1);
    CHECK(toss[0].top_order.empty());                      // bottomed
}

TEST_CASE("Galadriel scry LAND TIMING: land keeps on mana in play, Sol Ring + filters fill one colour each (USER)")
{
    EnsureCardsLoadedGal();
    struct Arm
    {
        Arm()  { heurarm::t_arm[heurarm::GALADRIEL_LAND_TIMING] = 1; }
        ~Arm() { heurarm::t_arm[heurarm::GALADRIEL_LAND_TIMING] = -1; }
    } arm;
    SUBCASE("one land, short, acceleration still useful: a plain land is thrown, acceleration kept")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, kTemple, 1);              // G/U
        HandGal(s, kGaladriel, 2);
        CHECK_FALSE(KeepsGal(s, kHavens));
        CHECK_FALSE(KeepsGal(s, "Temple of Triumph"));
        CHECK(KeepsGal(s, "Sol Ring"));
        CHECK(KeepsGal(s, kSkerry));
    }
    SUBCASE("two lands, short: any land kept, acceleration no longer useful")
    {
        GameState s = FreshGal();
        s.turn_number = 2;
        PutGal(s, kTemple, 1);
        PutGal(s, "Temple of Malice", 2);
        HandGal(s, kGaladriel, 3);
        CHECK(KeepsGal(s, kHavens));
        CHECK_FALSE(KeepsGal(s, "Sol Ring"));
    }
    SUBCASE("no scry land in hand: kept only at 3+ mana (go off next turn)")
    {
        GameState s = FreshGal();
        s.turn_number = 2;
        PutGal(s, kTemple, 1);
        PutGal(s, "Temple of Malice", 2);
        HandGal(s, kGaladriel, 3);
        HandGal(s, kSkerry, 4);             // three lands, none a scry land in hand
        CHECK_FALSE(KeepsGal(s, "Temple of Plenty"));   // 2 mana: next turn's drop casts her
        s.turn_number = 3;
        PutGal(s, "Zhalfirin Void", 5);
        CHECK(KeepsGal(s, "Temple of Plenty"));         // 3 mana: she lands next turn
    }
    SUBCASE("Sol Ring fills one missing colour per filter held")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, "Temple of Malice", 1);   // B/R: G and U missing
        HandGal(s, kGaladriel, 2);
        HandGal(s, "Sol Ring", 3);
        HandGal(s, "Crystal Grotto", 4);
        HandGal(s, "Temple of Triumph", 5);
        CHECK_FALSE(KeepsGal(s, kCrackle)); // one filter: one colour still missing
        CHECK(KeepsGal(s, "Temple of Deceit"));
        HandGal(s, "Rumble Arena", 6);      // two filters: both filled
        CHECK(KeepsGal(s, kCrackle));
    }
    SUBCASE("late, no Sol Ring: a filter fills ONE colour, not both")
    {
        GameState s = FreshGal();           // turn 4: the filter can be fed
        PutGal(s, "Temple of Malice", 1);   // B/R
        PutGal(s, "Crystal Grotto", 2);
        HandGal(s, kGaladriel, 3);
        HandGal(s, "Temple of Triumph", 4);
        CHECK_FALSE(KeepsGal(s, kCrackle)); // one colour still missing
        CHECK(KeepsGal(s, "Temple of Deceit"));
    }
    SUBCASE("the top card completing the pair is a fix")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, "Temple of Plenty", 1);   // G: U missing
        HandGal(s, kGaladriel, 2);
        HandGal(s, "Crystal Grotto", 3);
        CHECK(KeepsGal(s, "Sol Ring"));
        heurarm::t_arm[heurarm::GALADRIEL_LAND_TIMING] = 0;
        CHECK_FALSE(KeepsGal(s, "Sol Ring"));   // shipped rule: colourless acceleration on bad colours
        heurarm::t_arm[heurarm::GALADRIEL_LAND_TIMING] = 1;
    }
}

// ---- THE PROPOSED AMENDMENT (MTG_GALADRIEL_SCRY_AMEND, default OFF, 2026-10-20): four clauses, each on the
// overnight 2026-10-19 board the shipped rule lost honestly (the non-clairvoyant search won a turn earlier).
TEST_CASE("Galadriel scry AMENDMENT: G/U pairing, a held Crackle, Sol Ring mana, an untapped third land")
{
    EnsureCardsLoadedGal();
    struct Arm
    {
        Arm()  { heurarm::t_arm[heurarm::GALADRIEL_SCRY_AMEND] = 1; }
        ~Arm() { heurarm::t_arm[heurarm::GALADRIEL_SCRY_AMEND] = -1; }
        static void Off() { heurarm::t_arm[heurarm::GALADRIEL_SCRY_AMEND] = 0; }
        static void On()  { heurarm::t_arm[heurarm::GALADRIEL_SCRY_AMEND] = 1; }
    } arm;
    SUBCASE("PAIR: a lone Temple of Mystery pays one of {G}{U}, so a second G or U source is a fix (s6006 g20)")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, kTemple, 1);                  // G/U -- the only G or U source
        HandGal(s, kGaladriel, 2);
        HandGal(s, "Temple of Malice", 3);      // B/R
        HandGal(s, kHavens, 4);                 // {C}
        CHECK(KeepsGal(s, "Temple of Enlightenment"));   // U: Mystery then pays G
        CHECK(KeepsGal(s, "Temple of Plenty"));          // G
        CHECK_FALSE(KeepsGal(s, "Temple of Triumph"));   // R/W fixes nothing
        CHECK_FALSE(KeepsGal(s, kCrackle));              // one pip short: bad shape (the USER's rule)
        Arm::Off();                                      // shipped: Mystery read as both colours
        CHECK_FALSE(KeepsGal(s, "Temple of Enlightenment"));
        CHECK(KeepsGal(s, kCrackle));
        Arm::On();
        HandGal(s, "Temple of Deceit", 5);               // U from a second land: the pair is in
        CHECK_FALSE(KeepsGal(s, "Temple of Enlightenment"));
    }
    SUBCASE("CRACKLE HELD: a second Crackle is thrown before Galadriel too (s4004 g187)")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, "Temple of Malady", 1);       // B/G
        HandGal(s, kSkerry, 2);                 // U
        HandGal(s, kGaladriel, 3);
        HandGal(s, kSkerry, 4);
        HandGal(s, kCrackle, 5);
        CHECK_FALSE(KeepsGal(s, kCrackle));
        Arm::Off();
        CHECK(KeepsGal(s, kCrackle));
        Arm::On();
    }
    SUBCASE("SOL MANA: two lands + Sol Ring cast her next turn, so the land to follow must scry (s4004 g824)")
    {
        GameState s = FreshGal();
        s.turn_number = 2;
        PutGal(s, "Temple of Malady", 1);       // G
        PutGal(s, "Temple of Deceit", 2);       // U
        HandGal(s, kGaladriel, 3);
        HandGal(s, "Sol Ring", 4);
        CHECK_FALSE(KeepsGal(s, kSkerry));      // starts no chain
        CHECK(KeepsGal(s, "Temple of Plenty"));
        Arm::Off();
        CHECK(KeepsGal(s, kSkerry));            // shipped: short on lands, any land
        Arm::On();
    }
    SUBCASE("UNTAPPED: two lands in play pay {G}{U}, no untapped land in hand -> an untapped land is next turn's drop and she comes with it")
    {
        GameState s = FreshGal();
        s.turn_number = 2;
        PutGal(s, kTemple, 1);                  // G/U
        PutGal(s, "Temple of Enlightenment", 2);    // U: with Mystery's G, the pair is in play
        HandGal(s, kGaladriel, 3);
        HandGal(s, "Temple of Triumph", 4);
        CHECK(KeepsGal(s, "Rumble Arena"));
        CHECK(KeepsGal(s, "Zhalfirin Void"));
        CHECK_FALSE(KeepsGal(s, "Temple of Plenty"));   // tapped: nothing gained
        Arm::Off();
        CHECK_FALSE(KeepsGal(s, "Rumble Arena"));
        Arm::On();
        HandGal(s, kHavens, 5);                 // an untapped land already in hand
        CHECK_FALSE(KeepsGal(s, "Rumble Arena"));
        s.players[0].hand.pop_back();
        s.battlefield.pop_back();
        PutGal(s, "Temple of Malice", 2);       // B/R: the U would come from a hand land
        HandGal(s, "Temple of Deceit", 6);
        CHECK_FALSE(KeepsGal(s, "Rumble Arena"));
    }
    SUBCASE("UNTAPPED: not at ONE land in play (that branch measured worse -- the land is played at once)")
    {
        GameState s = FreshGal();
        s.turn_number = 1;
        PutGal(s, kTemple, 1);                  // G/U
        HandGal(s, kGaladriel, 2);
        HandGal(s, "Temple of Enlightenment", 3);
        HandGal(s, "Temple of Triumph", 4);
        CHECK_FALSE(KeepsGal(s, "Crystal Grotto"));
    }
}
