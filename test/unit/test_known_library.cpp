// THE KNOWN-LIBRARY TRACKER and THE BLIND RESHUFFLE (2026-10-28; Unpredictable Cyclone hard prunes, ledger
// "Hard prunes + known-top blind test"). USER: "any testing or inference about whether something is clairvoyant
// should take in to account known information about the top of the library, since this deck manipulates it a lot."
//
//   * Library keeps WHAT ITS OWNER KNOWS, maintained by the mutation primitives (no effect site notes anything):
//     a known-position mask over the top 32 live positions (every insert() is a placement the owner saw) and the
//     placed BOTTOM block (every push_back). A draw shifts the mask, an erase closes its gap, a shuffle forgets all.
//   * Each effect the deck uses is pinned through its REAL resolver: Brainstorm's put-back, Ponder's keep / shuffle,
//     a scry's keep-on-top + bottom, Long-Term Plans' third-from-the-top, the Cyclone walk's random-order bottom.
//   * Library::BlindUnknownOrder re-randomises ONLY the unknown positions: known positions keep their card, the
//     bottom block keeps its set, everything else is the same multiset in a salt-keyed order.
//   * Nothing reads the tracker unless the blind mode is on (gamesetup::BlindSearchSalt() defaults to -1).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void EnsureCardsKl()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardKl(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

// A library of n distinct-numbered cards built with assign() (no placement noted), top = number 1000.
Library LibKl(int n, const std::string& name = "Island")
{
    EnsureCardsKl();
    std::vector<Card> v;
    for (int i = 0; i < n; ++i) { v.push_back(CardKl(name, 1000 + i)); }
    Library lib;
    lib.assign(v.begin(), v.end());
    return lib;
}

GameState StateKl(const std::vector<std::string>& hand, int lib_n = 20)
{
    EnsureCardsKl();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.players[0].life     = 20;
    int num = 100;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardKl(n, num++)); }
    s.players[0].library = LibKl(lib_n);
    return s;
}

std::vector<int> Numbers(const Library& lib)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < lib.size(); ++i) { out.push_back(lib.RawAt(i).m_number); }
    return out;
}
}   // namespace

TEST_CASE("known library: the primitives keep the known-position mask and the placed bottom block")
{
    Library lib = LibKl(10);
    CHECK(lib.KnownTopMask() == 0u);
    CHECK(lib.BottomPlaced() == 0);
    // Two cards placed on top (a put-back): positions 0 and 1 known.
    lib.insert(lib.begin(), CardKl("Island", 1));
    lib.insert(lib.begin(), CardKl("Island", 2));
    CHECK(lib.KnownAt(0));
    CHECK(lib.KnownAt(1));
    CHECK_FALSE(lib.KnownAt(2));
    CHECK(lib.KnownTopRun() == 2);
    // A draw shifts the mask up.
    (void)lib.DrawTop();
    CHECK(lib.KnownAt(0));
    CHECK_FALSE(lib.KnownAt(1));
    // An insert at position 2 (Long-Term Plans' third from the top) opens a known hole below an unknown card.
    (void)lib.DrawTop();
    lib.insert(lib.begin() + 2, CardKl("Island", 3));
    CHECK_FALSE(lib.KnownAt(0));
    CHECK_FALSE(lib.KnownAt(1));
    CHECK(lib.KnownAt(2));
    CHECK(lib.KnownTopRun() == 0);
    // Erasing position 0 (a draw by erase, the look pattern) moves the known card up.
    lib.erase(lib.begin());
    CHECK(lib.KnownAt(1));
    CHECK_FALSE(lib.KnownAt(2));
    // A bottom placement counts in the block; erasing from inside the block shrinks it.
    lib.push_back(CardKl("Island", 4));
    lib.push_back(CardKl("Island", 5));
    CHECK(lib.BottomPlaced() == 2);
    lib.erase(lib.begin() + static_cast<std::ptrdiff_t>(lib.size() - 1));
    CHECK(lib.BottomPlaced() == 1);
    // A copy carries everything (and drops the drawn prefix).
    Library cp = lib;
    CHECK(cp.KnownTopMask() == lib.KnownTopMask());
    CHECK(cp.BottomPlaced() == lib.BottomPlaced());
    CHECK(Numbers(cp) == Numbers(lib));
    // Any shuffle forgets all of it.
    Library a = lib, b = lib;
    a.ShuffleByKey(7);
    b.Shuffle(7);
    CHECK(a.KnownTopMask() == 0u);
    CHECK(a.BottomPlaced() == 0);
    CHECK(b.KnownTopMask() == 0u);
    CHECK(b.BottomPlaced() == 0);
    // assign() forgets too.
    lib.assign(cp.begin(), cp.end());
    CHECK(lib.KnownTopMask() == 0u);
    CHECK(lib.BottomPlaced() == 0);
    // The library stays 32 bytes (Player 200 / GameState 1160: Dominance.h's tripwires).
    CHECK(sizeof(Library) == 32);
}

TEST_CASE("known library: Brainstorm's put-back, Ponder's keep / shuffle and a scry go through the real resolvers")
{
    // Brainstorm: the two cards put back are known, top first.
    {
        GameState s = StateKl({ "Island", "Toxic Deluge", "Emrakul, the Aeons Torn" });
        ScriptedPutBack pin(0);
        ResolvePutBackFromHand(s, 0, 2, "Brainstorm");
        const Library& lib = s.players[0].library;
        CHECK(lib.KnownAt(0));
        CHECK(lib.KnownAt(1));
        CHECK_FALSE(lib.KnownAt(2));
        CHECK(lib.size() == 22);
    }
    // Ponder KEEP: the three looked cards go back on top, all known; the following draw shifts them.
    {
        GameState s = StateKl({ "Island" });
        std::vector<Card> looked;
        for (int i = 0; i < 3; ++i) { looked.push_back(s.players[0].library.DrawTop()); }
        TopDisposition keep;
        keep.top_order = { 2, 0, 1 };
        ApplyTopDisposition(s, looked, keep, LookKind::Reorder);
        Library& lib = s.players[0].library;
        CHECK(lib.KnownTopRun() == 3);
        (void)lib.DrawTop();   // Ponder's draw
        CHECK(lib.KnownTopRun() == 2);
    }
    // Ponder SHUFFLE: nothing is known.
    {
        GameState s = StateKl({ "Island" });
        std::vector<Card> looked;
        for (int i = 0; i < 3; ++i) { looked.push_back(s.players[0].library.DrawTop()); }
        TopDisposition sh;
        sh.shuffle = true;
        ApplyTopDisposition(s, looked, sh, LookKind::Reorder);
        CHECK(s.players[0].library.KnownTopMask() == 0u);
        CHECK(s.players[0].library.BottomPlaced() == 0);
        CHECK(s.players[0].library.size() == 20);
    }
    // A scry 2 keeping one on top and bottoming one (the Curator cycle's scry, a scry land): position 0 known,
    // the bottomed card in the placed bottom block.
    {
        GameState s = StateKl({ "Island" });
        std::vector<Card> looked;
        for (int i = 0; i < 2; ++i) { looked.push_back(s.players[0].library.DrawTop()); }
        const int bottomed = looked[0].m_number;
        TopDisposition sc;
        sc.top_order = { 1 };
        ApplyTopDisposition(s, looked, sc, LookKind::Scry);
        const Library& lib = s.players[0].library;
        CHECK(lib.KnownTopRun() == 1);
        CHECK(lib.BottomPlaced() == 1);
        CHECK(lib.RawAt(lib.size() - 1).m_number == bottomed);
    }
}

TEST_CASE("known library: Long-Term Plans puts its card at a KNOWN third position under an unknown top")
{
    GameState s = StateKl({ "Island" }, 20);
    s.players[0].library.push_back(CardKl("Emrakul, the Aeons Torn", 555));
    const CardDefinition* ltp = CardDatabase::Instance().Lookup("Long-Term Plans");
    REQUIRE(ltp != nullptr);
    PerformTutor(s, 0, ltp->params, "Emrakul, the Aeons Torn", "Long-Term Plans", false);
    const Library& lib = s.players[0].library;
    CHECK_FALSE(lib.KnownAt(0));
    CHECK_FALSE(lib.KnownAt(1));
    CHECK(lib.KnownAt(2));
    CHECK(lib.RawAt(2).m_number == 555);
    CHECK(lib.BottomPlaced() == 0);   // the search shuffled away the earlier bottom placement
}

TEST_CASE("known library: the Cyclone walk's random-order bottom is a placed bottom block")
{
    GameState s = StateKl({ "Island" }, 10);
    std::vector<Card> rest = { CardKl("Island", 1), CardKl("Island", 2), CardKl("Island", 3) };
    BottomInRandomOrder(s, 0, std::move(rest));
    CHECK(s.players[0].library.BottomPlaced() == 3);
    CHECK(s.players[0].library.KnownTopMask() == 0u);
}

TEST_CASE("blind reshuffle: only the unknown positions move; known positions and the bottom set stay")
{
    Library lib = LibKl(30);
    lib.insert(lib.begin(), CardKl("Island", 7));                 // known top 0
    lib.insert(lib.begin(), CardKl("Island", 8));                 // known top 0, 1
    lib.insert(lib.begin() + 4, CardKl("Island", 9));             // known 4
    lib.push_back(CardKl("Island", 21));
    lib.push_back(CardKl("Island", 22));
    lib.push_back(CardKl("Island", 23));                          // bottom block of 3
    const std::vector<int> before = Numbers(lib);
    const std::uint32_t mask = lib.KnownTopMask();
    const std::size_t bottom = lib.BottomPlaced();
    REQUIRE(bottom == 3);

    Library a = lib;
    a.BlindUnknownOrder(1);
    const std::vector<int> after = Numbers(a);
    REQUIRE(after.size() == before.size());
    // Knowledge unchanged; known positions keep their card.
    CHECK(a.KnownTopMask() == mask);
    CHECK(a.BottomPlaced() == bottom);
    CHECK(after[0] == before[0]);
    CHECK(after[1] == before[1]);
    CHECK(after[4] == before[4]);
    // The bottom block keeps its SET.
    std::vector<int> bb(before.end() - 3, before.end()), ba(after.end() - 3, after.end());
    std::sort(bb.begin(), bb.end());
    std::sort(ba.begin(), ba.end());
    CHECK(bb == ba);
    // The whole library is the same multiset; the unknown middle actually moved.
    std::vector<int> sb = before, sa = after;
    std::sort(sb.begin(), sb.end());
    std::sort(sa.begin(), sa.end());
    CHECK(sb == sa);
    int moved = 0;
    for (std::size_t i = 0; i + 3 < after.size(); ++i) { if (after[i] != before[i]) { ++moved; } }
    CHECK(moved > 10);
    // Deterministic per salt; a different salt gives a different sample.
    Library a2 = lib;
    a2.BlindUnknownOrder(1);
    CHECK(Numbers(a2) == after);
    Library b = lib;
    b.BlindUnknownOrder(2);
    CHECK(Numbers(b) != after);
    // COMMON RANDOM NUMBERS: removing one unknown card leaves every other unknown card's relative order.
    Library c = lib;
    c.erase(c.begin() + 10);
    const int gone = before[10];
    c.BlindUnknownOrder(1);
    std::vector<int> rel_a, rel_c;
    for (std::size_t i = 0; i + 3 < after.size(); ++i)
    { if (!a.KnownAt(i) && after[i] != gone) { rel_a.push_back(after[i]); } }
    const std::vector<int> cn = Numbers(c);
    for (std::size_t i = 0; i + 3 < cn.size(); ++i) { if (!c.KnownAt(i)) { rel_c.push_back(cn[i]); } }
    CHECK(rel_a == rel_c);
}

TEST_CASE("blind search is OFF by default")
{
    gamesetup::t_setup.blind_salt = -1;
    CHECK(gamesetup::BlindSearchSalt() == -1);
    gamesetup::t_setup.blind_salt = 3;
    CHECK(gamesetup::BlindSearchSalt() == 3);
    gamesetup::t_setup.blind_salt = -1;
}
