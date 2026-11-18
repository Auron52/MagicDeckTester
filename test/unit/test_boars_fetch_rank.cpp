// Fetch TARGETS for a battlefield-model fetchland resolving at its land drop (2026-11-14). USER: "We should obviously use
// the same heuristic that just grabs Stomping Ground." The on-play route (OnPlayFetchCandidates: the TurnSolver
// land-drop fan, PerformFetch's default) reads the SAME list as the fetchland's activation (LandSearchPicksMemo,
// ordered by LandSearchFetchPickOrder -- base: the colours the hand NEEDS and the board LACKS first -- and narrowed by
// LandSearchFetchPickWidth). A legacy immediate-model fetchland (Misty Rainforest) orders GenericProvider's list by
// the same rule. Before: library order, top pick only -- Boars overnight seed 5158 gi153 T1 cracked Wooded Foothills
// for MOUNTAIN with Birds of Paradise and Herald of Ilharg in hand, while the activation route took Stomping Ground.

#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/LandSearch.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

void SetShockFirstTestEnv(const char* value)
{
#ifdef _WIN32
    _putenv_s("MTG_BOARS_SHOCK_FIRST", value ? value : "");
#else
    if (value) { setenv("MTG_BOARS_SHOCK_FIRST", value, 1); }
    else { unsetenv("MTG_BOARS_SHOCK_FIRST"); }
#endif
}
struct ShockFirstEnvScope
{
    bool had;
    std::string prior;
    explicit ShockFirstEnvScope(const char* value)
    {
        const char* old = std::getenv("MTG_BOARS_SHOCK_FIRST");
        had = old != nullptr;
        prior = old ? old : "";
        SetShockFirstTestEnv(value);
    }
    ~ShockFirstEnvScope() { SetShockFirstTestEnv(had ? prior.c_str() : nullptr); }
};

struct FetchPruneEnvScope
{
    bool had;
    std::string prior;
    explicit FetchPruneEnvScope(const char* value)
    {
        const char* old = std::getenv("MTG_BOARS_FETCH_PRUNE");
        had = old != nullptr;
        prior = old ? old : "";
#ifdef _WIN32
        _putenv_s("MTG_BOARS_FETCH_PRUNE", value ? value : "");
#else
        if (value) { setenv("MTG_BOARS_FETCH_PRUNE", value, 1); }
        else { unsetenv("MTG_BOARS_FETCH_PRUNE"); }
#endif
    }
    ~FetchPruneEnvScope()
    {
#ifdef _WIN32
        _putenv_s("MTG_BOARS_FETCH_PRUNE", had ? prior.c_str() : "");
#else
        if (had) { setenv("MTG_BOARS_FETCH_PRUNE", prior.c_str(), 1); }
        else { unsetenv("MTG_BOARS_FETCH_PRUNE"); }
#endif
    }
};

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

TEST_CASE("fetch target: Stomping Ground keeps the two-Birds alternative to War-Riders")
{
    EnsureCardsFr();
    static const CreatureGivingProvider cg;
    GameState s = BaseFr(cg, 2);
    s.players[0].life = 28;
    s.players[1].life = 30;
    PutFr(s, "Temple Garden", 51);
    PutFr(s, "Soul Warden", 39);
    Hand(s, { "Massacre Wurm", "Defense of the Heart", "Windswept Heath",
              "Sylvan Scrying", "Birds of Paradise", "Birds of Paradise",
              "Varchild's War-Riders" });
    SetLib(s, { "Stomping Ground", "Overgrown Tomb", "Forest", "Forbidden Orchard" });
    const std::int8_t prev = heurarm::t_arm[heurarm::FETCH_RANK_PIPS];
    heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = 1;
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = prev;
    bool birds = false, riders = false;
    for (const auto& p : plans)
    {
        if (p.land_to_play != "Windswept Heath") { continue; }
        int b = 0, r = 0;
        for (const auto& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            b += a.card_name == "Birds of Paradise";
            r += a.card_name == "Varchild's War-Riders";
        }
        birds |= p.fetch_target == "Stomping Ground" && b == 2 && r == 0;
        riders |= p.fetch_target == "Stomping Ground" && r == 1 && b == 0;
    }
    CHECK(riders);
    CHECK(birds);
}

TEST_CASE("fetch target: each selected cast line gets its required colour")
{
    EnsureCardsFr();
    static const GenericProvider gp;
    GameState s = BaseFr(gp, 1);
    PutFr(s, "City of Brass", 50);   // either fetch target can pay either one-spell line
    Hand(s, { "Wooded Foothills", "Birds of Paradise", "Lightning Bolt" });
    SetLib(s, { "Mountain", "Forest" });
    const std::int8_t prev = heurarm::t_arm[heurarm::FETCH_RANK_PIPS];
    heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = 1;
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = prev;
    bool birds_with_forest = false, bolt_with_mountain = false;
    for (const auto& p : plans)
    {
        if (p.land_to_play != "Wooded Foothills") { continue; }
        for (const auto& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name == "Birds of Paradise")
            {
                birds_with_forest |= p.fetch_target == "Forest";
            }
            if (a.card_name == "Lightning Bolt")
            {
                bolt_with_mountain |= p.fetch_target == "Mountain";
            }
        }
    }
    CHECK(birds_with_forest);
    CHECK(bolt_with_mountain);
    Action one;
    one.cost = DefFr("Birds of Paradise").card.m_mana_cost;
    CHECK(gp.FetchLineRankKey(s, 0, {one}, "Forest") > gp.FetchLineRankKey(s, 0, {one}, "Mountain"));
    one.cost = DefFr("Lightning Bolt").card.m_mana_cost;
    CHECK(gp.FetchLineRankKey(s, 0, {one}, "Mountain") > gp.FetchLineRankKey(s, 0, {one}, "Forest"));
}

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
    FetchPruneEnvScope full_candidates("0");
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
    FetchPruneEnvScope full_candidates("0");
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

TEST_CASE("fetch land-drop plans keep a single provider target named through resolution")
{
    EnsureCardsFr();
    static const FiveColourProvider fc;
    GameState s = BaseFr(fc, 2);
    PutFr(s, "Deathrite Shaman", 50);
    PutFr(s, "Breeding Pool", 51);
    Hand(s, { "Wooded Foothills", "Jared Carthalion", "Nicol Bolas, Planeswalker" });
    SetLib(s, { "Forest", "Mountain", "Stomping Ground", "Overgrown Tomb", "Jetmir's Garden" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
    REQUIRE(c.size() == 1);
    int land_plans = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.land_to_play != "Wooded Foothills") { continue; }
        ++land_plans;
        CHECK(p.fetch_target == c.front());
    }
    CHECK(land_plans > 0);
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
    CHECK(bp.FetchSearchCandidates(s, 0, foothills, ranked) == std::vector<std::string>{ "Stomping Ground" });
    CHECK(cg.FetchSearchCandidates(s, 0, foothills, ranked) == ranked);
    // The existing provider prunes (pending USER review), byte-identical to the old default.
    const std::vector<std::string> top = { "Stomping Ground" };
    CHECK(fc.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(mp.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(al.FetchSearchCandidates(s, 0, foothills, ranked) == top);
    CHECK(rw.FetchSearchCandidates(s, 0, foothills, ranked) == top);
}

// ---- MTG_FETCH_RANK_PIPS: the deliberate ranking (Boars sweep seed 97005, USER rulings 2026-11-14) --------------------
// With red and green both on the board the plain rule (NEEDED and LACKED colours) ties every target, and the list fell
// back to name / library order: the top-1 land drop fetched Forest while Ilharg ({3}{R}{R}) had one red source. The
// ranking: (1) the land adds a pip to the CASTABLE spells' pips matched SIMULTANEOUSLY against our sources, one pip per
// source (CG gi113: Birds is one mana, not a source of every colour at once); (2) the plain needed-and-lacked cover;
// (3) a shock / dual over a basic (USER: "getting a shock over a basic is usually what we want to do anyway. The only
// exception is if we have no shocks in the colour our line needs."); (4) castable-demanded colours made; (5) name.
namespace
{
struct FetchPipsArm
{
    std::int8_t prev;
    explicit FetchPipsArm(bool on) : prev(heurarm::t_arm[heurarm::FETCH_RANK_PIPS])
    { heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = on ? 1 : 0; }
    ~FetchPipsArm() { heurarm::t_arm[heurarm::FETCH_RANK_PIPS] = prev; }
};

GameState Boars97005T4(const DecisionProvider& prov)
{
    GameState s = BaseFr(prov, 4);
    PutFr(s, "Forest", 50);
    PutFr(s, "Stomping Ground", 51);
    PutFr(s, "Zhur-Taa Druid", 52);
    PutFr(s, "Forest", 53);
    PutFr(s, "Herald's Horn", 54);
    Hand(s, { "Decimator of the Provinces", "Wooded Foothills", "Ilharg, the Raze-Boar", "End-Raze Forerunners",
              "Wooded Foothills" });
    SetLib(s, { "Flinthoof Boar", "Forest", "Mountain", "Birds of Paradise", "Stomping Ground", "Forest" });
    return s;
}
}  // namespace

TEST_CASE("Boars default fetch order covers Ilharg's second red pip after the shock is gone")
{
    EnsureCardsFr();
    static const BoarsProvider bp;
    GameState s = Boars97005T4(bp);
    SetLib(s, { "Forest", "Mountain", "Forest" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    CHECK(OnPlayFetchCandidates(s, 0, foothills).front() == "Mountain");
}

TEST_CASE("fetch ranking: the 97005 T4 board fetches the next Stomping Ground (a SECOND red source for Ilharg)")
{
    FetchPruneEnvScope full_candidates("0");
    EnsureCardsFr();
    static const BoarsProvider bp;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    GameState s = Boars97005T4(bp);
    {
        ShockFirstEnvScope shock_off("0");
        FetchPipsArm off(false);
        const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 3);
        CHECK(c[0] == "Forest");   // the defect: every target ties, name order
    }
    {
        ShockFirstEnvScope shock_on("1");
        FetchPipsArm on(true);
        std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 3);
        // Ilharg (castable at five mana) needs RR and one red source pays one pip: Mountain and Stomping Ground each
        // add a matched pip, Forest adds none; of the two the SHOCK beats the basic (the line's colour is red and a
        // shock makes it). Stomping Ground is also the second red source Ilharg needs.
        CHECK(c == std::vector<std::string>{ "Stomping Ground", "Mountain", "Forest" });
        CHECK(c == ActivationNames(s, foothills.params));
        NarrowOnPlayFetch(s, 0, foothills, c);
        CHECK(c == std::vector<std::string>{ "Stomping Ground" });
        // No Stomping Ground left: Mountain (the colour the line needs), not Forest.
        SetLib(s, { "Forest", "Mountain", "Forest" });
        c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 2);
        CHECK(c[0] == "Mountain");
        // Ilharg gone from the hand: nothing left is castable at five mana (End-Raze 8, Decimator 10) and the board
        // makes both colours -> the shock anyway (two-colour deck: "always get the shock and then basics after that"),
        // then the basics by name (neither adds a pip or covers a lacked colour).
        GameState t = Boars97005T4(bp);
        t.players[0].hand.erase(t.players[0].hand.begin() + 2);
        c = OnPlayFetchCandidates(t, 0, foothills);
        CHECK(c == std::vector<std::string>{ "Stomping Ground", "Forest", "Mountain" });
    }
}

TEST_CASE("Boars two-colour fetch rule: Stomping Ground precedes basics on both routes")
{
    FetchPruneEnvScope full_candidates("0");
    EnsureCardsFr();
    static const BoarsProvider bp;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    GameState s = Boars97005T4(bp);
    ShockFirstEnvScope shock_on("1");
    {
        FetchPipsArm off(false);
        std::vector<std::string> picks = OnPlayFetchCandidates(s, 0, foothills);
        CHECK(picks == std::vector<std::string>{ "Stomping Ground", "Forest", "Mountain" });
        CHECK(picks == ActivationNames(s, foothills.params));
        NarrowOnPlayFetch(s, 0, foothills, picks);
        CHECK(picks == std::vector<std::string>{ "Stomping Ground" });
    }
}

TEST_CASE("Boars fetch prune: a held crack takes only the shared top pick, including the basic fallback")
{
    EnsureCardsFr();
    static const BoarsProvider bp;
    GameState s = BaseFr(bp, 1);
    Hand(s, { "Birds of Paradise", "Wooded Foothills" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    ShockFirstEnvScope shock_on("1");
    FetchPipsArm pips_off(false);
    SetLib(s, { "Mountain", "Forest", "Stomping Ground" });
    {
        FetchPruneEnvScope off("0");
        CHECK(ActivationNames(s, foothills.params)
              == std::vector<std::string>{ "Stomping Ground", "Forest", "Mountain" });
    }
    {
        FetchPruneEnvScope on("1");
        CHECK(ActivationNames(s, foothills.params) == std::vector<std::string>{ "Stomping Ground" });
        SetLib(s, { "Mountain", "Forest" });
        CHECK(ActivationNames(s, foothills.params) == std::vector<std::string>{ "Forest" });
        CHECK(bp.FetchSearchCandidates(s, 0, foothills.params, { "Forest", "Mountain" })
              == std::vector<std::string>{ "Forest" });
    }
    {   // The memo must distinguish the two arms even when the library is unchanged.
        FetchPruneEnvScope off("0");
        CHECK(ActivationNames(s, foothills.params) == std::vector<std::string>{ "Forest", "Mountain" });
    }
}

TEST_CASE("fetch ranking: the plain rule's decisions stand (gi153 Stomping Ground, Creature Giving)")
{
    FetchPruneEnvScope full_candidates("0");
    EnsureCardsFr();
    FetchPipsArm on(true);
    {
        static const BoarsProvider bp;
        GameState s = BaseFr(bp, 1);
        Hand(s, { "Herald of Ilharg", "Somberwald Sage", "Somberwald Sage", "Birds of Paradise", "Wooded Foothills" });
        SetLib(s, { "Ilharg, the Raze-Boar", "Mountain", "Flinthoof Boar", "Stomping Ground", "Forest", "Gruul Turf" });
        const CardDefinition& foothills = DefFr("Wooded Foothills");
        std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 3);
        CHECK(c[0] == "Stomping Ground");
        CHECK(c == ActivationNames(s, foothills.params));
        SetLib(s, { "Mountain", "Forest" });
        c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 2);
        CHECK(c[0] == "Forest");
    }
    {
        static const CreatureGivingProvider cg;
        GameState s = BaseFr(cg, 1);
        Hand(s, { "Essence Warden", "Soul Warden", "Windswept Heath" });
        SetLib(s, { "Forest", "Overgrown Tomb", "Stomping Ground", "Breeding Pool", "Temple Garden" });
        const CardDefinition& heath = DefFr("Windswept Heath");
        CHECK(OnPlayFetchCandidates(s, 0, heath)[0] == "Temple Garden");
    }
}

TEST_CASE("fetch ranking: library order never decides (battlefield list and the legacy FetchCandidates list)")
{
    FetchPruneEnvScope full_candidates("0");
    EnsureCardsFr();
    FetchPipsArm on(true);
    {
        static const BoarsProvider bp;
        GameState s = Boars97005T4(bp);
        const CardDefinition& foothills = DefFr("Wooded Foothills");
        std::vector<std::string> lib = { "Forest", "Mountain", "Stomping Ground" };
        std::sort(lib.begin(), lib.end());
        do
        {
            SetLib(s, lib);
            CHECK(OnPlayFetchCandidates(s, 0, foothills)
                  == std::vector<std::string>{ "Stomping Ground", "Mountain", "Forest" });
        } while (std::next_permutation(lib.begin(), lib.end()));
    }
    {
        // Legacy immediate-model Bloodstained Mire through GenericProvider::FetchCandidates (library order in): a board
        // that makes B and R, a hand whose Ilharg (castable at five mana) needs a second red -> the red producers
        // first, the shock (Blood Crypt) before the basic.
        static const GenericProvider gp;
        GameState s = BaseFr(gp, 5);
        PutFr(s, "Swamp", 50);
        PutFr(s, "Mountain", 51);
        PutFr(s, "Swamp", 52);
        PutFr(s, "Swamp", 53);
        Hand(s, { "Ilharg, the Raze-Boar", "Bloodstained Mire" });
        const CardParams& mire = DefFr("Bloodstained Mire").params;
        std::vector<std::string> lib = { "Blood Crypt", "Mountain", "Swamp" };
        std::sort(lib.begin(), lib.end());
        do
        {
            SetLib(s, lib);
            CHECK(gp.FetchCandidates(s, 0, mire) == std::vector<std::string>{ "Blood Crypt", "Mountain", "Swamp" });
        } while (std::next_permutation(lib.begin(), lib.end()));
    }
}

TEST_CASE("fetch ranking: the provider rankings are untouched (Morph)")
{
    EnsureCardsFr();
    static const MorphProvider mp;
    GameState s = BaseFr(mp, 3);
    SetLib(s, { "Forest", "Mountain", "Stomping Ground" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    std::vector<std::string> off_c, on_c;
    { FetchPipsArm off(false); off_c = OnPlayFetchCandidates(s, 0, foothills); }
    { FetchPipsArm on(true);   on_c  = OnPlayFetchCandidates(s, 0, foothills); }
    CHECK(off_c == on_c);
}

TEST_CASE("fetch ranking: CG smoke gi113 -- Birds is ONE source, so W and R are not both covered")
{
    // Creature Giving d5 smoke seed 1114 gi113, T2: Overgrown Tomb + Birds of Paradise; Enlightened Tutor {W} and
    // Varchild's War-Riders {1}{R} castable at three mana. Breeding Pool (first by name/library) pays neither pip
    // beside Birds; Stomping Ground or Temple Garden pays one while Birds pays the other (T4 -> T5 when missed).
    EnsureCardsFr();
    static const CreatureGivingProvider cg;
    GameState s = BaseFr(cg, 2);
    PutFr(s, "Overgrown Tomb", 50);
    PutFr(s, "Birds of Paradise", 51);
    Hand(s, { "Enlightened Tutor", "Varchild's War-Riders", "Massacre Wurm", "Defense of the Heart", "Windswept Heath" });
    SetLib(s, { "Temple Garden", "Breeding Pool", "Forest", "Stomping Ground", "Overgrown Tomb" });
    const CardDefinition& heath = DefFr("Windswept Heath");
    {
        FetchPipsArm off(false);
        CHECK(OnPlayFetchCandidates(s, 0, heath)[0] == "Breeding Pool");   // the defect
    }
    FetchPipsArm on(true);
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, heath);
    REQUIRE(c.size() == 5);
    // Stomping Ground (R while Birds pays W) and Temple Garden (W while Birds pays R) both add a pip and both are
    // shocks making one demanded colour: name. Then the shocks that add nothing, then the basic.
    CHECK(c == std::vector<std::string>{ "Stomping Ground", "Temple Garden", "Breeding Pool", "Overgrown Tomb", "Forest" });
    CHECK(c == ActivationNames(s, heath.params));
    NarrowOnPlayFetch(s, 0, heath, c);
    CHECK(c == std::vector<std::string>{ "Stomping Ground" });
    // Library order never decides here either.
    std::vector<std::string> lib = { "Breeding Pool", "Forest", "Stomping Ground", "Temple Garden" };
    do
    {
        SetLib(s, lib);
        CHECK(OnPlayFetchCandidates(s, 0, heath)
              == std::vector<std::string>{ "Stomping Ground", "Temple Garden", "Breeding Pool", "Forest" });
    } while (std::next_permutation(lib.begin(), lib.end()));
}

TEST_CASE("fetch ranking: a two-mana Karoo brings a costly spell's required colour into range")
{
    // Creature Giving 2HG d3 gi165, T5: Chancery makes {W}{U}, so the fetch gives six mana for Massacre Wurm
    // {3}{B}{B}{B}. Birds and Orchard can make two black; Overgrown Tomb supplies the third. Counting Chancery
    // as only one mana excluded Wurm and fetched Breeding Pool (T7 rather than T5 in the blind replay).
    EnsureCardsFr();
    FetchPipsArm on(true);
    static const CreatureGivingProvider cg;
    GameState s = BaseFr(cg, 5);
    PutFr(s, "Birds of Paradise", 50);
    PutFr(s, "Forbidden Orchard", 51);
    PutFr(s, "Azorius Chancery", 52);
    PutFr(s, "Stomping Ground", 53);
    Hand(s, { "Massacre Wurm", "Varchild's War-Riders", "Windswept Heath" });
    SetLib(s, { "Breeding Pool", "Overgrown Tomb", "Stomping Ground", "Temple Garden", "Forest" });
    const CardDefinition& heath = DefFr("Windswept Heath");
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, heath);
    REQUIRE(c.size() == 5);
    CHECK(c[0] == "Overgrown Tomb");
    CHECK(c == ActivationNames(s, heath.params));
    NarrowOnPlayFetch(s, 0, heath, c);
    CHECK(c == std::vector<std::string>{ "Overgrown Tomb" });
}

TEST_CASE("fetch ranking: a two-colour deck takes the shock even when the board already makes both colours")
{
    FetchPruneEnvScope full_candidates("0");
    // USER: "If there are 2 colours you literally can just always get the shock and then basics after that."
    // Forest + Mountain on the board, Birds of Paradise castable and already paid by the Forest: no land adds a pip
    // and nothing is lacked, so the shock (Stomping Ground) still comes first, then the basics -- Forest, the colour
    // the castable spell asks for, before Mountain.
    EnsureCardsFr();
    FetchPipsArm on(true);
    static const BoarsProvider bp;
    GameState s = BaseFr(bp, 3);
    PutFr(s, "Forest", 50);
    PutFr(s, "Mountain", 51);
    Hand(s, { "Birds of Paradise", "Wooded Foothills" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    std::vector<std::string> lib = { "Forest", "Mountain", "Stomping Ground" };
    do
    {
        SetLib(s, lib);
        std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        CHECK(c == std::vector<std::string>{ "Stomping Ground", "Forest", "Mountain" });
        CHECK(c == ActivationNames(s, foothills.params));
        NarrowOnPlayFetch(s, 0, foothills, c);
        CHECK(c == std::vector<std::string>{ "Stomping Ground" });
    } while (std::next_permutation(lib.begin(), lib.end()));
    // No shock left: the basics by the castable-pip rule.
    SetLib(s, { "Mountain", "Forest" });
    CHECK(OnPlayFetchCandidates(s, 0, foothills) == std::vector<std::string>{ "Forest", "Mountain" });
}

TEST_CASE("fetch ranking: no shock makes the colour the line needs -> the basic beats the shock")
{
    FetchPruneEnvScope full_candidates("0");
    // Forest, Forest, Zhur-Taa Druid: Herald of Ilharg {2}{R}{G} is castable at four mana and its R has no source. The
    // Temple Garden in the library (a legal Forest) is a shock, but it does not make red: Mountain first ("The only
    // exception is if we have no shocks in the colour our line needs"), then the shock, then Forest.
    EnsureCardsFr();
    FetchPipsArm on(true);
    static const BoarsProvider bp;
    GameState s = BaseFr(bp, 3);
    PutFr(s, "Forest", 50);
    PutFr(s, "Forest", 51);
    PutFr(s, "Zhur-Taa Druid", 52);
    Hand(s, { "Herald of Ilharg", "Wooded Foothills" });
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    std::vector<std::string> lib = { "Forest", "Mountain", "Temple Garden" };
    do
    {
        SetLib(s, lib);
        std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        CHECK(c == std::vector<std::string>{ "Mountain", "Temple Garden", "Forest" });
        CHECK(c == ActivationNames(s, foothills.params));
        NarrowOnPlayFetch(s, 0, foothills, c);
        CHECK(c == std::vector<std::string>{ "Mountain" });
    } while (std::next_permutation(lib.begin(), lib.end()));
}

TEST_CASE("fetch ranking: Cyclone / Landfall land-search doctrine never reads it")
{
    EnsureCardsFr();
    static const CycloneProvider cy;
    static const LandfallProvider lf;
    static const LandfallElementalsProvider lfe;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    for (const DecisionProvider* prov : { static_cast<const DecisionProvider*>(&cy),
                                          static_cast<const DecisionProvider*>(&lf),
                                          static_cast<const DecisionProvider*>(&lfe) })
    {
        GameState s = BaseFr(*prov, 3);
        PutFr(s, "Forest", 50);
        PutFr(s, "Mountain", 51);
        Hand(s, { "Birds of Paradise", "Wooded Foothills" });
        SetLib(s, { "Mountain", "Forest", "Stomping Ground" });
        std::vector<std::string> off_c, on_c;
        { FetchPipsArm off(false); off_c = ActivationNames(s, foothills.params); }
        { FetchPipsArm on(true);   on_c  = ActivationNames(s, foothills.params); }
        CHECK(off_c == on_c);
    }
}

// ---- MTG_FIVECOLOUR_TRIOME_BESIDE (USER 2026-11-17, "On Five Colour we could give it a try.") ------------------------
// RULE 6 ranks a tapped land (Jetmir's Garden / Zagoth Triome) first when no untapped land's mana is spendable from
// the current hand. Flag on: the best untapped pick rides beside it as a second searched target, on every route
// (the legacy Scalding Tarn, the battlefield Wooded Foothills' activation and its land drop). Off: top-1.
namespace
{
struct TriomeBesideArm
{
    std::int8_t prev;
    explicit TriomeBesideArm(bool on) : prev(heurarm::t_arm[heurarm::FIVECOLOUR_TRIOME_BESIDE])
    { heurarm::t_arm[heurarm::FIVECOLOUR_TRIOME_BESIDE] = on ? 1 : 0; }
    ~TriomeBesideArm() { heurarm::t_arm[heurarm::FIVECOLOUR_TRIOME_BESIDE] = prev; }
};
bool TappedFr(const std::string& n) { return DefFr(n).params.enters_tapped; }
std::vector<std::string> NarrowedFr(const GameState& s, const CardDefinition& fetch)
{
    std::vector<std::string> c = OnPlayFetchCandidates(s, 0, fetch);
    NarrowOnPlayFetch(s, 0, fetch, c);
    return c;
}
std::vector<std::string> ActivationFr(const GameState& s, const CardParams& pp)
{
    std::vector<std::string> out;
    for (const std::vector<std::string>& pick : LandSearchPicks(s, 0, LandSearchSpecOf(pp)))
    { if (!pick.empty()) { out.push_back(pick.front()); } }
    return out;
}
}  // namespace

TEST_CASE("Five Colour MTG_FIVECOLOUR_TRIOME_BESIDE: a tapped top pick searches the best untapped pick beside it")
{
    EnsureCardsFr();
    if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::Fetch)) { return; }   // process-static: every target
    static const FiveColourProvider fc;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    const CardDefinition& tarn      = DefFr("Scalding Tarn");
    const std::vector<std::string> lib{ "Mountain", "Stomping Ground", "Steam Vents", "Blood Crypt", "Jetmir's Garden",
                                        "Zagoth Triome", "Island", "Forest" };
    GameState idle = BaseFr(fc, 1);                        // T1, nothing out; Progenitus casts nothing -> idle mana
    Hand(idle, { "Wooded Foothills", "Progenitus" });
    SetLib(idle, lib);
    GameState busy = BaseFr(fc, 1);                        // Birds of Paradise: one {G} casts it -> untapped wins
    Hand(busy, { "Wooded Foothills", "Birds of Paradise" });
    SetLib(busy, lib);
    {
        TriomeBesideArm off(false);
        const std::vector<std::string> r = fc.FetchCandidates(idle, 0, foothills.params);
        REQUIRE(r.size() >= 2);
        CHECK(TappedFr(r.front()));                        // RULE 6: the triome wins when the mana is idle
        CHECK(NarrowedFr(idle, foothills).size() == 1);
        CHECK(NarrowedFr(idle, tarn).size() == 1);
        CHECK(fc.OnPlayFetchWidth(idle, foothills) == 1);
    }
    TriomeBesideArm on(true);
    const std::vector<std::string> r = fc.FetchCandidates(idle, 0, foothills.params);
    REQUIRE(r.size() >= 2);
    CHECK(TappedFr(r[0]));
    CHECK_FALSE(TappedFr(r[1]));                           // the best untapped pick moved up behind the triome
    const std::vector<std::string> pair{ r[0], r[1] };
    CHECK(fc.FetchSearchCandidates(idle, 0, foothills.params, r) == pair);
    CHECK(NarrowedFr(idle, foothills) == pair);            // battlefield model: the land drop
    CHECK(ActivationFr(idle, foothills.params) == pair);   // ... and its activation read one list
    const std::vector<std::string> t = NarrowedFr(idle, tarn);   // legacy model (Scalding Tarn)
    REQUIRE(t.size() == 2);
    CHECK(TappedFr(t[0]));
    CHECK_FALSE(TappedFr(t[1]));
    bool tapped = false, untapped = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(idle, true))
    {
        if (p.land_to_play != "Wooded Foothills") { continue; }
        tapped   = tapped   || p.fetch_target == pair[0];
        untapped = untapped || p.fetch_target == pair[1];
    }
    CHECK(tapped);
    CHECK(untapped);
    // The mana is needed (Birds): the untapped pick is first and alone, as before.
    const std::vector<std::string> b = NarrowedFr(busy, foothills);
    REQUIRE(b.size() == 1);
    CHECK_FALSE(TappedFr(b.front()));
    // Only tapped targets left: just the top one.
    GameState only = idle;
    SetLib(only, { "Jetmir's Garden", "Zagoth Triome" });
    CHECK(NarrowedFr(only, foothills).size() == 1);
}

// ---- MTG_FIVECOLOUR_RULE6_SEES_STEPS (default OFF, pending the USER) -------------------------------------------------
// RULE 6's "is the untapped land's mana spendable this turn" test, sharpened for the five blind spots the 2026-11-18
// TRIOME_BESIDE A/B found. Each hand below reads IDLE to the old test (a triome tops the ranking) and NEEDED to the
// sharpened one (an untapped land tops it). Width stays 1.
namespace
{
struct Rule6StepsArm
{
    std::int8_t prev;
    explicit Rule6StepsArm(bool on) : prev(heurarm::t_arm[heurarm::FIVECOLOUR_RULE6_SEES_STEPS])
    { heurarm::t_arm[heurarm::FIVECOLOUR_RULE6_SEES_STEPS] = on ? 1 : 0; }
    ~Rule6StepsArm() { heurarm::t_arm[heurarm::FIVECOLOUR_RULE6_SEES_STEPS] = prev; }
};
}  // namespace

TEST_CASE("Five Colour MTG_FIVECOLOUR_RULE6_SEES_STEPS: RULE 6 sees the steps that spend the untapped land's mana")
{
    EnsureCardsFr();
    if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::Fetch)) { return; }
    static const FiveColourProvider fc;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    const std::vector<std::string> lib{ "Mountain", "Stomping Ground", "Steam Vents", "Blood Crypt", "Jetmir's Garden",
                                        "Zagoth Triome", "Forest" };
    auto top_tapped = [&](const GameState& s, bool on)
    {
        Rule6StepsArm a(on);
        const std::vector<std::string> r = fc.FetchCandidates(s, 0, foothills.params);
        REQUIRE(!r.empty());
        return TappedFr(r.front());
    };
    auto board = [&](const std::vector<std::string>& perms, const std::vector<std::string>& hand)
    {
        GameState s = BaseFr(fc, 3);
        int n = 300;
        for (const std::string& nm : perms) { PutFr(s, nm, n++); }
        Hand(s, hand);
        SetLib(s, lib);
        return s;
    };
    SUBCASE("(1) Deathrite fuel: the cracked fetch feeds an unfuelled Deathrite")
    {
        // Forest + Deathrite (no land in the graveyard) + one more mana = Faeburrow Elder ({1}{G}{W}).
        const GameState s = board({ "Forest", "Deathrite Shaman" }, { "Wooded Foothills", "Faeburrow Elder" });
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
    }
    SUBCASE("(2) colour-exact: Steam Vents + Blood Crypt make no green for Bloom Tender")
    {
        // The flat pool books both shocks WILD, so Bloom Tender ({1}{G}) looked castable already.
        const GameState s = board({ "Steam Vents", "Blood Crypt" }, { "Wooded Foothills", "Bloom Tender" });
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
    }
    SUBCASE("(3) domain growth: Faeburrow first adds white to Bloom Tender, which then pays a second Tender")
    {
        // Bloom Tender (domain {G}) + Forest + Godless Shrine. One more mana pays Faeburrow from the lands alone, so
        // the Tender taps for {G}{W} afterwards and casts the second Tender (5 mana value instead of 3).
        GameState s = board({ "Bloom Tender", "Forest", "Godless Shrine" },
                            { "Wooded Foothills", "Faeburrow Elder", "Bloom Tender" });
        SetLib(s, { "Mountain", "Stomping Ground", "Blood Crypt", "Jetmir's Garden", "Zagoth Triome", "Forest" });
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
    }
    SUBCASE("(4) Lightning Greaves: a Faeburrow cast this turn taps the same turn")
    {
        // Domain WUBRG (Cosmic Spider-Man), Forest + Plains + Mountain. One more mana: Faeburrow, hasted, taps for
        // five and pays Two-Headed Hellkite. Without Greaves the same hand stays idle.
        const std::vector<std::string> hand{ "Wooded Foothills", "Faeburrow Elder", "Two-Headed Hellkite" };
        const GameState s = board({ "Lightning Greaves", "Cosmic Spider-Man", "Forest", "Plains", "Mountain" }, hand);
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
        const GameState no_greaves = board({ "Cosmic Spider-Man", "Forest", "Plains", "Mountain" }, hand);
        CHECK(top_tapped(no_greaves, true));
    }
    SUBCASE("(4b) Greaves, std gi 72: a permanent bringing a new colour need not be paid before a hasted dork")
    {
        // Breeding Pool + Birds + Blood Crypt + Greaves; Faeburrow Elder and Mana Cannons in hand. One more mana pays
        // Faeburrow, which (hasted) taps for {G}{W} and pays Mana Cannons. Cannons is red (a new colour), but no
        // domain source is waiting on it, so it is not "early" -- the haste-only credit variant applies.
        const GameState s = board({ "Breeding Pool", "Birds of Paradise", "Blood Crypt", "Lightning Greaves" },
                                  { "Wooded Foothills", "Faeburrow Elder", "Mana Cannons" });
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
    }
    SUBCASE("(5) best total mana value: Mana Cannons replaces Lightning Greaves")
    {
        // Four lands: Greaves + Bloom Tender (4). One more: Tender + Mana Cannons (5) -- two cards either way, so the
        // cheapest-first count saw nothing.
        const GameState s = board({ "Forest", "Forest", "Mountain", "Mountain" },
                                  { "Wooded Foothills", "Lightning Greaves", "Bloom Tender", "Mana Cannons" });
        CHECK(top_tapped(s, false));
        CHECK_FALSE(top_tapped(s, true));
    }
    SUBCASE("idle stays idle: nothing in hand a fifth mana can add to")
    {
        const GameState s = board({ "Forest", "Forest", "Mountain", "Mountain" }, { "Wooded Foothills", "Progenitus" });
        CHECK(top_tapped(s, false));
        CHECK(top_tapped(s, true));
    }
}
