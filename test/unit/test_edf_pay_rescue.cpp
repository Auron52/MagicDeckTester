// The EldraziDisplacerFlicker payment rescues (2026-10-31; docs/design/mana-completeness-oracle.md 13.7).
//
// The mana oracle's ENUM-GAP census (EDF, seed 1100001, 20 games) found 14 plans a mana gate kept off the
// menu that an order and payment realise. They fall in five shapes, one lever each (DEFAULT ON but
// MTG_EF_SCALAR_REFUND, OFF pending the USER); each case below forces its lever both ways, pins the shape on the
// board it was found on and shows the lever is what puts it on the menu:
//   MTG_ENERGY_AURA_PIP          a pip off an Aether Hub whose land Aura pays it keeps the {E} (the deck
//                                owner's report, viewer seed 3432 T3: Wild Growth paid by the Hub's
//                                any-colour mode)
//   MTG_TAPAHEAD_ENERGY_COVER    the untap tap-ahead does not count the Hub's energy colour as cover
//   MTG_PAINLAND_TAPAHEAD_CAST_AUTO  a painland banks its painless {C} ahead of an untap that returns it
//   MTG_EF_SCALAR_REFUND         the rescue's scalar ceiling counts the untap refund
//   MTG_ETB_BOUND_AURA           the odometer's untap refund counts a same-selection land Aura
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
#include <map>
#include <string>
#include <vector>

namespace
{

void EnsureCardsPr()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvPr()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhPr(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// A board builder. `salt` keeps the two arms' boards apart in every per-thread memo.
struct BoardPr
{
    GameState s;
    int       num = 1;
    explicit BoardPr(int turn, int salt)
    {
        EnsureCardsPr();
        s.active_player_index = 0;
        s.turn_number         = turn;
        s.game_seed           = 4242 + salt;
        s.players[0].life     = gamesetup::StartingLife();
        s.players[1].life     = 20;
        s.deck_has_land_aura  = true;
        s.m_provider          = &ProvPr();
        s.players[0].lands_played_this_turn = 1;
        for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhPr("Conservatory", 500 + i)); }
    }
    int Put(const char* nm, int host = 0, bool fresh = false)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = fresh;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    void Hand(const char* nm) { s.players[0].hand.push_back(PhPr(nm, num++)); }
};

struct ArmPr
{
    heurarm::Slot slot;
    ArmPr(heurarm::Slot sl, bool on) : slot(sl) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~ArmPr() { heurarm::t_arm[slot] = -1; }
};

// The hand casts of a plan, as a sorted name list (land drops and activations aside).
std::vector<std::string> Casts(const TurnSolver::Plan& p)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    std::sort(v.begin(), v.end());
    return v;
}

// The first menu plan casting exactly `want` (any land-Aura host unless `host` is given), or nullptr.
const TurnSolver::Plan* Find(const std::vector<TurnSolver::Plan>& menu, std::vector<std::string> want, int host = 0)
{
    std::sort(want.begin(), want.end());
    for (const TurnSolver::Plan& p : menu)
    {
        if (Casts(p) != want) { continue; }
        if (host != 0)
        {
            bool ok = true;
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::CastFromHand && a.def && a.def->params.is_land_aura && a.enchant_target != host)
                { ok = false; }
            }
            if (!ok) { continue; }
        }
        return &p;
    }
    return nullptr;
}

int OnBoard(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// Is a plan casting exactly `want` on the menu, and does the search's own apply cast every card of it?
bool OfferedAndRealised(const GameState& s, const std::vector<std::string>& want, int host = 0)
{
    const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* p = Find(menu, want, host);
    if (p == nullptr) { return false; }
    GameState r = s;
    TurnSolver::ApplyPlan(r, *p, /*is_pre_combat=*/true);
    std::map<std::string, int> need;
    for (const std::string& w : want) { ++need[w]; }
    for (const auto& kv : need) { if (OnBoard(r, kv.first) < kv.second) { return false; } }
    return true;
}

}   // namespace

// ---- the deck owner's report: a pip off an Aether Hub whose Aura already pays it keeps the {E} ------------------
// Viewer seed 3432 T3: Kitchen untapped, Aether Hub carrying Wild Growth, {E} 1; Wild Growth onto the Kitchen.
// The host is held while the Aura pays, so the Hub pays the {G}: its free {C} plus Wild Growth's {G} does it.
namespace
{
GameState Board3432(int salt)
{
    BoardPr b(3, salt);
    b.Put("Kitchen");
    const int hub = b.Put("Aether Hub");
    b.Put("Wild Growth", hub);
    b.Hand("Wild Growth");
    b.Hand("Cloud of Faeries");
    b.Hand("Eldrazi Displacer");
    b.s.players[0].energy_counters = 1;
    return b.s;
}
int KitchenOf(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Kitchen") { return p.card.m_number; } }
    return 0;
}
// Pay Wild Growth's {G} as the apply worlds do it: inside the line's hold, with the declared host held.
int EnergyAfterWildGrowth(bool lever, int salt, bool* paid)
{
    ArmPr arm(heurarm::ENERGY_AURA_PIP, lever);
    GameState s = Board3432(salt);
    const int kitchen = KitchenOf(s);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == kitchen) { p.tapped = true; } }   // the host hold
    const ManaCost g = CardDatabase::Instance().Lookup("Wild Growth")->card.m_mana_cost;
    LineUnpaidCostScope line(g);
    *paid = TapForCostDirect(s, g, /*for_creature=*/false);
    return s.players[0].energy_counters;
}
}   // namespace

TEST_CASE("EDF seed 3432: Wild Growth's {G} off an Aether Hub under Wild Growth keeps the {E} (MTG_ENERGY_AURA_PIP)")
{
    bool paid_on = false, paid_off = false;
    const int e_on  = EnergyAfterWildGrowth(true, 0, &paid_on);
    const int e_off = EnergyAfterWildGrowth(false, 1, &paid_off);
    CHECK(paid_on);
    CHECK(e_on == 1);
    // Precondition: the lever-off payer spends the energy on the any-colour mode.
    CHECK(paid_off);
    CHECK_MESSAGE(e_off == 0, "the lever-off payer should take the Hub's any-colour mode for the {G}");
}

TEST_CASE("EDF seed 3432: the search's apply of {Wild Growth -> Kitchen} keeps the {E} (MTG_ENERGY_AURA_PIP)")
{
    for (int lever = 1; lever >= 0; --lever)
    {
        ArmPr arm(heurarm::ENERGY_AURA_PIP, lever == 1);
        const GameState s = Board3432(10 + lever);
        const int kitchen = KitchenOf(s);
        // The human's line: Wild Growth alone, onto the Kitchen (the search's own menu folds the host variants,
        // so the action is taken from any menu plan casting it and re-aimed, as the viewer's host pick does).
        const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, true);
        TurnSolver::Plan line;
        for (const TurnSolver::Plan& m : menu)
        {
            for (const Action& a : m.actions)
            {
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Wild Growth" && line.actions.empty())
                { line.actions.push_back(a); line.actions.back().enchant_target = kitchen; }
            }
        }
        REQUIRE(line.actions.size() == 1);
        GameState r = s;
        TurnSolver::ApplyPlan(r, line, true);
        CHECK(OnBoard(r, "Wild Growth") == 2);
        if (lever == 1) { CHECK(r.players[0].energy_counters == 1); }
        else            { CHECK(r.players[0].energy_counters == 0); }
    }
}

// Outside a line nothing says what the floating unit is for, and with a later {G} pip owed in the line the {G}
// may be the one it needs: both keep the coloured tap.
TEST_CASE("EDF: the energy rule stands down when the line still owes a pip of that colour (MTG_ENERGY_AURA_PIP)")
{
    ArmPr arm(heurarm::ENERGY_AURA_PIP, true);
    GameState s = Board3432(20);
    const int kitchen = KitchenOf(s);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == kitchen) { p.tapped = true; } }
    const ManaCost g = CardDatabase::Instance().Lookup("Wild Growth")->card.m_mana_cost;
    ManaCost line_total = g;
    line_total.green += 1;   // a second {G} still owed after this payment
    LineUnpaidCostScope line(line_total);
    CHECK(TapForCostDirect(s, g, false));
    CHECK(s.players[0].energy_counters == 0);
}

// ---- an energy colour is not free cover for the tap-ahead -----------------------------------------------------
// EDF seed 1100006 gi5 T2: Kitchen, an Aether Hub played this turn ({E} 1); {Trace of Abundance, Cloud of Faeries}.
// Only Cloud first pays: Kitchen's {U} + the Hub's {C} for Cloud, the untap, then Kitchen's {G} + the Hub's {E} for
// Trace's {R/W}{G}. The tap-ahead banked Kitchen as {G} because the Hub's energy "covered" Cloud's {U}.
namespace
{
GameState BoardGi5(int salt, bool adarkar)
{
    BoardPr b(2, salt);
    b.Put("Kitchen");
    b.Put(adarkar ? "Adarkar Wastes" : "Aether Hub", 0, /*fresh=*/true);
    b.Hand("Living Wish");
    b.Hand("Trace of Abundance");
    b.Hand("Cloud of Faeries");
    b.Hand("Cloud of Faeries");
    b.s.players[0].energy_counters = adarkar ? 0 : 1;
    return b.s;
}
}   // namespace

TEST_CASE("EDF gi5: {Trace of Abundance, Cloud of Faeries} with an Aether Hub is offered and cast (MTG_TAPAHEAD_ENERGY_COVER)")
{
    {
        ArmPr arm(heurarm::TAPAHEAD_ENERGY_COVER, true);
        CHECK(OfferedAndRealised(BoardGi5(30, false), { "Trace of Abundance", "Cloud of Faeries" }));
    }
    {
        ArmPr arm(heurarm::TAPAHEAD_ENERGY_COVER, false);
        CHECK_FALSE(OfferedAndRealised(BoardGi5(31, false), { "Trace of Abundance", "Cloud of Faeries" }));
    }
}

// ---- the rescue's scalar ceiling counts the untap refund -------------------------------------------------------
// The same gi5 board with Adarkar Wastes as the land: {Trace of Abundance, Cloud of Faeries x2} owes four coloured
// pips against two lands plus Trace's one; each Cloud's two untaps pay the next cast.
TEST_CASE("EDF gi5: {Trace of Abundance, Cloud of Faeries x2} passes the scalar ceiling (MTG_EF_SCALAR_REFUND)")
{
    const std::vector<std::string> want = { "Trace of Abundance", "Cloud of Faeries", "Cloud of Faeries" };
    {
        ArmPr arm(heurarm::EF_SCALAR_REFUND, true);
        CHECK(OfferedAndRealised(BoardGi5(40, true), want));
    }
    {
        ArmPr arm(heurarm::EF_SCALAR_REFUND, false);
        CHECK_FALSE(OfferedAndRealised(BoardGi5(41, true), want));
    }
}

// ---- a painland banks its painless {C} ahead of an untap that gives it back ------------------------------------
// EDF seed 1100003 gi2 T3: Brushland + Wild Growth, Mariposa Military Base + Trace of Abundance; {Cloud of Faeries,
// Peregrine Drake}. Cloud first with both lands banked floats four, Cloud's untap returns both, and the Drake's
// {4}{U} is the two lands again plus the two left floating.
namespace
{
GameState BoardGi2(int salt)
{
    BoardPr b(3, salt);
    const int brush = b.Put("Brushland");
    b.Put("Wild Growth", brush);
    const int mari = b.Put("Mariposa Military Base");
    b.Put("Trace of Abundance", mari);
    b.Hand("Peregrine Drake");
    b.Hand("Cloud of Faeries");
    b.Hand("Eldrazi Displacer");
    return b.s;
}
}   // namespace

TEST_CASE("EDF gi2: {Cloud of Faeries, Peregrine Drake} with the painland banked is offered and cast (MTG_PAINLAND_TAPAHEAD_CAST_AUTO)")
{
    {
        ArmPr arm(heurarm::PAINLAND_TAPAHEAD_CAST_AUTO, true);
        CHECK(OfferedAndRealised(BoardGi2(50), { "Peregrine Drake", "Cloud of Faeries" }));
    }
    {
        ArmPr arm(heurarm::PAINLAND_TAPAHEAD_CAST_AUTO, false);
        CHECK_FALSE(OfferedAndRealised(BoardGi2(51), { "Peregrine Drake", "Cloud of Faeries" }));
    }
}

// ---- the odometer's untap refund counts a same-selection land Aura ---------------------------------------------
// EDF seed 1100009 gi8 T2: Adarkar Wastes, Brushland played this turn; {Wild Growth -> Adarkar, Cloud of Faeries,
// Eldrazi Displacer} = 6 against a bound of pool 2 + Wild Growth 1 + Cloud's board refund 2. Cloud untaps the
// enchanted Adarkar, whose next tap brings the {G} again.
namespace
{
GameState BoardGi8(int salt, int* adarkar)
{
    BoardPr b(2, salt);
    *adarkar = b.Put("Adarkar Wastes");
    b.Put("Brushland", 0, /*fresh=*/true);
    b.Hand("Wild Growth");
    b.Hand("Trace of Abundance");
    b.Hand("Eldrazi Displacer");
    b.Hand("Cloud of Faeries");
    b.Hand("Training Grounds");
    return b.s;
}
}   // namespace

TEST_CASE("EDF gi8: {Wild Growth -> Adarkar, Cloud of Faeries, Eldrazi Displacer} survives the odometer (MTG_ETB_BOUND_AURA)")
{
    const std::vector<std::string> want = { "Wild Growth", "Cloud of Faeries", "Eldrazi Displacer" };
    {
        ArmPr arm(heurarm::ETB_BOUND_AURA, true);
        int ad = 0;
        const GameState s = BoardGi8(60, &ad);
        CHECK(OfferedAndRealised(s, want, ad));
    }
    {
        ArmPr arm(heurarm::ETB_BOUND_AURA, false);
        int ad = 0;
        const GameState s = BoardGi8(61, &ad);
        CHECK_FALSE(OfferedAndRealised(s, want, ad));
    }
}
