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
#include "core/EffectHandler.h"
#include "core/GameLogger.h"

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
    for (const char* other : {"Corsair Captain", "Kitesail Larcenist", "Siren Stormtamer", "Goblin Tomb Raider", "Staunch Crewmate"})
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
        CHECK(BuccaneerCasts(TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0))) <= 1);
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
    CHECK(both);          // offered -- but ONLY in the puts-last order (next test)
}

TEST_CASE("Daring Buccaneer: a Vial put + Buccaneer affordable only puts-last is offered puts-last, and resolves at {R}")
{
    // One Mountain. Puts-first the Corsair has left the hand -> Buccaneer is {2}{R}: unaffordable.
    // Puts-last (Plan::vial_after_casts) it reveals the Corsair still in hand -> {R}. The enumerator's
    // puts-last pricing retry must emit that plan, flagged, and never the puts-first one.
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(1, {"Daring Buccaneer", "Corsair Captain"});
    const int vi = Put(s, "Aether Vial", 0, 70);
    s.battlefield[vi].charge_counters = 3;
    const TurnSolver::Plan* pick = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (!(HasVialPut(p, "Corsair Captain") && BuccaneerCasts(p) > 0)) { continue; }
        CHECK(p.vial_after_casts);
        pick = &p;
    }
    REQUIRE(pick != nullptr);
    GameState after = s;
    TurnSolver::ApplyPlan(after, *pick, true);
    CHECK(CountNamed(after, "Daring Buccaneer") == 1);
    CHECK(CountNamed(after, "Corsair Captain") == 1);
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

// ---- Kitesail Larcenist: "for each player, choose up to one other target artifact or creature ...
// the chosen permanents become Treasure artifacts ... and lose all other abilities" ---------------

namespace
{

// Larcenist enters (cast semantics: etb_kx = the searched own-side pick, 0 = none; -1 = a PUT).
int LarcenistEnters(GameState& s, int number, int etb_kx)
{
    const int idx = Put(s, "Kitesail Larcenist", 0, number, /*sick=*/true);
    FireEtbWatchers(s, 0, idx);
    FireOwnEtbTriggers(s, 0, idx, std::string(), etb_kx);
    return idx;
}

int PutOpp(GameState& s, const std::string& name, int number)
{
    const int i = Put(s, name, 1, number);
    s.battlefield[i].owner_index = 1;
    return i;
}

}   // namespace

TEST_CASE("Kitesail Larcenist: converting your own tapped Aether Vial -> a TAPPED Treasure with no Vial behaviour")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    const int vi = Put(s, "Aether Vial", 0, 80);
    s.battlefield[vi].tapped          = true;
    s.battlefield[vi].charge_counters = 3;
    REQUIRE(CardDatabase::Instance().LookupCached(s.battlefield[vi].card) == &Def("Aether Vial"));
    LarcenistEnters(s, 81, /*etb_kx=*/80);
    const Permanent& t = ByNumber(s, 80);
    CHECK(t.card.m_name.str() == "Treasure Token");
    CHECK(t.tapped);                                          // tapped state carries over
    CHECK(t.card.HasType(CardType::Artifact));
    CHECK_FALSE(t.card.IsCreature());
    CHECK(CardHasSubtype(t.card, "Treasure"));
    const CardDefinition* d = CardDatabase::Instance().LookupCached(t.card);
    REQUIRE(d == &Def("Treasure Token"));                     // the memo was reset: no stale Vial def
    CHECK_FALSE(d->params.upkeep_adds_charge);
    CHECK(d->params.sac_for_mana_amount == 1);
    CHECK(CountTreasuresControlled(s, 0) == 1);
}

TEST_CASE("Kitesail Larcenist: a converted UNTAPPED Vial is no longer offered as a Vial")
{
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(1, {"Daring Buccaneer"});
    const int vi = Put(s, "Aether Vial", 0, 80);
    s.battlefield[vi].charge_counters = 1;
    auto offers_vial = [&](const GameState& g)
    {
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g, true))
        { if (HasVialPut(p, "Daring Buccaneer")) { return true; } }
        return false;
    };
    REQUIRE(offers_vial(s));
    LarcenistEnters(s, 81, 80);
    CHECK_FALSE(offers_vial(s));
}

TEST_CASE("Kitesail Larcenist: choosing none (chosen_x 0) leaves the board unchanged")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Aether Vial", 0, 80);
    Put(s, "Corsair Captain", 0, 10);
    PutOpp(s, "Corsair Captain", 500);                          // an opponent body: autonomous = none
    LarcenistEnters(s, 81, /*etb_kx=*/0);
    CHECK(ByNumber(s, 80).card.m_name.str() == "Aether Vial");
    CHECK(ByNumber(s, 10).card.m_name.str() == "Corsair Captain");
    CHECK(ByNumber(s, 500).card.m_name.str() == "Corsair Captain");
    CHECK(CountTreasuresControlled(s, 0) == 0);
    // A PUT (etb_kx -1) with the default provider also converts nothing, on either side.
    LarcenistEnters(s, 82, /*etb_kx=*/-1);
    CHECK(CountTreasuresControlled(s, 0) == 0);
    CHECK(CountTreasuresControlled(s, 1) == 0);
}

TEST_CASE("Kitesail Larcenist: a converted Treasure counts as an artifact for Goblin Tomb Raider")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Goblin Tomb Raider", 0, 40, /*sick=*/true);
    Put(s, "Dire Fleet Captain", 0, 41);
    CHECK_FALSE(CanAttackFull(ByNumber(s, 40), s.battlefield, 0));
    LarcenistEnters(s, 81, /*etb_kx=*/41);
    CHECK(ByNumber(s, 41).card.HasType(CardType::Artifact));
    CHECK(CanAttackFull(ByNumber(s, 40), s.battlefield, 0));
    CHECK(LivePower(s, 40) == 2);
}

TEST_CASE("Kitesail Larcenist: a converted Pirate stops counting for Corsair Captain (both directions)")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Corsair Captain", 0, 10);
    Put(s, "Dire Fleet Captain", 0, 11);
    Put(s, "Staunch Crewmate", 0, 12);
    CHECK(LivePower(s, 11) == 3);                               // 2/2 + Corsair's +1/+1
    // Convert the LORD: its "other Pirates get +1/+1" is gone with its definition.
    LarcenistEnters(s, 81, /*etb_kx=*/10);
    CHECK(LivePower(s, 11) == 2);
    CHECK_FALSE(ByNumber(s, 10).card.IsCreature());
    CHECK_FALSE(CardHasSubtype(ByNumber(s, 10).card, "Pirate"));
    // Convert a buffed Pirate under a second lord: it is no longer a creature or a Pirate.
    Put(s, "Corsair Captain", 0, 13);
    LarcenistEnters(s, 82, /*etb_kx=*/12);
    const Permanent& c = ByNumber(s, 12);
    CHECK_FALSE(c.card.IsCreature());
    CHECK_FALSE(CardHasSubtype(c.card, "Pirate"));
    CHECK_FALSE(c.card.m_power.has_value());
    CHECK(LivePower(s, 11) == 3);                               // the new Corsair still buffs Dire Fleet
}

TEST_CASE("Kitesail Larcenist: the human treasurify chooser covers the opponent's side")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    PutOpp(s, "Dire Fleet Captain", 500);
    std::vector<int> seen_sides;
    BounceChooser pick_first = [&](const GameState&, int side, const std::string&,
                                   const std::vector<int>& legal, int heur) -> int
    {
        seen_sides.push_back(side);
        CHECK(heur == -1);                                      // the provider's default is none
        return side == 1 && !legal.empty() ? 0 : -1;
    };
    g_play_treasurify_chooser = &pick_first;
    Put(s, "Aether Vial", 0, 80);
    LarcenistEnters(s, 81, /*etb_kx=*/0);   // own side carried by the cast (none) -> no own prompt
    g_play_treasurify_chooser = nullptr;
    REQUIRE(seen_sides.size() == 1);
    CHECK(seen_sides[0] == 1);
    CHECK(ByNumber(s, 500).card.m_name.str() == "Treasure Token");
    CHECK(ByNumber(s, 80).card.m_name.str() == "Aether Vial");
}

TEST_CASE("Kitesail Larcenist: the enumerator offers none + each own artifact/creature, never an existing Treasure")
{
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(3, {"Kitesail Larcenist"});
    for (auto& p : s.battlefield) { p.card = Def("Island").card; p.card.m_number = 300 + (&p - &s.battlefield[0]); }
    Put(s, "Aether Vial", 0, 80);
    Put(s, "Dire Fleet Captain", 0, 41);
    CreateTreasureTokens(s, 0, 1);
    s.battlefield.back().entered_this_turn = false;
    const int treasure_num = s.battlefield.back().card.m_number;
    std::vector<int> xs;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Kitesail Larcenist"
                && std::find(xs.begin(), xs.end(), a.chosen_x) == xs.end())
            { xs.push_back(a.chosen_x); }
        }
    }
    CHECK(std::find(xs.begin(), xs.end(), 0) != xs.end());
    CHECK(std::find(xs.begin(), xs.end(), 80) != xs.end());
    CHECK(std::find(xs.begin(), xs.end(), 41) != xs.end());
    CHECK(std::find(xs.begin(), xs.end(), treasure_num) == xs.end());
    CHECK(std::find(xs.begin(), xs.end(), kEtbKxHeuristic) == xs.end());   // human-play only
}

TEST_CASE("Kitesail Larcenist: the executor carries the searched target (and none) to resolution")
{
    EnsureCardsLoaded();
    for (int pick : {0, 80})
    {
        CAPTURE(pick);
        GameState s = BuccaneerBoard(0, {});
        for (int k = 0; k < 3; ++k) { Put(s, "Island", 0, 300 + k); }
        Put(s, "Aether Vial", 0, 80);
        s.players[0].hand.push_back(HandCard("Kitesail Larcenist", 81));
        AIEngine eng;
        ManaPool avail = AvailableManaPool(s);
        MtgTestSeam::CastSpellFromHand(eng, s, s.players[0].hand.back(), avail, pick);
        REQUIRE(s.stack.size() == 1);
        const StackEntry entry = s.stack.back();
        s.stack.pop_back();
        REQUIRE(entry.chosen_x.has_value());
        CHECK(*entry.chosen_x == pick);
        EffectHandler::Resolve(s, entry, Def("Kitesail Larcenist"));
        CHECK((ByNumber(s, 80).card.m_name.str() == "Treasure Token") == (pick == 80));
    }
}

// ---- Claude-play sweep fixes (2026-09-26) ------------------------------------------------------

TEST_CASE("Vial-put Staunch Crewmate: Mimic's as-enters counter is on it BEFORE its ETB dig resolves")
{
    // CR 614 (enters-with counters is a replacement on the entering event) precedes CR 603.6a
    // (the "when this enters" dig trigger). The hand-cast path always fired the enter cascade first;
    // the Vial-put path dug first. Observed through the dig chooser, which sees the board AT the dig.
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Enter(s, "Metallic Mimic", 0, 1);
    const int vi = Put(s, "Aether Vial", 0, 2);
    s.battlefield[vi].charge_counters = 2;
    s.players[0].hand.push_back(HandCard("Staunch Crewmate", 3));
    int counters_at_dig = -1;
    DigChooser chooser = [&](const GameState& st, int, const std::string&, const std::vector<Card>&,
                             const std::vector<int>&, int heuristic_pick) -> int
    {
        for (const Permanent& p : st.battlefield)
        { if (p.card.m_number == 3) { counters_at_dig = PlusCounters(p); } }
        return heuristic_pick;
    };
    DigChooser* saved = g_play_dig_chooser;
    g_play_dig_chooser = &chooser;
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action a;
    a.kind      = Action::Kind::ActivateVial;
    a.card_name = "Staunch Crewmate";
    plan.actions.push_back(a);
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    g_play_dig_chooser = saved;
    CHECK(counters_at_dig == 1);
    CHECK(PlusCounters(ByNumber(s, 3)) == 1);
    CHECK(s.players[0].hand.size() == 1);   // the dig took a Pirate
}

TEST_CASE("Forerunner of the Coalition: the human menu's named decline leaves the library untouched")
{
    EnsureCardsLoaded();
    GameState s = PiratesState();
    std::vector<int> before;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { before.push_back(s.players[0].library[i].m_number); }
    PerformTutor(s, 0, Def("Forerunner of the Coalition").params, kTutorDeclineTarget,
                 "Forerunner of the Coalition");
    REQUIRE(s.players[0].library.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
    { CHECK(s.players[0].library[i].m_number == before[i]); }   // no fetch, no shuffle
    // A named target still fetches to the top (the decline is the sentinel alone).
    PerformTutor(s, 0, Def("Forerunner of the Coalition").params, "Corsair Captain",
                 "Forerunner of the Coalition");
    CHECK(s.players[0].library.front().m_name.str() == "Corsair Captain");
}

TEST_CASE("Forerunner of the Coalition: the declined cast resolves identically in the rollout apply")
{
    EnsureCardsLoaded();
    GameState s = BuccaneerBoard(0, {"Forerunner of the Coalition"});
    for (int k = 0; k < 3; ++k) { Put(s, "Blackcleave Cliffs", 0, 300 + k); }
    s.players[0].library = Library{};
    for (const char* n : {"Mountain", "Corsair Captain", "Dire Fleet Captain", "Mountain"})
    { s.players[0].library.push_back(Placeholder(n, 900 + static_cast<int>(s.players[0].library.size()))); }
    std::vector<int> before;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { before.push_back(s.players[0].library[i].m_number); }
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action a;
    a.kind         = Action::Kind::CastFromHand;
    a.card_name    = "Forerunner of the Coalition";
    a.def          = &Def("Forerunner of the Coalition");
    a.tutor_target = kTutorDeclineTarget;
    plan.actions.push_back(a);
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    CHECK(CountNamed(s, "Forerunner of the Coalition") == 1);
    REQUIRE(s.players[0].library.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
    { CHECK(s.players[0].library[i].m_number == before[i]); }
}

TEST_CASE("Forerunner of the Coalition: autonomous enumeration never offers the named decline")
{
    // The decline plan is HUMAN-PLAY only (the search's decline is the index axis, tutor_choice).
    EnsureCardsLoaded();
    GameState s = PiratesState();
    for (int k = 0; k < 3; ++k) { Put(s, "Blackcleave Cliffs", 0, 300 + k); }
    s.players[0].hand.push_back(HandCard("Forerunner of the Coalition", 81));
    bool cast_seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Forerunner of the Coalition") { continue; }
            cast_seen = true;
            CHECK(a.tutor_target.str() != kTutorDeclineTarget);
        }
    }
    CHECK(cast_seen);
}

TEST_CASE("Forerunner drain lethal in a main phase is a win the rollout apply sees at once")
{
    // Sweep finding E: the executor lets a main-1 drain to 0 continue into combat (0 -> -17 in the
    // log) and records the win after combat -- the SAME turn. What must hold for the search is that
    // the main-phase drain itself reads as lethal right after the plan apply (the wins_this_turn /
    // OpponentHasLost checks that follow every ApplyPlanDirect), which this pins for a Vial put.
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Put(s, "Forerunner of the Coalition", 0, 1);
    const int vi = Put(s, "Aether Vial", 0, 2);
    s.battlefield[vi].charge_counters = 2;
    s.players[0].hand.push_back(HandCard("Dire Fleet Captain", 3));
    s.players[1].life = 1;
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action a;
    a.kind      = Action::Kind::ActivateVial;
    a.card_name = "Dire Fleet Captain";
    plan.actions.push_back(a);
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    CHECK(s.players[1].life <= 0);
    CHECK(OpponentHasLost(s));
}

// ---- Vial-put order (Plan::vial_after_casts) ---------------------------------------------------

namespace
{

TurnSolver::Plan CastAndVial(const std::string& cast, const std::string& vial, bool after)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action c;
    c.kind      = Action::Kind::CastFromHand;
    c.card_name = cast;
    c.def       = &Def(cast);
    plan.actions.push_back(c);
    Action v;
    v.kind      = Action::Kind::ActivateVial;
    v.card_name = vial;
    plan.actions.push_back(v);
    plan.vial_after_casts = after;
    return plan;
}

int UntappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Vial order: cast Metallic Mimic THEN Vial-put a Pirate gives the Pirate Mimic's counter")
{
    EnsureCardsLoaded();
    for (bool after : {false, true})
    {
        CAPTURE(after);
        GameState s = PiratesState();
        for (int k = 0; k < 2; ++k) { Put(s, "Mountain", 0, 300 + k); }
        const int vi = Put(s, "Aether Vial", 0, 2);
        s.battlefield[vi].charge_counters = 2;
        s.players[0].hand.push_back(HandCard("Metallic Mimic", 3));
        s.players[0].hand.push_back(HandCard("Dire Fleet Captain", 4));
        const TurnSolver::Plan plan = CastAndVial("Metallic Mimic", "Dire Fleet Captain", after);
        CHECK(TurnSolver::VialOrderMatters(plan));
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        REQUIRE(CountNamed(s, "Metallic Mimic") == 1);
        REQUIRE(CountNamed(s, "Dire Fleet Captain") == 1);
        CHECK(PlusCounters(ByNumber(s, 4)) == (after ? 1 : 0));
    }
}

TEST_CASE("Vial order: a Daring Buccaneer cast before the Vial put still reveals the Pirate")
{
    // Hand Buccaneer + Corsair Captain, Vial at 3. Puts-first: Corsair leaves the hand, Buccaneer
    // has nothing to reveal and costs {2}{R}. Casts-first: it reveals the Corsair and costs {R}.
    EnsureCardsLoaded();
    for (bool after : {false, true})
    {
        CAPTURE(after);
        GameState s = BuccaneerBoard(3, {"Daring Buccaneer", "Corsair Captain"});
        const int vi = Put(s, "Aether Vial", 0, 70);
        s.battlefield[vi].charge_counters = 3;   // Corsair Captain is MV 3
        const TurnSolver::Plan plan = CastAndVial("Daring Buccaneer", "Corsair Captain", after);
        TurnSolver::ApplyPlan(s, plan, true);
        REQUIRE(CountNamed(s, "Daring Buccaneer") == 1);
        REQUIRE(CountNamed(s, "Corsair Captain") == 1);
        CHECK(UntappedLands(s) == (after ? 2 : 0));
    }
}

TEST_CASE("Vial order: the enumerator offers the casts-first variant only where the order can matter")
{
    EnsureCardsLoaded();
    auto variants = [](const GameState& s)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (!p.vial_after_casts) { continue; }
            ++n;
            CHECK(TurnSolver::VialOrderMatters(p));
        }
        return n;
    };
    {
        GameState s = PiratesState();
        for (int k = 0; k < 2; ++k) { Put(s, "Mountain", 0, 300 + k); }
        const int vi = Put(s, "Aether Vial", 0, 2);
        s.battlefield[vi].charge_counters = 2;
        s.players[0].hand.push_back(HandCard("Metallic Mimic", 3));
        s.players[0].hand.push_back(HandCard("Dire Fleet Captain", 4));
        CHECK(variants(s) > 0);
    }
    {
        // No entering-effect source among the casts: Vial-put Dire Fleet Captain + cast Goblin Tomb
        // Raider resolves identically either way -> no variant (and no other deck ever gets one).
        GameState s = PiratesState();
        for (int k = 0; k < 2; ++k) { Put(s, "Mountain", 0, 300 + k); }
        const int vi = Put(s, "Aether Vial", 0, 2);
        s.battlefield[vi].charge_counters = 2;
        s.players[0].hand.push_back(HandCard("Goblin Tomb Raider", 3));
        s.players[0].hand.push_back(HandCard("Dire Fleet Captain", 4));
        CHECK(variants(s) == 0);
    }
}

TEST_CASE("Vial order: a searched_order plan with the Vial put deferred puts it after EVERY cast")
{
    // "Metallic Mimic, Siren Stormtamer, then Staunch Crewmate (vial)": with a Mimic already on the
    // board the Crewmate enters with TWO counters (one per Mimic), and the explicit cast order holds.
    EnsureCardsLoaded();
    GameState s = PiratesState();
    Enter(s, "Metallic Mimic", 0, 1);
    for (int k = 0; k < 3; ++k) { Put(s, "Spirebluff Canal", 0, 300 + k); }
    const int vi = Put(s, "Aether Vial", 0, 2);
    s.battlefield[vi].charge_counters = 2;
    s.players[0].hand.push_back(HandCard("Metallic Mimic", 3));
    s.players[0].hand.push_back(HandCard("Siren Stormtamer", 4));
    s.players[0].hand.push_back(HandCard("Staunch Crewmate", 5));
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const char* n : {"Metallic Mimic", "Siren Stormtamer"})
    {
        Action c;
        c.kind = Action::Kind::CastFromHand; c.card_name = n; c.def = &Def(n);
        plan.actions.push_back(c);
    }
    Action v;
    v.kind = Action::Kind::ActivateVial; v.card_name = "Staunch Crewmate";
    plan.actions.push_back(v);
    plan.searched_order   = true;
    plan.vial_after_casts = true;
    CHECK(TurnSolver::VialOrderMatters(plan));
    TurnSolver::ApplyPlan(s, plan, true);
    CHECK(PlusCounters(ByNumber(s, 5)) == 2);   // Crewmate: both Mimics
    CHECK(PlusCounters(ByNumber(s, 4)) == 2);   // Stormtamer: both Mimics (cast after the 2nd Mimic)
}
