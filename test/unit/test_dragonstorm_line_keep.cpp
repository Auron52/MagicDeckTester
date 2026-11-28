// Dragonstorm APPLY-DROP (mana-clean gate claude-queue-20261125a, 728 of the deck's 1,137 issues). Every one of the
// 146 dropped-cast events has Lotus Bloom or Unclaimed Territory on the board: the deck's only {B} / {G} sources for
// Karrthus, Tyrant of Jund ({4}{B}{R}{G}) and Dragonlord Kolaghan ({4}{B}{R}), beside the mana a cracked Bloom leaves
// floating (docs/design/dragonstorm-apply-drop-2026-11-26.md).
//   MTG_ONDEMAND_VISIBLE_LUMP_EXACT -- the visible-closer lump rule cracked a Bloom early on a colour comparison that
//                                      read an uncracked Bloom as no supply.
//   MTG_LEFTOVER_SWAP_WIDE          -- the leftover swap moved only units the payment produced and judged the rest
//                                      without the ritual's float.
//   MTG_CCO_RESERVE_JOINT           -- the CCO line reserve let each of two Unclaimed Territories go unheld because the
//                                      other "covered" both of Karrthus's {B} and {G} (one tap makes one unit).
//   MTG_LINE_NC_LEFT                -- the line-keep rung judged a creature-only rest as a noncreature bill once an
//                                      earlier noncreature cast of the line was paid (the line's total never counts down).
//   MTG_ONDEMAND_LUMP_KEEP_OWED     -- a Bloom crack that paid a bill spent all its {B} on the generic, though another
//                                      untapped source could pay that part and let the {B} the line owes float on.
//   MTG_LINE_SEQ_REST               -- the payer judged the rest of the line as one summed bill, crediting none of the
//                                      rest's own rituals; with the line's casts walked in order, a floating {B} the rest
//                                      owes and does not make is kept when the plain payment strands the rest.
//   MTG_UNSEEN_SCARCE_RITUAL_NET    -- the unseen-keep-scarce drain counted the later red the paying ritual itself
//                                      makes as owed by the float, so a generic unit came off the {B}.
//   MTG_LINE_REST_MEMO              -- (CPU) the owed float keep's verdict memoised on the payment board digest and
//                                      context: a repeated entry pays exactly as the recompute does.
#include <doctest/doctest.h>

#include "ai/BoardIdentity.h"
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
    LineNoncreatureLeftScope lnl(LineNoncreatureLeftCount(line));
    const Card rc = CardDlk(line[0].card_name, 40);
    NoteLineNoncreatureCast(DefDlk(line[0].card_name));
    {
        SpellSubtypePayScope pay(&rc);
        REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
    }
    SubManaCost(g_line_unpaid_cost, line[0].cost);
    s.floating_mana.red += ritual_red;   // the ritual resolves
    const Card cc = CardDlk(line[1].card_name, 41);
    NoteLineNoncreatureCast(DefDlk(line[1].card_name));
    SpellSubtypePayScope pay(&cc);
    return TapForCostDirect(s, line[1].cost, /*for_creature=*/true);
}

// Pay the line in order under the seeds the apply worlds bind (each cast's own creature flag; a ritual's float is added
// after its payment, as it resolves). The first `paid` casts were paid before this board (only the hold is reduced).
// True when every other cast is paid.
bool PayLineDlk(GameState& s, const std::vector<Action>& line, std::size_t paid = 0, const GameState* seed = nullptr)
{
    // The scopes are seeded on the board the line was enumerated on (`seed`, before the `paid` casts), as both apply
    // worlds seed them; each cast pays and leaves the hold at its EFFECTIVE cost (a Ruby Medallion cast in the line
    // reduces the casts after it).
    const GameState& at = seed != nullptr ? *seed : s;
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(at, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    LineNoncreatureLeftScope lnl(LineNoncreatureLeftCount(line));   // counted down at each cast, before its payment
    int num = 40;
    for (std::size_t i = 0; i < paid; ++i)
    { NoteLineNoncreatureCast(DefDlk(line[i].card_name)); SubManaCost(g_line_unpaid_cost, line[i].cost); }
    for (std::size_t i = paid; i < line.size(); ++i)
    {
        const CardDefinition& d = DefDlk(line[i].card_name);
        const Card c = CardDlk(line[i].card_name, num++);
        const ManaCost cost = EffectiveSpellCost(d, s);
        NoteLineNoncreatureCast(d);
        {
            SpellSubtypePayScope pay(&c);
            if (!TapForCostDirect(s, cost, /*for_creature=*/d.card.IsCreature())) { return false; }
        }
        SubManaCost(g_line_unpaid_cost, cost);
        if (d.params.ritual_floating_mana > 0) { s.floating_mana.red += d.params.ritual_floating_mana; }
        if (d.params.impulse_float_amount > 0) { s.floating_mana.red += d.params.impulse_float_amount; }   // Apex: red
    }
    return true;
}
// Pay `line` in order under the seeds the apply worlds bind, the line's casts in order included (MTG_LINE_SEQ_REST:
// LineSeqScope, stepped at each cast by NoteLineSeqCast before its payment). The first `paid` casts were paid before
// this board (the hold is reduced and the sequence stepped). True when every other cast is paid.
bool PayLineSeqDlk(GameState& s, const std::vector<Action>& line, std::size_t paid, const GameState& seed)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(seed, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    LineNoncreatureLeftScope lnl(LineNoncreatureLeftCount(line));
    LineSeqScope lsq(line);
    int num = 40;
    for (std::size_t i = 0; i < paid; ++i)
    {
        const CardDefinition& d = DefDlk(line[i].card_name.str());
        NoteLineNoncreatureCast(d);
        NoteLineSeqCast(d);
        SubManaCost(g_line_unpaid_cost, EffectiveSpellCost(d, seed));
    }
    for (std::size_t i = paid; i < line.size(); ++i)
    {
        const CardDefinition& d = DefDlk(line[i].card_name.str());
        const Card c = CardDlk(line[i].card_name.str(), num++);
        const ManaCost cost = EffectiveSpellCost(d, s);
        NoteLineNoncreatureCast(d);
        NoteLineSeqCast(d);
        {
            SpellSubtypePayScope pay(&c);
            if (!TapForCostDirect(s, cost, /*for_creature=*/d.card.IsCreature())) { return false; }
        }
        SubManaCost(g_line_unpaid_cost, cost);
        if (d.params.ritual_floating_mana > 0) { s.floating_mana.red += d.params.ritual_floating_mana; }
    }
    return true;
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

// Dragonstorm s30096957 gi96957 T4 m1 {Ruby Medallion, Seething Song, Karrthus}: Ruby was paid by a Lotus Bloom cracked
// for {B}; one {B} floats beside two untapped Mountains and the second Bloom. Song's {1}{R} (Ruby) drained the {B} into
// its {1}, because both Mountains read as non-spare (Karrthus owes {R}), though Song's own {R}x5 pays that {R}; the Bloom
// then had to make Karrthus's {B} and {G} at once. MTG_LINE_FLOAT_RITUAL_NET: the Mountains are spare, the {B} is held.
TEST_CASE("Dragonstorm line keep: s30096957 T4 -- Song's own float makes the Mountains spare, the {B} is kept")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Ruby Medallion", 44);
    PutDlk(s0, "Mountain", 29);
    PutDlk(s0, "Mountain", 30);
    PutDlk(s0, "Lotus Bloom", 18);
    s0.floating_mana.black = 1;
    const std::vector<Action> line = { CastDlk(s0, "Seething Song"), CastDlk(s0, "Karrthus, Tyrant of Jund") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::LINE_FLOAT_RITUAL_NET, on);
        GameState s = s0;
        CHECK(PayRitualThenCreature(s, line, 5) == on);
    }
}

// Dragonstorm s30096797 gi96797 T5 m1 {Mountain, Desperate Ritual, Seething Song, Karrthus}: Mercadian Bazaar (two
// counters), Sandstone Needle, two Unclaimed Territories and the new Mountain. Karrthus's {B} and {G} have only the two
// Territories; the CCO line reserve let each go unheld because the other "covered" both colours, so Desperate's generic
// took a Territory's {C}. MTG_CCO_RESERVE_JOINT holds both (they jointly make only two units for the two pips).
TEST_CASE("Dragonstorm line keep: s30096797 T5 -- two Territories are held jointly for Karrthus's {B}{G}")
{
    GameState s0 = BaseDlk(5);
    Permanent& n = PutDlk(s0, "Sandstone Needle", 49);
    PutDepletionCounters(s0, n, 2);
    PutDlk(s0, "Mercadian Bazaar", 22).storage_counters = 2;
    PutDlk(s0, "Unclaimed Territory", 57);
    PutDlk(s0, "Unclaimed Territory", 58);
    PutDlk(s0, "Mountain", 31);
    // The game's hand (the casts' cards and two Dragonstorms): a noncreature card with a coloured pip in hand is what
    // sends an unheld Territory to the narrow rung (MTG_CCO_HAND_DEMAND), ahead of the Bazaar and the Needle.
    for (const char* h : { "Seething Song", "Dragonstorm", "Dragonstorm", "Karrthus, Tyrant of Jund" })
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    const std::vector<Action> line = { CastDlk(s0, "Desperate Ritual"), CastDlk(s0, "Seething Song"),
                                       CastDlk(s0, "Karrthus, Tyrant of Jund") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::CCO_RESERVE_JOINT, on);
        GameState s = s0;
        CHECK(PayLineDlk(s, line) == on);
    }
}

// Control: the same board, the line owes only Dragonlord Kolaghan's {B} -- either Territory covers it, so neither is
// held under either test and both arms pay the same way.
TEST_CASE("Dragonstorm line keep: two Territories, one owed colour -- the joint test holds nothing more")
{
    GameState s0 = BaseDlk(5);
    Permanent& n = PutDlk(s0, "Sandstone Needle", 49);
    PutDepletionCounters(s0, n, 2);
    PutDlk(s0, "Mercadian Bazaar", 22).storage_counters = 2;
    PutDlk(s0, "Unclaimed Territory", 57);
    PutDlk(s0, "Unclaimed Territory", 58);
    PutDlk(s0, "Mountain", 31);
    // The game's hand (the casts' cards and two Dragonstorms): a noncreature card with a coloured pip in hand is what
    // sends an unheld Territory to the narrow rung (MTG_CCO_HAND_DEMAND), ahead of the Bazaar and the Needle.
    for (const char* h : { "Seething Song", "Dragonstorm", "Dragonstorm", "Karrthus, Tyrant of Jund" })
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    const std::vector<Action> line = { CastDlk(s0, "Desperate Ritual"), CastDlk(s0, "Seething Song"),
                                       CastDlk(s0, "Dragonlord Kolaghan") };
    GameState a = s0, b = s0;
    {
        ArmDlk as(heurarm::CCO_RESERVE_JOINT, true);
        CHECK(PayLineDlk(a, line));
    }
    {
        ArmDlk as(heurarm::CCO_RESERVE_JOINT, false);
        CHECK(PayLineDlk(b, line));
    }
    REQUIRE(a.battlefield.size() == b.battlefield.size());
    for (std::size_t i = 0; i < a.battlefield.size(); ++i) { CHECK(a.battlefield[i].tapped == b.battlefield[i].tapped); }
    CHECK(a.floating_mana.Total() == b.floating_mana.Total());
}

// Dragonstorm s30129376 gi129376 T4 m1 {Mountain, Ruby Medallion, Seething Song, Karrthus}: Ruby was paid by a Lotus
// Bloom cracked for {B} (no land played); one {B} floats beside Sandstone Needle, one Unclaimed Territory and a Mountain. Song's {1}{R}
// (Ruby) was paid with the Mountain's {R} and the floating {B}, and Karrthus's {B} and {G} were left to the one Territory.
// The line-keep rung judged Karrthus as a NONCREATURE bill (the line's noncreature total, Ruby + Song = 5, is above
// Song's mana value 2, and it never counts down), so Karrthus read unpayable however Song was paid and the rung kept
// nothing. MTG_LINE_NC_LEFT sees that no noncreature cast is left after Song: the rung keeps the {B} and Sandstone
// Needle's {R}{R} pays Song. (Traced in game: MTG_DS2_PAYDBG, logs/ds-apply2/ds2pay-debug.snippet.cpp.)
TEST_CASE("Dragonstorm line keep: s30129376 T4 -- no noncreature cast is left after Song, so the {B} is kept for Karrthus")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Ruby Medallion", 43);
    Permanent& n = PutDlk(s0, "Sandstone Needle", 49);
    PutDepletionCounters(s0, n, 2);
    PutDlk(s0, "Unclaimed Territory", 57);
    PutDlk(s0, "Mountain", 29);
    s0.floating_mana.black = 1;
    for (const char* h : { "Apex of Power", "Seething Song", "Mountain", "Karrthus, Tyrant of Jund" })   // the game's hand
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    // Ruby is in the line (and the hold) and already paid; its cost as enumerated, before it was on the battlefield.
    GameState pre = s0;
    pre.battlefield.erase(pre.battlefield.begin());   // the Ruby
    const std::vector<Action> line = { CastDlk(pre, "Ruby Medallion"), CastDlk(pre, "Seething Song"),
                                       CastDlk(pre, "Karrthus, Tyrant of Jund") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::LINE_NC_LEFT, on);
        GameState s = s0;
        CHECK(PayLineDlk(s, line, 1, &pre) == on);
    }
}

// Dragonstorm s30120119 gi120119 T6 m1 {Dwarven Hold, Desperate Ritual, Seething Song, Apex of Power, Irencrag Feat,
// Dragonlord Kolaghan}: Desperate (Mountain#31) and Song (Ruby) leave six {R} floating beside Mountain#26 and Lotus Bloom.
// Apex's {6}{R}{R}{R} (Ruby) drained the six {R} and cracked the Bloom for its last three generic with the Mountain still
// untapped (the crack is in {B}, the colour the line has no other source of, but the drain spent all three); Apex's ten
// {R} and Irencrag's seven left Kolaghan's {B} with no source. MTG_ONDEMAND_LUMP_KEEP_OWED sets the owed {B} aside: the
// Mountain pays one generic, two {B} pay the rest, one {B} floats on to Kolaghan. (Traced in game: MTG_DS2_PAYDBG.)
TEST_CASE("Dragonstorm line keep: s30120119 T6 -- Apex taps the Mountain and the Bloom's {B} floats to Kolaghan")
{
    GameState s0 = BaseDlk(6);
    PutDlk(s0, "Mountain", 31).tapped = true;
    PutDlk(s0, "Mountain", 26);
    PutDlk(s0, "Ruby Medallion", 44);
    PutDlk(s0, "Lotus Bloom", 18);
    PutDlk(s0, "Dwarven Hold", 14).tapped = true;
    s0.floating_mana.red = 6;
    for (const char* h : { "Apex of Power", "Lotus Bloom", "Irencrag Feat", "Dragonlord Kolaghan" })   // the game's hand
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    const std::vector<Action> line = { CastDlk(s0, "Apex of Power"), CastDlk(s0, "Irencrag Feat"),
                                       CastDlk(s0, "Dragonlord Kolaghan") };
    LineOpensUnseenScope unseen(true);   // Apex opens a breakpoint
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::ONDEMAND_LUMP_KEEP_OWED, on);
        GameState s = s0;
        CHECK(PayLineDlk(s, line) == on);
    }
}

// Dragonstorm s30152803 gi152803 T4 m1 {Mountain, Ruby Medallion, Seething Song, Seething Song, Dragonlord Kolaghan,
// Irencrag Feat, Dragonstorm}: Ruby's {2} cracked Lotus Bloom for {B} and one {B} floats beside two untapped Mountains.
// The first Song's {1}{R} spent the {B} on the {1} (the float rung reads the second Mountain as non-spare: the rest owes
// red beyond Song's own five) and Kolaghan's {B} had no source. MTG_LINE_SEQ_REST's owed float keep: the plain payment
// strands the rest walked in order, so the {B} is set aside and both Mountains pay; the walk (Song, Kolaghan, Irencrag's
// seven, Dragonstorm, every red cast reduced by the Ruby) pays the rest. (Traced in game: MTG_DS2_PAYDBG.)
TEST_CASE("Dragonstorm line keep: s30152803 T4 -- the first Song taps both Mountains and the Bloom's {B} floats to Kolaghan")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Mountain", 33);
    PutDlk(s0, "Mountain", 27);
    PutDlk(s0, "Ruby Medallion", 43);
    s0.floating_mana.black = 1;
    for (const char* h : { "Seething Song", "Seething Song", "Dragonlord Kolaghan", "Irencrag Feat", "Dragonstorm" })
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    GameState pre = BaseDlk(4);   // the board the line was enumerated on (no Ruby yet)
    const std::vector<Action> line = { CastDlk(pre, "Ruby Medallion"), CastDlk(pre, "Seething Song"),
                                       CastDlk(pre, "Seething Song"), CastDlk(pre, "Dragonlord Kolaghan"),
                                       CastDlk(pre, "Irencrag Feat"), CastDlk(pre, "Dragonstorm") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::LINE_SEQ_REST, on);
        GameState s = s0;
        CHECK(PayLineSeqDlk(s, line, 1, pre) == on);
    }
}

// MTG_LINE_REST_MEMO: the s30152803 entry repeated. The owed float keep's first entry on this board stores its verdict
// (the kept payment: the plain one strands Kolaghan's {B}); the second, on an equal board in an equal context, makes
// the same payment from the memo, and both leave exactly the board the recompute (memo off) leaves. A context that
// differs (another lever's arm: PayContextDigest folds the arm) or a board that differs (one more Mountain tapped) is
// not served; neither is a board that differs only in a field BoardHash does not read.
TEST_CASE("Dragonstorm line keep: MTG_LINE_REST_MEMO -- a repeated owed-float entry pays exactly as the recompute")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Mountain", 33);
    PutDlk(s0, "Mountain", 27);
    PutDlk(s0, "Ruby Medallion", 43);
    s0.floating_mana.black = 1;
    for (const char* h : { "Seething Song", "Seething Song", "Dragonlord Kolaghan", "Irencrag Feat", "Dragonstorm" })
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    GameState pre = BaseDlk(4);
    const std::vector<Action> line = { CastDlk(pre, "Ruby Medallion"), CastDlk(pre, "Seething Song"),
                                       CastDlk(pre, "Seething Song"), CastDlk(pre, "Dragonlord Kolaghan"),
                                       CastDlk(pre, "Irencrag Feat"), CastDlk(pre, "Dragonstorm") };
    ArmDlk seq(heurarm::LINE_SEQ_REST, true);
    GameState off = s0;
    {
        ArmDlk m(heurarm::LINE_REST_MEMO, false);
        ClearLineRestMemoPerGame();
        REQUIRE(PayLineSeqDlk(off, line, 1, pre));
        CHECK(LineRestMemoCount(0) == 0);   // memo off: never consulted
    }
    ArmDlk m(heurarm::LINE_REST_MEMO, true);
    ClearLineRestMemoPerGame();
    GameState a = s0;
    REQUIRE(PayLineSeqDlk(a, line, 1, pre));
    CHECK(LineRestMemoCount(1) == 0);
    CHECK(LineRestMemoCount(2) >= 1);       // the first Song's entry stored its verdict
    CHECK(boardid::BoardsEqual(a, off));
    const long long hits0 = LineRestMemoCount(1);
    GameState b = s0;
    REQUIRE(PayLineSeqDlk(b, line, 1, pre));
    CHECK(LineRestMemoCount(1) > hits0);    // served from the memo
    CHECK(boardid::BoardsEqual(b, off));
    CHECK(b.floating_mana.black == off.floating_mana.black);
    // Another context: the arm of an unrelated lever moves PayContextDigest, so nothing is served.
    {
        const long long h1 = LineRestMemoCount(1);
        ArmDlk other(heurarm::LINE_FLOAT_COVERED, false);   // -1 -> 0: the arm PayContextDigest folds moves
        GameState c = s0;
        const bool rc = PayLineSeqDlk(c, line, 1, pre);
        CHECK(LineRestMemoCount(1) == h1);
        GameState c_ref = s0;
        {
            ArmDlk mo(heurarm::LINE_REST_MEMO, false);
            CHECK(PayLineSeqDlk(c_ref, line, 1, pre) == rc);
        }
        CHECK(boardid::BoardsEqual(c, c_ref));
    }
    // Another board: one Mountain tapped. Its first entry is not the stored one (a later entry may meet a board the
    // first run left, which is a legitimate hit), and it pays as the recompute pays it.
    {
        GameState d0 = s0;
        d0.battlefield[d0.battlefield.size() - 3].tapped = true;   // the first Mountain
        GameState d = d0;
        const bool rd = PayLineSeqDlk(d, line, 1, pre);
        GameState d_ref = d0;
        {
            ArmDlk mo(heurarm::LINE_REST_MEMO, false);
            CHECK(PayLineSeqDlk(d_ref, line, 1, pre) == rd);
        }
        CHECK(boardid::BoardsEqual(d, d_ref));
    }
    // A field BoardHash does not read (a tapped Mercadian Bazaar's storage counters) is in the payment board digest:
    // the board with three counters is not served the verdict stored for the board with none.
    {
        GameState e0 = s0;
        PutDlk(e0, "Mercadian Bazaar", 24).tapped = true;
        GameState ea = e0;
        REQUIRE(PayLineSeqDlk(ea, line, 1, pre));
        GameState e1 = e0;
        e1.battlefield.back().storage_counters = 3;
        const long long h1 = LineRestMemoCount(1);
        GameState eb = e1;
        const bool rb = PayLineSeqDlk(eb, line, 1, pre);
        CHECK(LineRestMemoCount(1) == h1);
        GameState eb_ref = e1;
        {
            ArmDlk mo(heurarm::LINE_REST_MEMO, false);
            CHECK(PayLineSeqDlk(eb_ref, line, 1, pre) == rb);
        }
        CHECK(boardid::BoardsEqual(eb, eb_ref));
    }
}

// Dragonstorm s30093026 gi93026 T4, two Apexes before Dragonlord Kolaghan: the continuation's Seething Song {2}{R} over
// {B} + fourteen {R} (no untapped source left, so the unseen-keep-scarce drain keeps both colours) owes more later red
// than the float holds after its own pip; red's spare read zero, and both generic came off the keep colours in hand
// order (the hand wants more red than floats), the {B} first. (Board reduced from the game's: B1 R14, later red 12.) Song's own five red pay those later pips: MTG_UNSEEN_SCARCE_RITUAL_NET
// nets them, red pays both generic, and the {B} floats on for Kolaghan.
TEST_CASE("Dragonstorm line keep: s30093026 T4 -- Song's own red nets the later red, the scarce drain keeps the {B}")
{
    GameState s0 = BaseDlk(4);
    PutDlk(s0, "Mountain", 28).tapped = true;
    PutDlk(s0, "Mercadian Bazaar", 24).tapped = true;
    s0.floating_mana.black = 1;
    s0.floating_mana.red = 14;
    for (const char* h : { "Seething Song", "Scourge of Valkas", "Dragonlord Kolaghan", "Irencrag Feat", "Apex of Power",
                           "Apex of Power", "Dragonstorm" })
    { s0.players[0].hand.push_back(CardDlk(h, 60 + static_cast<int>(s0.players[0].hand.size()))); }
    const std::vector<Action> line = { CastDlk(s0, "Seething Song"), CastDlk(s0, "Scourge of Valkas"),
                                       CastDlk(s0, "Dragonlord Kolaghan"), CastDlk(s0, "Irencrag Feat"),
                                       CastDlk(s0, "Apex of Power"), CastDlk(s0, "Apex of Power"),
                                       CastDlk(s0, "Dragonstorm") };
    LineOpensUnseenScope unseen(true);   // the continuation follows an Apex (a breakpoint opener)
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmDlk as(heurarm::UNSEEN_SCARCE_RITUAL_NET, on);
        GameState s = s0;
        LineUnpaidCostScope luc(LineCastCostTotal(line));
        const Card c = CardDlk("Seething Song", 40);
        {
            SpellSubtypePayScope pay(&c);
            REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
        }
        CHECK(s.floating_mana.black == (on ? 1 : 0));
    }
}
