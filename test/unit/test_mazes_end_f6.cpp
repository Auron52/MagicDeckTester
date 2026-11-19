// Maze's End deck, F6 (2026-11-19; docs/design/analysis-Maze's End.md "F6"): the USER's viewer rejection
// Maze_s_End_cod_s6_gi5_t5 -- "Mana difficulties ... In theory this is workable due to lands coming into play
// untapped." Board: Baldur's Gate, Gond Gate, three other Gates, two Maze's End; Sylvan Scrying in hand. The line
// "activate Maze's End, then Sylvan Scrying + the second activation" is payable only if the FIRST {3} leaves
// Baldur's Gate (fed later at X = 5, after the fetched Gate entered untapped under Gond Gate) and the second Maze's
// End untapped. The generic ladder ranks both {C}-only sources 5 (spend first), so the first payment ate them and
// the line was unreachable at every budget. MazesEndProvider::ManaSourceRank pays them last.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

namespace
{
void LoadF6Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

bool Tapped(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p.tapped; } }
    FAIL("permanent not on the battlefield");
    return false;
}

// T5 of the rejection, Maze's End #43 already tapped for its own activation's {T}. Gates besides Baldur's: Gond,
// Simic, Selesnya, Gruul (X = 4 now, 5 once the fetch lands).
GameState Board(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 18;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Baldur's Gate", 7);
    Put(s, "Gond Gate", 29);
    Put(s, "Maze's End", 43, /*tapped=*/true);
    Put(s, "Maze's End", 46);
    Put(s, "Simic Guildgate", 53);
    Put(s, "Selesnya Guildgate", 50);
    Put(s, "Gruul Guildgate", 60);
    return s;
}
}   // namespace

TEST_CASE("Maze's End F6: the first {3} keeps Baldur's Gate and the other Maze's End untapped")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost three; three.generic = 3;
    REQUIRE(TapForCostDirect(s, three, /*for_creature=*/false));
    CHECK_FALSE(Tapped(s, 7));    // Baldur's Gate: its fed mode is worth X later
    CHECK_FALSE(Tapped(s, 46));   // Maze's End: its {T} is the second activation
}

TEST_CASE("Maze's End F6: after the fetched Gate enters untapped, Scrying + the second activation still pay")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost three; three.generic = 3;
    REQUIRE(TapForCostDirect(s, three, false));
    Put(s, "Azorius Guildgate", 70);   // Maze's End #43's fetch, untapped under Gond Gate (X -> 5)
    // Sylvan Scrying ({1}{G}) and the second activation's {3}, Maze's End #46 tapping itself: {4}{G} from what
    // the first payment left -- only Baldur's Gate's fed mode (X = 5) makes it.
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 46) { p.tapped = true; } }
    ManaCost rest; rest.generic = 4; rest.green = 1;
    CHECK(TapForCostDirect(s, rest, false));
}

TEST_CASE("Maze's End F6: the generic ladder (another provider) still spends the {C}-only sources first")
{
    LoadF6Cards();
    static const GenericProvider kGen;
    GameState s = Board(kGen);
    ManaCost three; three.generic = 3;
    REQUIRE(TapForCostDirect(s, three, false));
    // The pre-F6 order: at least one of the two {C}-only sources is spent on the first {3}.
    CHECK((Tapped(s, 7) || Tapped(s, 46)));
}

// ---- F6-2: Baldur's Gate's fed surplus floats (MTG_SCALED_FED_FLOAT; ManaPayment.cpp ScaledFedFloatAfter) ----------
// The rejection's exact T5 board (Baldur's Gate, Gond Gate, Sea Gate, Simic Guildgate, Thran Portal on Forest, Maze's
// End #43 / #46; Sylvan Scrying + Archdruid's Charm in hand) and the USER's line in the order the viewer validates
// it: activate Maze's End #43, activate #46, then Sylvan Scrying. 8 mana from 7 sources is payable only if the second
// {3} taps Baldur's Gate in its fed mode (X = 5 other Gates once the first fetch landed) and floats 2.
namespace
{
GameState S6Board(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 18;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Baldur's Gate", 7);
    Put(s, "Gond Gate", 29);
    Put(s, "Maze's End", 43);
    Put(s, "Maze's End", 46);
    Put(s, "Sea Gate", 50);
    Put(s, "Simic Guildgate", 53);
    Put(s, "Thran Portal", 60);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 60) { p.chosen_color = 4; } }   // Forest
    for (const char* n : { "Sylvan Scrying", "Archdruid's Charm" })
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE(d != nullptr);
        Card c = d->card; c.m_number = (n[0] == 'S') ? 57 : 5;
        s.players[0].hand.push_back(c);
    }
    return s;
}

void DumpS6(const GameState& s, const char* tag)
{
    std::string t;
    for (const Permanent& p : s.battlefield) { if (p.tapped) { t += " #" + std::to_string(p.card.m_number); } }
    const ManaPool& f = s.floating_mana;
    MESSAGE(tag << " tapped:" << t << " float W" << f.white << " U" << f.blue << " B" << f.black << " R" << f.red
                << " G" << f.green << " C" << f.colorless << " wild" << f.wild << " life " << s.players[0].life);
}

bool TapSelf(GameState& s, int num)
{
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number != num) { continue; }
        if (p.tapped) { return false; }
        p.tapped = true;
        return true;
    }
    return false;
}

// Activations first, then Scrying. Returns whether Scrying's {1}{G} paid.
// rank_arm: -1 = the F6 tap order as shipped, 0 = MTG_MAZE_TAP_BALDUR_LAST / MAZE_LAST both off.
bool S6ActsThenScrying(int fed_float_arm, int* float_after_second = nullptr, int rank_arm = -1)
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    heurarm::Clear();
    if (fed_float_arm >= 0) { heurarm::t_arm[heurarm::SCALED_FED_FLOAT] = static_cast<std::int8_t>(fed_float_arm); }
    if (rank_arm >= 0)
    {
        heurarm::t_arm[heurarm::MAZE_TAP_BALDUR_LAST] = static_cast<std::int8_t>(rank_arm);
        heurarm::t_arm[heurarm::MAZE_TAP_MAZE_LAST]   = static_cast<std::int8_t>(rank_arm);
    }
    GameState s = S6Board(kProv);
    ManaCost three; three.generic = 3;
    REQUIRE(TapSelf(s, 43));
    REQUIRE(TapForCostDirect(s, three, false));
    DumpS6(s, "after #43");
    Put(s, "Azorius Guildgate", 70);   // #43's fetch, untapped under Gond Gate
    if (!TapSelf(s, 46)) { DumpS6(s, "#46 spent on the first {3}"); heurarm::Clear(); return false; }
    REQUIRE(TapForCostDirect(s, three, false));
    DumpS6(s, "after #46");
    if (float_after_second != nullptr) { *float_after_second = s.floating_mana.Total(); }
    Put(s, "Izzet Guildgate", 71);     // #46's fetch
    ManaCost scry; scry.generic = 1; scry.green = 1;
    const bool ok = TapForCostDirect(s, scry, false);
    DumpS6(s, ok ? "Scrying paid" : "Scrying FAILED");
    heurarm::Clear();
    return ok;
}
}   // namespace

TEST_CASE("Maze's End F6-2: two activations then Sylvan Scrying pay -- the second {3} floats Baldur's Gate's surplus")
{
    int fl = -1;
    CHECK(S6ActsThenScrying(/*fed_float_arm=*/-1, &fl));
    CHECK(fl == 2);   // X = 5 for {2}: 5 - 2 fed - 1 plain
}

TEST_CASE("Maze's End F6-2: with MTG_SCALED_FED_FLOAT off the plain {C} tap strands Scrying (the hole it closes)")
{
    int fl = -1;
    CHECK_FALSE(S6ActsThenScrying(/*fed_float_arm=*/0, &fl));
    CHECK(fl == 0);
}

TEST_CASE("Maze's End F6-2: a bill below {feeder}+1 generic is untouched (Scrying off Baldur's Gate floats nothing)")
{
    LoadF6Cards();
    static const MazesEndProvider kProv;
    heurarm::Clear();
    GameState s = S6Board(kProv);
    for (Permanent& p : s.battlefield) { if (p.card.m_number != 7 && p.card.m_number != 29) { p.tapped = true; } }
    ManaCost scry; scry.generic = 1; scry.green = 1;
    REQUIRE(TapForCostDirect(s, scry, false));   // Gond {G} + Baldur {C}
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("Maze's End F6-2: DIAG the s6 line under the F6 ranks off")
{
    int fl = -1;
    MESSAGE("ranks off, fed on: " << S6ActsThenScrying(-1, &fl, 0) << " float " << fl);
}
