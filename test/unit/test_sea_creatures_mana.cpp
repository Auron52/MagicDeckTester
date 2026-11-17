// Sea Creatures I1 (2026-11-16): the mana cards and the SeaCreaturesProvider prunes.
//   * Worn Powerstone (the Fire Diamond enters_tapped rock with Sol Ring's {C}{C}).
//   * Flooded Grove + fix F2: a land Aura on a filter land adds its bonus on EVERY greedy-payer tap (the free {C}
//     mode and the fed mode used to drop it).
//   * Hedge Maze + fix F1: a SEARCHED-OUT look-land surveils AFTER the search's shuffle (CR 603.3), on the
//     battlefield-model fetch route (PerformLandSearch) and the legacy one (PerformFetch).
//   * The USER's one-option fetch prune (Breeding Pool > Island > Forest > Hedge Maze; MTG_SEA_FETCH_PRUNE /
//     MTG_SEA_FETCH_ISLAND_FIRST) on Misty Rainforest's legacy route and the Windswept Heath / Flooded Strand
//     battlefield route.
//   * Utopia Sprawl always blue (MTG_SEA_SPRAWL_BLUE; the sound {U, G} fold at =0; exception E1 default off).

#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSea()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSea(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSea(const std::string& name, int number)
{
    Card c     = DefSea(name).card;
    c.m_number = number;
    return c;
}

const SeaCreaturesProvider& ProvSea()
{
    static const SeaCreaturesProvider p;
    return p;
}

GameState BaseSea(int turn = 1)
{
    GameState s;
    s.m_provider          = &ProvSea();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.game_seed           = 4242;
    return s;
}

Permanent& PutSea(GameState& s, const std::string& name, int number, int host = 0)
{
    Permanent p;
    p.card              = CardSea(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.aura_attached_to  = host;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandSea(GameState& s, const std::vector<std::string>& names)
{
    int n = 100;
    for (const std::string& nm : names) { s.players[0].hand.push_back(CardSea(nm, n++)); }
}

void LibSea(GameState& s, const std::vector<std::string>& names)
{
    while (s.players[0].library.size() > 0) { s.players[0].library.erase(s.players[0].library.begin()); }
    int n = 200;
    for (const std::string& nm : names) { s.players[0].library.push_back(CardSea(nm, n++)); }
}

// A per-test arm scope: the flag readers are static env reads, so tests flip the HeuristicArm slot instead.
struct ArmScope
{
    int slot;
    std::int8_t prev;
    ArmScope(int sl, int v) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm[slot] = prev; }
};

std::vector<std::string> ActivationNamesSea(const GameState& s, const CardParams& pp)
{
    std::vector<std::string> out;
    for (const std::vector<std::string>& pick : LandSearchPicks(s, 0, LandSearchSpecOf(pp)))
    { if (!pick.empty()) { out.push_back(pick.front()); } }
    return out;
}

std::vector<std::string> OnPlayNarrowed(const GameState& s, const CardDefinition& fetch)
{
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, fetch);
    NarrowOnPlayFetch(s, 0, fetch, c);
    return c;
}

}  // namespace

// ---- Worn Powerstone ---------------------------------------------------------------------------------------
TEST_CASE("Sea I1: Worn Powerstone is an enters-tapped two-colourless rock")
{
    EnsureCardsSea();
    const CardDefinition& d = DefSea("Worn Powerstone");
    CHECK(d.card.m_mana_cost.ManaValue() == 3);
    CHECK(d.params.mana_rock);
    CHECK(d.params.enters_tapped);
    CHECK(d.params.produces_amount == 2);
    REQUIRE(d.params.produces.size() == 1);
    CHECK(d.params.produces[0] == Color::Colorless);
    GameState s = BaseSea(4);
    Permanent& p = PutSea(s, "Worn Powerstone", 1);
    p.tapped = true;   // as it entered
    CHECK(AvailableManaPool(s).Total() == 0);
    s.battlefield.back().tapped = false;   // the next untap step
    const ManaPool pool = AvailableManaPool(s);
    CHECK(pool.Total() == 2);
    CHECK(pool.colorless == 2);
}

// ---- Flooded Grove + F2 -------------------------------------------------------------------------------------
TEST_CASE("Sea I1 F2: Wild Growth on Flooded Grove adds its {G} on the free {C} tap and on the fed tap")
{
    EnsureCardsSea();
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    {   // The free {C} tap: Grove alone pays {1} with its free {C} -- the Aura's {G} floats.
        GameState s = BaseSea(3);
        s.deck_has_land_aura = true;
        PutSea(s, "Flooded Grove", 1);
        PutSea(s, "Wild Growth", 2, /*host=*/1);
        ManaPool avail = AvailableManaPool(s);
        ManaCost one;
        one.generic = 1;
        REQUIRE(MtgTestSeam::TapForCost(eng, s, one, avail, false));
        CHECK(s.battlefield[0].tapped);
        CHECK(s.floating_mana.green == 1);
    }
    {   // The fed tap: a Forest feeds the Grove's {G/U}, the Grove yields {U}{U} -- and the Aura's {G} floats.
        GameState s = BaseSea(3);
        s.deck_has_land_aura = true;
        PutSea(s, "Flooded Grove", 1);
        PutSea(s, "Wild Growth", 2, /*host=*/1);
        PutSea(s, "Forest", 3);
        ManaPool avail = AvailableManaPool(s);
        ManaCost uu;
        uu.blue = 2;
        REQUIRE(MtgTestSeam::TapForCost(eng, s, uu, avail, false));
        CHECK(s.battlefield[0].tapped);
        CHECK(s.battlefield[2].tapped);
        CHECK(s.floating_mana.green == 1);
    }
}

// ---- Hedge Maze + F1 ----------------------------------------------------------------------------------------
namespace
{
// The library a search leaves, as the shuffle orders it, WITHOUT any look: the per-card shuffle key means
// removing the fetched Maze leaves every other card's relative order unchanged.
std::string PostShuffleTop(GameState s)
{
    Library& lib = s.players[0].library;
    for (std::size_t i = 0; i < lib.size(); ++i)
    { if (lib[i].m_name.str() == "Hedge Maze") { lib.erase(lib.begin() + static_cast<long>(i)); break; } }
    ShuffleAfterSearch(s, 0);
    return lib[0].m_name.str();
}
// Nothing here but the Maze is a Plains / Island / Forest card, so it is the search's only pick.
const std::vector<std::string> kF1Lib = { "Sol Ring", "Hedge Maze", "Overgrowth", "Wild Growth", "Utopia Sprawl",
                                          "Worn Powerstone", "Flooded Grove", "Lightning Bolt", "Birds of Paradise",
                                          "Llanowar Elves", "Soul Warden", "Essence Warden" };
}  // namespace

TEST_CASE("Sea I1 F1: a fetched Hedge Maze surveils the POST-shuffle top card (both fetch routes)")
{
    EnsureCardsSea();
    std::vector<std::string> seen;
    TopChooser rec = [&seen](const GameState&, const std::string&, const std::vector<Card>& looked, LookKind)
    {
        for (const Card& c : looked) { seen.push_back(c.m_name.str()); }
        TopDisposition d;
        for (int i = 0; i < static_cast<int>(looked.size()); ++i) { d.top_order.push_back(i); }   // keep
        return d;
    };
    const std::vector<std::string>& lib = kF1Lib;
    {   // The battlefield route: Flooded Strand's search (Plains or Island; Maze is the only Island).
        GameState s = BaseSea(2);
        LibSea(s, lib);
        const std::string pre_top = s.players[0].library[0].m_name.str();
        const std::string want = PostShuffleTop(s);
        CHECK_MESSAGE(want != pre_top, "fixture: the shuffle must move the top card for this test to discriminate");
        seen.clear();
        g_play_top_chooser = &rec;
        PerformLandSearch(s, 0, LandSearchSpecOf(DefSea("Flooded Strand").params), "Flooded Strand", -1);
        g_play_top_chooser = nullptr;
        bool maze_in_play = false;
        for (const Permanent& p : s.battlefield) { maze_in_play |= p.card.m_name.str() == "Hedge Maze"; }
        CHECK(maze_in_play);
        REQUIRE(seen.size() == 1);
        CHECK(seen[0] == want);
    }
    {   // The legacy immediate route (Misty Rainforest's autonomous model): PerformFetch.
        GameState s = BaseSea(2);
        LibSea(s, lib);
        const std::string want = PostShuffleTop(s);
        seen.clear();
        g_play_top_chooser = &rec;
        PerformFetch(s, 0, DefSea("Misty Rainforest").params, "Hedge Maze");
        g_play_top_chooser = nullptr;
        REQUIRE(seen.size() == 1);
        CHECK(seen[0] == want);
    }
}

// ---- The one-option fetch prune -----------------------------------------------------------------------------
TEST_CASE("Sea fetch prune: Breeding Pool first, on the legacy route (Misty) and the battlefield route (Heath)")
{
    EnsureCardsSea();
    ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1), isl(heurarm::SEA_FETCH_ISLAND_FIRST, 1);
    GameState s = BaseSea(1);
    HandSea(s, { "Misty Rainforest", "Windswept Heath", "Utopia Sprawl" });
    LibSea(s, { "Island", "Forest", "Hedge Maze", "Breeding Pool" });
    const CardDefinition& misty = DefSea("Misty Rainforest");
    const CardDefinition& heath = DefSea("Windswept Heath");
    // Legacy route: GenericProvider::FetchCandidates ordered by the rule, the search's set = the top pick.
    const std::vector<std::string> ranked = ProvSea().FetchCandidates(s, 0, misty.params);
    REQUIRE(ranked.size() == 4);
    CHECK(ranked == std::vector<std::string>{ "Breeding Pool", "Island", "Forest", "Hedge Maze" });
    CHECK(ProvSea().FetchSearchCandidates(s, 0, misty.params, ranked) == std::vector<std::string>{ "Breeding Pool" });
    CHECK(OnPlayNarrowed(s, misty) == std::vector<std::string>{ "Breeding Pool" });
    // Battlefield route: the activation list and the land drop read one list, narrowed to one.
    CHECK(ActivationNamesSea(s, heath.params) == std::vector<std::string>{ "Breeding Pool" });
    CHECK(OnPlayNarrowed(s, heath) == std::vector<std::string>{ "Breeding Pool" });
    // The land drop's plans carry only that target.
    bool any = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.land_to_play != "Misty Rainforest" && p.land_to_play != "Windswept Heath") { continue; }
        any = true;
        CHECK_MESSAGE(p.fetch_target == "Breeding Pool", p.land_to_play, " -> ", p.fetch_target);
    }
    CHECK(any);
}

TEST_CASE("Sea fetch prune: no Pool left -> Island first, Forest under MTG_SEA_FETCH_ISLAND_FIRST=0; per-fetch legality")
{
    EnsureCardsSea();
    ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1);
    const CardDefinition& misty  = DefSea("Misty Rainforest");
    const CardDefinition& heath  = DefSea("Windswept Heath");
    const CardDefinition& strand = DefSea("Flooded Strand");
    GameState s = BaseSea(2);
    LibSea(s, { "Forest", "Hedge Maze", "Island" });
    {
        ArmScope isl(heurarm::SEA_FETCH_ISLAND_FIRST, 1);
        CHECK(OnPlayNarrowed(s, misty) == std::vector<std::string>{ "Island" });
        CHECK(ActivationNamesSea(s, heath.params) == std::vector<std::string>{ "Forest" });    // no Island legal
        CHECK(ActivationNamesSea(s, strand.params) == std::vector<std::string>{ "Island" });   // no Forest legal
    }
    {
        ArmScope isl(heurarm::SEA_FETCH_ISLAND_FIRST, 0);
        CHECK(OnPlayNarrowed(s, misty) == std::vector<std::string>{ "Forest" });
        CHECK(ActivationNamesSea(s, strand.params) == std::vector<std::string>{ "Island" });
    }
    LibSea(s, { "Hedge Maze", "Forest" });
    CHECK(ActivationNamesSea(s, strand.params) == std::vector<std::string>{ "Hedge Maze" });
}

TEST_CASE("Sea fetch prune: the pick is stable under library order")
{
    EnsureCardsSea();
    ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1), isl(heurarm::SEA_FETCH_ISLAND_FIRST, 1);
    const CardDefinition& misty = DefSea("Misty Rainforest");
    const CardDefinition& heath = DefSea("Windswept Heath");
    std::vector<std::string> lib = { "Breeding Pool", "Forest", "Hedge Maze", "Island" };
    std::sort(lib.begin(), lib.end());
    do
    {
        GameState s = BaseSea(1);
        LibSea(s, lib);
        CHECK(OnPlayNarrowed(s, misty) == std::vector<std::string>{ "Breeding Pool" });
        CHECK(ActivationNamesSea(s, heath.params) == std::vector<std::string>{ "Breeding Pool" });
    } while (std::next_permutation(lib.begin(), lib.end()));
}

TEST_CASE("Sea fetch prune: =0 restores every target on both routes; <= 3 life keeps the full held list")
{
    EnsureCardsSea();
    const CardDefinition& misty = DefSea("Misty Rainforest");
    const CardDefinition& heath = DefSea("Windswept Heath");
    GameState s = BaseSea(1);
    LibSea(s, { "Island", "Forest", "Hedge Maze", "Breeding Pool" });
    {
        ArmScope prune(heurarm::SEA_FETCH_PRUNE, 0);
        CHECK(OnPlayNarrowed(s, misty).size() == 4);          // OnPlayFetchWidth 0: no cap
        CHECK(ActivationNamesSea(s, heath.params).size() == 3);   // Forest or Plains: Pool, Forest, Maze
    }
    {
        ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1);
        // The memo key carries the flag: flipping the arm on one thread serves the other arm's list.
        CHECK(ActivationNamesSea(s, heath.params) == std::vector<std::string>{ "Breeding Pool" });
        GameState low = s;
        low.players[0].life = 3;
        const std::vector<std::string> full = ActivationNamesSea(low, heath.params);
        REQUIRE(full.size() >= 2);
        CHECK(full[0] == "Breeding Pool");
        low.players[0].life = 4;
        CHECK(ActivationNamesSea(low, heath.params) == std::vector<std::string>{ "Breeding Pool" });
    }
}

TEST_CASE("Sea fetch Maze (MTG_SEA_FETCH_MAZE): admitted beside the untapped pick only when its mana is not needed")
{
    EnsureCardsSea();
    ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1), isl(heurarm::SEA_FETCH_ISLAND_FIRST, 1);
    const CardDefinition& misty = DefSea("Misty Rainforest");
    const CardDefinition& heath = DefSea("Windswept Heath");
    const std::vector<std::string> pool_maze{ "Breeding Pool", "Hedge Maze" };
    GameState idle = BaseSea(1);                           // T1, no mana, hand: only Koma (7 mana) -> not needed
    HandSea(idle, { "Misty Rainforest", "Koma, World-Eater" });   // a land in hand never reads as "needed"
    LibSea(idle, { "Island", "Forest", "Hedge Maze", "Breeding Pool" });
    GameState busy = idle;                                 // + Utopia Sprawl ({G}): one more mana casts it -> needed
    HandSea(busy, { "Utopia Sprawl" });
    {
        ArmScope maze(heurarm::SEA_FETCH_MAZE, 0);         // OFF (the default): the one-option rule, unchanged
        CHECK(OnPlayNarrowed(idle, misty) == std::vector<std::string>{ "Breeding Pool" });
        CHECK(ActivationNamesSea(idle, heath.params) == std::vector<std::string>{ "Breeding Pool" });
    }
    {
        ArmScope maze(heurarm::SEA_FETCH_MAZE, 1);
        // Not needed: both routes and the legacy prune search the Pool AND the Maze.
        CHECK(OnPlayNarrowed(idle, misty) == pool_maze);
        CHECK(ActivationNamesSea(idle, heath.params) == pool_maze);
        const std::vector<std::string> ranked = ProvSea().FetchCandidates(idle, 0, misty.params);
        CHECK(ProvSea().FetchSearchCandidates(idle, 0, misty.params, ranked) == pool_maze);
        bool pool = false, mz = false;
        for (const TurnSolver::Plan& pl : TurnSolver::EnumerateMainPlans(idle, true))
        {
            if (pl.land_to_play != "Misty Rainforest") { continue; }
            pool = pool || pl.fetch_target == "Breeding Pool";
            mz   = mz   || pl.fetch_target == "Hedge Maze";
            CHECK(pl.fetch_target != "Island");
        }
        CHECK(pool);
        CHECK(mz);
        // Needed (solo): one more {G} casts the Sprawl -> the untapped Pool alone.
        CHECK(OnPlayNarrowed(busy, misty) == std::vector<std::string>{ "Breeding Pool" });
        CHECK(ActivationNamesSea(busy, heath.params) == std::vector<std::string>{ "Breeding Pool" });
        // The memo key separates the two hands (the verdict is a key bit): re-reading idle after busy is right.
        CHECK(ActivationNamesSea(idle, heath.params) == pool_maze);
        // No Pool left: Island is the untapped pick and Maze rides beside it.
        GameState nopool = idle;
        LibSea(nopool, { "Forest", "Hedge Maze", "Island" });
        CHECK(OnPlayNarrowed(nopool, misty) == std::vector<std::string>{ "Island", "Hedge Maze" });
        // Maze the only target: just Maze.
        LibSea(nopool, { "Hedge Maze" });
        CHECK(OnPlayNarrowed(nopool, misty) == std::vector<std::string>{ "Hedge Maze" });
        // Prune off: the flag is inert (every target, as before).
        ArmScope off(heurarm::SEA_FETCH_PRUNE, 0);
        CHECK(OnPlayNarrowed(idle, misty).size() == 4);
    }
}

TEST_CASE("Sea fetch prune: human play / the unpruned audit is never narrowed (process-static gates)")
{
    EnsureCardsSea();
    ArmScope prune(heurarm::SEA_FETCH_PRUNE, 1);
    GameState s = BaseSea(1);
    LibSea(s, { "Island", "Forest", "Hedge Maze", "Breeding Pool" });
    const CardDefinition& misty = DefSea("Misty Rainforest");
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, misty);
    NarrowOnPlayFetch(s, 0, misty, c);
    // MTG_HUMAN_PLAY / MTG_UNPRUNED are process-static: assert the contract in whichever process runs this.
    if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::Fetch)) { CHECK(c.size() == 4); }
    else { CHECK(c == std::vector<std::string>{ "Breeding Pool" }); }
}

// ---- Utopia Sprawl -------------------------------------------------------------------------------------------
TEST_CASE("Sea Sprawl: always blue; =0 searches {U, G}; a W/B/R card breaks the fold; E1 admits green")
{
    EnsureCardsSea();
    const CardDefinition& sprawl = DefSea("Utopia Sprawl");
    const int u = static_cast<int>(Color::Blue) + 1, g = static_cast<int>(Color::Green) + 1;
    GameState s = BaseSea(2);
    PutSea(s, "Forest", 1);
    PutSea(s, "Island", 2);
    HandSea(s, { "Utopia Sprawl", "Worn Powerstone" });
    LibSea(s, { "Forest", "Island" });
    if (DecisionUnpruned(UnprunedGate::EtbColor)) { return; }   // the audit opens every colour (process-static)
    {
        ArmScope blue(heurarm::SEA_SPRAWL_BLUE, 1);
        CHECK(ProvSea().EtbChoiceCandidates(s, 0, sprawl) == std::vector<int>{ u });
        CHECK(ProvSea().EtbChosenColor(s, 0, sprawl) == static_cast<int>(Color::Blue));
    }
    {
        ArmScope blue(heurarm::SEA_SPRAWL_BLUE, 0);
        CHECK(ProvSea().EtbChoiceCandidates(s, 0, sprawl) == std::vector<int>{ u, g });
    }
    {   // A white card in the list: the {U, G} proof is gone, the full fan returns.
        ArmScope blue(heurarm::SEA_SPRAWL_BLUE, 1);
        GameState w = s;
        HandSea(w, { "Soul Warden" });
        CHECK(ProvSea().EtbChoiceCandidates(w, 0, sprawl).size() == 5);
    }
    {   // E1: one green source (the Forest) and two other {G} pips in hand -> {U, G}; off -> {U}.
        ArmScope blue(heurarm::SEA_SPRAWL_BLUE, 1);
        GameState e = BaseSea(2);
        PutSea(e, "Forest", 1);
        PutSea(e, "Island", 2);
        HandSea(e, { "Utopia Sprawl", "Wild Growth", "Overgrowth" });
        {
            ArmScope e1(heurarm::SEA_SPRAWL_GREEN_NEED, 0);
            CHECK(ProvSea().EtbChoiceCandidates(e, 0, sprawl) == std::vector<int>{ u });
        }
        {
            ArmScope e1(heurarm::SEA_SPRAWL_GREEN_NEED, 1);
            CHECK(ProvSea().EtbChoiceCandidates(e, 0, sprawl) == std::vector<int>{ u, g });
            PutSea(e, "Forest", 3);   // a second green source: no exception
            CHECK(ProvSea().EtbChoiceCandidates(e, 0, sprawl) == std::vector<int>{ u });
        }
    }
}

TEST_CASE("Sea routing: the list resolves to SeaCreaturesProvider")
{
    EnsureCardsSea();
    const Decklist d = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Sea Creatures/Sea Creatures.cod"));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "SeaCreatures");
}

// ---- Misty Rainforest: the battlefield model for a HUMAN, the immediate route for the search ---------------
// USER 2026-11-16: "We don't need the hold option for those decks or for this deck." / "(in searched)" / "The viewer
// can keep it." -- fetch_hold_human_only. Autonomous play cracks on play at EVERY life total (no <= 3-life hold) and
// reads the legacy list (FetchCandidates), so Creature Giving / Five Colour play is unchanged.
TEST_CASE("Sea I1: Misty Rainforest is a battlefield fetchland whose hold is the viewer's only")
{
    EnsureCardsSea();
    const CardDefinition& misty = DefSea("Misty Rainforest");
    const CardDefinition& heath = DefSea("Windswept Heath");
    CHECK(misty.params.land_search_activation_cost.has_value());
    CHECK(misty.params.land_search_activation_sacrifice_self);
    CHECK(misty.params.land_search_activation_life_cost == 1);
    CHECK(misty.params.fetch_hold_human_only);
    CHECK_FALSE(heath.params.fetch_hold_human_only);
    if (HumanPlayActive()) { return; }   // process-static: the human contract is the viewer's (HumanFetchHoldVariant)
    static const CreatureGivingProvider cg;
    for (const DecisionProvider* prov : { static_cast<const DecisionProvider*>(&ProvSea()),
                                          static_cast<const DecisionProvider*>(&cg) })
    {
        GameState s = BaseSea(2);
        s.m_provider = prov;
        LibSea(s, { "Island", "Breeding Pool", "Forest" });
        for (int life : { 20, 3, 1 })
        {
            s.players[0].life = life;
            CHECK_MESSAGE(FetchResolvesOnPlay(s, misty), prov->Name(), " life ", life);   // never held in search
            CHECK_FALSE(FetchCrackOnPlayOffered(s, misty));
            // The legacy candidate list: the provider's FetchCandidates, exactly.
            CHECK(OnPlayFetchCandidates(s, 0, misty) == prov->FetchCandidates(s, 0, misty.params));
        }
        // A battlefield fetch without the flag keeps its existing <= 3-life autonomous hold (unchanged).
        s.players[0].life = 3;
        CHECK_FALSE(FetchResolvesOnPlay(s, heath));
    }
}
