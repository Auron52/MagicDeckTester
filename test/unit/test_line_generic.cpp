// MTG_AUTO_LINE_GENERIC -- the line-aware generic-pip orders (SpellEffects.h: the float drain's surplus-first
// order, ConsumeFloatingAny, LineDemandAnyPipColor) in autonomous play, not only in human play.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

Card CardLg(const std::string& name, int number)
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

int PutLg(GameState& s, int& num, const char* nm, int host = 0, bool tapped = false)
{
    Permanent p;
    p.card             = CardLg(nm, num++);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.aura_attached_to = host;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

}   // namespace

// AccomplishedAlchemist held-out gi682 (seed 1030683) T5, after two new Utopia Sprawls (blue) joined a black one on
// the Forest: {B} floating, the Forest untapped, the second Forest and Arbor Elf spent, and the line still owes
// Accomplished Alchemist {3}{G} then Crab Umbra {U}. The float pays a generic pip, the Forest's {G}{B}{U}{U} the
// rest -- and WUBRG spent both {U} on the Alchemist's generic and kept the {B}, so Crab Umbra was dropped. Line-aware,
// the generic eats the {B} and a {U} floats on for Crab Umbra.
TEST_CASE("payment: a generic pip spends the colour the line does not owe (MTG_AUTO_LINE_GENERIC, AA gi682)")
{
    static const AccomplishedAlchemistProvider aa;
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        GameState s;
        int num = 1;
        s.active_player_index = 0;
        s.turn_number         = 5;
        s.players[0].life     = gamesetup::StartingLife();
        s.players[1].life     = 20;
        s.deck_has_land_aura  = true;
        s.m_provider          = &aa;
        const int forest = PutLg(s, num, "Forest");
        PutLg(s, num, "Utopia Sprawl", forest); s.battlefield.back().chosen_color = static_cast<std::int8_t>(Color::Black);
        PutLg(s, num, "Utopia Sprawl", forest); s.battlefield.back().chosen_color = static_cast<std::int8_t>(Color::Blue);
        PutLg(s, num, "Utopia Sprawl", forest); s.battlefield.back().chosen_color = static_cast<std::int8_t>(Color::Blue);
        PutLg(s, num, "Forest", 0, /*tapped=*/true);
        PutLg(s, num, "Arbor Elf", 0, /*tapped=*/true);
        s.floating_mana.black = 1;
        const ManaCost alchemist = CardDatabase::Instance().Lookup("Accomplished Alchemist")->card.m_mana_cost;
        ManaCost line = alchemist;
        line.blue += 1;   // ...then Crab Umbra
        for (bool unseen : { false, true })
        {
            CAPTURE(unseen);
            GameState g = s;
            const ManaCost saved = g_line_unpaid_cost;
            g_line_unpaid_cost = line;
            LineOpensUnseenScope _unseen(unseen);   // a line that casts a breakpoint opener keeps WUBRG
            heurarm::t_arm[heurarm::AUTO_LINE_GENERIC] = static_cast<std::int8_t>(arm);
            const bool ok = TapForCostShared(g, alchemist, /*for_creature=*/true, nullptr, false);
            heurarm::t_arm[heurarm::AUTO_LINE_GENERIC] = -1;
            g_line_unpaid_cost = saved;
            REQUIRE(ok);
            CHECK(g.floating_mana.Total() == 1);
            if (arm == 1 && !unseen) { CHECK(g.floating_mana.blue == 1); }    // Crab Umbra's {U} survives
            else                     { CHECK(g.floating_mana.black == 1); }   // WUBRG: the {U} are spent
        }
    }
}

// MTG_SCARCE_COLOR_MASK -- the per-cast payer keeps the line's scarce colour providers up (EDF smoke s1009 T3, found by
// the mana oracle: APPLY-DROP in both prepay arms). Mariposa Military Base, Yavimaya Coast and a fresh Brushland; the
// searched order Wild Growth, then Cloud of Faeries {1}{U}. The Coast is the board's only {U}.
//   * Wild Growth onto Mariposa: its {G} took the Coast (a tie with Brushland's {G}, broken by battlefield order) and
//     Cloud of Faeries had no {U}. Held, Brushland pays.
//   * Wild Growth onto Brushland: the per-cast host hold keeps Brushland up, so the Coast was the only {G} left. The
//     scarce hold outranks the host's (ScarceHeldOnlyScope): Wild Growth is paid off its own host, the bonus lost,
//     and the Coast pays Cloud of Faeries.
// The staged land-Aura prepay (MTG_PREPAY_AURA_STAGE) now pays the Mariposa batch itself, before the per-cast payer
// is asked, but not the Brushland one (its host is the board's only spare {G}). The test isolates the per-cast payer
// (the staged split off), which is what pays every batch the prepay declines.
TEST_CASE("payment: the scarce hold keeps the line's only {U} for Cloud of Faeries, over a land Aura's host hold (MTG_SCARCE_COLOR_MASK)")
{
    static const EldraziFlickerProvider edf;
    for (int host : { 1, 3 })   // Wild Growth onto Mariposa (#1) / Brushland (#3)
    for (int arm : { 0, 1 })
    {
        CAPTURE(host);
        CAPTURE(arm);
        GameState s;
        int num = 1;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.players[0].life     = 19;
        s.players[1].life     = 20;
        s.deck_has_land_aura  = true;
        s.m_provider          = &edf;
        PutLg(s, num, "Mariposa Military Base");
        PutLg(s, num, "Yavimaya Coast");
        PutLg(s, num, "Brushland");
        s.battlefield.back().entered_this_turn = true;
        s.players[0].lands_played_this_turn = 1;
        for (const char* nm : { "Emiel the Blessed", "Aether Hub", "Cloud of Faeries", "Wild Growth" })
        { s.players[0].hand.push_back(CardLg(nm, num++)); }
        for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardLg("Brushland", num++)); }
        auto cast = [&](const char* nm, int et)
        {
            const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
            Action a;
            a.kind           = Action::Kind::CastFromHand;
            a.card_name      = d->card.m_name;
            a.def            = d;
            a.cost           = d->card.m_mana_cost;
            a.is_noncreature = !d->card.IsCreature();
            a.card_mv        = d->card.m_mana_cost.ManaValue();
            a.enchant_target = et;
            for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
            { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == nm) { a.hand_index = i; break; } }
            return a;
        };
        TurnSolver::Plan plan;
        plan.actions        = { cast("Wild Growth", host), cast("Cloud of Faeries", 0) };
        plan.searched_order = true;
        heurarm::t_arm[heurarm::SCARCE_COLOR_MASK] = static_cast<std::int8_t>(arm);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = 0;
        GameState g = s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
        heurarm::t_arm[heurarm::SCARCE_COLOR_MASK] = -1;
        CHECK(g.spells_cast_this_turn == (arm == 1 ? 2 : 1));   // =0 pins the historical drop
    }
}
