// Maze's End deck, F6 (2026-11-19; docs/design/analysis-Maze's End.md "F6"): the USER's viewer rejection
// Maze_s_End_cod_s6_gi5_t5 -- "Mana difficulties ... In theory this is workable due to lands coming into play
// untapped." Board: Baldur's Gate, Gond Gate, three other Gates, two Maze's End; Sylvan Scrying in hand. The line
// "activate Maze's End, then Sylvan Scrying + the second activation" is payable only if the FIRST {3} leaves
// Baldur's Gate (fed later at X = 5, after the fetched Gate entered untapped under Gond Gate) and the second Maze's
// End untapped. The generic ladder ranks both {C}-only sources 5 (spend first), so the first payment ate them and
// the line was unreachable at every budget. MazesEndProvider::ManaSourceRank pays them last.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
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
