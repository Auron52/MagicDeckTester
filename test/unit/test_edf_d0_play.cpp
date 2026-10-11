// Unit tests for the EldraziDisplacerFlicker depth-0 play defects of the 2026-10-10 discard round
// (docs/design/discard-round-play-defects-2026-10-10.md sections 3 and 4), one lever per defect; each
// section below names its lever and the game it was found in.
//
// Each case runs the lever ON (default) and OFF (heurarm override, =0) on the same hand-built board and
// asserts the two arms differ in the direction the defect names -- so every case FAILS without its fix.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsD0()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& EdfProv()
{
    static const EldraziFlickerProvider p;
    return p;
}

// Zone cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhD0(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// A board permanent; `host` = index of the land (in the same list) an Aura is attached to, -1 none.
struct BpD0 { std::string name; int host = -1; };

struct EdfBoard
{
    GameState s;
    std::vector<int> ids;   // battlefield m_numbers, in list order
};

EdfBoard MakeBoard(const std::vector<std::string>& hand, const std::vector<BpD0>& board,
                   const std::vector<std::string>& library, int opp_life, int energy = 0)
{
    EnsureCardsD0();
    EdfBoard b;
    GameState& s = b.s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = opp_life;
    s.players[0].energy_counters = energy;
    s.deck_has_land_aura  = true;
    s.m_provider          = &EdfProv();
    int num = 1;
    for (const std::string& nm : hand)    { s.players[0].hand.push_back(PhD0(nm, num++)); }
    for (const std::string& nm : library) { s.players[0].library.push_back(PhD0(nm, num++)); }
    for (const BpD0& bp : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(bp.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (bp.host >= 0) { p.aura_attached_to = b.ids[static_cast<std::size_t>(bp.host)]; }
        b.ids.push_back(p.card.m_number);
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;   // the land drop is spent: casts only
    return b;
}

std::vector<std::string> Filler(int n)
{
    return std::vector<std::string>(static_cast<std::size_t>(n), "Brushland");
}

// Solve with one heurarm lever pinned (1 on, 0 off), restored on exit.
TurnSolver::Plan SolveArm(const GameState& s, heurarm::Slot slot, int arm)
{
    heurarm::t_arm[slot] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[slot] = -1;
    return p;
}

bool HasRoute(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::ComboRoute) { return true; } }
    return false;
}

bool Casts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

// ---- 1. MTG_SOLVE_VERIFIED_WIN ---------------------------------------------------------------------
// A live Displacer + Peregrine Drake loop and a Shivan Gorge whose {R} a Fertile Ground renews every
// iteration, opponent on 3: the COMBO OFF route's trial kills, so CollectActions offers the route (eval
// 999). The same board makes the Gorge projection call EVERY plan lethal, so a cast plan worth >= 1000
// (Drake 500 + Displacer 300 + Cloud 200) out-values the route under the old (wins, value, mask) order.
// Opponent life 3 keeps the board's blink go-off count at 3 (= kmax), so the EDF go-off cut has no
// candidate and the ranking itself decides.
TEST_CASE("EDF d0: a trial-verified COMBO OFF route outranks a projected win (MTG_SOLVE_VERIFIED_WIN)")
{
    EdfBoard b = MakeBoard(
        { "Peregrine Drake", "Eldrazi Displacer", "Cloud of Faeries" },
        { { "Yavimaya Coast" }, { "Overgrowth", 0 },
          { "Yavimaya Coast" }, { "Overgrowth", 2 },
          { "Brushland" },      { "Fertile Ground", 4 },
          { "Adarkar Wastes" }, { "Adarkar Wastes" }, { "Brushland" },
          { "Shivan Gorge" },   { "Mariposa Military Base" },
          { "Eldrazi Displacer" }, { "Peregrine Drake" } },
        Filler(20), /*opp_life=*/3);

    const TurnSolver::Plan on  = SolveArm(b.s, heurarm::SOLVE_VERIFIED_WIN, 1);
    const TurnSolver::Plan off = SolveArm(b.s, heurarm::SOLVE_VERIFIED_WIN, 0);

    // Precondition: the route is on the menu and the old order buries it under a cast plan.
    CHECK_MESSAGE(!HasRoute(off), "the old order should rank a >=1000-eval projected win above the route");
    CHECK(off.wins_this_turn);
    // The fix: the verified win is the plan.
    CHECK(HasRoute(on));
    CHECK(on.wins_this_turn);
    CHECK(on.actions.size() == 1);
}

// ---- 2. MTG_EDF_GORGE_RED_SIZING ---------------------------------------------------------------------
// Displacer + Drake loop, a Shivan Gorge, opponent on 10, and NO red source the loop renews: the only
// red is Aether Hub's coloured mode, which spends {E} (one energy = one red, however often the Hub is
// untapped). A Mariposa Military Base can draw the 30-card library down. The old sizing returns the
// Gorge's "life / damage" = 10 before the draw-land route is ever priced; the Gorge then fires once and
// the loop stops nine iterations into a 29-card dig. With the fix the Gorge count is a floor under the
// draw-land route's 29.
TEST_CASE("EDF d0: one-shot red does not let the Gorge's count pre-empt the dig sizing (MTG_EDF_GORGE_RED_SIZING)")
{
    EdfBoard b = MakeBoard(
        {},
        { { "Yavimaya Coast" }, { "Overgrowth", 0 },
          { "Yavimaya Coast" }, { "Overgrowth", 2 },
          { "Brushland" }, { "Adarkar Wastes" }, { "Aether Hub" },
          { "Shivan Gorge" }, { "Mariposa Military Base" },
          { "Eldrazi Displacer" }, { "Peregrine Drake" } },
        Filler(30), /*opp_life=*/10, /*energy=*/1);
    const Permanent* disp  = nullptr;
    const Permanent* drake = nullptr;
    for (const Permanent& p : b.s.battlefield)
    {
        if (p.card.m_name.str() == "Eldrazi Displacer") { disp = &p; }
        if (p.card.m_name.str() == "Peregrine Drake")   { drake = &p; }
    }
    REQUIRE(disp != nullptr);
    REQUIRE(drake != nullptr);

    const auto counts = [&](int arm)
    {
        heurarm::t_arm[heurarm::EDF_GORGE_RED_SIZING] = static_cast<std::int8_t>(arm);
        const std::vector<int> v = EdfProv().BlinkActivationCounts(b.s, *disp, *drake, 3);
        heurarm::t_arm[heurarm::EDF_GORGE_RED_SIZING] = -1;
        return v.empty() ? 0 : *std::max_element(v.begin(), v.end());
    };
    const int off = counts(0);
    const int on  = counts(1);
    CHECK(off == 10);    // the Gorge's life/damage, returned before any other route is sized
    CHECK(on == 29);     // the library drawn down to one card (Mariposa), the Gorge's 10 a floor under it
}

// The other side of the same lever: when every iteration DOES renew red (a Fertile Ground on a land the
// untap set keeps), the Gorge's count is still the answer -- the lever must not over-size a board whose
// Gorge kills on its own.
TEST_CASE("EDF d0: a renewed red source keeps the Gorge's own count (MTG_EDF_GORGE_RED_SIZING)")
{
    EdfBoard b = MakeBoard(
        {},
        { { "Yavimaya Coast" }, { "Overgrowth", 0 },
          { "Yavimaya Coast" }, { "Overgrowth", 2 },
          { "Brushland" }, { "Fertile Ground", 4 }, { "Adarkar Wastes" }, { "Aether Hub" },
          { "Shivan Gorge" }, { "Mariposa Military Base" },
          { "Eldrazi Displacer" }, { "Peregrine Drake" } },
        Filler(30), /*opp_life=*/10, /*energy=*/1);
    const Permanent* disp  = nullptr;
    const Permanent* drake = nullptr;
    for (const Permanent& p : b.s.battlefield)
    {
        if (p.card.m_name.str() == "Eldrazi Displacer") { disp = &p; }
        if (p.card.m_name.str() == "Peregrine Drake")   { drake = &p; }
    }
    REQUIRE(disp != nullptr);
    REQUIRE(drake != nullptr);
    heurarm::t_arm[heurarm::EDF_GORGE_RED_SIZING] = 1;
    const std::vector<int> v = EdfProv().BlinkActivationCounts(b.s, *disp, *drake, 3);
    heurarm::t_arm[heurarm::EDF_GORGE_RED_SIZING] = -1;
    REQUIRE(!v.empty());
    CHECK(*std::max_element(v.begin(), v.end()) == 10);
}

// ---- 3. MTG_SOLVE_PENDING_AURA_EXACT -----------------------------------------------------------------
// The gi 1261 board: Aether Hub with no energy ({C} only), Mariposa Military Base ({C}), Kitchen ({G} or
// {U}). Nothing makes {W}. The Fertile Ground in HAND widened both colour gates to every colour, and the
// flat pool counts the Kitchen dual as `wild`, so {Eldrazi Displacer} alone (eval 300) read as payable
// and won the ranking; the executor could not pay {W} and the turn was empty. The real line is Fertile
// Ground (on the Hub) then Cloud of Faeries off the Aura's extra mana.
TEST_CASE("EDF d0: an in-hand land Aura does not make an uncast {W} payable (MTG_SOLVE_PENDING_AURA_EXACT)")
{
    EdfBoard b = MakeBoard(
        { "Eldrazi Displacer", "Eldrazi Displacer", "Fertile Ground", "Fertile Ground", "Cloud of Faeries" },
        { { "Aether Hub" }, { "Mariposa Military Base" }, { "Kitchen" } },
        Filler(20), /*opp_life=*/20, /*energy=*/0);

    // Pinned without MTG_SOLVE_UNTAP_CREDIT, so this case isolates the phantom {W}: once Cloud's ETB
    // untap is credited, {Fertile Ground, Cloud, Displacer} becomes a REAL line on this board (next case).
    heurarm::t_arm[heurarm::SOLVE_UNTAP_CREDIT] = 0;
    const TurnSolver::Plan off = SolveArm(b.s, heurarm::SOLVE_PENDING_AURA_EXACT, 0);
    const TurnSolver::Plan on  = SolveArm(b.s, heurarm::SOLVE_PENDING_AURA_EXACT, 1);
    heurarm::t_arm[heurarm::SOLVE_UNTAP_CREDIT] = -1;

    CHECK_MESSAGE(Casts(off, "Eldrazi Displacer"), "precondition: the widened gates admit the phantom {W}");
    CHECK_FALSE(Casts(on, "Eldrazi Displacer"));
    CHECK(Casts(on, "Fertile Ground"));
    CHECK(Casts(on, "Cloud of Faeries"));
}

// The same board with Cloud of Faeries' untap credited (MTG_SOLVE_UNTAP_CREDIT): Fertile Ground goes on
// the Hub, the Hub's Aura bonus pays Cloud's {U}, Cloud's ETB untaps the Hub, and the Hub's next tap
// (its {C} plus the Aura's any colour) pays Displacer's {W}. Legal -- so Displacer may now be cast, but
// only beside the Aura that makes its {W}, and execution must realise every declared cast.
TEST_CASE("EDF d0: with Cloud's untap credited the {W} comes from a cast Aura, and it is real (MTG_SOLVE_PENDING_AURA_EXACT)")
{
    EdfBoard b = MakeBoard(
        { "Eldrazi Displacer", "Eldrazi Displacer", "Fertile Ground", "Fertile Ground", "Cloud of Faeries" },
        { { "Aether Hub" }, { "Mariposa Military Base" }, { "Kitchen" } },
        Filler(20), /*opp_life=*/20, /*energy=*/0);

    const TurnSolver::Plan on = SolveArm(b.s, heurarm::SOLVE_PENDING_AURA_EXACT, 1);
    if (Casts(on, "Eldrazi Displacer"))
    {
        CHECK(Casts(on, "Fertile Ground"));
        CHECK(Casts(on, "Cloud of Faeries"));
    }
    int displacers_cast = 0;
    for (const Action& a : on.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Eldrazi Displacer") { ++displacers_cast; } }
    TurnSolver::ApplyPlan(b.s, on, /*is_pre_combat=*/true);
    int displacers_in_play = 0;
    for (const Permanent& p : b.s.battlefield)
    { if (p.card.m_name.str() == "Eldrazi Displacer") { ++displacers_in_play; } }
    CHECK(displacers_in_play == displacers_cast);
}
