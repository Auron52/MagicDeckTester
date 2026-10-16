// Unit tests for two closed land-search gaps (docs/design/land-search-cluster.md, 2026-10-15):
//
//  GAP 2 -- SEARCH FOR TOMORROW HARD-CAST MANA. Its basic enters UNTAPPED, so a later cast of the same
//  main phase may spend it. The cast now carries a LAND-PUT CREDIT on Action::rock_mana, emitted once
//  per credit class of its legal picks, and its resolution is RESTRICTED to the picks earning that
//  credit (core/LandSearch.h LandSearchCreditGroups / LandSearchCreditFilter) -- in the rollout through
//  apply_one's cast_land_put, in the executor through StackEntry::land_put_credit. Pinned:
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
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <map>
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
