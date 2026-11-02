// MTG_PREPAY_COLOURS -- the whole-turn prepay (TurnSolver::BatchPrepayMainCasts) settles the colours of
// its joint pool instead of declining to the per-cast payer.
//
// Two shapes, both from EldraziDisplacerFlicker and both measured by the mana oracle as lines the engine's
// own payment dropped while the board pays them:
//   * an any-colour Aura bonus (Fertile Ground) in the joint pool used to decline the batch outright
//     (PP_WILD); the per-cast payer then paid Fertile Ground with Brushland's {G} AND its bonus and left
//     Living Wish only colourless lands (EDF viewer seed 3040 T3);
//   * a hybrid pip entered the joint bill as its first colour, so Trace of Abundance's {R/W} read as a
//     hard {R} and a batch the board's {W} pays read unpayable.
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

void EnsureCardsPc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvPc()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card CardPc(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

struct BoardPc
{
    GameState s;
    int       num = 1;
    int Put(const char* nm, int host = 0, bool fresh = false)
    {
        Permanent p;
        p.card              = CardPc(nm, num++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = fresh;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
};

BoardPc MakeBoard(const std::vector<const char*>& hand)
{
    EnsureCardsPc();
    BoardPc b;
    GameState& s = b.s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvPc();
    for (const char* nm : hand) { s.players[0].hand.push_back(CardPc(nm, b.num++)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardPc("Brushland", b.num++)); }
    return b;
}

Action CastPc(const GameState& s, const std::string& name, int enchant_target = 0)
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

bool PrepayUnder(int arm, GameState& s, const std::vector<Action>& line)
{
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = static_cast<std::int8_t>(arm);
    const bool ok = TurnSolver::BatchPrepayMainCasts(s, line);
    heurarm::t_arm[heurarm::PREPAY_COLOURS] = -1;
    return ok;
}

const ManaCost& CostOf(const char* nm) { return CardDatabase::Instance().Lookup(nm)->card.m_mana_cost; }

}   // namespace

// EDF viewer seed 3040 T3: Brushland under Fertile Ground, Mariposa Military Base, a fresh Shivan Gorge;
// Fertile Ground {1}{G} then Living Wish {1}{G}. Jointly: Brushland's {G} + the bonus as {G}, the two {C}
// for the generics -- exactly four for four. The new Fertile Ground goes on a land tapped earlier in the
// turn: on an untapped one, MTG_PREPAY_AURA_HOST holds that host out of the joint solve (tests below).
TEST_CASE("prepay: a land Aura's any-colour bonus is settled to the colour the batch needs (MTG_PREPAY_COLOURS)")
{
    for (int arm : { 1, 0 })
    {
        BoardPc b = MakeBoard({ "Fertile Ground", "Living Wish" });
        const int brush = b.Put("Brushland");
        b.Put("Fertile Ground", brush);
        b.Put("Mariposa Military Base");
        b.Put("Shivan Gorge", 0, /*fresh=*/true);
        const int spent = b.Put("Forest");
        b.s.battlefield.back().tapped = true;
        b.s.players[0].lands_played_this_turn = 1;
        const std::vector<Action> line = { CastPc(b.s, "Fertile Ground", spent), CastPc(b.s, "Living Wish") };
        const bool ok = PrepayUnder(arm, b.s, line);
        if (arm == 1)
        {
            REQUIRE(ok);
            // Every land is tapped and the pre-loaded pool pays both casts in turn.
            for (const Permanent& p : b.s.battlefield) { if (p.card.IsLand()) { CHECK(p.tapped); } }
            CHECK(TapForCostDirect(b.s, CostOf("Fertile Ground"), /*for_creature=*/false));
            CHECK(TapForCostDirect(b.s, CostOf("Living Wish"), /*for_creature=*/false));
        }
        else
        {
            CHECK_FALSE(ok);   // =0: the any-colour unit declines the batch (PP_WILD)
            for (const Permanent& p : b.s.battlefield)   // and nothing was paid
            { if (p.card.m_number != spent) { CHECK_FALSE(p.tapped); } }
        }
    }
}

// Trace of Abundance {R/W}{G} + Wild Growth {G} off Plains, Forest, Forest: the {W} pays the hybrid pip.
// The flat bill read it as {R} -- unpayable on a board with no red -- and declined. Both Auras go on a
// Forest tapped earlier in the turn (an untapped host is held out of the solve: MTG_PREPAY_AURA_HOST).
TEST_CASE("prepay: a hybrid pip in the joint bill may take its second colour (MTG_PREPAY_COLOURS)")
{
    for (int arm : { 1, 0 })
    {
        BoardPc b = MakeBoard({ "Trace of Abundance", "Wild Growth" });
        b.Put("Plains");
        b.Put("Forest");
        b.Put("Forest");
        const int host = b.Put("Forest");
        b.s.battlefield.back().tapped = true;
        const std::vector<Action> line = { CastPc(b.s, "Trace of Abundance", host), CastPc(b.s, "Wild Growth", host) };
        const bool ok = PrepayUnder(arm, b.s, line);
        if (arm == 1)
        {
            REQUIRE(ok);
            CHECK(TapForCostDirect(b.s, CostOf("Trace of Abundance"), /*for_creature=*/false));
            CHECK(TapForCostDirect(b.s, CostOf("Wild Growth"), /*for_creature=*/false));
            CHECK(b.s.floating_mana.Total() == 0);
        }
        else
        {
            CHECK_FALSE(ok);
        }
    }
}

// MTG_PREPAY_AURA_HOST. EDF A/B gi188 (seed 950189) T3: Conservatory under Fertile Ground, Mariposa Military
// Base under Wild Growth, a fresh Shivan Gorge; the line casts a second Fertile Ground ON Conservatory, then
// Eladamri's Call (for Cloud of Faeries, whose {1}{U} the doubled host pays). The joint solve tapped
// Conservatory before its new Aura resolved -- its bonus gone for the turn. With the host held the rest of
// the board ({C}{G} + {C}) cannot pay {1}{G}+{G}{W}, so without MTG_PREPAY_AURA_STAGE the batch declines to
// the per-cast payer, which pays the Aura with the host held and the Call off the host's three. (With the
// staged solve on, the joint prepay pays it the same way itself: tests below.)
TEST_CASE("prepay: a land Aura's own host is never tapped before the Aura resolves (MTG_PREPAY_AURA_HOST)")
{
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = 0;
    for (int arm : { 1, 0 })
    {
        BoardPc b = MakeBoard({ "Fertile Ground", "Eladamri's Call", "Cloud of Faeries" });
        const int cons = b.Put("Conservatory");
        b.Put("Fertile Ground", cons);
        const int mari = b.Put("Mariposa Military Base");
        b.Put("Wild Growth", mari);
        b.Put("Shivan Gorge", 0, /*fresh=*/true);
        b.s.players[0].lands_played_this_turn = 1;
        const std::vector<Action> line = { CastPc(b.s, "Fertile Ground", cons), CastPc(b.s, "Eladamri's Call") };
        heurarm::t_arm[heurarm::PREPAY_AURA_HOST] = static_cast<std::int8_t>(arm);
        const bool ok = PrepayUnder(1, b.s, line);
        heurarm::t_arm[heurarm::PREPAY_AURA_HOST] = -1;
        if (arm == 1)
        {
            CHECK_FALSE(ok);   // unpayable without the host -> the per-cast payer
            for (const Permanent& p : b.s.battlefield) { CHECK_FALSE(p.tapped); }   // the hold left nothing behind
        }
        else
        {
            REQUIRE(ok);       // =0: the historical joint solve, host included
            CHECK(b.s.battlefield[0].tapped);
        }
    }
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
}

// ...and when the rest of the board pays the batch (a Forest and a Plains more), the joint solve still runs,
// with the host left up for the Aura to land on.
TEST_CASE("prepay: a batch the board pays without the land Aura's host is prepaid with the host untapped")
{
    BoardPc b = MakeBoard({ "Fertile Ground", "Eladamri's Call" });
    const int cons = b.Put("Conservatory");
    b.Put("Fertile Ground", cons);
    const int mari = b.Put("Mariposa Military Base");
    b.Put("Wild Growth", mari);
    b.Put("Shivan Gorge");
    b.Put("Forest");
    b.Put("Plains");
    b.s.players[0].lands_played_this_turn = 1;
    const std::vector<Action> line = { CastPc(b.s, "Fertile Ground", cons), CastPc(b.s, "Eladamri's Call") };
    REQUIRE(PrepayUnder(1, b.s, line));
    CHECK_FALSE(b.s.battlefield[0].tapped);   // Conservatory: the new Aura's host
    CHECK(TapForCostDirect(b.s, CostOf("Fertile Ground"), /*for_creature=*/false));
    CHECK(TapForCostDirect(b.s, CostOf("Eladamri's Call"), /*for_creature=*/false));
    CHECK_FALSE(b.s.battlefield[0].tapped);
}

namespace
{
// MTG_PREPAY_AURA_STAGE's two callers: an APPLY (allow_partial: the held Auras' own bills are pre-loaded and
// the call returns false) and a PROBE (false with the board untouched).
bool PrepayStaged(int arm, GameState& s, const std::vector<Action>& line, bool apply = true)
{
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = static_cast<std::int8_t>(arm);
    heurarm::t_arm[heurarm::PREPAY_COLOURS]    = 1;
    const bool ok = TurnSolver::BatchPrepayMainCasts(s, line, /*mint_prefix=*/false, /*hand_demand=*/false,
                                                     /*allow_partial=*/apply);
    heurarm::t_arm[heurarm::PREPAY_COLOURS]    = -1;
    heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
    return ok;
}

// The Aura has resolved onto its host (what the cast would leave behind), for the per-cast payments after it.
void ResolveAura(BoardPc& b, const char* aura, int host)
{
    b.Put(aura, host);
}

bool AllUntapped(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.tapped) { return false; } }
    return true;
}
}   // namespace

// MTG_PREPAY_AURA_STAGE, the gi188 board above: the held host left the batch unpayable. Staged, stage 1 pays
// Fertile Ground off Mariposa's {C}{G} (host held), and stage 2 shows the Call payable off Conservatory carrying
// both Fertile Grounds -- so the apply pre-loads stage 1 only: Mariposa tapped, Conservatory UP. The Call then
// pays per cast once the new Aura is on, and its third unit floats on for the tutored Cloud of Faeries' {U}.
TEST_CASE("prepay: a batch the held host leaves unpayable is prepaid for its land Aura, the rest per cast")
{
    for (bool apply : { true, false })
    {
        CAPTURE(apply);
        BoardPc b = MakeBoard({ "Fertile Ground", "Eladamri's Call", "Cloud of Faeries" });
        const int cons = b.Put("Conservatory");
        b.Put("Fertile Ground", cons);
        const int mari = b.Put("Mariposa Military Base");
        b.Put("Wild Growth", mari);
        b.Put("Shivan Gorge", 0, /*fresh=*/true);
        b.s.players[0].lands_played_this_turn = 1;
        const std::vector<Action> line = { CastPc(b.s, "Fertile Ground", cons), CastPc(b.s, "Eladamri's Call") };
        CHECK_FALSE(PrepayStaged(1, b.s, line, apply));   // never the whole batch
        CHECK(b.s.battlefield.size() == 5);                // no stand-in left behind
        if (!apply)
        {
            CHECK(AllUntapped(b.s));                       // a probe: untouched
            CHECK(b.s.floating_mana.Total() == 0);
            continue;
        }
        CHECK_FALSE(b.s.battlefield[0].tapped);   // Conservatory: the new Aura's host, left up
        CHECK(b.s.battlefield[2].tapped);         // Mariposa: the Aura's own bill
        CHECK_FALSE(b.s.battlefield[4].tapped);   // Shivan Gorge
        CHECK(TapForCostDirect(b.s, CostOf("Fertile Ground"), /*for_creature=*/false));
        CHECK(b.s.floating_mana.Total() == 0);
        ResolveAura(b, "Fertile Ground", cons);
        CHECK(TapForCostDirect(b.s, CostOf("Eladamri's Call"), /*for_creature=*/false));
        CHECK(TapForCostDirect(b.s, CostOf("Cloud of Faeries"), /*for_creature=*/true));
    }
}

// Fertile Ground onto an untapped Forest, then Living Wish, off Forest, Forest, Plains: three lands for four
// mana, so the batch NEEDS the new Aura's bonus -- no single joint solve pays it, held host or not (the probe's
// 'combined UNPAYABLE'). Staged: the Aura off the other Forest and the Plains (pre-loaded), the Wish off the
// host + bonus once the Aura is on.
TEST_CASE("prepay: a batch that needs its own land Aura's bonus is prepaid for the Aura, the rest per cast (MTG_PREPAY_AURA_STAGE)")
{
    for (int arm : { 1, 0 })
    {
        CAPTURE(arm);
        BoardPc b = MakeBoard({ "Fertile Ground", "Living Wish" });
        const int host = b.Put("Forest");
        b.Put("Forest");
        b.Put("Plains");
        b.s.players[0].lands_played_this_turn = 1;
        const std::vector<Action> line = { CastPc(b.s, "Fertile Ground", host), CastPc(b.s, "Living Wish") };
        CHECK_FALSE(PrepayStaged(arm, b.s, line));
        CHECK(b.s.battlefield.size() == 3);
        if (arm == 1)
        {
            CHECK_FALSE(b.s.battlefield[0].tapped);   // the host
            CHECK(b.s.battlefield[1].tapped);
            CHECK(b.s.battlefield[2].tapped);
            CHECK(TapForCostDirect(b.s, CostOf("Fertile Ground"), /*for_creature=*/false));
            CHECK(b.s.floating_mana.Total() == 0);
            ResolveAura(b, "Fertile Ground", host);
            CHECK(TapForCostDirect(b.s, CostOf("Living Wish"), /*for_creature=*/false));
        }
        else
        {
            CHECK(AllUntapped(b.s));   // =0: the hold's decline
        }
    }
}

// ...and in the opposite ORDER no bonus mana is ever pre-loaded. The batch arrives with the Wish first, and the
// apply may realise it that way (AccomplishedAlchemist's PROPOSED cast order does, test_accomplished_alchemist.cpp):
// only Fertile Ground's own {1}{G} is pre-loaded, off the Forest and the Plains, so the Wish cast first drains it
// and the Aura then finds one Forest for two mana -- what that order really does. The bonus exists only once
// the Aura is on (CR 605.1b), and nothing here credits it before.
TEST_CASE("prepay: the staged solve never pre-loads a land Aura's bonus (a cast made before the Aura cannot spend it)")
{
    BoardPc b = MakeBoard({ "Fertile Ground", "Living Wish" });
    const int host = b.Put("Forest");
    b.Put("Forest");
    b.Put("Plains");
    b.s.players[0].lands_played_this_turn = 1;
    const std::vector<Action> line = { CastPc(b.s, "Living Wish"), CastPc(b.s, "Fertile Ground", host) };
    CHECK_FALSE(PrepayStaged(1, b.s, line));
    CHECK_FALSE(b.s.battlefield[0].tapped);           // the host
    CHECK(b.s.floating_mana.Total() == 2);            // Fertile Ground's bill and nothing more
    CHECK(TapForCostDirect(b.s, CostOf("Living Wish"), /*for_creature=*/false));
    CHECK_FALSE(TapForCostDirect(b.s, CostOf("Fertile Ground"), /*for_creature=*/false));
}

// Wild Growth onto a Forest, then Sigarda's Aid {W}, off Brushland and the Forest. Held, Brushland alone cannot
// pay {G}{W}; staged, the Forest + Wild Growth make {G}{G} and the Aid has no {W}. Only the UNHELD joint solve
// pays it (Brushland's {W}, the Forest's {G}) -- and the per-cast payer would not: it pays Wild Growth off
// Brushland with the host held, and the Aid then finds green only. The bonus is lost, as in every payment.
// Probe and apply alike (the unheld solve prepays the whole batch).
TEST_CASE("prepay: a batch neither held nor staged pays falls back to the unheld joint solve (MTG_PREPAY_AURA_UNHELD)")
{
    for (int arm : { 1, 0 })
    {
        for (bool apply : { true, false })
        {
            CAPTURE(arm);
            CAPTURE(apply);
            BoardPc b = MakeBoard({ "Wild Growth", "Sigarda's Aid" });
            b.Put("Brushland");
            const int host = b.Put("Forest");
            b.s.players[0].lands_played_this_turn = 1;
            const std::vector<Action> line = { CastPc(b.s, "Wild Growth", host), CastPc(b.s, "Sigarda's Aid") };
            heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = 1;   // default OFF (see the lever)
            const bool ok = PrepayStaged(arm, b.s, line, apply);
            heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = -1;
            if (arm == 1)
            {
                REQUIRE(ok);
                CHECK(b.s.battlefield[0].tapped);
                CHECK(b.s.battlefield[1].tapped);
                CHECK(TapForCostDirect(b.s, CostOf("Wild Growth"), /*for_creature=*/false));
                CHECK(TapForCostDirect(b.s, CostOf("Sigarda's Aid"), /*for_creature=*/false));
            }
            else
            {
                CHECK_FALSE(ok);
                CHECK(AllUntapped(b.s));
            }
        }
    }
    // MTG_PREPAY_AURA_UNHELD=0 (the default): the staged split cannot pay it, so the batch declines untouched.
    BoardPc b = MakeBoard({ "Wild Growth", "Sigarda's Aid" });
    b.Put("Brushland");
    const int host = b.Put("Forest");
    b.s.players[0].lands_played_this_turn = 1;
    const std::vector<Action> line = { CastPc(b.s, "Wild Growth", host), CastPc(b.s, "Sigarda's Aid") };
    heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = 0;
    CHECK_FALSE(PrepayStaged(1, b.s, line));
    heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = -1;
    CHECK(AllUntapped(b.s));
}

// EDF regression seed 2002 gi 85 (d5 b20) turn 4, after the land drop: Aether Hub carrying Wild Growth (no {E} left
// -- Wild Growth took it on turn 1), Yavimaya Coast, Brushland, a fresh Adarkar Wastes. The plan casts Trace of
// Abundance {R/W}{G} onto the Hub, then Emiel the Blessed {2}{W}{W}: six mana off four lands only with the Hub carrying
// both Auras (its {C}, Wild Growth's {G}, Trace's any) -- Trace off the Coast's {G} and one {W} land, Emiel off the
// Hub's three and the other {W} land. The first staged solve pre-loaded Trace off THREE lands ({W}{G}{C}: the
// backtracker's first find) and, reading its leftover hybrid-blind, counted the {W} Trace's pip took as left over for
// Emiel; the executor then cast Trace and dropped Emiel. Stage 1 now gives back what its bill does not need and
// settles its hybrid pips before stage 2 reads the leftover. Staged or not, execution casts both.
TEST_CASE("prepay: EDF gi85 -- Trace of Abundance onto the Wild Growth Hub, then Emiel; execution casts both")
{
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        BoardPc b = MakeBoard({ "Trace of Abundance", "Emiel the Blessed" });
        const int hub = b.Put("Aether Hub");
        b.Put("Wild Growth", hub);
        b.Put("Yavimaya Coast");
        b.Put("Brushland");
        b.Put("Adarkar Wastes", 0, /*fresh=*/true);
        b.s.turn_number = 4;
        b.s.players[0].lands_played_this_turn = 1;
        TurnSolver::Plan plan;
        plan.actions = { CastPc(b.s, "Trace of Abundance", hub), CastPc(b.s, "Emiel the Blessed") };
        if (arm == 1)
        {
            // The staged pre-load itself: Trace's two units off two lands, the Hub and one {W} land left up.
            GameState g = b.s;
            heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = 1;
            heurarm::t_arm[heurarm::PREPAY_COLOURS]    = 1;
            CHECK_FALSE(TurnSolver::BatchPrepayMainCasts(g, plan.actions, false, false, /*allow_partial=*/true));
            heurarm::t_arm[heurarm::PREPAY_COLOURS]    = -1;
            heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
            CHECK(g.floating_mana.Total() == 2);
            int up = 0;
            for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && !p.tapped) { ++up; } }
            CHECK(up == 2);
        }
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = static_cast<std::int8_t>(arm);
        GameState g = b.s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
        std::string hand;
        for (const Card& c : g.players[0].hand) { hand += c.m_name.str() + " "; }
        CHECK_MESSAGE(g.players[0].hand.empty(), "not cast: ", hand);
    }
}

// ...and the staged split only runs when EVERY land Aura of the batch is a held one. EDF d0 held-out gi1264 (seed
// 991265) T3: Yavimaya Coast, a Mariposa Military Base carrying Fertile Ground, a fresh second Mariposa; the d0 plan
// casts Overgrowth onto the Coast and a second Fertile Ground onto the doubled Mariposa. The Coast is held, the
// Mariposa cannot be (the rest of the board cannot pay Fertile Ground without it). Staged, stage 1 paid Overgrowth
// off that Mariposa and stage 2 Fertile Ground off the Coast + Overgrowth's bonus; the apply cast Fertile Ground
// first, which drained Overgrowth's pre-load, and Overgrowth was dropped (T4 -> T5). Now the batch declines and the
// per-cast walk casts both: Fertile Ground off the Coast and the fresh Mariposa, Overgrowth off the doubled host.
TEST_CASE("prepay: the staged split declines while a batch land Aura's host is unheld (EDF d0 gi1264)")
{
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        BoardPc b = MakeBoard({ "Overgrowth", "Peregrine Drake", "Eladamri's Call", "Fertile Ground" });
        const int coast = b.Put("Yavimaya Coast");
        const int m1    = b.Put("Mariposa Military Base");
        b.Put("Fertile Ground", m1);
        b.Put("Cloud of Faeries");
        b.Put("Cloud of Faeries");
        b.Put("Mariposa Military Base", 0, /*fresh=*/true);
        b.s.players[0].life = 17;
        b.s.players[0].lands_played_this_turn = 1;
        TurnSolver::Plan plan;
        plan.actions = { CastPc(b.s, "Overgrowth", coast), CastPc(b.s, "Fertile Ground", m1) };
        heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = 0;
        GameState q = b.s;
        CHECK_FALSE(PrepayStaged(arm, q, plan.actions));
        CHECK(AllUntapped(q));   // declined: nothing pre-loaded
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = static_cast<std::int8_t>(arm);
        GameState g = b.s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE]  = -1;
        heurarm::t_arm[heurarm::PREPAY_AURA_UNHELD] = -1;
        int left = 0;
        for (const Card& c : g.players[0].hand)
        { if (c.m_name.str() == "Overgrowth" || c.m_name.str() == "Fertile Ground") { ++left; } }
        CHECK(left == 0);
    }
}

// ...and stage 1 never spends an Arbor Elf that can re-tap a held host. AccomplishedAlchemist held-out gi682 (seed
// 1030683, d8 b0 T5 -> T6) T5: Forest + a black Utopia Sprawl, a second Forest, Arbor Elf; Crab Umbra onto the Elf,
// two more Sprawls onto the enchanted Forest, then Accomplished Alchemist -- seven mana. The Arbor's tap is one more
// tap of a Forest, Aura bonuses included: spent after a new Sprawl it makes three, spent in stage 1 (before them) two.
// Staged, stage 1 paid the Sprawls off the bare Forest and the Arbor, the turn made seven, and the per-cast payer
// spent both new {U} on the Alchemist's generic: Crab Umbra was dropped. Held, stage 1 cannot pay and the batch
// declines to the walk, which taps the Arbor after the first Sprawl and casts all four.
TEST_CASE("prepay: the staged split never spends an Arbor Elf before the held Forest's new Aura (AccomplishedAlchemist gi682)")
{
    static const AccomplishedAlchemistProvider aa;
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        BoardPc b = MakeBoard({ "Stroke of Genius", "Crab Umbra", "Nourish", "Living Wish", "Utopia Sprawl",
                                "Utopia Sprawl", "Accomplished Alchemist", "Elvish Harbinger" });
        b.s.m_provider  = &aa;
        b.s.turn_number = 5;
        const int forest = b.Put("Forest");
        b.Put("Utopia Sprawl", forest);
        b.s.battlefield.back().chosen_color = static_cast<std::int8_t>(Color::Black);
        b.Put("Forest");
        const int elf = b.Put("Arbor Elf");
        b.s.players[0].lands_played_this_turn = 1;
        TurnSolver::Plan plan;
        plan.actions = { CastPc(b.s, "Crab Umbra", elf), CastPc(b.s, "Utopia Sprawl", forest),
                         CastPc(b.s, "Utopia Sprawl", forest), CastPc(b.s, "Accomplished Alchemist") };
        int k = 0;   // the second Sprawl is the second hand copy
        for (int i = 0; i < static_cast<int>(b.s.players[0].hand.size()); ++i)
        { if (b.s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == "Utopia Sprawl" && k++ == 1) { plan.actions[2].hand_index = i; } }
        GameState q = b.s;
        CHECK_FALSE(PrepayStaged(arm, q, plan.actions));
        CHECK(AllUntapped(q));   // declined: nothing pre-loaded
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = static_cast<std::int8_t>(arm);
        GameState g = b.s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
        std::string hand;
        for (const Card& c : g.players[0].hand) { hand += c.m_name.str() + " "; }
        CHECK_MESSAGE(g.spells_cast_this_turn == 4, "left in hand: ", hand);
    }
}

// ...and stage 1 keeps its most flexible sources up even when it does not over-tap. Bruna (stage-shadow sweep, seed
// 1060001) T6: Remote Farm, Razorverge Thicket, a fresh Forest; two Open the Armory and Wild Growth onto the Farm --
// five for five, once Wild Growth is on before the Farm taps. Stage 1 paid Wild Growth's {G} off the Thicket ({G}/{W});
// stage 2 is jointly payable (the Farm's {W}{W} on the two {W} pips), but the apply's first Armory spent the Farm's
// second {W} on its {1} and the second Armory had no {W}. The give-back now trades the Thicket for the Forest (as
// many sources, fewer colours), and the apply casts all three.
TEST_CASE("prepay: the staged split's stage 1 taps the least flexible source it can (Bruna, Open the Armory)")
{
    static const BrunaProvider bp;
    for (int arm : { 0, 1 })
    {
        CAPTURE(arm);
        BoardPc b = MakeBoard({ "Prodigious Growth", "Open the Armory", "Open the Armory", "Wild Growth", "Arcanum Wings",
                                "Eldrazi Conscription", "Glittering Wish" });
        b.s.m_provider  = &bp;
        b.s.turn_number = 6;
        const int farm    = b.Put("Remote Farm");
        const int thicket = b.Put("Razorverge Thicket");
        const int forest  = b.Put("Forest", 0, /*fresh=*/true);
        b.s.players[0].lands_played_this_turn = 1;
        TurnSolver::Plan plan;
        plan.actions = { CastPc(b.s, "Open the Armory"), CastPc(b.s, "Open the Armory"), CastPc(b.s, "Wild Growth", farm) };
        plan.actions[1].hand_index = plan.actions[0].hand_index + 1;
        if (arm == 1)
        {
            GameState q = b.s;
            CHECK_FALSE(PrepayStaged(1, q, plan.actions));   // staged: Wild Growth's {G} pre-loaded, the rest per cast
            CHECK(q.floating_mana.green == 1);
            for (const Permanent& p : q.battlefield)
            {
                if (p.card.m_number == forest)  { CHECK(p.tapped); }
                if (p.card.m_number == thicket) { CHECK_FALSE(p.tapped); }
                if (p.card.m_number == farm)    { CHECK_FALSE(p.tapped); }
            }
        }
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = static_cast<std::int8_t>(arm);
        GameState g = b.s;
        TurnSolver::ApplyPlan(g, plan, /*is_pre_combat=*/true);
        heurarm::t_arm[heurarm::PREPAY_AURA_STAGE] = -1;
        std::string hand;
        for (const Card& c : g.players[0].hand) { hand += c.m_name.str() + " "; }
        CHECK_MESSAGE(g.spells_cast_this_turn == 3, "left in hand: ", hand);
    }
}
