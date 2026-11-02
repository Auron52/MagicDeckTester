// MTG_PREPAY_ENERGY_RESTORE -- a DECLINED whole-turn prepay (TurnSolver::BatchPrepayMainCasts) gives back
// the {E} its rolled-back solve spent on a coloured Aether Hub tap.
//
// The board is EDF gi 1985 (seed 1651986, d0) turn 3: Yavimaya Coast carrying Wild Growth, Kitchen, and
// an Aether Hub dropped this turn ({E} 1). The line is {Trace of Abundance {R/W}{G} -> the Coast,
// Eldrazi Displacer {2}{W}}: legal, because the Hub's energy pays Trace's {R/W} and Trace's own bonus on
// the Coast then makes Displacer's {W}. The prepay prices both casts against the board as it stands
// (five mana against four, Trace not yet down), taps the Hub coloured on the way, and declines -- and
// its rollback restored the battlefield and the float but not the energy, so the per-cast payer found a
// spent-out Hub and Trace (then Displacer) could not be paid. The executor and the rollout share this
// function, so both worlds leaked.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsPe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvPe()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhPe(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct Board1985
{
    GameState s;
    int coast = 0;
};

Board1985 Make1985()
{
    EnsureCardsPe();
    Board1985 b;
    GameState& s = b.s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.players[0].energy_counters = 1;   // the Hub dropped this turn
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvPe();
    int num = 1;
    for (const char* nm : { "Trace of Abundance", "Eldrazi Displacer", "Overgrowth", "Living Wish" })
    { s.players[0].hand.push_back(PhPe(nm, num++)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhPe("Brushland", num++)); }
    auto put = [&](const char* nm, int host, bool fresh) -> int
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
    };
    b.coast = put("Yavimaya Coast", 0, false);
    put("Wild Growth", b.coast, false);
    put("Kitchen", 0, false);
    put("Aether Hub", 0, true);
    s.players[0].lands_played_this_turn = 1;
    return b;
}

Action CastAction(const GameState& s, const std::string& name, int enchant_target)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d->card.m_name;
    a.def            = d;
    a.cost           = d->card.m_mana_cost;
    a.is_noncreature = !d->card.IsCreature();
    a.card_mv        = d->card.m_mana_cost.ManaValue();
    a.enchant_target = enchant_target;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == name) { a.hand_index = i; break; } }
    return a;
}

int PrepayEnergyAfter(int arm, bool* accepted)
{
    Board1985 b = Make1985();
    const std::vector<Action> line = { CastAction(b.s, "Trace of Abundance", b.coast),
                                       CastAction(b.s, "Eldrazi Displacer", 0) };
    heurarm::t_arm[heurarm::PREPAY_ENERGY_RESTORE] = static_cast<std::int8_t>(arm);
    *accepted = TurnSolver::BatchPrepayMainCasts(b.s, line);
    heurarm::t_arm[heurarm::PREPAY_ENERGY_RESTORE] = -1;
    return b.s.players[0].energy_counters;
}

}   // namespace

// Both tests below are about the rollback of a DECLINED prepay, so they run without MTG_PREPAY_AURA_STAGE,
// which pays this very batch (the last test).
TEST_CASE("EDF: a declined prepay gives back the Aether Hub's {E} (MTG_PREPAY_ENERGY_RESTORE)")
{
    bool acc_on = true, acc_off = true;
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = 0;
    const int on  = PrepayEnergyAfter(1, &acc_on);
    const int off = PrepayEnergyAfter(0, &acc_off);
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
    // The prepay cannot pay both casts off the pre-Trace board, so it declines either way...
    CHECK_FALSE(acc_on);
    CHECK_FALSE(acc_off);
    // ...and a declined payment paid nothing: the energy is still there.
    CHECK(on == 1);
    // Precondition (the leak this lever closes): the old rollback left the {E} spent.
    CHECK_MESSAGE(off == 0, "the lever-off arm should reproduce the leak");
}

// The line the leak broke: after the declined prepay the per-cast payer must still find the Hub's
// coloured mode for Trace's {R/W}.
TEST_CASE("EDF: after a declined prepay Trace of Abundance still pays off the Hub (MTG_PREPAY_ENERGY_RESTORE)")
{
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = 0;
    for (int arm : { 1, 0 })
    {
        Board1985 b = Make1985();
        const std::vector<Action> line = { CastAction(b.s, "Trace of Abundance", b.coast),
                                           CastAction(b.s, "Eldrazi Displacer", 0) };
        heurarm::t_arm[heurarm::PREPAY_ENERGY_RESTORE] = static_cast<std::int8_t>(arm);
        TurnSolver::BatchPrepayMainCasts(b.s, line);
        heurarm::t_arm[heurarm::PREPAY_ENERGY_RESTORE] = -1;
        // Pay Trace with the Coast held back (the executor reserves the Aura's declared host).
        for (Permanent& p : b.s.battlefield) { if (p.card.m_number == b.coast) { p.tapped = true; } }
        const bool paid = TapForCostDirect(b.s, CardDatabase::Instance().Lookup("Trace of Abundance")->card.m_mana_cost,
                                           /*for_creature=*/false);
        if (arm == 1) { CHECK(paid); }
        else          { CHECK_FALSE(paid); }
    }
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
}

// MTG_PREPAY_AURA_STAGE, as an apply makes it: Trace is prepaid off Kitchen's {G} and the Hub's coloured mode
// (the one {E}) with the Coast held, and Displacer pays once Trace is on the Coast -- the Coast's own mana,
// Wild Growth's {G} and Trace's bonus as the {W}. Five mana from three lands, the bonus only after its Aura.
TEST_CASE("EDF: the staged prepay pays Trace of Abundance first, and Eldrazi Displacer off the Trace'd Coast (MTG_PREPAY_AURA_STAGE)")
{
    Board1985 b = Make1985();
    const std::vector<Action> line = { CastAction(b.s, "Trace of Abundance", b.coast),
                                       CastAction(b.s, "Eldrazi Displacer", 0) };
    CHECK_FALSE(TurnSolver::BatchPrepayMainCasts(b.s, line, /*mint_prefix=*/false, /*hand_demand=*/false,
                                                 /*allow_partial=*/true));
    CHECK(b.s.players[0].energy_counters == 0);   // the Hub's {E}, spent on Trace's {R/W}
    CHECK(b.s.battlefield.size() == 4);           // no stand-in left behind
    CHECK_FALSE(b.s.battlefield[0].tapped);       // the Coast: Trace's host, left up
    CHECK(TapForCostDirect(b.s, CardDatabase::Instance().Lookup("Trace of Abundance")->card.m_mana_cost,
                           /*for_creature=*/false));
    CHECK(b.s.floating_mana.Total() == 0);
    Permanent tr;
    tr.card             = CardDatabase::Instance().Lookup("Trace of Abundance")->card;
    tr.card.m_number    = 900;
    tr.aura_attached_to = b.coast;
    b.s.battlefield.push_back(tr);
    CHECK(TapForCostDirect(b.s, CardDatabase::Instance().Lookup("Eldrazi Displacer")->card.m_mana_cost,
                           /*for_creature=*/true));
}
