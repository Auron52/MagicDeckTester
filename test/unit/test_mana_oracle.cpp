// The MANA COMPLETENESS ORACLE's mana table (manaoracle::MinimalPayments; docs/design/mana-completeness-oracle.md).
//
// The oracle's whole claim is that its payment enumeration is COMPLETE: every way to pay is listed. Its
// runtime self-check (I1, "the engine's own outcome is among the oracle's") catches a gap only on a board
// some game happens to reach, so the shapes that once escaped it are pinned here: a land Aura's bonus on a
// painland's painless {C} face, a fed filter whose feeder is also a payer, Generator Servant's
// haste-tagged {C} kept floating, and an optimistic cover whose real payment fails.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaOracle.h"
#include "ai/TurnSolver.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
void EnsureOracleCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A board of named permanents with DISTINCT ids (the oracle names each tap by Card::m_number).
GameState Board(const std::vector<std::string>& names)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    int num = 1;
    for (const std::string& n : names)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", n);
        Permanent p;
        p.card              = def->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        s.battlefield.push_back(p);
    }
    return s;
}

ManaCost Cost(int generic, int w = 0, int u = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}

// "Name:FACE" for every tap of an option, sorted -- an order-free description to assert on.
std::vector<std::string> Taps(const manaoracle::PayOption& o)
{
    static const char kL[] = "WUBRGC";
    std::vector<std::string> v;
    for (const TurnSolver::PreTap& t : o.taps)
    {
        std::string f;
        if (!t.combo.empty()) { for (int c : t.combo) { f.push_back(kL[c]); } }
        else if (t.color >= 0) { f.push_back(kL[t.color]); }
        v.push_back(t.name + ":" + f);
    }
    std::sort(v.begin(), v.end());
    return v;
}

bool HasOption(const std::vector<manaoracle::PayOption>& opts, std::vector<std::string> want)
{
    std::sort(want.begin(), want.end());
    for (const manaoracle::PayOption& o : opts) { if (Taps(o) == want) { return true; } }
    return false;
}
} // namespace

TEST_CASE("mana oracle: a painland pays a {G} land Aura cast painlessly through its {C} face + the Aura's {G}")
{
    EnsureOracleCards();
    // Brushland enchanted by Wild Growth: {T}: {C} is painless, and Wild Growth adds {G}{G} on ANY tap
    // for mana -- so {G} is payable without the land's 1 damage. The EDF ORACLE-GAP of 2026-10-26
    // was this option missing (the Aura host was reserved by the engine's heuristic).
    GameState s = Board({ "Brushland", "Wild Growth" });
    s.battlefield[1].aura_attached_to = s.battlefield[0].card.m_number;
    const auto opts = manaoracle::MinimalPayments(s, Cost(0, 0, 0, 0, 0, 1), /*for_creature=*/false, nullptr);
    CHECK(HasOption(opts, { "Brushland:C" }));
}

TEST_CASE("mana oracle: a fed filter whose feeder also pays is offered (the converter cannot be fed later)")
{
    EnsureOracleCards();
    // Island + Cascade Bluffs paying {U}: Island alone pays, but "Island feeds Bluffs -> {U}{U}" pays AND
    // floats a {U} -- and once the Island is tapped Bluffs has no feeder, so it is not "the same as tapping
    // Bluffs later". The NivMizzetCombo Ponder/Preordain ORACLE-GAP was this option missing.
    const GameState s = Board({ "Island", "Cascade Bluffs" });
    const auto opts = manaoracle::MinimalPayments(s, Cost(0, 0, 1), false, nullptr);
    CHECK(HasOption(opts, { "Island:U" }));
    CHECK(HasOption(opts, { "Island:U", "Cascade Bluffs:UU" }));
}

TEST_CASE("mana oracle: Generator Servant's haste-tagged {C} can be kept floating for a later creature spell")
{
    EnsureOracleCards();
    // Two haste-tagged {C} floating (a cracked Servant) and an Island; a noncreature {1}. Spending a
    // tagged {C} here costs a later creature its haste, so paying with the Island while BOTH stay
    // floating must be one of the options -- the NivMizzetCombo DOMINATED-HASTE shape.
    GameState s = Board({ "Island" });
    s.floating_mana.colorless = 2;
    s.floating_haste_c        = 2;
    const auto opts = manaoracle::MinimalPayments(s, Cost(1), false, nullptr);
    bool kept_both = false;
    for (const manaoracle::PayOption& o : opts)
    { if (Taps(o) == std::vector<std::string>{ "Island:U" } && o.stash_haste == 2) { kept_both = true; } }
    CHECK(kept_both);
    // ...and simply spending the float is one too.
    CHECK(HasOption(opts, {}));
}

TEST_CASE("mana oracle: every listed option really pays (verified), and none is listed twice")
{
    EnsureOracleCards();
    const GameState s = Board({ "Forest", "Forest", "Island", "Brushland" });
    const ManaCost c = Cost(1, 0, 0, 0, 0, 1);
    const auto opts = manaoracle::MinimalPayments(s, c, false, nullptr);
    REQUIRE(!opts.empty());
    for (const manaoracle::PayOption& o : opts)
    {
        GameState sc = s;
        for (const TurnSolver::PreTap& t : o.taps) { REQUIRE(ApplyHumanPreTap(sc, t).empty()); }
        manaoracle::t_float_only = true;
        const bool paid = TapForCostShared(sc, c, false, nullptr, true);
        manaoracle::t_float_only = false;
        CHECK(paid);
    }
    // The two Forests are one class: "Forest + Island" appears once, not once per Forest.
    int forest_island = 0;
    for (const manaoracle::PayOption& o : opts)
    { if (Taps(o) == std::vector<std::string>{ "Forest:G", "Island:U" }) { ++forest_island; } }
    CHECK(forest_island == 1);
}

// Found by the oracle (2026-10-27; docs/design/mana-completeness-oracle.md section 7.1): on an UNARMED
// board TapSourceIntoFloat used to treat every {C} tap of a land with a {C} mode as a painland's painless
// ability and never read tap_self_damage_any_mode, so the payment and the pre-tap took Ancient Tomb for
// free (a real Fungus game: tapped, life stays 20) while the search's plan application charged it -- the
// source of every Fungus DOMINATED-LIFE "+2 life". Both now go through dmgev::PainForTap.
TEST_CASE("mana oracle: a scripted Ancient Tomb tap deals its 2 damage, as the engine's own payment does")
{
    EnsureOracleCards();
    // Ancient Tomb hurts in EVERY mode (tap_self_damage_any_mode): its {C}{C} is not a painland's
    // separate painless {C}. A pre-tap that skipped the damage would make every oracle payment through
    // the Tomb look 2 life better than the engine's, i.e. a false DOMINATED-LIFE.
    GameState eng = Board({ "Ancient Tomb" });
    REQUIRE(TapForCostShared(eng, Cost(2), false, nullptr, true));
    GameState pre = Board({ "Ancient Tomb" });
    const TurnSolver::PreTap t{ "Ancient Tomb", 1, static_cast<int>(Color::Colorless) };
    REQUIRE(ApplyHumanPreTap(pre, t).empty());
    CHECK(eng.players[0].life == 18);
    CHECK(pre.players[0].life == eng.players[0].life);
}

namespace
{
// The EDF viewer game seed 3000, turn 4 board: Conservatory, Mariposa Military Base enchanted by Fertile
// Ground, Shivan Gorge, with Training Grounds ({U}) and Eldrazi Displacer ({2}{W}) in hand.
GameState Edf3000T4()
{
    static const EldraziFlickerProvider prov;
    GameState s = Board({ "Conservatory", "Mariposa Military Base", "Shivan Gorge", "Fertile Ground" });
    s.battlefield[3].aura_attached_to = 2;
    s.deck_has_land_aura = true;
    s.m_provider = &prov;
    s.players[0].life = 20;
    s.players[1].life = 20;
    int n = 100;
    for (const char* nm : { "Training Grounds", "Eldrazi Displacer", "Cloud of Faeries", "Fertile Ground" })
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number = n++;
        s.players[0].hand.push_back(c);
    }
    for (int i = 0; i < 20; ++i)
    { Card c = CardDatabase::Instance().Lookup("Brushland")->card; c.m_number = 300 + i; s.players[0].library.push_back(c); }
    return s;
}
Action CastAction(const GameState& s, const char* nm)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d->card.m_name;
    a.def            = d;
    a.cost           = d->card.m_mana_cost;
    a.is_noncreature = !d->card.IsCreature();
    a.card_mv        = d->card.m_mana_cost.ManaValue();
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == nm) { a.hand_index = i; break; } }
    return a;
}
} // namespace

// THE VIEWER'S "CAN THE SEARCH FIND THIS LINE" (manaoracle::AuditLine). On this board the engine's own
// payment of "Training Grounds, then Eldrazi Displacer" drops the Displacer -- the backtracker over-tap
// pinned below spends Conservatory's {W} on the {U} -- while Mariposa's bonus {U} alone pays the Grounds
// and leaves Conservatory for the Displacer. So the search can never score this line, the board can pay
// it, and the rollback has to reach back one decision: the Grounds' payment (#1) before the Displacer's
// (#2), which is the one that failed. Two levers now pay it, each on its own: the per-cast give-back
// (MTG_PAY_SHRINK) fixes that over-tap, and the whole-turn prepay (MTG_PREPAY_COLOURS) settles Fertile
// Ground's any-colour bonus to {U} and pays both casts jointly -- and since 2026-10-28 the per-payment refine
// (MTG_PAY_REFINE) gives the over-tapped Conservatory back too. So the case runs with all three off, the
// historical payer; the next test is the fixed one.
TEST_CASE("viewer line check: the search's payment drops a cast the board pays for, and says how to pay it")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Training Grounds"), CastAction(s, "Eldrazi Displacer") };
    plan.land_decided = true;
    const std::int8_t saved    = heurarm::t_arm[heurarm::PAY_SHRINK];
    const std::int8_t saved_pc = heurarm::t_arm[heurarm::PREPAY_COLOURS];
    const std::int8_t saved_rf = heurarm::t_arm[heurarm::PAY_REFINE];
    heurarm::t_arm[heurarm::PAY_SHRINK]     = 0;
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = 0;
    heurarm::t_arm[heurarm::PAY_REFINE]     = 0;
    const manaoracle::LineAudit a = manaoracle::AuditLine(s, /*pre=*/true, plan);
    heurarm::t_arm[heurarm::PAY_SHRINK]     = saved;
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = saved_pc;
    heurarm::t_arm[heurarm::PAY_REFINE]     = saved_rf;
    REQUIRE(a.audited);
    CHECK_FALSE(a.engine_full);
    CHECK(a.sibling == -1);
    CHECK(a.dropped == "cast Eldrazi Displacer");
    REQUIRE(a.payable);
    CHECK(a.other_order.empty());
    REQUIRE(a.pays.size() == 2);
    CHECK(a.pays[0].what == "{U}");
    CHECK(a.pays[1].what == "{2}{W}");
    // The Grounds' {U} comes from Mariposa alone (its {C} face plus Fertile Ground's {U}); Conservatory is
    // left for the Displacer.
    REQUIRE(a.pays[0].taps.size() == 1);
    CHECK(a.pays[0].taps[0].rfind("tap=Mariposa Military Base#2:", 0) == 0);
    CHECK(a.rollback == "depth1");
    CHECK(a.keep == 0);
    CHECK(a.failed == 1);
    CHECK_FALSE(a.later);
    // ...and nothing it did leaked: the board it was asked about is untouched.
    for (const Permanent& p : s.battlefield) { CHECK_FALSE(p.tapped); }
}

TEST_CASE("viewer line check: with the give-back the search's own payment casts the EDF seed 3000 T4 line")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Training Grounds"), CastAction(s, "Eldrazi Displacer") };
    plan.land_decided = true;
    const manaoracle::LineAudit a = manaoracle::AuditLine(s, /*pre=*/true, plan);
    REQUIRE(a.audited);
    CHECK(a.engine_full);
    CHECK(a.dropped.empty());
}

// MTG_REAL_PAY_SCRIPT never pays a DROP: the search scored the plan by the engine's own apply, the drop included,
// so paying the dropped cast would play a line nobody evaluated (the A/B: 17 slower, 0 faster, 15 of them drops).
// The historical payer (the give-back, the joint colours and the refine off) drops Eldrazi Displacer here; no
// script is offered, and the fixed payer casts both on its own.
TEST_CASE("executor payment script: a dropped cast gets no script -- the drop is the search's apply to fix (MTG_REAL_PAY_SCRIPT)")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Training Grounds"), CastAction(s, "Eldrazi Displacer") };
    plan.land_decided = true;
    heurarm::t_arm[heurarm::PAY_SHRINK]     = 0;
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = 0;
    heurarm::t_arm[heurarm::PAY_REFINE]     = 0;
    std::vector<manaoracle::PayOption> script;
    const bool got = manaoracle::PlanPaymentScript(s, plan, /*pre=*/true, script);
    heurarm::t_arm[heurarm::PAY_SHRINK]     = -1;
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = -1;
    heurarm::t_arm[heurarm::PAY_REFINE]     = -1;
    CHECK_FALSE(got);
    CHECK_FALSE(manaoracle::PlanPaymentScript(s, plan, /*pre=*/true, script));
}

// MTG_REAL_PAY_SCRIPT's dominating script: a LINE-level over-tap the per-payment refine must refuse. Angels seed
// 1100025 gi 24 T2 (census DOMINATED-LAND): Remote Farm up, Seraph Sanctuary to play, Lightning Greaves {2}. The
// greedy pays Sanctuary's {C} and Farm's {W}{W}, floating a {W}; per payment that {W} is a resource the Sanctuary
// cannot re-make, so the refine keeps it -- but the line ends there and the {W} empties. The script pays Greaves
// off the Farm alone and the Sanctuary stays up.
TEST_CASE("executor payment script: a line that floats a unit away is paid by the oracle's dominating script (MTG_REAL_PAY_SCRIPT, Angels s1100025)")
{
    EnsureOracleCards();
    GameState s = Board({ "Remote Farm" });
    s.turn_number = 2;
    s.players[0].life = 20;
    s.players[1].life = 20;
    int n = 100;
    for (const char* nm : { "Seraph Sanctuary", "Lightning Greaves" })
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number = n++;
        s.players[0].hand.push_back(c);
    }
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Lightning Greaves") };
    plan.land_decided = true;
    plan.land_to_play = "Seraph Sanctuary";
    auto sanctuary_tapped = [](const GameState& g)
    {
        for (const Permanent& p : g.battlefield) { if (p.card.m_name.str() == "Seraph Sanctuary") { return p.tapped; } }
        FAIL("no Seraph Sanctuary on the battlefield");
        return false;
    };
    auto greaves_in = [](const GameState& g)
    {
        for (const Permanent& p : g.battlefield) { if (p.card.m_name.str() == "Lightning Greaves") { return true; } }
        return false;
    };
    {
        GameState g = s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        CHECK(greaves_in(g));
        CHECK(sanctuary_tapped(g));   // the line-level over-tap, pinned
    }
    std::vector<manaoracle::PayOption> script;
    std::string what;
    REQUIRE(manaoracle::PlanPaymentScript(s, plan, /*pre=*/true, script, &what));
    CHECK(what.find("dominates") != std::string::npos);
    GameState g = s;
    {
        manaoracle::RealPayScope rp(g, script);
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
    }
    CHECK(greaves_in(g));
    CHECK_FALSE(sanctuary_tapped(g));
}

TEST_CASE("viewer line check: a line the engine's own payment casts in full is search-findable, and nothing more is asked")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Eldrazi Displacer") };
    plan.land_decided = true;
    const manaoracle::LineAudit a = manaoracle::AuditLine(s, /*pre=*/true, plan);
    REQUIRE(a.audited);
    CHECK(a.engine_full);
    CHECK(a.dropped.empty());
    CHECK(a.pays.empty());
    CHECK(a.rollback.empty());
}

// THE USEFUL-COLOUR TABLE READS THE DEFINITIONS (EDF seed 3002 T3 ORACLE-GAP). In a game a card in hand
// carries only its name and number, so a table built off Card::m_mana_cost saw no colour but the bill's own,
// and {1}{G} off Conservatory + a Fertile Ground host never offered the Aura's {U} -- the colour a later
// Training Grounds needs. The hand here is built the way a game holds it: costs stripped.
TEST_CASE("mana oracle: a colour only a later card in hand can spend is still offered (costs read from the definitions)")
{
    EnsureOracleCards();
    GameState s = Board({ "Conservatory", "Adarkar Wastes", "Fertile Ground" });
    s.battlefield[2].aura_attached_to = 2;
    s.deck_has_land_aura = true;
    int n = 100;
    for (const char* nm : { "Living Wish", "Training Grounds" })
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number    = n++;
        c.m_mana_cost = ManaCost{};   // as a game holds it
        s.players[0].hand.push_back(c);
    }
    const auto opts = manaoracle::MinimalPayments(s, Cost(1, 0, 0, 0, 0, 1), /*for_creature=*/false, nullptr);
    bool floats_u = false;
    for (const manaoracle::PayOption& o : opts)
    {
        for (const TurnSolver::PreTap& t : o.taps)
        {
            if (t.name != "Adarkar Wastes") { continue; }
            for (int ac : t.aura_colors) { if (ac == static_cast<int>(Color::Blue)) { floats_u = true; } }
        }
    }
    CHECK(floats_u);
}

// A hand whose costs are stripped, as a game holds it: Training Grounds makes {U} a useful colour.
void BlueInHand(GameState& s)
{
    Card c = CardDatabase::Instance().Lookup("Training Grounds")->card;
    c.m_number    = 100;
    c.m_mana_cost = ManaCost{};
    s.players[0].hand.push_back(c);
}

// EDF seed 3353 T8 (an ORACLE-GAP): a tap set that makes more than its bill floats ANY of its units on, not
// the float-only payer's fixed pick. Forest + Wild Growth ({G}{G}) and Plains + Fertile Ground ({W} + any)
// make four for {3}; the payer's generic order spends {W} and the bonus before {G}, so without a set-aside
// of the new units the one left over is always a {G} -- never the {U} a later Training Grounds wants.
TEST_CASE("mana oracle: a payment that makes more than its bill can float any one of its units")
{
    EnsureOracleCards();
    GameState s = Board({ "Forest", "Wild Growth", "Plains", "Fertile Ground" });
    s.battlefield[1].aura_attached_to = 1;
    s.battlefield[3].aura_attached_to = 3;
    s.deck_has_land_aura = true;
    BlueInHand(s);
    const auto opts = manaoracle::MinimalPayments(s, Cost(3), /*for_creature=*/false, nullptr);
    bool keeps_u = false;
    for (const manaoracle::PayOption& o : opts) { if (o.taps.size() == 2 && o.stash.blue == 1) { keeps_u = true; } }
    CHECK(keeps_u);
}

// EDF seed 3101 T4 (an ORACLE-GAP): a multi-unit source may pay part of a bill and float the rest when one
// more tap covers what that rest would have paid. Forest {G} + Plains/Fertile Ground {W}+any pays {2}{G}
// exactly, so the minimal search never taps Mariposa beside them; with Mariposa's {C} as well the Fertile
// Ground unit floats on (as {U}, the payer spending {C} and {W} on the generic).
TEST_CASE("mana oracle: one more tap beside a multi-unit source lets its surplus float to a later payment")
{
    EnsureOracleCards();
    GameState s = Board({ "Forest", "Plains", "Fertile Ground", "Mariposa Military Base" });
    s.battlefield[2].aura_attached_to = 2;
    s.deck_has_land_aura = true;
    BlueInHand(s);
    const auto opts = manaoracle::MinimalPayments(s, Cost(2, 0, 0, 0, 0, 1), /*for_creature=*/false, nullptr);
    bool superset = false;
    for (const manaoracle::PayOption& o : opts)
    {
        bool mariposa = false;
        for (const TurnSolver::PreTap& t : o.taps) { if (t.name == "Mariposa Military Base") { mariposa = true; } }
        if (mariposa && o.taps.size() == 3) { superset = true; }
    }
    CHECK(superset);
}

// Found by the oracle's rollback census (2026-10-27; EDF viewer game seed 3000 T4): a {U} whose only blue
// is Fertile Ground's any-colour bonus on Mariposa Military Base fails the greedy, and the backtracker's
// DFS -- which taps sources in battlefield order and accepts the FIRST covering tap set -- tapped
// Conservatory (G/W, useless for {U}) before reaching Mariposa, so a one-mana spell tapped two lands and
// floated {G}{C}. In the game, Training Grounds then left Eldrazi Displacer's {2}{W} without its {W}
// (APPLY-DROP, rollback depth 1). The per-cast give-back (MTG_PAY_SHRINK) now returns Conservatory.
TEST_CASE("payment: {U} off a Fertile Ground host taps the host alone, not a useless land before it")
{
    EnsureOracleCards();
    GameState s = Board({ "Conservatory", "Mariposa Military Base", "Shivan Gorge", "Fertile Ground" });
    s.battlefield[3].aura_attached_to = 2;   // Fertile Ground on Mariposa Military Base
    s.deck_has_land_aura = true;
    REQUIRE(TapForCostDirect(s, Cost(0, 0, 1), /*for_creature=*/false));
    CHECK_FALSE(s.battlefield[0].tapped);    // Conservatory: its {G}/{W} cannot pay {U}
    CHECK(s.battlefield[1].tapped);
    CHECK(s.floating_mana.Total() == 1);     // Mariposa's own {C}
}

// MTG_PAY_SHRINK=0: the backtracker's first covering tap set, Conservatory as well as the host -- unless the
// per-payment refine (MTG_PAY_REFINE, pinned on here) gives Conservatory back, which it does on its own in a deck
// with no same-phase untapper. EDF holds Cloud of Faeries and Peregrine Drake (deck_has_mana_untapper): there the
// floated unit is worth more than the untapped Conservatory, so the refine keeps the engine's payment (and
// MTG_PAY_SHRINK, default on, is what gives the over-tap back in play).
TEST_CASE("payment: MTG_PAY_SHRINK=0 keeps the backtracker's first covering tap set for a {U}")
{
    EnsureOracleCards();
    for (int refine : { 0, 1 })
    for (bool untapper : { false, true })
    {
        CAPTURE(refine);
        CAPTURE(untapper);
        GameState s = Board({ "Conservatory", "Mariposa Military Base", "Shivan Gorge", "Fertile Ground" });
        s.battlefield[3].aura_attached_to = 2;
        s.deck_has_land_aura = true;
        s.deck_has_mana_untapper = untapper;
        const std::int8_t saved = heurarm::t_arm[heurarm::PAY_SHRINK];
        heurarm::t_arm[heurarm::PAY_SHRINK] = 0;
        heurarm::t_arm[heurarm::PAY_REFINE] = static_cast<std::int8_t>(refine);
        const bool ok = TapForCostDirect(s, Cost(0, 0, 1), /*for_creature=*/false);
        heurarm::t_arm[heurarm::PAY_SHRINK] = saved;
        heurarm::t_arm[heurarm::PAY_REFINE] = -1;
        REQUIRE(ok);
        const bool kept = refine == 0 || untapper;
        CHECK(s.battlefield[0].tapped == kept);
        CHECK(s.battlefield[1].tapped);
        CHECK(s.floating_mana.Total() == (kept ? 2 : 1));
    }
}

// THE FRONTIER PRUNE KEYS A PERMANENT BY WHAT IS ATTACHED TO IT (EDF seed 3003 T5, nine ORACLE-GAPs). Aether Hub #1
// carries Wild Growth; no energy, so both Hubs tap for {C} alone. Fertile Ground onto Hub #1, Trace of Abundance onto
// Adarkar Wastes, then Eldrazi Displacer's {2}{C} blink: the engine pays Fertile Ground off Brushland {G} + Hub #7,
// Trace off Hub #1 (its {C}, Wild Growth's {G}, Fertile Ground's {W}), and the blink off the floating {C} + the Wastes
// carrying Trace's bonus -- seven mana from four lands. The prune read "Hub #1 untapped" (three mana) as covered by
// "Hub #7 untapped" (one), so the board after Fertile Ground's payment off Hub #1 itself -- a Brushland more, a life
// up -- discarded the engine's, and every line the oracle kept was a mana short of the blink.
TEST_CASE("mana oracle: an enchanted land is not its bare twin's equal in the frontier prune (EDF seed 3003 T5)")
{
    EnsureOracleCards();
    static const EldraziFlickerProvider prov;
    GameState s = Board({ "Aether Hub", "Wild Growth", "Brushland", "Eldrazi Displacer", "Adarkar Wastes",
                          "Emiel the Blessed", "Aether Hub", "Essence Depleter" });
    s.battlefield[1].aura_attached_to = 1;
    s.deck_has_land_aura = true;
    s.m_provider = &prov;
    s.turn_number = 5;
    s.players[0].life = 16;
    s.players[1].life = 20;
    int n = 100;
    for (const char* nm : { "Trace of Abundance", "Fertile Ground", "Brushland", "Trace of Abundance" })
    { Card c = CardDatabase::Instance().Lookup(nm)->card; c.m_number = n++; s.players[0].hand.push_back(c); }
    for (int i = 0; i < 20; ++i)
    { Card c = CardDatabase::Instance().Lookup("Brushland")->card; c.m_number = 300 + i; s.players[0].library.push_back(c); }
    Action fg = CastAction(s, "Fertile Ground");     fg.enchant_target = 1;
    Action tr = CastAction(s, "Trace of Abundance"); tr.enchant_target = 5;
    Action bl;
    bl.kind          = Action::Kind::ActivateBlink;
    bl.card_name     = CardDatabase::Instance().Lookup("Eldrazi Displacer")->card.m_name;
    bl.sac_source_id = 4;
    bl.sac_victim_id = 6;
    bl.chosen_x      = 1;
    bl.cost          = Cost(2);
    bl.cost.colorless = 1;
    TurnSolver::Plan plan;
    plan.actions      = { fg, tr, bl };
    plan.land_decided = true;
    CHECK(manaoracle::CoversEngineOutcome(s, /*pre=*/true, plan));
}

// ---- THE ENUM-GAP PASS'S FALSE POSITIVES (eg1 triage, 2026-10-29; docs/design/mana-completeness-oracle.md 13.5) ------
namespace
{
// Hand cards with distinct ids from 100 on, as a game holds them (costs stripped).
void Hand(GameState& s, const std::vector<std::string>& names)
{
    int n = 100 + static_cast<int>(s.players[0].hand.size());
    for (const std::string& nm : names)
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number    = n++;
        c.m_mana_cost = ManaCost{};
        s.players[0].hand.push_back(c);
    }
}
void SetLibrary(GameState& s, const std::vector<std::string>& names)
{
    int n = 200;
    for (const std::string& nm : names)
    { Card c = CardDatabase::Instance().Lookup(nm)->card; c.m_number = n++; s.players[0].library.push_back(c); }
}
int HandNum(const GameState& s, const char* nm)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == nm) { return c.m_number; } }
    FAIL("not in hand: ", nm);
    return 0;
}
TurnSolver::Plan PlanOf(std::vector<Action> actions)
{
    TurnSolver::Plan p;
    p.actions      = std::move(actions);
    p.land_decided = true;
    return p;
}
} // namespace

// Giada, Font of Hope: "{T}: Add {W}. Spend this mana only to cast an Angel spell." The oracle's restricted route once
// offered it to ANY creature spell, and paid Bishop of Wings (a Human Cleric) with it -- Angels eg1, two ENUM-GAPs no
// legal payment realises. The paying spell (the frontier's PayingSpellCard) now rides into the table.
TEST_CASE("mana oracle: Giada's Angel-only {W} pays an Angel spell, never another creature spell")
{
    EnsureOracleCards();
    const GameState s = Board({ "Plains", "Giada, Font of Hope" });
    const Card bishop   = CardDatabase::Instance().Lookup("Bishop of Wings")->card;     // {W}{W}, Human Cleric
    const Card valkyrie = CardDatabase::Instance().Lookup("Youthful Valkyrie")->card;   // {1}{W}, Angel
    CHECK(manaoracle::MinimalPayments(s, Cost(0, 2), /*for_creature=*/true, nullptr, false, false, &bishop).empty());
    const auto for_angel = manaoracle::MinimalPayments(s, Cost(1, 1), /*for_creature=*/true, nullptr, false, false, &valkyrie);
    CHECK(HasOption(for_angel, { "Plains:W", "Giada, Font of Hope:W" }));
}

// Generator Servant makes its {C}{C} only through a plan's own SacForMana action (both apply worlds crack it in the
// pre-pass; the payer never does). NivMizzetCombo eg1: 95 of 106 ENUM-GAPs cracked it mid-payment for a plan that
// does not hold the action. The plan WITH the action is the executable form, and is checked on its own.
TEST_CASE("ENUM-GAP: a plan payable only by cracking Generator Servant mid-payment is no gap; the plan with the crack is")
{
    EnsureOracleCards();
    GameState s = Board({ "Island", "Generator Servant" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Tandem Lookout" });
    const TurnSolver::Plan bare = PlanOf({ CastAction(s, "Tandem Lookout") });
    Action sac;
    sac.kind               = Action::Kind::SacForMana;
    sac.card_name          = CardDatabase::Instance().Lookup("Generator Servant")->card.m_name;
    sac.hand_index         = -1;
    sac.ritual_float       = 2;
    sac.chosen_float_color = "C";
    sac.sac_source_id      = 2;
    sac.is_noncreature     = true;
    const TurnSolver::Plan twin = PlanOf({ sac, CastAction(s, "Tandem Lookout") });
    const std::vector<TurnSolver::Plan> none;
    const std::vector<TurnSolver::Plan> observed{ twin };
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, bare, none, &observed).empty());
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, twin, none) == "ENUM-GAP");
}

// Sliver Hive's {5},{T}: create a Sliver. A payment that taps the Hive for the cast strands the activation, and the
// apply's trailing pass skips it without a drop (its source is tapped). Slivers eg1, 34 ENUM-GAPs: "cast Plated Sliver
// + make a token" realised as "cast Plated Sliver" -- a plan on the menu.
TEST_CASE("ENUM-GAP: a realisation that skipped a stale activation stands for the plan without it")
{
    EnsureOracleCards();
    GameState s = Board({ "Sliver Hive", "Sliver Hive", "Plated Sliver" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Plated Sliver" });
    const CardDefinition* hive = CardDatabase::Instance().Lookup("Sliver Hive");
    Action tok;
    tok.kind           = Action::Kind::TapForTokenPay;
    tok.card_name      = hive->card.m_name;
    tok.hand_index     = -1;
    tok.sac_source_id  = 1;
    tok.cost           = hive->params.tap_token_cost.value();
    tok.is_noncreature = true;
    const TurnSolver::Plan cast = PlanOf({ CastAction(s, "Plated Sliver") });
    const TurnSolver::Plan both = PlanOf({ CastAction(s, "Plated Sliver"), tok });
    // The token's {5} is unpayable off two Hives: every paying realisation is "the Hive paid the cast, the token
    // fizzled" -- the cast-only plan, which the enumeration holds (it is checked on its own, or is on the menu).
    const std::vector<TurnSolver::Plan> observed{ cast };
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, both, {}, &observed).empty());
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, both, { cast }).empty());
    // ...and only that: with the cast-only plan nowhere (not on the menu, not enumerated) it IS a lost option.
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, both, {}) == "ENUM-GAP");
}

// Genesis Wave: "reveal the top X, put the permanents onto the battlefield". Selesnya / Landfall eg1, 11 ENUM-GAPs:
// the follow-up cast paid with lands the Wave put onto the battlefield from the library -- payable only because the
// oracle knows the library's order. Five Forests pay the Wave for X = 2; Rampant Growth then needs the two it reveals.
TEST_CASE("ENUM-GAP: a follow-up paid by lands Genesis Wave revealed is HIDDEN-INFO, not a gap")
{
    EnsureOracleCards();
    auto make = [](int forests)
    {
        std::vector<std::string> bf(static_cast<std::size_t>(forests), "Forest");
        GameState s = Board(bf);
        s.players[0].life = 20;
        s.players[1].life = 20;
        Hand(s, { "Genesis Wave", "Rampant Growth" });
        std::vector<std::string> lib{ "Forest", "Forest" };
        for (int i = 0; i < 20; ++i) { lib.push_back(i % 2 ? "Forest" : "Plains"); }
        SetLibrary(s, lib);
        return s;
    };
    auto plan_of = [](const GameState& s)
    {
        Action wave = CastAction(s, "Genesis Wave");
        wave.chosen_x     = 2;
        wave.cost.generic = 2;
        return PlanOf({ wave, CastAction(s, "Rampant Growth") });
    };
    const GameState five = make(5);
    std::string why;
    CHECK(manaoracle::EnumGapVerdict(five, /*pre=*/true, plan_of(five), {}, nullptr, &why) == "HIDDEN-INFO");
    CHECK(why.find("Forest#200") != std::string::npos);
    // With two Forests more on the board the plan pays without the revealed ones: the search goes on past the
    // clairvoyant payments to that one, and it is a gap (nothing on the menu here).
    const GameState seven = make(7);
    CHECK(manaoracle::EnumGapVerdict(seven, /*pre=*/true, plan_of(seven), {}) == "ENUM-GAP");
}

// EDF seed 1100009 gi8 T2 (2 ENUM-GAPs): Wild Growth on the new Brushland, then Cloud of Faeries, pays only Cloud-first.
// The menu holds the same casts with Wild Growth on Adarkar Wastes, which ends with a land more untapped: Brushland's
// {G} pays the Aura, the Wastes' {U} plus its {G} pay Cloud, and Cloud untaps both. USER: "With Adarkar Wastes and
// Brushland you would play Wild Growth first ... I see no need to change the order for that case".
TEST_CASE("ENUM-GAP: a sibling with the land Aura on another host that ends with at least the resources covers the plan")
{
    EnsureOracleCards();
    static const EldraziFlickerProvider prov;
    GameState s = Board({ "Adarkar Wastes", "Brushland" });
    s.battlefield[1].entered_this_turn = true;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_land_aura = true;
    s.m_provider = &prov;
    s.turn_number = 2;
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Wild Growth", "Cloud of Faeries" });
    SetLibrary(s, std::vector<std::string>(20, "Island"));
    auto plan_on = [&](int host)
    {
        Action wg = CastAction(s, "Wild Growth");
        wg.enchant_target = host;
        return PlanOf({ wg, CastAction(s, "Cloud of Faeries") });
    };
    const TurnSolver::Plan on_brushland = plan_on(2);
    std::string why;
    REQUIRE(manaoracle::EnumGapVerdict(s, /*pre=*/true, on_brushland, {}, nullptr, &why) == "ENUM-GAP");
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, on_brushland, { plan_on(1) }).empty());
}

// Colossus Hammer eg1 (4 ENUM-GAPs): Sigarda's Aid, then the Hammer -- whose entry trigger attaches it to the board's
// only creature -- then "equip {8}" onto that creature, which the apply skips as already done and never pays. The
// gate is right that {8} cannot be paid; the realisation is the menu's Aid + Hammer, paid another way.
TEST_CASE("ENUM-GAP: an equip the Equipment's own entry trigger already made stands for the plan without it")
{
    EnsureOracleCards();
    GameState s = Board({ "Mountain", "Plains", "Kor Duelist" });
    s.turn_number = 3;
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Sigarda's Aid", "Colossus Hammer" });
    SetLibrary(s, std::vector<std::string>(20, "Plains"));
    Action eq;
    eq.kind          = Action::Kind::Equip;
    eq.card_name     = CardDatabase::Instance().Lookup("Colossus Hammer")->card.m_name;
    eq.hand_index    = -1;
    eq.cost.generic  = 8;
    eq.sac_source_id = HandNum(s, "Colossus Hammer");
    eq.sac_victim_id = 3;
    const TurnSolver::Plan casts = PlanOf({ CastAction(s, "Sigarda's Aid"), CastAction(s, "Colossus Hammer") });
    const TurnSolver::Plan with_equip = PlanOf({ CastAction(s, "Sigarda's Aid"), CastAction(s, "Colossus Hammer"), eq });
    {
        // (the premise: the Aid really attaches the Hammer on its own)
        GameState g = s;
        TurnSolver::ApplyPlan(g, casts, /*is_pre_combat=*/true);
        bool attached = false;
        for (const Permanent& p : g.battlefield)
        { if (p.card.m_name.str() == "Colossus Hammer" && p.equipped_to == 3) { attached = true; } }
        REQUIRE(attached);
    }
    const std::vector<TurnSolver::Plan> observed{ casts };
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, with_equip, {}, &observed).empty());
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, with_equip, { casts }).empty());
    CHECK(manaoracle::EnumGapVerdict(s, /*pre=*/true, with_equip, {}) == "ENUM-GAP");
}
