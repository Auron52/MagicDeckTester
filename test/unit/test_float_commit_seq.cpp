// The SEQUENCE-aware float-colour commit (MTG_FLOAT_COMMIT_SEQ, DEFAULT ON; SpellEffects.h
// CommitFloatChoicesBySeq; docs/design/placeholder-card-reads.md, MANA).
//
// A choice source tapped ahead (Mystic Monastery {U}/{R}/{W}, Reflecting Pool, Cascade Bluffs) floats one
// unit that must be committed to a colour, and nothing searches that colour. The historical commit weighed
// the HAND's pips as a total: with the hand's real pips (MTG_PLACEHOLDER_MANA) a Hinata hand -- Irencrag
// Feat, Crackle with Power, Soulfire Eruption: {R} everywhere -- committed every tap-ahead to {R}, and the
// Reality Spasm the tap-ahead was floating for could not pay its own {U}{U}. These pin the rule that
// replaces it while a line is applying: the pending cast's pips first, then the rest of the line in cast
// order (what the board left after the step cannot make before what it can).
//
// Every case runs with MTG_PLACEHOLDER_MANA forced ON (the read this lever unblocks) and leaves
// MTG_FLOAT_COMMIT_SEQ at its env default, so `MTG_FLOAT_COMMIT_SEQ=0 mtg-test -tc="FloatCommitSeq*"`
// runs the same cases on the historical commit -- and every one of them FAILS there. The hands are built
// from placeholders exactly as play builds them (a def-built hand would carry real pips with the
// placeholder lever off too).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsFs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFs(const std::string& name)
{
    EnsureCardsFs();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// MTG_PLACEHOLDER_MANA forced ON for this thread (the hand pips are real), restored on exit. The lever
// under test is NOT forced: its env default decides, which is what lets the =0 run show the failures.
struct FsArms
{
    std::int8_t saved;
    FsArms() : saved(heurarm::t_arm[heurarm::PLACEHOLDER_MANA]) { heurarm::t_arm[heurarm::PLACEHOLDER_MANA] = 1; }
    ~FsArms() { heurarm::t_arm[heurarm::PLACEHOLDER_MANA] = saved; }
};

// A zone card exactly as play builds it (DeckLoader::MakePlaceholder: the name and its hash, nothing else).
Card PhFs(const std::string& name, int number)
{
    DefFs(name);
    Card c;
    c.m_name = CardDatabase::Instance().CanonicalName(name);
    c.RehashName();
    c.m_number = number;
    return c;
}

// `board` on our side (untapped, not summoning sick), `hand` as placeholders, land drop spent, turn 5.
GameState BoardFs(const std::vector<std::string>& board, const std::vector<std::string>& hand, int salt)
{
    EnsureCardsFs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20 + salt;
    s.players[1].life     = 40;
    int num = 1;
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefFs(n).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhFs(n, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhFs("Island", 900 + k)); }
    return s;
}

// A hand cast priced on `s`. Reality Spasm the way the enumerator emits it: X = every mana source (untap them
// all), Hinata's discount taking the X back off, so the bill is {U}{U}.
Action CastFs(const GameState& s, const std::string& name)
{
    const CardDefinition& d = DefFs(name);
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &d;
    a.card_mv   = d.card.m_mana_cost.ManaValue();
    if (d.params.untap_x_mana_sources)
    {
        const int x = ManaSourceCount(s);
        int pips = d.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
        ManaCost c = EffectiveSpellCost(d, s, 1);
        c.generic += x * pips;
        c.generic = std::max(0, c.generic - HinataGenericDiscount(d, s, x));
        a.cost         = c;
        a.chosen_x     = x;
        a.ritual_float = RitualFloatAmount(s, d, x);
    }
    else { a.cost = EffectiveSpellCost(d, s, 1); }
    a.is_noncreature = !d.card.IsCreature();
    return a;
}

int InHandFs(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

const char* kHinata = "Hinata, Dawn-Crowned";
const char* kSpasm  = "Reality Spasm";
const char* kBolt   = "Lightning Bolt";
// The red-heavy rest of a Hinata hand: by pip TOTAL these outweigh any number of Spasms.
const std::vector<std::string> kRedHand = { "Crackle with Power", "Irencrag Feat", "Soulfire Eruption" };

std::vector<std::string> HandWith(std::vector<std::string> v)
{
    v.insert(v.end(), kRedHand.begin(), kRedHand.end());
    return v;
}

}   // namespace

TEST_CASE("FloatCommitSeq: Reality Spasm's tap-ahead commits the Spasm's own {U}{U} first")
{
    // Hinata + Mountain + Mystic Monastery + Reflecting Pool. Spasm untaps all three (X = 3, free under Hinata):
    // the tap-ahead floats the Mountain's {R} and the two choice units, and the Spasm's {U}{U} is paid out of
    // that float with every source tapped. Hand: the second Spasm and the red rest of the deck. By the hand's
    // pip total both units go {R} (Feat + Crackle + Soulfire = 8 red against 2-4 blue) and the Spasm cannot pay;
    // in sequence the pending {U}{U} comes first.
    FsArms arms;
    GameState s = BoardFs({ kHinata, "Mountain", "Mystic Monastery", "Reflecting Pool" }, HandWith({ kSpasm, kSpasm }), 1);
    const std::vector<Action> line = { CastFs(s, kSpasm), CastFs(s, kSpasm) };
    REQUIRE(line[0].chosen_x == 3);
    REQUIRE(line[0].cost.blue == 2);
    REQUIRE(line[0].cost.ManaValue() == 2);
    LineSeqScope seq(FloatCommitSeqOn());
    if (seq.On()) { BuildLineSeq(seq.Seq(), line); }
    const std::string name = kSpasm;
    RitualTapAheadIntoFloat(s, line[0].chosen_x, &line[0].cost, &name);
    const ManaPool& f = s.floating_mana;
    CHECK(f.Total() == 3);
    CHECK(f.red >= 1);                           // the Mountain
    CHECK(f.blue + f.wild >= 2);                 // the Spasm's {U}{U} -- and nothing but the float can pay it
    CHECK(f.CanPay(line[0].cost));
    // With no line applying (no LineSeqScope -- real resolution off a plan, the d0 runner) nothing is known
    // about the rest of the turn and the commit keeps today's rule, the hand's pip total: {R} here.
    GameState s2 = BoardFs({ kHinata, "Mountain", "Mystic Monastery", "Reflecting Pool" }, HandWith({ kSpasm }), 5);
    {
        LineSeq* const bound = g_line_seq;   // step outside the line for this one call
        g_line_seq = nullptr;
        const Action sp = CastFs(s2, kSpasm);
        RitualTapAheadIntoFloat(s2, sp.chosen_x, &sp.cost, &name);
        g_line_seq = bound;
    }
    CHECK(s2.floating_mana.red == 3);
}

TEST_CASE("FloatCommitSeq: two Spasms' {U}{U} after a red spell -- the line applies in full, and the walk agrees")
{
    // The same board, the line Lightning Bolt (off the Mountain) -> Reality Spasm -> Reality Spasm. Each Spasm's
    // tap-ahead floats the two choice lands and pays {U}{U} out of them; the first one untaps the Mountain the Bolt
    // used as well, so the second floats its {R} too. The hand's pip total commits both lands {R} at EVERY
    // tap-ahead: neither Spasm can be paid, so the line the search priced collapses to the Bolt.
    FsArms arms;
    const std::vector<std::string> board = { kHinata, "Mountain", "Mystic Monastery", "Reflecting Pool" };
    {
        GameState s = BoardFs(board, HandWith({ kBolt, kSpasm, kSpasm }), 2);
        TurnSolver::Plan plan;
        plan.actions = { CastFs(s, kBolt), CastFs(s, kSpasm), CastFs(s, kSpasm) };
        plan.land_decided = true;
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        CHECK(InHandFs(s, kBolt) == 0);
        CHECK(InHandFs(s, kSpasm) == 0);          // both Spasms cast
        CHECK(s.players[1].life == 37);
    }
    {
        // ...and the enumeration's sequential walk (SubsetPayableSequential's body) prices the same order the
        // same way -- the commit runs inside it with the walk's own sequence bound (lockstep).
        const GameState s = BoardFs(board, HandWith({ kBolt, kSpasm, kSpasm }), 3);
        const std::vector<Action> acts = { CastFs(s, kBolt), CastFs(s, kSpasm), CastFs(s, kSpasm) };
        CHECK(TurnSolver::OrderPaysSequentially(s, acts, { 0, 1, 2 }));
    }
}

TEST_CASE("FloatCommitSeq: a later cast's scarce colour outranks the hand -- the Karoo float keeps the {W}")
{
    // Island + Mountain + Mountain + Mystic Monastery; Izzet Boilerworks has just entered and bounces the
    // untapped Monastery, which is tapped in response (FloatKarooBouncedLand). The line still owes Hinata
    // {1}{U}{R}{W}, and once the Monastery leaves no source on the board makes {W}: the float is the only
    // white the line will ever see. By the hand's total the unit is {R} (Crackle + Feat + Soulfire); first-listed
    // it would be {U}. In sequence it is the later cast's uncoverable pip: {W}.
    FsArms arms;
    GameState s = BoardFs({ "Island", "Mountain", "Mountain", "Mystic Monastery" }, HandWith({ kHinata }), 4);
    const std::vector<Action> line = { CastFs(s, kHinata) };
    LineSeqScope seq(FloatCommitSeqOn());
    if (seq.On()) { BuildLineSeq(seq.Seq(), line); }
    int mon = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_name.str() == "Mystic Monastery") { mon = i; } }
    REQUIRE(mon >= 0);
    REQUIRE(FloatKarooBouncedLand(s, 0, mon));
    CHECK(s.floating_mana.white == 1);
    CHECK(s.floating_mana.Total() == 1);
}
