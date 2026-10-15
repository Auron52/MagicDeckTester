// Unit tests for Courser of Kruphix's two static clauses (2026-10-15): "Play with the top card of your
// library revealed" and "You may play lands from the top of your library" -- the library TOP as a
// second SOURCE for the turn's land drop (Plan::land_from_top). Design:
// logs/landfall_drafts/_courser_design.md; contract: docs/design/landfall-trigger-infrastructure.md.
//
// What is guarded, and why each failure would be SILENT:
//  1. THE SHARED PREDICATES (TopLandPlayStaticSource / PlayableTopLandNumber / LibraryTopVisible):
//     one definition both worlds read. A top that is not a land, a Courser that is not OURS, an
//     empty library and a cleared deck constant must all read "no top land".
//  2. THE SHARED BODY (PlayLandFromLibraryTop): the top card leaves the LIBRARY (not the hand), it is
//     the turn's land drop (not an extra one), it is not a draw, and the landfall dispatch fires
//     (Courser's own lifegain) -- exactly as for a hand land.
//  3. NEVER A SUBSTITUTE: PlayLandByName / the executor refuse a top play whose top is not that land,
//     instead of quietly playing a hand copy (the auras-gi20 divergence class).
//  4. BRANCHED, NEVER FOLDED: with a Forest in hand AND a Forest on top, the enumeration offers BOTH
//     (a top play keeps the hand copy and advances the library); applying the top plan leaves the hand
//     Forest in hand.
//  5. THE GREEDY SITES (GreedyLandChoice -- d0 runner, beyond-horizon playout): the top is a candidate
//     in the same passes and wins a same-pass tie (provider default), and with no playable top the
//     pick is the hand ranker's, unchanged.
//  6. THE BREAKPOINT PREDICATE (TopLandNewlyPlayable): a NEW playable top with the drop open and not
//     reserved -- and nothing else.
//  7. COMPOSITION with the landfall land-after-casts axis: "cast Rampaging Baloths, THEN play the TOP
//     Forest" is enumerated and, applied, makes the Beast and plays the top card.
//  8. MAIN 2: a top that was already a playable land as main 1 began counts as "held".
#include <doctest/doctest.h>

#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoadedCk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefCk(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshCk()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int PutCk(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card              = DefCk(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card CardCk(const std::string& name, int number)
{
    Card c = DefCk(name).card;
    c.m_number = number;
    return c;
}

// The library, top first.
void LibraryCk(GameState& s, const std::vector<std::pair<std::string, int>>& top_first)
{
    for (const auto& e : top_first) { s.players[0].library.push_back(CardCk(e.first, e.second)); }
}

int CountNamedCk(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

int HandCountCk(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

constexpr const char* kCourserCk = "Courser of Kruphix";

}   // namespace

TEST_CASE("Courser of Kruphix: both static clauses are params, and the body is a 2/4 for {1}{G}{G}")
{
    EnsureCardsLoadedCk();
    const CardDefinition& d = DefCk(kCourserCk);
    CHECK(d.params.library_top_revealed);
    CHECK(d.params.play_lands_from_library_top);
    CHECK(d.params.landfall_lifegain == 1);
    CHECK(d.card.m_power.value_or(-1) == 2);
    CHECK(d.card.m_toughness.value_or(-1) == 4);
    CHECK(d.card.m_mana_cost.ManaValue() == 3);
}

TEST_CASE("Library top predicates: a land on top under OUR Courser, and nothing else")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, "Forest", 10);
    LibraryCk(s, { { "Mountain", 7 }, { "Forest", 8 } });
    // No Courser: nothing is playable from the top, nothing is visible.
    CHECK(TopLandPlayStaticSource(s) == nullptr);
    CHECK(PlayableTopLandNumber(s) == 0);
    CHECK_FALSE(LibraryTopVisible(s));
    // An OPPONENT's Courser does nothing for us.
    PutCk(s, kCourserCk, 90, /*controller=*/1);
    CHECK(PlayableTopLandNumber(s) == 0);
    CHECK_FALSE(LibraryTopVisible(s));
    // Our Courser: the top Mountain is playable and visible.
    PutCk(s, kCourserCk, 3);
    CHECK(TopLandPlayStaticSource(s) != nullptr);
    CHECK(PlayableTopLandNumber(s) == 7);
    CHECK(LibraryTopVisible(s));
    // A non-land on top: visible, not playable.
    GameState t = s;
    t.players[0].library.DrawTop();
    t.players[0].library.DrawTop();
    t.players[0].library.push_back(CardCk("Lightning Bolt", 9));
    CHECK(PlayableTopLandNumber(t) == 0);
    CHECK(LibraryTopVisible(t));
    // The deck constant gates the walk: cleared, nothing is playable (a deck with no Courser).
    GameState u = s;
    u.deck_has_top_land_play = false;
    CHECK(PlayableTopLandNumber(u) == 0);
    // An empty library.
    GameState v = FreshCk();
    PutCk(v, kCourserCk, 3);
    CHECK(PlayableTopLandNumber(v) == 0);
}

TEST_CASE("PlayLandFromLibraryTop: the top leaves the LIBRARY as the turn's drop, fires landfall, is no draw")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    PutCk(s, "Forest", 10);
    s.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(s, { { "Mountain", 7 }, { "Forest", 8 } });
    const std::size_t lib_before = s.players[0].library.size();
    LandPlayOptions o;
    REQUIRE(PlayLandFromLibraryTop(s, DefCk("Mountain"), o));
    CHECK(s.players[0].library.size() == lib_before - 1);
    CHECK(s.players[0].library.front().m_number == 8);         // the next card is the new top
    CHECK(HandCountCk(s, "Forest") == 1);                        // the hand is untouched
    CHECK(s.players[0].lands_played_this_turn == 1);             // it IS the land drop
    CHECK(s.players[0].cards_drawn_this_turn == 0);              // and it is NOT a draw
    CHECK(s.players[0].life == 21);                              // Courser's own landfall lifegain
    bool on_bf = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 7) { on_bf = true; } }
    CHECK(on_bf);
}

TEST_CASE("Top-land play never substitutes a hand copy: a top that is not that land plays NOTHING")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    PutCk(s, "Forest", 10);
    s.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(s, { { "Mountain", 7 } });
    // Ask the rollout's applier (through a top-sourced plan) for a top FOREST: the top is a Mountain.
    TurnSolver::Plan p;
    p.land_decided  = true;
    p.land_to_play  = "Forest";
    p.land_from_top = true;
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, true);
    CHECK(t.players[0].lands_played_this_turn == 0);   // refused...
    CHECK(HandCountCk(t, "Forest") == 1);              // ...and the hand Forest was NOT played instead
    CHECK(t.players[0].library.size() == 1);
    // The same plan naming the Mountain plays it from the top.
    p.land_to_play = "Mountain";
    GameState u = s;
    TurnSolver::ApplyPlan(u, p, true);
    CHECK(u.players[0].lands_played_this_turn == 1);
    CHECK(u.players[0].library.empty());
    CHECK(HandCountCk(u, "Forest") == 1);
}

TEST_CASE("Enumeration: a top Forest and a hand Forest are BOTH offered -- branched, never folded")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    for (int i = 0; i < 3; ++i) { PutCk(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(s, { { "Forest", 7 }, { "Mountain", 8 } });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* top_plan = nullptr;
    bool hand_plan = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play != "Forest") { continue; }
        if (p.land_from_top) { if (top_plan == nullptr) { top_plan = &p; } }
        else                 { hand_plan = true; }
    }
    CHECK(hand_plan);
    REQUIRE(top_plan != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *top_plan, true);
    CHECK(HandCountCk(t, "Forest") == 1);                        // the hand copy is kept
    CHECK(t.players[0].library.front().m_number == 8);           // the top Forest was played
    CHECK(CountNamedCk(t, "Forest") == 4);
}

TEST_CASE("Enumeration: no Courser -> no top-sourced plan (every other deck is unchanged)")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    for (int i = 0; i < 3; ++i) { PutCk(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(s, { { "Forest", 7 } });
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { CHECK_FALSE(p.land_from_top); }
}

TEST_CASE("GreedyLandChoice: the top is a candidate in the same passes and wins a same-pass tie")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    PutCk(s, "Forest", 10);
    s.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(s, { { "Forest", 7 } });
    const GreedyLandPick gp = GreedyLandChoice(s);
    CHECK(gp.from_top);                                          // same pass, provider default: top first
    // With no playable top the pick is exactly the hand ranker's.
    GameState t = s;
    t.players[0].library.DrawTop();
    t.players[0].library.push_back(CardCk("Lightning Bolt", 9));
    const GreedyLandPick hp = GreedyLandChoice(t);
    CHECK_FALSE(hp.from_top);
    CHECK(hp.hand_index == GreedyLandChoiceIndex(t));
    // A better PASS beats the tie rule: an untapped hand land over a tapped top land.
    GameState u = FreshCk();
    PutCk(u, kCourserCk, 3);
    PutCk(u, "Forest", 10);
    u.players[0].hand.push_back(CardCk("Forest", 20));
    LibraryCk(u, { { "Gruul Turf", 7 } });                      // enters tapped
    const GreedyLandPick up = GreedyLandChoice(u);
    CHECK_FALSE(up.from_top);
    CHECK(up.hand_index == 0);
}

TEST_CASE("TopLandNewlyPlayable: a NEW playable top with the drop open and not reserved")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, "Forest", 10);
    LibraryCk(s, { { "Mountain", 7 } });
    const int before = PlayableTopLandNumber(s);                 // no Courser: 0
    CHECK(before == 0);
    PutCk(s, kCourserCk, 3);                                     // Courser enters
    CHECK(TurnSolver::TopLandNewlyPlayable(before, s, false));   // the top Mountain is new
    CHECK_FALSE(TurnSolver::TopLandNewlyPlayable(7, s, false));  // ...but not if it was already the top
    CHECK_FALSE(TurnSolver::TopLandNewlyPlayable(before, s, true));   // a reserved drop
    GameState t = s;
    t.players[0].lands_played_this_turn = 1;                     // the drop is used
    CHECK_FALSE(TurnSolver::TopLandNewlyPlayable(before, t, false));
    GameState u = s;
    u.deck_has_top_land_play = false;                            // the deck constant
    CHECK_FALSE(TurnSolver::TopLandNewlyPlayable(before, u, false));
}

TEST_CASE("Composition: 'cast Rampaging Baloths, THEN play the TOP Forest' is enumerated and makes the Beast")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    for (int i = 0; i < 6; ++i) { PutCk(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(CardCk("Rampaging Baloths", 1));
    LibraryCk(s, { { "Forest", 7 }, { "Mountain", 8 } });
    const TurnSolver::Plan* after_top = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool baloths = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Rampaging Baloths") { baloths = true; } }
        if (baloths && p.land_slot == TurnSolver::kLandSlotAfterCasts && p.land_from_top && p.land_to_play == "Forest")
        { after_top = &p; break; }
    }
    REQUIRE(after_top != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *after_top, true);
    CHECK(CountNamedCk(t, "4/4 Beast Token") == 1);              // the top land entered after Baloths
    CHECK(t.players[0].library.front().m_number == 8);           // and it came off the top
    CHECK(t.players[0].lands_played_this_turn == 1);
}

TEST_CASE("Main 2: a top that was already a playable land as main 1 began is 'held'")
{
    EnsureCardsLoadedCk();
    GameState s = FreshCk();
    PutCk(s, kCourserCk, 3);
    PutCk(s, "Forest", 10);
    LibraryCk(s, { { "Mountain", 7 } });
    s.StampMain1Hand(PlayableTopLandNumber(s));
    CHECK(s.HeldSinceMain1(s.players[0].library.front()));
    GameState t = s;
    t.StampMain1Hand(0);                                         // no playable top at main 1
    CHECK_FALSE(t.HeldSinceMain1(t.players[0].library.front()));
}
