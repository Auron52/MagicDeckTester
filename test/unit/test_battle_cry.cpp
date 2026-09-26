// Unit tests for BATTLE CRY (CR 702.92) and the flat-vs-per-opponent token count, added with the
// WhiteKnights onboarding (2026-09-25). Accorder Paladin and Hero of Bladehold.
//
// Every one of these pins something that is silently wrong in a way that still compiles, still
// runs, and still produces a plausible win rate -- which is why they are unit tests and not a
// sign test on an A/B:
//
//  1. It FIRES AT ALL. A new mechanism that builds but never fires produces digests identical to
//     the baseline, and "identical" reads as "proven neutral" rather than "proven dead" (this
//     repo's digest-equality-can-mean-BROKEN lesson). One attacking Accorder Paladin must make a
//     Venerable Knight a 3/1, not a 2/1.
//  2. "each OTHER": a battle-cry creature never pumps ITSELF. Implemented as (bc_total - own)
//     rather than a nested loop, so the self-exclusion is arithmetic and has to be checked at
//     K = 1 (no pump at all when attacking alone) AND K >= 2 (still exactly one short of the
//     total). A nested-loop implementation and a forgotten `- own` both pass the K = 1 case.
//  3. The SOURCE must itself be attacking, and only the controller's attackers are pumped. This is
//     the difference from Kragma Warcaller (attack_pump_matching_power), whose source need NOT be
//     attacking and which does not self-exclude -- the two mechanics look interchangeable and are
//     not, which is why battle cry got its own param.
//  4. TOKENS PUT ONTO THE BATTLEFIELD ATTACKING ARE PUMPED. Hero of Bladehold's two Soldiers and
//     Adeline's Human arrive from a simultaneous attack trigger; CR 603.3b lets the controller
//     order them, and tokens-first is strictly dominant against an opponent that never blocks.
//     Getting the call order backwards costs Hero exactly 2 damage a swing and raises no flag.
//  5. The token count's OPPONENT-COUNT SCALING is per-card, read from the card's own oracle.
//     Adeline says "for each opponent, create a ... token" -> scales. Hero says "create two
//     ... tokens" -> FLAT. The multiply used to be unconditional, which is inert at one head and
//     doubles Hero in 2HG -- a format this repo ships (angels2hg / knights2hg / melira2hg). Only
//     a test that actually sets opponent_heads = 2 can see it.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
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

int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

// Replays the declare-attackers sequence BOTH worlds run (GameEngine::CombatPhase and
// TurnSolver::SimulateCombat): attack-trigger tokens enter tapped-and-attacking and JOIN the
// attacker list, and only then do the attack pumps apply. The order is the thing under test in
// case 4, so the test must go through it rather than calling ApplyBattleCry on a hand-built list.
std::vector<int> DeclareAndPump(GameState& s, int controller, std::vector<int> atk_idx)
{
    if (!atk_idx.empty())
    {
        // atk_idx is passed exactly as both real call sites pass it -- a creature-scoped token
        // trigger reads it to decide whether its own source was declared.
        const int tok_start = FireAttackCreateTokens(s, controller, &atk_idx);
        for (int i = tok_start; i < static_cast<int>(s.battlefield.size()); ++i)
        { atk_idx.push_back(i); }
    }
    ApplyAttackSelfPumps(s, controller, atk_idx);
    ApplyBattleCry(s, controller, atk_idx);
    return atk_idx;
}

// Counts only actual TOKENS of this subtype. The `is_token` test is load-bearing rather than
// pedantic: Adeline's token is a Human and so are Adeline herself, Venerable Knight and every other
// Human Knight in the deck, so a plain subtype count silently folds real cards into the tally.
int TokensCreated(const GameState& s, const std::string& subtype)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, subtype)) { ++n; } }
    return n;
}

// A scoped 2HG switch. gamesetup::t_setup is thread_local and doctest runs a case on one thread,
// but restore it anyway -- leaking opponent_heads into a later case would corrupt a passing test
// into a passing test that measures nothing.
struct ScopedHeads
{
    int saved;
    explicit ScopedHeads(int n) : saved(gamesetup::t_setup.opponent_heads)
    { gamesetup::t_setup.opponent_heads = n; }
    ~ScopedHeads() { gamesetup::t_setup.opponent_heads = saved; }
};

}   // namespace

TEST_CASE("battle cry FIRES: an attacking Accorder Paladin pumps the other attackers")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pal = Put(s, "Accorder Paladin", 0, 1);    // 3/1, battle cry
    const int vk  = Put(s, "Venerable Knight", 0, 2);    // 2/1, no battle cry

    DeclareAndPump(s, 0, {pal, vk});

    CHECK(ByNumber(s, 2).EffectivePower() == 3);         // 2/1 -> 3/1: the pump landed
    CHECK(ByNumber(s, 1).EffectivePower() == 3);         // "each OTHER": the Paladin is unchanged
    // And the toughness is untouched -- battle cry is +1/+0, not +1/+1.
    CHECK(ByNumber(s, 2).EffectiveToughness() == 1);
}

TEST_CASE("battle cry: attacking ALONE pumps nothing (the self-exclusion at K=1)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pal = Put(s, "Accorder Paladin", 0, 1);

    DeclareAndPump(s, 0, {pal});

    CHECK(ByNumber(s, 1).EffectivePower() == 3);         // still a plain 3/1
}

TEST_CASE("battle cry: copies CROSS-PUMP and STACK -- each attacker gets total minus its own")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int p1 = Put(s, "Accorder Paladin", 0, 1);
    const int p2 = Put(s, "Accorder Paladin", 0, 2);
    const int vk = Put(s, "Venerable Knight", 0, 3);

    DeclareAndPump(s, 0, {p1, p2, vk});

    // bc_total = 2. Each Paladin gets 2 - 1 = 1; the Knight gets the full 2.
    CHECK(ByNumber(s, 1).EffectivePower() == 4);         // 3/1 -> 4/1
    CHECK(ByNumber(s, 2).EffectivePower() == 4);
    CHECK(ByNumber(s, 3).EffectivePower() == 4);         // 2/1 -> 4/1

    // The team total is K*(A-1) above printed = 2*(3-1) = 4, NOT K = 2. A forgotten cross-pump
    // reads 3/1 + 3/1 + 4/1 here and is off by only 2, which no win rate would ever localise.
    const int printed = 3 + 3 + 2;
    const int live    = ByNumber(s, 1).EffectivePower() + ByNumber(s, 2).EffectivePower()
                      + ByNumber(s, 3).EffectivePower();
    CHECK(live - printed == 4);
}

TEST_CASE("battle cry: the SOURCE must be attacking, and only the controller's attackers are pumped")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pal_home = Put(s, "Accorder Paladin", 0, 1);   // stays home -> no trigger
    const int vk       = Put(s, "Venerable Knight", 0, 2);
    Put(s, "Venerable Knight", 0, 3);                        // ours, but not attacking
    Put(s, "Accorder Paladin", 1, 4);                        // the OPPONENT's, irrelevant
    (void)pal_home;

    DeclareAndPump(s, 0, {vk});                              // only the Knight is declared

    CHECK(ByNumber(s, 2).EffectivePower() == 2);              // no source attacked -> no pump
    CHECK(ByNumber(s, 3).EffectivePower() == 2);              // a non-attacker is never pumped
    CHECK(ByNumber(s, 1).EffectivePower() == 3);

    // Now send the Paladin too: same board, one more attacker, and the pump appears. This is the
    // paired half of the check -- without it, "no pump" is indistinguishable from a dead code path.
    GameState s2 = Fresh();
    const int pal2 = Put(s2, "Accorder Paladin", 0, 1);
    const int vk2  = Put(s2, "Venerable Knight", 0, 2);
    Put(s2, "Venerable Knight", 0, 3);
    DeclareAndPump(s2, 0, {pal2, vk2});
    CHECK(ByNumber(s2, 2).EffectivePower() == 3);             // attacked -> pumped
    CHECK(ByNumber(s2, 3).EffectivePower() == 2);             // stayed home -> not pumped
}

TEST_CASE("Hero of Bladehold: the two Soldier tokens enter attacking AND receive the battle cry")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int hero = Put(s, "Hero of Bladehold", 0, 1);       // 3/4, battle cry + two Soldiers

    const std::vector<int> atk = DeclareAndPump(s, 0, {hero});

    CHECK(TokensCreated(s, "Soldier") == 2);
    CHECK(static_cast<int>(atk.size()) == 3);                 // Hero + both tokens are attacking
    CHECK(ByNumber(s, 1).EffectivePower() == 3);              // "each other" -> Hero unchanged

    // Both Soldiers are 1/1 printed and must be 2/1 here. If ApplyBattleCry ran BEFORE
    // FireAttackCreateTokens they would be 1/1 and Hero would quietly deal 2 less every swing.
    int pumped = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (!CardHasSubtype(p.card, "Soldier")) { continue; }
        CHECK(p.tapped);                                      // "tapped and attacking"
        CHECK(p.EffectivePower() == 2);
        ++pumped;
    }
    CHECK(pumped == 2);
}

TEST_CASE("Hero of Bladehold makes NO tokens when Hero itself does not attack")
{
    EnsureCardsLoaded();

    // THE REGRESSION TEST FOR THE ONE BUG THE UNIT TESTS MISSED. Hero's oracle is "Whenever THIS
    // CREATURE attacks", a CREATURE-scoped trigger, so a Hero that is not a declared attacker (the
    // turn it lands, summoning-sick) creates nothing. The original implementation reused Adeline's
    // param family, whose trigger is PLAYER-scoped ("whenever YOU attack"), and so fired on any
    // attack at all -- two attacking Soldiers plus their battle-cry pump, a full turn early, in 9 of
    // 40 measured games.
    //
    // Why every earlier case here was blind to it: they all put Hero INTO the attacker list. The
    // one untested branch was the only broken one. Found by the Stage-5d claude-play sweep.
    GameState s = Fresh();
    Put(s, "Hero of Bladehold", 0, 1);                  // on the battlefield but staying home
    const int vk = Put(s, "Venerable Knight", 0, 2);

    const std::vector<int> atk = DeclareAndPump(s, 0, {vk});   // only the Knight is declared

    CHECK(TokensCreated(s, "Soldier") == 0);            // the bug produced 2
    CHECK(static_cast<int>(atk.size()) == 1);           // and no extra attackers
    CHECK(ByNumber(s, 2).EffectivePower() == 2);        // no battle cry either: Hero did not attack

    // Paired half: the SAME board with Hero declared makes its two Soldiers. Without this, "0
    // tokens" is indistinguishable from a token path that is simply dead.
    GameState s2 = Fresh();
    const int hero = Put(s2, "Hero of Bladehold", 0, 1);
    const int vk2  = Put(s2, "Venerable Knight", 0, 2);
    DeclareAndPump(s2, 0, {hero, vk2});
    CHECK(TokensCreated(s2, "Soldier") == 2);
}

TEST_CASE("Adeline's trigger is PLAYER-scoped: she makes her token without attacking herself")
{
    EnsureCardsLoaded();

    // The other side of the distinction, and the reason the fix needed a new param rather than a
    // blanket gate. Adeline reads "Whenever YOU attack, for each opponent, create ..." -- she need
    // not be a declared attacker. Gating her the way Hero is gated would silently delete her token
    // from every turn she stays home, so this test is what stops the fix over-applying.
    GameState s = Fresh();
    Put(s, "Adeline, Resplendent Cathar", 0, 1);        // stays home
    const int vk = Put(s, "Venerable Knight", 0, 2);

    DeclareAndPump(s, 0, {vk});

    CHECK(TokensCreated(s, "Human") == 1);              // her token is made even so
}

TEST_CASE("Hero of Bladehold's token count is FLAT; Adeline's scales per opponent (2HG)")
{
    EnsureCardsLoaded();

    // One head (normal play): Hero makes 2, Adeline makes 1. This is the arm that was already
    // right, and it is here so the 2HG arm below cannot be read as a change in the base case.
    {
        REQUIRE(gamesetup::OpponentHeads() == 1);
        GameState s = Fresh();
        const int hero = Put(s, "Hero of Bladehold", 0, 1);
        DeclareAndPump(s, 0, {hero});
        CHECK(TokensCreated(s, "Soldier") == 2);

        GameState a = Fresh();
        const int adeline = Put(a, "Adeline, Resplendent Cathar", 0, 1);
        DeclareAndPump(a, 0, {adeline});
        CHECK(TokensCreated(a, "Human") == 1);   // one token at one head
    }

    // Two heads: Adeline's "for each opponent" doubles to 2 tokens. Hero's flat "create two"
    // must NOT become four.
    {
        ScopedHeads heads(2);
        REQUIRE(gamesetup::OpponentHeads() == 2);

        GameState s = Fresh();
        const int hero = Put(s, "Hero of Bladehold", 0, 1);
        DeclareAndPump(s, 0, {hero});
        CHECK(TokensCreated(s, "Soldier") == 2);              // FLAT -- the bug would read 4

        GameState a = Fresh();
        const int adeline = Put(a, "Adeline, Resplendent Cathar", 0, 1);
        DeclareAndPump(a, 0, {adeline});
        CHECK(TokensCreated(a, "Human") == 2);                // two tokens: hers SCALES with heads
    }

    // And the switch is restored, so a later case in this binary is not silently running 2HG.
    CHECK(gamesetup::OpponentHeads() == 1);
}

TEST_CASE("Hero of Bladehold under a Knight lord: the anthem and the battle cry both apply")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Knight Exemplar gives OTHER Knights +1/+1. Hero and Accorder Paladin are both Human Knights;
    // the Soldier tokens are not, so they get the battle cry only. This pins that the pump is a
    // temp_power_bonus on the PERMANENT and composes with the lord layer rather than replacing it.
    Put(s, "Knight Exemplar", 0, 9);
    const int hero = Put(s, "Hero of Bladehold", 0, 1);
    const int pal  = Put(s, "Accorder Paladin", 0, 2);

    DeclareAndPump(s, 0, {hero, pal});

    // bc_total = 2 (Hero + Paladin). Each gets 2 - 1 = 1 from battle cry; the lord's +1/+1 is a
    // continuous effect read separately, so check the two layers independently.
    const Permanent& h = ByNumber(s, 1);
    const Permanent& p = ByNumber(s, 2);
    CHECK(h.EffectivePower() == 4);                                    // 3 printed + 1 battle cry
    CHECK(p.EffectivePower() == 4);                                    // 3 printed + 1 battle cry
    CHECK(ComputeLordBonus(h.card, s, 0, false, &h).first == 1);       // + the Exemplar anthem
    CHECK(ComputeLordBonus(p.card, s, 0, false, &p).first == 1);

    // The Soldiers are not Knights: battle cry yes (2 of it), lord no.
    for (const Permanent& t : s.battlefield)
    {
        if (!CardHasSubtype(t.card, "Soldier")) { continue; }
        CHECK(t.EffectivePower() == 3);                                // 1 printed + 2 battle cry
        CHECK(ComputeLordBonus(t.card, s, 0, false, &t).first == 0);
    }
}
