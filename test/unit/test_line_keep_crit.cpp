// MTG_LINE_KEEP_CRIT (the line-keep rung on every line of two or more held casts, gated per payment on a last-source
// colour: LineKeepCritGate + LineReducerKeepHold, ManaPayment.cpp; docs/design/line-keep-crit-2026-11-24.md). The
// defect the static line-critical re-ranks 7a / 7b targeted (docs/design/bruna-line-critical-levers.md): the greedy
// pays a pip with the LAST source of a colour the rest of the line owes, where another source could pay it. Here the
// payment changes only when the greedy's own payment strands the rest and a hold-out of its sources keeps it.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
void EnsureCardsLkc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLkc(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutLkc(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefLkc(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card CardLkc(const std::string& name, int number)
{
    Card c     = DefLkc(name).card;
    c.m_number = number;
    return c;
}

struct ArmLkc
{
    heurarm::Slot slot;
    ArmLkc(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmLkc() { heurarm::t_arm[slot] = -1; }
};

Action CastLkc(const GameState& s, const std::string& n)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = n;
    a.def       = &DefLkc(n);
    a.cost      = EffectiveSpellCost(DefLkc(n), s);
    return a;
}

GameState BaseLkc(int turn)
{
    EnsureCardsLkc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardLkc(k % 2 ? "Forest" : "Plains", 900 + k)); }
    return s;
}

// Pay line[0] under the seeds the apply worlds bind (LineUnpaidCostScope + LineReducerCreditScope, the AIEngine /
// ApplyPlanDirect twin), with `paying` as the spell being paid for when set (SpellSubtypePayScope, the cast site's
// binding); `then` (a rock that entered) is put on the board; then whether line[1]'s cost is still payable.
bool PayFirstThenRest(GameState& s, const std::vector<Action>& line, const Card* paying = nullptr,
                      const Card* then = nullptr)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(s, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    {
        SpellSubtypePayScope pay(paying);
        REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
    }
    if (then != nullptr)
    {
        Permanent p;
        p.card             = *then;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    return TapForCostDirect(s, line[1].cost, /*for_creature=*/false);
}

std::vector<int> TappedOf(const GameState& s)
{
    std::vector<int> t;
    for (const Permanent& p : s.battlefield)
    {
        t.push_back(p.tapped ? 1 : 0);
        for (const Counter& c : p.counters) { t.push_back(static_cast<int>(c.type) * 1000 + c.count); }
        t.push_back(p.storage_counters);
    }
    t.push_back(s.floating_mana.Total());
    return t;
}
}   // namespace

TEST_CASE("Line keep (crit): the binding modes -- full for KEEP_ALL / KEEP_ROCK lines, crit for the rest, none for one cast")
{
    GameState s0 = BaseLkc(3);
    const std::vector<Action> rock = { CastLkc(s0, "Sol Ring"), CastLkc(s0, "Arcanum Wings") };
    const std::vector<Action> plain = { CastLkc(s0, "Lightning Greaves"), CastLkc(s0, "Arcanum Wings") };
    const std::vector<Action> one = { CastLkc(s0, "Lightning Greaves") };
    {
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, true);
        ArmLkc r(heurarm::LINE_KEEP_ROCK, true);
        ArmLkc all(heurarm::LINE_KEEP_ALL, false);
        CHECK(LineKeepBound(rock) == kLineKeepFull);
        CHECK(LineKeepBound(plain) == kLineKeepCrit);
        CHECK(LineKeepBound(one) == 0);
    }
    {
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, true);
        ArmLkc all(heurarm::LINE_KEEP_ALL, true);
        CHECK(LineKeepBound(plain) == kLineKeepFull);
    }
    {
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, false);
        ArmLkc r(heurarm::LINE_KEEP_ROCK, false);
        ArmLkc all(heurarm::LINE_KEEP_ALL, false);
        CHECK(LineKeepBound(rock) == 0);
        CHECK(LineKeepBound(plain) == 0);
    }
}

// Bruna s30051257 T2 {Sol Ring, Arcanum Wings} -- the 7a case. Botanical Sanctum (the only {U}) + Avacyn's Pilgrim:
// the greedy pays Sol Ring's {1} with the land and Wings' {1}{U} meets Pilgrim's {W} + Sol Ring's {C}{C}. It is a ROCK
// line, so MTG_LINE_KEEP_ROCK (default ON since 2026-11-20) binds it in the full mode: the rung already keeps it.
TEST_CASE("Line keep (crit): Bruna s30051257 T2 -- Pilgrim pays Sol Ring and the Sanctum's {U} stays for Wings (rock rung)")
{
    GameState s0 = BaseLkc(2);
    PutLkc(s0, "Botanical Sanctum", 12);
    PutLkc(s0, "Avacyn's Pilgrim", 3);
    const std::vector<Action> line = { CastLkc(s0, "Sol Ring"), CastLkc(s0, "Arcanum Wings") };
    const Card ring = CardLkc("Sol Ring", 55);
    for (const bool rock : { true, false })
    {
        CAPTURE(rock);
        ArmLkc r(heurarm::LINE_KEEP_ROCK, rock);
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, false);
        ArmLkc all(heurarm::LINE_KEEP_ALL, false);
        GameState s = s0;
        CHECK(PayFirstThenRest(s, line, &ring, &ring) == rock);
    }
    // With the rock rung off the crit mode binds the line, but it judges the rest WITHOUT the rock being paid for (that
    // credit is MTG_LINE_KEEP_ROCK's): {1} + {1}{U} off two sources is unpayable, so it cannot repair it alone.
    {
        ArmLkc r(heurarm::LINE_KEEP_ROCK, false);
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, true);
        GameState s = s0;
        CHECK_FALSE(PayFirstThenRest(s, line, &ring, &ring));
    }
}

// Bruna s30051968 T2 {Sol Ring, Glittering Wish} -- the 7b (opener) case. Remote Farm ({W}{W}, depletion) + Forest (the
// only {G}): the greedy pays Sol Ring's {1} with the Forest, and the Wish's {G}{W} meets Remote Farm + Sol Ring. Also a
// rock line: the rock rung keeps it (Remote Farm pays the {1} and floats a {W}; the Forest pays the {G}).
TEST_CASE("Line keep (crit): Bruna s30051968 T2 -- Remote Farm pays Sol Ring and the Forest's {G} stays for the Wish")
{
    GameState s0 = BaseLkc(2);
    PutLkc(s0, "Remote Farm", 20).counters.Add(Counter::Type::Depletion, 2);
    PutLkc(s0, "Forest", 21);
    const std::vector<Action> line = { CastLkc(s0, "Sol Ring"), CastLkc(s0, "Glittering Wish") };
    const Card ring = CardLkc("Sol Ring", 55);
    for (const bool rock : { true, false })
    {
        CAPTURE(rock);
        ArmLkc r(heurarm::LINE_KEEP_ROCK, rock);
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, false);
        LineOpensUnseenScope opens(true);   // the Wish is a breakpoint opener (tutor_to_hand)
        GameState s = s0;
        CHECK(PayFirstThenRest(s, line, &ring, &ring) == rock);
    }
}

// THE CRIT MODE'S OWN CASE: the same shape with no rock in the line, which no rung reached before. Botanical Sanctum
// (the only {U}) + Plains + two Avacyn's Pilgrims; the line {Lightning Greaves {2}, Arcanum Wings {1}{U}}. Lands before
// creatures: the greedy pays the {2} with the Sanctum and the Plains, and Wings meets two {W}. Plains + a Pilgrim pay the
// {2}; the Sanctum's {U} + the other Pilgrim pay Wings.
TEST_CASE("Line keep (crit): no rock -- Lightning Greaves' {2} leaves the only {U} land for Arcanum Wings")
{
    GameState s0 = BaseLkc(3);
    PutLkc(s0, "Botanical Sanctum", 12);
    PutLkc(s0, "Plains", 13);
    PutLkc(s0, "Avacyn's Pilgrim", 3);
    PutLkc(s0, "Avacyn's Pilgrim", 4);
    const std::vector<Action> line = { CastLkc(s0, "Lightning Greaves"), CastLkc(s0, "Arcanum Wings") };
    REQUIRE(line[0].cost.generic == 2);
    REQUIRE(line[1].cost.blue == 1);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, on);
        GameState s = s0;
        CHECK(PayFirstThenRest(s, line) == on);
    }
}

// ...and Remote Farm's shape with no rock: Remote Farm ({W}{W}) + Forest (the only {G}); {Bonesplitter {1}, Glittering
// Wish {G}{W}}. The greedy's {1} off the Forest strands the Wish; Remote Farm pays it and floats the Wish's {W}.
TEST_CASE("Line keep (crit): no rock -- Bonesplitter's {1} off Remote Farm keeps the Forest for Glittering Wish")
{
    GameState s0 = BaseLkc(2);
    PutLkc(s0, "Remote Farm", 20).counters.Add(Counter::Type::Depletion, 2);
    PutLkc(s0, "Forest", 21);
    const std::vector<Action> line = { CastLkc(s0, "Bonesplitter"), CastLkc(s0, "Glittering Wish") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, on);
        LineOpensUnseenScope opens(true);
        GameState s = s0;
        CHECK(PayFirstThenRest(s, line) == on);
    }
}

// INERT where the greedy keeps the line, and where no source is the last of a colour the rest owes: byte for byte the
// greedy's payment on both arms (the gate returns before any scratch board).
TEST_CASE("Line keep (crit): inert when nothing is stranded, and when no colour is down to its last source")
{
    // (a) A second {U} land: the Sanctum is no longer the last {U}; whatever pays the {2}, a {U} is left.
    {
        GameState s0 = BaseLkc(3);
        PutLkc(s0, "Botanical Sanctum", 12);
        PutLkc(s0, "Island", 14);
        PutLkc(s0, "Plains", 13);
        PutLkc(s0, "Avacyn's Pilgrim", 3);
        PutLkc(s0, "Avacyn's Pilgrim", 4);
        const std::vector<Action> line = { CastLkc(s0, "Lightning Greaves"), CastLkc(s0, "Arcanum Wings") };
        std::vector<int> t_on, t_off;
        for (const bool on : { true, false })
        {
            ArmLkc crit(heurarm::LINE_KEEP_CRIT, on);
            GameState s = s0;
            CHECK(PayFirstThenRest(s, line));
            (on ? t_on : t_off) = TappedOf(s);
        }
        CHECK(t_on == t_off);
    }
    // (b) The Sanctum IS the last {U} (the gate passes), but the greedy keeps it anyway: Plains x2 + Sanctum + Pilgrim,
    //     the {2} takes the two Plains (mono before dual). The trial's witness pays the rest; the payment is the greedy's.
    {
        GameState s0 = BaseLkc(3);
        PutLkc(s0, "Plains", 13);
        PutLkc(s0, "Plains", 15);
        PutLkc(s0, "Botanical Sanctum", 12);
        PutLkc(s0, "Avacyn's Pilgrim", 3);
        const std::vector<Action> line = { CastLkc(s0, "Lightning Greaves"), CastLkc(s0, "Arcanum Wings") };
        std::vector<int> t_on, t_off;
        for (const bool on : { true, false })
        {
            ArmLkc crit(heurarm::LINE_KEEP_CRIT, on);
            GameState s = s0;
            CHECK(PayFirstThenRest(s, line));
            (on ? t_on : t_off) = TappedOf(s);
        }
        CHECK(t_on == t_off);
    }
}

// Dragonstorm gi679 (seed 1810679), the board 7a lost on: {Desperate Ritual, Desperate Ritual, Scourge of Valkas} off
// Mercadian Bazaar (2 storage counters) + Sandstone Needle (2 depletion) + Mountain. 7a moved the Needle behind the
// Bazaar's held tier and the Ritual's {1} spent a storage counter (T5 kill -> T6 at every depth). The crit mode
// leaves the Ritual's payment exactly as the greedy makes it: the rest of a ritual line is not payable from the board
// alone (the Rituals' own float pays it), so the rung's joint test refuses before any repair.
TEST_CASE("Line keep (crit): Dragonstorm gi679's Desperate Ritual payment is the greedy's (the board 7a lost on)")
{
    GameState s0 = BaseLkc(3);
    PutLkc(s0, "Mercadian Bazaar", 30).storage_counters = 2;
    PutLkc(s0, "Sandstone Needle", 31).counters.Add(Counter::Type::Depletion, 2);
    PutLkc(s0, "Mountain", 32);
    const std::vector<Action> line = { CastLkc(s0, "Desperate Ritual"), CastLkc(s0, "Desperate Ritual"),
                                       CastLkc(s0, "Scourge of Valkas") };
    const Card ritual = CardLkc("Desperate Ritual", 90);
    std::vector<int> t_on, t_off;
    for (const bool on : { true, false })
    {
        ArmLkc crit(heurarm::LINE_KEEP_CRIT, on);
        CHECK(LineKeepBound(line) == (on ? kLineKeepCrit : 0));
        GameState s = s0;
        LineUnpaidCostScope luc(LineCastCostTotal(line));
        const int bound = LineKeepBound(line);
        LineReducerCreditScope lrc(0, LineNoncreatureMv(line), bound);
        SpellSubtypePayScope pay(&ritual);
        REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
        (on ? t_on : t_off) = TappedOf(s);
    }
    CHECK(t_on == t_off);
}
