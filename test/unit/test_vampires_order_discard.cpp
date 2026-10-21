// Unit cover for the two AI-DRAFTED Vampires rules awaiting the USER (2026-10-18;
// docs/design/vampires-cast-order-2026-10-18.md). Both ship DEFAULT OFF; these cases force them on per
// thread through their heurarm slots, exactly as a pooled A/B manifest would.
//
//   MTG_VAMPIRES_ORDER -- the TOTAL cast order, by params:
//     Vampire Socialite > Falkenrath Gorger > Stromkirk Captain > Legion Lieutenant > Captivating Vampire
//     > every other creature cheapest first, then by NAME > Markov Baron (convoke) > Aether Vial,
//     plus the drafted main-phase VIEW (MainPhaseOverride; display-only for this deck).
//   MTG_VAMPIRES_BUCKET_DISCARD -- the 5i buckets: FREE (madness-castable now; Bloodghast with a land)
//     -> DEAD -> lands past four counting the board -> a late Vial -> threats past a floor of two by
//     castability x value -> an early Vial -> the floor -> the kept lands. ONE cleanup index; the same
//     order drives the searched outlet discard pins (ActivationDiscardCandidates).
//
// Every expected order below is derived BY HAND from the doc's rule text (the derivation sits beside
// each case); each lever is also checked OFF against VialProvider, which is what the deck plays today.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>
#include "vamp_rules_off.h"

namespace
{

void EnsureCardsVo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefVo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const VampiresProvider& ProvVo()
{
    static const VampiresProvider p;
    return p;
}

// Force one lever for this thread, restored on scope exit.
struct ArmVo
{
    heurarm::Slot slot;
    ArmVo(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~ArmVo() { heurarm::t_arm[slot] = -1; }
};

void PutVo(GameState& s, const std::string& name, int number, bool tapped = false, int charge = 0)
{
    Permanent p;
    p.card              = DefVo(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = false;
    p.charge_counters   = charge;
    s.battlefield.push_back(p);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands that
// way so a rule reading the card's own masks instead of its definition would fail here too.
Card PhVo(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct BoardVo
{
    std::string name;
    bool tapped = false;
    int charge  = 0;
};

// A Vampires state: our hand and board, a 20-card library of 3 Swamps + 4 Unclaimed Territory + 13 Vampire
// of the Dire Moon (the discard_dist census reads the library's land and colour density: 7 lands, 4 of
// them making W / R for a creature spell), the land drop spent unless a case opens it.
GameState MakeVo(const std::vector<std::string>& hand, const std::vector<BoardVo>& board, int turn = 4)
{
    EnsureCardsVo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvVo();
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhVo(n, num++)); }
    for (const BoardVo& b : board) { PutVo(s, b.name, num++, b.tapped, b.charge); }
    for (int k = 0; k < 20; ++k)
    {
        const char* n = k < 3 ? "Swamp" : (k < 7 ? "Unclaimed Territory" : "Vampire of the Dire Moon");
        s.players[0].library.push_back(PhVo(n, 500 + k));
    }
    return s;
}

std::vector<int> CleanupVo(const GameState& s, bool on)
{
    ArmVo arm(heurarm::VAMPIRES_BUCKET_DISCARD, on);
    return ProvVo().CleanupDiscardCandidates(s, nullptr);
}

std::string CleanupPickVo(const GameState& s)
{
    const std::vector<int> v = CleanupVo(s, true);
    REQUIRE(v.size() == 1);   // ONE index (5i rule 5)
    return s.players[0].hand[static_cast<std::size_t>(v.front())].m_name.str();
}

// The full bucket order, read through the searched outlet's discard axis (every distinct name, most
// expendable first).
std::vector<std::string> NamesVo(const GameState& s)
{
    std::vector<std::string> names;
    for (const Card& c : s.players[0].hand)
    { if (std::find(names.begin(), names.end(), c.m_name.str()) == names.end()) { names.push_back(c.m_name.str()); } }
    return names;
}

std::vector<std::string> ActivationVo(const GameState& s, bool on)
{
    ArmVo arm(heurarm::VAMPIRES_BUCKET_DISCARD, on);
    return ProvVo().ActivationDiscardCandidates(s, DefVo("Stromkirk Condemned"), NamesVo(s));
}

int PosVo(const std::vector<std::string>& v, const std::string& n)
{
    const auto it = std::find(v.begin(), v.end(), n);
    REQUIRE_MESSAGE(it != v.end(), "missing from the order: ", n);
    return static_cast<int>(it - v.begin());
}

// ---- cast-order fixtures ---------------------------------------------------------------------------

const std::vector<std::string> kVampOrder = {
    "Vampire Socialite", "Falkenrath Gorger", "Stromkirk Captain", "Legion Lieutenant", "Captivating Vampire",
    "Falkenrath Pit Fighter", "Vampire of the Dire Moon",                    // mana value 1, by name
    "Asylum Visitor", "Bloodghast", "Stromkirk Condemned",                   // 2
    "Drana, Liberator of Malakir", "Mavren Fein, Dusk Apostle",              // 3
    "Bloodline Keeper",                                                      // 4
    "Markov Baron", "Aether Vial",
};

GameState CastBoardVo()
{
    EnsureCardsVo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 9;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvVo();
    // Unclaimed Territory pays any colour for a creature spell and {C} for Aether Vial -- enough that the
    // payable-order fallback never has a reason to move anything.
    for (int k = 0; k < 45; ++k) { PutVo(s, "Unclaimed Territory", 300 + k); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(PhVo("Swamp", 900 + k)); }
    return s;
}

TurnSolver::Plan PlanVo(const std::vector<std::string>& names, const std::vector<std::string>& puts = {})
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const std::string& n : names)
    {
        Action a;
        const bool put = std::find(puts.begin(), puts.end(), n) != puts.end();
        a.kind      = put ? Action::Kind::ActivateVial : Action::Kind::CastFromHand;
        a.card_name = n;
        a.def       = &DefVo(n);
        plan.actions.push_back(a);
    }
    plan.searched_order = false;
    return plan;
}

Action CastVo(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefVo(name);
    return a;
}

}   // namespace

// =====================================================================================================
// MTG_VAMPIRES_ORDER
// =====================================================================================================

TEST_CASE("Vampires cast order: the rank lays the list out in the drafted order, and it is TOTAL")
{
    ArmVo arm(heurarm::VAMPIRES_ORDER, true);
    const GameState s = CastBoardVo();
    const VampiresProvider& prov = ProvVo();
    // Strictly increasing across the slots that differ by RULE; within one mana value of the free block
    // the rank ties and the NAME decides (checked through the comparator below).
    for (std::size_t i = 1; i < kVampOrder.size(); ++i)
    {
        CAPTURE(kVampOrder[i - 1]);
        CAPTURE(kVampOrder[i]);
        CHECK(prov.CastOrderRank(s, DefVo(kVampOrder[i - 1])) <= prov.CastOrderRank(s, DefVo(kVampOrder[i])));
        CHECK(CastOrderLess(s, CastVo(kVampOrder[i - 1]), CastVo(kVampOrder[i])));
        CHECK_FALSE(CastOrderLess(s, CastVo(kVampOrder[i]), CastVo(kVampOrder[i - 1])));
    }
    CHECK(prov.CastOrderTotalByName());
    // TOTAL over every distinct name, two cards the list does not hold included (a creature lands at its
    // mana value among the bodies -- before Baron -- a noncreature after the Vial).
    std::vector<std::string> names = kVampOrder;
    names.push_back("Goblin Guide");
    names.push_back("Lightning Bolt");
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        for (std::size_t j = 0; j < names.size(); ++j)
        {
            if (i == j) { continue; }
            const bool ab = CastOrderLess(s, CastVo(names[i]), CastVo(names[j]));
            const bool ba = CastOrderLess(s, CastVo(names[j]), CastVo(names[i]));
            CHECK_MESSAGE(ab != ba, "tie between ", names[i], " and ", names[j]);
        }
    }
    CHECK(CastOrderLess(s, CastVo("Goblin Guide"), CastVo("Markov Baron")));
    CHECK(CastOrderLess(s, CastVo("Aether Vial"), CastVo("Lightning Bolt")));
    // Every slot has a report label.
    for (const std::string& n : kVampOrder)
    { CHECK_MESSAGE(prov.CastOrderTierName(prov.CastOrderRank(s, DefVo(n))) != nullptr, n); }
}

TEST_CASE("Vampires cast order: permuting the hand never changes the realised order (casts and Vial puts)")
{
    ArmVo arm(heurarm::VAMPIRES_ORDER, true);
    GameState s = CastBoardVo();
    PutVo(s, "Aether Vial", 50, false, 2);
    std::vector<std::vector<std::string>> perms;
    perms.push_back(kVampOrder);
    perms.emplace_back(kVampOrder.rbegin(), kVampOrder.rend());
    std::mt19937 rng(20261018u);
    for (int k = 0; k < 40; ++k)
    {
        std::vector<std::string> p = kVampOrder;
        std::shuffle(p.begin(), p.end(), rng);
        perms.push_back(p);
    }
    for (const std::vector<std::string>& perm : perms)
    {
        CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanVo(perm)) == kVampOrder);
        // Vial-put the mana-value-2 Vampires instead: a put is sequenced at its creature's slot.
        CHECK(TurnSolver::RealisedNonSacCastOrder(
                  s, PlanVo(perm, {"Legion Lieutenant", "Bloodghast", "Stromkirk Condemned"}))
              == kVampOrder);
    }
}

TEST_CASE("Vampires cast order: Markov Baron convokes off the bodies cast before it")
{
    ArmVo arm(heurarm::VAMPIRES_ORDER, true);
    const GameState s = CastBoardVo();
    // Baron first in hand order; the rank puts every other creature ahead of it, the Vial after it.
    CHECK(TurnSolver::RealisedNonSacCastOrder(
              s, PlanVo({"Markov Baron", "Aether Vial", "Vampire of the Dire Moon", "Bloodline Keeper"}))
          == std::vector<std::string>{"Vampire of the Dire Moon", "Bloodline Keeper", "Markov Baron", "Aether Vial"});
    // Two Barons stay together, after the body.
    CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanVo({"Markov Baron", "Markov Baron", "Falkenrath Gorger"}))
          == std::vector<std::string>{"Falkenrath Gorger", "Markov Baron", "Markov Baron"});
}

TEST_CASE("Vampires cast order: the drafted main-phase view (display only)")
{
    const GameState s = CastBoardVo();
    const VampiresProvider& prov = ProvVo();
    using MP = DecisionProvider::MainPhase;
    {
        ArmVo arm(heurarm::VAMPIRES_ORDER, true);
        auto mp = [&](const char* n) { return prov.MainPhaseOverride(s, DefVo(n)); };
        CHECK(mp("Vampire Socialite") == MP::Main2);
        CHECK(mp("Falkenrath Pit Fighter") == MP::Main2);
        CHECK(mp("Stromkirk Captain") == MP::Main1);
        CHECK(mp("Legion Lieutenant") == MP::Main1);
        CHECK(mp("Mavren Fein, Dusk Apostle") == MP::Main1);
        CHECK(mp("Aether Vial") == MP::Main1);
        for (const char* n : {"Markov Baron", "Captivating Vampire", "Stromkirk Condemned", "Bloodghast",
                              "Bloodline Keeper", "Drana, Liberator of Malakir", "Falkenrath Gorger",
                              "Asylum Visitor", "Vampire of the Dire Moon"})
        { CHECK_MESSAGE(mp(n) == MP::Both, n); }
        // Display only: the deck opts into neither consumer, so play still searches both mains.
        CHECK_FALSE(prov.ClassifiesMainPhases());
        CHECK_FALSE(prov.CondemnsPassedMainPhase());
    }
    ArmVo off(heurarm::VAMPIRES_ORDER, false);
    static const VialProvider vial;
    for (const std::string& n : kVampOrder)
    { CHECK(prov.MainPhaseOverride(s, DefVo(n)) == vial.MainPhaseOverride(s, DefVo(n))); }
}

TEST_CASE("Vampires cast order: lever off is VialProvider's order, ties kept in plan order")
{
    ArmVo arm(heurarm::VAMPIRES_ORDER, false);
    const GameState s = CastBoardVo();
    static const VialProvider vial;
    for (const std::string& n : kVampOrder)
    {
        CHECK(ProvVo().CastOrderRank(s, DefVo(n)) == vial.CastOrderRank(s, DefVo(n)));
        const int r = ProvVo().CastOrderRank(s, DefVo(n));
        const char* a = ProvVo().CastOrderTierName(r);
        const char* b = vial.CastOrderTierName(r);
        CHECK(((a == nullptr) == (b == nullptr)));
    }
    CHECK(ProvVo().CastOrderTotalByName() == vial.CastOrderTotalByName());
    // Every creature ties at the generic 10, so plan order decides -- Baron first if the hand says so.
    CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanVo({"Markov Baron", "Vampire of the Dire Moon"}))
          == std::vector<std::string>{"Markov Baron", "Vampire of the Dire Moon"});
}

// =====================================================================================================
// MTG_VAMPIRES_BUCKET_DISCARD
// =====================================================================================================

TEST_CASE("Vampires discard buckets: a creature madness can cast now is the free CLEANUP shed, best first")
{
    VampRulesOff _rules_off;   // tests the machinery the USER rules restrict (vamp_rules_off.h)
    // Gorger out, three untapped Swamps (BBB). Madness-castable now: Markov Baron {2}{B} (own madness,
    // and Gorger's; convoke off the untapped Gorger) and Vampire of the Dire Moon {B} (Gorger's); NOT
    // Stromkirk Captain {1}{B}{R} (no red) nor Legion Lieutenant {W}{B} (no white) nor Bloodline Keeper
    // (four mana). Best value first: Baron (2+2 + 6 lord + 1 convoke = 11) over Dire Moon (2).
    const GameState s = MakeVo({"Vampire of the Dire Moon", "Stromkirk Captain", "Markov Baron",
                                "Legion Lieutenant", "Swamp", "Swamp", "Swamp", "Bloodline Keeper"},
                               {{"Falkenrath Gorger"}, {"Swamp"}, {"Swamp"}, {"Swamp"}});
    CHECK(CleanupPickVo(s) == "Markov Baron");
    // The promotion is CLEANUP-ONLY: an outlet's order is built on the pre-plan board, whose mana the
    // plan's own casts spend first. There the surplus Swamps (board 3 -> the hand owes one) lead and the
    // Baron sits at its EV (castable next turn, value 11) behind the 1/1.
    const std::vector<std::string> order = ActivationVo(s, true);
    CHECK(order.front() == "Swamp");
    CHECK(PosVo(order, "Vampire of the Dire Moon") < PosVo(order, "Markov Baron"));
}

TEST_CASE("Vampires discard buckets: with no untapped mana nothing is free -- a land past four goes")
{
    // Same hand, every land tapped: no madness cast is payable. Board 3 lands -> the hand owes ONE land
    // (four counting the board), so two of the three Swamps are surplus and shed first.
    const GameState s = MakeVo({"Vampire of the Dire Moon", "Stromkirk Captain", "Markov Baron",
                                "Legion Lieutenant", "Swamp", "Swamp", "Swamp", "Bloodline Keeper"},
                               {{"Falkenrath Gorger"}, {"Swamp", true}, {"Swamp", true}, {"Swamp", true}});
    CHECK(CleanupPickVo(s) == "Swamp");
}

TEST_CASE("Vampires discard buckets: Bloodghast with a land in hand is a free shed")
{
    VampRulesOff _rules_off;   // tests the machinery the USER rules restrict (vamp_rules_off.h)
    // No granter, no mana: Bloodghast comes back on the held land, ahead of a dead second Vial and the
    // surplus land.
    const GameState s = MakeVo({"Bloodghast", "Aether Vial", "Swamp", "Swamp", "Legion Lieutenant",
                                "Stromkirk Captain", "Drana, Liberator of Malakir", "Stromkirk Condemned"},
                               {{"Aether Vial"}, {"Swamp", true}, {"Swamp", true}, {"Swamp", true}});
    CHECK(CleanupPickVo(s) == "Bloodghast");
    const std::vector<std::string> order = ActivationVo(s, true);
    // Bloodghast -> the dead 2nd Vial -> the surplus Swamp (one name: the kept Swamp shares it, so the
    // name's first appearance is the surplus copy) -> threats.
    CHECK(order.front() == "Bloodghast");
    CHECK(order[1] == "Aether Vial");
    CHECK(order[2] == "Swamp");
}

TEST_CASE("Vampires discard buckets: an outlet discard leads with Bloodghast; a cleanup deploys the Baron")
{
    // Gorger out, three untapped Swamps, a Swamp in hand. OUTLET order (the land drop open or spent):
    // Bloodghast first -- the held land returns it (this turn while the drop is open, for no mana) --
    // then the threats by EV with the board's 3 Swamps + the kept Swamp and a library of 7 lands / 4
    // rainbow: Legion Lieutenant 10 x ~0.41 (no white yet) = 4.1, Stromkirk Captain 11 x ~0.41 (no red
    // yet) = 4.5, Markov Baron 11 x 1 (convoke off Gorger) = 11; the kept Swamp last.
    GameState s = MakeVo({"Bloodghast", "Markov Baron", "Swamp", "Stromkirk Captain", "Legion Lieutenant"},
                         {{"Falkenrath Gorger"}, {"Swamp"}, {"Swamp"}, {"Swamp"}});
    const std::vector<std::string> expect = {"Bloodghast", "Legion Lieutenant", "Stromkirk Captain",
                                             "Markov Baron", "Swamp"};
    s.players[0].lands_played_this_turn = 0;
    CHECK(ActivationVo(s, true) == expect);
    s.players[0].lands_played_this_turn = 1;
    CHECK(ActivationVo(s, true) == expect);
    // CLEANUP in the same state: the untapped BBB pays Baron ({2}{B}, convoke -1) and Bloodghast ({B}{B},
    // Gorger's), so both are free sheds -- the Baron, worth more, deployed first.
    CHECK(CleanupPickVo(s) == "Markov Baron");
}

TEST_CASE("Vampires discard buckets: the Vial is kept early (after the threat floor) and shed late")
{
    // Turn 1, no Vial out, board ONE Swamp: the hand owes 3 lands, so both held Swamps are kept. Five
    // mono-black threats (no colour noise), EV = castability x value with board 1 + the 2 kept Swamps and
    // a library of 7 lands in 20:  Dire Moon 1 x 2 = 2; Asylum Visitor 1 x 5 = 5; Bloodline Keeper (a land
    // short by turn 3) ~0.46 x 12 = 5.6; Stromkirk Condemned 1 x 6 = 6; Captivating Vampire (turn 2) 0.75
    // x 11 = 8.25. Past the floor of two: Dire Moon, Visitor, Keeper -> then the EARLY Vial -> Condemned,
    // Captivating -> the kept Swamps.
    const std::vector<std::string> hand = {"Aether Vial", "Swamp", "Swamp", "Vampire of the Dire Moon",
                                           "Asylum Visitor", "Stromkirk Condemned", "Captivating Vampire",
                                           "Bloodline Keeper"};
    {
        const GameState s = MakeVo(hand, {{"Swamp", true}}, /*turn=*/1);
        const std::vector<std::string> order = ActivationVo(s, true);
        CHECK(order == std::vector<std::string>{"Vampire of the Dire Moon", "Asylum Visitor", "Bloodline Keeper",
                                                "Aether Vial", "Stromkirk Condemned", "Captivating Vampire",
                                                "Swamp"});
        CHECK(CleanupPickVo(s) == "Vampire of the Dire Moon");
    }
    {
        // Turn 4: the Vial is surplus and sheds first (no free / dead / surplus-land card in this hand).
        const GameState s = MakeVo(hand, {{"Swamp", true}}, /*turn=*/4);
        CHECK(CleanupPickVo(s) == "Aether Vial");
    }
}

TEST_CASE("Vampires discard buckets: threats shed by castability x value")
{
    // Board two Swamps + an Unclaimed Territory (every colour for a creature spell), no land in hand, no
    // granter. Castable next turn: Dire Moon (value 2), Legion Lieutenant (10), Stromkirk Captain (11).
    // Bloodline Keeper (value 12, the highest) is a land short: P(castable) 0.35 / 0.59 / 0.75 / 0.85 over
    // the next four turns -> factor ~0.66 -> EV ~8.0. So: Dire Moon (2) -> Keeper (8.0) -> Lieutenant (10)
    // -> Captain (11). Distance-to-playable puts the most valuable card second.
    const GameState s = MakeVo({"Bloodline Keeper", "Legion Lieutenant", "Vampire of the Dire Moon",
                                "Stromkirk Captain"},
                               {{"Swamp", true}, {"Swamp", true}, {"Unclaimed Territory", true}});
    const std::vector<std::string> order = ActivationVo(s, true);
    CHECK(order == std::vector<std::string>{"Vampire of the Dire Moon", "Bloodline Keeper", "Legion Lieutenant",
                                            "Stromkirk Captain"});
    // Every name exactly once.
    CHECK(order.size() == NamesVo(s).size());
}

TEST_CASE("Vampires discard buckets: a held Vial makes a 1-drop castable whatever its colour")
{
    // Board two tapped Swamps (no red), no land in hand, turn 3, an outlet's order. Every card here needs
    // {R}, which only a drawn Territory (4 of the library's 20) can make: Pit Fighter 5 x ~0.46 = 2.3,
    // Socialite 7 x ~0.46 = 3.2, Captain 11 x ~0.34 = 3.7. With an Aether Vial in hand the 1-drop
    // Pit Fighter is put the turn after the Vial lands, colour or not: castability >= 0.75 -> EV 3.75,
    // now behind the Socialite (d0 probe 2026-10-18, seed 1242: the pump shed the Pit Fighter that the
    // same turn's Vial would have put for free).
    const std::vector<BoardVo> board = {{"Swamp", true}, {"Swamp", true}};
    {
        const GameState s = MakeVo({"Vampire Socialite", "Falkenrath Pit Fighter", "Stromkirk Captain"}, board, 3);
        const std::vector<std::string> order = ActivationVo(s, true);
        CHECK(PosVo(order, "Falkenrath Pit Fighter") < PosVo(order, "Vampire Socialite"));
    }
    {
        const GameState s = MakeVo({"Vampire Socialite", "Falkenrath Pit Fighter", "Stromkirk Captain",
                                    "Aether Vial"}, board, 3);
        const std::vector<std::string> order = ActivationVo(s, true);
        CHECK(PosVo(order, "Vampire Socialite") < PosVo(order, "Falkenrath Pit Fighter"));
    }
}

TEST_CASE("Vampires discard buckets: lever off is VialProvider's rule, byte for byte")
{
    const GameState s = MakeVo({"Vampire of the Dire Moon", "Stromkirk Captain", "Markov Baron",
                                "Legion Lieutenant", "Swamp", "Swamp", "Swamp", "Bloodline Keeper"},
                               {{"Falkenrath Gorger"}, {"Swamp"}, {"Swamp"}, {"Swamp"}});
    static const VialProvider vial;
    CHECK(CleanupVo(s, false) == vial.CleanupDiscardCandidates(s, nullptr));
    {
        ArmVo arm(heurarm::VAMPIRES_BUCKET_DISCARD, false);
        CHECK(ProvVo().CleanupDiscardShedStable() == vial.CleanupDiscardShedStable());
    }
    {
        ArmVo arm(heurarm::VAMPIRES_BUCKET_DISCARD, true);
        CHECK_FALSE(ProvVo().CleanupDiscardShedStable());
    }
    // The control arm must differ here: the bucket rule deploys the Baron, the inherited rule does not.
    CHECK(CleanupVo(s, true).front() != CleanupVo(s, false).front());
}

// ---- the USER's rules of 2026-10-21 ------------------------------------------------------------------

namespace
{
std::vector<std::string> PinsUr(const GameState& s)
{ return ProvVo().ActivationDiscardCandidates(s, DefVo("Stromkirk Condemned"), NamesVo(s)); }

bool PlansCastUr(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if ((a.kind == Action::Kind::CastFromHand || a.kind == Action::Kind::ActivateVial) && a.card_name == name)
            { return true; }
        }
    }
    return false;
}
}   // namespace

TEST_CASE("USER rule MTG_VAMP_FREE_DISCARD: an affordable madness card is the whole discard axis; =0 every name")
{
    // Markov Baron's own madness {2}{B} = its mana cost; three untapped Swamps pay it.
    GameState s = MakeVo({"Markov Baron", "Swamp", "Aether Vial"}, {{"Swamp"}, {"Swamp"}, {"Swamp"}});
    s.phase = Phase::PreCombatMain;
    CHECK(PinsUr(s) == std::vector<std::string>{ "Markov Baron" });
    {
        VampRulesOff off;
        CHECK(PinsUr(s).size() == 3);
    }
    // Two untapped Swamps cannot pay it, and no land drop is open: nothing is free -> every name.
    GameState poor = MakeVo({"Markov Baron", "Swamp", "Aether Vial"}, {{"Swamp"}, {"Swamp"}, {"Swamp", true}});
    poor.phase = Phase::PreCombatMain;
    CHECK(PinsUr(poor).size() == 3);
    // ...unless the land drop is still open and a land is in hand: the drop pays the third mana.
    GameState drop = poor;
    drop.players[0].lands_played_this_turn = 0;
    CHECK(PinsUr(drop) == std::vector<std::string>{ "Markov Baron" });
}

TEST_CASE("USER rule MTG_VAMP_FREE_DISCARD: Bloodghast is a free discard with no mana at all")
{
    GameState s = MakeVo({"Bloodghast", "Swamp", "Stromkirk Captain"}, {{"Swamp", true}});
    s.phase = Phase::PreCombatMain;
    CHECK(PinsUr(s) == std::vector<std::string>{ "Bloodghast" });
}

TEST_CASE("USER rules: a pre-combat Vampire Socialite is not a free discard (its madness cast would be a main-1 cast)")
{
    // Gorger grants Socialite madness {B}{R}; Blackcleave Cliffs + a Swamp pay it.
    GameState s = MakeVo({"Vampire Socialite", "Swamp", "Aether Vial"},
                         {{"Falkenrath Gorger"}, {"Blackcleave Cliffs"}, {"Swamp"}});
    s.phase = Phase::PreCombatMain;
    CHECK(PinsUr(s).size() == 3);                                              // no free discard before combat
    s.phase = Phase::PostCombatMain;
    CHECK(PinsUr(s) == std::vector<std::string>{ "Vampire Socialite" });       // after combat it is
}

TEST_CASE("USER rule MTG_VAMP_SOCIALITE_M2: Vampire Socialite is never cast or Vial-put before combat; =0 restores it")
{
    // "Vampire Socialite should always be cast in main 2, so it can benefit from the damage dealt."
    GameState s = MakeVo({"Vampire Socialite", "Swamp"},
                         {{"Blackcleave Cliffs"}, {"Swamp"}, {"Aether Vial", false, 2}});
    s.phase = Phase::PreCombatMain;
    CHECK_FALSE(PlansCastUr(TurnSolver::EnumerateMainPlans(s, true), "Vampire Socialite"));
    GameState post = s;
    post.phase = Phase::PostCombatMain;
    CHECK(PlansCastUr(TurnSolver::EnumerateMainPlans(post, false), "Vampire Socialite"));
    VampRulesOff off;
    CHECK(PlansCastUr(TurnSolver::EnumerateMainPlans(s, true), "Vampire Socialite"));
}
