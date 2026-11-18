// MTG_LINE_REDUCER_KEEP (LineReducerKeepHold, ManaPayment.cpp; docs/design/dragons-incubator-drop-2026-11-18.md).
// Dragons mana-clean gate claude-moderate-20261117, s30081761 gi81761 T5: Mountain x2, Mind Stone, Gruul Turf and
// Dragonspeaker Shaman in play, the line {Urza's Incubator, Inferno of the Star Mounts}. The greedy paid the
// Incubator's {3} with Mind Stone + both Mountains, and Inferno's {R}{R} (after the Shaman's and the Incubator's {2})
// met Gruul Turf's {R}{G} alone.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsLrk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLrk(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutLrk(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefLrk(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardLrk(const std::string& name, int number)
{
    Card c     = DefLrk(name).card;
    c.m_number = number;
    return c;
}

struct ArmLrk
{
    heurarm::Slot slot;
    ArmLrk(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmLrk() { heurarm::t_arm[slot] = -1; }
};

// The s30081761 T5 board: board order as in the game (Mountain, Mind Stone, Mountain, Shaman, Gruul Turf).
GameState BoardLrk()
{
    EnsureCardsLrk();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    PutLrk(s, "Mountain", 32);
    PutLrk(s, "Mind Stone", 29);
    PutLrk(s, "Mountain", 33);
    PutLrk(s, "Dragonspeaker Shaman", 8);
    PutLrk(s, "Gruul Turf", 13);
    s.players[0].hand.push_back(CardLrk("Urza's Incubator", 54));
    s.players[0].hand.push_back(CardLrk("Inferno of the Star Mounts", 18));
    s.players[0].hand.push_back(CardLrk("Glorybringer", 11));
    for (int k = 0; k < 20; ++k)
    { s.players[0].library.push_back(CardLrk(k % 2 ? "Mountain" : "Scourge of Valkas", 900 + k)); }
    return s;
}

Action CastLrk(const GameState& s, const std::string& n)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = n;
    a.def       = &DefLrk(n);
    a.cost      = EffectiveSpellCost(DefLrk(n), s);
    return a;
}
}   // namespace

TEST_CASE("Line keep: Urza's Incubator's {3} leaves both Mountains for Inferno's {R}{R} (MTG_LINE_REDUCER_KEEP)")
{
    const GameState s0 = BoardLrk();
    const std::vector<Action> line = { CastLrk(s0, "Urza's Incubator"), CastLrk(s0, "Inferno of the Star Mounts") };
    // The hold prices Inferno before the Incubator is in play: {2}{R}{R} (the Shaman's {2} only).
    REQUIRE(line[1].cost.generic == 2);
    REQUIRE(line[1].cost.red == 2);
    const ManaCost hold = LineCastCostTotal(line);
    CHECK(hold.generic == 5);
    CHECK(hold.red == 2);
    {
        ArmLrk arm(heurarm::LINE_REDUCER_KEEP, true);
        // The credit is the enumerator's own same-turn discount: the Incubator's {2} off Inferno.
        CHECK(LineReducerCredit(s0, line) == 2);
        CHECK(LineNoncreatureMv(line) == 3);
    }
    {
        ArmLrk arm(heurarm::LINE_REDUCER_KEEP, false);
        CHECK(LineReducerCredit(s0, line) == 0);
    }
    ManaCost rr;
    rr.red = 2;
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLrk arm(heurarm::LINE_REDUCER_KEEP, on);
        GameState s = s0;
        bool inferno_pays = false;
        {
            LineUnpaidCostScope luc(hold);
            const int credit = LineReducerCredit(s, line);
            LineReducerCreditScope lrc(credit, credit > 0 ? LineNoncreatureMv(line) : 0);
            REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
            // ON: Gruul Turf pays (with Mind Stone or a Mountain whose red then floats). OFF: the greedy's Mind Stone +
            // both Mountains, Gruul Turf left untapped.
            CHECK(s.battlefield[4].tapped == on);
            if (!on) { CHECK(s.battlefield[0].tapped); CHECK(s.battlefield[1].tapped); CHECK(s.battlefield[2].tapped); }
            // With the Incubator in play Inferno costs {R}{R}: payable only with the Mountains kept.
            inferno_pays = TapForCostDirect(s, rr, /*for_creature=*/true);
        }
        CHECK(inferno_pays == on);
    }
}

TEST_CASE("Line keep: inert without a reducer credit, and when the greedy already keeps the line")
{
    const GameState s0 = BoardLrk();
    ManaCost three;
    three.generic = 3;
    ManaCost rr;
    rr.red = 2;
    // No credit (the lever on, but the line holds no reducer): the hold {5}{R}{R} does not fit the board, nothing fires.
    {
        std::vector<bool> t_on, t_off;
        for (const bool on : { true, false })
        {
            ArmLrk a2(heurarm::LINE_REDUCER_KEEP, on);
            GameState s = s0;
            ManaCost hold = three;
            hold.generic += 2;
            hold.red = 2;
            LineUnpaidCostScope luc(hold);
            REQUIRE(TapForCostDirect(s, three, /*for_creature=*/false));
            for (const Permanent& p : s.battlefield) { (on ? t_on : t_off).push_back(p.tapped); }
        }
        CHECK(t_on == t_off);
    }
    // A third Mountain: the greedy's payment already leaves {R}{R}, so the payment is the greedy's on both arms.
    {
        GameState sb = s0;
        PutLrk(sb, "Mountain", 41);
        std::vector<bool> t_on, t_off;
        for (const bool on : { true, false })
        {
            ArmLrk arm(heurarm::LINE_REDUCER_KEEP, on);
            GameState s = sb;
            ManaCost hold = three;
            hold.generic += 2;
            hold.red = 2;
            LineUnpaidCostScope luc(hold);
            LineReducerCreditScope lrc(on ? 2 : 0, on ? 3 : 0);
            REQUIRE(TapForCostDirect(s, three, /*for_creature=*/false));
            for (const Permanent& p : s.battlefield) { (on ? t_on : t_off).push_back(p.tapped); }
            CHECK(TapForCostDirect(s, rr, /*for_creature=*/true));
        }
        CHECK(t_on == t_off);
    }
}

TEST_CASE("Line keep, the oracle-table stage: two Gruul Turfs + Mind Stone pay the {3} and float Inferno's {R}{R}")
{
    // Dragons s30081938 gi81938 T5: Gruul Turf x2, Mind Stone, Dragonspeaker Shaman. The greedy pays the Incubator's {3}
    // with Mind Stone + one Turf (exact) and leaves the other Turf's {R}{G}; no hold-out of those two keeps {R}{R}. The
    // keeping payment taps all three, spends the {C} and both {G} on the generic and floats {R}{R}.
    EnsureCardsLrk();
    GameState s0;
    s0.active_player_index = 0;
    s0.turn_number         = 5;
    s0.players[0].life = s0.players[1].life = gamesetup::StartingLife();
    s0.players[0].lands_played_this_turn = 1;
    PutLrk(s0, "Gruul Turf", 13);
    PutLrk(s0, "Dragonspeaker Shaman", 8);
    PutLrk(s0, "Mind Stone", 29);
    PutLrk(s0, "Gruul Turf", 14);
    s0.players[0].hand.push_back(CardLrk("Urza's Incubator", 54));
    s0.players[0].hand.push_back(CardLrk("Inferno of the Star Mounts", 18));
    for (int k = 0; k < 20; ++k)
    { s0.players[0].library.push_back(CardLrk(k % 2 ? "Mountain" : "Scourge of Valkas", 900 + k)); }
    const std::vector<Action> line = { CastLrk(s0, "Urza's Incubator"), CastLrk(s0, "Inferno of the Star Mounts") };
    const ManaCost hold = LineCastCostTotal(line);
    ManaCost rr;
    rr.red = 2;
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLrk arm(heurarm::LINE_REDUCER_KEEP, on);
        GameState s = s0;
        bool inferno_pays = false;
        {
            LineUnpaidCostScope luc(hold);
            const int credit = LineReducerCredit(s, line);
            CHECK(credit == (on ? 2 : 0));
            LineReducerCreditScope lrc(credit, credit > 0 ? LineNoncreatureMv(line) : 0);
            REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
            CHECK(s.battlefield[0].tapped);
            CHECK(s.battlefield[2].tapped);
            CHECK(s.battlefield[3].tapped == on);
            CHECK(s.floating_mana.red == (on ? 2 : 0));
            inferno_pays = TapForCostDirect(s, rr, /*for_creature=*/true);
        }
        CHECK(inferno_pays == on);
    }
}
