// Dragonstorm APPLY-DROP (mana-clean gate claude-queue-20261125a, 728 of the deck's 1,137 issues). Every one of the
// 146 dropped-cast events has Lotus Bloom or Unclaimed Territory on the board: the deck's only {B} / {G} sources for
// Karrthus, Tyrant of Jund ({4}{B}{R}{G}) and Dragonlord Kolaghan ({4}{B}{R}), beside the mana a cracked Bloom leaves
// floating (docs/design/dragonstorm-apply-drop-2026-11-26.md).
//   MTG_ONDEMAND_VISIBLE_LUMP_EXACT -- the visible-closer lump rule cracked a Bloom early on a colour comparison that
//                                      read an uncracked Bloom as no supply.
//   MTG_LEFTOVER_SWAP_WIDE          -- the leftover swap moved only units the payment produced and judged the rest
//                                      without the ritual's float.
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

#include <string>
#include <vector>

namespace
{
void EnsureCardsDlk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefDlk(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutDlk(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefDlk(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card CardDlk(const std::string& name, int number)
{
    Card c     = DefDlk(name).card;
    c.m_number = number;
    return c;
}

struct ArmDlk
{
    heurarm::Slot slot;
    ArmDlk(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmDlk() { heurarm::t_arm[slot] = -1; }
};

Action CastDlk(const GameState& s, const std::string& n)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = n;
    a.def            = &DefDlk(n);
    a.cost           = EffectiveSpellCost(DefDlk(n), s);
    a.is_noncreature = !DefDlk(n).card.IsCreature();
    return a;
}

const DragonstormProvider& DsProvDlk()
{
    static const DragonstormProvider p;
    return p;
}

GameState BaseDlk(int turn)
{
    EnsureCardsDlk();
    GameState s;
    s.m_provider = &DsProvDlk();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardDlk("Mountain", 900 + k)); }
    return s;
}

int CountNamed(const GameState& s, const std::string& n)
{
    int k = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == n) { ++k; } }
    return k;
}

// Pay line[0] (a ritual: its float is added after the payment, as it resolves) under the seeds the apply worlds bind,
// then whether line[1] (a creature) is still payable.
bool PayRitualThenCreature(GameState& s, const std::vector<Action>& line, int ritual_red)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(s, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    const Card rc = CardDlk(line[0].card_name, 40);
    {
        SpellSubtypePayScope pay(&rc);
        REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
    }
    SubManaCost(g_line_unpaid_cost, line[0].cost);
    s.floating_mana.red += ritual_red;   // the ritual resolves
    const Card cc = CardDlk(line[1].card_name, 41);
    SpellSubtypePayScope pay(&cc);
    return TapForCostDirect(s, line[1].cost, /*for_creature=*/true);
}
}   // namespace

// Dragonstorm s30152662 gi152662 T5 m1 {Seething Song, Irencrag Feat, Karrthus}: Song was paid with Mercadian Bazaar and
// one Lotus Bloom cracked for {B}; {B} + seven {R} float. Irencrag's {1}{R}{R}{R} is payable from the float alone, but the
// greedy cracked the second Bloom for {B} as well, and Karrthus's {G} had no source. Holding that Bloom out keeps it.
TEST_CASE("Dragonstorm line keep: s30152662 T5 -- Irencrag is paid from the float, the second Bloom stays for Karrthus's {G}")
{
    GameState s0 = BaseDlk(5);
    PutDlk(s0, "Mercadian Bazaar", 24).tapped = true;
    PutDlk(s0, "Dwarven Hold", 14).storage_counters = 2;
    PutDlk(s0, "Lotus Bloom", 18);
    s0.floating_mana.black = 1;
    s0.floating_mana.red = 7;
    const std::vector<Action> line = { CastDlk(s0, "Irencrag Feat"), CastDlk(s0, "Karrthus, Tyrant of Jund") };
    ArmDlk crit(heurarm::LINE_KEEP_CRIT, true);
    ArmDlk all(heurarm::LINE_KEEP_ALL, false);
    REQUIRE(LineKeepBound(line) == kLineKeepCrit);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::ONDEMAND_VISIBLE_LUMP_EXACT, on);
        GameState s = s0;
        CHECK(PayRitualThenCreature(s, line, 7) == on);
        if (on) { CHECK(CountNamed(s, "Lotus Bloom") == 0); }   // cracked for Karrthus's {G}, not for Irencrag
    }
}

// Dragonstorm s30087280 gi87280 T4 m1 {Desperate Ritual, Scourge of Valkas, Irencrag Feat, Dragonlord Kolaghan}: after
// Scourge (paid with Desperate's {R}{R}{R} and a Bloom cracked for {B}), one {B} floats beside Mercadian Bazaar (two
// counters) and an untapped Sandstone Needle. Irencrag's {1} drained the {B} and Kolaghan's {B} had nothing; the oracle
// pays Irencrag with the Bazaar and the Needle and keeps the {B} floating.
TEST_CASE("Dragonstorm line keep: s30087280 T4 -- the floating {B} is kept through Irencrag for Kolaghan")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Mercadian Bazaar", 22).storage_counters = 2;
    PutDlk(s0, "Sandstone Needle", 46).tapped = true;
    Permanent& n47 = PutDlk(s0, "Sandstone Needle", 47);
    PutDepletionCounters(s0, n47, 2);
    s0.floating_mana.black = 1;
    const std::vector<Action> line = { CastDlk(s0, "Irencrag Feat"), CastDlk(s0, "Dragonlord Kolaghan") };
    ArmDlk crit(heurarm::LINE_KEEP_CRIT, true);
    ArmDlk all(heurarm::LINE_KEEP_ALL, false);
    REQUIRE(LineKeepBound(line) == kLineKeepCrit);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::LEFTOVER_SWAP_WIDE, on);
        GameState s = s0;
        CHECK(PayRitualThenCreature(s, line, 7) == on);
    }
}

// No strand: a Swamp and a Forest are up, so the Blooms are not the last {B} / {G}. Both arms pay the line and leave the
// same amount of mana (floating + untapped lands). WHICH unit is left may differ: the 2026-11-26 unit run had one more
// land untapped with the levers on and a different float (the levers re-attribute a generic pip; they never tap more).
TEST_CASE("Dragonstorm line keep: no strand -- both arms pay the line and leave the same mana")
{
    GameState s0 = BaseDlk(5);
    PutDlk(s0, "Mercadian Bazaar", 24).tapped = true;
    PutDlk(s0, "Dwarven Hold", 14).storage_counters = 2;
    PutDlk(s0, "Lotus Bloom", 18);
    PutDlk(s0, "Swamp", 30);
    PutDlk(s0, "Forest", 31);
    s0.floating_mana.black = 1;
    s0.floating_mana.red = 7;
    const std::vector<Action> line = { CastDlk(s0, "Irencrag Feat"), CastDlk(s0, "Karrthus, Tyrant of Jund") };
    ArmDlk crit(heurarm::LINE_KEEP_CRIT, true);
    ArmDlk all(heurarm::LINE_KEEP_ALL, false);
    GameState a = s0, b = s0;
    {
        ArmDlk ex(heurarm::ONDEMAND_VISIBLE_LUMP_EXACT, true);
        ArmDlk sw(heurarm::LEFTOVER_SWAP_WIDE, true);
        CHECK(PayRitualThenCreature(a, line, 7));
    }
    {
        ArmDlk ex(heurarm::ONDEMAND_VISIBLE_LUMP_EXACT, false);
        ArmDlk sw(heurarm::LEFTOVER_SWAP_WIDE, false);
        CHECK(PayRitualThenCreature(b, line, 7));
    }
    CHECK(CountNamed(a, "Lotus Bloom") == CountNamed(b, "Lotus Bloom"));
    REQUIRE(a.battlefield.size() == b.battlefield.size());
    const auto left = [](const GameState& g)
    {
        int n = static_cast<int>(g.floating_mana.Total());
        for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
        return n;
    };
    CHECK(left(a) == left(b));
    int untapped_a = 0, untapped_b = 0;
    for (std::size_t i = 0; i < a.battlefield.size(); ++i)
    {
        untapped_a += a.battlefield[i].tapped ? 0 : 1;
        untapped_b += b.battlefield[i].tapped ? 0 : 1;
    }
    CHECK(untapped_a >= untapped_b);   // the wide swap never taps more than the narrow one
}

// 2HG s5131993 gi1993 T5 {Rite of Flame, Seething Song, Dragonlord Kolaghan}, Scourge of Valkas attacking after: Rite's
// {R}{R} floats beside an untapped Sandstone Needle and Lotus Bloom. Both payments of Song pay Kolaghan later (the
// ordinary one taps the Needle, Kolaghan cracks the Bloom for {B}), so neither strands the bill; the early crack pays
// Song's generic with the Bloom's {B} and keeps the Needle's {R}{R} for firebreathing. The strand-only refusal leaves the
// early crack alone: both arms crack the Bloom at Song and keep the Needle (the first EXACT refused it and cost the kill).
TEST_CASE("Dragonstorm line keep: s5131993 2HG T5 -- both boards pay, the early crack keeps the Needle")
{
    GameState s0 = BaseDlk(5);
    PutDlk(s0, "Mountain", 26).tapped = true;
    Permanent& n = PutDlk(s0, "Sandstone Needle", 46);
    PutDepletionCounters(s0, n, 2);
    PutDlk(s0, "Lotus Bloom", 18);
    s0.floating_mana.red = 2;
    const std::vector<Action> line = { CastDlk(s0, "Seething Song"), CastDlk(s0, "Dragonlord Kolaghan") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::ONDEMAND_VISIBLE_LUMP_EXACT, on);
        GameState s = s0;
        CHECK(PayRitualThenCreature(s, line, 5));
        CHECK(CountNamed(s, "Lotus Bloom") == 0);
        bool needle_up = false;
        for (const Permanent& p : s.battlefield)
        { if (p.card.m_name.str() == "Sandstone Needle" && !p.tapped) { needle_up = true; } }
        CHECK(needle_up);
    }
}
