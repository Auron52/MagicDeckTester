// Unit tests for the two SelesnyaLifegain payoffs whose semantics are easy to get silently wrong
// (2026-09-30). Both ride the shared lifegain machinery, so both are tested against the same
// GainLife hook the executor and the rollout share.
//
// NYKTHOS PARAGON -- "Whenever you gain life, you may put that many +1/+1 counters on each creature
// you control. Do this only once each turn."
//  1. "THAT MANY" is the EVENT's amount, not a fixed 1: one gain of 5 is +5/+5 on the team.
//  2. "ONLY ONCE EACH TURN" IS PER PERMANENT, NOT PER NAME (Scryfall ruling 2). This is the whole
//     reason the deck runs four, and it is THE failure mode: if the flag were read per name, four
//     Paragons on one gain of 5 would give +5/+5 instead of +20/+20.
//  3. Once a copy has been used it NO LONGER TRIGGERS (ruling 3) -- a second gain in the same turn
//     adds nothing, and the turn boundary re-arms every copy.
//  4. DECLINING DOES NOT CONSUME THE USE (ruling 1): spend 0 on a 1-life gain and the full team
//     wave is still available to a 10-life gain later in the SAME turn. This is what makes the
//     "you may" a real decision rather than a dominated one.
//  5. The provider's spend COUNT is honoured, and the per-event collapse spends copies in
//     battlefield-index order so executor and rollout agree.
//
// BLOSSOMING BOGBEAST -- "Whenever this creature attacks, you gain 2 life. Then creatures you
// control gain trample and get +X/+X until end of turn, where X is the amount of life you gained
// this turn."
//  6. The gain is a REAL life-gain event (it bumps life_gained_this_turn AND fires the watchers),
//     and X is read AFTER it ("Then" = oracle order), so a bare Bogbeast pumps for 2, not 0.
//  7. X counts the turn's OTHER gains too, not just this card's 2.
//  8. THE GAIN LANDS BEFORE COMBAT DAMAGE, so the counters it triggers grow the very swing they
//     were meant to grow -- the load-bearing ordering claim for this card.
//  9. Two attacking copies are two separate triggers: (+2, pump L+2) then (+2, pump L+4).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/Combat.h"

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

int Put(GameState& s, const std::string& name, int controller, int number, bool sick = false)
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

int PlusCounters(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters)
    { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
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
    s.turn_number         = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// The turn boundary, as far as these per-turn flags are concerned. Mirrors the two real reset sites
// (GameEngine::UntapStep and TurnSolver's per-turn reset) -- if either of those ever drops the line,
// the real game diverges from this test AND from itself (the [fd-diverge] class).
void NextTurn(GameState& s)
{
    ++s.turn_number;
    s.players[0].life_gained_this_turn = 0;
    s.players[1].life_gained_this_turn = 0;
    for (Permanent& p : s.battlefield)
    {
        p.lifegain_counters_used_this_turn = false;
        p.entered_this_turn = false;
        p.temp_power_bonus = 0;
        p.temp_tough_bonus = 0;
    }
}

}   // namespace

TEST_CASE("Nykthos Paragon: 'that many' is the EVENT's amount, on every own creature incl. itself")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Nykthos Paragon", 0, 1);
    Put(s, "Llanowar Elves",  0, 2);
    Put(s, "Llanowar Elves",  1, 3);      // the opponent's -- must never receive a counter

    GainLife(s, 0, 5);                     // ONE event of 5

    CHECK(PlusCounters(ByNumber(s, 1)) == 5);   // the Paragon counters itself
    CHECK(PlusCounters(ByNumber(s, 2)) == 5);
    CHECK(PlusCounters(ByNumber(s, 3)) == 0);   // controller-scoped ("whenever YOU gain life")
}

TEST_CASE("Nykthos Paragon: 'only once each turn' is PER PERMANENT, not per NAME -- 4 copies x 5 = 20")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    for (int i = 1; i <= 4; ++i) { Put(s, "Nykthos Paragon", 0, i); }
    Put(s, "Llanowar Elves", 0, 9);

    GainLife(s, 0, 5);                     // ONE event of 5, four independent resolutions

    // THE DECK'S KILL. If this reads 5 instead of 20, the flag is being treated as per-NAME.
    CHECK(PlusCounters(ByNumber(s, 9)) == 20);
    for (int i = 1; i <= 4; ++i)
    {
        CHECK(PlusCounters(ByNumber(s, i)) == 20);
        CHECK(ByNumber(s, i).lifegain_counters_used_this_turn);
    }

    // A SECOND gain the same turn adds nothing: a used copy does not merely do nothing, it does not
    // trigger at all (Scryfall ruling 3).
    GainLife(s, 0, 7);
    CHECK(PlusCounters(ByNumber(s, 9)) == 20);

    // The turn boundary re-arms every copy -- reset in only ONE of the two real sites is the
    // systematic [fd-diverge] this card's lockstep pair exists to prevent.
    NextTurn(s);
    GainLife(s, 0, 1);
    CHECK(PlusCounters(ByNumber(s, 9)) == 24);
}

TEST_CASE("Nykthos Paragon: DECLINING does not consume the use; the bank survives to a bigger gain")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Nykthos Paragon", 0, 1);
    Put(s, "Llanowar Elves",  0, 9);

    // Decline this event entirely (the human/provider answer 0). Nothing is written and -- the
    // ruling that makes this a real decision -- the use is NOT spent.
    struct Decline
    {
        static int Fn(const GameState&, int, const std::string&, int, int, int) { return 0; }
    };
    LifegainCountersChooser decline = &Decline::Fn;
    g_play_lifegain_counters_chooser = &decline;
    GainLife(s, 0, 1);
    g_play_lifegain_counters_chooser = nullptr;

    CHECK(PlusCounters(ByNumber(s, 9)) == 0);
    CHECK_FALSE(ByNumber(s, 1).lifegain_counters_used_this_turn);

    // ...so the SAME turn's bigger gain still gets the full wave.
    GainLife(s, 0, 10);
    CHECK(PlusCounters(ByNumber(s, 9)) == 10);
    CHECK(ByNumber(s, 1).lifegain_counters_used_this_turn);
}

TEST_CASE("Nykthos Paragon: a partial spend uses exactly that many copies, in battlefield order")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    for (int i = 1; i <= 4; ++i) { Put(s, "Nykthos Paragon", 0, i); }
    Put(s, "Llanowar Elves", 0, 9);

    struct SpendTwo
    {
        static int Fn(const GameState&, int, const std::string&, int, int, int) { return 2; }
    };
    LifegainCountersChooser two = &SpendTwo::Fn;
    g_play_lifegain_counters_chooser = &two;
    GainLife(s, 0, 3);
    g_play_lifegain_counters_chooser = nullptr;

    CHECK(PlusCounters(ByNumber(s, 9)) == 6);            // 2 copies x 3
    CHECK(ByNumber(s, 1).lifegain_counters_used_this_turn);
    CHECK(ByNumber(s, 2).lifegain_counters_used_this_turn);
    CHECK_FALSE(ByNumber(s, 3).lifegain_counters_used_this_turn);
    CHECK_FALSE(ByNumber(s, 4).lifegain_counters_used_this_turn);

    // The two banked copies are still live for a later gain this turn.
    GainLife(s, 0, 4);
    CHECK(PlusCounters(ByNumber(s, 9)) == 6 + 8);
}

TEST_CASE("Blossoming Bogbeast: gain 2 THEN pump, X read after the gain (a bare Bogbeast is +2/+2)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int bog = Put(s, "Blossoming Bogbeast", 0, 1);
    Put(s, "Llanowar Elves", 0, 2);
    Put(s, "Llanowar Elves", 1, 3);        // the opponent's -- never pumped

    std::vector<int> atk{bog};
    ApplyAttackLifegainTeamPump(s, 0, atk);

    CHECK(s.players[0].life == 22);
    CHECK(s.players[0].life_gained_this_turn == 2);
    // X == 2, not 0: "Then" means the pump reads the counter AFTER this trigger's own gain.
    CHECK(ByNumber(s, 1).temp_power_bonus == 2);
    CHECK(ByNumber(s, 1).temp_tough_bonus == 2);
    CHECK(ByNumber(s, 2).temp_power_bonus == 2);   // team-wide, not attackers-only
    CHECK(ByNumber(s, 3).temp_power_bonus == 0);
}

TEST_CASE("Blossoming Bogbeast: X counts the turn's OTHER gains too, not just its own 2")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int bog = Put(s, "Blossoming Bogbeast", 0, 1);
    Put(s, "Llanowar Elves", 0, 2);

    GainLife(s, 0, 10);                    // a pre-combat Feed the Clan (ferocious)
    std::vector<int> atk{bog};
    ApplyAttackLifegainTeamPump(s, 0, atk);

    CHECK(s.players[0].life_gained_this_turn == 12);
    CHECK(ByNumber(s, 1).temp_power_bonus == 12);
    CHECK(ByNumber(s, 2).temp_power_bonus == 12);
}

TEST_CASE("Blossoming Bogbeast: two copies are two triggers -- (+2, pump 2) then (+2, pump 4)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int a = Put(s, "Blossoming Bogbeast", 0, 1);
    const int b = Put(s, "Blossoming Bogbeast", 0, 2);

    std::vector<int> atk{a, b};
    ApplyAttackLifegainTeamPump(s, 0, atk);

    CHECK(s.players[0].life_gained_this_turn == 4);
    // 2 from the first trigger + 4 from the second = 6, the faithful sum of two resolutions
    // (NOT 4+4=8, which is what reading X once after both gains would give).
    CHECK(ByNumber(s, 1).temp_power_bonus == 6);
    CHECK(ByNumber(s, 2).temp_power_bonus == 6);
}

TEST_CASE("Bogbeast + Paragon: the attack gain lands BEFORE damage, so its counters grow THIS swing")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int bog = Put(s, "Blossoming Bogbeast", 0, 1);   // 3/3
    Put(s, "Nykthos Paragon", 0, 2);                       // 4/6, unused
    Put(s, "Ageless Entity",  0, 3);                       // 4/4

    // Declare attackers: the Bogbeast alone. Its 2 life is ONE life-gain event, so it must both
    // spend the Paragon's once-each-turn wave (+2/+2 counters on the whole team) and feed its own
    // +X/+X -- and both must be on the board before the damage loop reads power.
    std::vector<int> atk{bog};
    ApplyAttackLifegainTeamPump(s, 0, atk);

    CHECK(s.players[0].life_gained_this_turn == 2);
    CHECK(ByNumber(s, 2).lifegain_counters_used_this_turn);
    CHECK(PlusCounters(ByNumber(s, 1)) == 2);              // Paragon's wave
    CHECK(PlusCounters(ByNumber(s, 3)) == 2 + 2);          // + Ageless Entity's own "that many"
    CHECK(ByNumber(s, 1).temp_power_bonus == 2);           // Bogbeast's team pump

    // THE POINT: the swing is 3 (printed) + 2 (Paragon counters) + 2 (its own pump) = 7, not 3.
    const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
    CHECK(r.total_damage == 7);
    CHECK(s.players[1].life == 13);
}

// ---- GENESIS WAVE ----------------------------------------------------------------------------
// "{X}{G}{G}{G} Sorcery: Reveal the top X cards of your library. You may put any number of permanent
// cards with mana value X or less from among them onto the battlefield. Then put all cards revealed
// this way that weren't put onto the battlefield into your graveyard." (reveal_x_put_permanents)
//
// 10. SIMULTANEITY. "Put ... onto the battlefield" is ONE event, so every entrant's watchers are
//     live before ANY enter-trigger resolves (CR 603.6d). With A Verdant Sun's Avatars and N
//     creatures entering, the answer is A x N life-gain events -- each Avatar sees every
//     simultaneous entrant AND itself. A per-put sequential loop silently undercounts, and that
//     undercount then propagates into every Ageless Entity counter batch and every Paragon wave.
// 11. The MV cap and the reveal depth are the SAME X, read off the definition card (a library card
//     is a placeholder with an EMPTY type mask, so the tests below would put nothing if the
//     implementation read `raw`).
// 12. A put LAND does not consume the land drop (it is not PLAYED), and its own triggered ETBs
//     (Karoo bounce, land lifegain) are DEFERRED to the second pass so they too obey simultaneity.
// 13. Leftovers are MILLED (not bottomed), in reveal order, and the library is NOT shuffled.

namespace
{
void Stack(GameState& s, int controller, const std::vector<std::string>& top_down, int first_number)
{
    int n = first_number;
    for (const std::string& nm : top_down)
    {
        Card c = Def(nm).card;
        c.m_number = n++;
        // A real library card is a NAME-ONLY placeholder: no type mask, no mana cost. Reproduce that
        // here, because reading `raw` instead of LookupCached is the trap this card is most likely to
        // fall into and a fully-populated fixture card would hide it completely.
        Card raw;
        raw.m_name   = c.m_name;
        raw.m_number = c.m_number;
        raw.RehashName();
        s.players[static_cast<std::size_t>(controller)].library.push_back(raw);
    }
}
int CountOnBoard(const GameState& s, const std::string& name, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && p.card.m_name.str() == name) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Genesis Wave: the MV cap and the reveal depth are the same X, read off the DEFINITION")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Top-down, by mana value: Llanowar Elves {G} = 1, Verdant Sun's Avatar {5}{G}{G} = 7,
    // Forest = 0 (a land is ALWAYS within the cap), Ageless Entity {3}{G}{G} = 5 -- and a fifth
    // card past the reveal window.
    Stack(s, 0, { "Llanowar Elves", "Verdant Sun's Avatar", "Forest", "Ageless Entity",
                  "Priest of Titania" }, 101);

    PerformGenesisWave(s, 0, 4, "Genesis Wave");   // X = 4: reveal FOUR, cap mana value <= 4

    // Elves (1) and Forest (0) are legal; the Avatar (7) and the Entity (5) are over the cap.
    CHECK(CountOnBoard(s, "Llanowar Elves", 0) == 1);
    CHECK(CountOnBoard(s, "Forest", 0) == 1);
    CHECK(CountOnBoard(s, "Verdant Sun's Avatar", 0) == 0);
    CHECK(CountOnBoard(s, "Ageless Entity", 0) == 0);
    // The over-cap cards are MILLED (not bottomed), in reveal order.
    REQUIRE(s.players[0].graveyard.size() == 2);
    CHECK(s.players[0].graveyard[0].m_name.str() == "Verdant Sun's Avatar");
    CHECK(s.players[0].graveyard[1].m_name.str() == "Ageless Entity");
    // The 5th card was never revealed: it is still the top of the library. NO SHUFFLE.
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library[0].m_name.str() == "Priest of Titania");
}

TEST_CASE("Genesis Wave: X = 0 (and a negative from value_or(-1)) is a clean no-op, not 'no cap'")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Stack(s, 0, { "Llanowar Elves", "Forest" }, 101);
    PerformGenesisWave(s, 0, 0, "Genesis Wave");
    CHECK(s.battlefield.empty());
    CHECK(s.players[0].library.size() == 2);
    PerformGenesisWave(s, 0, -1, "Genesis Wave");   // the executor's entry.chosen_x.value_or(-1)
    CHECK(s.battlefield.empty());
    CHECK(s.players[0].library.size() == 2);
}

TEST_CASE("Genesis Wave: SIMULTANEITY -- A live Avatars x N entrants = A x N life-gain events")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // TWO Verdant Sun's Avatars already on the battlefield (5/5 each; each trigger gains the
    // ENTERING creature's toughness), and an Ageless Entity to bank the events as counters.
    Put(s, "Verdant Sun's Avatar", 0, 1);
    Put(s, "Verdant Sun's Avatar", 0, 2);
    Put(s, "Ageless Entity",       0, 3);   // 4/4; "whenever you gain life, put that many counters"
    // The Wave puts THREE 1/1 creatures: Llanowar Elves, Elvish Mystic, Priest of Titania.
    Stack(s, 0, { "Llanowar Elves", "Elvish Mystic", "Priest of Titania" }, 101);

    const int life_before = s.players[0].life;
    PerformGenesisWave(s, 0, 3, "Genesis Wave");

    // THE ARITHMETIC. A = 2 pre-existing Avatars, N = 3 entrants -> A x N = 6 triggers of 1 life.
    // Sequential per-put resolution would give the same 6 here (no Avatar is ENTERING), which is
    // exactly why the next test is the one that separates the two shapes -- this pins the base case.
    CHECK(s.players[0].life == life_before + 6);
    CHECK(s.players[0].life_gained_this_turn == 6);
    // Six SEPARATE gain events (CR 119.10), so six separate +1/+1 counter batches of 1.
    CHECK(PlusCounters(ByNumber(s, 3)) == 6);
}

TEST_CASE("Genesis Wave: an Avatar PUT BY THE WAVE sees its co-entrants AND itself (the undercount)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Verdant Sun's Avatar", 0, 1);   // ONE Avatar already out (5/5)
    Put(s, "Ageless Entity",       0, 2);   // banks every gain event as counters
    // The Wave reveals, in this order: TWO more Verdant Sun's Avatars (5/5, mana value 7) and a
    // Llanowar Elves (1/1). X = 7, so all three are within the cap.
    Stack(s, 0, { "Verdant Sun's Avatar", "Verdant Sun's Avatar", "Llanowar Elves" }, 101);

    const int life_before = s.players[0].life;
    PerformGenesisWave(s, 0, 7, "Genesis Wave");

    // A = 3 Avatars (one live + the two the Wave put), N = 3 entrants -> A x N = 9 triggers, each
    // gaining the ENTERING creature's toughness. Under SIMULTANEITY all three Avatars are already on
    // the battlefield when any enter-trigger resolves:
    //   Avatar #101 enters:  live(5) + #101 itself(5) + #102(5)   = 15
    //   Avatar #102 enters:  live(5) + #101(5)        + #102 self(5) = 15
    //   Elves  #103 enters:  live(1) + #101(1)        + #102(1)   =  3
    //                                                              ----
    //                                                               33   over 9 events
    //
    // A SEQUENTIAL per-put loop (PerformTutorToBattlefield's shape -- fire the cascade INSIDE the
    // placement loop) would instead give 28 over 8 events: #101 enters with only the live Avatar and
    // itself on board (5 + 5 = 10), then #102 (5 + 5 + 5 = 15), then the Elves (3). The missing
    // trigger is #102 watching #101 -- one per (watcher-entered-after-entrant) pair, and it is the
    // whole reason this function has two passes.
    CHECK(s.players[0].life == life_before + 33);
    CHECK(s.players[0].life_gained_this_turn == 33);
    // Nine SEPARATE events, so the Ageless Entity banks each amount: 5+5+5 + 5+5+5 + 1+1+1 = 33.
    CHECK(PlusCounters(ByNumber(s, 2)) == 33);
}

TEST_CASE("Genesis Wave: a put LAND does not consume the land drop, and Blossoming Sands' 1 life "
          "is a real gain event seen by a SIMULTANEOUS Ageless Entity")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.players[0].lands_played_this_turn = 0;
    // Reveal order puts the LAND FIRST, so its deferred lifegain can only reach an Ageless Entity
    // that entered AFTER it if the implementation really does two passes.
    Stack(s, 0, { "Blossoming Sands", "Ageless Entity" }, 101);

    const int life_before = s.players[0].life;
    PerformGenesisWave(s, 0, 5, "Genesis Wave");   // X = 5: Ageless Entity is {3}{G}{G}

    CHECK(CountOnBoard(s, "Blossoming Sands", 0) == 1);
    CHECK(CountOnBoard(s, "Ageless Entity", 0) == 1);
    // A land PUT by an effect is not PLAYED: the land drop is untouched (EnterLand, not LandPlay).
    CHECK(s.players[0].lands_played_this_turn == 0);
    // Blossoming Sands enters tapped and gains 1 life -- EnterLand alone resolves NEITHER of those
    // gains, so this is the deferred ResolveLandEnterEtbExtras firing in pass 2.
    CHECK(ByNumber(s, 101).tapped);
    CHECK(s.players[0].life == life_before + 1);
    CHECK(PlusCounters(ByNumber(s, 102)) == 1);   // the co-entrant Entity saw the land's gain
}

TEST_CASE("Genesis Wave: a put Selesnya Sanctuary DOES bounce (mandatory), and never its own drop")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Forest", 0, 1);            // a victim for the mandatory bounce
    s.battlefield.back().tapped = true;   // BounceKarooLand prefers an already-spent land
    s.players[0].lands_played_this_turn = 1;
    Stack(s, 0, { "Selesnya Sanctuary" }, 101);

    PerformGenesisWave(s, 0, 2, "Genesis Wave");

    // The Sanctuary is on the battlefield, tapped, and the Forest is back in hand -- skipping the
    // bounce would gift a free Karoo (2 mana a turn for nothing).
    CHECK(CountOnBoard(s, "Selesnya Sanctuary", 0) == 1);
    CHECK(CountOnBoard(s, "Forest", 0) == 0);
    CHECK(ByNumber(s, 101).tapped);
    int forests_in_hand = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == "Forest") { ++forests_in_hand; } }
    CHECK(forests_in_hand == 1);
    // The put did not touch the land drop either way.
    CHECK(s.players[0].lands_played_this_turn == 1);
}

TEST_CASE("Genesis Wave: nonpermanents are always milled, and a short library caps the reveal")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Stack(s, 0, { "Feed the Clan", "Genesis Wave", "Forest" }, 101);

    PerformGenesisWave(s, 0, 9, "Genesis Wave");   // X = 9 but only 3 cards remain

    CHECK(CountOnBoard(s, "Forest", 0) == 1);
    CHECK(s.players[0].library.empty());
    REQUIRE(s.players[0].graveyard.size() == 2);   // the Instant and the other Sorcery
    CHECK(s.players[0].graveyard[0].m_name.str() == "Feed the Clan");   // reveal order
    CHECK(s.players[0].graveyard[1].m_name.str() == "Genesis Wave");
}

TEST_CASE("Genesis Wave: the provider default takes every legal permanent (nothing left behind)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Stack(s, 0, { "Llanowar Elves", "Elvish Mystic", "Wellwisher", "Brushland",
                  "Accomplished Alchemist" }, 101);

    PerformGenesisWave(s, 0, 5, "Genesis Wave");

    CHECK(s.battlefield.size() == 5);
    CHECK(s.players[0].graveyard.empty());
    CHECK(s.players[0].library.empty());
    // Every put creature is summoning sick: the Wave's board cannot attack the turn it lands.
    for (const Permanent& p : s.battlefield)
    { if (p.card.IsCreature()) { CHECK(p.entered_this_turn); } }
}
