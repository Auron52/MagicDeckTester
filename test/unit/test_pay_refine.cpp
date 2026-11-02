// MTG_PAY_REFINE -- the cheapest payment of the same bill out of the same taps (ManaPayment.cpp TapForCostShared,
// ManaOracle.cpp RefinePayment). Every board here is a mana oracle census shape (seed 1100001): a payment that a
// payment of a SUBSET of its own taps dominates.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
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
