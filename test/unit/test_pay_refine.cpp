// MTG_PAY_REFINE -- the cheapest payment of the same bill out of the same taps (ManaPayment.cpp TapForCostShared,
// ManaOracle.cpp RefinePayment). Every board here is a mana oracle census shape (seed 1100001): a payment that a
// payment of a SUBSET of its own taps dominates.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaOracle.h"
#include "ai/ManaPayment.h"
#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

Card CardPr(const std::string& name, int number)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

int PutPr(GameState& s, int& num, const char* nm, bool tapped = false)
{
    Permanent p;
    p.card             = CardPr(nm, num++);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

GameState BoardPr(int turn)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    // The census decks these boards come from run no same-phase untapper (StampDeckTraits would clear it); an
    // unstamped board keeps it set, which forbids the float-for-source trade (see the untapper test below).
    s.deck_has_mana_untapper = false;
    return s;
}

int AttachPr(GameState& s, int& num, const char* nm, int host)
{
    const int n = PutPr(s, num, nm);
    s.battlefield.back().aura_attached_to = host;
    return n;
}

bool TappedPr(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p.tapped; } }
    FAIL("no permanent #", num);
    return false;
}

bool PayPr(GameState& g, const ManaCost& cost, bool for_creature, int arm)
{
    heurarm::t_arm[heurarm::PAY_REFINE] = static_cast<std::int8_t>(arm);
    const bool ok = TapForCostShared(g, cost, for_creature, nullptr, false);
    heurarm::t_arm[heurarm::PAY_REFINE] = -1;
    return ok;
}

}   // namespace

// Angels seed 1100008 T2 (DOMINATED-LAND x21): the land drop Plains and Remote Farm ({T}: {W}{W}) up, Giada, Font
// of Hope {1}{W}. The greedy taps the Plains for the {W}, then the Farm for the {1} -- a {W} floats away and the
// Plains is spent for nothing. The Farm alone pays.
TEST_CASE("payment: Remote Farm alone pays {1}{W}, the Plains beside it is given back (MTG_PAY_REFINE, Angels s1100008)")
{
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        GameState s = BoardPr(2);
        int num = 1;
        const int farm   = PutPr(s, num, "Remote Farm");
        const int plains = PutPr(s, num, "Plains");
        const ManaCost giada = CardDatabase::Instance().Lookup("Giada, Font of Hope")->card.m_mana_cost;
        GameState g = s;
        REQUIRE(PayPr(g, giada, /*for_creature=*/true, arm));
        CHECK(TappedPr(g, farm));
        if (arm == 1)
        {
            CHECK_FALSE(TappedPr(g, plains));
            CHECK(g.floating_mana.Total() == 0);
        }
        else { CHECK(TappedPr(g, plains)); CHECK(g.floating_mana.white == 1); }   // the defect, pinned
    }
}

// Melira Pod (DOMINATED-LIFE, 1,042 single-payment flags): Llanowar Wastes ({T}: {C}; {T}: {B} or {G}, 1 damage)
// and Birds of Paradise up, a {1}{G} bill. Whatever the greedy taps first, the same two taps pay with no pain:
// Birds for the {G}, the Wastes' {C} for the {1}.
TEST_CASE("payment: Birds pays the {G} and Llanowar Wastes the {1} on its painless face (MTG_PAY_REFINE, Melira Pod)")
{
    GameState s = BoardPr(4);
    int num = 1;
    const int wastes = PutPr(s, num, "Llanowar Wastes");
    const int birds  = PutPr(s, num, "Birds of Paradise");
    ManaCost bill;
    bill.generic = 1;
    bill.green   = 1;
    GameState g = s;
    REQUIRE(PayPr(g, bill, /*for_creature=*/false, 1));
    CHECK(TappedPr(g, wastes));
    CHECK(TappedPr(g, birds));
    CHECK(g.players[0].life == s.players[0].life);
    CHECK(g.floating_mana.Total() == 0);
}

// The refine never reaches past the payment's own taps: a Forest the payment did not tap stays out of it even where
// it would have been the painless {G} (Llanowar Wastes alone pays {G} for 1 life; the Forest was the greedy's to
// pick, not the refine's).
TEST_CASE("payment: the refine never taps a source the payment left untapped (MTG_PAY_REFINE)")
{
    GameState s = BoardPr(3);
    int num = 1;
    const int wastes = PutPr(s, num, "Llanowar Wastes");
    ManaCost bill;
    bill.green = 1;
    GameState g = s;
    REQUIRE(PayPr(g, bill, /*for_creature=*/false, 1));
    CHECK(TappedPr(g, wastes));
    CHECK(g.players[0].life == s.players[0].life - 1);
}

// The refine's COST levers never change its CHOICE: the lazy surplus (a superset that cannot release as many sources
// as the best payment found is not verified) and the memo (a repeated refine is read back). FiveColour seed 1110001
// (MTG_PAY_REFINE_TRACE, 44 refines of this board): Faeburrow Elder makes one unit of each colour among the permanents
// -- {G}{W} here -- and the greedy pays {W}{U}{B}{R}{G} with all five sources, floating a unit. Four of them pay; the
// paying sets hold the Elder, so each is a parked surplus slot that can release no more than the best found.
TEST_CASE("payment: the refine's lazy surplus and memo pay exactly as the eager enumeration (MTG_PAY_REFINE_LAZY/_MEMO)")
{
    GameState s = BoardPr(5);
    int num = 1;
    PutPr(s, num, "Stomping Ground");
    PutPr(s, num, "Birds of Paradise");
    PutPr(s, num, "Blood Crypt");
    PutPr(s, num, "Faeburrow Elder");
    PutPr(s, num, "Breeding Pool");
    ManaCost wubrg;
    wubrg.white = wubrg.blue = wubrg.black = wubrg.red = wubrg.green = 1;
    auto pay = [&](int refine, int lazy, int memo, int* tapped = nullptr)
    {
        heurarm::t_arm[heurarm::PAY_REFINE_LAZY] = static_cast<std::int8_t>(lazy);
        heurarm::t_arm[heurarm::PAY_REFINE_MEMO] = static_cast<std::int8_t>(memo);
        GameState g = s;
        const bool ok = PayPr(g, wubrg, /*for_creature=*/true, refine);
        heurarm::t_arm[heurarm::PAY_REFINE_LAZY] = -1;
        heurarm::t_arm[heurarm::PAY_REFINE_MEMO] = -1;
        std::string r = ok ? "paid" : "failed";
        int n = 0;
        for (const Permanent& p : g.battlefield) { r += p.tapped ? " T" : " U"; n += p.tapped ? 1 : 0; }
        if (tapped != nullptr) { *tapped = n; }
        const ManaPool& f = g.floating_mana;
        r += " float " + std::to_string(f.white) + std::to_string(f.blue) + std::to_string(f.black) + std::to_string(f.red)
           + std::to_string(f.green) + std::to_string(f.colorless) + std::to_string(f.wild);
        r += " life " + std::to_string(g.players[0].life);
        return r;
    };
    int greedy = 0, refined = 0;
    pay(0, -1, -1, &greedy);
    const std::string eager = pay(1, 0, 0, &refined);
    CAPTURE(eager);
    CHECK(greedy == 5);    // the defect, pinned
    CHECK(refined == 4);
    CHECK(pay(1, 1, 0) == eager);
    CHECK(pay(1, 1, 1) == eager);
    CHECK(pay(1, 1, 1) == eager);   // the memo's own entry, read back
}

// THE UNTAPPER RULE (GameState::deck_has_mana_untapper). EDF seed 2110 gi108's search shape: Brushland under Wild Growth,
// Adarkar Wastes under Fertile Ground, Cloud of Faeries {1}{U} to pay. Adarkar alone ({C} + Fertile Ground's {U}) pays
// it, and with no untapper in the deck giving Brushland back for the floated unit dominates. In EDF it does not:
// Cloud's own enter untaps two lands, so the engine's payment comes back with BOTH lands up and the unit still
// floating, where the refined one has nothing floating -- the refine must keep the engine's float there.
TEST_CASE("payment: the refine never trades a floated unit for a source where a same-phase untapper lives (MTG_PAY_REFINE, EDF)")
{
    GameState s = BoardPr(2);
    int num = 1;
    const int brush = PutPr(s, num, "Brushland");
    AttachPr(s, num, "Wild Growth", brush);
    const int adarkar = PutPr(s, num, "Adarkar Wastes");
    AttachPr(s, num, "Fertile Ground", adarkar);
    const ManaCost cloud = CardDatabase::Instance().Lookup("Cloud of Faeries")->card.m_mana_cost;
    auto pay = [&](int refine, bool untapper)
    {
        GameState g = s;
        g.deck_has_mana_untapper = untapper;
        REQUIRE(PayPr(g, cloud, /*for_creature=*/true, refine));
        return g;
    };
    const GameState greedy = pay(0, false);
    CAPTURE(greedy.floating_mana.Total());
    if (!(TappedPr(greedy, brush) && TappedPr(greedy, adarkar)))
    { MESSAGE("the greedy pays this bill off one land here; nothing for the refine to trade"); return; }
    const GameState traded = pay(1, false);
    CHECK_FALSE(TappedPr(traded, brush));         // no untapper: Brushland given back for the floated unit
    const GameState kept = pay(1, true);
    CHECK(TappedPr(kept, brush));                  // an untapper in the deck: the engine's float stands
    CHECK(kept.floating_mana.Total() >= greedy.floating_mana.Total());
}

// MTG_PAY_REFINE_C_FOLD (item 5d of the mana-clean chain), the refine's {C} fold. Angels mana-clean s30210844 T3:
// Plains, Remote Farm ({T}: {W}{W}) and Sol Ring ({T}: {C}{C}) up, Serra the Benevolent {2}{W}{W}. The greedy pays
// the first {W} off the Plains, the second off the Farm (a {W} floats), the generic drains that {W} and then Sol
// Ring, so a {C} floats away. The Farm and Sol Ring pay exactly; the Plains given back re-makes a {W}, which covers
// that {C} in a deck with no {C} pip -- and never in one that has one (deck_spends_c_pip, EDF).
TEST_CASE("payment: Remote Farm + Sol Ring pay {2}{W}{W} and give the Plains back in a deck with no {C} pip (MTG_PAY_REFINE_C_FOLD, Angels s30210844)")
{
    for (int arm : { 0, 1 })
    {
        for (bool cpip : { false, true })
        {
            CAPTURE(arm);
            CAPTURE(cpip);
            GameState s = BoardPr(3);
            s.deck_spends_c_pip = cpip;
            int num = 1;
            const int plains = PutPr(s, num, "Plains");
            const int farm   = PutPr(s, num, "Remote Farm");
            const int ring   = PutPr(s, num, "Sol Ring");
            const ManaCost serra = CardDatabase::Instance().Lookup("Serra the Benevolent")->card.m_mana_cost;
            GameState g = s;
            heurarm::t_arm[heurarm::PAY_REFINE_C_FOLD] = static_cast<std::int8_t>(arm);
            const bool ok = PayPr(g, serra, /*for_creature=*/true, 1);
            heurarm::t_arm[heurarm::PAY_REFINE_C_FOLD] = -1;
            REQUIRE(ok);
            CHECK(TappedPr(g, farm));
            CHECK(TappedPr(g, ring));
            if (arm == 1 && !cpip)
            {
                CHECK_FALSE(TappedPr(g, plains));
                CHECK(g.floating_mana.Total() == 0);
            }
            else { CHECK(TappedPr(g, plains)); CHECK(g.floating_mana.colorless == 1); }   // the defect, pinned
        }
    }
}

// THE BUDGET-CHARGED CAP (MTG_PAY_REFINE_BUDGET_CAP, Maze's End F5R-3/F5R-4). The refine's probe cap is a price: the
// free allowance (4,096; lowered here through the test hook so a small board reaches it) is untouched, and past it
// each probe charges the decision's budget. With no budget (an unlimited root) the refine runs to its answer; with an
// exhausted budget it stops (budget_cut), and that answer -- a function of the budget, not of the boards -- is never
// memoised. Without the lever the allowance is a wall.
TEST_CASE("payment: the refine's probe cap is a price -- free allowance, then the budget; never memoised when cut (MTG_PAY_REFINE_BUDGET_CAP)")
{
    GameState s = BoardPr(5);
    int num = 1;
    PutPr(s, num, "Stomping Ground");
    PutPr(s, num, "Birds of Paradise");
    PutPr(s, num, "Blood Crypt");
    PutPr(s, num, "Faeburrow Elder");
    PutPr(s, num, "Breeding Pool");
    ManaCost wubrg;
    wubrg.white = wubrg.blue = wubrg.black = wubrg.red = wubrg.green = 1;
    auto pay = [&](int cap_arm, std::size_t free_probes)
    {
        manaoracle::ClearRefineMemoPerGame();
        manaoracle::SetRefineFreeProbesForTest(free_probes);
        heurarm::t_arm[heurarm::PAY_REFINE_BUDGET_CAP] = static_cast<std::int8_t>(cap_arm);
        manaoracle::LastRefineRunForTest(/*reset=*/true);
        GameState g = s;
        const bool ok = PayPr(g, wubrg, /*for_creature=*/true, 1);
        heurarm::t_arm[heurarm::PAY_REFINE_BUDGET_CAP] = -1;
        manaoracle::SetRefineFreeProbesForTest(0);
        std::string r = ok ? "paid" : "failed";
        for (const Permanent& p : g.battlefield) { r += p.tapped ? " T" : " U"; }
        return r;
    };
    const std::string full = pay(0, 0);
    const manaoracle::RefineRunStats st0 = manaoracle::LastRefineRunForTest();
    CAPTURE(full);
    CAPTURE(st0.probes);
    REQUIRE(st0.probes > 3);    // the board must reach the lowered allowance below
    CHECK_FALSE(st0.capped);
    // Lever off: the lowered allowance is a wall.
    pay(0, 2);
    CHECK(manaoracle::LastRefineRunForTest().capped);
    // Lever on, no decision budget (an unlimited root): the refine runs past the allowance to the full answer.
    CHECK(pay(1, 2) == full);
    {
        const manaoracle::RefineRunStats st = manaoracle::LastRefineRunForTest();
        CHECK_FALSE(st.capped);
        CHECK_FALSE(st.budget_cut);
        CHECK(st.probes == st0.probes);
    }
    // Lever on, an exhausted decision budget: cut at the allowance, and the cut answer is not memoised.
    SearchBudget spent(10);
    spent.Consume(10);
    SearchBudget* const prev = TurnSolver::SwapDecisionBudgetForTest(&spent);
    manaoracle::ClearRefineMemoPerGame();
    manaoracle::SetRefineFreeProbesForTest(2);
    heurarm::t_arm[heurarm::PAY_REFINE_BUDGET_CAP] = 1;
    manaoracle::LastRefineRunForTest(true);
    { GameState g = s; PayPr(g, wubrg, true, 1); }
    const manaoracle::RefineRunStats cut = manaoracle::LastRefineRunForTest(true);
    { GameState g = s; PayPr(g, wubrg, true, 1); }   // the same refine again: recomputed, not a memo hit
    const manaoracle::RefineRunStats again = manaoracle::LastRefineRunForTest(true);
    // A live budget with room: charged past the allowance, one unit per probe, and finishes.
    SearchBudget room(1000000);
    TurnSolver::SwapDecisionBudgetForTest(&room);
    manaoracle::ClearRefineMemoPerGame();
    std::string with_room;
    { GameState g = s; const bool ok = PayPr(g, wubrg, true, 1); with_room = ok ? "paid" : "failed";
      for (const Permanent& p : g.battlefield) { with_room += p.tapped ? " T" : " U"; } }
    const manaoracle::RefineRunStats roomy = manaoracle::LastRefineRunForTest(true);
    TurnSolver::SwapDecisionBudgetForTest(prev);
    heurarm::t_arm[heurarm::PAY_REFINE_BUDGET_CAP] = -1;
    manaoracle::SetRefineFreeProbesForTest(0);
    CHECK(cut.budget_cut);
    CHECK(cut.capped);
    CHECK(cut.probes == 3);      // two free, the third asked the budget and stopped
    CHECK(again.probes == 3);    // ran again (a memo hit runs no refine and leaves the record reset)
    CHECK(again.budget_cut);
    CHECK(with_room == full);
    CHECK_FALSE(roomy.budget_cut);
    CHECK(room.Used() == static_cast<long long>(st0.probes) - 2);
}

// THE PRICED, FOLDED ReachesByTapping (MTG_PAY_REFINE_RBT_PRICED). Five Plains given back for the five {W} the
// engine's payment floated: the plain test refuses any release past four sources outright, at every budget; the
// priced one reaches it with no budget and stops (cut) on an exhausted one. On a release of at most four sources
// within the 256-combination guard both answer the same.
TEST_CASE("payment: ReachesByTapping prices its release and combination caps instead of refusing (MTG_PAY_REFINE_RBT_PRICED)")
{
    auto board = [](int lands, const char* nm, bool tapped)
    {
        GameState g = BoardPr(5);
        int num = 1;
        for (int i = 0; i < lands; ++i) { PutPr(g, num, nm, tapped); }
        return g;
    };
    auto reaches = [](const GameState& c, const GameState& e, int arm, bool* cut)
    {
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_PRICED] = static_cast<std::int8_t>(arm);
        const bool r = manaoracle::ReachesByTappingForTest(c, e, cut);
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_PRICED] = -1;
        return r;
    };
    {
        const GameState c = board(5, "Plains", false);
        GameState e = board(5, "Plains", true);
        e.floating_mana.white = 5;
        bool cut = false;
        CHECK_FALSE(reaches(c, e, 0, nullptr));   // the defect, pinned: five released is refused at any budget
        CHECK(reaches(c, e, 1, &cut));
        CHECK_FALSE(cut);
        SearchBudget spent(10);
        spent.Consume(10);
        SearchBudget* const prev = TurnSolver::SwapDecisionBudgetForTest(&spent);
        const bool r = reaches(c, e, 1, &cut);
        TurnSolver::SwapDecisionBudgetForTest(prev);
        CHECK_FALSE(r);
        CHECK(cut);
        e.floating_mana.white = 6;                 // one more than five Plains make: unreachable either way
        CHECK_FALSE(reaches(c, e, 0, nullptr));
        CHECK_FALSE(reaches(c, e, 1, nullptr));
    }
    for (const char* nm : { "Stomping Ground", "Breeding Pool" })
    {
        const GameState c = board(3, nm, false);
        GameState e = board(3, nm, true);
        for (int w = 0; w <= 3; ++w)
        {
            e.floating_mana = ManaPool{};
            if (std::string(nm) == "Stomping Ground") { e.floating_mana.red = w; e.floating_mana.green = 3 - w; }
            else { e.floating_mana.green = w; e.floating_mana.blue = 3 - w; }
            CAPTURE(nm);
            CAPTURE(w);
            CHECK(reaches(c, e, 0, nullptr));
            CHECK(reaches(c, e, 1, nullptr));
            e.floating_mana.white = 1;              // a colour none of them makes
            CHECK_FALSE(reaches(c, e, 0, nullptr));
            CHECK_FALSE(reaches(c, e, 1, nullptr));
        }
    }
}

// THE SUBSET RULE (MTG_PAY_REFINE_RBT_SUBSET). A candidate that gives back a source the engine's payment tapped covers
// it outright when nothing it floated is missing -- an untapped source is at least the tapped one (CoversBoards, and a
// deck with a same-phase untapper reads it that way already). The plain test re-taps EVERY released source, so a
// released source whose every face costs what the engine's board did not pay (a fed face with nothing floating, a
// life payment) refused the candidate. Cascade Bluffs is NOT such a source (its {C} face is free): both rules agree on
// it, and the Horizon Canopy board below is where they differ.
TEST_CASE("payment: ReachesByTapping may leave a released source untapped (MTG_PAY_REFINE_RBT_SUBSET)")
{
    GameState c = BoardPr(5);
    int num = 1;
    PutPr(c, num, "Cascade Bluffs");
    PutPr(c, num, "Island");
    GameState e = c;
    for (Permanent& p : e.battlefield) { p.tapped = true; }
    e.floating_mana.blue = 1;
    auto reaches = [](const GameState& x, const GameState& y, int subset, int priced)
    {
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_SUBSET] = static_cast<std::int8_t>(subset);
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_PRICED] = static_cast<std::int8_t>(priced);
        const bool r = manaoracle::ReachesByTappingForTest(x, y, nullptr);
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_SUBSET] = -1;
        heurarm::t_arm[heurarm::PAY_REFINE_RBT_PRICED] = -1;
        return r;
    };
    // Both rules accept here: the Bluffs' free {C} face re-taps without a feed (the plain rule floats {U}{C}).
    CHECK(reaches(c, e, 0, 0));
    CHECK(reaches(c, e, 0, 1));
    CHECK(reaches(c, e, 1, 0));         // the Island alone makes the {U}; the Bluffs stays up
    CHECK(reaches(c, e, 1, 1));
    // Nothing floated at all: the candidate as it is covers.
    e.floating_mana = ManaPool{};
    CHECK(reaches(c, e, 1, 0));
    // More than the two can make: still refused.
    e.floating_mana.blue = 1; e.floating_mana.red = 1; e.floating_mana.white = 1;
    CHECK_FALSE(reaches(c, e, 1, 0));
    CHECK_FALSE(reaches(c, e, 1, 1));
    // Where the two rules DIFFER: a released source every face of which costs something the engine's board did not
    // pay. Horizon Canopy has no {C} face (every tap costs 1 life); on this constructed board the engine's payment
    // shows the Canopy tapped at full life, so re-tapping it can never cover, while leaving it up does.
    GameState c3 = BoardPr(5);
    int n3 = 1;
    PutPr(c3, n3, "Horizon Canopy");
    PutPr(c3, n3, "Island");
    GameState e3 = c3;
    for (Permanent& p : e3.battlefield) { p.tapped = true; }
    e3.floating_mana.blue = 1;
    CHECK_FALSE(reaches(c3, e3, 0, 0));   // the plain rule re-taps the Canopy: one life short
    CHECK_FALSE(reaches(c3, e3, 0, 1));
    CHECK(reaches(c3, e3, 1, 0));         // the subset rule leaves it up
    CHECK(reaches(c3, e3, 1, 1));
}
