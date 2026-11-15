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
    CHECK(bp.FetchSearchCandidates(s, 0, foothills, ranked) == ranked);
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

TEST_CASE("fetch ranking: the 97005 T4 board fetches the next Stomping Ground (a SECOND red source for Ilharg)")
{
    EnsureCardsFr();
    static const BoarsProvider bp;
    const CardDefinition& foothills = DefFr("Wooded Foothills");
    GameState s = Boars97005T4(bp);
    {
        FetchPipsArm off(false);
        const std::vector<std::string> c = OnPlayFetchCandidates(s, 0, foothills);
        REQUIRE(c.size() == 3);
        CHECK(c[0] == "Forest");   // the defect: every target ties, name order
    }
    {
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

TEST_CASE("fetch ranking: the plain rule's decisions stand (gi153 Stomping Ground, Creature Giving)")
{
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
