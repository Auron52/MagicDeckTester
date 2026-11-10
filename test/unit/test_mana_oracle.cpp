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
#include "core/SpellEffects.h"

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

// THE ORACLE IS A CHECKER: play must be byte-identical with it on or off. It drives the engine's own
// enumeration and applies on copies, on the game's thread, so everything it touched in the engine's per-game
// caches (the plan memos, the breakpoint-continuation cache, the payable-mana cache, the route memos, the
// decision epoch, the work counters) used to stay behind for the play that followed. Mana-clean gate, Bruna
// seed 30003487 gi3487: the T4 main replayed a committed line's positional bp_choice, the bp-enum cache served
// a list a fresh derivation would not, and the oracle's 51st menu apply had changed that cache -- T5 win with
// the oracle off, T4 with it on. TurnSolver::ObserverIsolationScope now stashes all of it for the check's
// duration and puts it back.
TEST_CASE("mana oracle is checker-only: a line check leaves the engine's per-game caches exactly as it found them")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    // Warm the caches the way the search does at this decision: its menu, each entry applied once.
    const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    REQUIRE_FALSE(menu.empty());
    for (const TurnSolver::Plan& p : menu) { GameState c = s; TurnSolver::ApplyPlan(c, p, /*is_pre_combat=*/true); }
    TurnSolver::Plan plan;
    plan.actions      = { CastAction(s, "Training Grounds"), CastAction(s, "Eldrazi Displacer") };
    plan.land_decided = true;
    const std::uint64_t before = TurnSolver::PerGameCacheDigestForTest();
    for (const TurnSolver::Plan& p : menu) { (void)manaoracle::AuditLine(s, /*pre=*/true, p, &menu, 400, /*deep=*/true); }
    (void)manaoracle::AuditLine(s, /*pre=*/true, plan, &menu, 400, /*deep=*/true);
    CHECK(TurnSolver::PerGameCacheDigestForTest() == before);
}

TEST_CASE("observer isolation: the observer starts on empty caches, and closing the scope restores the game's")
{
    EnsureOracleCards();
    const GameState s = Edf3000T4();
    const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    for (const TurnSolver::Plan& p : menu) { GameState c = s; TurnSolver::ApplyPlan(c, p, /*is_pre_combat=*/true); }
    const std::uint64_t before = TurnSolver::PerGameCacheDigestForTest();
    const std::uint64_t epoch  = TurnSolver::DecisionEpoch();
    {
        TurnSolver::ObserverIsolationScope iso;
        CHECK(TurnSolver::DecisionEpoch() != epoch);
        CHECK(TurnSolver::PerGameCacheDigestForTest() != before);
        {
            TurnSolver::ObserverIsolationScope nested;   // a no-op inside the outer scope
            const std::vector<TurnSolver::Plan> again = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
            CHECK(again.size() == menu.size());
            for (const TurnSolver::Plan& p : again) { GameState c = s; TurnSolver::ApplyPlan(c, p, /*is_pre_combat=*/true); }
        }
        TurnSolver::ClearPerGameCaches();   // even a wholesale clear inside the scope reaches only the observer's
    }
    CHECK(TurnSolver::DecisionEpoch() == epoch);
    CHECK(TurnSolver::PerGameCacheDigestForTest() == before);
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

TEST_CASE("mana oracle: Lake's on-demand crack is distinct from its ordinary black tap")
{
    EnsureOracleCards();
    GameState s = Board({ "Lake of the Dead", "Swamp" });
    s.deck_has_ondemand_sac = true;
    s.scripted_early_ondemand_crack = true;
    const auto opts = manaoracle::MinimalPayments(s, Cost(0, 0, 0, 4), false, nullptr);
    bool found_crack = false;
    for (const manaoracle::PayOption& o : opts)
    {
        if (o.taps.size() != 1 || !o.taps[0].sac_crack) { continue; }
        GameState paid = s;
        REQUIRE(manaoracle::PerformPayTaps(paid, o));
        CHECK(paid.floating_mana.black == 5);   // taps add {B}+{B}{B}{B}{B}; PerformPayTaps does not pay the bill
        CHECK(paid.battlefield.size() == 1);
        CHECK_FALSE(paid.scripted_early_ondemand_crack);
        found_crack = true;
    }
    CHECK(found_crack);

    GameState no_victim = Board({ "Lake of the Dead" });
    no_victim.deck_has_ondemand_sac = true;
    const auto none = manaoracle::MinimalPayments(no_victim, Cost(0, 0, 0, 4), false, nullptr);
    for (const manaoracle::PayOption& o : none)
        for (const TurnSolver::PreTap& t : o.taps) { CHECK_FALSE(t.sac_crack); }
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

// LAZY STOP (lever (c), USER 2026-11-06): a capped caller asks for at most `max_opts` options, and gets exactly the
// first `max_opts` of the full enumeration, in order -- Realize applies that prefix and nothing past it.
TEST_CASE("mana oracle: a lazily stopped enumeration is a prefix of the full one")
{
    EnsureOracleCards();
    GameState s = Board({ "Forest", "Forest", "Plains", "Island", "Swamp", "Mountain" });
    const auto full = manaoracle::MinimalPayments(s, Cost(2), /*for_creature=*/false, nullptr);
    REQUIRE(full.size() >= 4);
    auto same = [](const manaoracle::PayOption& a, const manaoracle::PayOption& b)
    {
        if (a.taps.size() != b.taps.size() || a.stash.Total() != b.stash.Total()) { return false; }
        for (std::size_t i = 0; i < a.taps.size(); ++i)
        { if (a.taps[i].name != b.taps[i].name || a.taps[i].num != b.taps[i].num) { return false; } }
        return true;
    };
    for (std::size_t k = 1; k <= full.size() + 1; ++k)
    {
        std::vector<std::string> notes;
        const auto lazy = manaoracle::MinimalPayments(s, Cost(2), false, &notes, false, false, nullptr, nullptr, 0, k);
        REQUIRE(lazy.size() == std::min(k, full.size()));
        for (std::size_t i = 0; i < lazy.size(); ++i) { CHECK(same(lazy[i], full[i])); }
        // The stop is not an enumeration cap: no "payment enumeration capped" note.
        for (const std::string& n : notes) { CHECK(n != "payment enumeration capped"); }
    }
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

namespace
{
// Angels, mana-clean pass 1 (seed 30010687 gi 10687 T3, APPLY-DROP x208 and the 1,794-flag family around it): Plains,
// Sol Ring, Remote Farm (two depletion counters) and Giada, Font of Hope #7 untapped; a second Giada and Serra the
// Benevolent in hand.
GameState AngelsGiadaSerraBoard()
{
    static const AngelsProvider prov;
    GameState s = Board({ "Plains", "Sol Ring", "Remote Farm", "Giada, Font of Hope" });
    s.turn_number = 3;
    s.m_provider  = &prov;
    s.players[0].life = 20;
    s.players[1].life = 20;
    PutDepletionCounters(s, s.battlefield[2], 2);
    int n = 100;
    for (const char* nm : { "Serra the Benevolent", "Giada, Font of Hope", "Lightstall Inquisitor" })
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number = n++;
        s.players[0].hand.push_back(c);
    }
    for (int i = 0; i < 20; ++i)
    { Card c = CardDatabase::Instance().Lookup("Plains")->card; c.m_number = 300 + i; s.players[0].library.push_back(c); }
    return s;
}
} // namespace

TEST_CASE("payment: a {C} left over beside Giada's Angel-only {W} is Sol Ring's, so it floats unrestricted")
{
    EnsureOracleCards();
    GameState s = Board({ "Sol Ring", "Giada, Font of Hope" });
    s.turn_number = 3;
    const Card giada = CardDatabase::Instance().Lookup("Giada, Font of Hope")->card;
    ManaCost bill = giada.m_mana_cost;
    REQUIRE(bill.white == 1);
    REQUIRE(bill.generic == 1);
    {
        SpellSubtypePayScope ps(&giada);
        REQUIRE(TapForCostShared(s, bill, /*for_creature=*/true, nullptr, true));
    }
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
    // Giada makes only {W}, and the {W} pip took it: the {C} still floating can only be Sol Ring's.
    CHECK(s.floating_mana.colorless == 1);
    CHECK(s.floating_bigspell_mana.Total() == 0);
}

TEST_CASE("Angels s30010687 T3: Serra the Benevolent and a second Giada both pay (the Angel-only {W} goes to the Angel)")
{
    EnsureOracleCards();
    const GameState s = AngelsGiadaSerraBoard();
    for (int order = 0; order < 2; ++order)
    {
        TurnSolver::Plan plan = order == 0
            ? PlanOf({ CastAction(s, "Serra the Benevolent"), CastAction(s, "Giada, Font of Hope") })
            : PlanOf({ CastAction(s, "Giada, Font of Hope"), CastAction(s, "Serra the Benevolent") });
        const manaoracle::LineAudit a = manaoracle::AuditLine(s, /*pre=*/true, plan);
        INFO("order ", order, " dropped=", a.dropped, " rollback=", a.rollback);
        REQUIRE(a.audited);
        CHECK(a.engine_full);
        CHECK(a.dropped.empty());
    }
}

TEST_CASE("Angels s30001531 T2: Lightning Greaves' {2} off Sol Ring keeps Remote Farm's floating {W} for Lightstall Inquisitor")
{
    EnsureOracleCards();
    static const AngelsProvider prov;
    GameState s = Board({ "Remote Farm" });
    s.turn_number = 2;
    s.m_provider  = &prov;
    s.players[0].life = 20;
    s.players[1].life = 20;
    PutDepletionCounters(s, s.battlefield[0], 2);
    int n = 100;
    for (const char* nm : { "Lightning Greaves", "Lightstall Inquisitor", "Sol Ring" })
    {
        Card c = CardDatabase::Instance().Lookup(nm)->card;
        c.m_number = n++;
        s.players[0].hand.push_back(c);
    }
    for (int i = 0; i < 20; ++i)
    { Card c = CardDatabase::Instance().Lookup("Plains")->card; c.m_number = 300 + i; s.players[0].library.push_back(c); }
    const TurnSolver::Plan plan = PlanOf({ CastAction(s, "Lightning Greaves"), CastAction(s, "Lightstall Inquisitor"),
                                           CastAction(s, "Sol Ring") });
    const manaoracle::LineAudit a = manaoracle::AuditLine(s, /*pre=*/true, plan);
    INFO("dropped=", a.dropped, " rollback=", a.rollback);
    REQUIRE(a.audited);
    CHECK(a.engine_full);
    CHECK(a.dropped.empty());
}

// A plain land's AGE is not part of the end board (Auras pass1, ORACLE-GAP x11,688 in 98,000 games, every one this
// shape). Seed 30000073 T3: Brushland and Horizon Canopy #2 in play, a second Canopy the land drop, Slippery Bogle and
// the searched Canopy dig. The engine pays the Bogle off the old Canopy and sacrifices the new one to dig, paying the
// {1} with Brushland's painless {C}; the oracle's frontier prune keeps only "new Canopy tapped, old one up" (the two
// frontiers hold the same resources), so its dig sacrifices the OLD copy. Both end on one tapped Canopy, Brushland
// tapped for {C}, a life down -- and differed only in whether the surviving Canopy entered this turn.
TEST_CASE("mana oracle: the surviving copy's age does not tell two end boards apart (Auras Horizon Canopy dig)")
{
    EnsureOracleCards();
    static const AurasProvider prov;
    GameState s = Board({ "Brushland", "Horizon Canopy", "Light-Paws, Emperor's Voice" });
    s.m_provider  = &prov;
    s.turn_number = 3;
    s.players[0].life = 19;
    s.players[1].life = 20;
    Hand(s, { "Horizon Canopy", "Slippery Bogle", "Armadillo Cloak" });
    SetLibrary(s, std::vector<std::string>(20, "Plains"));
    TurnSolver::Plan plan = PlanOf({ CastAction(s, "Slippery Bogle") });
    plan.land_to_play = "Horizon Canopy";
    plan.dig_choice   = 1;
    {
        // (the premise: the engine's line digs with the new Canopy and pays the {1} painlessly)
        GameState g = s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        int canopies = 0;
        bool survivor_new = true;
        for (const Permanent& p : g.battlefield)
        { if (p.card.m_name.str() == "Horizon Canopy") { ++canopies; survivor_new = p.entered_this_turn; } }
        REQUIRE(canopies == 1);
        REQUIRE(g.players[0].life == 18);
        REQUIRE_FALSE(survivor_new);
    }
    CHECK(manaoracle::CoversEngineOutcome(s, /*pre=*/true, plan));
}

// A breakpoint CONTINUATION's cast section owes its own line (TurnSolver.cpp cont_line_armed; Auras pass1, APPLY-DROP
// x25, every one this shape). Seed 30013411 T4: Forest, Plains, Branchloft Pathway, Kor Spiritdancer (Slippery Bogle
// and a Rancor in play); the land drop is a Pathway as Boulderloft {W}, the plan casts Rancor on Kor. Kor's draw
// re-solves to {Light-Paws, Rancor}. With the continuation unbound, the payer read the line as owing nothing more and
// paid Light-Paws' {1}{W} with Plains + the Branchloft's {G}, the board's last green, so the continuation's Rancor was
// dropped -- while Plains + Boulderloft pay Light-Paws and leave the {G} for it.
TEST_CASE("payment: a breakpoint continuation's first cast keeps the colour its later cast needs (Auras Kor draw)")
{
    EnsureOracleCards();
    static const AurasProvider prov;
    GameState s = Board({ "Forest", "Plains", "Kor Spiritdancer", "Branchloft Pathway", "Slippery Bogle", "Rancor" });
    s.m_provider  = &prov;
    s.turn_number = 4;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.battlefield[5].aura_attached_to = 3;   // Rancor on Kor Spiritdancer
    Hand(s, { "Rancor", "Rancor", "Light-Paws, Emperor's Voice", "Daybreak Coronet", "Branchloft Pathway" });
    SetLibrary(s, { "Horizon Canopy", "Horizon Canopy", "Ethereal Armor", "Plains", "Plains", "Plains", "Plains",
                    "Plains", "Plains", "Plains", "Plains", "Plains", "Plains", "Plains" });
    Action rancor = CastAction(s, "Rancor");
    rancor.enchant_target = 3;
    TurnSolver::Plan plan = PlanOf({ rancor });
    plan.land_to_play = "Branchloft Pathway";
    plan.land_face    = "back";
    GameState g = s;
    TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
    int rancors_in_hand = 0;
    bool light_paws = false;
    for (const Card& c : g.players[0].hand) { if (c.m_name.str() == "Rancor") { ++rancors_in_hand; } }
    for (const Permanent& p : g.battlefield) { if (p.card.m_name.str() == "Light-Paws, Emperor's Voice") { light_paws = true; } }
    REQUIRE(light_paws);              // (the premise: the continuation casts Light-Paws)
    CHECK(rancors_in_hand == 0);      // ...and its Rancor too
    const manaoracle::LineAudit au = manaoracle::AuditLine(s, /*pre=*/true, plan);
    CHECK(au.engine_full);
}

// ---- SLIVERS, mana-clean pass 1 (2026-11-03; docs/design/mana-completeness-oracle.md 13.12) -------------------------
namespace
{
Action AnimateAction(int num)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup("Mutavault");
    Action a;
    a.kind           = Action::Kind::AnimateLand;
    a.card_name      = d->card.m_name;
    a.hand_index     = -1;
    a.sac_source_id  = num;
    a.cost           = d->params.animate_cost.value();
    a.eval           = 1;
    a.is_noncreature = true;
    return a;
}
Action HiveTokenAction(int num)
{
    const CardDefinition* hive = CardDatabase::Instance().Lookup("Sliver Hive");
    Action a;
    a.kind           = Action::Kind::TapForTokenPay;
    a.card_name      = hive->card.m_name;
    a.hand_index     = -1;
    a.sac_source_id  = num;
    a.cost           = hive->params.tap_token_cost.value();
    a.eval           = 1;
    a.is_noncreature = true;
    return a;
}
std::string Joined(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& c : v) { s += c + " "; }
    return s;
}
bool Has(const std::vector<std::string>& v, const char* c) { return std::find(v.begin(), v.end(), c) != v.end(); }
} // namespace

// Slivers s30001200 T4 (ORACLE-GAP x125, ~6,000 flags of the family): {animate Mutavault #1 + animate Mutavault #2}. The
// engine pays each animation with the land itself, so both end animated and tapped. The frontier's Pareto prune read
// "#2 pays for #1" and "#1 pays for itself" as one board (both leave one untapped Mutavault) and kept the first, after
// which #2 is tapped and its own animation has no source: the oracle never reproduced the engine (I1).
// The oracle keys a copy the plan names (t_plan_srcs) apart from its twins in the prune (the Vampires KeyedApart fix).
TEST_CASE("mana oracle: two Mutavaults each paying its own animation is among the oracle's outcomes")
{
    EnsureOracleCards();
    GameState s = Board({ "Mutavault", "Mutavault", "Galerider Sliver" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    SetLibrary(s, std::vector<std::string>(20, "Plated Sliver"));
    const TurnSolver::Plan plan = PlanOf({ AnimateAction(1), AnimateAction(2) });
    CHECK(manaoracle::CoversEngineOutcome(s, /*pre=*/true, plan));
}

// Slivers s30001477 T4 (ORACLE-GAP x53): {cast Sinew Sliver + animate Mutavault #3}, the engine paying the Sliver off
// Secluded Courtyard + Mutavault #2 and the animation off #3 itself. The prune kept "#3 pays the Sliver" instead.
TEST_CASE("mana oracle: a Sliver paid off the other Mutavault, the animated one paying itself, is among the outcomes")
{
    EnsureOracleCards();
    GameState s = Board({ "Secluded Courtyard", "Mutavault", "Mutavault", "Plated Sliver" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Sinew Sliver" });
    SetLibrary(s, std::vector<std::string>(20, "Plated Sliver"));
    const TurnSolver::Plan plan = PlanOf({ CastAction(s, "Sinew Sliver"), AnimateAction(3) });
    CHECK(manaoracle::CoversEngineOutcome(s, /*pre=*/true, plan));
}

// Kithkin seed 30002411 T4: paying Figure's {R/W} ability when it is already
// a 2/2 changes no board field. The unpaid branch leaves more mana, but cannot
// stand in for the paid branch before a later Mutavault animation.
TEST_CASE("mana oracle: an unpaid effect-free Figure activation cannot prune the paid line")
{
    EnsureOracleCards();
    static const KithkinProvider prov;
    GameState s = Board({ "Plains", "Figure of Destiny", "Mutavault", "Metallic Mimic",
                          "Mutavault", "Adaptive Automaton" });
    s.m_provider = &prov;
    s.players[0].life = s.players[1].life = 20;
    s.battlefield[1].card.m_power = 2;
    s.battlefield[1].card.m_toughness = 2;
    s.battlefield[1].card.m_subtypes = { "Kithkin", "Spirit" };
    Hand(s, { "Wizened Cenn", "Plains", "Aether Vial", "Figure of Destiny" });
    SetLibrary(s, std::vector<std::string>(20, "Plains"));

    Action level;
    level.kind = Action::Kind::ActivatePermAbility;
    level.card_name = CardDatabase::Instance().Lookup("Figure of Destiny")->card.m_name;
    level.def = CardDatabase::Instance().Lookup("Figure of Destiny");
    level.sac_source_id = 2;
    level.ability_mode = Action::AbilityMode::FigureTwo;
    level.cost = *level.def->params.figure_two_cost;
    level.chosen_x = 1;
    TurnSolver::Plan plan = PlanOf({ CastAction(s, "Aether Vial"), CastAction(s, "Figure of Destiny"),
                                     level, AnimateAction(5) });
    plan.land_to_play = "Plains";
    CHECK(manaoracle::CoversEngineOutcome(s, /*pre=*/true, plan));
}

// Sliver Hive's "{5}, {T}: create a 1/1 Sliver" is an ACTIVATED ABILITY of a land: Ancient Ziggurat's mana ("only to
// cast a creature spell") cannot pay it, and Cavern of Souls pays it only with its {C} face (CR 106.6). Both apply worlds
// paid it as a creature spell (for_creature=true), so a Hive token was bought with Ziggurat mana -- 48 ENUM-GAPs whose
// "paying" realisation was illegal.
TEST_CASE("payment: a Sliver Hive token is never paid with creature-spell-only mana")
{
    EnsureOracleCards();
    {
        GameState s = Board({ "Sliver Hive", "Ancient Ziggurat", "Ancient Ziggurat", "Ancient Ziggurat",
                              "Ancient Ziggurat", "Cavern of Souls", "Plated Sliver" });
        s.players[0].life = 20;
        s.players[1].life = 20;
        const std::size_t before = s.battlefield.size();
        TurnSolver::ApplyPlan(s, PlanOf({ HiveTokenAction(1) }), /*is_pre_combat=*/true);
        CHECK(s.battlefield.size() == before);   // no token: only Cavern's {C} may pay
        CHECK_FALSE(s.battlefield[0].tapped);    // and the Hive is untapped again
    }
    {
        GameState s = Board({ "Sliver Hive", "Cavern of Souls", "Cavern of Souls", "Cavern of Souls",
                              "Unclaimed Territory", "Secluded Courtyard", "Plated Sliver" });
        s.players[0].life = 20;
        s.players[1].life = 20;
        const std::size_t before = s.battlefield.size();
        TurnSolver::ApplyPlan(s, PlanOf({ HiveTokenAction(1) }), /*is_pre_combat=*/true);
        CHECK(s.battlefield.size() == before + 1);   // the five {C} faces pay it
        for (const Permanent& p : s.battlefield) { if (p.card.IsLand()) { CHECK(p.tapped); } }
    }
}

// Slivers s30019665 T4 (APPLY-DROP x15, ~3,700 flags of the family): Hatchery Sliver on the battlefield grants
// replicate. The UNPINNED cast (replicate_count -1: copies made greedily at resolution) of Predatory Sliver spends the
// mana Plated Sliver needed, so {Predatory, Plated} realises as {Predatory + a copy}. The oracle pays both by overpaying
// Predatory (no copy affordable), leaving Mutavault up; the pinned-zero sibling pays Predatory off Mutavault + Cavern
// and leaves Secluded Courtyard up -- incomparable boards, but the sibling's OWN payments include the Mutavault-up one,
// and its own audit vouches for the payment the engine picks there: DROP-COVERED.
TEST_CASE("mana oracle: a replicate drop whose board a same-actions sibling reaches under its own payment is covered")
{
    EnsureOracleCards();
    GameState s = Board({ "Secluded Courtyard", "Sliver Hive", "Mutavault", "Cavern of Souls", "Hatchery Sliver" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Predatory Sliver", "Plated Sliver" });
    SetLibrary(s, std::vector<std::string>(20, "Plated Sliver"));
    const Action pred = CastAction(s, "Predatory Sliver");
    Action pred0 = pred;
    pred0.replicate_count = 0;
    const TurnSolver::Plan unpinned = PlanOf({ pred, CastAction(s, "Plated Sliver") });
    const TurnSolver::Plan pinned0  = PlanOf({ pred0, CastAction(s, "Plated Sliver") });
    // Alone on the menu, the drop is a lost line: a payment of this order realises both casts.
    const std::vector<std::string> alone = manaoracle::PlanVerdict(s, /*pre=*/true, unpinned, { unpinned });
    INFO("alone: ", Joined(alone));
    REQUIRE(Has(alone, "APPLY-DROP"));
    // Beside its pinned-zero sibling it is covered.
    const std::vector<std::string> v = manaoracle::PlanVerdict(s, /*pre=*/true, unpinned, { unpinned, pinned0 });
    INFO("with the sibling: ", Joined(v));
    CHECK_FALSE(Has(v, "APPLY-DROP"));
    CHECK(Has(v, "DROP-COVERED"));
}

TEST_CASE("mana oracle: repeated casts admit exactly bounded six-action order proofs")
{
    EnsureOracleCards();
    GameState s = Board({});
    s.players[0].life = s.players[1].life = 20;
    Hand(s, { "Diabolic Intent", "Diabolic Intent", "Shadowborn Apostle", "Shadowborn Apostle",
              "Dark Ritual", "Thrumming Stone", "Sire of Insanity" });
    // No source can pay the first cast. The multiset has 6!/(2!2!) = 180 orders,
    // so a PHANTOM verdict requires the oracle to inspect every one.
    const TurnSolver::Plan bounded = PlanOf({ CastAction(s, "Diabolic Intent"), CastAction(s, "Shadowborn Apostle"),
                                               CastAction(s, "Dark Ritual"), CastAction(s, "Thrumming Stone"),
                                               CastAction(s, "Shadowborn Apostle"), CastAction(s, "Diabolic Intent") });
    const auto exact = manaoracle::PlanVerdict(s, true, bounded, { bounded });
    CHECK(Has(exact, "PHANTOM"));
    CHECK_FALSE(Has(exact, "UNMODELED"));

    // Replacing one duplicate leaves 6!/2! = 360 distinct orders, beyond the bound.
    const TurnSolver::Plan beyond = PlanOf({ CastAction(s, "Diabolic Intent"), CastAction(s, "Shadowborn Apostle"),
                                              CastAction(s, "Dark Ritual"), CastAction(s, "Thrumming Stone"),
                                              CastAction(s, "Sire of Insanity"), CastAction(s, "Diabolic Intent") });
    const auto partial = manaoracle::PlanVerdict(s, true, beyond, { beyond });
    CHECK(Has(partial, "UNMODELED"));
    CHECK_FALSE(Has(partial, "PHANTOM"));
}

// THE EQUIP ROUTE'S TAPE (KittyEquipment mana-clean, 2026-11-04). The route's links go through RouteLinkApply: with no
// tape armed (all of play) it is ApplyPlan and nothing else; armed, it records the link -- the board before and after,
// the plan, the DFS frame -- and a DFS branch that does not win is rewound off it.
TEST_CASE("route tape: a link is ApplyPlan, recorded only while the oracle's tape is armed; the DFS rewind drops a branch")
{
    EnsureOracleCards();
    GameState s = Board({ "Plains", "Plains" });
    s.players[0].life = 20;
    s.players[1].life = 20;
    Hand(s, { "Bone Saw", "Accorder's Shield" });
    const TurnSolver::Plan saw    = PlanOf({ CastAction(s, "Bone Saw") });
    GameState plain = s, taped = s;
    REQUIRE(manaoracle::t_route_tape == nullptr);
    TurnSolver::ApplyPlan(plain, saw, /*is_pre_combat=*/true);
    GameState untaped = s;
    manaoracle::RouteLinkApply(untaped, saw, /*in_dfs=*/false);
    CHECK(untaped.battlefield.size() == plain.battlefield.size());
    CHECK(untaped.players[0].hand.size() == plain.players[0].hand.size());

    manaoracle::RouteTape tape;
    manaoracle::t_route_tape = &tape;
    manaoracle::RouteLinkApply(taped, saw, /*in_dfs=*/true);
    REQUIRE(tape.links.size() == 1);
    CHECK(tape.links[0].in_dfs);
    CHECK(tape.links[0].before.players[0].hand.size() == s.players[0].hand.size());
    CHECK(tape.links[0].after.battlefield.size() == plain.battlefield.size());
    CHECK(taped.battlefield.size() == plain.battlefield.size());
    const std::size_t mark = manaoracle::RouteTapeMark();
    const TurnSolver::Plan shield = PlanOf({ CastAction(taped, "Accorder's Shield") });
    manaoracle::RouteLinkApply(taped, shield, /*in_dfs=*/true);
    CHECK(tape.links.size() == 2);
    manaoracle::RouteTapeRewind(mark);
    CHECK(tape.links.size() == 1);
    {
        manaoracle::RouteTapeSuspend trial;   // a trial records nothing
        GameState probe = s;
        manaoracle::RouteLinkApply(probe, saw, /*in_dfs=*/false);
        CHECK(manaoracle::t_route_tape == nullptr);
    }
    CHECK(manaoracle::t_route_tape == &tape);
    CHECK(tape.links.size() == 1);
    manaoracle::t_route_tape = nullptr;
}

// The tap-ahead table (MTG_ORACLE_SCALED_AHEAD): while a cast's additional cost sacrifices an Elf, every scaled dork
// that counts it taps for one more BEFORE the sacrifice than after, and the surplus floats for the main's later casts.
// The plain table offers a paying set plus at most ONE further tap (ExtendSurplus) -- and every count of one class
// (the DFS), so two Priests beside Archdruid is already there. What it never offers is further taps of two DIFFERENT
// scaled classes beside a paying set: {Forest, Forest} paying Natural Order's {G}{G}-part with BOTH Archdruid and
// Priest tapped ahead of Elvish Mystic's sacrifice (three Elves each, six floating, where after the sacrifice they make
// two each). That is the table a re-pay of the earlier payment needs (handoff-stompy-mana.md, LINE_REPAY section).
TEST_CASE("mana oracle: scaled dorks counting the sacrifice victim can all tap ahead of the sacrifice")
{
    EnsureOracleCards();
    GameState s = Board({ "Forest", "Forest", "Elvish Mystic", "Elvish Archdruid", "Priest of Titania" });
    const int victim = s.battlefield[2].card.m_number;
    const std::vector<std::string> both = { "Forest:G", "Forest:G", "Elvish Archdruid:G", "Priest of Titania:G" };
    const auto plain = manaoracle::MinimalPayments(s, Cost(0, 0, 0, 0, 0, 2), false, nullptr);
    const auto ahead = manaoracle::MinimalPayments(s, Cost(0, 0, 0, 0, 0, 2), false, nullptr, false, false, nullptr, nullptr,
                                                   victim);
    CHECK_FALSE(HasOption(plain, both));
    CHECK(HasOption(ahead, both));
    // A victim no scaled dork counts (not an Elf) adds nothing.
    GameState t = Board({ "Forest", "Forest", "Craterhoof Behemoth", "Elvish Archdruid", "Priest of Titania" });
    const auto none = manaoracle::MinimalPayments(t, Cost(0, 0, 0, 0, 0, 2), false, nullptr, false, false, nullptr, nullptr,
                                                  t.battlefield[2].card.m_number);
    CHECK(none.size() == manaoracle::MinimalPayments(t, Cost(0, 0, 0, 0, 0, 2), false, nullptr).size());
}

// StompySurprise mana-clean (APPLY-DROP, seed 31000062 T3 {Elvish Mystic, Natural Order, Worldly Tutor}): Natural
// Order's {2}{G}{G} with its victim Elvish Mystic #2 untapped. The victim's mana is free (MTG_SAC_FODDER_PAYS):
// Mystic + Archdruid (three Elves) pay the bill and the Forest stays up for Worldly Tutor.
TEST_CASE("payment: Natural Order's victim pays before a land (sac-fodder first, scaled Archdruid beside it)")
{
    EnsureOracleCards();
    GameState s = Board({ "Forest", "Elvish Mystic", "Forest", "Elvish Archdruid", "Elvish Mystic" });
    s.deck_has_mana_untapper = false;   // StompySurprise has none: the payment refine may trade a float for a source
    s.battlefield[0].tapped = true;
    s.battlefield[4].entered_this_turn = true;
    Hand(s, { "Worldly Tutor" });
    bool ok = false;
    {
        PaySacVictimScope _psv(s.battlefield[1].card.m_number);
        ok = TapForCostDirect(s, Cost(2, 0, 0, 0, 0, 2), /*for_creature=*/false);
    }
    REQUIRE(ok);
    CHECK(s.battlefield[1].tapped);         // the victim
    CHECK(s.battlefield[3].tapped);         // Archdruid, three Elves
    CHECK_FALSE(s.battlefield[2].tapped);   // the Forest Worldly Tutor needs
}

// StompySurprise mana-clean (DOMINATED-CREATURE, seed 30004002 T3): {Elvish Mystic, Elvish Archdruid} on {Forest,
// Llanowar Elves, Elvish Archdruid}. The whole-turn prepay taps before the first cast resolves, so the Archdruid pays
// for two Elves and the batch's four mana takes all three sources. Mystic is food the Archdruid counts and the board
// without it cannot pay the batch, so the prepay declines (MTG_PREPAY_SCALER_GROWN) and the per-cast payer pays
// Mystic off the Forest, then the Archdruid alone (three Elves) pays {2}{G}: Llanowar Elves stays up to attack.
TEST_CASE("prepay: declines when the batch grows a scaled dork it needs")
{
    EnsureOracleCards();
    auto run = [](int arm, bool* prepaid) -> GameState
    {
        GameState s = Board({ "Forest", "Llanowar Elves", "Elvish Archdruid" });
        Card c1 = CardDatabase::Instance().Lookup("Elvish Mystic")->card;    c1.m_number = 11;
        Card c2 = CardDatabase::Instance().Lookup("Elvish Archdruid")->card; c2.m_number = 12;
        s.players[0].hand = { c1, c2 };
        const std::vector<Action> line = { CastAction(s, "Elvish Mystic"), CastAction(s, "Elvish Archdruid") };
        const std::int8_t saved = heurarm::t_arm[heurarm::PREPAY_SCALER_GROWN];
        heurarm::t_arm[heurarm::PREPAY_SCALER_GROWN] = static_cast<std::int8_t>(arm);
        *prepaid = TurnSolver::BatchPrepayMainCasts(s, line);
        heurarm::t_arm[heurarm::PREPAY_SCALER_GROWN] = saved;
        return s;
    };
    bool prepaid = true;
    const GameState on = run(1, &prepaid);
    CHECK_FALSE(prepaid);
    for (const Permanent& p : on.battlefield) { CHECK_FALSE(p.tapped); }   // a decline leaves the board untouched
    const GameState off = run(0, &prepaid);
    CHECK(prepaid);
    CHECK(off.battlefield[1].tapped);   // the old prepay: Llanowar Elves tapped with the rest
    // No food in the batch: the prepay runs as before.
    GameState t = Board({ "Forest", "Forest", "Llanowar Elves", "Elvish Archdruid" });
    Card w1 = CardDatabase::Instance().Lookup("Worldly Tutor")->card; w1.m_number = 21;
    Card w2 = CardDatabase::Instance().Lookup("Worldly Tutor")->card; w2.m_number = 22;
    t.players[0].hand = { w1, w2 };
    std::vector<Action> line2 = { CastAction(t, "Worldly Tutor"), CastAction(t, "Worldly Tutor") };
    line2[1].hand_index = 1;
    CHECK(TurnSolver::BatchPrepayMainCasts(t, line2));
}
