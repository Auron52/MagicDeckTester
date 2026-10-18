// Unit tests for the Vampires cluster C onboarding (2026-10-18): the CR 510.4 TWO-STEP combat damage
// model in the shared combat core, and the three cards that need it or ride beside it -- Drana,
// Liberator of Malakir; Stromkirk Captain; Mavren Fein, Dusk Apostle; Vampire Socialite.
//
// Why each group exists:
//
//  1. THE SPLIT. Combat damage used to be ONE summed step (double strike as power x2, first strike with
//     no reader). That is exact against a never-blocking opponent EXCEPT for whatever happens between
//     the steps -- Drana's counters, a first-striker's lifelink feeding Archangel of Thune. The split is
//     applied wherever it is valid (every combat with a first/double striker), so the tests pin both the
//     new between-step effects AND the single-step path that every other combat still takes.
//  2. LOCKSTEP. The executor (GameEngine::CombatPhase) and the rollout (TurnSolver::SimulateCombat)
//     share ResolveCombatDamage and FireAttackOneOrMoreTokens; the search's projection
//     (PendingAttackDamage) is a separate twin that must agree with the realised damage on Drana
//     boards -- an over-credit there is a phantom lethal (the fd-diverge class), and the Stromkirk
//     Captain case is exactly where a first-strike-blind addend would over-credit.
//  3. MAVREN. "One or more nontoken Vampires attack" is one trigger per combat, over DECLARED
//     attackers, never per opponent head; the token is untapped and NOT attacking.
//  4. SOCIALITE. An intervening-if ETB and a CR 614 as-enters counter, both keyed on "an opponent lost
//     life this turn"; the static must reach TOKENS (Mavren's, Keeper's), which enter via CreateToken.
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// A permanent placed directly (no enter cascade -- its own ETB does NOT fire), not summoning-sick
// unless asked.
int Put(GameState& s, const std::string& name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A permanent ENTERING through the universal cascade both worlds use (watchers, then its own ETB).
int Enter(GameState& s, const std::string& name, int number)
{
    const int idx = Put(s, name, number, /*sick=*/true);
    FireEtbWatchers(s, 0, idx);
    FireOwnEtbTriggers(s, 0, idx);
    return idx;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int Plus(const Permanent& p) { return PlusCountersOn(p); }

std::vector<int> Idx(const GameState& s, const std::vector<int>& nums)
{
    std::vector<int> out;
    for (int n : nums)
    {
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == n) { out.push_back(i); break; } }
    }
    return out;
}

// The realised damage of a declared attack, through the real combat core (the executor's call shape).
int Swing(GameState& s, const std::vector<int>& nums, std::vector<std::string>* descs = nullptr)
{
    const CombatDamageResult r = ResolveCombatDamage(s, Idx(s, nums), /*exalted_bonus=*/0,
                                                     /*collect_descs=*/descs != nullptr);
    if (descs) { *descs = r.attacker_descs; }
    return r.total_damage;
}

// The rollout's whole combat (declare via the provider's AttackWith, attack triggers, damage).
int RolloutDrop(GameState s)
{
    const int before = s.players[1].life;
    RolloutSimulateCombat(s);
    return before - s.players[1].life;
}

int Tokens(const GameState& s, const std::string& subtype)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && CardHasSubtype(p.card, subtype)) { ++n; } }
    return n;
}

struct ScopedHeads
{
    int saved;
    explicit ScopedHeads(int n) : saved(gamesetup::t_setup.opponent_heads) { gamesetup::t_setup.opponent_heads = n; }
    ~ScopedHeads() { gamesetup::t_setup.opponent_heads = saved; }
};

Decklist ListOf(const std::vector<std::string>& names)
{
    Decklist d;
    int n = 1;
    for (const std::string& nm : names)
    {
        Card c = Def(nm).card;
        c.m_number = n++;
        d.mainboard.push_back(c);
    }
    return d;
}

}   // namespace

// ================================================================================================
// 1. The first-strike oracle
// ================================================================================================

TEST_CASE("CreatureHasFirstStrike: printed keyword and every grant shape in the pool")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int cap = Put(s, "Stromkirk Captain", 1);
    const int soc = Put(s, "Vampire Socialite", 2);
    const int gob = Put(s, "Goblin Guide", 3);
    CHECK(CreatureHasFirstStrike(s.battlefield[cap], s));     // printed
    CHECK(CreatureHasFirstStrike(s.battlefield[soc], s));     // "Other Vampire creatures ... have first strike"
    CHECK_FALSE(CreatureHasFirstStrike(s.battlefield[gob], s));   // not a Vampire
    // A Vampire TOKEN is reached (the grant matches live subtypes, not a definition).
    CreateToken(s, 0, 1, 1, {"Vampire"}, "W", {"Lifelink"});
    CHECK(CreatureHasFirstStrike(s.battlefield.back(), s));
    // The opponent's Vampire is not ours.
    const int theirs = Put(s, "Vampire Socialite", 50, false, /*controller=*/1);
    CHECK_FALSE(CreatureHasFirstStrike(s.battlefield[theirs], s));
    // The BoardSources prefilter answers identically.
    const BoardSources bs = GatherBoardSources(s.battlefield, 0);
    CHECK(bs.fs.size() == 1);
    CHECK(CreatureHasFirstStrike(s.battlefield[soc], s, &bs.fs));
    CHECK_FALSE(CreatureHasFirstStrike(s.battlefield[gob], s, &bs.fs));

    // The pre-existing grants this work wired: Field Marshal (Soldiers), Neheb (Minotaurs), Striking
    // Sliver (Slivers), an attached first-strike Aura.
    GameState t = Fresh();
    Put(t, "Field Marshal", 10);
    const int sol = Put(t, "Thalia's Lieutenant", 11);       // Human Soldier
    Put(t, "Neheb, the Worthy", 12);
    const int mino = Put(t, "Boros Reckoner", 13);           // Minotaur Wizard
    Put(t, "Striking Sliver", 14);
    const int sliv = Put(t, "Muscle Sliver", 15);
    const int bear = Put(t, "Goblin Guide", 16);
    CHECK(CreatureHasFirstStrike(t.battlefield[sol], t));
    CHECK(CreatureHasFirstStrike(t.battlefield[mino], t));
    CHECK(CreatureHasFirstStrike(t.battlefield[sliv], t));
    CHECK_FALSE(CreatureHasFirstStrike(t.battlefield[bear], t));
    const int umbra = Put(t, "Hyena Umbra", 17);
    t.battlefield[umbra].aura_attached_to = 16;
    CHECK(CreatureHasFirstStrike(t.battlefield[bear], t));
    // Field Marshal's "Other": a lone Marshal has no first strike of its own.
    GameState u = Fresh();
    const int fm = Put(u, "Field Marshal", 20);
    CHECK_FALSE(CreatureHasFirstStrike(u.battlefield[fm], u));
}

// ================================================================================================
// 2. The two-step damage model
// ================================================================================================

TEST_CASE("Drana strikes first: her counters land before the regular step (CR 510.4)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Drana, Liberator of Malakir", 1);
    Put(s, "Vampire Socialite", 2);      // 2/2, no first strike
    Put(s, "Vampire Socialite", 3);
    std::vector<std::string> descs;
    GameState r = s;
    // Drana 2 in the first step; then +1/+1 on all three; then 3 + 3 in the regular step.
    CHECK(Swing(r, {1, 2, 3}, &descs) == 2 + 3 + 3);
    CHECK(r.players[1].life == 20 - 8);
    CHECK(Plus(ByNumber(r, 1)) == 1);
    CHECK(Plus(ByNumber(r, 2)) == 1);
    CHECK(Plus(ByNumber(r, 3)) == 1);
    CHECK(r.opponent_lost_life_this_turn);
    // The viewer line keeps ONE "Name (damage)" per attacker, in attack order (the reference-replay
    // attacker pin parses it).
    REQUIRE(descs.size() == 3);
    CHECK(descs[0] == "Drana, Liberator of Malakir (2)");
    CHECK(descs[1] == "Vampire Socialite (3)");
    CHECK(descs[2] == "Vampire Socialite (3)");
    // Lockstep: the projection and the rollout agree with the realised combat.
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 8);
    CHECK(RolloutDrop(s) == 8);
}

TEST_CASE("Drana's trigger emits one play event, in real play only")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Drana, Liberator of Malakir", 1);
    Put(s, "Vampire Socialite", 2);
    std::vector<PlayEvent> events;
    g_play_event_sink = &events;
    Swing(s, {1, 2});
    g_play_event_sink = nullptr;
    int drana = 0;
    for (const PlayEvent& e : events)
    { if (e.text.find("counter on each attacking creature") != std::string::npos) { ++drana; } }
    CHECK(drana == 1);
}

TEST_CASE("Stromkirk Captain: every Vampire strikes first, so Drana's counters add nothing this combat")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Drana, Liberator of Malakir", 1);
    Put(s, "Stromkirk Captain", 2);
    Put(s, "Vampire Socialite", 3);
    GameState r = s;
    // All in the first step: Drana 2+1 (Captain lord), Captain 2, Socialite 2+1.
    CHECK(Swing(r, {1, 2, 3}) == 3 + 2 + 3);
    CHECK(Plus(ByNumber(r, 1)) == 1);    // counters still land (they pay off next turn)
    CHECK(Plus(ByNumber(r, 2)) == 1);
    CHECK(Plus(ByNumber(r, 3)) == 1);
    // The projection's addend is 0 here -- a first-strike-blind addend would over-credit 3.
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 8);
    CHECK(RolloutDrop(s) == 8);

    // The next combat reads the counters (all three attack again, untapped).
    for (Permanent& p : r.battlefield) { p.tapped = false; }
    r.players[1].life = 20;
    CHECK(Swing(r, {1, 2, 3}) == 4 + 3 + 4);

    // A Captain that stays home still grants first strike: Drana 3 + Socialite 3, counters add 0.
    GameState h = Fresh();
    Put(h, "Drana, Liberator of Malakir", 1);
    Put(h, "Stromkirk Captain", 2, /*sick=*/true);
    Put(h, "Vampire Socialite", 3);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(h) == 6);
    CHECK(RolloutDrop(h) == 6);
}

TEST_CASE("Drana absent / not attacking, and Drana alone")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Drana, Liberator of Malakir", 1, /*sick=*/true);   // cannot attack
        Put(s, "Vampire Socialite", 2);
        Put(s, "Vampire Socialite", 3);
        CHECK(RolloutDrop(s) == 4);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 4);
        GameState r = s;
        Swing(r, {2, 3});
        CHECK(Plus(ByNumber(r, 2)) == 0);
        CHECK(Plus(ByNumber(r, 1)) == 0);
    }
    {
        GameState s = Fresh();
        Put(s, "Drana, Liberator of Malakir", 1);
        GameState r = s;
        CHECK(Swing(r, {1}) == 2);               // her own counter arrives after her only damage
        CHECK(Plus(ByNumber(r, 1)) == 1);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 2);
    }
}

TEST_CASE("Double strike is a hit in EACH step; Drana's counter raises its second hit")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Swiftblade Vindicator", 1);      // 1/1 double strike, vigilance
        CHECK(Swing(s, {1}) == 2);               // unchanged from the old x2 collapse
        CHECK_FALSE(ByNumber(s, 1).tapped);      // vigilance still read once
    }
    {
        GameState s = Fresh();
        Put(s, "Drana, Liberator of Malakir", 1);
        Put(s, "Swiftblade Vindicator", 2);
        GameState r = s;
        // First step: Drana 2, Vindicator 1. Counters. Regular step: Vindicator 2.
        CHECK(Swing(r, {1, 2}) == 2 + 1 + 2);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 5);
        CHECK(RolloutDrop(s) == 5);
    }
}

TEST_CASE("A double striker's lifelink is one life-gain EVENT per step, and the first one feeds the second")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int th = Put(s, "Archangel of Thune", 5);     // 3/4 lifelink: "whenever you gain life, +1/+1 counter on each creature you control"
    s.battlefield[th].temp_double_strike = true;
    // First step: 3 damage, gain 3 -> one counter wave. Regular step: 4 damage, gain 4 -> another.
    CHECK(Swing(s, {5}) == 3 + 4);
    CHECK(s.players[0].life == 20 + 3 + 4);
    CHECK(Plus(ByNumber(s, 5)) == 2);
}

TEST_CASE("Angels: Lyra's first-step lifelink pumps the regular step through Archangel of Thune")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Lyra Dawnbringer", 1);       // 5/5 flying, first strike, lifelink; other Angels +1/+1 and lifelink
    Put(s, "Archangel of Thune", 2);     // 3/4 -> 4/5 under Lyra
    GameState r = s;
    // First step: Lyra 5, gain 5 -> +1/+1 on both. Regular step: Thune 3 + 1 (Lyra) + 1 (counter) = 5.
    // The old single-step model dealt 5 + 4 = 9.
    CHECK(Swing(r, {1, 2}) == 5 + 5);
    CHECK(Plus(ByNumber(r, 1)) == 2);    // Lyra's gain wave + Thune's own gain wave
    CHECK(Plus(ByNumber(r, 2)) == 2);
    CHECK(RolloutDrop(s) == 10);
    // The projection does not price the between-step gain: a CONSERVATIVE under-credit (disclosed).
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) <= 10);
}

TEST_CASE("Critter: Ocelot Pride's first-step lifelink pumps Ajani's Pridemate before it deals damage")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Ocelot Pride", 1);           // 1/1 first strike, lifelink
    Put(s, "Ajani's Pridemate", 2);      // 2/2: "whenever you gain life, +1/+1 counter on this"
    CHECK(Swing(s, {1, 2}) == 1 + 3);
    CHECK(Plus(ByNumber(s, 2)) == 1);
}

TEST_CASE("Attackers tap at declaration: vigilance gained BETWEEN the steps does not untap one")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Ocelot Pride", 1);
    const int v = Put(s, "Voice of the Blessed", 2);   // vigilance at four +1/+1 counters
    AddPlusCounters(s.battlefield[v], 3);
    REQUIRE_FALSE(CreatureHasVigilance(s.battlefield[v], s));
    // First step: the Pride's lifelink -> Voice's 4th counter (flying + vigilance). Regular step: 2+4.
    CHECK(Swing(s, {1, 2}) == 1 + 6);
    CHECK(Plus(ByNumber(s, 2)) == 4);
    CHECK(CreatureHasVigilance(ByNumber(s, 2), s));
    CHECK(ByNumber(s, 2).tapped);                       // it was declared without vigilance
}

TEST_CASE("No first or double striker: the single-step path is unchanged")
{
    EnsureCardsLoaded();
    // The CR 510.2 deferral the Critter tests pin: Thune's gain does not pump this same step.
    GameState s = Fresh();
    Put(s, "Soul Warden", 1);
    Put(s, "Archangel of Thune", 5);
    Put(s, "Ajani's Pridemate", 10);
    std::vector<std::string> descs;
    CHECK(Swing(s, {1, 5, 10}, &descs) == 1 + 3 + 2);
    REQUIRE(descs.size() == 3);
    CHECK(descs[1] == "Archangel of Thune (3)");
    CHECK(Plus(ByNumber(s, 10)) == 2);
    // And a first striker WITHOUT any between-step effect deals exactly what the collapse did.
    GameState k = Fresh();
    Put(k, "Knight Exemplar", 1);        // 2/2 first strike; other Knights +1/+1
    Put(k, "Venerable Knight", 2);       // 2/1 Knight
    CHECK(Swing(k, {1, 2}) == 2 + 3);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(Fresh()) == 0);
}

// ================================================================================================
// 3. Mavren Fein
// ================================================================================================

TEST_CASE("Mavren Fein: one untapped, non-attacking 1/1 white Vampire lifelink token per combat")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Mavren Fein, Dusk Apostle", 1, /*sick=*/true);   // need not attack (player-scoped)
        Put(s, "Vampire Socialite", 2);
        std::vector<int> declared = Idx(s, {2});
        FireAttackOneOrMoreTokens(s, 0, declared);
        REQUIRE(Tokens(s, "Vampire") == 1);
        const Permanent& t = s.battlefield.back();
        CHECK(t.is_token);
        CHECK(t.entered_this_turn);
        CHECK_FALSE(t.tapped);
        CHECK(t.EffectivePower() == 1);
        CHECK(t.card.HasKeyword(Keyword::Lifelink));
        CHECK(t.card.HasColor(Color::White));
        CHECK(declared.size() == 1);                              // never joins the attack
    }
    {
        GameState s = Fresh();
        Put(s, "Mavren Fein, Dusk Apostle", 1);
        Put(s, "Vampire Socialite", 2);
        Put(s, "Vampire Socialite", 3);
        FireAttackOneOrMoreTokens(s, 0, Idx(s, {1, 2, 3}));
        CHECK(Tokens(s, "Vampire") == 1);                          // "one or more": one trigger
    }
    {
        GameState s = Fresh();
        Put(s, "Mavren Fein, Dusk Apostle", 1, /*sick=*/true);
        CreateToken(s, 0, 1, 1, {"Vampire"}, "W", {"Lifelink"});
        s.battlefield.back().entered_this_turn = false;
        const int tok = static_cast<int>(s.battlefield.size()) - 1;
        FireAttackOneOrMoreTokens(s, 0, {tok});
        CHECK(Tokens(s, "Vampire") == 1);                          // only a TOKEN attacked: nothing new
    }
    {
        GameState s = Fresh();
        Put(s, "Mavren Fein, Dusk Apostle", 1, /*sick=*/true);
        Put(s, "Goblin Guide", 2);
        FireAttackOneOrMoreTokens(s, 0, Idx(s, {2}));
        CHECK(Tokens(s, "Vampire") == 0);                          // not a Vampire
    }
    {
        ScopedHeads heads(2);
        GameState s = Fresh();
        Put(s, "Mavren Fein, Dusk Apostle", 1);
        FireAttackOneOrMoreTokens(s, 0, Idx(s, {1}));
        CHECK(Tokens(s, "Vampire") == 1);                          // never per opponent head
    }
}

TEST_CASE("Mavren Fein in the rollout's combat: the token is made but does not swing")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Mavren Fein, Dusk Apostle", 1);
    Put(s, "Vampire Socialite", 2);
    GameState r = s;
    RolloutSimulateCombat(r);
    CHECK(r.players[1].life == 20 - 2 - 2);
    CHECK(Tokens(r, "Vampire") == 1);
    CHECK_FALSE(r.battlefield.back().tapped);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 4);
}

// ================================================================================================
// 4. Vampire Socialite
// ================================================================================================

TEST_CASE("Socialite ETB: intervening-if on an opponent having lost life this turn")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Vampire Socialite", 1);
        Enter(s, "Vampire Socialite", 2);
        CHECK(Plus(ByNumber(s, 1)) == 0);                          // flag false: nothing
    }
    {
        GameState s = Fresh();
        s.opponent_lost_life_this_turn = true;
        Put(s, "Drana, Liberator of Malakir", 1);
        Put(s, "Goblin Guide", 2);                                 // not a Vampire
        Put(s, "Vampire Socialite", 3, false, /*controller=*/1);   // not ours
        CreateToken(s, 0, 1, 1, {"Vampire"}, "W", {"Lifelink"});   // a Vampire TOKEN
        const int tok = s.battlefield.back().card.m_number;
        Enter(s, "Vampire Socialite", 4);
        CHECK(Plus(ByNumber(s, 1)) == 1);
        CHECK(Plus(ByNumber(s, tok)) == 1 + 0);                    // the token: +1 from the ETB
        CHECK(Plus(ByNumber(s, 2)) == 0);
        CHECK(Plus(ByNumber(s, 3)) == 0);
        CHECK(Plus(ByNumber(s, 4)) == 0);                          // "each OTHER"
    }
    {
        // Thalia's Lieutenant's shared block is unchanged.
        GameState s = Fresh();
        s.opponent_lost_life_this_turn = true;
        Put(s, "Thalia's Lieutenant", 1);
        Enter(s, "Thalia's Lieutenant", 2);
        CHECK(Plus(ByNumber(s, 1)) == 1 + 1);   // the entrant's ETB + its own "another Human enters" watcher
    }
}

TEST_CASE("Socialite static: other Vampires (tokens included) enter with a counter while the flag holds")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        s.opponent_lost_life_this_turn = true;
        Put(s, "Vampire Socialite", 1);
        CreateToken(s, 0, 1, 1, {"Vampire"}, "W", {"Lifelink"});
        CHECK(Plus(s.battlefield.back()) == 1);                    // a 2/2
        CreateToken(s, 0, 1, 1, {"Cat"}, "W", {});
        CHECK(Plus(s.battlefield.back()) == 0);                    // not a Vampire
        Enter(s, "Vampire Socialite", 2);
        CHECK(Plus(ByNumber(s, 2)) == 1);                          // the first Socialite's static
    }
    {
        GameState s = Fresh();
        Put(s, "Vampire Socialite", 1);
        CreateToken(s, 0, 1, 1, {"Vampire"}, "W", {"Lifelink"});
        CHECK(Plus(s.battlefield.back()) == 0);                    // flag false: none
    }
    {
        // Mavren's token at declare-attackers: flag false -> 1/1; flag already set -> 2/2.
        GameState s = Fresh();
        Put(s, "Vampire Socialite", 1, /*sick=*/true);
        Put(s, "Mavren Fein, Dusk Apostle", 2);
        FireAttackOneOrMoreTokens(s, 0, Idx(s, {2}));
        CHECK(Plus(s.battlefield.back()) == 0);
        GameState t = Fresh();
        t.opponent_lost_life_this_turn = true;
        Put(t, "Vampire Socialite", 1, /*sick=*/true);
        Put(t, "Mavren Fein, Dusk Apostle", 2);
        FireAttackOneOrMoreTokens(t, 0, Idx(t, {2}));
        CHECK(Plus(t.battlefield.back()) == 1);
    }
}

TEST_CASE("Second main: Socialite and Mavren each turn it on by themselves")
{
    EnsureCardsLoaded();
    CHECK(GoldFishRunner::DeckUsesSecondMain(ListOf({"Swamp", "Vampire Socialite"})));
    CHECK(GoldFishRunner::DeckUsesSecondMain(ListOf({"Swamp", "Mavren Fein, Dusk Apostle"})));
    CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(ListOf({"Swamp", "Vampire of the Dire Moon",
                                                           "Drana, Liberator of Malakir",
                                                           "Stromkirk Captain"})));
}
