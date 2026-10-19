// Unit tests for the LandfallElementals claude-play sweep fixes (2026-10-19): Smokebraider's
// restricted mana and Incandescent Soulstoke's put. Each case pins one sweep flag:
//   B / I  one copy-accounting guard per plan (casts, Vial puts, Soulstoke puts and Fury pitches may
//          not use more copies of a name than the hand holds), and a put that checks its card BEFORE
//          paying, in both worlds;
//   D      the enumeration never prices an ineligible cost (Metallic Mimic, Aether Vial) off
//          Elemental-only mana, and a put is offered only when THIS ability can be paid (Unclaimed
//          Territory makes only {C} for an ability; Secluded Courtyard's coloured mana pays it);
//   H      the colour gate credits the restricted float for a spell it may pay;
//   G      the restricted float's wild unit goes to the pip the board cannot make;
//   C / E  the restricted-first rank's attack exception never strands the declared line;
//   R      a stranded activation is reported to the viewer's drop sink.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsFp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefF(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int g_nf = 9000;

GameState BoardF()
{
    EnsureCardsFp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    // An Elemental library (Metallic Mimic's chosen type is then Elemental, and a draw never decks us).
    for (int k = 0; k < 12; ++k)
    {
        Card c = DefF(k % 3 == 0 ? "Smokebraider" : (k % 3 == 1 ? "Incandescent Soulstoke" : "Fury")).card;
        c.m_number = g_nf++;
        s.players[0].library.push_back(c);
    }
    return s;
}

int PutF(GameState& s, const std::string& name, bool sick = false, bool tapped = false)
{
    Permanent p;
    p.card              = DefF(name).card;
    p.card.m_number     = g_nf++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

int HandF(GameState& s, const std::string& name)
{
    Card c = DefF(name).card;
    c.m_number = g_nf++;
    s.players[0].hand.push_back(c);
    return c.m_number;
}

const Permanent* ByNumF(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
int OnBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

ManaCost CostF(int generic, int w = 0, int u = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}

std::vector<TurnSolver::Plan> PlansF(const GameState& s, bool pre = true)
{
    return TurnSolver::EnumerateMainPlans(s, pre);
}

bool IsCast(const Action& a, const std::string& n)
{ return a.kind == Action::Kind::CastFromHand && a.card_name.str() == n; }
bool IsPut(const Action& a, const std::string& n)
{ return a.kind == Action::Kind::PutFromHandAbility && a.card_name.str() == n; }
bool IsVial(const Action& a, const std::string& n)
{ return a.kind == Action::Kind::ActivateVial && a.card_name.str() == n; }
bool IsPitch(const Action& a, const std::string& n)
{ return a.kind == Action::Kind::CastFromHand && a.evoke && a.pitch_name.str() == n; }

template <class Pred>
bool PlanHas(const TurnSolver::Plan& p, Pred pred)
{
    for (const Action& a : p.actions) { if (pred(a)) { return true; } }
    return false;
}

// Binds the line hold the plan-apply paths bind (what the line still owes, this payment included).
struct LineOwes
{
    ManaCost saved;
    explicit LineOwes(const ManaCost& c) : saved(g_line_unpaid_cost) { g_line_unpaid_cost = c; }
    ~LineOwes() { g_line_unpaid_cost = saved; }
};

struct PayF { bool ok = false; GameState after; };
PayF ExecPayF(const GameState& board, const ManaCost& c, const std::string& spell)
{
    const CardDefinition& d = DefF(spell);
    SpellSubtypePayScope scope(&d.card);
    PayF r; r.after = board;
    AIEngine eng;
    ManaPool avail = AvailableManaPool(r.after);
    r.ok = MtgTestSeam::TapForCost(eng, r.after, c, avail, d.card.IsCreature());
    return r;
}
PayF RollPayF(const GameState& board, const ManaCost& c, const std::string& spell)
{
    const CardDefinition& d = DefF(spell);
    SpellSubtypePayScope scope(&d.card);
    PayF r; r.after = board;
    r.ok = TapForCostDirect(r.after, c, d.card.IsCreature());
    return r;
}

// Smokebraider's leftover unit, floating WILD under its own restriction.
void FloatSmokeWild(GameState& s, int units)
{
    s.floating_bigspell_mana.wild = units;
    s.floating_bigspell_pp        = &DefF("Smokebraider").params;
}

}   // namespace

// ===================================== B / I: COPY ACCOUNTING =====================================

TEST_CASE("LFE fix B: no plan casts AND Soulstoke-puts the only copy of a card")
{
    GameState s = BoardF();
    PutF(s, "Incandescent Soulstoke");
    PutF(s, "Mountain"); PutF(s, "Mountain"); PutF(s, "Mountain"); PutF(s, "Forest");
    HandF(s, "Smokebraider");
    bool saw_cast = false, saw_put = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        const bool c = PlanHas(p, [](const Action& a) { return IsCast(a, "Smokebraider"); });
        const bool u = PlanHas(p, [](const Action& a) { return IsPut(a, "Smokebraider"); });
        saw_cast = saw_cast || c;
        saw_put  = saw_put || u;
        CHECK_FALSE((c && u));
    }
    CHECK(saw_cast);
    CHECK(saw_put);
    // With TWO copies both uses are legal together.
    HandF(s, "Smokebraider");
    bool saw_both = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        saw_both = saw_both || (PlanHas(p, [](const Action& a) { return IsCast(a, "Smokebraider"); })
                                && PlanHas(p, [](const Action& a) { return IsPut(a, "Smokebraider"); }));
    }
    CHECK(saw_both);
}

TEST_CASE("LFE fix I: no plan Vial-puts a card AND pitches the same (only) copy to Fury, or puts it twice")
{
    GameState s = BoardF();
    PutF(s, "Aether Vial");
    s.battlefield.back().charge_counters = 2;   // Smokebraider's mana value
    PutF(s, "Incandescent Soulstoke");
    PutF(s, "Mountain"); PutF(s, "Mountain");
    HandF(s, "Fury");
    HandF(s, "Smokebraider");
    bool saw_vial = false, saw_pitch = false, saw_put = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        const bool v = PlanHas(p, [](const Action& a) { return IsVial(a, "Smokebraider"); });
        const bool e = PlanHas(p, [](const Action& a) { return IsPitch(a, "Smokebraider"); });
        const bool u = PlanHas(p, [](const Action& a) { return IsPut(a, "Smokebraider"); });
        saw_vial = saw_vial || v; saw_pitch = saw_pitch || e; saw_put = saw_put || u;
        CHECK_FALSE((v && e));
        CHECK_FALSE((v && u));
        CHECK_FALSE((u && e));
    }
    CHECK(saw_vial);
    CHECK(saw_pitch);
    CHECK(saw_put);
}

// ============================ B: THE PUT CHECKS ITS CARD BEFORE PAYING ============================

TEST_CASE("LFE fix B: a put whose card is gone pays nothing and taps nothing (rollout apply)")
{
    GameState s = BoardF();
    const int soul = PutF(s, "Incandescent Soulstoke");
    const int m1 = PutF(s, "Mountain");
    const int m2 = PutF(s, "Mountain");
    HandF(s, "Fury");
    TurnSolver::Plan put_plan;
    bool found = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        if (p.actions.size() == 1 && IsPut(p.actions[0], "Fury")) { put_plan = p; found = true; }
    }
    REQUIRE(found);
    // The legal put: paid, the card enters.
    {
        GameState t = s;
        TurnSolver::ApplyPlan(t, put_plan, /*is_pre_combat=*/true);
        CHECK(OnBf(t, "Fury") == 1);
        CHECK(ByNumF(t, soul)->tapped);
    }
    // The card left the hand (another use took it): nothing is paid.
    s.players[0].hand.clear();
    std::vector<std::string> drops;
    g_play_dropped_cast_sink = &drops;
    TurnSolver::ApplyPlan(s, put_plan, /*is_pre_combat=*/true);
    g_play_dropped_cast_sink = nullptr;
    CHECK_FALSE(ByNumF(s, soul)->tapped);
    CHECK_FALSE(ByNumF(s, m1)->tapped);
    CHECK_FALSE(ByNumF(s, m2)->tapped);
    CHECK(s.floating_mana.Total() == 0);
    // R: the dropped put is reported, named.
    CHECK(std::find(drops.begin(), drops.end(), std::string("put Fury (activation)")) != drops.end());
}

TEST_CASE("LFE fix B: PutFromHandLegalIndex -- the shared legality both worlds check before paying")
{
    GameState s = BoardF();
    const int soul = PutF(s, "Incandescent Soulstoke");
    HandF(s, "Metallic Mimic");
    HandF(s, "Fury");
    CHECK(PutFromHandLegalIndex(s, 0, soul, "Fury") == 1);
    CHECK(PutFromHandLegalIndex(s, 0, soul, "Smokebraider") == -1);     // not in hand
    CHECK(PutFromHandLegalIndex(s, 0, soul, "Metallic Mimic") == -1);   // not an Elemental card
    GameState t = s;
    for (Permanent& p : t.battlefield) { if (p.card.m_number == soul) { p.tapped = true; } }
    CHECK(PutFromHandLegalIndex(t, 0, soul, "Fury") == -1);             // the {T} is spent
    GameState u = BoardF();
    const int sick = PutF(u, "Incandescent Soulstoke", /*sick=*/true);
    HandF(u, "Fury");
    CHECK(PutFromHandLegalIndex(u, 0, sick, "Fury") == -1);             // summoning sick
}

// ================================ D: ELIGIBILITY IN THE ENUMERATION ================================

TEST_CASE("LFE fix D: an untapped Smokebraider never funds Metallic Mimic or Aether Vial in a plan")
{
    GameState s = BoardF();
    PutF(s, "Smokebraider");
    PutF(s, "Island");
    HandF(s, "Metallic Mimic");   // {2}: one general mana on the board
    HandF(s, "Aether Vial");      // {1}: payable by the Island
    bool saw_vial = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        CHECK_FALSE(PlanHas(p, [](const Action& a) { return IsCast(a, "Metallic Mimic"); }));
        saw_vial = saw_vial || PlanHas(p, [](const Action& a) { return IsCast(a, "Aether Vial"); });
    }
    CHECK(saw_vial);
    // An ELEMENTAL is eligible: Risen Reef {1}{G}{U} = Smokebraider's two units + the Island.
    HandF(s, "Risen Reef");
    bool saw_reef = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        const bool reef = PlanHas(p, [](const Action& a) { return IsCast(a, "Risen Reef"); });
        saw_reef = saw_reef || reef;
        // ...and Reef + Vial would need the Island twice.
        CHECK_FALSE((reef && PlanHas(p, [](const Action& a) { return IsCast(a, "Aether Vial"); })));
    }
    CHECK(saw_reef);
}

TEST_CASE("LFE fix D/H: the restricted FLOAT pays an Elemental's colour and never an ineligible cost")
{
    GameState s = BoardF();
    FloatSmokeWild(s, 1);
    HandF(s, "Flamekin Harbinger");   // {R} Elemental: the wild unit pays it, no land needed (H)
    HandF(s, "Aether Vial");          // {1}: NOT payable by Elemental-only mana (D)
    bool saw_harb = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        saw_harb = saw_harb || PlanHas(p, [](const Action& a) { return IsCast(a, "Flamekin Harbinger"); });
        CHECK_FALSE(PlanHas(p, [](const Action& a) { return IsCast(a, "Aether Vial"); }));
    }
    CHECK(saw_harb);
}

TEST_CASE("LFE fix D (61013): a Soulstoke put is offered only when an untapped source can pay {1}{R} for an ABILITY")
{
    auto put_offered = [](const std::string& land)
    {
        GameState s = BoardF();
        PutF(s, "Incandescent Soulstoke");
        PutF(s, land);
        PutF(s, "Island");
        HandF(s, "Fury");
        for (const TurnSolver::Plan& p : PlansF(s))
        { if (PlanHas(p, [](const Action& a) { return IsPut(a, "Fury"); })) { return true; } }
        return false;
    };
    CHECK_FALSE(put_offered("Unclaimed Territory"));   // coloured only for creature SPELLS: {C} here
    CHECK(put_offered("Mountain"));
    CHECK(put_offered("Secluded Courtyard"));          // coloured for a creature's ABILITY too
}

TEST_CASE("LFE fix D: Secluded Courtyard pays a creature ability's coloured pip in BOTH payer twins")
{
    GameState s = BoardF();
    const int soul = PutF(s, "Incandescent Soulstoke");
    PutF(s, "Secluded Courtyard");
    PutF(s, "Island");
    const Card* src = AbilitySourceCard(s, 0, soul);
    REQUIRE(src != nullptr);
    CreatureAbilityPayScope scope(src);
    GameState a = s, b = s;
    AIEngine eng;
    ManaPool avail = AvailableManaPool(a);
    CHECK(MtgTestSeam::TapForCost(eng, a, CostF(1, 0, 0, 0, 1), avail, false));
    CHECK(TapForCostDirect(b, CostF(1, 0, 0, 0, 1), false));
}

// ===================================== G: WILD UNIT RE-SPLIT =====================================

TEST_CASE("LFE fix G: a floating wild unit pays the pip the board cannot make (Brushfire {R}{G} + Mountain)")
{
    GameState s = BoardF();
    FloatSmokeWild(s, 1);
    const int mtn = PutF(s, "Mountain");
    const PayF ex = ExecPayF(s, CostF(0, 0, 0, 0, 1, 1), "Brushfire Elemental");
    const PayF ro = RollPayF(s, CostF(0, 0, 0, 0, 1, 1), "Brushfire Elemental");
    CHECK(ex.ok);
    CHECK(ro.ok);
    CHECK(ByNumF(ro.after, mtn)->tapped);
    CHECK(ro.after.floating_bigspell_mana.Total() == 0);
}

// ============================== C / E: THE HOLD MAY NOT STRAND THE LINE ==============================

TEST_CASE("LFE fix E: with the line owing Metallic Mimic {2}, Smokebraider pays Brushfire and the lands stay up")
{
    GameState s = BoardF();
    const int smoke = PutF(s, "Smokebraider");   // can attack: the old attack exception held it back
    const int mtn   = PutF(s, "Mountain");
    const int frs   = PutF(s, "Forest");
    const ManaCost brush = CostF(0, 0, 0, 0, 1, 1);
    {
        LineOwes line(AddManaCosts(brush, CostF(2)));
        const PayF ex = ExecPayF(s, brush, "Brushfire Elemental");
        const PayF ro = RollPayF(s, brush, "Brushfire Elemental");
        REQUIRE(ex.ok);
        REQUIRE(ro.ok);
        for (const PayF* r : { &ex, &ro })
        {
            CHECK(ByNumF(r->after, smoke)->tapped);
            CHECK_FALSE(ByNumF(r->after, mtn)->tapped);
            CHECK_FALSE(ByNumF(r->after, frs)->tapped);
        }
    }
    // Nothing owed after it: the attack exception stands -- the lands pay, Smokebraider stays up to swing.
    {
        LineOwes line(brush);
        const PayF ro = RollPayF(s, brush, "Brushfire Elemental");
        REQUIRE(ro.ok);
        CHECK_FALSE(ByNumF(ro.after, smoke)->tapped);
        CHECK(ByNumF(ro.after, mtn)->tapped);
        CHECK(ByNumF(ro.after, frs)->tapped);
    }
}

TEST_CASE("LFE fix C: the line 'Brushfire Elemental, Metallic Mimic' is applied whole by the rollout")
{
    GameState s = BoardF();
    PutF(s, "Smokebraider");
    PutF(s, "Mountain");
    PutF(s, "Forest");
    HandF(s, "Brushfire Elemental");
    HandF(s, "Metallic Mimic");
    TurnSolver::Plan both;
    bool found = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        if (PlanHas(p, [](const Action& a) { return IsCast(a, "Brushfire Elemental"); })
            && PlanHas(p, [](const Action& a) { return IsCast(a, "Metallic Mimic"); }))
        { both = p; found = true; }
    }
    REQUIRE(found);
    std::vector<std::string> drops;
    g_play_dropped_cast_sink = &drops;
    TurnSolver::ApplyPlan(s, both, /*is_pre_combat=*/true);
    g_play_dropped_cast_sink = nullptr;
    CHECK(OnBf(s, "Brushfire Elemental") == 1);
    CHECK(OnBf(s, "Metallic Mimic") == 1);
    CHECK(drops.empty());
}

// ===================================== R: STRANDED ACTIVATION =====================================

TEST_CASE("LFE fix R: a land activation the line can no longer pay is reported to the drop sink")
{
    GameState s = BoardF();
    // Basics to find (a crack with nothing legal to find is pruned as a dominated self-sacrifice).
    for (int k = 0; k < 2; ++k)
    {
        Card c = DefF("Mountain").card;
        c.m_number = g_nf++;
        s.players[0].library.insert(s.players[0].library.begin(), c);
    }
    PutF(s, "Myriad Landscape");
    const int m1 = PutF(s, "Mountain");
    const int m2 = PutF(s, "Mountain");
    TurnSolver::Plan crack;
    bool found = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        if (p.actions.size() == 1 && p.actions[0].card_name.str() == "Myriad Landscape"
            && p.actions[0].kind != Action::Kind::PlayLand)
        { crack = p; found = true; }
    }
    REQUIRE(found);
    for (Permanent& p : s.battlefield)
    { if (p.card.m_number == m1 || p.card.m_number == m2) { p.tapped = true; } }
    std::vector<std::string> drops;
    g_play_dropped_cast_sink = &drops;
    TurnSolver::ApplyPlan(s, crack, /*is_pre_combat=*/true);
    g_play_dropped_cast_sink = nullptr;
    CHECK(OnBf(s, "Myriad Landscape") == 1);
    CHECK(std::find(drops.begin(), drops.end(), std::string("Myriad Landscape (activation)")) != drops.end());
}

// ======================= THE LINE TEST COUNTS ONLY WHAT THE RESTRICTED SOURCE CANNOT PAY =======================

namespace
{
// Apply the plan casting exactly `first` then `second` (in that order) and report drops.
bool ApplyPairF(GameState& s, const std::string& first, const std::string& second,
                std::vector<std::string>& drops)
{
    TurnSolver::Plan pick;
    bool found = false;
    for (const TurnSolver::Plan& p : PlansF(s))
    {
        int i1 = -1, i2 = -1;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[static_cast<std::size_t>(i)];
            if (i1 < 0 && (IsCast(a, first) || IsPut(a, first))) { i1 = i; }
            else if (i2 < 0 && (IsCast(a, second) || IsPut(a, second))) { i2 = i; }
        }
        if (i1 >= 0 && i2 > i1 && p.actions.size() == 2) { pick = p; found = true; break; }
    }
    if (!found) { return false; }
    g_play_dropped_cast_sink = &drops;
    TurnSolver::ApplyPlan(s, pick, /*is_pre_combat=*/true);
    g_play_dropped_cast_sink = nullptr;
    return true;
}
}   // namespace

TEST_CASE("LFE fix C (61010 T4): Soulstoke then Risen Reef -- Smokebraider is HELD for Reef's {G}, both land")
{
    // Both casts are Elementals. Spending Smokebraider on Soulstoke {2}{R} first left Reef's {G} with no
    // source; the lands (Mountain, two Myriads) pay Soulstoke, Smokebraider + Island pay Reef.
    GameState s = BoardF();
    PutF(s, "Smokebraider");
    PutF(s, "Mountain");
    PutF(s, "Myriad Landscape");
    PutF(s, "Myriad Landscape");
    PutF(s, "Island");
    HandF(s, "Incandescent Soulstoke");
    HandF(s, "Risen Reef");
    std::vector<std::string> drops;
    REQUIRE(ApplyPairF(s, "Incandescent Soulstoke", "Risen Reef", drops));
    CHECK(OnBf(s, "Incandescent Soulstoke") == 1);
    CHECK(OnBf(s, "Risen Reef") == 1);
    CHECK(drops.empty());
}

TEST_CASE("LFE fix C (61016 T9): Risen Reef then Metallic Mimic -- Gruul Turf's surplus stays GENERAL")
{
    // Smokebraider pays Reef's {U}{G}; Gruul Turf's {R}{G} pays the {1} and its surplus unit must float
    // as general mana (it was booked Elemental-only), so Mimic's {2} = that unit + Secluded Courtyard.
    GameState s = BoardF();
    PutF(s, "Smokebraider");
    PutF(s, "Gruul Turf");
    PutF(s, "Secluded Courtyard");
    HandF(s, "Risen Reef");
    HandF(s, "Metallic Mimic");
    std::vector<std::string> drops;
    REQUIRE(ApplyPairF(s, "Risen Reef", "Metallic Mimic", drops));
    CHECK(OnBf(s, "Risen Reef") == 1);
    CHECK(OnBf(s, "Metallic Mimic") == 1);
    CHECK(drops.empty());
}

TEST_CASE("LFE fix C (61010 T6): Risen Reef then a Soulstoke put -- the put's {R} source is held from the cast")
{
    // Unclaimed Territory's colours pay creature SPELLS only; Gruul Turf is the only {R} the put's
    // ability may use. Reef = Territory + Territory + Myriad, the put = Turf.
    GameState s = BoardF();
    PutF(s, "Incandescent Soulstoke");
    PutF(s, "Gruul Turf");
    PutF(s, "Unclaimed Territory");
    PutF(s, "Unclaimed Territory");
    PutF(s, "Myriad Landscape");
    HandF(s, "Risen Reef");
    HandF(s, "Fury");
    std::vector<std::string> drops;
    REQUIRE(ApplyPairF(s, "Risen Reef", "Fury", drops));
    CHECK(OnBf(s, "Risen Reef") == 1);
    CHECK(OnBf(s, "Fury") == 1);
    CHECK(drops.empty());
}

TEST_CASE("LFE fix D (Angels 2hg gi42): an untapped Giada never shrinks the NONCREATURE pool it was never in")
{
    // Giada's {W} pays Angel SPELLS only. The noncreature pool never held it, so taking it off that pool
    // as well under-counted it: Resplendent Angel's {3}{W}{W}{W} pump, payable off four Plains and a
    // Sol Ring, vanished from the menu (and the T4 kill with it).
    auto pump_offered = [](int plains)
    {
        GameState s = BoardF();
        PutF(s, "Giada, Font of Hope");
        PutF(s, "Resplendent Angel");
        PutF(s, "Sol Ring");
        for (int k = 0; k < plains; ++k) { PutF(s, "Plains"); }
        for (const TurnSolver::Plan& p : PlansF(s))
        {
            if (PlanHas(p, [](const Action& a)
                { return a.card_name.str() == "Resplendent Angel" && a.kind != Action::Kind::CastFromHand; }))
            { return true; }
        }
        return false;
    };
    CHECK(pump_offered(4));          // 4 Plains + Sol Ring = exactly {3}{W}{W}{W}
    CHECK_FALSE(pump_offered(3));    // ...and Giada's {W} can never be the sixth mana of an ability
}

// ===================== THE BATCH PREPAY HONOURS GIADA'S ANGEL-ONLY MANA (Angels 2HG gi308) =====================

namespace
{
std::vector<Action> HandCastsF(const GameState& s, const std::vector<std::string>& names)
{
    std::vector<Action> acts;
    for (const std::string& n : names)
    {
        const CardDefinition& d = DefF(n);
        Action a;
        a.kind = Action::Kind::CastFromHand;
        a.card_name = d.card.m_name;
        a.def = &d;
        a.cost = d.card.m_mana_cost;
        for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
        { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name == d.card.m_name) { a.hand_index = i; break; } }
        acts.push_back(a);
    }
    return acts;
}
}   // namespace

TEST_CASE("Angels fix (2HG gi308): the whole-turn prepay never spends Giada's {W} on a batch with no Angel")
{
    // Plains, Plains, Seraph Sanctuary ({C}) and Giada: Lightning Greaves {2} + Bishop of Wings {W}{W} is
    // four mana for two non-Angels off three general sources. The joint solve had no spell identity,
    // read Giada as payable, and tapped her: the batch "paid", Bishop and Greaves both resolved.
    GameState s = BoardF();
    PutF(s, "Plains"); PutF(s, "Plains"); PutF(s, "Seraph Sanctuary");
    const int giada = PutF(s, "Giada, Font of Hope");
    HandF(s, "Lightning Greaves"); HandF(s, "Bishop of Wings");
    const std::vector<Action> acts = HandCastsF(s, { "Lightning Greaves", "Bishop of Wings" });
    GameState t = s;
    CHECK_FALSE(TurnSolver::BatchPrepayMainCasts(t, acts));
    CHECK_FALSE(ByNumF(t, giada)->tapped);
    CHECK(t.floating_mana.Total() == 0);

    // A batch whose Angel casts can absorb her {W} may still use her (unchanged): Lyra + Righteous
    // Valkyrie ({2}{W} each) off four Plains, Seraph Sanctuary and Giada needs all six.
    GameState u = BoardF();
    for (int k = 0; k < 4; ++k) { PutF(u, "Plains"); }
    PutF(u, "Seraph Sanctuary");
    const int giada_u = PutF(u, "Giada, Font of Hope");
    HandF(u, "Lyra, Archangel of Dawn"); HandF(u, "Righteous Valkyrie");
    const std::vector<Action> acts_u = HandCastsF(u, { "Lyra, Archangel of Dawn", "Righteous Valkyrie" });
    CHECK(TurnSolver::BatchPrepayMainCasts(u, acts_u));
    CHECK(ByNumF(u, giada_u)->tapped);
}
