// Fetch TARGETS for a battlefield-model fetchland resolving at its land drop (2026-11-14). USER: "We should obviously use
// the same heuristic that just grabs Stomping Ground." The on-play route (OnPlayFetchCandidates: the TurnSolver
// land-drop fan, PerformFetch's default) reads the SAME list as the fetchland's activation (LandSearchPicksMemo,
// ordered by LandSearchFetchPickOrder -- base: the colours the hand NEEDS and the board LACKS first -- and narrowed by
// LandSearchFetchPickWidth). A legacy immediate-model fetchland (Misty Rainforest) orders GenericProvider's list by
// the same rule. Before: library order, top pick only -- Boars overnight seed 5158 gi153 T1 cracked Wooded Foothills
// for MOUNTAIN with Birds of Paradise and Herald of Ilharg in hand, while the activation route took Stomping Ground.

#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/LandSearch.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsFr()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFr(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardFr(const std::string& name, int number)
{
    Card c     = DefFr(name).card;
    c.m_number = number;
    return c;
}

void PutFr(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardFr(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

GameState BaseFr(const DecisionProvider& prov, int turn)
{
    GameState s;
    s.m_provider          = &prov;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

void Hand(GameState& s, const std::vector<std::string>& names)
{
    int n = 100;
    for (const std::string& nm : names) { s.players[0].hand.push_back(CardFr(nm, n++)); }
}

void SetLib(GameState& s, const std::vector<std::string>& names)
{
    while (s.players[0].library.size() > 0) { s.players[0].library.erase(s.players[0].library.begin()); }
    int n = 200;
    for (const std::string& nm : names) { s.players[0].library.push_back(CardFr(nm, n++)); }
}

}  // namespace

namespace
{
std::vector<std::string> ActivationNames(const GameState& s, const CardParams& pp)
{
    std::vector<std::string> out;
    for (const std::vector<std::string>& pick : LandSearchPicks(s, 0, LandSearchSpecOf(pp)))
    { if (!pick.empty()) { out.push_back(pick.front()); } }
    return out;
}
}  // namespace

TEST_CASE("fetch targets: the gi153 T1 hand takes Stomping Ground on play, the same list as the activation")
{
    EnsureCardsFr();
    static const BoarsProvider bp;
    GameState s = BaseFr(bp, 1);
    Hand(s, { "Herald of Ilharg", "Somberwald Sage", "Somberwald Sage", "Birds of Paradise", "Wooded Foothills" });
    // Library order as it bit: Mountain is the first legal name.
    SetLib(s, { "Ilharg, the Raze-Boar", "Mountain", "Flinthoof Boar", "Stomping Ground", "Forest", "Gruul Turf" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
    REQUIRE(c.size() == 3);   // every legal target is searched (Gruul Turf is not a Mountain / Forest)
    CHECK(c[0] == "Stomping Ground");
    CHECK(c == ActivationNames(s, foothills.params));
    std::vector<std::string> n = c;
    NarrowOnPlayFetch(s, 0, foothills, n);
    // The land drop's own width (OnPlayFetchWidth, default the top pick of the shared order, 2026-11-14).
    CHECK(n == std::vector<std::string>{ "Stomping Ground" });
    // No Stomping Ground left: Forest (green for the Birds) is not behind the library-first Mountain.
    SetLib(s, { "Mountain", "Forest" });
    c = OnPlayFetchCandidates(s, 0, foothills);
    REQUIRE(c.size() == 2);
    CHECK(c[0] == "Forest");
    CHECK(c == ActivationNames(s, foothills.params));
}

TEST_CASE("fetch targets: stable under library order")
{
    EnsureCardsFr();
    static const BoarsProvider bp;
    GameState s = BaseFr(bp, 1);
    Hand(s, { "Herald of Ilharg", "Birds of Paradise", "Wooded Foothills" });
    std::vector<std::string> lib = { "Forest", "Mountain", "Stomping Ground" };
    std::sort(lib.begin(), lib.end());
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    std::vector<std::string> first;
    do
    {
        SetLib(s, lib);
        const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        if (first.empty()) { first = c; }
        CHECK(c == first);
    } while (std::next_permutation(lib.begin(), lib.end()));
    REQUIRE(first.size() == 3);
    CHECK(first[0] == "Stomping Ground");
}

TEST_CASE("fetch targets: Creature Giving -- the battlefield Heath and the legacy Misty Rainforest read one rule")
{
    EnsureCardsFr();
    static const CreatureGivingProvider cg;
    {
        // Soul Warden {W} + Essence Warden {G}: Temple Garden covers both lacking colours.
        GameState s = BaseFr(cg, 1);
        Hand(s, { "Essence Warden", "Soul Warden", "Windswept Heath" });
        SetLib(s, { "Forest", "Overgrown Tomb", "Stomping Ground", "Breeding Pool", "Temple Garden" });
        const CardDefinition& heath = DefFr("Windswept Heath");
        const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, heath);
        REQUIRE(c.size() == 5);
        CHECK(c[0] == "Temple Garden");
        CHECK(c == ActivationNames(s, heath.params));
    }
    {
        // Hunted Phantasm {1}{U}{U} with a Forest down: blue is the lacking colour -> Breeding Pool, through the
        // legacy (FetchCandidates) route; the land drop searches its top pick (OnPlayFetchWidth default 1).
        GameState s = BaseFr(cg, 2);
        PutFr(s, "Forest", 50);
        Hand(s, { "Hunted Phantasm", "Misty Rainforest" });
        SetLib(s, { "Temple Garden", "Overgrown Tomb", "Breeding Pool" });
        const CardDefinition& misty = DefFr("Misty Rainforest");
        std::vector<std::string> c = OnPlayFetchCandidates(s, 0, misty);
        REQUIRE(c.size() == 3);
        CHECK(c[0] == "Breeding Pool");
        std::vector<std::string> n = c;
        NarrowOnPlayFetch(s, 0, misty, n);
        CHECK(n == std::vector<std::string>{ "Breeding Pool" });
    }
}

TEST_CASE("fetch targets: a provider-ranked battlefield fetch keeps its ranking and its top-1 on both routes (Morph)")
{
    EnsureCardsFr();
    static const MorphProvider mp;
    GameState s = BaseFr(mp, 3);
    SetLib(s, { "Forest", "Mountain", "Stomping Ground" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
    CHECK(c == std::vector<std::string>{ "Stomping Ground" });   // INT-9's pick, narrowed (an existing prune)
    CHECK(c == ActivationNames(s, foothills.params));
}

TEST_CASE("fetch search width: the base default searches every target; the reviewed provider prunes stay top-1")
{
    EnsureCardsFr();
    const std::vector<std::string> ranked = { "Stomping Ground", "Forest", "Mountain" };
    const CardParams& foothills = DefFr("Wooded Foothills").params;
    static const GenericProvider gp;
    static const BoarsProvider bp;
    static const CreatureGivingProvider cg;
    static const FiveColourProvider fc;
    static const MorphProvider mp;
    static const AntiLifegainProvider al;
    static const ModernRwBurnProvider rw;
    GameState s = BaseFr(gp, 1);
    // USER 2026-11-14: "why do we only try the first candidate by default? ... It's a different story if we have
    // an intentional prune."
    CHECK(gp.FetchSearchCandidates(s, 0, foothills, ranked) == ranked);
    CHECK(bp.FetchSearchCandidates(s, 0, foothills, ranked) == ranked);
    CHECK(cg.FetchSearchCandidates(s, 0, foothills, ranked) == ranked);
    // The existing provider prunes (pending USER review), byte-identical to the old default.
    const std::vector<std::string> top = { "Stomping Ground" };
    CHECK(fc.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(mp.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(al.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(rw.FetchSearchCandidates(s, 0, foothills, ranked) == top);
}
