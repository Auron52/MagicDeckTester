// MTG_LINE_REPAY -- the depth-1 rollback of a line's previous payment (ManaPayment.cpp LineRepayTry;
// docs/design/mana-completeness-oracle.md section 14). The mana-clean gate's Dragons pass (pass1-dragons, 71,000
// games) found the per-cast payer dropping a line's later cast because the cast before it spent the wrong
// source, on two shapes that both turn on Gruul Turf ({T}: add {R}{G}):
//
//  * APPLY-DROP: {Mind Stone, Scourge of Valkas} off Mountain, Mountain, Mind Stone, Gruul Turf with Dragonspeaker
//    Shaman out (Scourge {R}{R}{R}). The payer paid Mind Stone's {2} with a Mountain and the old Mind Stone; Gruul
//    Turf then makes only one {R}. The line pays when the Turf's {G} and the old Mind Stone pay the {2} and its {R}
//    floats into Scourge.
//  * ENUM-GAP: {Urza's Incubator, Lathliss, Dragon Queen} off Mountain, Mind Stone, Gruul Turf and a fresh Haven of
//    the Spirit Dragon with the Shaman out. Incubator ({3}) discounts Lathliss to {R}{R}; the payer paid the {3}
//    with the Mountain, the Haven's {C} and Mind Stone, so the enumerator's sequential walk (the same payer) found
//    Lathliss unpayable and kept the plan off the menu.
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

#include <cstdint>
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

void Put(GameState& s, const std::string& name, int number, bool entered_this_turn = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = entered_this_turn;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState BaseBoard()
{
    static const DragonsProvider prov;   // the deck's own cast order: rocks and reducers before the Dragons
    GameState s;
    s.m_provider          = &prov;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    // A Dragon library: Urza's Incubator names the deck's dominant creature type as it enters.
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def(k % 2 == 0 ? "Glorybringer" : "Mountain").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

bool PlanCasts(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    for (const std::string& n : names)
    {
        bool found = false;
        for (const Action& x : p.actions)
        { if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == n) { found = true; } }
        if (!found) { return false; }
    }
    return true;
}

bool OnBattlefield(const GameState& s, const std::string& name, int number)
{
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == name && p.card.m_number == number) { return true; } }
    return false;
}

// Mountain, Mountain (this turn's land), Mind Stone, Dragonspeaker Shaman, Gruul Turf; Scourge + Mind Stone in hand.
GameState ScourgeBoard()
{
    GameState s = BaseBoard();
    Put(s, "Mountain", 1);
    Put(s, "Mind Stone", 2);
    Put(s, "Dragonspeaker Shaman", 3);
    Put(s, "Gruul Turf", 4);
    Put(s, "Mountain", 5, /*entered_this_turn=*/true);
    Hand(s, "Scourge of Valkas", 10);
    Hand(s, "Mind Stone", 11);
    return s;
}

// The applied plan: does Scourge of Valkas reach the battlefield next to the new Mind Stone?
bool ScourgeLinePays(const GameState& s0)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s0, /*is_pre_combat=*/true))
    {
        if (p.actions.size() != 2 || !PlanCasts(p, { "Scourge of Valkas", "Mind Stone" })) { continue; }
        GameState s = s0;
        TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
        return OnBattlefield(s, "Scourge of Valkas", 10) && OnBattlefield(s, "Mind Stone", 11);
    }
    FAIL("the menu does not hold {Mind Stone, Scourge of Valkas}");
    return false;
}

// Mountain, Mind Stone, Dragonspeaker Shaman, Gruul Turf, a fresh Haven of the Spirit Dragon; Incubator + Lathliss.
GameState IncubatorBoard()
{
    GameState s = BaseBoard();
    Put(s, "Mountain", 1);
    Put(s, "Mind Stone", 2);
    Put(s, "Dragonspeaker Shaman", 3);
    Put(s, "Gruul Turf", 4);
    Put(s, "Haven of the Spirit Dragon", 5, /*entered_this_turn=*/true);
    Hand(s, "Urza's Incubator", 10);
    Hand(s, "Lathliss, Dragon Queen", 11);
    return s;
}

bool OffersIncubatorLathliss(const GameState& s)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (PlanCasts(p, { "Urza's Incubator", "Lathliss, Dragon Queen" })) { return true; } }
    return false;
}

}   // namespace

void LineRepayClearPerGame();   // ManaPayment.cpp: the record ring and memo (TurnSolver::ClearPerGameCaches)

TEST_CASE("Line repay: {Mind Stone, Scourge of Valkas} pays both off Gruul Turf's {G} and its floating {R}")
{
    EnsureCardsLoaded();
    const GameState s = ScourgeBoard();
    {
        ArmPin on(heurarm::LINE_REPAY, true);
        CHECK(ScourgeLinePays(s));
    }
    // The lever off: the per-cast payer's Mind Stone payment strands the third {R} and Scourge is dropped.
    ArmPin off(heurarm::LINE_REPAY, false);
    CHECK_FALSE(ScourgeLinePays(s));
}

TEST_CASE("Line repay: {Urza's Incubator, Lathliss} is on the menu (the sequential walk shares the payer)")
{
    EnsureCardsLoaded();
    const GameState s = IncubatorBoard();
    {
        ArmPin on(heurarm::LINE_REPAY, true);
        CHECK(OffersIncubatorLathliss(s));
    }
    ArmPin off(heurarm::LINE_REPAY, false);
    CHECK_FALSE(OffersIncubatorLathliss(s));
}

TEST_CASE("Line repay: a line the board cannot pay by amount is left alone")
{
    EnsureCardsLoaded();
    // Without the second Mountain the board makes five mana against Mind Stone {2} + Scourge {R}{R}{R}: short by
    // one. No re-payment can conjure mana, so the plan stays dropped with the lever on.
    GameState s = ScourgeBoard();
    s.battlefield.pop_back();
    ArmPin on(heurarm::LINE_REPAY, true);
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.actions.size() != 2 || !PlanCasts(p, { "Scourge of Valkas", "Mind Stone" })) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK_FALSE((OnBattlefield(t, "Scourge of Valkas", 10) && OnBattlefield(t, "Mind Stone", 11)));
    }
}

TEST_CASE("Line repay: a re-made payment cannot use what entered after it (Wild Growth does not pay for itself)")
{
    EnsureCardsLoaded();
    // One Forest makes one {G}: Wild Growth ({G}) or Llanowar Elves ({G}), never both. Re-making Wild Growth's
    // payment on a board that still held Wild Growth (enchanting the Forest it was paid with) drew its own extra {G}
    // and paid the Elves too: mana from nowhere (Bruna, MTG_LINE_REPAY_MEMO_VERIFY 2026-11-03). The re-made payment
    // runs on the board it was made on, so the line stays dropped.
    GameState s = BaseBoard();
    s.deck_has_land_aura = true;
    Put(s, "Forest", 1);
    Hand(s, "Wild Growth", 10);
    Hand(s, "Llanowar Elves", 11);
    ArmPin on(heurarm::LINE_REPAY, true);
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!PlanCasts(p, { "Wild Growth", "Llanowar Elves" })) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK_FALSE((OnBattlefield(t, "Wild Growth", 10) && OnBattlefield(t, "Llanowar Elves", 11)));
    }
}

TEST_CASE("Line repay: a land Aura paid under its host hold can still be re-made (EDF gi323 T2)")
{
    EnsureCardsLoaded();
    // EldraziDisplacerFlicker seed 1870324 gi323 T2: Brushland (Wild Growth on it) and a fresh untapped Mariposa
    // Military Base ({C}); Wild Growth #2 (onto Mariposa) and Eldrazi Displacer {2}{W} in hand. Four mana: tap
    // Brushland for {W} (+ the old Wild Growth's {G}), pay the new Wild Growth with that {G} and float the {W}; then
    // Mariposa's {C} + the new Wild Growth's {G} + the {W} pay the Displacer. The per-cast payer taps Brushland for {G}
    // instead, so the Displacer needs the line repay. Every land-Aura cast site holds the Aura's host (Mariposa) tapped
    // across the Aura's own payment; the repay record was pushed INSIDE that hold, on a board with Mariposa tapped
    // that the line never stands on again, so the Displacer's payment could not find it. The search credited the line
    // from records pushed without the hold and the executor dropped the Displacer (EDF +1 at LINE_REPAY on, games
    // 323 / 365 / 935). LineRepayHostUntap re-states the record on the released board.
    static const EldraziFlickerProvider prov;
    GameState s;
    s.m_provider          = &prov;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_land_aura  = true;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def(k % 2 == 0 ? "Peregrine Drake" : "Brushland").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Brushland", 9);
    Put(s, "Wild Growth", 55);
    s.battlefield.back().aura_attached_to = 9;
    Put(s, "Mariposa Military Base", 35, /*entered_this_turn=*/true);
    Hand(s, "Wild Growth", 56);
    Hand(s, "Eldrazi Displacer", 18);
    auto line_pays = [&](bool on) -> int   // -1: no such plan on the menu; 0: dropped; 1: both cast
    {
        ArmPin pin(heurarm::LINE_REPAY, on);
        LineRepayClearPerGame();
        int seen = -1;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            int wg = -1, ed = -1;
            for (std::size_t i = 0; i < p.actions.size(); ++i)
            {
                const Action& x = p.actions[i];
                if (x.kind != Action::Kind::CastFromHand) { continue; }
                if (x.card_name.str() == "Wild Growth" && x.enchant_target == 35) { wg = static_cast<int>(i); }
                if (x.card_name.str() == "Eldrazi Displacer") { ed = static_cast<int>(i); }
            }
            if (wg < 0 || ed < 0 || wg > ed || p.actions.size() != 2) { continue; }
            LineRepayClearPerGame();
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            seen = OnBattlefield(t, "Wild Growth", 56) && OnBattlefield(t, "Eldrazi Displacer", 18) ? 1 : 0;
            if (seen == 1) { break; }
        }
        return seen;
    };
    CHECK(line_pays(true) == 1);
    CHECK(line_pays(false) != 1);   // off: the per-cast payer strands the {W} (or the menu never offers the line)
}

TEST_CASE("Line repay: a held Aura host remains findable for a previous payment")
{
    EnsureCardsLoaded();
    ArmPin on(heurarm::LINE_REPAY, true);
    auto second_pays = [&](bool scope_host)
    {
        GameState s = BaseBoard();
        s.deck_has_land_aura = true;
        Put(s, "Razorverge Thicket", 1); // the host, held while the second Aura pays
        Put(s, "Forest", 2);
        Put(s, "Plains", 3); // enough total mana remains; only green is stranded
        ManaCost g;
        g.green = 1;
        ManaCost total = g;
        total.green = 2;
        LineRepayClearPerGame();
        LineUnpaidCostScope line(total);
        const auto before = LineRepayMark();
        REQUIRE(TapForCostDirect(s, g, false));
        CHECK(LineRepayMark() == before + 1);
        SubManaCost(g_line_unpaid_cost, g);
        CHECK_FALSE(s.battlefield[0].tapped); // the first payment chose Forest
        CHECK(s.battlefield[1].tapped);
        s.battlefield[0].tapped = true; // temporary hold on the second Aura's host
        bool host_tapped_by_repay = false;
        bool paid = false;
        if (scope_host)
        {
            LineRepayHeldHostScope held(1, &host_tapped_by_repay);
            paid = TapForCostDirect(s, g, false);
        }
        else { paid = TapForCostDirect(s, g, false); }
        if (paid)
        {
            CHECK(host_tapped_by_repay == scope_host);
            CHECK(s.battlefield[0].tapped); // the re-made first payment really tapped the host
            CHECK(s.battlefield[1].tapped); // the second payment used Forest
            CHECK_FALSE(s.battlefield[2].tapped);
        }
        return paid;
    };
    CHECK_FALSE(second_pays(false)); // pre-fix find sees the temporary tap and misses the record
    CHECK(second_pays(true));
}
