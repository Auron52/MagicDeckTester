// Unit tests for two closed land-search gaps (docs/design/land-search-cluster.md, 2026-10-15):
//
//  GAP 2 -- SEARCH FOR TOMORROW HARD-CAST MANA. Its basic enters UNTAPPED, so a later cast of the same
//  main phase may spend it. The cast now carries a LAND-PUT CREDIT on Action::rock_mana, emitted once
//  per credit class of its legal picks, and its resolution is RESTRICTED to the picks earning that
//  credit (core/LandSearch.h LandSearchCreditGroups / LandSearchCreditFilter) -- in the rollout through
//  apply_one's land_put cast rider, in the executor through StackEntry::land_put_credit. Pinned:
//   * the credit is the pick's own colour, split per class, front class first;
//   * "Search for Tomorrow, then Rampant Growth off the fetched Forest" is ENUMERATED (4 mana of board
//     for 5 of cost), and the variant credits {G} even though the provider's FRONT pick is a Mountain;
//   * the funding order casts Search for Tomorrow first, and the rollout and the executor realise the
//     same line (Forest fetched UNTAPPED, Rampant Growth paid with it);
//   * no basic left -> no credit, and the line is not offered.
//
//  GAP 3 -- A PUT KAROO'S BOUNCE IS SEARCHED. Gruul Turf put by Primeval Titan (ETB and attack) or by
//  Genesis Wave returned BounceSearchCandidates' front (an in-window greedy pick). Pinned:
//   * the extended pick list (LandSearchPinDecode): pick values keep their meaning, the Karoo picks'
//     bounce sub-choices follow;
//   * each of the three put routes reaches BOTH contested candidates through its pin;
//   * the enumeration fans the sub-choices (Titan ETB, Titan attack, Genesis Wave) and applying the
//     variants returns both lands;
//   * a human playing the put is asked through the ordinary `bounce` chooser.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ContPinScope.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{

void LoadCardsGaps()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DG(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutG(GameState& s, const std::string& name, int number, bool tapped, bool sick = false)
{
    Permanent p;
    p.card              = DG(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void LibG(GameState& s, const std::string& name, int number)
{
    Card c = DG(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

void HandG(GameState& s, const std::string& name, int number)
{
    Card c = DG(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshG(int turn = 4)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int CountBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool InHandG(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

// The plan's CastFromHand action of `name`, or nullptr.
const Action* CastOf(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

// ---- GAP 2 board ---------------------------------------------------------------------------------
// Turn 4: Forest + two Radiant Fountains + Haunted Fengraf untapped ({G} + three {C}); hand Search for
// Tomorrow {2}{G}, Rampant Growth {1}{G} and Phylath {4}{R}{G}. Five mana of cost on four lands, and
// the SECOND {G} can only be the basic Search for Tomorrow fetches. Phylath's {R} with no red source
// makes the provider's FRONT pick a Mountain -- so the {G} line exists only as the Forest variant.
GameState SftBoard(bool with_basics)
{
    GameState s = FreshG();
    PutG(s, "Forest", 1, false);
    PutG(s, "Radiant Fountain", 2, false);
    PutG(s, "Radiant Fountain", 3, false);
    PutG(s, "Haunted Fengraf", 4, false);
    HandG(s, "Search for Tomorrow", 20);
    HandG(s, "Rampant Growth", 21);
    HandG(s, "Phylath, World Sculptor", 22);
    if (with_basics)
    {
        LibG(s, "Mountain", 50); LibG(s, "Mountain", 51);
        LibG(s, "Forest", 52);   LibG(s, "Forest", 53);
    }
    else
    {
        LibG(s, "Gruul Turf", 50); LibG(s, "Radiant Fountain", 51);
    }
    for (int i = 0; i < 12; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    s.players[0].lands_played_this_turn = 1;   // the drop is spent: the casts are the whole decision
    return s;
}

const TurnSolver::Plan* FindSftAndRampant(const std::vector<TurnSolver::Plan>& plans, int want_green)
{
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr || CastOf(p, "Rampant Growth") == nullptr) { continue; }
        if (want_green < 0 || sft->rock_mana.green == want_green) { return &p; }
    }
    return nullptr;
}

}   // namespace

TEST_CASE("Land-put credit: one credit per pick CLASS, front class first; basics only")
{
    LoadCardsGaps();
    const LandSearchSpec sft = LandSearchSpecOf(DG("Search for Tomorrow").params);
    CHECK(LandSearchPutsUntapped(DG("Search for Tomorrow")));
    CHECK_FALSE(LandSearchPutsUntapped(DG("Rampant Growth")));     // tapped: no same-turn mana
    CHECK_FALSE(LandSearchPutsUntapped(DG("Cultivate")));          // tapped (and one to hand)
    CHECK_FALSE(LandSearchPutsUntapped(DG("Primeval Titan")));     // a creature's ETB, tapped
    {
        GameState s = SftBoard(true);
        // Phylath's {R} with no red source: the provider's front pick is the Mountain.
        const std::vector<std::vector<std::string>> picks = LandSearchPicks(s, 0, sft);
        REQUIRE(picks.size() == 2);
        CHECK(picks.front() == std::vector<std::string>{ "Mountain" });
        const std::vector<ManaPool> g = LandSearchCreditGroups(s, 0, sft);
        REQUIRE(g.size() == 2);
        CHECK(g[0].red == 1);
        CHECK(g[0].Total() == 1);
        CHECK(g[1].green == 1);
        CHECK(g[1].Total() == 1);
    }
    {
        // No basic in the library: nothing creditable (fail toward NOT crediting).
        GameState s = SftBoard(false);
        CHECK(LandSearchCreditGroups(s, 0, sft).empty());
    }
}

TEST_CASE("Land-put credit: Search for Tomorrow then Rampant Growth off the fetched Forest is enumerated, hoisted, and realised identically by the rollout and the executor")
{
    LoadCardsGaps();
    const GameState s = SftBoard(true);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both = FindSftAndRampant(plans, /*want_green=*/1);
    REQUIRE_MESSAGE(both != nullptr, "Search for Tomorrow + Rampant Growth (Forest credit) was not enumerated");
    // The {R} class cannot fund Rampant Growth's {G}: never offered with it (no phantom colour).
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr || CastOf(p, "Rampant Growth") == nullptr) { continue; }
        CHECK(sft->rock_mana.green == 1);
        CHECK(sft->rock_mana.red == 0);
    }
    // Search for Tomorrow ALONE is still offered in both classes (the classes partition the picks).
    std::set<int> alone_reds;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft != nullptr) { alone_reds.insert(sft->rock_mana.red); }
    }
    CHECK(alone_reds == std::set<int>{ 0, 1 });

    // The funding order both apply worlds run: Search for Tomorrow first, whatever order the plan holds.
    int sft_i = -1, rg_i = -1;
    for (int i = 0; i < static_cast<int>(both->actions.size()); ++i)
    {
        if (both->actions[static_cast<std::size_t>(i)].card_name.str() == "Search for Tomorrow") { sft_i = i; }
        if (both->actions[static_cast<std::size_t>(i)].card_name.str() == "Rampant Growth")      { rg_i  = i; }
    }
    REQUIRE(sft_i >= 0);
    REQUIRE(rg_i >= 0);
    std::vector<int> order{ rg_i, sft_i };
    ApplyPayableCastOrder(s, both->actions, order);
    CHECK(order == std::vector<int>{ sft_i, rg_i });

    // ROLLOUT: the plan applies in full -- the Forest (not the provider's front Mountain) arrives
    // untapped and pays Rampant Growth, whose own basic enters tapped.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, /*is_pre_combat=*/true);
    CHECK_FALSE(InHandG(roll, "Search for Tomorrow"));
    CHECK_FALSE(InHandG(roll, "Rampant Growth"));
    CHECK(CountBf(roll, "Forest") >= 2);

    // EXECUTOR: the same two casts through the real cast path (pay, stack entry, EffectHandler), in
    // the funding order, the credit armed exactly as the plan-execution sites arm it.
    GameState ex = s;
    AIEngine eng;
    for (int idx : order)
    {
        const Action& a = both->actions[static_cast<std::size_t>(idx)];
        auto it = std::find_if(ex.players[0].hand.begin(), ex.players[0].hand.end(),
                               [&](const Card& c) { return c.m_name == a.card_name; });
        REQUIRE(it != ex.players[0].hand.end());
        MtgTestSeam::SetPendingLandPut(eng, a.rock_mana);
        ManaPool avail = AvailableManaPool(ex);
        const std::size_t before = ex.stack.size();
        MtgTestSeam::CastSpellFromHand(eng, ex, *it, avail, 0);
        REQUIRE_MESSAGE(ex.stack.size() == before + 1, "executor could not cast ", a.card_name.str());
        const StackEntry entry = ex.stack.back();
        ex.stack.pop_back();
        if (a.card_name.str() == "Search for Tomorrow")
        {
            CHECK(entry.land_put_credit.green == 1);
            CHECK(entry.land_put_credit.Total() == 1);
        }
        else { CHECK(entry.land_put_credit.Total() == 0); }
        EffectHandler::Resolve(ex, entry, DG(a.card_name.str()));
        if (a.card_name.str() == "Search for Tomorrow")
        {
            // The restricted pick: an UNTAPPED Forest, not the front-pick Mountain.
            int untapped_forests = 0;
            for (const Permanent& p : ex.battlefield)
            { if (p.card.m_name.str() == "Forest" && !p.tapped) { ++untapped_forests; } }
            CHECK(untapped_forests >= 1);
            CHECK(CountBf(ex, "Mountain") == 0);
        }
    }
    // Both worlds end on the same zones (names; which source tapped for what may differ).
    auto names = [](const std::vector<Card>& z)
    { std::multiset<std::string> m; for (const Card& c : z) { m.insert(c.m_name.str()); } return m; };
    auto bf_names = [](const GameState& g)
    { std::multiset<std::string> m; for (const Permanent& p : g.battlefield) { m.insert(p.card.m_name.str()); } return m; };
    CHECK(bf_names(roll) == bf_names(ex));
    CHECK(names(roll.players[0].hand) == names(ex.players[0].hand));
    CHECK(names(roll.players[0].graveyard) == names(ex.players[0].graveyard));
    CHECK(roll.players[0].library.size() == ex.players[0].library.size());
}

TEST_CASE("Land-put credit: no basic left -> no credit, and the line is not offered")
{
    LoadCardsGaps();
    const GameState s = SftBoard(false);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(FindSftAndRampant(plans, -1) == nullptr);
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft != nullptr) { CHECK(sft->rock_mana.Total() == 0); }
    }
}

TEST_CASE("Land-put credit: a one-class library credits one action, and the resolution is unrestricted by it")
{
    LoadCardsGaps();
    GameState s = SftBoard(true);
    // Forests only.
    s.players[0].library = Library{};
    LibG(s, "Forest", 52); LibG(s, "Forest", 53);
    for (int i = 0; i < 8; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    std::set<int> greens, reds;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr) { continue; }
        greens.insert(sft->rock_mana.green);
        reds.insert(sft->rock_mana.red);
    }
    CHECK(greens == std::set<int>{ 1 });
    CHECK(reds == std::set<int>{ 0 });
    CHECK(FindSftAndRampant(plans, 1) != nullptr);
}

// ---- TWO credited Search for Tomorrow casts, then Myriad Landscape's {2},{T} activation ----------
// USER, Landfall seed 6 gi5 T6 (references/Landfall/claude_s6_gi5.json, decision 27): "trying to cast
// Search Search Myriad activation fails". The board: Forest x3, Mountain x2, Gruul Turf ({R}{G}),
// Myriad Landscape, Courser of Kruphix and Primeval Titan; hand Evolving Wilds + Search for Tomorrow x2,
// the land drop unspent. No Mountain is left in the library, so both searches fetch a Forest.
// Excluding Myriad (its {T} is part of its own cost) the board makes 7 mana; each Search for Tomorrow
// costs 3 and puts an UNTAPPED Forest (+1), and the activation costs {2}: 3 + 3 + 2 = 8 <= 7 + 2. The
// line is legal only with the searches' land-put credits funding the activation.
//
// This pins the AUTONOMOUS half: the search enumerates the line and both apply worlds realise it (it
// held before the fix too -- the search was never missing it). The defect was HUMAN-PLAY only: the
// LAND-PUT CREDIT post-pass skipped human play, so the viewer's menu never held the line. That half is
// pinned by test/scenarios/landsearch_sft_sft_myriad_{validate,human_line}.json, because
// HumanPlayActive() is a process-static MTG_HUMAN_PLAY read that a unit test cannot switch on.
namespace
{
GameState SftMyriadBoard()
{
    GameState s = FreshG(6);
    PutG(s, "Myriad Landscape", 37, false);
    PutG(s, "Forest", 12, false);
    PutG(s, "Mountain", 33, false);
    PutG(s, "Mountain", 34, false);
    PutG(s, "Forest", 21, false);
    PutG(s, "Courser of Kruphix", 5, false);
    PutG(s, "Forest", 16, false);
    PutG(s, "Primeval Titan", 44, false);
    PutG(s, "Gruul Turf", 27, false);
    HandG(s, "Evolving Wilds", 10);
    HandG(s, "Search for Tomorrow", 55);
    HandG(s, "Search for Tomorrow", 58);
    LibG(s, "Cultivate", 7);   // the top (Courser reveals it; not a land)
    for (int i = 0; i < 10; ++i) { LibG(s, "Forest", 80 + i); }
    for (int i = 0; i < 20; ++i) { LibG(s, "Avenger of Zendikar", 100 + i); }
    return s;
}
// The plan "land=none; Search for Tomorrow, Search for Tomorrow (both HARD-cast), Myriad Landscape's
// sacrifice-search" -- or nullptr.
const TurnSolver::Plan* FindSftSftMyriad(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_decided && !p.land_to_play.empty()) { continue; }
        int sft = 0, myriad = 0, other = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.alt_cost
                && a.card_name.str() == "Search for Tomorrow") { ++sft; }
            else if (TurnSolver::IsLandSearchActivation(a) && a.card_name.str() == "Myriad Landscape") { ++myriad; }
            else { ++other; }
        }
        if (sft == 2 && myriad == 1 && other == 0) { return &p; }
    }
    return nullptr;
}
}   // namespace

TEST_CASE("Land-put credit: Search for Tomorrow x2 then Myriad Landscape's activation is enumerated and realised (Landfall s6 T6)")
{
    LoadCardsGaps();
    const GameState s = SftMyriadBoard();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = FindSftSftMyriad(plans);
    REQUIRE_MESSAGE(line != nullptr, "Search for Tomorrow, Search for Tomorrow, Myriad Landscape was not enumerated");
    for (const Action& a : line->actions)
    {
        if (a.card_name.str() != "Search for Tomorrow") { continue; }
        CHECK(a.rock_mana.green == 1);       // each cast carries its own Forest's credit
        CHECK(a.rock_mana.Total() == 1);
    }

    // ROLLOUT and EXECUTOR both realise the whole line: two Forests from the casts, two from Myriad.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *line, /*is_pre_combat=*/true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *line, true);
    GameState exec = s;
    // GameEngine::MainPhase's shape: each cast resolves before the next (the land it puts is mana).
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
    eng.TakeTurn(exec, true, resolver);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK_FALSE(InHandG(*g, "Search for Tomorrow"));
        CHECK(g->stack.empty());
        CHECK(CountBf(*g, "Myriad Landscape") == 0);           // sacrificed by its own activation
        CHECK(CountBf(*g, "Forest") == 3 + 2 + 2);
        int gy_sft = 0, gy_myriad = 0;
        for (const Card& c : g->players[0].graveyard)
        {
            if (c.m_name.str() == "Search for Tomorrow") { ++gy_sft; }
            if (c.m_name.str() == "Myriad Landscape")    { ++gy_myriad; }
        }
        CHECK(gy_sft == 2);
        CHECK(gy_myriad == 1);
    }
}

// ---- GAP 3 ---------------------------------------------------------------------------------------
namespace
{
// Board for a put Gruul Turf with TWO contested candidates: a tapped Forest and a tapped Mountain both
// re-enter untapped, the Turf covers both colours (so the USER rule sees two names and leaves it to the
// search), and a land in hand keeps the tapped lands from folding into one replay class.
GameState TurfBoard()
{
    GameState s = FreshG(6);
    PutG(s, "Forest", 9, /*tapped=*/true);
    PutG(s, "Mountain", 12, /*tapped=*/true);
    HandG(s, "Forest", 30);
    return s;
}
std::string Returned(const GameState& before, const GameState& after)
{
    // The land the bounce sent to hand: the one hand card whose per-copy number was not in hand before.
    std::set<int> was;
    for (const Card& c : before.players[0].hand) { was.insert(c.m_number); }
    std::vector<std::string> fresh;
    for (const Card& c : after.players[0].hand)
    { if (was.count(c.m_number) == 0 && c.IsLand()) { fresh.push_back(c.m_name.str()); } }
    if (fresh.empty())
    {
        // A hand card's mask may be a placeholder: fall back to the definition.
        for (const Card& c : after.players[0].hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (was.count(c.m_number) == 0 && d != nullptr && d->card.IsLand()) { fresh.push_back(c.m_name.str()); }
        }
    }
    return fresh.size() == 1 ? fresh.front() : std::string("?");
}
}   // namespace

TEST_CASE("Put Karoo bounce: the extended pick list keeps every pick value and appends the Karoo picks' sub-choices")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51); LibG(s, "Forest", 52);
    const LandSearchSpec titan = LandSearchSpecOf(DG("Primeval Titan").params);
    const landsearchmemo::Slot& ms0 = LandSearchMemoSlot(s, 0, titan);
    const std::vector<std::vector<std::string>> picks = ms0.picks;
    const std::vector<int> karoo = ms0.karoo_picks;
    const int w = static_cast<int>(picks.size());
    REQUIRE(w == 3);                    // {Forest, Gruul Turf}, {Forest, Mountain}, {Gruul Turf, Mountain}
    REQUIRE(karoo.size() == 2);
    const landsearchmemo::Slot& ms = LandSearchMemoSlot(s, 0, titan);
    CHECK(LandSearchExtendedWidth(ms) == w + 2 * kLandSearchBounceStride);
    for (int v = 0; v < w; ++v)
    {
        const LandSearchPinPick pp = LandSearchPinDecode(ms, v);
        CHECK(pp.pick == v);
        CHECK(pp.bounce == -1);
    }
    for (int r = 0; r < 2; ++r)
    {
        for (int b = 1; b <= kLandSearchBounceStride; ++b)
        {
            const LandSearchPinPick pp = LandSearchPinDecode(ms, LandSearchBouncePinValue(w, r, b));
            CHECK(pp.pick == karoo[static_cast<std::size_t>(r)]);
            CHECK(pp.bounce == b);
        }
    }
    // Past the end clamps onto the last entry; -1 is the front.
    CHECK(LandSearchPinDecode(ms, 999).pick == karoo.back());
    CHECK(LandSearchPinDecode(ms, -1).pick == 0);
    // A list with no Karoo IS the plain list (byte-identical decoding).
    GameState t = TurfBoard();
    LibG(t, "Mountain", 51); LibG(t, "Forest", 52);
    const landsearchmemo::Slot& mt = LandSearchMemoSlot(t, 0, titan);
    CHECK(mt.karoo_picks.empty());
    CHECK(LandSearchExtendedWidth(mt) == static_cast<int>(mt.picks.size()));
    CHECK(LandSearchPinDecode(mt, 999).pick == static_cast<int>(mt.picks.size()) - 1);
}

TEST_CASE("Put Karoo bounce: Primeval Titan's ETB put reaches BOTH contested lands through its pin")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);   // the one pick: {Gruul Turf, Mountain}
    const LandSearchSpec titan = LandSearchSpecOf(DG("Primeval Titan").params);
    const int w = static_cast<int>(LandSearchMemoSlot(s, 0, titan).picks.size());
    REQUIRE(w == 1);
    std::set<std::string> returned;
    for (int pin : { -1, 0, LandSearchBouncePinValue(w, 0, 1) })
    {
        GameState t = s;
        PerformLandSearch(t, 0, titan, "Primeval Titan", pin);
        CHECK(CountBf(t, "Gruul Turf") == 1);
        returned.insert(Returned(s, t));
        CHECK(g_scripted_bounce_choice == -1);
    }
    CHECK(returned == std::set<std::string>{ "Forest", "Mountain" });
}

TEST_CASE("Put Karoo bounce: Primeval Titan's ATTACK trigger reaches both lands through the packed pin")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    const int t = PutG(s, "Primeval Titan", 10, false);
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);
    const int w = static_cast<int>(LandSearchMemoSlot(s, 0, LandSearchSpecOf(DG("Primeval Titan").params)).picks.size());
    std::set<std::string> returned;
    for (int packed : { -1, LandSearchBouncePinValue(w, 0, 1) })
    {
        GameState g = s;
        g.scripted_attack_land_search = packed;
        std::vector<int> atk{ t };
        FireAttackLandSearch(g, 0, atk);
        REQUIRE(atk.size() == 1);
        CHECK(g.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == 10);   // still the Titan
        returned.insert(Returned(s, g));
    }
    CHECK(returned == std::set<std::string>{ "Forest", "Mountain" });
}

TEST_CASE("Put-bounce fold: the probe records the attack ordinal's pick, bounce width and index -- on its bound state only")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    const int t = PutG(s, "Primeval Titan", 10, false);
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);   // the one pick: {Gruul Turf, Mountain}
    const LandSearchSpec titan = LandSearchSpecOf(DG("Primeval Titan").params);
    const int w = static_cast<int>(LandSearchMemoSlot(s, 0, titan).picks.size());
    REQUIRE(w == 1);
    // The front (no pin), and both bounce sub-choices: the narrowed width at the bounce is 2 (the tapped
    // Forest, the tapped Mountains folded), so b = 2 CLAMPS onto index 1 -- the duplicate the fold drops.
    const std::pair<int, int> cases[] = { { -1, 0 }, { LandSearchBouncePinValue(w, 0, 1), 1 },
                                          { LandSearchBouncePinValue(w, 0, 2), 1 } };
    for (const auto& c : cases)
    {
        GameState g = s;
        g.scripted_attack_land_search = c.first;
        PutBounceProbe probe;
        probe.target = &g;
        g_put_bounce_probe = &probe;
        std::vector<int> atk{ t };
        FireAttackLandSearch(g, 0, atk);
        g_put_bounce_probe = nullptr;
        REQUIRE(probe.obs.size() == 1);
        const PutBounceObs& o = probe.obs[0];
        CHECK(o.ordinal == 0);
        CHECK(o.w == w);
        CHECK(o.karoo == std::vector<int>{ 0 });
        CHECK(o.pick == 0);
        CHECK(o.width == 2);
        CHECK(o.index == c.second);
        CHECK(probe.ordinal == -1);
        CHECK_FALSE(g_put_bounce_live);
    }
    // A probe bound to ANOTHER state object -- a projection's copy, a nested search -- records nothing.
    {
        GameState g = s;
        PutBounceProbe other;
        other.target = &s;
        g_put_bounce_probe = &other;
        std::vector<int> atk{ t };
        FireAttackLandSearch(g, 0, atk);
        g_put_bounce_probe = nullptr;
        CHECK(other.obs.empty());
    }
    // ...and neither does a land search outside the attack trigger, even on the bound state (the ETB).
    {
        GameState e = s;
        PutBounceProbe etb;
        etb.target = &e;
        g_put_bounce_probe = &etb;
        PerformLandSearch(e, 0, titan, "Primeval Titan", -1);
        g_put_bounce_probe = nullptr;
        CHECK(etb.obs.empty());
        CHECK(CountBf(e, "Gruul Turf") == 1);
    }
}

TEST_CASE("Put Karoo bounce: a Genesis Wave's put Turf reaches both lands through the plan's put pin")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    LibG(s, "Gruul Turf", 50); LibG(s, "Forest", 51);
    for (int i = 0; i < 6; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    std::set<std::string> returned;
    for (int pin : { -1, 1 })
    {
        GameState g = s;
        {
            ScriptedPutBounce put(pin);
            PerformGenesisWave(g, 0, /*x=*/2, "Genesis Wave");
        }
        CHECK(CountBf(g, "Gruul Turf") == 1);
        returned.insert(Returned(s, g));
    }
    CHECK(returned == std::set<std::string>{ "Forest", "Mountain" });
    // A plan whose own drop is the Karoo keeps the pin for the drop.
    CHECK(PutBouncePinOf(1, true, "Gruul Turf") == -1);
    CHECK(PutBouncePinOf(1, true, "Forest") == 1);
    CHECK(PutBouncePinOf(-1, false, "") == -1);
}

TEST_CASE("Put Karoo bounce: the enumeration fans the ETB sub-choice of a cast Primeval Titan, and the variants return both lands")
{
    LoadCardsGaps();
    GameState s = FreshG(6);
    for (int n = 1; n <= 5; ++n) { PutG(s, "Forest", n, false); }
    PutG(s, "Mountain", 6, false);
    HandG(s, "Primeval Titan", 20);
    HandG(s, "Forest", 30);
    HandG(s, "Forest", 31);   // one stays in hand after the drop: the bounce is contested
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);
    for (int i = 0; i < 8; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const int w = static_cast<int>(LandSearchMemoSlot(s, 0, LandSearchSpecOf(DG("Primeval Titan").params)).picks.size());
    REQUIRE(w == 1);
    const int sub = LandSearchBouncePinValue(w, 0, 1);
    // A base plan casting the Titan (front bounce) and its sub-choice twin, same land drop.
    const TurnSolver::Plan* base = nullptr;
    const TurnSolver::Plan* twin = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (CastOf(p, "Primeval Titan") == nullptr || p.actions.size() != 1) { continue; }
        if (p.land_to_play != "Forest" || p.land_slot != 0) { continue; }
        if (p.land_search_pins.empty() && base == nullptr) { base = &p; }
        if (p.land_search_pins == std::vector<int>{ sub } && twin == nullptr) { twin = &p; }
    }
    REQUIRE_MESSAGE(base != nullptr, "no base Titan plan");
    REQUIRE_MESSAGE(twin != nullptr, "the Titan's put-Turf bounce sub-choice was not fanned");
    GameState a = s, b = s;
    TurnSolver::ApplyPlan(a, *base, true);
    TurnSolver::ApplyPlan(b, *twin, true);
    std::set<std::string> returned{ Returned(s, a), Returned(s, b) };
    // Both plans played one hand Forest; the bounce added one land back.
    CHECK(returned.size() == 2);
}

TEST_CASE("Put Karoo bounce: the attack pin fan carries the attacking Titan's sub-choices")
{
    LoadCardsGaps();
    GameState s = FreshG(7);
    for (int n = 1; n <= 4; ++n) { PutG(s, "Forest", n, false); }
    PutG(s, "Mountain", 6, false);
    PutG(s, "Primeval Titan", 10, false);   // on the battlefield since last turn: attacks
    HandG(s, "Forest", 30);
    HandG(s, "Forest", 31);
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);
    for (int i = 0; i < 8; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const int w = static_cast<int>(LandSearchMemoSlot(s, 0, LandSearchSpecOf(DG("Primeval Titan").params)).picks.size());
    const int sub = LandSearchBouncePinValue(w, 0, 1);
    bool found = false;
    for (const TurnSolver::Plan& p : plans)
    { if (p.attack_land_search_choice == sub) { found = true; break; } }
    CHECK_MESSAGE(found, "the attacking Titan's put-Turf bounce sub-choice was not fanned");
}

TEST_CASE("Put Karoo bounce: the enumeration fans a Genesis Wave's put Turf (Plan::bounce_choice), and the variants return both lands")
{
    LoadCardsGaps();
    GameState s = FreshG(7);
    for (int n = 1; n <= 5; ++n) { PutG(s, "Forest", n, false); }
    PutG(s, "Mountain", 6, false);
    HandG(s, "Genesis Wave", 20);
    HandG(s, "Forest", 30);
    HandG(s, "Forest", 31);
    LibG(s, "Gruul Turf", 50); LibG(s, "Forest", 51); LibG(s, "Avenger of Zendikar", 52);
    for (int i = 0; i < 8; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* base = nullptr;
    const TurnSolver::Plan* twin = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* wv = CastOf(p, "Genesis Wave");
        if (wv == nullptr || p.actions.size() != 1 || p.land_to_play != "Forest") { continue; }
        if (p.bounce_choice < 0 && base == nullptr) { base = &p; }
        if (p.bounce_choice == 1 && twin == nullptr) { twin = &p; }
    }
    REQUIRE_MESSAGE(base != nullptr, "no base Genesis Wave plan");
    REQUIRE_MESSAGE(twin != nullptr, "the Wave's put-Turf bounce was not fanned");
    REQUIRE(base->actions.front().chosen_x == twin->actions.front().chosen_x);
    GameState a = s, b = s;
    TurnSolver::ApplyPlan(a, *base, true);
    TurnSolver::ApplyPlan(b, *twin, true);
    CHECK(Returned(s, a) != Returned(s, b));
}

TEST_CASE("Put Karoo bounce: a human playing the put is asked through the ordinary bounce chooser")
{
    LoadCardsGaps();
    GameState s = TurfBoard();
    LibG(s, "Gruul Turf", 50); LibG(s, "Mountain", 51);
    int bounce_calls = 0;
    std::string bounce_source;
    BounceChooser bc = [&](const GameState&, int, const std::string& source, const std::vector<int>& legal, int)
    {
        ++bounce_calls;
        bounce_source = source;
        return static_cast<int>(legal.size()) - 1;
    };
    // The human's land prompts take the defaults (the Turf + Mountain pick is the only one).
    TutorChooser tc = [&](const GameState&, int, const std::string&, const std::vector<std::string>&, int def, const std::vector<int>&)
    { return def; };
    BounceChooser* sv_b = g_play_bounce_chooser;
    TutorChooser*  sv_t = g_play_tutor_chooser;
    g_play_bounce_chooser = &bc;
    g_play_tutor_chooser  = &tc;
    GameState t = s;
    PerformLandSearch(t, 0, LandSearchSpecOf(DG("Primeval Titan").params), "Primeval Titan", -1);
    g_play_bounce_chooser = sv_b;
    g_play_tutor_chooser  = sv_t;
    CHECK(bounce_calls == 1);
    CHECK(bounce_source == "Gruul Turf");
    CHECK(CountBf(t, "Gruul Turf") + (InHandG(t, "Gruul Turf") ? 1 : 0) == 1);
}

// ---- THE IDLE PASS's combat pins (2026-10-17; TurnSolver.cpp AppendIdlePassAxes) ----------------
// A pre-combat main with the drop spent and nothing to cast enumerates ZERO plans and the host appends
// the pass after the post-dedup fan-out ran -- so the attacking Titan's pick and the in-combat Khalni
// crack took their front (no crack at all). The pass now goes through the fan-out itself. End-to-end:
// test/scenarios/landsearch_idle_pass_khalni_cracked_in_combat.json.
namespace
{
GameState IdleTitanBoard()
{
    GameState s = FreshG(6);
    s.players[0].lands_played_this_turn = 1;
    for (int n = 1; n <= 6; ++n) { PutG(s, "Forest", n, false); }
    PutG(s, "Primeval Titan", 10, false);
    const int kx = PutG(s, "Khalni Heart Expedition", 11, false);
    s.battlefield[static_cast<std::size_t>(kx)].quest_counters = 1;
    LibG(s, "Radiant Fountain", 50); LibG(s, "Haunted Fengraf", 51);
    for (int i = 0; i < 4; ++i) { LibG(s, "Forest", 52 + i); }
    LibG(s, "Mountain", 56);
    return s;
}
bool HasLs(const std::vector<TurnSolver::Plan>& v, int attack, int crack)
{
    for (std::size_t i = 1; i < v.size(); ++i)
    {
        if (v[i].attack_land_search_choice == attack && v[i].combat_crack_choice == crack) { return true; }
    }
    return false;
}
}   // namespace

TEST_CASE("Idle pass: an empty main 1 still searches the attacking Titan's pick and the in-combat crack")
{
    LoadCardsGaps();
    const GameState s = IdleTitanBoard();
    REQUIRE(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true).empty());
    const std::vector<TurnSolver::Plan> fresh = TurnSolver::IdlePassWithAxes(s);
    REQUIRE_FALSE(fresh.empty());
    CHECK(fresh[0].attack_land_search_choice == -1);
    CHECK(fresh[0].combat_crack_choice == -1);
    CHECK(HasLs(fresh, -1, 1));                       // crack the Expedition in combat
    CHECK_FALSE(HasLs(fresh, -1, 0));                 // "no crack" is the pass itself
    CHECK_FALSE(HasLs(fresh, 0, -1));                 // ...and so is the all-front pick
}

TEST_CASE("Idle pass: an inherited crack / attack pick makes the front a variant")
{
    LoadCardsGaps();
    GameState c = IdleTitanBoard();
    c.scripted_combat_land_search_crack = 1;          // the committed plan cracks (a re-solve)
    const std::vector<TurnSolver::Plan> vc = TurnSolver::IdlePassWithAxes(c);
    CHECK(HasLs(vc, -1, 0));                          // "crack nothing" is reachable
    CHECK_FALSE(HasLs(vc, -1, 1));                    // the bare crack IS the pass now
    GameState a = IdleTitanBoard();
    a.scripted_attack_land_search = 1;                // the committed plan pinned a non-front pick
    const std::vector<TurnSolver::Plan> va = TurnSolver::IdlePassWithAxes(a);
    CHECK(HasLs(va, 0, -1));                          // the all-front pick is reachable
    CHECK_FALSE(HasLs(va, 1, -1));
}

// ---- THE POST-APPLY STATE PINS, one body for both worlds (2026-10-17; TurnSolver::WritePostApplyStatePins)
// ApplyPlanDirect's preamble and the executor (its committed plan, and a breakpoint re-solve's plan) write the
// same fields the same way: WRITE-WHEN-SET, so a re-solve's plan that did not branch on an axis leaves the
// committed plan's pin standing (what the re-solve's own ApplyPlanDirect scored), and one that did replaces it.
TEST_CASE("Post-apply state pins: write-when-set over the pins a state already holds")
{
    GameState s = FreshG(6);
    s.scripted_bruna_gather        = 2;   // the committed plan's pins
    s.scripted_attack_land_search  = 1;
    s.scripted_saga_target         = 0;
    TurnSolver::Plan x;                   // a re-solve's plan: two pins of its own, the rest unset
    x.attack_attach_choice      = 3;
    x.combat_crack_choice       = 0;      // an explicit "crack nothing" is a value, written
    x.fling_victim_choice       = -2;     // a pinned DECLINE is a value too (the != -1 guard)
    TurnSolver::WritePostApplyStatePins(s, x);
    CHECK(s.scripted_attack_attach == 3);
    CHECK(s.scripted_combat_land_search_crack == 0);
    CHECK(s.scripted_fling_victim == -2);
    CHECK(s.scripted_bruna_gather == 2);          // unset on the plan -> inherited, untouched
    CHECK(s.scripted_attack_land_search == 1);
    CHECK(s.scripted_saga_target == 0);
    CHECK(s.scripted_cheat_choice == -1);
    TurnSolver::Plan y;
    y.bruna_gather_choice = 0;                    // a later re-solve picks the front explicitly
    TurnSolver::WritePostApplyStatePins(s, y);
    CHECK(s.scripted_bruna_gather == 0);
    CHECK(s.scripted_attack_attach == 3);
}

// ---- THE COMBAT LANDFALL TARGET (2026-10-17; Plan::combat_landfall_target_choice) -------------------------
// Primeval Titan's attack fetch (and the in-combat Khalni crack) put lands at declare-attackers, after the
// main-phase landfall policy has gone out of scope, so Phylath's TARGETED trigger took the provider's front
// for every plan. The main-1 plan now carries a combat RANK (the crack mask's state-pin route), fanned beside
// an attacking Titan. No win-turn fixture: in goldfish the combat front (attackers first) is never worse in Landfall.
namespace
{
int PlantTokenG(GameState& s, int power, int toughness, int number)
{
    Permanent p;
    p.card.m_name = std::to_string(power) + "/" + std::to_string(toughness) + " Plant Token";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes  = { "Plant" };
    p.card.m_power     = power;
    p.card.m_toughness = toughness;
    p.card.m_number    = number;
    p.is_token         = true;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
// Titan (can attack), a sick Phylath, a 0/1 and a 1/1 Plant (two classes), drop spent, nothing in hand.
GameState IdlePhylathTitanBoard()
{
    GameState s = FreshG(7);
    s.players[0].lands_played_this_turn = 1;
    for (int n = 1; n <= 3; ++n) { PutG(s, "Forest", n, /*tapped=*/true); }
    PutG(s, "Primeval Titan", 10, false);
    PutG(s, "Phylath, World Sculptor", 11, false, /*sick=*/true);
    PlantTokenG(s, 0, 1, 20);
    PlantTokenG(s, 1, 1, 21);
    LibG(s, "Radiant Fountain", 50); LibG(s, "Haunted Fengraf", 51);
    return s;
}
bool HasClt(const std::vector<TurnSolver::Plan>& v, int k)
{
    for (std::size_t i = 1; i < v.size(); ++i) { if (v[i].combat_landfall_target_choice == k) { return true; } }
    return false;
}
int PowerOfNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p.EffectivePower(); } }
    return -100;
}
}   // namespace

TEST_CASE("Combat landfall target: an attacking Titan beside Phylath fans the combat rank, even on the idle pass")
{
    LoadCardsGaps();
    const GameState s = IdlePhylathTitanBoard();
    REQUIRE(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true).empty());
    const std::vector<TurnSolver::Plan> v = TurnSolver::IdlePassWithAxes(s);
    REQUIRE_FALSE(v.empty());
    CHECK(v[0].combat_landfall_target_choice == -1);   // the pass itself: the front
    CHECK(HasClt(v, 1));                                // rank 1: the other Plant class first
    CHECK_FALSE(HasClt(v, 0));                          // the front IS the pass
    CHECK_FALSE(HasClt(v, 2));                          // two singleton classes, no cast: no headroom
    // On a state already holding a rank (a breakpoint re-solve under a committed plan that pinned 1), the
    // pass inherits it, so the front is the variant.
    GameState r = IdlePhylathTitanBoard();
    r.scripted_combat_landfall_target = 1;
    const std::vector<TurnSolver::Plan> vr = TurnSolver::IdlePassWithAxes(r);
    CHECK(HasClt(vr, 0));
    CHECK_FALSE(HasClt(vr, 1));
}

TEST_CASE("Combat landfall target: no attacking Titan, no combat rank")
{
    LoadCardsGaps();
    GameState s = IdlePhylathTitanBoard();
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 10) { p.tapped = true; } }   // Titan can't attack
    const std::vector<TurnSolver::Plan> v = TurnSolver::IdlePassWithAxes(s);
    CHECK_FALSE(HasClt(v, 1));
}

TEST_CASE("Combat landfall target: the Titan's attack fetch resolves Phylath at the combat rank, -1 = the front")
{
    LoadCardsGaps();
    auto attack = [](GameState& g)
    {
        std::vector<int> atk;
        for (int i = 0; i < static_cast<int>(g.battlefield.size()); ++i)
        {
            const int n = g.battlefield[static_cast<std::size_t>(i)].card.m_number;
            if (n == 10 || n == 20 || n == 21) { atk.push_back(i); }
        }
        FireAttackLandSearch(g, 0, atk);
    };
    // The front (swings now, then power): both +4s on the 1/1.
    GameState f = IdlePhylathTitanBoard();
    attack(f);
    CHECK(PowerOfNumber(f, 21) == 9);
    CHECK(PowerOfNumber(f, 20) == 0);
    // Rank 1: the 0/1 first (4/5), then -- it now leads on power -- rank 1 is the 1/1 (5/5).
    GameState k = IdlePhylathTitanBoard();
    k.scripted_combat_landfall_target = 1;
    attack(k);
    CHECK(PowerOfNumber(k, 20) == 4);
    CHECK(PowerOfNumber(k, 21) == 5);
    // A MAIN-PHASE policy live around the call (a projection inside ApplyPlanDirect) does not leak into the
    // combat entries: the combat rank -- here -1, the front -- is installed for them, as in the real combat.
    GameState m = IdlePhylathTitanBoard();
    {
        ScriptedLandfallTarget live(1, m.turn_number);
        attack(m);
        CHECK(LandfallTargetPinFor(m) == 1);            // the outer policy is restored on exit
    }
    CHECK(PowerOfNumber(m, 21) == 9);
    CHECK(PowerOfNumber(m, 20) == 0);
}

TEST_CASE("Combat landfall target: the pin is a post-apply state pin, written write-when-set")
{
    GameState s = FreshG(7);
    TurnSolver::Plan p;
    p.combat_landfall_target_choice = 2;
    TurnSolver::WritePostApplyStatePins(s, p);
    CHECK(s.scripted_combat_landfall_target == 2);
    TurnSolver::Plan q;   // no rank: the inherited one stands
    TurnSolver::WritePostApplyStatePins(s, q);
    CHECK(s.scripted_combat_landfall_target == 2);
}

// ---- A PLAN'S PAYMENT PINS, one body for both worlds (2026-10-17; PlanPaymentPinScope, ai/ContPinScope.h)
// ApplyPlanDirect installs a plan's tap mode and fresh-spend pair UNCONDITIONALLY for its whole apply, so a
// searched breakpoint re-solve's candidates were scored under their own pair. The executor now installs the
// chosen re-solve's pair over the committed plan's for the rest of the phase, through the same scope.
TEST_CASE("Payment pins: the scope installs both pins unconditionally and restores LIFO")
{
    const int tm0 = g_scripted_tapmode, fm0 = g_scripted_freshmode;
    TurnSolver::Plan committed;
    committed.tapmode_choice   = 1;
    committed.freshmode_choice = 1;
    {
        PlanPaymentPinScope pay(committed);
        CHECK(g_scripted_tapmode == 1);
        CHECK(g_scripted_freshmode == 1);
        std::optional<PlanPaymentPinScope> rs_pay;   // the executor's re-solve slot
        TurnSolver::Plan resolve;                     // the re-solve's choice: the shipped rule (0, 0)
        rs_pay.emplace(resolve);
        CHECK(g_scripted_tapmode == 0);               // 0 is a value, not "inherit"
        CHECK(g_scripted_freshmode == 0);
        TurnSolver::Plan later;                       // a later re-solve replaces it
        later.freshmode_choice = 1;
        rs_pay.reset();
        rs_pay.emplace(later);
        CHECK(g_scripted_tapmode == 0);
        CHECK(g_scripted_freshmode == 1);
        rs_pay.reset();
        CHECK(g_scripted_tapmode == 1);               // the committed pair again
        CHECK(g_scripted_freshmode == 1);
    }
    CHECK(g_scripted_tapmode == tm0);
    CHECK(g_scripted_freshmode == fm0);
}
