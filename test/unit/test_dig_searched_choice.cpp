// THE DIG IS A SEARCHED CHOICE, NEVER AN AUTOMATIC ACTION (MTG_DIG_SEARCHED_CHOICE, default ON).
//
// USER 2026-10-13, on FiveColour gi1106's T2 Zagoth Triome cycle (which starved a main-2 Faeburrow
// Elder): "'Main 1's automatic cycle' This seems to be a bit of a problem in of itself. It shouldn't
// be automatic." A provider that does not opt into the searched dig axis (GenericProvider and
// TreasureHunt) used to emit no variant, so every plan's apply cycled whenever the provider's gate
// passed. Now every base plan -- and the PASS a search host appends when a main enumerates nothing --
// gets a NO-DIG TWIN (Plan::dig_choice 0) and is itself marked 2 (the gate decides, a sibling exists):
// where the gate passes the search sees both worlds; where it fails nothing digs (the twin is then an
// exact post-apply duplicate the dedup skips). The opt-in axis's pre-gate also widens from "payable
// from the pre-cast pool" to "a source exists, or the plan's own land drop is a sac-draw land", so a
// Horizon Canopy played this turn is offered to the search too.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
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
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

Card NumberedCard(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = NumberedCard(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = false;
    s.battlefield.push_back(p);
}

struct ArmScope
{
    explicit ArmScope(heurarm::Slot slot, int on) { heurarm::t_arm[slot] = static_cast<std::int8_t>(on); }
    ~ArmScope() { heurarm::Clear(); }
};

struct SearchedPlayScope
{
    bool saved;
    SearchedPlayScope() : saved(g_searched_play) { TurnSolver::SetSearchedPlay(true); }
    ~SearchedPlayScope() { TurnSolver::SetSearchedPlay(saved); }
};

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool InGraveyard(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool OnBattlefield(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

int UntappedSources(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (!p.tapped) { ++n; } }
    return n;
}

int CountChoice(const std::vector<TurnSolver::Plan>& plans, int k)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans) { if (p.dig_choice == k) { ++n; } }
    return n;
}

bool Casts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

// The plan that casts exactly `name` (or nothing, for "") carrying dig choice k, dropping `land`.
const TurnSolver::Plan* PlanWith(const std::vector<TurnSolver::Plan>& plans, int k,
                                 const std::string& name, const std::string& land = "")
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.dig_choice != k || p.bp_choice >= 0 || p.land_to_play != land) { continue; }
        if (name.empty() ? p.actions.empty() : (p.actions.size() == 1 && Casts(p, name))) { return &p; }
    }
    return nullptr;
}

// FiveColour (GenericProvider's gate, not an opt-in), turn 3, the drop made, three untapped lands, a
// Zagoth Triome (cycling {3}) and an uncastable Maelstrom Archangel (MV 5) in hand, plus -- when
// `spare_land` -- an Island. Nothing castable + a spare land in hand => the generic empty-of-gas gate
// PASSES; without the Island the Triome is the last land in hand and the gate's carve-out (b) REFUSES.
// Nothing castable means the enumeration is EMPTY and the search host appends the pass.
GameState Stuck(int salt, bool spare_land)
{
    static const FiveColourProvider fc;
    GameState s;
    s.m_provider          = &fc;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 11 + salt;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    Put(s, "Stomping Ground", 46);
    Put(s, "Blood Crypt", 47);
    Put(s, "Overgrown Tomb", 48);
    int num = 100;
    s.players[0].hand.push_back(NumberedCard("Zagoth Triome", num++));
    s.players[0].hand.push_back(NumberedCard("Maelstrom Archangel", num++));
    if (spare_land) { s.players[0].hand.push_back(NumberedCard("Island", num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(NumberedCard("Forest", 900 + k)); }
    return s;
}

}   // namespace

TEST_CASE("Dig searched choice: the PASS (nothing castable) offers dig AND no-dig when the gate passes")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const GameState pre = Stuck(0, /*spare_land=*/true);
    REQUIRE(pre.m_provider->ShouldConsiderDig(pre));
    REQUIRE_FALSE(pre.m_provider->DigDecisionSearched());
    CHECK(TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true).empty());   // the host appends the pass
    const std::vector<TurnSolver::Plan> plans = TurnSolver::IdlePassWithAxes(pre);
    REQUIRE(plans.size() == 2);
    // No automatic heuristic plan, and still NO dig-while-affordable variant for the generic provider
    // (the USER's 2026-09-29 clairvoyance ruling).
    CHECK(CountChoice(plans, -1) == 0);
    CHECK(CountChoice(plans, 1) == 0);
    REQUIRE(plans[0].dig_choice == 2);
    REQUIRE(plans[1].dig_choice == 0);
    {   // The pass runs the provider's gate: it passes, so this world cycles the Triome.
        GameState s = pre;
        TurnSolver::ApplyPlan(s, plans[0], /*is_pre_combat=*/true);
        CHECK_FALSE(InHand(s, "Zagoth Triome"));
        CHECK(InGraveyard(s, "Zagoth Triome"));
        CHECK(UntappedSources(s) == 0);
    }
    {   // The twin is the searched NO-DIG world: nothing automatic happens in its apply.
        GameState s = pre;
        TurnSolver::ApplyPlan(s, plans[1], /*is_pre_combat=*/true);
        CHECK(InHand(s, "Zagoth Triome"));
        CHECK(UntappedSources(s) == 3);
    }
}

TEST_CASE("Dig searched choice: the gate refuses -> nothing digs (the twin is an exact duplicate)")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const GameState pre = Stuck(1, /*spare_land=*/false);
    REQUIRE_FALSE(pre.m_provider->ShouldConsiderDig(pre));   // the last land in hand is not cycled
    const std::vector<TurnSolver::Plan> plans = TurnSolver::IdlePassWithAxes(pre);
    CHECK(CountChoice(plans, 1) == 0);
    std::vector<std::uint64_t> keys;
    for (const TurnSolver::Plan& p : plans)
    {
        GameState s = pre;
        TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
        CHECK(InHand(s, "Zagoth Triome"));                   // the provider's restriction holds
        keys.push_back(TurnSolver::DedupKeyOf(s));
    }
    for (std::uint64_t k : keys) { CHECK(k == keys.front()); }   // the post-apply dedup skips the twin
}

TEST_CASE("Dig searched choice: an enumerated base plan gets the twin too (the gate passes after its cast)")
{
    // Four lands, a castable Birds of Paradise: before the cast the gate refuses (castable gas), after
    // it the hand is a Triome, an Island and an uncastable Archangel with three lands untapped -- the
    // gate passes inside the apply, which is why the pre-gate cannot ask it and the twin is emitted on
    // the source alone.
    EnsureCardsLoaded();
    heurarm::Clear();
    GameState pre = Stuck(2, /*spare_land=*/true);
    Put(pre, "Breeding Pool", 49);
    pre.players[0].hand.push_back(NumberedCard("Birds of Paradise", 120));
    REQUIRE_FALSE(pre.m_provider->ShouldConsiderDig(pre));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true);
    const TurnSolver::Plan* base = PlanWith(plans, 2, "Birds of Paradise");
    const TurnSolver::Plan* twin = PlanWith(plans, 0, "Birds of Paradise");
    REQUIRE(base != nullptr);
    REQUIRE(twin != nullptr);
    CHECK(PlanWith(plans, -1, "Birds of Paradise") == nullptr);
    GameState a = pre, b = pre;
    TurnSolver::ApplyPlan(a, *base, /*is_pre_combat=*/true);
    TurnSolver::ApplyPlan(b, *twin, /*is_pre_combat=*/true);
    CHECK(OnBattlefield(a, "Birds of Paradise"));
    CHECK(OnBattlefield(b, "Birds of Paradise"));
    CHECK_FALSE(InHand(a, "Zagoth Triome"));                 // dig world
    CHECK(InHand(b, "Zagoth Triome"));                       // no-dig world
}

TEST_CASE("Dig searched choice: MTG_DIG_SEARCHED_CHOICE=0 restores the automatic dig")
{
    EnsureCardsLoaded();
    ArmScope arm(heurarm::DIG_SEARCHED_CHOICE, 0);
    const GameState pre = Stuck(3, /*spare_land=*/true);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::IdlePassWithAxes(pre);
    REQUIRE(plans.size() == 1);
    CHECK(plans[0].dig_choice == -1);
    GameState s = pre;
    TurnSolver::ApplyPlan(s, plans[0], /*is_pre_combat=*/true);
    CHECK_FALSE(InHand(s, "Zagoth Triome"));                 // cycled, and the search had no "don't"
    GameState pre2 = Stuck(4, /*spare_land=*/true);
    Put(pre2, "Breeding Pool", 49);
    pre2.players[0].hand.push_back(NumberedCard("Birds of Paradise", 120));
    const std::vector<TurnSolver::Plan> en = TurnSolver::EnumerateMainPlans(pre2, /*is_pre_combat=*/true);
    CHECK(CountChoice(en, 0) == 0);
    CHECK(CountChoice(en, 2) == 0);
}

TEST_CASE("Dig searched choice: gi1106 T2 -- main 1 offers the cycle AND the mana-keeping pass")
{
    // FiveColour gi1106 (seed 6211107): Stomping Ground + Blood Crypt + Birds untapped, the drop
    // made, Faeburrow withheld to main 2 by the USER phase rule, a Zagoth Triome in hand -- main 1
    // enumerates nothing, so the search's pass is the whole decision. With its no-dig twin in the list
    // cycle-now and keep-the-mana are both SEARCHED, instead of one being deleted by a rule (the
    // 2026-10-13 deferral patch, MTG_DIG_DEFER_MAIN2, was deleted 2026-10-14).
    EnsureCardsLoaded();
    heurarm::Clear();
    SearchedPlayScope searched;
    static const FiveColourProvider fc;
    GameState pre;
    pre.m_provider          = &fc;
    pre.active_player_index = 0;
    pre.turn_number         = 2;
    pre.uses_second_main    = true;
    pre.players[0].life     = 9;
    pre.players[1].life     = 20;
    pre.players[0].lands_played_this_turn = 1;
    Put(pre, "Stomping Ground", 46);
    Put(pre, "Birds of Paradise", 2);
    Put(pre, "Blood Crypt", 47);
    int num = 100;
    for (const char* n : { "Unite the Coalition", "Faeburrow Elder", "Maelstrom Archangel",
                           "Maelstrom Archangel", "Island", "Zagoth Triome" })
    { pre.players[0].hand.push_back(NumberedCard(n, num++)); }
    for (int k = 0; k < 10; ++k) { pre.players[0].library.push_back(NumberedCard("Forest", 900 + k)); }
    const std::vector<TurnSolver::Plan> m1 = TurnSolver::IdlePassWithAxes(pre);
    REQUIRE(m1.size() == 2);
    {   // dig now: not deferred (the twin is the no-dig world)
        GameState s = pre;
        TurnSolver::ApplyPlan(s, m1[0], /*is_pre_combat=*/true);
        CHECK_FALSE(InHand(s, "Zagoth Triome"));
    }
    {   // keep the mana: main 2 is offered the Faeburrow
        GameState s = pre;
        TurnSolver::ApplyPlan(s, m1[1], /*is_pre_combat=*/true);
        CHECK(InHand(s, "Zagoth Triome"));
        CHECK(UntappedSources(s) == 3);
        bool fae = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        { if (Casts(p, "Faeburrow Elder")) { fae = true; } }
        CHECK(fae);
    }
}

TEST_CASE("Dig searched choice: an opt-in deck's sac-draw land played THIS turn is offered to the search")
{
    // Auras (DigDecisionSearched): two lands in play, Horizon Canopy the only card in hand, the drop
    // open. The old pre-gate asked for a source PAYABLE ON THE PRE-DROP BOARD -- the Canopy was still in
    // hand, so no dig variant existed and the base plan's heuristic was the only dig world.
    EnsureCardsLoaded();
    static const AurasProvider au;
    auto make = [](int salt)
    {
        GameState s;
        s.m_provider          = &au;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.players[0].life     = 12 + salt;
        s.players[1].life     = 20;
        Put(s, "Plains", 46);
        Put(s, "Forest", 47);
        s.players[0].hand.push_back(NumberedCard("Horizon Canopy", 100));
        for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(NumberedCard("Forest", 900 + k)); }
        return s;
    };
    {
        heurarm::Clear();
        const GameState pre = make(0);
        REQUIRE(pre.m_provider->DigDecisionSearched());
        REQUIRE_FALSE(pre.m_provider->HasAnyDigSource(pre));
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true);
        const TurnSolver::Plan* never = PlanWith(plans, 0, "", "Horizon Canopy");
        const TurnSolver::Plan* dig   = PlanWith(plans, 1, "", "Horizon Canopy");
        REQUIRE(never != nullptr);
        REQUIRE(dig != nullptr);
        CHECK(PlanWith(plans, 2, "", "Horizon Canopy") != nullptr);
        GameState a = pre, b = pre;
        TurnSolver::ApplyPlan(a, *never, /*is_pre_combat=*/true);
        TurnSolver::ApplyPlan(b, *dig, /*is_pre_combat=*/true);
        CHECK(OnBattlefield(a, "Horizon Canopy"));
        CHECK_FALSE(OnBattlefield(b, "Horizon Canopy"));
        CHECK(InGraveyard(b, "Horizon Canopy"));
    }
    {
        ArmScope arm(heurarm::DIG_SEARCHED_CHOICE, 0);
        const GameState pre = make(1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true);
        CHECK(CountChoice(plans, 0) == 0);
        CHECK(CountChoice(plans, 1) == 0);
    }
}

// ---- THE DECK'S OWN PRUNE (DecisionProvider::DigOfferAt -> DigOfferPrunedToGate; MTG_TH_DIG_PRUNE) -----
// USER 2026-10-14: "dig-while-affordable is okay as a search prune in a provider heuristic if the deck
// desires it. It is not okay as greedy logic built into the search." TreasureHuntProvider prunes its dig
// decision to its gate (the twin measured 4 faster / 64 slower over ~17,000 paired games): where the gate
// passes, the pass carries NO no-dig twin and is not marked 2; the gate digs in the apply. =0 restores
// the twin.
namespace
{
// treasure_hunt, turn 3, the drop made, three untapped Islands, nothing castable: a Lonely Sandbar
// (cycling {U}) and two spare lands in hand.
GameState ThStuck()
{
    static const TreasureHuntProvider th;
    GameState s;
    s.m_provider          = &th;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    Put(s, "Island", 46);
    Put(s, "Island", 47);
    Put(s, "Island", 48);
    int num = 100;
    s.players[0].hand.push_back(NumberedCard("Lonely Sandbar", num++));
    s.players[0].hand.push_back(NumberedCard("Island", num++));
    s.players[0].hand.push_back(NumberedCard("Island", num++));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(NumberedCard("Island", 900 + k)); }
    return s;
}
}   // namespace

TEST_CASE("Dig searched choice: treasure_hunt's provider prunes the dig to its gate (no twin)")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const GameState pre = ThStuck();
    REQUIRE(pre.m_provider->HasAnyDigSource(pre));
    REQUIRE(pre.m_provider->ShouldConsiderDig(pre));
    REQUIRE_FALSE(pre.m_provider->DigDecisionSearched());
    REQUIRE_FALSE(pre.m_provider->DigOfferAt(pre).Any());   // the deck's offer: no sibling world
    const std::vector<TurnSolver::Plan> plans = TurnSolver::IdlePassWithAxes(pre);
    REQUIRE(plans.size() == 1);
    CHECK(plans[0].dig_choice == -1);   // the deck's gate decides; no sibling to choose
    GameState s = pre;
    TurnSolver::ApplyPlan(s, plans[0], /*is_pre_combat=*/true);
    CHECK_FALSE(InHand(s, "Lonely Sandbar"));   // the gate passed: the pruned decision is "dig"
    CHECK(InGraveyard(s, "Lonely Sandbar"));
}

TEST_CASE("Dig searched choice: MTG_TH_DIG_PRUNE=0 gives treasure_hunt the no-dig twin again")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    ArmScope off(heurarm::TH_DIG_PRUNE, 0);
    const GameState pre = ThStuck();
    REQUIRE(pre.m_provider->DigOfferAt(pre).no_dig);       // the searched offer: the no-dig twin
    const std::vector<TurnSolver::Plan> plans = TurnSolver::IdlePassWithAxes(pre);
    REQUIRE(plans.size() == 2);
    CHECK(plans[0].dig_choice == 2);
    CHECK(plans[1].dig_choice == 0);
    GameState s = pre;
    TurnSolver::ApplyPlan(s, plans[1], /*is_pre_combat=*/true);
    CHECK(InHand(s, "Lonely Sandbar"));         // the twin keeps the land
}
