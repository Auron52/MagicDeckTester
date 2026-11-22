// Opponent-lockdown FREE TURNS (USER proposal + rulings 2026-11-21; docs/design/opponent-lockdown-free-turns.md,
// core/FreeTurns.h). MTG_FREE_TURNS is forced per test through freeturn::t_force (the env is unset in the suite).
//   * the lock: (no land OR no nonland in hand) AND no opposing creature (a due spawn counts); 2HG per head;
//   * a free turn keeps turn_number, advances TurnSeq / RawRound, in both worlds (lockstep);
//   * the opening hurdles: screw (rightmost nonlands -> basics in place), flood (rightmost lands out,
//     placeholders at the right end), the bounceland extension, the basic choice (incl. the no-basic lists);
//   * Worldfire's post-cast board is credited;
//   * nothing changes with the flag off.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/EngineFlags.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/FreeTurns.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "runner/GoldFishRunner.h"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FtScope
{
    int saved = freeturn::t_force, saved_h = freeturn::t_force_hurdles;
    FtScope(int on, int hurdles = -1) { freeturn::t_force = on; freeturn::t_force_hurdles = hurdles; }
    ~FtScope() { freeturn::t_force = saved; freeturn::t_force_hurdles = saved_h; }
};
struct FtHeads
{
    int saved = gamesetup::t_setup.opponent_heads;
    explicit FtHeads(int h) { gamesetup::t_setup.opponent_heads = h; }
    ~FtHeads() { gamesetup::t_setup.opponent_heads = saved; }
};

void FtLoadDb()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card FtCard(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void FtOppPerm(GameState& s, const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 1;
    p.owner_index = 1;
    s.battlefield.push_back(p);
}

Decklist FtForests()
{
    Decklist deck;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 40; ++i) { deck.mainboard.push_back(forest->card); }
    return deck;
}

// A fresh real-opponent game whose zones the test then sets by hand.
GameState FtFresh(int heads = 1)
{
    FtLoadDb();
    FtHeads h(heads);
    GameState s = GoldFishRunner::SetupGame(FtForests(), 8, 0);
    REQUIRE(s.opponent_plays_lands);
    s.players[1].hand.clear();
    s.opponent2_hand.clear();
    s.battlefield.clear();
    s.opponent_spawns = nullptr;
    s.turn_number = 3;
    s.deck_has_free_turn = true;   // the rule acts only for a lock-trait deck; these boards stand for one
    return s;
}

int FtSlotOf(const char* list)
{
    for (std::size_t i = 0; i < opponentdeck::Pool().size(); ++i)
    { if (std::string(opponentdeck::Pool()[i].name) == list) { return static_cast<int>(i); } }
    FAIL("no pool list ", list);
    return -1;
}

std::vector<std::string> FtNames(const std::vector<Card>& hand)
{
    std::vector<std::string> out;
    for (const Card& c : hand) { out.push_back(c.m_name.str()); }
    return out;
}

void FtEmpty(Library& lib) { while (!lib.empty()) { lib.erase(lib.begin()); } }

std::vector<std::string> FtLibNames(const Library& lib)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < lib.size(); ++i) { out.push_back(lib.RawAt(i).m_name.str()); }
    return out;
}

} // namespace

TEST_CASE("free turns: the lock condition (lands OR hand) AND no creature, incl. a due spawn")
{
    GameState s = FtFresh();
    // No land, a nonland in hand, no creature: locked.
    s.players[1].hand.push_back(FtCard("Lightning Bolt", 100001));
    CHECK(freeturn::LockHolds(s));
    // A land and a nonland: not locked.
    FtOppPerm(s, "Mountain", 100002);
    CHECK_FALSE(freeturn::LockHolds(s));
    // A land and only lands in hand: locked.
    s.players[1].hand = { FtCard("Mountain", 100003) };
    CHECK(freeturn::LockHolds(s));
    // An MDFC spell//land counts as the side the hand has FEWER of (USER 2026-11-22), so it fills an empty side.
    // Beside a land it is the hand's nonland: not locked.
    s.players[1].hand = { FtCard("Mountain", 100003), FtCard("Turntimber Symbiosis", 100004) };
    CHECK_FALSE(freeturn::LockHolds(s));
    // Alone, both sides are empty and the tie goes to NONLAND: with a land out it would cast the spell side, not locked.
    s.players[1].hand = { FtCard("Turntimber Symbiosis", 100004) };
    CHECK_FALSE(freeturn::LockHolds(s));
    // Empty hand, a land, but an opposing creature: not locked.
    s.players[1].hand.clear();
    CHECK(freeturn::LockHolds(s));
    FtOppPerm(s, "Goblin Guide", 100005);
    CHECK_FALSE(freeturn::LockHolds(s));
    s.battlefield.pop_back();
    // A spawn due with our next regular turn counts as the opponent's creature.
    static const std::vector<OpponentSpawn> due = { { 4, 1, 1 } };
    static const std::vector<OpponentSpawn> past = { { 3, 1, 1 } };
    s.opponent_spawns = &due;
    CHECK_FALSE(freeturn::LockHolds(s));
    s.opponent_spawns = &past;
    CHECK(freeturn::LockHolds(s));
    // A decked opponent never locks (the game is over).
    s.opponent_decked = true;
    CHECK_FALSE(freeturn::LockHolds(s));
}

TEST_CASE("free turns: 2HG -- EVERY head must be locked, each on its own zones (the number band)")
{
    GameState s = FtFresh(2);
    REQUIRE(opponentdeck::HasSecondHead(s));
    // Head 1: no land (locked). Head 2: a land of ITS band and a nonland in ITS hand (not locked).
    s.players[1].hand = { FtCard("Lightning Bolt", 100001) };
    FtOppPerm(s, "Forest", 110001);
    s.opponent2_hand = { FtCard("Lightning Bolt", 110002) };
    CHECK(freeturn::HeadLocked(s, 1));
    CHECK_FALSE(freeturn::HeadLocked(s, 2));
    CHECK_FALSE(freeturn::LockHolds(s));
    // Head 2 out of nonlands: both locked.
    s.opponent2_hand.clear();
    CHECK(freeturn::LockHolds(s));
    // Head 1's land (its band) unlocks head 1 only while head 1 still holds a nonland.
    FtOppPerm(s, "Mountain", 100009);
    CHECK_FALSE(freeturn::HeadLocked(s, 1));
    CHECK_FALSE(freeturn::LockHolds(s));
}

TEST_CASE("free turns: a free turn keeps turn_number and advances TurnSeq / RawRound, in BOTH worlds")
{
    FtScope on(1, 0);
    GameState s = FtFresh();
    s.players[1].hand.clear();   // nothing to play, nothing to cast: the opponent stays locked through its turn
    FtEmpty(s.players[1].library);
    s.players[1].library.push_back(FtCard("Lightning Bolt", 100050));   // its draw: a nonland, no land to play
    s.players[1].library.push_back(FtCard("Lightning Bolt", 100051));
    const int seq0 = s.TurnSeq();

    GameState roll = s;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
    CHECK(roll.turn_number == 3);
    CHECK(roll.in_free_turn);
    CHECK(static_cast<int>(roll.free_turns_taken) == 1);
    CHECK(roll.TurnSeq() == seq0 + 1);
    CHECK(roll.RawRound() == 4);
    CHECK_FALSE(roll.pending_free_turn);

    GameState exec = s;
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    REQUIRE(engine.AdvanceToFirstMainForTest(exec));
    CHECK(exec.turn_number == roll.turn_number);
    CHECK(exec.in_free_turn == roll.in_free_turn);
    CHECK(exec.free_turns_taken == roll.free_turns_taken);
    CHECK(exec.players[1].hand.size() == roll.players[1].hand.size());

    // The opponent draws a land next: it plays it, holds the Bolt -> unlocked -> the round advances.
    roll.players[1].library.insert(roll.players[1].library.begin(), FtCard("Mountain", 100052));
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
    CHECK(roll.turn_number == 4);
    CHECK_FALSE(roll.in_free_turn);
    CHECK(static_cast<int>(roll.free_turns_taken) == 1);
    CHECK(roll.RawRound() == 5);
}

TEST_CASE("free turns: with the flag OFF nothing changes (the round advances on a locked board)")
{
    FtScope off(0);
    GameState s = FtFresh();
    FtEmpty(s.players[1].library);
    s.players[1].library.push_back(FtCard("Lightning Bolt", 100050));
    GameState roll = s;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
    CHECK(roll.turn_number == 4);
    CHECK_FALSE(roll.in_free_turn);
    CHECK(roll.free_turns_taken == 0);
    CHECK(roll.TurnSeq() == 4);
    // ...and the hurdles do not touch a dealt hand.
    std::vector<Card> hand = { FtCard("Lightning Bolt", 1), FtCard("Lightning Bolt", 2) };
    GameState d = FtFresh();
    d.players[1].hand = hand;
    freeturn::ApplyOpeningHurdles(d);
    CHECK(FtNames(d.players[1].hand) == FtNames(hand));
}

TEST_CASE("free turns: Worldfire's post-cast board is credited (the opponent draws one card and stays locked)")
{
    FtScope on(1);
    FtLoadDb();
    GameState s = GoldFishRunner::SetupGame(FtForests(), 8, 0);
    REQUIRE(s.opponent_plays_lands);
    s.opponent_spawns = nullptr;
    s.turn_number = 5;
    s.deck_has_free_turn = true;   // a Worldfire list carries the lock trait
    FtOppPerm(s, "Mountain", 100090);
    FtOppPerm(s, "Goblin Guide", 100091);
    const CardDefinition* wf = CardDatabase::Instance().Lookup("Worldfire");
    REQUIRE(wf != nullptr);
    ResolveExileAllAndSetLife(s, wf->params);
    CHECK(s.players[1].hand.empty());
    for (const Permanent& p : s.battlefield) { CHECK(p.controller_index == 0); }
    // Whatever the top card is (land: played, empty hand; nonland: no land), the opponent ends its turn locked.
    for (const char* top : { "Mountain", "Lightning Bolt" })
    {
        GameState r = s;
        r.players[1].library.insert(r.players[1].library.begin(), FtCard(top, 100099));
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(r));
        INFO("top = " << top);
        CHECK(r.turn_number == 5);
        CHECK(r.in_free_turn);
    }
}

TEST_CASE("free turns: the screw hurdle -- the rightmost nonlands become basics in place; MDFC faces are lands")
{
    FtLoadDb();
    // 0 lands: the two rightmost nonlands become basics, in their slots.
    std::vector<Card> h = { FtCard("Lightning Bolt", 1), FtCard("Goblin Guide", 2), FtCard("Ponder", 3),
                            FtCard("Lightning Bolt", 4), FtCard("Sol Ring", 5), FtCard("Ponder", 6),
                            FtCard("Goblin Guide", 7) };
    freeturn::HurdleResult r = freeturn::ApplyHurdlesToHand(h, "Mountain", opponentdeck::kNumberBase);
    CHECK(r.screw_basics == 2);
    CHECK(h.size() == 7);
    CHECK(FtNames(h) == std::vector<std::string>{ "Lightning Bolt", "Goblin Guide", "Ponder", "Lightning Bolt",
                                                  "Sol Ring", "Mountain", "Mountain" });
    CHECK(opponentdeck::HeadOfNumber(h[6].m_number) == 1);
    // 1 land (an MDFC's land face counts): one basic replaces the rightmost nonland.
    std::vector<Card> m = { FtCard("Turntimber Symbiosis", 1), FtCard("Goblin Guide", 2), FtCard("Ponder", 3),
                            FtCard("Lightning Bolt", 4), FtCard("Sol Ring", 5), FtCard("Ponder", 6),
                            FtCard("Goblin Guide", 7) };
    r = freeturn::ApplyHurdlesToHand(m, "Forest", opponentdeck::kNumberBase2);
    CHECK(r.screw_basics == 1);
    CHECK(m[0].m_name.str() == "Turntimber Symbiosis");
    CHECK(m[6].m_name.str() == "Forest");
    CHECK(opponentdeck::HeadOfNumber(m[6].m_number) == 2);
    // 2 lands: untouched.
    std::vector<Card> ok = { FtCard("Mountain", 1), FtCard("Goblin Guide", 2), FtCard("Mountain", 3),
                             FtCard("Ponder", 4) };
    const std::vector<std::string> before = FtNames(ok);
    r = freeturn::ApplyHurdlesToHand(ok, "Mountain", opponentdeck::kNumberBase);
    CHECK(r.screw_basics + r.flood_placeholders + r.bounce_basics == 0);
    CHECK(FtNames(ok) == before);
}

TEST_CASE("free turns: the flood hurdle -- the rightmost lands leave, placeholders take the right end")
{
    FtLoadDb();
    std::vector<Card> h = { FtCard("Mountain", 1), FtCard("Forest", 2), FtCard("Mountain", 3), FtCard("Island", 4),
                            FtCard("Lightning Bolt", 5), FtCard("Forest", 6), FtCard("Plains", 7) };
    const freeturn::HurdleResult r = freeturn::ApplyHurdlesToHand(h, "Mountain", opponentdeck::kNumberBase);
    CHECK(r.flood_placeholders == 1);
    CHECK(h.size() == 7);
    CHECK(FtNames(h) == std::vector<std::string>{ "Mountain", "Forest", "Mountain", "Island", "Lightning Bolt",
                                                  "Forest", freeturn::kPlaceholderName });
    // The placeholder is a NONLAND card nobody plays as a land.
    const CardDefinition* ph = CardDatabase::Instance().LookupCached(h.back());
    REQUIRE(ph != nullptr);
    CHECK_FALSE(ph->card.IsLand());
    CHECK_FALSE(PlayableAsLand(ph));
    CHECK_FALSE(opponentdeck::HandCardIsLandFront(h.back()));
    // Seven lands: two placeholders.
    std::vector<Card> all(7, FtCard("Forest", 9));
    CHECK(freeturn::ApplyHurdlesToHand(all, "Forest", opponentdeck::kNumberBase).flood_placeholders == 2);
    CHECK(all[5].m_name.str() == freeturn::kPlaceholderName);
    CHECK(all[6].m_name.str() == freeturn::kPlaceholderName);
}

TEST_CASE("free turns: the bounceland extension -- no land that can stay alone -> the rightmost becomes a basic")
{
    FtLoadDb();
    std::vector<Card> h = { FtCard("Gruul Turf", 1), FtCard("Goblin Guide", 2), FtCard("Gruul Turf", 3),
                            FtCard("Ponder", 4), FtCard("Lightning Bolt", 5), FtCard("Ponder", 6),
                            FtCard("Goblin Guide", 7) };
    freeturn::HurdleResult r = freeturn::ApplyHurdlesToHand(h, "Forest", opponentdeck::kNumberBase);
    CHECK(r.screw_basics == 0);
    CHECK(r.bounce_basics == 1);
    CHECK(h[0].m_name.str() == "Gruul Turf");
    CHECK(h[2].m_name.str() == "Forest");
    // A lone Karoo plus a basic: nothing to do (the drop policy plays the basic first).
    std::vector<Card> k = { FtCard("Gruul Turf", 1), FtCard("Forest", 2), FtCard("Ponder", 3), FtCard("Ponder", 4) };
    r = freeturn::ApplyHurdlesToHand(k, "Forest", opponentdeck::kNumberBase);
    CHECK(r.bounce_basics == 0);
    // One Karoo, six nonlands: the screw basic already stays alone, so no extension.
    std::vector<Card> one = { FtCard("Gruul Turf", 1), FtCard("Goblin Guide", 2), FtCard("Ponder", 3),
                              FtCard("Ponder", 4), FtCard("Lightning Bolt", 5), FtCard("Ponder", 6),
                              FtCard("Goblin Guide", 7) };
    r = freeturn::ApplyHurdlesToHand(one, "Forest", opponentdeck::kNumberBase);
    CHECK(r.screw_basics == 1);
    CHECK(r.bounce_basics == 0);
    // Lake of the Dead cannot stay alone either.
    std::vector<Card> lake = { FtCard("Lake of the Dead", 1), FtCard("Lake of the Dead", 2), FtCard("Ponder", 3),
                               FtCard("Ponder", 4) };
    CHECK(freeturn::ApplyHurdlesToHand(lake, "Swamp", opponentdeck::kNumberBase).bounce_basics == 1);
    CHECK(lake[1].m_name.str() == "Swamp");
}

TEST_CASE("free turns: the hurdle basic -- the list's most common basic; the four no-basic lists by colour")
{
    FtLoadDb();
    for (std::size_t i = 0; i < opponentdeck::Pool().size(); ++i)
    {
        const std::string& b = freeturn::HurdleBasicName(i);
        const CardDefinition* d = CardDatabase::Instance().Lookup(b);
        INFO("list " << opponentdeck::Pool()[i].name << " -> " << b);
        REQUIRE(d != nullptr);
        CHECK(d->card.IsLand());
        CHECK(d->card.HasSupertype(Supertype::Basic));
    }
    CHECK(freeturn::HurdleBasicName(static_cast<std::size_t>(FtSlotOf("Accomplished Alchemist"))) == "Forest");
    // No basics: the colour their lands produce most (copies x colours, ties in WUBRG order).
    for (const char* list : { "Eldrazi Displacer Flicker", "Fluctuator", "Galadriel", "Prevent Damage" })
    {
        const std::string& b = freeturn::HurdleBasicName(static_cast<std::size_t>(FtSlotOf(list)));
        MESSAGE(std::string(list) << " -> " << b);
        CHECK(b != "Wastes");
    }
}

namespace {
Decklist FtDeckWith(const std::vector<std::string>& main, const std::vector<std::string>& side = {})
{
    Decklist deck = FtForests();
    for (const std::string& n : main)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(d != nullptr, "not in cards.json: ", n);
        deck.mainboard.push_back(d->card);
    }
    for (const std::string& n : side)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(d != nullptr, "not in cards.json: ", n);
        deck.sideboard.push_back(d->card);
    }
    return deck;
}
} // namespace

TEST_CASE("free turns: the deal applies the hurdles under the flag (every head), never without it")
{
    FtLoadDb();
    auto lands_nonlands = [](const std::vector<Card>& hand) {
        int lands = 0;
        for (const Card& c : hand) { if (PlayableAsLand(CardDatabase::Instance().LookupCached(c))) { ++lands; } }
        return std::make_pair(lands, static_cast<int>(hand.size()) - lands);
    };
    int screwed_off = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        FtHeads h(seed % 2 == 0 ? 2 : 1);
        GameState off, on;
        const Decklist lock = FtDeckWith({ "Worldfire" });   // a lock-trait deck: the hurdles act
        { FtScope f(0); off = GoldFishRunner::SetupGame(lock, seed, 0); }
        { FtScope f(1); on = GoldFishRunner::SetupGame(lock, seed, 0); }
        REQUIRE(on.deck_has_free_turn);
        REQUIRE(on.players[1].hand.size() == off.players[1].hand.size());
        CHECK(on.players[1].library.size() == off.players[1].library.size());   // library untouched
        if (lands_nonlands(off.players[1].hand).first < 2) { ++screwed_off; }
        const auto ln = lands_nonlands(on.players[1].hand);
        CHECK(ln.first >= 2);
        CHECK(ln.second >= 2);
        if (opponentdeck::HasSecondHead(on))
        {
            const auto l2 = lands_nonlands(on.opponent2_hand);
            CHECK(l2.first >= 2);
            CHECK(l2.second >= 2);
        }
        // The player's own game is untouched.
        CHECK(FtLibNames(on.players[0].library) == FtLibNames(off.players[0].library));
    }
    CHECK(screwed_off > 0);   // the sample does exercise the screw hurdle
}


TEST_CASE("free turns: the lock trait arms only on a card that can strip ALL lands or ALL nonland hand cards")
{
    FtLoadDb();
    // IN: mass exile, the end-step hand discard (MTG_FREE_TURN_SIRE, default ON), annihilator.
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Worldfire" })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Sire of Insanity" })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Emrakul, the Aeons Torn" })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Ulamog, the Infinite Gyre" })));
    // OUT: a creature steal (Captivating Vampire), an opponent discard the engine does not model (Neheb), a land
    // the opponent replaces with a basic (White Orchid Phantom), one-land removals.
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Captivating Vampire" })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Neheb, the Worthy" })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "White Orchid Phantom" })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Acidic Slime" })));   // one-land removal: IN (USER 2026-11-22)
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Fulminator Mage" })));   // one-land removal: IN (USER 2026-11-22)
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Kiora Bests the Sea God" })));   // one-land removal: IN (USER 2026-11-22)
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({})));
    // The SIDEBOARD counts only when a main-deck wish can reach it: Vampires' wishless sideboard Worldfire is out.
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Captivating Vampire" }, { "Worldfire" })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(FtDeckWith({ "Living Wish" }, { "Emrakul, the Aeons Torn" })));
}

TEST_CASE("free turns: under the rule the opponent's cleanup discards the more common kind (hand + field)")
{
    auto overfull = [] {
        GameState s = FtFresh();
        s.players[1].hand = { FtCard("Lightning Bolt", 100001), FtCard("Turntimber Symbiosis", 100002) };
        for (int i = 0; i < 7; ++i) { s.players[1].hand.push_back(FtCard("Mountain", 100010 + i)); }
        FtOppPerm(s, "Mountain", 100030);
        return s;
    };
    {   // OFF: oldest first, as ever -- both spells go, the kept hand is all lands: "locked" with no lock card.
        FtScope f(0);
        GameState s = overfull();
        opponentdeck::EndOfTurnCleanup(s);
        CHECK(s.players[1].hand.size() == 7);
        CHECK(FtNames(s.players[1].graveyard) == std::vector<std::string>{ "Lightning Bolt", "Turntimber Symbiosis" });
    }
    {   // ON: oldest first while more than two nonlands remain, then the oldest LAND instead.
        FtScope f(1);
        GameState s = overfull();
        s.players[1].hand.insert(s.players[1].hand.begin(), FtCard("Rift Bolt", 100003));   // 10 cards, 3 nonlands
        opponentdeck::EndOfTurnCleanup(s);
        // 8 lands (7 in hand + 1 in play) vs 3 nonlands (the MDFC joins the smaller side): three lands go.
        CHECK(FtNames(s.players[1].graveyard) == std::vector<std::string>{ "Mountain", "Mountain", "Mountain" });
    }
    {   // ON: lands are the more common kind, the two oldest LANDS go; the spells stay (the MDFC joins the smaller, nonland side), so no lock.
        FtScope f(1);
        GameState s = overfull();
        opponentdeck::EndOfTurnCleanup(s);
        CHECK(s.players[1].hand.size() == 7);
        CHECK(FtNames(s.players[1].graveyard) == std::vector<std::string>{ "Mountain", "Mountain" });
        CHECK(s.players[1].hand[0].m_name == "Lightning Bolt");
        CHECK(s.players[1].hand[1].m_name == "Turntimber Symbiosis");
        CHECK_FALSE(freeturn::LockHolds(s));
        // A hand with no land left sheds its oldest nonland.
        s.players[1].hand.clear();
        for (int i = 0; i < 8; ++i) { s.players[1].hand.push_back(FtCard("Lightning Bolt", 100040 + i)); }
        opponentdeck::EndOfTurnCleanup(s);
        CHECK(s.players[1].hand.size() == 7);
        CHECK(s.players[1].hand.front().m_number == 100041);
    }
    {   // ON with the hurdles off (MTG_FREE_TURN_HURDLES=0, the credit alone): oldest first.
        FtScope f(1, 0);
        GameState s = overfull();
        opponentdeck::EndOfTurnCleanup(s);
        CHECK(FtNames(s.players[1].graveyard) == std::vector<std::string>{ "Lightning Bolt", "Turntimber Symbiosis" });
    }
}

TEST_CASE("free turns: 2HG -- the second head's cleanup discards the more common kind too")
{
    FtScope f(1);
    GameState s = FtFresh(2);
    REQUIRE(opponentdeck::HasSecondHead(s));
    s.opponent2_hand = { FtCard("Lightning Bolt", 110001) };
    for (int i = 0; i < 7; ++i) { s.opponent2_hand.push_back(FtCard("Forest", 110010 + i)); }
    opponentdeck::EndOfTurnCleanup(s);
    REQUIRE(s.opponent2_hand.size() == 7);
    CHECK(s.opponent2_hand.front().m_name == "Lightning Bolt");
}

TEST_CASE("free turns: a deck WITHOUT the lock trait is untouched under the flag (hurdles, lock, floor, spawns)")
{
    // USER 2026-11-22: "if we do it right, there won't be any change to any of our existing decks".
    FtLoadDb();
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        FtHeads h(seed % 2 == 0 ? 2 : 1);
        GameState off, on;
        { FtScope f(0); off = GoldFishRunner::SetupGame(FtForests(), seed, 0); }
        { FtScope f(1); on = GoldFishRunner::SetupGame(FtForests(), seed, 0); }
        REQUIRE_FALSE(on.deck_has_free_turn);
        CHECK(FtNames(on.players[1].hand) == FtNames(off.players[1].hand));
        CHECK(FtNames(on.opponent2_hand) == FtNames(off.opponent2_hand));
    }
    FtScope f(1);
    GameState s = FtFresh();
    s.deck_has_free_turn = false;
    // A locked board raises no free turn...
    s.players[1].hand = { FtCard("Lightning Bolt", 100001) };
    REQUIRE(freeturn::LockHolds(s));
    freeturn::NoteOpponentTurnEnd(s);
    CHECK_FALSE(s.pending_free_turn);
    // ...the cleanup sheds oldest first...
    s.players[1].hand = { FtCard("Lightning Bolt", 100001), FtCard("Lightning Bolt", 100002) };
    for (int i = 0; i < 7; ++i) { s.players[1].hand.push_back(FtCard("Mountain", 100010 + i)); }
    opponentdeck::EndOfTurnCleanup(s);
    CHECK(FtNames(s.players[1].graveyard) == std::vector<std::string>{ "Lightning Bolt", "Lightning Bolt" });
    // ...and a spawn appears with no opponent land.
    CHECK_FALSE(freeturn::SpawnNeedsLand(s));
    CHECK(freeturn::SpawnMayAppear(s));
}

TEST_CASE("free turns: an MDFC counts as the side the hand has fewer of (ties to nonland)")
{
    FtLoadDb();
    auto sides = [](std::initializer_list<const char*> names) {
        std::vector<Card> h;
        int n = 1;
        for (const char* x : names) { h.push_back(FtCard(x, n++)); }
        const opponentdeck::HandSides r = opponentdeck::CountHandSides(h);
        return std::make_pair(r.lands, r.nonlands);
    };
    CHECK(sides({ "Turntimber Symbiosis" }) == std::make_pair(0, 1));
    CHECK(sides({ "Lightning Bolt", "Turntimber Symbiosis" }) == std::make_pair(1, 1));
    CHECK(sides({ "Mountain", "Turntimber Symbiosis" }) == std::make_pair(1, 1));
    CHECK(sides({ "Turntimber Symbiosis", "Turntimber Symbiosis", "Turntimber Symbiosis" }) == std::make_pair(1, 2));
    CHECK(sides({ "Mountain", "Mountain", "Lightning Bolt", "Turntimber Symbiosis" }) == std::make_pair(2, 2));
    // The flood hurdle: six lands + an MDFC is 6 lands + 1 nonland (the MDFC), so ONE placeholder; the MDFC stays.
    std::vector<Card> h = { FtCard("Turntimber Symbiosis", 1) };
    for (int i = 0; i < 6; ++i) { h.push_back(FtCard("Forest", 2 + i)); }
    const freeturn::HurdleResult r = freeturn::ApplyHurdlesToHand(h, "Forest", opponentdeck::kNumberBase);
    CHECK(r.flood_placeholders == 1);
    CHECK(h.front().m_name.str() == "Turntimber Symbiosis");
    CHECK(h.back().m_name.str() == freeturn::kPlaceholderName);
}

TEST_CASE("free turns: a scheduled spawn needs an opponent land (both worlds); the lock counts only one that appears")
{
    static const std::vector<OpponentSpawn> due = { { 4, 1, 1 } };
    auto board = [](bool land) {
        GameState s = FtFresh();
        s.players[1].hand.clear();
        FtEmpty(s.players[1].library);
        s.players[1].library.push_back(FtCard("Lightning Bolt", 100050));   // its draw: no land to play
        s.players[1].library.push_back(FtCard("Lightning Bolt", 100051));
        if (land) { FtOppPerm(s, "Mountain", 100002); }
        s.opponent_spawns = &due;
        return s;
    };
    auto opp_creatures = [](const GameState& s) {
        int n = 0;
        for (const Permanent& p : s.battlefield) { if (p.controller_index == 1 && p.card.IsCreature()) { ++n; } }
        return n;
    };
    {   // Armed, no opponent land: no token in either world, and the due spawn does not block the lock.
        FtScope f(1, 0);
        GameState s = board(false);
        REQUIRE(freeturn::SpawnNeedsLand(s));
        CHECK(freeturn::LockHolds(s));
        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        CHECK(opp_creatures(roll) == 0);
        CHECK(roll.in_free_turn);
        GameState exec = s;
        AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine engine(ai);
        REQUIRE(engine.AdvanceToFirstMainForTest(exec));
        CHECK(opp_creatures(exec) == 0);
        CHECK(exec.in_free_turn == roll.in_free_turn);
    }
    {   // Armed, an opponent land and an empty hand: the token appears, so it blocks the lock.
        FtScope f(1, 0);
        GameState s = board(true);
        CHECK_FALSE(freeturn::LockHolds(s));
        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        CHECK(opp_creatures(roll) == 1);
        CHECK_FALSE(roll.in_free_turn);
    }
    {   // MTG_OPP_SPAWN_NEEDS_LAND=1 with the free-turn flag OFF: the requirement alone, for every deck.
        FtScope f(0);
        const int saved = freeturn::t_force_spawn_land;
        freeturn::t_force_spawn_land = 1;
        GameState s = board(false);
        s.deck_has_free_turn = false;
        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        CHECK(opp_creatures(roll) == 0);
        freeturn::t_force_spawn_land = 0;   // =0: off even under the rule
        CHECK_FALSE(freeturn::SpawnNeedsLand(s));
        freeturn::t_force_spawn_land = saved;
    }
    {   // Flag off, env unset: every spawn appears, as ever.
        FtScope f(0);
        GameState s = board(false);
        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        CHECK(opp_creatures(roll) == 1);
    }
}

TEST_CASE("free turns: Sire fairness -- a castable nonland permanent in the discarded hand vetoes the lock")
{
    FtLoadDb();
    GameState s = FtFresh();
    s.deck_has_demon_triggers = true;
    const CardDefinition* sire = CardDatabase::Instance().Lookup("Sire of Insanity");
    REQUIRE(sire != nullptr);
    Permanent p;
    p.card = sire->card;
    p.card.m_number = 5;
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
    FtOppPerm(s, "Mountain", 100001);
    // Goblin Guide ({R}) with a Mountain: castable. Ponder: not a permanent.
    s.players[1].hand = { FtCard("Ponder", 100002), FtCard("Goblin Guide", 100003) };
    CHECK(freeturn::SireDeployablePermanent(s));
    s.players[1].hand = { FtCard("Ponder", 100002) };
    CHECK_FALSE(freeturn::SireDeployablePermanent(s));
    // Artifact is a nonland permanent, while a land is never a virtual deployment.
    s.players[1].hand = { FtCard("Sol Ring", 100006) };
    CHECK(freeturn::SireDeployablePermanent(s));
    {
        FtScope f(1);
        freeturn::NoteSireBeforeDiscard(s);
        CHECK(s.sire_lock_veto);
    }
    s.sire_lock_veto = false;
    s.players[1].hand = { FtCard("Mountain", 100007) };
    CHECK_FALSE(freeturn::SireDeployablePermanent(s));
    // A green creature with only a Mountain: the colour is missing.
    s.players[1].hand = { FtCard("Avenger of Zendikar", 100004) };
    CHECK_FALSE(freeturn::SireDeployablePermanent(s));
    // The veto: armed + lever on only; consumed by the lock check.
    s.players[1].hand.clear();
    {
        FtScope f(1);
        s.sire_lock_veto = true;
        CHECK_FALSE(freeturn::LockHolds(s));
        freeturn::NoteOpponentTurnEnd(s);
        CHECK_FALSE(s.pending_free_turn);
        CHECK_FALSE(s.sire_lock_veto);
        CHECK(freeturn::LockHolds(s));   // empty hand, a land, no creature: locked once the veto is gone
    }
}

TEST_CASE("free turns: annihilator fairness -- virtual targets from castable nonland permanents veto the lock")
{
    FtLoadDb();
    GameState s = FtFresh();
    FtOppPerm(s, "Mountain", 100001);
    // One real permanent, annihilator 1: it is all taken. A castable Goblin Guide would have been a second target.
    s.players[1].hand = { FtCard("Ponder", 100002), FtCard("Goblin Guide", 100003) };
    CHECK(opponentdeck::AnnihilatorWouldKeepPermanent(s, 1));
    CHECK_FALSE(opponentdeck::AnnihilatorWouldKeepPermanent(s, 2));   // 1 real + 1 virtual = 2, all taken
    // A sorcery is no permanent; a green creature has no Forest to cast it.
    s.players[1].hand = { FtCard("Ponder", 100002), FtCard("Avenger of Zendikar", 100004) };
    CHECK_FALSE(opponentdeck::AnnihilatorWouldKeepPermanent(s, 1));
    // More real permanents than the count: a real one stays, the check does not apply.
    FtOppPerm(s, "Mountain", 100005);
    s.players[1].hand = { FtCard("Goblin Guide", 100003) };
    CHECK_FALSE(opponentdeck::AnnihilatorWouldKeepPermanent(s, 1));

    // Through ApplyAnnihilator: Ulamog (annihilator 4) attacks a Mountain; the hand holds four castable Goblin Guides.
    GameState g = FtFresh();
    g.deck_has_annihilator = true;
    FtOppPerm(g, "Mountain", 100001);
    g.opponent_played_land_this_turn = true;
    g.players[1].hand.clear();
    for (int i = 0; i < 4; ++i) { g.players[1].hand.push_back(FtCard("Goblin Guide", 100010 + i)); }
    const CardDefinition* ulamog = CardDatabase::Instance().Lookup("Ulamog, the Infinite Gyre");
    REQUIRE(ulamog != nullptr);
    Permanent u;
    u.card = ulamog->card;
    u.card.m_number = 7;
    u.controller_index = 0;
    u.owner_index = 0;
    g.battlefield.push_back(u);
    auto attack = [](GameState st, int fair) {
        FtScope f(1);
        const int saved = freeturn::t_force_ann_fair;
        freeturn::t_force_ann_fair = fair;
        std::vector<int> atk;
        for (int i = 0; i < static_cast<int>(st.battlefield.size()); ++i)
        { if (st.battlefield[static_cast<std::size_t>(i)].controller_index == 0) { atk.push_back(i); } }
        ApplyAnnihilator(st, 0, atk);
        freeturn::t_force_ann_fair = saved;
        return st;
    };
    {   // Lever off: the Mountain is sacrificed, no veto, the lock holds (no land, no creature).
        GameState r = attack(g, 0);
        CHECK_FALSE(r.annihilator_lock_veto);
        FtScope f(1);
        CHECK(freeturn::LockHolds(r));
    }
    {   // Lever on: same sacrifice (a check only), but 1 real + 4 virtual > 4 -> the veto; consumed by the turn-end check.
        GameState r = attack(g, 1);
        CHECK(r.annihilator_lock_veto);
        int opp = 0;
        for (const Permanent& p : r.battlefield) { if (p.controller_index == 1) { ++opp; } }
        CHECK(opp == 0);
        FtScope f(1);
        CHECK_FALSE(freeturn::LockHolds(r));
        freeturn::NoteOpponentTurnEnd(r);
        CHECK_FALSE(r.pending_free_turn);
        CHECK_FALSE(r.annihilator_lock_veto);
    }
    {   // No land played on the following opponent turn: virtual targets do not prevent the lock.
        GameState r = attack(g, 1);
        r.opponent_played_land_this_turn = false;
        CHECK(r.annihilator_lock_veto);
        FtScope f(1);
        CHECK(freeturn::LockHolds(r));
        freeturn::NoteOpponentTurnEnd(r);
        CHECK(r.pending_free_turn);
    }
    {   // Lever on, but a deck without the lock trait: never armed.
        GameState n = g;
        n.deck_has_free_turn = false;
        GameState r = attack(n, 1);
        CHECK_FALSE(r.annihilator_lock_veto);
    }
}

TEST_CASE("free turns: the annihilator fairness veto observes whether the opponent played a land")
{
    GameState s = FtFresh();
    s.players[1].hand = { FtCard("Mountain", 120001) };
    ResolvePassiveOpponentLandTurn(s);
    CHECK(s.opponent_played_land_this_turn);
    ResolvePassiveOpponentLandTurn(s);
    CHECK_FALSE(s.opponent_played_land_this_turn);
}

TEST_CASE("free turns: MTG_OPP_BOUNCE_OPENING -- the bounceland opening rule in every game, its own lever")
{
    FtLoadDb();
    GameState s = FtFresh();
    s.deck_has_free_turn = false;   // a deck without the lock trait: the hurdles never act
    s.players[1].hand = { FtCard("Gruul Turf", 1), FtCard("Goblin Guide", 2), FtCard("Gruul Turf", 3),
                          FtCard("Ponder", 4) };
    const std::vector<std::string> before = FtNames(s.players[1].hand);
    struct BScope { int saved = freeturn::t_force_bounce_opening;
                    explicit BScope(int v) { freeturn::t_force_bounce_opening = v; }
                    ~BScope() { freeturn::t_force_bounce_opening = saved; } };
    {   // Off (=0; the default is ON since the fourth pass), flag on or off: untouched.
        BScope b(0);
        FtScope f(1);
        GameState t = s;
        freeturn::ApplyOpeningHurdles(t);
        CHECK(FtNames(t.players[1].hand) == before);
    }
    {   // On, flag off: the rightmost Karoo becomes the list's hurdle basic; nothing else moves (no screw / flood swap).
        BScope b(1);
        FtScope f(0);
        GameState t = s;
        freeturn::ApplyOpeningHurdles(t);
        const std::vector<std::string> after = FtNames(t.players[1].hand);
        CHECK(after[0] == "Gruul Turf");
        CHECK(after[1] == "Goblin Guide");
        CHECK(after[2] == freeturn::HurdleBasicName(t.opponent_slot));
        CHECK(after[3] == "Ponder");
        CHECK(opponentdeck::HeadOfNumber(t.players[1].hand[2].m_number) == 1);
        // A hand with a land that stays alone: untouched.
        GameState k = s;
        k.players[1].hand[2] = FtCard("Forest", 3);
        const std::vector<std::string> kb = FtNames(k.players[1].hand);
        freeturn::ApplyOpeningHurdles(k);
        CHECK(FtNames(k.players[1].hand) == kb);
    }
}

TEST_CASE("free turns: cleanup by kind -- the field counts, creature tokens do not, a tie sheds a land")
{
    FtScope f(1);
    GameState s = FtFresh();
    // 4 nonlands + 4 lands in hand, nothing in play: a tie -> the oldest LAND (agent default).
    s.players[1].hand = { FtCard("Lightning Bolt", 100001), FtCard("Mountain", 100002), FtCard("Ponder", 100003),
                          FtCard("Mountain", 100004), FtCard("Lightning Bolt", 100005), FtCard("Mountain", 100006),
                          FtCard("Ponder", 100007), FtCard("Mountain", 100008) };
    {
        GameState t = s;
        opponentdeck::EndOfTurnCleanup(t);
        CHECK(FtNames(t.players[1].graveyard) == std::vector<std::string>{ "Mountain" });
        CHECK(t.players[1].graveyard.front().m_number == 100002);
    }
    {   // Two nonland permanents in play: 4 lands vs 6 nonlands -> the oldest NONLAND goes.
        GameState t = s;
        FtOppPerm(t, "Sol Ring", 100020);
        FtOppPerm(t, "Sol Ring", 100021);
        opponentdeck::EndOfTurnCleanup(t);
        CHECK(t.players[1].graveyard.front().m_number == 100001);
    }
    {   // Creature TOKENS do not count: two token bodies leave the tie -> a land.
        GameState t = s;
        FtOppPerm(t, "Goblin Guide", 100030);
        FtOppPerm(t, "Goblin Guide", 100031);
        for (Permanent& p : t.battlefield) { if (p.controller_index == 1) { p.is_token = true; } }
        opponentdeck::EndOfTurnCleanup(t);
        CHECK(t.players[1].graveyard.front().m_number == 100002);
    }
    {   // Off the rule: oldest first.
        FtScope off(0);
        GameState t = s;
        opponentdeck::EndOfTurnCleanup(t);
        CHECK(t.players[1].graveyard.front().m_number == 100001);
    }
}

TEST_CASE("free turns: MTG_OPP_BOUNCE_OPENING default ON, --legacy-opponent-bounce-opening turns it off for a replay")
{
    REQUIRE(freeturn::t_force_bounce_opening == -1);
    CHECK(OppOpeningBounceBasicOn());   // the environment is unset in the suite: the default
    g_play_legacy_opponent_bounce_opening = true;
    CHECK_FALSE(OppOpeningBounceBasicOn());
    g_play_legacy_opponent_bounce_opening = false;
    CHECK(OppOpeningBounceBasicOn());
}

TEST_CASE("free turns: MTG_FREE_TURNS default ON")
{
    REQUIRE(freeturn::t_force == -1);
    CHECK(FreeTurnsOn());
    g_play_legacy_no_free_turns = true;
    CHECK_FALSE(FreeTurnsOn());
    g_play_legacy_no_free_turns = false;
}

TEST_CASE("free turns: MTG_FREE_TURN_REACH -- the reachable-lock certificate")
{
    FtScope f(1);
    GameState s = FtFresh();
    s.deck_free_turn_hand_intact = true;
    auto hand = [](std::initializer_list<std::pair<const char*, int>> cards) {
        std::vector<Card> h;
        for (const auto& c : cards) { h.push_back(FtCard(c.first, c.second)); }
        return h;
    };
    // (a) + (b): a plain land and a plain nonland -> certified, the prunes stay armed.
    CHECK(freeturn::HeadHoldsSureLandAndNonland(hand({ { "Lightning Bolt", 100001 }, { "Forest", 100002 } })));
    // no nonland: a head with a land in play and no nonland in hand is LOCKED -> no certificate.
    CHECK_FALSE(freeturn::HeadHoldsSureLandAndNonland(hand({ { "Forest", 100001 }, { "Mountain", 100002 } })));
    // no land, only a Karoo (it may return itself): no certificate.
    CHECK_FALSE(freeturn::HeadHoldsSureLandAndNonland(hand({ { "Gruul Turf", 100001 }, { "Ponder", 100002 } })));
    // a fetchland LEFT of the plain land is the drop and may find nothing: no certificate ...
    CHECK_FALSE(freeturn::HeadHoldsSureLandAndNonland(
        hand({ { "Misty Rainforest", 100001 }, { "Forest", 100002 }, { "Ponder", 100003 } })));
    // ... to its RIGHT it is never the drop: certified.
    CHECK(freeturn::HeadHoldsSureLandAndNonland(
        hand({ { "Forest", 100001 }, { "Misty Rainforest", 100002 }, { "Ponder", 100003 } })));
    // a Karoo left of the plain land: taken only with another land in play, and it keeps itself -> certified.
    CHECK(freeturn::HeadHoldsSureLandAndNonland(
        hand({ { "Gruul Turf", 100001 }, { "Forest", 100002 }, { "Ponder", 100003 } })));
    // an MDFC is neither side's proof.
    CHECK_FALSE(freeturn::HeadHoldsSureLandAndNonland(hand({ { "Turntimber Symbiosis", 100001 }, { "Forest", 100002 } })));

    s.players[1].hand = hand({ { "Lightning Bolt", 100001 }, { "Forest", 100002 } });
    CHECK_FALSE(freeturn::FreeTurnMayFollow(s));
    CHECK_FALSE(freeturn::NextTurnMayShareRound(s));
    {   // =0 restores the stand-down at every node.
        const std::int8_t saved = heurarm::t_arm[heurarm::FREE_TURN_REACH];
        heurarm::t_arm[heurarm::FREE_TURN_REACH] = 0;
        CHECK(freeturn::NextTurnMayShareRound(s));
        heurarm::t_arm[heurarm::FREE_TURN_REACH] = saved;
    }
    {   // a deck whose cards touch the opponent's hand or library: never certified.
        GameState t = s;
        t.deck_free_turn_hand_intact = false;
        CHECK(freeturn::NextTurnMayShareRound(t));
    }
    {   // no sure land: the lock may be reachable -> stand down.
        GameState t = s;
        t.players[1].hand = hand({ { "Lightning Bolt", 100001 }, { "Ponder", 100002 } });
        CHECK(freeturn::NextTurnMayShareRound(t));
    }
    {   // an extra-turn deck stands down whatever the certificate says.
        GameState t = s;
        t.deck_has_extra_turn = true;
        CHECK(freeturn::NextTurnMayShareRound(t));
    }
    {   // a deck without the lock trait never stands down for a free turn.
        GameState t = s;
        t.deck_has_free_turn = false;
        t.players[1].hand = hand({ { "Lightning Bolt", 100001 } });
        CHECK_FALSE(freeturn::NextTurnMayShareRound(t));
    }
}

TEST_CASE("free turns: MTG_FREE_TURN_REACH 2HG -- ONE certified head is enough (the lock needs every head)")
{
    FtScope f(1);
    GameState s = FtFresh(2);
    REQUIRE(opponentdeck::HasSecondHead(s));
    s.deck_free_turn_hand_intact = true;
    s.players[1].hand = { FtCard("Lightning Bolt", 100001) };
    s.opponent2_hand = { FtCard("Forest", opponentdeck::kNumberBase2 + 1), FtCard("Ponder", opponentdeck::kNumberBase2 + 2) };
    CHECK_FALSE(freeturn::FreeTurnMayFollow(s));
    s.opponent2_hand = { FtCard("Ponder", opponentdeck::kNumberBase2 + 2) };
    CHECK(freeturn::FreeTurnMayFollow(s));
}

TEST_CASE("free turns: the certificate's deck precondition (GoldFishRunner::DeckLockNeedsLandStrip)")
{
    FtLoadDb();
    Decklist d = FtForests();
    d.mainboard.push_back(CardDatabase::Instance().Lookup("Acidic Slime")->card);
    CHECK(GoldFishRunner::DeckMayLockOpponent(d));
    CHECK(GoldFishRunner::DeckLockNeedsLandStrip(d));
    Decklist w = d;
    w.mainboard.push_back(CardDatabase::Instance().Lookup("Worldfire")->card);
    CHECK_FALSE(GoldFishRunner::DeckLockNeedsLandStrip(w));
    Decklist si = d;
    si.mainboard.push_back(CardDatabase::Instance().Lookup("Sire of Insanity")->card);
    CHECK_FALSE(GoldFishRunner::DeckLockNeedsLandStrip(si));
}

TEST_CASE("free turns: the spawn land requirement reads the battlefield only (the T1 lever is deleted)")
{
    // On the draw the opponent's turn 0 has already played the leftmost land it held (GameEngine::RunTurnFrom), so a
    // land still only in hand at our first untap is a land it could not play; it never counts.
    FtScope f(1);
    GameState s = FtFresh();
    struct SScope { int saved = freeturn::t_force_spawn_land; SScope() { freeturn::t_force_spawn_land = 1; }
                    ~SScope() { freeturn::t_force_spawn_land = saved; } } sp;
    s.turn_number = 1;
    s.free_turns_taken = 0;
    s.on_the_play = false;
    s.players[1].hand = { FtCard("Forest", 100001), FtCard("Ponder", 100002) };
    CHECK_FALSE(freeturn::SpawnMayAppear(s));
}
