// Maze's End APPLY-DROP family (mana-clean gate claude-queue-20261121a, 2026-11-24): THE RANKED OBJECT
// (MTG_RANK_OBJECT_COLORS, heurarm RANK_OBJECT_COLORS; RankObjectScope in ai/DecisionProvider.h).
//
// ManaSourceRank is keyed on the card DEFINITION, and an etb_choose_color land's definition carries the whole
// five-colour menu, so a CLB Gate locked to its printed colour + one chosen colour (a dual) ranked "rainbow" (50),
// past Gond Gate (its {C} plus the Gates' colours: 40 with three colours out). Seed 30006540 gi6540 T3, plan
// {land=Rakdos Guildgate, cast Sylvan Scrying}: Scrying's {G} was paid with Gond Gate instead of the Cliffgate
// locked to G; the continuation played Heap Gate (Gond now reflects every colour) and Guild Summit's {U} -- Gond's
// alone -- was gone. With the object's own colours Cliffgate ranks as the dual it is and pays the {G}.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

namespace
{
void LoadRoCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Permanent& Put(GameState& s, const char* name, int number, bool tapped = false)
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
    return s.battlefield.back();
}

void Hand(GameState& s, const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

bool Tapped(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p.tapped; } }
    FAIL("permanent not on the battlefield");
    return false;
}

// s30006540 T3 after the land drop: Cliffgate (R, chosen G), Gond Gate, Maze's End, Rakdos Guildgate (untapped
// under Gond), Burgeoning, Exploration; Guild Summit and Heap Gate in hand.
GameState Board(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Cliffgate", 16).chosen_color = static_cast<int>(Color::Green);
    Put(s, "Gond Gate", 28);
    Put(s, "Burgeoning", 11);
    Put(s, "Exploration", 19);
    Put(s, "Maze's End", 43);
    Put(s, "Rakdos Guildgate", 49);
    Hand(s, "Guild Summit", 33);
    Hand(s, "Heap Gate", 37);
    return s;
}

// Pay Sylvan Scrying's {1}{G}, then put Heap Gate in (untapped under Gond Gate) and pay Guild Summit's {2}{U}.
bool ScryingThenSummit(int arm, bool* gond_kept)
{
    heurarm::Clear();
    if (arm >= 0) { heurarm::t_arm[heurarm::RANK_OBJECT_COLORS] = static_cast<std::int8_t>(arm); }
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    ManaCost scry; scry.generic = 1; scry.green = 1;
    const bool paid = TapForCostDirect(s, scry, /*for_creature=*/false);
    *gond_kept = !Tapped(s, 28);
    Put(s, "Heap Gate", 37);
    ManaCost summit; summit.generic = 2; summit.blue = 1;
    const bool ok = paid && TapForCostDirect(s, summit, /*for_creature=*/false);
    heurarm::Clear();
    return ok;
}
}   // namespace

TEST_CASE("Maze's End rank object: a Cliffgate locked to G pays Scrying's {G}, Gond Gate stays up for Summit's {U}")
{
    LoadRoCards();
    bool gond_kept = false;
    CHECK(ScryingThenSummit(/*arm=*/1, &gond_kept));
    CHECK(gond_kept);
}

TEST_CASE("Maze's End rank object: OFF, the definition's five colours rank Cliffgate rainbow and Summit strands")
{
    LoadRoCards();
    bool gond_kept = true;
    CHECK_FALSE(ScryingThenSummit(/*arm=*/0, &gond_kept));
    CHECK_FALSE(gond_kept);
}

TEST_CASE("Maze's End rank object: the rank reads the object's colours only inside a RankObjectScope")
{
    LoadRoCards();
    static const MazesEndProvider kProv;
    GameState s = Board(kProv);
    const CardDefinition* cg = CardDatabase::Instance().Lookup("Cliffgate");
    REQUIRE(cg != nullptr);
    heurarm::Clear();
    const int def_rank = kProv.ManaSourceRank(s, *cg);          // no object: the definition's menu
    int obj_rank = 0;
    {
        const RankObjectScope ro(&s.battlefield[0]);
        obj_rank = kProv.ManaSourceRank(s, *cg);
    }
    CHECK(def_rank == 50);
    CHECK(obj_rank == 20);                                       // R or G: the dual rung
    CHECK(kProv.ManaSourceRank(s, *cg) == def_rank);             // the scope is gone
    // An UNLOCKED object (chosen_color -1) still reads the definition.
    s.battlefield[0].chosen_color = -1;
    {
        const RankObjectScope ro(&s.battlefield[0]);
        CHECK(kProv.ManaSourceRank(s, *cg) == def_rank);
    }
}

// ---- THE MAZE'S END HOLD-UP (MTG_MAZE_HOLD_UP, heurarm MAZE_HOLD_UP; DEFAULT ON) -------------------------------------
// s30006137 gi6137 T3 (gate claude-queue-20261121a, need-later APPLY-DROP): Sea Gate (U, chosen G), Izzet Guildgate,
// Exploration, Maze's End, Manor Gate (entered tapped), Baldur's Gate; Manabond in hand. The continuation's Sylvan
// Scrying {1}{G} was paid with Sea Gate's {G} + Maze's End, and Manabond's {G} then had nothing. Fed by Izzet
// Guildgate + Maze's End, Baldur's Gate makes {G}{G}{G} (three other Gates): Scrying and Manabond both pay.
namespace
{
GameState HoldBoard(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Sea Gate", 50).chosen_color = static_cast<int>(Color::Green);
    Put(s, "Izzet Guildgate", 38);
    Put(s, "Exploration", 19);
    Put(s, "Maze's End", 43);
    Put(s, "Manor Gate", 41, /*tapped=*/true).chosen_color = static_cast<int>(Color::Blue);
    Put(s, "Baldur's Gate", 7);
    Hand(s, "Golgari Guildgate", 27);
    Hand(s, "Gates Ablaze", 23);
    Hand(s, "Manabond", 39);
    return s;
}

bool ScryingThenManabond(int arm)
{
    heurarm::Clear();
    if (arm >= 0) { heurarm::t_arm[heurarm::MAZE_HOLD_UP] = static_cast<std::int8_t>(arm); }
    static const MazesEndProvider kProv;
    GameState s = HoldBoard(kProv);
    ManaCost scry; scry.generic = 1; scry.green = 1;
    const bool paid = TapForCostDirect(s, scry, /*for_creature=*/false);
    ManaCost bond; bond.green = 1;
    const bool ok = paid && TapForCostDirect(s, bond, /*for_creature=*/false);
    heurarm::Clear();
    return ok;
}
}   // namespace

TEST_CASE("Maze's End hold-up: ON (the default), Scrying's payment keeps Manabond's {G} (Baldur's Gate fed by Izzet + Maze's End)")
{
    LoadRoCards();
    CHECK(ScryingThenManabond(/*arm=*/1));
    CHECK(ScryingThenManabond(/*arm=*/-1));
    // ...and with the spell being paid for in scope (the cast site's SpellSubtypePayScope).
    const CardDefinition* scry_def = CardDatabase::Instance().Lookup("Sylvan Scrying");
    REQUIRE(scry_def != nullptr);
    heurarm::Clear();
    static const MazesEndProvider kProv;
    GameState s = HoldBoard(kProv);
    ManaCost scry; scry.generic = 1; scry.green = 1;
    bool paid = false;
    { const SpellSubtypePayScope paying(&scry_def->card); paid = TapForCostDirect(s, scry, /*for_creature=*/false); }
    ManaCost bond; bond.green = 1;
    CHECK((paid && TapForCostDirect(s, bond, /*for_creature=*/false)));
}

TEST_CASE("Maze's End hold-up: OFF (=0), the greedy spends Sea Gate's {G} and Manabond strands")
{
    LoadRoCards();
    CHECK_FALSE(ScryingThenManabond(/*arm=*/0));
}

TEST_CASE("Maze's End hold-up: the costs are the hand's spells, deduplicated, cheapest first, Gates Ablaze left out")
{
    LoadRoCards();
    static const MazesEndProvider kProv;
    GameState s = HoldBoard(kProv);
    Hand(s, "Guild Summit", 33);
    Hand(s, "Manabond", 40);
    heurarm::Clear();
    heurarm::t_arm[heurarm::MAZE_HOLD_UP] = 1;
    const std::vector<ManaCost> h = kProv.PaymentHoldUp(s, 0);
    heurarm::Clear();
    REQUIRE(h.size() == 2);
    CHECK(h[0].green == 1);
    CHECK(h[0].ManaValue() == 1);
    CHECK(h[1].blue == 1);
    CHECK(h[1].generic == 2);
    CHECK(kProv.PaymentHoldUp(s, 0).size() == 2);   // default ON
    heurarm::t_arm[heurarm::MAZE_HOLD_UP] = 0;
    CHECK(kProv.PaymentHoldUp(s, 0).empty());
    heurarm::Clear();
}

// ---- THE LIST'S COLOURS (MTG_MAZE_RANK_SPENDABLE, heurarm MAZE_RANK_SPENDABLE; DEFAULT ON with RANK_OBJECT_COLORS) ----
// 2HG s26200993 gi993 T4, the first Maze's End {3} (the other Maze's End held for its own activation, so it is left
// out here): Planar Nexus, Manor Gate (G, chosen U), Cliffgate (R, chosen G), Azorius Guildgate untapped. With the
// object colours alone Manor, Cliffgate and Azorius all rank 20 and the tie goes by battlefield order -- Cliffgate's
// {G} pays the generic, and once Gond Gate and Baldur's Gate are in, Manabond's {G} and Sylvan Scrying's {1}{G} cannot
// both be paid. Read as the list's colours (G/U/R) Azorius is mono-blue (10) and pays first.
namespace
{
bool CrackThenManabondScrying(int spend_arm, bool* azorius_paid_crack)
{
    heurarm::Clear();
    heurarm::t_arm[heurarm::RANK_OBJECT_COLORS] = 1;
    if (spend_arm >= 0) { heurarm::t_arm[heurarm::MAZE_RANK_SPENDABLE] = static_cast<std::int8_t>(spend_arm); }
    static const MazesEndProvider kProv;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 30;
    s.players[1].life     = 30;
    s.m_provider          = &kProv;
    s.deck_spell_pip_colors = static_cast<std::uint8_t>((1u << static_cast<int>(Color::Blue))
                                                        | (1u << static_cast<int>(Color::Red))
                                                        | (1u << static_cast<int>(Color::Green)));
    Put(s, "Selesnya Guildgate", 52, /*tapped=*/true);
    Put(s, "Manor Gate", 41).chosen_color = static_cast<int>(Color::Blue);
    Put(s, "Cliffgate", 16).chosen_color = static_cast<int>(Color::Green);
    Put(s, "Guild Summit", 33);
    Put(s, "Azorius Guildgate", 6);
    Put(s, "Planar Nexus", 48);
    Put(s, "Exploration", 20);
    ManaCost crack; crack.generic = 3;
    const bool c_ok = TapForCostDirect(s, crack, /*for_creature=*/false);
    *azorius_paid_crack = Tapped(s, 6) && !Tapped(s, 16);
    Put(s, "Baldur's Gate", 7);
    Put(s, "Gond Gate", 28);
    ManaCost bond; bond.green = 1;
    const bool b_ok = c_ok && TapForCostDirect(s, bond, /*for_creature=*/false);
    ManaCost scry; scry.generic = 1; scry.green = 1;
    const bool s_ok = b_ok && TapForCostDirect(s, scry, /*for_creature=*/false);
    heurarm::Clear();
    return s_ok;
}
}   // namespace

TEST_CASE("Maze's End list colours: Azorius Guildgate (W/U in a G/U/R list) pays the {3}, both {G} stay payable")
{
    LoadRoCards();
    bool az = false;
    CHECK(CrackThenManabondScrying(/*spend_arm=*/-1, &az));   // the default
    CHECK(az);
}

TEST_CASE("Maze's End list colours: OFF, the dual tie spends Cliffgate's {G} and Scrying's {G} strands")
{
    LoadRoCards();
    bool az = true;
    CHECK_FALSE(CrackThenManabondScrying(/*spend_arm=*/0, &az));
    CHECK_FALSE(az);
}

TEST_CASE("Maze's End list colours: the rank rewrites only the plain ladder of a plain coloured land")
{
    LoadRoCards();
    static const MazesEndProvider kProv;
    GameState s;
    s.active_player_index = 0;
    s.m_provider = &kProv;
    heurarm::Clear();
    const CardDefinition* az = CardDatabase::Instance().Lookup("Azorius Guildgate");
    const CardDefinition* oz = CardDatabase::Instance().Lookup("Orzhov Guildgate");
    const CardDefinition* me = CardDatabase::Instance().Lookup("Maze's End");
    REQUIRE(az != nullptr); REQUIRE(oz != nullptr); REQUIRE(me != nullptr);
    CHECK(kProv.ManaSourceRank(s, *az) == 20);                   // unstamped state: every colour counts
    s.deck_spell_pip_colors = static_cast<std::uint8_t>((1u << static_cast<int>(Color::Blue))
                                                        | (1u << static_cast<int>(Color::Red))
                                                        | (1u << static_cast<int>(Color::Green)));
    CHECK(kProv.ManaSourceRank(s, *az) == 10);                   // W is no flexibility here
    CHECK(kProv.ManaSourceRank(s, *oz) == 5);                    // W/B: a {C} source in this list
    CHECK(kProv.ManaSourceRank(s, *me) == 5);                    // the {C} tier untouched
    heurarm::t_arm[heurarm::RANK_OBJECT_COLORS] = 0;             // the base arm: the historical ladder
    CHECK(kProv.ManaSourceRank(s, *az) == 20);
    heurarm::t_arm[heurarm::RANK_OBJECT_COLORS] = 1;
    heurarm::t_arm[heurarm::MAZE_RANK_SPENDABLE] = 0;
    CHECK(kProv.ManaSourceRank(s, *az) == 20);
    heurarm::Clear();
}

// THE NEW-NAME SLOT NEVER TAKES TALON'S NAME (MAZE_RANK_SPENDABLE's scope). 2HG s26200993 gi993 T4, the SECOND cause:
// under the spend rank The Black Gate's {B} spends on nothing in this G/U/R list (rank 5), so Talon Gates (a filter
// land) won the new-name slot -- and Talon has its own slot (the P3 amendment), so Sylvan Scrying's menu collapsed to
// {Talon}. The searched tutor axis is SIZED on that state (one name: no index-1 variant) and READ at resolution, where
// Gond Gate was down, every Gate usable on entry, and the menu was {new name, Talon}: the base plan's Scrying took the
// new name, and the Talon find the end-step Maze's End {3} needs was never emitted. Skipping Talon in the new-name
// pick keeps the menu two names at both states.
namespace
{
void Lib(GameState& s, std::initializer_list<const char*> names, int base)
{
    int n = base;
    for (const char* nm : names)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        REQUIRE(d != nullptr);
        Card c = d->card;
        c.m_number = n++;
        s.players[0].library.push_back(c);
    }
}
GameState G993ScryingState(const MazesEndProvider& prov, bool gond_down)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.m_provider          = &prov;
    s.deck_spell_pip_colors = static_cast<std::uint8_t>((1u << static_cast<int>(Color::Blue))
                                                        | (1u << static_cast<int>(Color::Red))
                                                        | (1u << static_cast<int>(Color::Green)));
    Put(s, "Selesnya Guildgate", 52, true);
    Put(s, "Manor Gate", 41, true).chosen_color = static_cast<int>(Color::Blue);
    Put(s, "Manabond", 39);
    Put(s, "Maze's End", 44);
    Put(s, "Cliffgate", 16).chosen_color = static_cast<int>(Color::Green);
    Put(s, "Guild Summit", 33);
    Put(s, "Azorius Guildgate", 6, true);
    Put(s, "Planar Nexus", 48, true);
    Put(s, "Exploration", 20);
    Put(s, "Baldur's Gate", 7);
    if (gond_down) { Put(s, "Gond Gate", 28); } else { Hand(s, "Gond Gate", 28); }
    Hand(s, "Manor Gate", 42);
    Hand(s, "Maze's End", 43);
    Lib(s, { "Talon Gates of Madara", "The Black Gate", "Citadel Gate", "Heap Gate", "Basilisk Gate", "Gateway Plaza",
             "Rakdos Guildgate", "Simic Guildgate", "Black Dragon Gate", "Sea Gate", "Sea Gate", "Gond Gate" }, 600);
    return s;
}
}   // namespace

TEST_CASE("Maze's End list colours: the new-name slot skips Talon Gates, so Sylvan Scrying's menu stays two names")
{
    LoadRoCards();
    static const MazesEndProvider kProv;
    const CardDefinition* scry = CardDatabase::Instance().Lookup("Sylvan Scrying");
    REQUIRE(scry != nullptr);
    heurarm::Clear();
    for (const bool gond_down : { false, true })   // the axis's sizing state, then Scrying's resolution state
    {
        const GameState s = G993ScryingState(kProv, gond_down);
        const std::vector<std::string> m = kProv.TutorCandidates(s, 0, scry->params);
        INFO("gond_down=" << gond_down);
        REQUIRE(m.size() == 2);
        CHECK(m[0] != "Talon Gates of Madara");
        CHECK(m[1] == "Talon Gates of Madara");
    }
    // The base arm (RANK_OBJECT_COLORS off) is the historical menu: The Black Gate is the new name, then Talon.
    heurarm::t_arm[heurarm::RANK_OBJECT_COLORS] = 0;
    {
        const GameState s = G993ScryingState(kProv, /*gond_down=*/false);
        CHECK(kProv.TutorCandidates(s, 0, scry->params)
              == std::vector<std::string>{ "The Black Gate", "Talon Gates of Madara" });
    }
    heurarm::Clear();
}
