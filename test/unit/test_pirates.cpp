// Unit tests for the Pirates onboarding (2026-09-26). They pin what a game log cannot show: the
// chosen-type machinery (Metallic Mimic / Adaptive Automaton ARE Pirates on the battlefield and NOT in
// hand/library), the ordering of Mimic's CR 614 replacement against the entrant's own type choice,
// and the per-card params the Pirates cards introduced. Every enter is routed through the SAME
// universal cascade (FireEtbWatchers) both worlds use.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/SubtypeRegistry.h"
#include "core/HeuristicDefaults.h"
#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"

#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// A name-only zone card, the way DeckLoader::MakePlaceholder builds library/hand cards.
Card Placeholder(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int Put(GameState& s, const std::string& name, int controller, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int Enter(GameState& s, const std::string& name, int controller, int number)
{
    const int idx = Put(s, name, controller, number, /*sick=*/true);
    FireEtbWatchers(s, controller, idx);
    return idx;
}

int PlusCounters(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

Permanent& ByNumber(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int LivePower(GameState& s, int number)
{
    const Permanent& p = ByNumber(s, number);
    return p.EffectivePower() + ComputeLordBonus(p.card, s, p.controller_index, p.is_animated, &p).first;
}
int LiveToughness(GameState& s, int number)
{
    const Permanent& p = ByNumber(s, number);
    return p.EffectiveToughness() + ComputeLordBonus(p.card, s, p.controller_index, p.is_animated, &p).second;
}

int CountNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// A Pirates-shaped deck in the library so DominantCreatureSubtypeId picks Pirate (the choice is
// deck-derived, exactly as in a real game).
GameState PiratesState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    int num = 100;
    for (const char* n : {"Corsair Captain", "Dire Fleet Captain", "Goblin Tomb Raider",
                          "Staunch Crewmate", "Siren Stormtamer", "Forerunner of the Coalition"})
    { for (int k = 0; k < 3; ++k) { s.players[0].library.push_back(Placeholder(n, num++)); } }
    return s;
}

uint16_t PirateId() { return SubtypeRegistry::Instance().Id("Pirate"); }

}   // namespace

TEST_CASE("Metallic Mimic: chooses the deck's tribe and IS that type on the battlefield only")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Enter(s, "Metallic Mimic", 0, 1);
    const Permanent& m = ByNumber(s, 1);
    CHECK(m.chosen_subtype_id == PirateId());
    CHECK(CardHasSubtype(m.card, "Pirate"));
    CHECK(CardHasSubtype(m.card, "Shapeshifter"));
    // The printed definition is untouched: in hand/library Mimic is only a Shapeshifter.
    CHECK_FALSE(CardHasSubtype(Def("Metallic Mimic").card, "Pirate"));
    CHECK_FALSE(CardMatchesTypeName(ZoneCard(Placeholder("Metallic Mimic", 50)), "Pirate"));
    CHECK(CardMatchesTypeName(ZoneCard(Placeholder("Metallic Mimic", 50)), "Artifact"));
    CHECK_FALSE(CardMatchesTypeName(ZoneCard(Placeholder("Adaptive Automaton", 51)), "Pirate"));
    // Not a cost reducer (the chooses_creature_type trap).
    CHECK_FALSE(Def("Metallic Mimic").params.chooses_creature_type);
}

TEST_CASE("Metallic Mimic: a Pirate entering afterwards gets +1/+1; Mimic's own entry gets nothing")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Enter(s, "Metallic Mimic", 0, 1);
    CHECK(PlusCounters(ByNumber(s, 1)) == 0);
    Enter(s, "Dire Fleet Captain", 0, 2);
    CHECK(PlusCounters(ByNumber(s, 2)) == 1);
    // A second Mimic naming Pirate is a Pirate AS it enters (CR 614.12) -> the first Mimic's counter.
    Enter(s, "Metallic Mimic", 0, 3);
    CHECK(PlusCounters(ByNumber(s, 3)) == 1);
    CHECK(PlusCounters(ByNumber(s, 1)) == 0);
}

TEST_CASE("Corsair Captain's lord reaches a Mimic that chose Pirate; Captains buff each other")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Corsair Captain", 0, 10);
    Enter(s, "Metallic Mimic", 0, 1);                   // 2/1 + Corsair +1/+1
    CHECK(LivePower(s, 1) == 3);
    CHECK(LiveToughness(s, 1) == 2);
    CHECK(LivePower(s, 10) == 2);                       // "Other" -- no self-buff
    Put(s, "Corsair Captain", 0, 11);
    CHECK(LivePower(s, 10) == 3);
    CHECK(LivePower(s, 1) == 4);
}

TEST_CASE("Adaptive Automaton x2 + Corsair Captain: every body is a 4/4")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Corsair Captain", 0, 10);
    Enter(s, "Adaptive Automaton", 0, 1);
    Enter(s, "Adaptive Automaton", 0, 2);
    CHECK(ByNumber(s, 1).chosen_subtype_id == PirateId());
    for (int n : {1, 2, 10})
    {
        CHECK(LivePower(s, n) == 4);
        CHECK(LiveToughness(s, n) == 4);
    }
}

TEST_CASE("Forerunner of the Coalition: another Pirate entering drains 1 -- a chosen-Pirate Mimic included")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    const int opp0 = s.players[1].life;
    const int me0  = s.players[0].life;
    Enter(s, "Forerunner of the Coalition", 0, 20);     // its own entry is not "another"
    CHECK(s.players[1].life == opp0);
    Enter(s, "Metallic Mimic", 0, 1);                   // Pirate by choice -> drains
    CHECK(s.players[1].life == opp0 - 1);
    Enter(s, "Corsair Captain", 0, 2);
    CHECK(s.players[1].life == opp0 - 2);
    CHECK(s.players[0].life == me0);                    // life LOSS on the opponent only
    CHECK(s.opponent_lost_life_this_turn);
}

TEST_CASE("Forerunner does not drain on a non-Pirate entering (the subtype filter)")
{
    EnsureCardsLoaded();
    GameState s;                                        // no Pirate deck: Mimic picks its own type
    s.active_player_index = 0;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    const int opp0 = s.players[1].life;
    Put(s, "Forerunner of the Coalition", 0, 20);
    Enter(s, "Adaptive Automaton", 0, 1);               // deck of nothing -> not a Pirate
    CHECK_FALSE(CardHasSubtype(ByNumber(s, 1).card, "Pirate"));
    CHECK(s.players[1].life == opp0);
}

TEST_CASE("Dire Fleet Captain counts a chosen-Pirate Mimic among the other attackers")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    const int df = Put(s, "Dire Fleet Captain", 0, 30);
    Enter(s, "Metallic Mimic", 0, 1);
    int mimic = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == 1) { mimic = i; } }
    const int adapt = Put(s, "Adaptive Automaton", 0, 2);   // PUT, not entered: no chosen type
    ApplyAttackSelfPumps(s, 0, std::vector<int>{df, mimic, adapt});
    CHECK(s.battlefield[df].temp_power_bonus == 1);     // Mimic counts, the un-chosen Automaton doesn't
    CHECK(s.battlefield[df].temp_tough_bonus == 1);
}

TEST_CASE("Goblin Tomb Raider: +1/+0 and haste only while an artifact is controlled")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Goblin Tomb Raider", 0, 40, /*sick=*/true);
    {
        const Permanent& r = ByNumber(s, 40);
        CHECK_FALSE(CanAttackFull(r, s.battlefield, 0));
        CHECK(LivePower(s, 40) == 1);
    }
    CreateTreasureTokens(s, 0, 1);                      // any artifact counts, a Treasure included
    {
        const Permanent& r = ByNumber(s, 40);
        CHECK(CanAttackFull(r, s.battlefield, 0));
        CHECK(CanTapNow(r, s.battlefield));
        CHECK(LivePower(s, 40) == 2);
        CHECK(LiveToughness(s, 40) == 2);
    }
    // The projection-only durable test ignores the Treasure (a plan may sacrifice it first).
    CHECK_FALSE(HasConditionalSelfHaste(ByNumber(s, 40).card, s.battlefield, 0, /*durable_only=*/true));
    Put(s, "Aether Vial", 0, 41);
    CHECK(HasConditionalSelfHaste(ByNumber(s, 40).card, s.battlefield, 0, /*durable_only=*/true));
}

TEST_CASE("Malcolm, the Eyes: investigates on the SECOND spell of the turn only")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Malcolm, the Eyes", 0, 50);
    const CardDefinition& bolt = Def("Lightning Bolt");
    s.spells_cast_this_turn = 1;
    FireOnCastTriggers(s, bolt);
    CHECK(CountNamed(s, "Clue Token") == 0);
    s.spells_cast_this_turn = 2;
    FireOnCastTriggers(s, bolt);
    CHECK(CountNamed(s, "Clue Token") == 1);
    s.spells_cast_this_turn = 3;
    FireOnCastTriggers(s, bolt);
    CHECK(CountNamed(s, "Clue Token") == 1);
}

TEST_CASE("Corsair Captain's own ETB creates a Treasure")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    const int idx = Put(s, "Corsair Captain", 0, 60, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, idx);
    CHECK(CountNamed(s, "Treasure Token") == 1);
}

TEST_CASE("Staunch Crewmate digs an ARTIFACT (type filter) as well as a Pirate")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    for (const char* n : {"Mountain", "Aether Vial", "Mountain", "Mountain", "Mountain"})
    { s.players[0].library.push_back(Placeholder(n, static_cast<int>(s.players[0].library.size()) + 1)); }
    const bool took = PerformEtbDig(s, 0, Def("Staunch Crewmate").params, nullptr);
    CHECK(took);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand[0].m_name.str() == "Aether Vial");
    CHECK(s.players[0].library.size() == 4);
}

// ---- Daring Buccaneer: "reveal a Pirate card from your hand or pay {2}" ------------------------

namespace
{

Card HandCard(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

// Mountains on the battlefield + the given hand, pre-combat on turn 4.
GameState BuccaneerBoard(int mountains, const std::vector<std::string>& hand)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    int num = 200;
    for (int k = 0; k < mountains; ++k) { Put(s, "Mountain", 0, num++); }
    for (const std::string& n : hand) { s.players[0].hand.push_back(HandCard(n, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(Placeholder("Mountain", 900 + k)); }
    return s;
}

int BuccaneerCasts(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Daring Buccaneer") { ++n; } }
    return n;
}

bool HasVialPut(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::ActivateVial && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Daring Buccaneer: a lone Buccaneer pays {2}; a second copy in hand IS a reveal")
{
    EnsureCardsLoaded();
    const ManaCost lone = EffectiveSpellCost(Def("Daring Buccaneer"), BuccaneerBoard(0, {"Daring Buccaneer"}), 1);
    CHECK(lone.ManaValue() == 3);
    CHECK(lone.red == 1);
    CHECK(lone.generic == 2);
    // Two in hand: the first is {R} (it reveals the other); once it has left the hand, the second
    // has nothing to reveal and costs {2}{R}. Total {R} + {2}{R}, not {R}{R}.
    GameState two = BuccaneerBoard(0, {"Daring Buccaneer", "Daring Buccaneer"});
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), two, 1).ManaValue() == 1);
    two.players[0].hand.erase(two.players[0].hand.begin());
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), two, 1).ManaValue() == 3);
}

TEST_CASE("Daring Buccaneer: another Pirate card in hand makes it cost {R}")
{
    EnsureCardsLoaded();
    for (const char* other : {"Corsair Captain", "Siren Stormtamer", "Goblin Tomb Raider", "Staunch Crewmate"})
    {
        CAPTURE(other);
        CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), BuccaneerBoard(0, {"Daring Buccaneer", other}), 1).ManaValue() == 1);
    }
    // A staged (exiled-playable) Pirate is not "in your hand".
    GameState st = BuccaneerBoard(0, {"Daring Buccaneer", "Corsair Captain"});
    st.players[0].hand.back().m_is_staged = true;
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), st, 1).ManaValue() == 3);
}

TEST_CASE("Daring Buccaneer: Metallic Mimic / Adaptive Automaton in hand do NOT enable the reveal")
{
    EnsureCardsLoaded();
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), BuccaneerBoard(0, {"Daring Buccaneer", "Metallic Mimic"}), 1).ManaValue() == 3);
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), BuccaneerBoard(0, {"Daring Buccaneer", "Adaptive Automaton"}), 1).ManaValue() == 3);
    // ... even with a chosen-Pirate Mimic on the BATTLEFIELD (that is not a card in hand).
    GameState s = BuccaneerBoard(0, {"Daring Buccaneer"});
    for (const char* n : {"Corsair Captain", "Dire Fleet Captain", "Goblin Tomb Raider"})
    { for (int k = 0; k < 3; ++k) { s.players[0].library.push_back(Placeholder(n, 950 + k)); } }
    Enter(s, "Metallic Mimic", 0, 60);
    REQUIRE(CardHasSubtype(ByNumber(s, 60).card, "Pirate"));
    CHECK(EffectiveSpellCost(Def("Daring Buccaneer"), s, 1).ManaValue() == 3);
}

TEST_CASE("Daring Buccaneer: the enumerator never offers the unaffordable two-Buccaneer subset")
{
    EnsureCardsLoaded();
    {
        // Two Mountains: {R} + {2}{R} = 4 > 2. One Buccaneer is fine; both together are not.
        const GameState s = BuccaneerBoard(2, {"Daring Buccaneer", "Daring Buccaneer"});
        int max_casts = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        { max_casts = std::max(max_casts, BuccaneerCasts(p)); }
        CHECK(max_casts == 1);
        CHECK(BuccaneerCasts(TurnSolver::Solve(s, true)) <= 1);
    }
    {
        // Four Mountains pay both.
        const GameState s = BuccaneerBoard(4, {"Daring Buccaneer", "Daring Buccaneer"});
        int max_casts = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        { max_casts = std::max(max_casts, BuccaneerCasts(p)); }
        CHECK(max_casts == 2);
    }
}

TEST_CASE("Daring Buccaneer: Vial-putting the only other Pirate leaves the Buccaneer at {2}{R}")
{
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(1, {"Daring Buccaneer", "Corsair Captain"});
    const int vi = Put(s, "Aether Vial", 0, 70);
    s.battlefield[vi].charge_counters = 3;
    bool bucc_alone = false, vial_alone = false, both = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const bool v = HasVialPut(p, "Corsair Captain");
        const int  b = BuccaneerCasts(p);
        if (v && b > 0)       { both = true; }
        else if (b > 0)       { bucc_alone = true; }    // reveals the Corsair still in hand: {R}
        else if (v)           { vial_alone = true; }
    }
    CHECK(bucc_alone);
    CHECK(vial_alone);
    CHECK_FALSE(both);    // the Corsair has left the hand -> {2}{R} on one Mountain
}

TEST_CASE("Daring Buccaneer: the executor charges the second lone Buccaneer {2}{R}")
{
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(4, {"Daring Buccaneer", "Daring Buccaneer"});
    AIEngine eng;
    for (int k = 0; k < 2; ++k)
    {
        ManaPool avail = AvailableManaPool(s);
        MtgTestSeam::CastSpellFromHand(eng, s, s.players[0].hand.front(), avail, 0);
    }
    CHECK(s.stack.size() == 2);
    int tapped = 0;
    for (const Permanent& p : s.battlefield) { if (p.tapped) { ++tapped; } }
    CHECK(tapped == 4);   // {R} + {2}{R}
}
