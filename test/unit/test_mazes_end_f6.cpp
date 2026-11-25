// Maze's End deck, F6 (2026-11-19; docs/design/analysis-Maze's End.md "F6"): the USER's viewer rejection
// Maze_s_End_cod_s6_gi5_t5 -- "Mana difficulties ... In theory this is workable due to lands coming into play
// untapped." Board: Baldur's Gate, Gond Gate, three other Gates, two Maze's End; Sylvan Scrying in hand. The line
// "activate Maze's End, then Sylvan Scrying + the second activation" is payable only if the FIRST {3} leaves
// Baldur's Gate (fed later at X = 5, after the fetched Gate entered untapped under Gond Gate) and the second Maze's
// End untapped. The generic ladder ranks both {C}-only sources 5 (spend first), so the first payment ate them and
// the line was unreachable at every budget. MazesEndProvider::ManaSourceRank pays Baldur's Gate last WHILE THE LINE
// STILL OWES {feeder}+1 mana after the payment (F6-3: conditional; an unconditional hold failed d8 b0); the second
// Maze's End is kept by the plan's own activation-source hold (MTG_ACT_LINE_HOLD, PlanTraits::act_src_nums).
#include <doctest/doctest.h>

#include <optional>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/PlanContext.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

namespace
{
void LoadF6Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

bool Tapped(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p.tapped; } }
    FAIL("permanent not on the battlefield");
    return false;
}

// T5 of the rejection, Maze's End #43 already tapped for its own activation's {T}. Gates besides Baldur's: Gond,
// Simic, Selesnya, Gruul (X = 4 now, 5 once the fetch lands).
GameState Board(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 18;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Baldur's Gate", 7);
    Put(s, "Gond Gate", 29);
    Put(s, "Maze's End", 43, /*tapped=*/true);
    Put(s, "Maze's End", 46);
    Put(s, "Simic Guildgate", 53);
    Put(s, "Selesnya Guildgate", 50);
    Put(s, "Gruul Guildgate", 60);
    return s;
}

// The plan {activate Maze's End #43, activate #46} as the apply installs it: both activations' {3} owed, both {T}
// sources named. `line_casts` = what the line's casts still owe (Sylvan Scrying's {1}{G} in the USER's line).
PlanTraits TwoActsTraits()
{
    PlanTraits t;
    t.act_mana_total = 6;
    t.act_src_nums[0] = 43;
    t.act_src_nums[1] = 46;
    t.act_src_count = 2;
    return t;
}
}   // namespace

TEST_CASE("Maze's End F6: the first {3} of a two-activation plan keeps Baldur's Gate and the other Maze's End untapped")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost three; three.generic = 3;
    const PlanTraits t = TwoActsTraits();
    PlanTraitsScope _pts(&t);
    ActLinePassScope _pass;
    ActLinePayScope pay(three);
    REQUIRE(TapForCostDirect(s, three, /*for_creature=*/false));
    pay.Paid();
    CHECK_FALSE(Tapped(s, 7));    // Baldur's Gate: the line still owes {3}, so its fed mode will be used
    CHECK_FALSE(Tapped(s, 46));   // Maze's End: its {T} is the second activation (act_src_nums)
}

TEST_CASE("Maze's End F6: with nothing owed after the payment Baldur's Gate keeps the plain {C}-only rank")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost three; three.generic = 3;
    PlanTraits t;
    t.act_mana_total = 3;               // the single activation being paid
    t.act_src_nums[0] = 43;
    t.act_src_count = 1;
    PlanTraitsScope _pts(&t);
    ActLinePassScope _pass;
    ActLinePayScope pay(three);
    REQUIRE(TapForCostDirect(s, three, false));
    CHECK(Tapped(s, 7));          // the fed mode will not be used: spent first, as the base ladder does
}

TEST_CASE("Maze's End F6: after the fetched Gate enters untapped, Scrying + the second activation still pay")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost three; three.generic = 3;
    const PlanTraits t = TwoActsTraits();
    PlanTraitsScope _pts(&t);
    ActLinePassScope _pass;
    {
        ActLinePayScope pay(three);
        REQUIRE(TapForCostDirect(s, three, false));
        pay.Paid();
    }
    Put(s, "Azorius Guildgate", 70);   // Maze's End #43's fetch, untapped under Gond Gate (X -> 5)
    // Sylvan Scrying ({1}{G}) and the second activation's {3}, Maze's End #46 tapping itself: {4}{G} from what
    // the first payment left -- only Baldur's Gate's fed mode (X = 5) makes it.
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 46) { p.tapped = true; } }
    ManaCost rest; rest.generic = 4; rest.green = 1;
    CHECK(TapForCostDirect(s, rest, false));
}

TEST_CASE("Maze's End F6: the generic ladder (another provider) still spends the {C}-only sources first")
{
    LoadF6Cards();
    static const GenericProvider kGen;
    GameState s = Board(kGen);
    ManaCost three; three.generic = 3;
    REQUIRE(TapForCostDirect(s, three, false));
    // The pre-F6 order: at least one of the two {C}-only sources is spent on the first {3}.
    CHECK((Tapped(s, 7) || Tapped(s, 46)));
}

// ---- F6-2: Baldur's Gate's fed surplus floats (MTG_SCALED_FED_FLOAT; ManaPayment.cpp ScaledFedFloatAfter) ----------
// The rejection's exact T5 board (Baldur's Gate, Gond Gate, Sea Gate, Simic Guildgate, Thran Portal on Forest, Maze's
// End #43 / #46; Sylvan Scrying + Archdruid's Charm in hand) and the USER's line in the order the viewer validates
// it: activate Maze's End #43, activate #46, then Sylvan Scrying. 8 mana from 7 sources is payable only if the second
// {3} taps Baldur's Gate in its fed mode (X = 5 other Gates once the first fetch landed) and floats 2.
namespace
{
GameState S6Board(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 18;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Baldur's Gate", 7);
    Put(s, "Gond Gate", 29);
    Put(s, "Maze's End", 43);
    Put(s, "Maze's End", 46);
    Put(s, "Sea Gate", 50);
    Put(s, "Simic Guildgate", 53);
    Put(s, "Thran Portal", 60);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 60) { p.chosen_color = 4; } }   // Forest
    for (const char* n : { "Sylvan Scrying", "Archdruid's Charm" })
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE(d != nullptr);
        Card c = d->card; c.m_number = (n[0] == 'S') ? 57 : 5;
        s.players[0].hand.push_back(c);
    }
    return s;
}

bool TapSelf(GameState& s, int num)
{
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number != num) { continue; }
        if (p.tapped) { return false; }
        p.tapped = true;
        return true;
    }
    return false;
}

// Activations first, then Scrying. Returns whether Scrying's {1}{G} paid. `line` installs the line as the apply does
// (PlanTraits for the two activations, the line hold for Scrying's {1}{G}); without it the payer sees one bill at a
// time and Baldur's Gate keeps the plain rank.
bool S6ActsThenScrying(int fed_float_arm, int* float_after_second = nullptr, bool line = true)
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    heurarm::Clear();
    if (fed_float_arm >= 0) { heurarm::t_arm[heurarm::SCALED_FED_FLOAT] = static_cast<std::int8_t>(fed_float_arm); }
    GameState s = S6Board(kProv);
    ManaCost three; three.generic = 3;
    ManaCost scry; scry.generic = 1; scry.green = 1;
    const PlanTraits t = TwoActsTraits();
    std::optional<PlanTraitsScope> pts;
    std::optional<LineUnpaidCostScope> lus;
    if (line) { pts.emplace(&t); lus.emplace(scry); }
    ActLinePassScope _pass;
    REQUIRE(TapSelf(s, 43));
    {
        ActLinePayScope pay(three);
        REQUIRE(TapForCostDirect(s, three, false));
        pay.Paid();
    }
    Put(s, "Azorius Guildgate", 70);   // #43's fetch, untapped under Gond Gate
    if (!TapSelf(s, 46)) { heurarm::Clear(); return false; }   // #46 was spent on the first {3}
    {
        ActLinePayScope pay(three);
        REQUIRE(TapForCostDirect(s, three, false));
        pay.Paid();
    }
    if (float_after_second != nullptr) { *float_after_second = s.floating_mana.Total(); }
    Put(s, "Izzet Guildgate", 71);     // #46's fetch
    const bool ok = TapForCostDirect(s, scry, false);
    heurarm::Clear();
    return ok;
}
}   // namespace

TEST_CASE("Maze's End F6-2: two activations then Sylvan Scrying pay -- the second {3} floats Baldur's Gate's surplus")
{
    int fl = -1;
    CHECK(S6ActsThenScrying(/*fed_float_arm=*/-1, &fl));
    CHECK(fl == 2);   // X = 5 for {2}: 5 - 2 fed - 1 plain
}

TEST_CASE("Maze's End F6-2: with MTG_SCALED_FED_FLOAT off the fed surplus floats WHITE and strands Scrying (the hole it closes)")
{
    // Thran Portal's life cost triggers the pay refine on the second {3}, which re-makes it with Baldur's Gate fed --
    // in the oracle's WUBRG face order. ScaledFedRecolourAfter re-reads that surplus in the line's colour.
    int fl = -1;
    CHECK_FALSE(S6ActsThenScrying(/*fed_float_arm=*/0, &fl));
    CHECK(fl == 2);
}

TEST_CASE("Maze's End F6-2: a bill below {feeder}+1 generic is untouched (Scrying off Baldur's Gate floats nothing)")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    heurarm::Clear();
    GameState s = S6Board(kProv);
    for (Permanent& p : s.battlefield) { if (p.card.m_number != 7 && p.card.m_number != 29) { p.tapped = true; } }
    ManaCost scry; scry.generic = 1; scry.green = 1;
    REQUIRE(TapForCostDirect(s, scry, false));   // Gond {G} + Baldur {C}
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("Maze's End F6-2: without the line in scope the first {3} spends the second Maze's End (the unheld order)")
{
    CHECK_FALSE(S6ActsThenScrying(/*fed_float_arm=*/-1, nullptr, /*line=*/false));
}

// ---- MTG_SCALED_ROUTE_ACTS (ManaPayment.cpp ScaledLandRouteHold): the route's rest counts pending activations ------
// 2HG s26701483 gi1483 T4 (USER 2026-11-25: "We should be using Baldur's Gate. Tapping it somewhat late makes sense,
// but not tapping it for multiple mana at all does not"). The line: Maze's End #43 fetches Baldur's Gate, #43 is
// replayed (Amulet of Vigor untaps it), Archdruid's Charm {G}{G}{G}, then #43 again ({3}, {T}). The first {3} took
// Azorius, Rakdos and Planar Nexus. Charm's greedy payment (Golgari + Sea Gate + Gond Gate) leaves Baldur's Gate's
// {C} alone for the second {3}; holding one of the three out pays Charm through the fed mode (X = 6 other Gates)
// and its {G}{G}{G} surplus floats the {3}. The line hold carries the casts only, so the route needs the plan's
// pending activation.
namespace
{
bool G1483CharmThenAct(int arm, bool* baldur_tapped_after_charm = nullptr)
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    heurarm::Clear();
    if (arm >= 0) { heurarm::t_arm[heurarm::SCALED_ROUTE_ACTS] = static_cast<std::int8_t>(arm); }
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.m_provider          = &kProv;
    Put(s, "Amulet of Vigor", 1);
    Put(s, "Azorius Guildgate", 6, /*tapped=*/true);
    Put(s, "Golgari Guildgate", 27);
    Put(s, "Gond Gate", 28);
    Put(s, "Manabond", 39);
    Put(s, "Maze's End", 43);
    Put(s, "Planar Nexus", 48, /*tapped=*/true);
    Put(s, "Rakdos Guildgate", 49, /*tapped=*/true);
    Put(s, "Sea Gate", 50);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 50) { p.chosen_color = static_cast<std::int8_t>(Color::Green); } }
    Put(s, "Baldur's Gate", 7);
    REQUIRE(ScaledManaCreatureCount(s, &s.battlefield.back()) == 6);
    const CardDefinition* charm = CardDatabase::Instance().Lookup("Archdruid's Charm");
    REQUIRE(charm != nullptr);
    Card cc = charm->card;
    cc.m_number = 12;
    ManaCost ggg; ggg.green = 3;
    ManaCost three; three.generic = 3;
    PlanTraits t;
    t.act_mana_total = 3;          // the second activation, still pending while Charm is paid
    t.act_src_nums[0] = 43;
    t.act_src_count = 1;
    PlanTraitsScope _pts(&t);
    ActLinePassScope _pass;
    bool ok = false;
    {
        LineUnpaidCostScope luc(ggg);
        {
            SpellSubtypePayScope pay(&cc);
            REQUIRE(TapForCostDirect(s, ggg, /*for_creature=*/false));
        }
        if (baldur_tapped_after_charm != nullptr) { *baldur_tapped_after_charm = Tapped(s, 7); }
        REQUIRE(TapSelf(s, 43));
        ActLinePayScope pay(three);
        ok = TapForCostDirect(s, three, false);
        if (ok) { pay.Paid(); }
    }
    heurarm::Clear();
    return ok;
}
}   // namespace

TEST_CASE("Maze's End g1483: Charm pays through Baldur's Gate's fed mode so the second Maze's End {3} pays (MTG_SCALED_ROUTE_ACTS)")
{
    bool fed = false;
    CHECK(G1483CharmThenAct(/*arm=*/-1, &fed));
    CHECK(fed);
}

TEST_CASE("Maze's End g1483: with MTG_SCALED_ROUTE_ACTS off Charm is paid directly and the second {3} strands (the hole)")
{
    bool fed = true;
    CHECK_FALSE(G1483CharmThenAct(/*arm=*/0, &fed));
    CHECK_FALSE(fed);
}
