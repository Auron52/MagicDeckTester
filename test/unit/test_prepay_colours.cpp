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
// the board ({C}{G} + {C}) cannot pay {1}{G}+{G}{W}, so the batch declines to the per-cast payer, which pays
// the Aura with the host held and the Call off the host's three.
TEST_CASE("prepay: a land Aura's own host is never tapped before the Aura resolves (MTG_PREPAY_AURA_HOST)")
{
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
