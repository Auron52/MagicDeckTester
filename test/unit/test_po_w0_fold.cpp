// MTG_BP_PO_W0_FOLD -- the PLAYOUT wave-0 identity (Vampires search-cost round 2, agent B;
// docs/design/vampires-search-cost-2.md and the pofold namespace in TurnSolver.cpp).
//
// Inside a playout both canon defaults stand down, so a base plan resolves every breakpoint to EMPTY.
// Its EMPTY arm, its rank variants past the first list's length and its chain arm with nothing to chain
// (one class-on breakpoint) then re-apply the base's own line: SolveWithLookahead declines them before
// the unit charge. These pin
//   * the decision table (TurnSolver::PoW0SkipReasonForTest) -- which arm is a twin of what, and that a
//     family the memo knows nothing about (or whose base apply showed an asymmetry) is never declined;
//   * the identity ON THE REAL APPLY PATH (TurnSolver::PoW0ProbeForTest): Falkenrath Pit Fighter's
//     second-main draw opens the hand-entry breakpoint, and every arm the fold would decline lands on
//     its twin's post-apply dedup key, while the arms it keeps (an in-list rank) do not;
//   * an asymmetry disarms the family: a base that casts a permanent with a live activation (site 9,
//     which only a variant can open) is not clean, so nothing of its family is declined.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsPo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefPo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

constexpr int kE = TurnSolver::kBpEmptyChoice;
constexpr int kC = TurnSolver::kBpChainChoice;
constexpr int kD = TurnSolver::kBpDemotedChoice;
enum : int { kNone = 0, kEmpty = 1, kRank = 2, kChainEmpty = 3, kChainRank = 4 };   // pofold's verdicts

TurnSolver::Plan Var(int choice, int at = 0, bool all = false, int base = 0)
{
    TurnSolver::Plan v;
    v.bp_choice = choice;
    v.bp_at     = at;
    v.bp_all    = all;
    v.bp_base   = base;
    return v;
}

// A second-main Vampires board where Falkenrath Pit Fighter's activation is live: an opponent lost life,
// {1}{R} up, a card to discard, the Fighter itself as the Vampire to sacrifice. `extra_red` leaves mana for
// a drawn {R} card after the activation, so the hand-entry list is not empty.
GameState PitFighterBoard(bool extra_red)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PostCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife() - 4;
    s.opponent_lost_life_this_turn       = true;
    s.players[0].lands_played_this_turn = 1;   // land drop spent
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    s.deck_has_madness = true;
    int n = 1;
    auto put = [&](const std::string& name)
    {
        Permanent p;
        p.card             = DefPo(name).card;
        p.card.m_number    = n++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    };
    put("Falkenrath Pit Fighter");
    put("Blackcleave Cliffs");
    put("Blackcleave Cliffs");
    if (extra_red) { put("Blackcleave Cliffs"); }
    Card disc = DefPo("Swamp").card;
    disc.m_number = 100;
    s.players[0].hand.push_back(disc);
    for (int k = 0; k < 12; ++k)
    {
        Card c = DefPo(k % 2 == 0 ? "Falkenrath Gorger" : "Swamp").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// The enumerated second-main plan that activates Pit Fighter and nothing else.
bool FindActivationPlan(const GameState& s, TurnSolver::Plan& out)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, false))
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::SacCreatureOutlet)
        { out = p; return true; }
    }
    return false;
}

std::vector<TurnSolver::Plan> FamilyOf(const TurnSolver::Plan& base)
{
    std::vector<TurnSolver::Plan> v;
    auto add = [&](int choice, bool all)
    {
        TurnSolver::Plan x = base;
        x.bp_choice = choice;
        x.bp_at     = 0;
        x.bp_all    = all;
        x.bp_base   = 0;
        x.bp_wave0  = false;
        v.push_back(x);
    };
    add(0, false);    // rank 0 (measures the list)
    add(1, false);    // rank 1
    add(kE, false);   // EMPTY
    add(0, true);     // uniform 0
    add(kC, true);    // chain slot 0
    return v;
}

}   // namespace

TEST_CASE("po-w0 decision table: which wave-0 arm is a twin, and no evidence means no skip")
{
    const int W = 2;
    // EMPTY: a twin of the base whenever the base apply was clean, at any occurrence count.
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kE), true, 3, -1, -1, 0, -1, W) == kEmpty);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kE, 1), true, 2, -1, -1, 0, -1, W) == kEmpty);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kE), false, 1, 0, -1, 1, -1, W) == kNone);   // asymmetry
    // RANK: past the measured list only, at the first occurrence only, never a uniform / demoted entry.
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(1), true, 1, 1, -1, 1, -1, W) == kRank);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(1), true, 1, 2, -1, 1, -1, W) == kNone);     // in the list
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(1), true, 1, -1, -1, 1, -1, W) == kNone);    // not measured
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(1, 1), true, 2, 0, -1, 1, -1, W) == kNone);  // bp_at 1
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(1, 0, true), true, 1, 0, -1, 1, -1, W) == kNone);   // uniform
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kD), true, 1, 0, -1, 1, -1, W) == kNone);    // demoted
    // CHAIN: one class-on occurrence and nothing chainable -> the base; ci < W on a one-occurrence rank -> it.
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, -1, 1, -1, W) == kChainEmpty);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 2, 3, -1, 1, -1, W) == kNone);   // 2 occurrences
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, 1, 1, 1, W) == kChainRank);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, 1, 1, 2, W) == kNone);     // rank saw 2
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, 1, 1, -1, W) == kNone);    // rank not on file
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, 2, 1, 1, W) == kNone);     // ci >= W
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, 3, -1, 0, -1, W) == kNone);   // slot not scanned
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC, 0, true), true, 1, -1, -1, 1, -1, W) == kNone);  // rank 0 never measured
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kC + 1, 0, true), true, 1, 3, -1, 1, -1, W) == kNone);   // j past n_ci
    // Never a base plan, never a variant without a family.
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(-1), true, 1, 0, -1, 1, -1, W) == kNone);
    CHECK(TurnSolver::PoW0SkipReasonForTest(Var(kE, 0, false, -1), true, 1, 0, -1, 1, -1, W) == kNone);
}

TEST_CASE("po-w0 on the real apply: Pit Fighter's draw, an empty continuation list -> every arm is the base")
{
    EnsureCardsPo();
    GameState s = PitFighterBoard(/*extra_red=*/false);   // the activation spends the last two lands
    TurnSolver::Plan base;
    REQUIRE(FindActivationPlan(s, base));
    const std::vector<TurnSolver::Plan> fam = FamilyOf(base);
    const TurnSolver::PoW0Probe r = TurnSolver::PoW0ProbeForTest(s, base, fam, /*is_pre_combat=*/false);
    CHECK(r.clean);
    CHECK(r.classon >= 1);   // the hand-entry breakpoint was reached
    CHECK(r.n0 == 0);        // nothing castable after the draw: an empty list
    CHECK(r.base_n == 0);    // ...which the BASE read at the same occurrence (MTG_BP_PO_W0_BASE_LEN)
    REQUIRE(r.reason.size() == fam.size());
    CHECK(r.reason[0] == kRank);        // so rank 0 is a twin too (0 >= n0 = 0): no measurer needed
    CHECK(r.same[0]);                   // ...and with n0 = 0 it does overrun onto the base
    CHECK(r.reason[1] == kRank);        // rank 1 >= n0
    CHECK(r.same[1]);
    CHECK(r.reason[2] == kEmpty);
    CHECK(r.same[2]);
    CHECK(r.same[3]);                   // uniform 0 (the existing uniform collapse's business)
    if (r.classon == 1)
    {
        CHECK(r.reason[4] == kChainEmpty);
        CHECK(r.same[4]);
    }
}

TEST_CASE("po-w0 on the real apply: with mana left the in-list rank is a different line, the twins are not")
{
    EnsureCardsPo();
    GameState s = PitFighterBoard(/*extra_red=*/true);    // one Cliffs left for a drawn Gorger
    TurnSolver::Plan base;
    REQUIRE(FindActivationPlan(s, base));
    const std::vector<TurnSolver::Plan> fam = FamilyOf(base);
    const TurnSolver::PoW0Probe r = TurnSolver::PoW0ProbeForTest(s, base, fam, false);
    CHECK(r.clean);
    REQUIRE(r.n0 >= 1);                 // the drawn Gorger is castable
    CHECK(r.base_n == r.n0);            // the base's read and rank 0's agree (same occurrence, same list)
    CHECK(r.reason[0] == kNone);        // a non-empty list is left to rank 0, which is a real line
    CHECK(!r.same[0]);                  // rank 0 casts it: NOT the base, and never declined
    CHECK(r.reason[2] == kEmpty);       // EMPTY: stop after the draw, which is the base's own line
    CHECK(r.same[2]);
    if (r.n0 == 1)
    {
        CHECK(r.reason[1] == kRank);
        CHECK(r.same[1]);
    }
    else { CHECK(r.reason[1] == kNone); }
    // Every arm the fold declines lands on its twin's state.
    for (std::size_t i = 0; i < fam.size(); ++i)
    { if (r.reason[i] != kNone) { CHECK_MESSAGE(r.same[i], "arm ", i, " declined but not a twin"); } }
}

TEST_CASE("po-w0: MTG_BP_PO_W0_BASE_LEN=0 leaves rank 0 as the measurer (it is applied, and is the base)")
{
    EnsureCardsPo();
    GameState s = PitFighterBoard(/*extra_red=*/false);
    TurnSolver::Plan base;
    REQUIRE(FindActivationPlan(s, base));
    const std::vector<TurnSolver::Plan> fam = FamilyOf(base);
    heurarm::t_arm[heurarm::BP_PO_W0_BASE_LEN] = 0;
    const TurnSolver::PoW0Probe r = TurnSolver::PoW0ProbeForTest(s, base, fam, false);
    heurarm::t_arm[heurarm::BP_PO_W0_BASE_LEN] = -1;
    CHECK(r.clean);
    CHECK(r.base_n == -1);              // not read
    CHECK(r.n0 == 0);                   // rank 0 measured it instead
    REQUIRE(!r.reason.empty());
    CHECK(r.reason[0] == kNone);
    CHECK(r.same[0]);
    CHECK(r.reason[1] == kRank);        // ...and the rest of the family is declined exactly as before
}

TEST_CASE("po-w0: a base that casts a permanent with a live activation (site 9) is not clean")
{
    EnsureCardsPo();
    GameState s = PitFighterBoard(/*extra_red=*/true);
    // A second Pit Fighter in hand: casting it puts a NEW activatable ability on the battlefield, the
    // post-entry class only a variant can open.
    Card pf = DefPo("Falkenrath Pit Fighter").card;
    pf.m_number = 101;
    s.players[0].hand.push_back(pf);
    TurnSolver::Plan base;
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, false))
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromHand
            && p.actions[0].card_name.str() == "Falkenrath Pit Fighter")
        { base = p; found = true; break; }
    }
    REQUIRE(found);
    const TurnSolver::PoW0Probe r = TurnSolver::PoW0ProbeForTest(s, base, FamilyOf(base), false);
    CHECK(!r.clean);
    for (int why : r.reason) { CHECK(why == kNone); }
}
