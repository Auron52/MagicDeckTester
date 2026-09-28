// Unit tests for the Prevent Damage onboarding, phase I1 (2026-09-27): the damage-event core in
// src/core/DamageEvents.h. Every number here is a RULES number read off the cards' Oracle text and
// rulings, pinned exactly -- the per-EVENT granularity is observable (Dina drains per event, Bilbo
// adds per event), so an aggregate that sums right but counts events wrong must fail here.
//
//  1. Tamanoa gains on a painland's coloured tap; nothing on its painless {C} tap.
//  2. Rhox Faithmender doubles every lifegain event; two multiply (x4); Bilbo's +1 goes FIRST.
//  3. Vito drains "that much" (the REPLACED amount); Dina drains 1 per EVENT.
//  4. Manabarbs: one damage event per land tap -> one Tamanoa trigger each -> one Dina drain each.
//  5. Lethal self-damage LOSES even with Tamanoa out (the SBA runs before the trigger resolves), and
//     the loss is sticky (a later gain cannot revive us).
//  6. Purity prevents the pain and gains instead; prevented damage is not dealt, so no Tamanoa.
//  7. Ancient Tomb deals 2 on its {C}{C} tap (tap_self_damage_any_mode).
//  8. Both players to 0 in one event is a DRAW (CR 104.4a) -- not a win.
//  9. An UNARMED board is the legacy model: raw pain, no triggers.
// 10. Routing: the deck lands on PreventDamageProvider, and the signature survives any one cut.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "deck/DeckLoader.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>

namespace
{

void EnsureCardsPd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const PreventDamageProvider& Prov()
{
    static const PreventDamageProvider p;
    return p;
}

// An ARMED board (what StampDeckTraits stamps for this deck), PreventDamageProvider attached.
GameState Board(bool armed = true)
{
    EnsureCardsPd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.dmg_events_armed    = armed;
    s.own_death_live      = armed;
    s.m_provider          = &Prov();
    return s;
}

int g_num = 1;
void Put(GameState& s, const std::string& name, int controller = 0)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = d->card;
    p.card.m_number     = g_num++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

ManaCost Cost(int generic, int w = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.black = b; c.red = r; c.green = g;
    return c;
}

bool Pay(GameState& s, const ManaCost& c)
{ return TapForCostShared(s, c, /*for_creature=*/false, /*available=*/nullptr, /*honor_legacy_cco=*/true); }

int Me(const GameState& s)  { return s.players[0].life; }
int Opp(const GameState& s) { return s.players[1].life; }

}   // namespace

TEST_CASE("Prevent Damage: Tamanoa gains on a painland's coloured tap, not on its painless {C} tap")
{
    GameState s = Board();
    Put(s, "Tamanoa");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, /*r=*/1)));   // {R}: the coloured mode -- 1 damage, Tamanoa +1
    CHECK(Me(s) == 20);
    CHECK(s.players[0].life_gained_this_turn == 1);
    for (const Permanent& p : s.battlefield) { CHECK(p.mana_tap_mark == 0); }   // flushed

    GameState c = Board();
    Put(c, "Tamanoa");
    Put(c, "Battlefield Forge");
    Put(c, "Vito, Thorn of the Dusk Rose");
    // Vito makes the Tamanoa trigger visible on the opponent: 1 gained -> opponent loses 1.
    REQUIRE(Pay(c, Cost(0, 0, 0, 1)));
    CHECK(Opp(c) == 19);

    // A GENERIC pip: with a provider that does not want our damage (Generic) the painland takes its
    // separate painless {C} ability -- no damage, no Tamanoa trigger...
    GameState g = Board();
    g.m_provider = &DefaultProvider();
    Put(g, "Tamanoa");
    Put(g, "Battlefield Forge");
    REQUIRE(Pay(g, Cost(1)));
    CHECK(Me(g) == 20);
    CHECK(g.players[0].life_gained_this_turn == 0);
    // ...while PreventDamageProvider (SelfDamageUseful with Tamanoa out) takes the damaging mode.
    GameState d = Board();
    Put(d, "Tamanoa");
    Put(d, "Battlefield Forge");
    REQUIRE(Pay(d, Cost(1)));
    CHECK(Me(d) == 20);
    CHECK(d.players[0].life_gained_this_turn == 1);
}

TEST_CASE("Prevent Damage: Faithmender doubles, two multiply, Bilbo's +1 applies first")
{
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 26);
        CHECK(s.players[0].life_gained_this_turn == 6);
    }
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        Put(s, "Rhox Faithmender");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 32);   // x4
    }
    {
        GameState s = Board();
        Put(s, "Rhox Faithmender");
        Put(s, "Bilbo, Birthday Celebrant");
        GainLife(s, 0, 3);
        CHECK(Me(s) == 28);   // (3+1)*2, never 3*2+1
    }
    {
        // Tamanoa + Faithmender on a painland tap: -1, then Tamanoa's 1 doubled to 2.
        GameState s = Board();
        Put(s, "Tamanoa");
        Put(s, "Rhox Faithmender");
        Put(s, "Battlefield Forge");
        REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
        CHECK(Me(s) == 21);
    }
}

TEST_CASE("Prevent Damage: Vito drains the replaced amount; Dina drains 1 per event")
{
    GameState s = Board();
    Put(s, "Rhox Faithmender");
    Put(s, "Vito, Thorn of the Dusk Rose");
    Put(s, "Dina, Soul Steeper");
    GainLife(s, 0, 3);   // one event of 6
    CHECK(Opp(s) == 20 - 6 - 1);
    GainLife(s, 0, 1);   // a second event of 2
    CHECK(Opp(s) == 13 - 2 - 1);
    CHECK(s.opponent_lost_life_this_turn);
}

TEST_CASE("Prevent Damage: two Manabarbs land taps are two events -> two Dina drains")
{
    GameState s = Board();
    Put(s, "Manabarbs");
    Put(s, "Tamanoa");
    Put(s, "Dina, Soul Steeper");
    Put(s, "Forest");
    Put(s, "Forest");
    REQUIRE(Pay(s, Cost(0, 0, 0, 0, /*g=*/2)));
    CHECK(Me(s) == 20);        // -1 +1, twice
    CHECK(Opp(s) == 18);       // one drain per lifegain EVENT
    // Without Tamanoa the barbs are pure damage and nothing is drained.
    GameState t = Board();
    Put(t, "Manabarbs");
    Put(t, "Dina, Soul Steeper");
    Put(t, "Forest");
    Put(t, "Forest");
    REQUIRE(Pay(t, Cost(0, 0, 0, 0, 2)));
    CHECK(Me(t) == 18);
    CHECK(Opp(t) == 20);
}

TEST_CASE("Prevent Damage: lethal self-damage LOSES even with Tamanoa out, and stays lost")
{
    GameState s = Board();
    s.players[0].life = 1;
    Put(s, "Tamanoa");
    Put(s, "Rhox Faithmender");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));   // {R}: 1 damage takes us to 0 before Tamanoa resolves
    CHECK(Me(s) <= 0);
    CHECK(SelfHasLost(s));
    CHECK(s.players[0].life_gained_this_turn == 0);   // the trigger never resolved
    GainLife(s, 0, 50);                               // a later gain cannot revive us
    CHECK(SelfHasLost(s));
    s.players[1].life = 0;                            // ...and a later opponent death is no win
    CHECK_FALSE(OpponentHasLost(s));
}

TEST_CASE("Prevent Damage: Purity prevents the pain and gains; Tamanoa does NOT trigger on it")
{
    GameState s = Board();
    Put(s, "Purity");
    Put(s, "Tamanoa");
    Put(s, "Vito, Thorn of the Dusk Rose");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
    CHECK(Me(s) == 21);    // no damage dealt; Purity's gain of 1
    CHECK(Opp(s) == 19);   // ONE lifegain event (Purity's) -- a Tamanoa trigger would make it 18

    // And a Manabarbs damage event is prevented the same way.
    GameState t = Board();
    Put(t, "Purity");
    Put(t, "Manabarbs");
    Put(t, "Forest");
    REQUIRE(Pay(t, Cost(0, 0, 0, 0, 1)));
    CHECK(Me(t) == 21);
}

TEST_CASE("Prevent Damage: Ancient Tomb deals 2 on its {C}{C} tap")
{
    GameState s = Board();
    Put(s, "Ancient Tomb");
    REQUIRE(Pay(s, Cost(2)));
    CHECK(Me(s) == 18);
    GameState t = Board();
    Put(t, "Tamanoa");
    Put(t, "Ancient Tomb");
    REQUIRE(Pay(t, Cost(2)));
    CHECK(Me(t) == 20);    // one 2-damage event, one Tamanoa trigger for 2
    // Its signature separates it from a painless {C}{C} land.
    CHECK(CardDatabase::Instance().Lookup("Ancient Tomb")->params.tap_self_damage_any_mode);
}

TEST_CASE("Prevent Damage: both players to 0 in one event is a DRAW, not a win")
{
    GameState s = Board();
    s.players[0].life = 1;
    s.players[1].life = 1;
    Put(s, "Tamanoa");
    dmgev::DealDamageEvent(s, 0, /*src_is_creature=*/false, /*combat=*/false,
                           /*to_self=*/1, /*to_opp=*/1, 0, "test sweeper");
    CHECK(SelfHasLost(s));
    CHECK_FALSE(OpponentHasLost(s));
    // Contrast: the opponent dying FIRST is a win, and later damage to us is a no-op (won-lock).
    GameState w = Board();
    w.players[1].life = 1;
    w.players[0].life = 1;
    dmgev::DealDamageEvent(w, 0, false, false, 0, 1, 0, "test burn");
    CHECK(OpponentHasLost(w));
    dmgev::DealDamageEvent(w, 0, false, false, 5, 0, 0, "test self-hit");
    CHECK(Me(w) == 1);
    CHECK(OpponentHasLost(w));
}

TEST_CASE("Prevent Damage: an UNARMED board keeps the legacy model byte-for-byte")
{
    GameState s = Board(/*armed=*/false);
    Put(s, "Tamanoa");
    Put(s, "Rhox Faithmender");
    Put(s, "Battlefield Forge");
    REQUIRE(Pay(s, Cost(0, 0, 0, 1)));
    CHECK(Me(s) == 19);     // raw pain, no trigger
    GainLife(s, 0, 3);
    CHECK(Me(s) == 22);     // no replacement
    for (const Permanent& p : s.battlefield) { CHECK(p.mana_tap_mark == 0); }
}

TEST_CASE("Prevent Damage routing: PreventDamageProvider, and the signature survives any one cut")
{
    EnsureCardsPd();
    const Decklist full = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Prevent Damage/Prevent Damage.cod"));
    CHECK(std::string(DetectDecisionProvider(full).Name()) == "PreventDamage");
    for (const char* cut : {"Tamanoa", "Manabarbs", "Vito, Thorn of the Dusk Rose", "Dina, Soul Steeper"})
    {
        Decklist d = full;
        d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(),
                                         [&](const Card& c) { return c.m_name.str() == cut; }),
                          d.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(d).Name()) == "PreventDamage", "cutting ", cut);
    }
}

// ---- PAIN-AWARE PAYMENT (dmgev::PainAwarePay, MTG_PD_PAIN_PAY; claude-play sweep 2026-09-27) ----
namespace
{
struct PainPayArm
{
    std::int8_t prev;
    explicit PainPayArm(bool on) : prev(heurarm::t_arm[heurarm::PD_PAIN_PAY])
    { heurarm::t_arm[heurarm::PD_PAIN_PAY] = on ? 1 : 0; }
    ~PainPayArm() { heurarm::t_arm[heurarm::PD_PAIN_PAY] = prev; }
};

// Sweep gi9's board: 5 life, Tamanoa + Vito out, the Citadels first in battlefield order.
GameState Gi9Board()
{
    GameState s = Board();
    s.players[0].life = 5;
    Put(s, "Tarnished Citadel");
    Put(s, "Tarnished Citadel");
    Put(s, "Reflecting Pool");
    Put(s, "Tamanoa");
    Put(s, "Grand Coliseum");
    Put(s, "Vito, Thorn of the Dusk Rose");
    Put(s, "Battlefield Forge");
    return s;
}
}   // namespace

TEST_CASE("Prevent Damage pain-aware payment: a COLOURED payment never pays itself dead (sweep gi9)")
{
    // CONTROL ARM (the lever off = the pain-blind payer): Dina's {B}{G} goes on both Citadels in
    // their coloured mode, 3 + 3 = 6 >= 5 -- the SBA kills us before either Tamanoa gain resolves.
    // This arm MUST lose, or the test below has no power.
    {
        PainPayArm off(false);
        GameState s = Gi9Board();
        REQUIRE(Pay(s, Cost(0, 0, /*b=*/1, 0, /*g=*/1)));
        CHECK(SelfHasLost(s));
    }
    // The fix: the same payment survives (a Citadel + the painless Pool, or Coliseum + a Citadel),
    // and -- a gain engine being out -- it still takes pain, which Tamanoa pays back and Vito drains.
    PainPayArm on(true);
    GameState s = Gi9Board();
    REQUIRE(Pay(s, Cost(0, 0, 1, 0, 1)));
    CHECK_FALSE(SelfHasLost(s));
    CHECK(Me(s) >= 5);            // pain p then +p from Tamanoa
    CHECK(Opp(s) < 20);           // the pain was USED: Vito drained the gain
    for (const Permanent& p : s.battlefield) { CHECK(p.mana_tap_mark == 0); }   // flushed
}

TEST_CASE("Prevent Damage pain-aware payment: with NO gain engine the minimum-pain assignment (sweep gi2)")
{
    // {1}{W} off Battlefield Forge + City of Brass. Pain-blind: {W} on the Forge (1) and the generic
    // on City (its tap always hurts, 1) = 2. Minimal: City pays the {W} (1), the Forge its painless
    // {C} (0) = 1.
    auto board = []()
    {
        GameState s = Board();
        Put(s, "Battlefield Forge");
        Put(s, "City of Brass");
        return s;
    };
    {
        PainPayArm off(false);
        GameState s = board();
        REQUIRE(Pay(s, Cost(1, /*w=*/1)));
        CHECK(Me(s) == 18);   // the control arm reproduces the sweep's extra life
    }
    PainPayArm on(true);
    GameState s = board();
    REQUIRE(Pay(s, Cost(1, 1)));
    CHECK(Me(s) == 19);
}

TEST_CASE("Prevent Damage pain-aware payment: Manabarbs hits count when nothing pays them back")
{
    // No Tamanoa, Manabarbs out, a painless land and a painland: {G} from Forest costs the barb (1);
    // the painland's coloured mode would cost barb + pain. Either way the minimum is ONE land tap.
    PainPayArm on(true);
    GameState s = Board();
    Put(s, "Manabarbs", /*controller=*/1);
    Put(s, "Karplusan Forest");
    Put(s, "Forest");
    REQUIRE(Pay(s, Cost(0, 0, 0, 0, /*g=*/1)));
    CHECK(Me(s) == 19);   // Forest: 0 pain + 1 barb (Karplusan {G} would be 1 + 1)
}

TEST_CASE("Prevent Damage pain-aware payment: when EVERY assignment is lethal the payment is still made")
{
    // The plan is a suicide: the self-lethal guard / kOwnDeath own that, not the payer (which must
    // not report a payable cost unpayable).
    PainPayArm on(true);
    GameState s = Board();
    s.players[0].life = 2;
    Put(s, "Tamanoa");
    Put(s, "Tarnished Citadel");
    REQUIRE(Pay(s, Cost(0, 0, 1)));
    CHECK(SelfHasLost(s));
}

// ---- The pain sweep runs ONCE, at the true end of main 1 (claude-play sweep gi0 / gi7) ----
#include "ai/TurnSolver.h"
TEST_CASE("Prevent Damage: the pain sweep is deferred while human play is still applying the phase")
{
    auto board = []()
    {
        GameState s = Board();
        Put(s, "Tamanoa");
        Put(s, "Battlefield Forge");
        return s;
    };
    auto forge_tapped = [](const GameState& s)
    {
        for (const Permanent& p : s.battlefield)
        { if (p.card.m_name.str() == "Battlefield Forge") { return p.tapped; } }
        return false;
    };
    // An applied plan that ends main 1 sweeps (the rollout / search / breakpoint-resume contract)...
    {
        GameState s = board();
        TurnSolver::ApplyPlan(s, TurnSolver::Plan{}, /*is_pre_combat=*/true);
        CHECK(forge_tapped(s));
    }
    // ...but the human-play loop applies the phase plan by plan and holds the scope: a plan applied
    // under it leaves the lands for the human's next pick (gi7: Karplusan, the only red source).
    GameState s = board();
    {
        PainSweepDeferScope defer;
        TurnSolver::ApplyPlan(s, TurnSolver::Plan{}, true);
        CHECK_FALSE(forge_tapped(s));
    }
    TapPainSourcesIfUseful(s, 0);   // the loop's one explicit sweep when the phase ends
    CHECK(forge_tapped(s));
}

// ---- The viewer valve bounds PAYABLE positions, not the raw odometer (claude-play sweep gi4) ----
#include "ai/EngineFlags.h"
TEST_CASE("Viewer plan-cap estimate: an unpayable tutor fan does not count against the bound")
{
    // gi4's T6 shape: two Beseech the Queen digits of 68 variants (MV 3..6), Faithmender / Dina /
    // Spellshock singletons, Green Sun's Zenith (2 variants), 4 lands up (bound 4).
    std::vector<int> beseech;
    for (int k = 0; k < 68; ++k) { beseech.push_back(3 + (k % 4)); }
    const std::vector<std::vector<int>> g = { {4}, beseech, {2}, {3}, beseech, {3, 4} };
    const std::pair<double, double> e = viewerplancap::Estimate(g, 0, 4);
    CHECK(e.first == doctest::Approx(114264.0));          // the raw product the old valve read
    CHECK(e.second < viewerplancap::Positions());         // what the walk can actually materialise
    CHECK(e.second < 200.0);
    // A usable bound is required; without one the estimate is the raw product (conservative).
    CHECK(viewerplancap::Estimate(g, 0, -1).second == doctest::Approx(e.first));
}

TEST_CASE("Prevent Damage pain-aware payment: the damage floor is EXACT on a simple board")
{
    // The floor the payer's one-retry path trusts (PaymentDamageFloor's DP). gi2's {1}{W}: 1.
    GameState s = Board();
    Put(s, "Battlefield Forge");
    Put(s, "City of Brass");
    std::pair<int, bool> f = PaymentDamageFloor(s, Cost(1, 1), ManaPool{}, false, 0);
    CHECK(f.second);
    CHECK(f.first == 1);
    // + Ancient Tomb and a Reflecting Pool, {3}{W}: Tomb {C}{C} (2) + Pool {W} (0) + Forge {C} (0) = 2.
    Put(s, "Ancient Tomb");
    Put(s, "Reflecting Pool");
    f = PaymentDamageFloor(s, Cost(3, 1), ManaPool{}, false, 0);
    CHECK(f.second);
    CHECK(f.first == 2);
    // The same board under an opponent's Manabarbs with no Tamanoa (barbs counted, 1 per land tap):
    // 3 land taps (Tomb makes 2) + 2 pain = 5.
    f = PaymentDamageFloor(s, Cost(3, 1), ManaPool{}, true, 1);
    CHECK(f.second);
    CHECK(f.first == 5);
    // And the payer realises it.
    GameState t = s;
    Put(t, "Manabarbs", 1);
    REQUIRE(Pay(t, Cost(3, 1)));
    CHECK(Me(t) == 15);
}
