// Unit tests for the Giants cleanup-discard buckets: GiantsProvider::CleanupDiscardCandidates, the
// USER-settled design of 2026-10-09 (docs/design/giants-discard-policy-proposal.md, "Giants: SETTLED").
//
// Every expected order below was derived BY HAND from the design text -- the keep ladder
//   MANA, GIANT, DAREDEVIL, MANA, GIANT, MANA, GREAVES, DAREDEVIL2, GIANT3, MANA, BOLT, MANA, MANA,
//   MANA7 (soft), BOLT2 (soft)
// and the shed order S0 Pyroclasm / dead rock (MTG_GIANTS_ROCK_AFTER_LANDS=0 only; ON, a rock we cannot
// cast next turn is a mana source after every land) > S1 mana past the 7th (Fire Diamond, Mountains, Sol Ring)
// > S2 spare Greaves > S3 3rd/4th Bolt > S4 spare Daredevils > S5 spare Giants (lowest discounted value
// first) > the ladder read backwards -- with the Giants valued 7 x 0.85^position in the USER's order
// (Titan 0, Flinger 1, Behemoth 2 [-1 with 2+ Giants in play, 7 with none], Tectonic 3, Sovereign 4,
// Goliath 5, Harbinger 6) times P(play) = {1, .70, .35, .10, .03, .02} by mana deficit. The derivation
// is written next to each case. Every case also asserts the GENERIC fallback's index 0 (the control
// arm), and the hatch case proves MTG_GIANTS_BUCKET_DISCARD=0 restores exactly the generic ranking.
//
// G1-G17 are the 2026-10-09 boards, derived for that table and pinned to MTG_DISCARD_DIST_MODEL=0 (which
// must reproduce it exactly). D1-D4 replace the table with the shared distance model (discard_dist, the
// default since the USER's 2026-10-10 ruling): castability = sum over our next 4 turns of the newly
// castable probability x 0.75^(turn - 1), + 0.02 x 0.75^4 for the stranded mass, from the board's mana,
// the KEPT lands (one drop per turn), the kept rocks' mana from the turn they can be cast, and lands drawn
// at the library's density. Each D case asserts both arms; each differs from the table.
// T1-T2 pin GiantsProvider::TutorSearchWidth (MTG_GIANTS_TUTOR_WIDTH): 7, one per Giant name.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsGd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands that
// way so a policy reading the card's own (empty) type / cost / subtype fields would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeGiants(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                     const std::vector<std::string>& library = {})
{
    EnsureCardsGd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(Ph(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

// `dist` pins MTG_DISCARD_DIST_MODEL: the G cases were derived for the 2026-10-09 table (dist 0); the D
// cases for the shared distance model (dist 1, the default).
std::vector<int> GiArm(const GameState& s, int arm, int dist = 0)
{
    heurarm::t_arm[heurarm::GIANTS_BUCKET_DISCARD] = arm;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]    = dist;
    std::vector<int> v = GiantsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::GIANTS_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]    = -1;
    return v;
}
std::vector<int> Gi(const GameState& s)  { return GiArm(s, 1, 0); }
std::vector<int> GiD(const GameState& s) { return GiArm(s, 1, 1); }
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones last, via tier C of the shared ranking).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kM   = "Mountain";
const char* kSol = "Sol Ring";
const char* kFD  = "Fire Diamond";
const char* kDd  = "Stinkdrinker Daredevil";
const char* kTi  = "Inferno Titan";
const char* kSov = "Sunrise Sovereign";
const char* kBeh = "Borderland Behemoth";
const char* kTec = "Tectonic Giant";
const char* kFl  = "Surtland Flinger";
const char* kGol = "Hamletback Goliath";
const char* kHar = "Giant Harbinger";
const char* kGr  = "Lightning Greaves";
const char* kBo  = "Lightning Bolt";
const char* kPy  = "Pyroclasm";

std::vector<std::string> Mountains(int k) { return std::vector<std::string>(static_cast<std::size_t>(k), kM); }

// A library of `n` cards: `lands` Mountains, the rest Lightning Bolts (counts are all the model reads).
std::vector<std::string> Lib(int n, int lands)
{
    std::vector<std::string> lib = Mountains(lands);
    for (int k = lands; k < n; ++k) { lib.push_back(kBo); }
    return lib;
}

}   // namespace

// Board 2 Mountains: need 4, soft 5, Daredevil 2, reach 5. Titan (pos 0, 1 short) 4.9 > Sovereign
// (pos 4, 1 short) 2.56. Ladder: M0 Ti4 Dd3 M1 Sov5 M2 -- -- -- -- Bo6. Pyroclasm (S0) first, then
// the ladder backwards.
TEST_CASE("Giants discard G1: Pyroclasm is the first shed (the proposal's worked example A)")
{
    const GameState s = MakeGiants({ kM, kM, kM, kDd, kTi, kSov, kBo, kPy }, Mountains(2));
    CHECK(Gi(s) == std::vector<int>{ 7, 6, 2, 5, 1, 3, 4, 0 });
    CHECK(Gen(s).front() == 4);   // generic max-MV sheds Inferno Titan and keeps the Pyroclasm
}

// Board 3 Mountains: need 3, soft 4. Pick list Sol7 M0 M1 M2 M3 M4 (land in play -> Sol Ring leads).
// Ladder: Sol7 Ti5 -- M0 Gol6 M1 ... MANA7=M2. Surplus Mountains 4, 3 shed first.
TEST_CASE("Giants discard G2: a flooded hand sheds the Mountains past the soft 7th source (example B)")
{
    const GameState s = MakeGiants({ kM, kM, kM, kM, kM, kTi, kGol, kSol }, Mountains(3));
    CHECK(Gi(s) == std::vector<int>{ 4, 3, 2, 1, 6, 0, 5, 7 });
    CHECK(Gen(s).front() == 6);   // generic sheds Hamletback Goliath
}

// Board 7 Mountains: the mana target (6) and the soft 7th are both met by the board, so every hand
// mana card is surplus, shed as the pick list reversed: Fire Diamond, Mountains, Sol Ring.
TEST_CASE("Giants discard G3: mana past the 7th sheds Fire Diamond, then Mountains, then Sol Ring")
{
    const GameState s = MakeGiants({ kFD, kM, kSol, kM, kTi, kFl, kBo, kGr }, Mountains(7));
    CHECK(Gi(s) == std::vector<int>{ 0, 3, 1, 2, 6, 7, 5, 4 });
    CHECK(Gen(s).front() == 4);
}

// Board 3 Mountains + a Daredevil: target 4 -> need 1, soft 2; Daredevil quota 1. reach 5; Titan costs
// 4 (P 1, 7.0), Behemoth costs 5 but no Giant is in play so it is LAST (pos 7, 2.24). The 2nd hand
// Daredevil is spare (S4). Ladder: M2 Ti5 Dd0 -- Beh4 -- -- -- -- -- Bo6 -- -- M3 Bo7.
TEST_CASE("Giants discard G4: a Daredevil in play lowers the mana target to 4 and the Daredevil quota to 1")
{
    std::vector<std::string> board = Mountains(3);
    board.push_back(kDd);
    const GameState s = MakeGiants({ kDd, kDd, kM, kM, kBeh, kTi, kBo, kBo }, board);
    CHECK(Gi(s) == std::vector<int>{ 1, 7, 3, 6, 4, 0, 5, 2 });
    CHECK(Gen(s).front() == 4);   // generic sheds Borderland Behemoth
}

// Board 3 Mountains + TWO Daredevils: the discount SUMS to {4}. reach 3. Goliath {6}{R} -> 3 (P 1,
// pos 5: 3.11) stays above Harbinger {4}{R} -> 1 (pos 6: 2.64). (A max-of-one discount would price
// Goliath at 5 -- 0.35 x 3.11 = 1.09 -- and keep the Harbinger instead.) Daredevil quota 0, so the hand
// copy is spare; the 3rd Bolt sheds before it (S3 before S4).
TEST_CASE("Giants discard G5: the Daredevil discount stacks per copy in play (SUM)")
{
    std::vector<std::string> board = Mountains(3);
    board.push_back(kDd);
    board.push_back(kDd);
    const GameState s = MakeGiants({ kGol, kHar, kTi, kFl, kDd, kBo, kBo, kBo }, board);
    CHECK(Gi(s) == std::vector<int>{ 7, 4, 1, 6, 5, 0, 3, 2 });
    CHECK(Gen(s).front() == 0);   // generic sheds Hamletback Goliath
}

// Board 1 Mountain + two Daredevils, nothing in hand to add mana: reach 1. The {4} discount is FLOORED
// at the coloured pips: Tectonic {2}{R}{R} costs 2 (1 short: 0.7 x 4.30 = 3.01), Flinger {3}{R}{R} costs
// 2 (0.7 x 5.95 = 4.17), Titan costs 2 (0.7 x 7 = 4.9), Harbinger 1 (castable, 2.64). Kept Giants in
// order Titan, Flinger, Tectonic -- unfloored, Flinger (cost 1) would lead and Tectonic (cost 0) pass
// the Titan.
TEST_CASE("Giants discard G6: the discount is floored at the coloured pips")
{
    const GameState s = MakeGiants({ kTec, kTi, kFl, kHar, kGr, kPy, kBo, kDd }, { kM, kDd, kDd });
    CHECK(Gi(s) == std::vector<int>{ 5, 7, 3, 6, 0, 4, 2, 1 });
    CHECK(Gen(s).front() == 1);   // generic sheds Inferno Titan
}

// Borderland Behemoth is board-conditional. Board 5 Mountains (+ Giants); hand Sov0 Fl1 Ti2 Beh3 M4 M5
// Bo6 Gr7; reach 7, every Giant castable. need 1, soft 2.
TEST_CASE("Giants discard G7: Borderland Behemoth is TOP with 2+ Giants in play, LAST with none")
{
    const std::vector<std::string> hand = { kSov, kFl, kTi, kBeh, kM, kM, kBo, kGr };

    // 2 Giants in play: Behemoth 8.24 > Titan 7 > Flinger 5.95 > Sovereign 3.65 -> Sovereign is spare.
    std::vector<std::string> two = Mountains(5);
    two.push_back(kTec);
    two.push_back(kHar);
    const GameState s2 = MakeGiants(hand, two);
    CHECK(Gi(s2) == std::vector<int>{ 0, 5, 6, 1, 7, 2, 3, 4 });
    CHECK(Gen(s2).front() == 3);   // generic sheds the Behemoth -- here the best card in the hand

    // None in play: Behemoth is last (2.24 < Sovereign 3.65) -> the Behemoth is spare.
    const GameState s0 = MakeGiants(hand, Mountains(5));
    CHECK(Gi(s0) == std::vector<int>{ 3, 5, 6, 0, 7, 1, 2, 4 });

    // Exactly one in play: its listed place, between Flinger and Tectonic (5.06 > Sovereign 3.65).
    std::vector<std::string> one = Mountains(5);
    one.push_back(kTec);
    const GameState s1 = MakeGiants(hand, one);
    CHECK(Gi(s1) == std::vector<int>{ 0, 5, 6, 3, 7, 1, 2, 4 });
}

// Board 7 Mountains: everything castable, so the order is the USER's own: Titan > Flinger > Tectonic >
// Sovereign > Goliath > Harbinger. Keep 3 (Titan, Flinger, Tectonic); the spares shed lowest first.
TEST_CASE("Giants discard G8: the USER's Giant order -- Flinger 2nd, Sovereign below Tectonic")
{
    const GameState s = MakeGiants({ kHar, kGol, kSov, kTec, kFl, kTi, kPy, kBo }, Mountains(7));
    CHECK(Gi(s) == std::vector<int>{ 6, 0, 1, 2, 7, 3, 4, 5 });
    CHECK(Gen(s).front() == 1);   // generic sheds Hamletback Goliath
}

// A second copy in HAND must be paid for on top of the first. reach 7: the 2nd Titan needs 12 (5 short,
// 0.02 x 7 = 0.14) and falls below the Harbinger (2.64). A copy IN PLAY is already paid for and adds
// nothing (review fix 2026-10-09): beside a resolved Titan the hand Titan is castable (6 <= reach 7), so
// it is kept as the first Giant and the Harbinger -- the 4th Giant -- sheds first.
TEST_CASE("Giants discard G9: duplicates in hand pay cumulative mana; a copy in play does not")
{
    const GameState s = MakeGiants({ kTi, kTi, kHar, kM, kM, kBo, kGr, kFl }, Mountains(5));
    CHECK(Gi(s) == std::vector<int>{ 1, 4, 5, 2, 6, 7, 0, 3 });

    std::vector<std::string> board = Mountains(5);
    board.push_back(kTi);
    const GameState t = MakeGiants({ kTi, kHar, kM, kM, kBo, kGr, kFl, kTec }, board);
    CHECK(Gi(t) == std::vector<int>{ 1, 3, 4, 7, 5, 6, 0, 2 });
    CHECK(Gen(t).front() == 0);
}

// No land in play: the FIRST mana slot takes a land, then Sol Ring, then Fire Diamond (pick list M2
// Sol0 FD1; reach 4: Flinger 1 short 4.17 > Titan 2 short 2.45). With a land in play Sol Ring leads
// the pick list instead (Sol0 M2 FD1), so it is the last mana card given up.
TEST_CASE("Giants discard G10: a land first while none is in play; Sol Ring > lands > Fire Diamond")
{
    const std::vector<std::string> hand = { kSol, kFD, kM, kTi, kFl, kDd, kBo, kGr };
    const GameState s = MakeGiants(hand, {});
    CHECK(Gi(s) == std::vector<int>{ 6, 7, 1, 3, 0, 5, 4, 2 });
    CHECK(Gen(s).front() == 3);

    const GameState t = MakeGiants(hand, { kM });
    CHECK(Gi(t) == std::vector<int>{ 6, 7, 1, 3, 2, 5, 4, 0 });
}

// MTG_GIANTS_ROCK_AFTER_LANDS pinned per case (default ON).
std::vector<int> GiRock(const GameState& st, int lever)
{
    heurarm::t_arm[heurarm::GIANTS_ROCK_AFTER_LANDS] = static_cast<std::int8_t>(lever);
    std::vector<int> v = Gi(st);
    heurarm::t_arm[heurarm::GIANTS_ROCK_AFTER_LANDS] = -1;
    return v;
}

// No land in play and none in hand (giants regression gi 130's shape). USER 2026-10-10: "It makes no
// sense to treat it as dead because they are better than no mana source, but it does make sense to
// prioritize lands over rocks when we cannot cast the rocks." Lever ON: both rocks are mana sources,
// after every land (there is none): Sol Ring is the first mana slot and Fire Diamond the second, and
// their 3 mana count toward reach (the table, dist 0): Flinger (5) is 2 short -> 0.35 x 7(0.85) = 2.08,
// Titan (6) 3 short -> 0.10 x 7 = 0.70, Behemoth (6, no Giant in play: last) 3 short. The ladder reads
// Sol, Flinger, Daredevil, Fire Diamond, Titan, Greaves, Behemoth and sheds Pyroclasm, then that ladder
// backwards. Lever OFF (the first build): both rocks are blanks (S0, after the Pyroclasm; Fire Diamond
// first) and fill no mana slot. A land left in the library changes neither arm.
TEST_CASE("Giants discard G11: an uncastable rock is still a mana source (=0: the dead-rock blank)")
{
    const GameState s = MakeGiants({ kSol, kFD, kTi, kFl, kDd, kBo, kGr, kPy }, {});
    CHECK(GiRock(s, 1) == std::vector<int>{ 7, 5, 6, 2, 1, 4, 3, 0 });
    CHECK(Gi(s)        == std::vector<int>{ 7, 5, 6, 2, 1, 4, 3, 0 });   // default ON
    CHECK(GiRock(s, 0) == std::vector<int>{ 7, 1, 0, 5, 6, 3, 4, 2 });   // the dead-rock rule as first built
    CHECK(Gen(s).front() == 2);
    const GameState lib = MakeGiants({ kSol, kFD, kTi, kFl, kDd, kBo, kGr, kPy }, {}, { kM, kTi });
    CHECK(GiRock(lib, 1) == std::vector<int>{ 7, 5, 6, 2, 1, 4, 3, 0 });
    CHECK(GiRock(lib, 0) == std::vector<int>{ 7, 1, 0, 5, 6, 3, 4, 2 });
}

// "Prioritize lands over rocks when we cannot cast the rocks." No land in play and two enters-tapped
// lands in hand: next turn's land comes down tapped, so Sol Ring ({1}) cannot be cast next turn and
// ranks after BOTH lands (pick list Sands1, Sands2, Sol0) -- read backwards, it is given up before the
// second land. Lever OFF: a land in hand made the rock live in its usual place, Sol Ring > lands (pick
// list Sands1, Sol0, Sands2), so the second land went first. Reach (dist 0) counts the same three mana
// cards either way and all three sit in MANA slots (need 6), so the orders differ only in that swap.
TEST_CASE("Giants discard G11b: a rock we cannot cast next turn ranks after every land")
{
    const char* kSands = "Blossoming Sands";   // enters tapped
    const GameState s = MakeGiants({ kSol, kSands, kSands, kTi, kFl, kDd, kBo, kPy }, {});
    auto pos = [](const std::vector<int>& v, int idx)
    { return static_cast<int>(std::find(v.begin(), v.end(), idx) - v.begin()); };
    const std::vector<int> on  = GiRock(s, 1);
    const std::vector<int> off = GiRock(s, 0);
    CheckPermutation(s, on);
    CheckPermutation(s, off);
    CHECK(on.front() == 7);
    CHECK(off.front() == 7);
    CHECK(pos(on, 0) < pos(on, 2));     // Sol Ring given up before the second land
    CHECK(pos(off, 2) < pos(off, 0));   // =0: the second land before Sol Ring
    std::vector<int> swapped = on;
    for (int& v : swapped) { v = v == 0 ? 2 : (v == 2 ? 0 : v); }
    CHECK(swapped == off);
    // With an untapped land drop in hand the rock is castable next turn and keeps its usual place.
    const GameState t = MakeGiants({ kSol, kM, kM, kTi, kFl, kDd, kBo, kPy }, {});
    CHECK(GiRock(t, 1) == GiRock(t, 0));
}

// Board 5 Mountains + Greaves: haste quota 0, so the hand Greaves is spare (S2) -- after the Mountain
// past the soft 7th (S1) and before the 3rd Bolt (S3).
TEST_CASE("Giants discard G12: a Greaves in play makes the hand copy spare; S1 > S2 > S3")
{
    std::vector<std::string> board = Mountains(5);
    board.push_back(kGr);
    const GameState s = MakeGiants({ kGr, kBo, kBo, kBo, kM, kM, kM, kTi }, board);
    CHECK(Gi(s) == std::vector<int>{ 6, 0, 3, 2, 5, 1, 7, 4 });
    CHECK(Gen(s).front() == 7);   // generic sheds Inferno Titan
}

// No Daredevil in play: the quota is 2, so the 3rd copy is spare (S4), right after the Pyroclasm.
TEST_CASE("Giants discard G13: Daredevil quota 2 while none is in play")
{
    const GameState s = MakeGiants({ kDd, kDd, kDd, kTi, kFl, kM, kBo, kPy }, Mountains(3));
    CHECK(Gi(s) == std::vector<int>{ 7, 2, 6, 1, 3, 0, 4, 5 });
    CHECK(Gen(s).front() == 3);
}

TEST_CASE("Giants discard G14: MTG_GIANTS_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeGiants({ kM, kM, kM, kDd, kTi, kSov, kBo, kPy }, Mountains(2));
    const std::vector<int> off = GiArm(s, 0);
    const std::vector<int> on  = GiArm(s, 1);
    CHECK(off == Gen(s));
    CHECK(off.front() == 4);   // the old max-MV answer: Inferno Titan
    CHECK(on.front() == 7);    // the policy: Pyroclasm
    CHECK(on != off);          // the control that MUST differ
}

TEST_CASE("Giants discard G15: a staged card is never named by the policy and sorts last")
{
    GameState s = MakeGiants({ kM, kM, kM, kDd, kTi, kSov, kBo, kPy }, Mountains(2));
    s.players[0].hand[4].m_is_staged = true;   // as if exiled by Tectonic Giant's impulse mode
    const std::vector<int> o = Gi(s);
    CHECK(o == std::vector<int>{ 7, 6, 2, 1, 3, 5, 0, 4 });
    CheckPermutation(s, o);
}

// The implementation contract's duplicate-legend rule (inert on this decklist, which has no legend):
// a copy on our battlefield, or a 2nd copy in hand, is dead and sheds in S0.
TEST_CASE("Giants discard G16: a duplicate legend is dead")
{
    const char* kVito = "Vito, Thorn of the Dusk Rose";
    std::vector<std::string> board = Mountains(4);
    board.push_back(kVito);
    const GameState s = MakeGiants({ kVito, kTi, kFl, kM, kM, kBo, kGr, kDd }, board);
    const std::vector<int> o = Gi(s);
    CHECK(o.front() == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 1);

    const GameState t = MakeGiants({ kTi, kVito, kVito, kM, kM, kBo, kGr, kDd }, Mountains(4));
    const std::vector<int> u = Gi(t);
    CHECK(u.front() == 2);
    CheckPermutation(t, u);
}

TEST_CASE("Giants discard G17: every hand index is named exactly once on every board above")
{
    const std::vector<std::vector<std::string>> hands = {
        { kM, kM, kM, kDd, kTi, kSov, kBo, kPy }, { kM, kM, kM, kM, kM, kTi, kGol, kSol },
        { kFD, kM, kSol, kM, kTi, kFl, kBo, kGr }, { kGol, kHar, kTi, kFl, kDd, kBo, kBo, kBo },
        { kSol, kFD, kTi, kFl, kDd, kBo, kGr, kPy },
    };
    const std::vector<std::vector<std::string>> boards = {
        Mountains(2), Mountains(3), Mountains(7), { kM, kDd, kDd }, {},
    };
    for (const auto& h : hands)
    {
        for (const auto& b : boards)
        {
            const GameState s = MakeGiants(h, b);
            CheckPermutation(s, Gi(s));
            const GameState t = MakeGiants(h, b, Lib(24, 21));
            CheckPermutation(t, GiD(t));
        }
    }
}

// ---- the shared distance model (MTG_DISCARD_DIST_MODEL, default ON) ------------------------------------
// Libraries are `Lib(n, lands)`: `lands` Mountains then Lightning Bolts. With no land in the library a
// drawn land never comes, so only the board, the kept lands and the kept rocks count -- which isolates
// each input. Castability for "first castable on our t-th next turn" (no draws involved) is
// 0.75^(t-1): 1, .75, .5625, .421875; never castable = 0.02 x 0.75^4 = 0.0063281.

// D1. Board 3 Mountains, hand M0 M1 M2 Ti3 Fl4 Tec5 Har6 Gol7, no land in the library. need 3 -> all three
// Mountains kept, played one per turn: mana 4 / 5 / 6 / 6. Tectonic (4) T1 -> 1 x 4.30 = 4.30; Flinger
// (5) T2 -> .75 x 5.95 = 4.46; Titan (6) T3 -> .5625 x 7 = 3.94; Harbinger (5) T2 -> .75 x 2.64 = 1.98;
// Goliath (7) never -> 0.02. Kept Giants Fl, Tec, Ti (ladder slots 2/5/9); Goliath, then Harbinger spare.
// The table counted all three Mountains at once (reach 6): Titan, Flinger, Tectonic all P 1 -- Titan
// first in the keep order, so Tectonic, not Titan, was the last Giant kept.
TEST_CASE("Giants discard D1: the kept lands arrive one per turn -- Titan three turns out ranks below Flinger and Tectonic")
{
    const GameState s = MakeGiants({ kM, kM, kM, kTi, kFl, kTec, kHar, kGol }, Mountains(3), Lib(20, 0));
    CHECK(GiD(s) == std::vector<int>{ 7, 6, 3, 2, 5, 1, 4, 0 });
    CHECK(Gi(s)  == std::vector<int>{ 7, 6, 5, 2, 4, 1, 3, 0 });   // the table (=0)
}

// D2. Board 3 Mountains, hand Sol0 FD1 M2 Ti3 Fl4 Tec5 Gol6 Har7, no land in the library. need 3 -> keep
// Sol Ring, the Mountain and Fire Diamond. Next turn the board's 3 + the Mountain = 4 mana: Sol Ring
// ({1}, untapped, 2) is cast and nets +1; Fire Diamond ({2}, tapped) is cast too and adds 1 from the turn
// after. Extra = 1 / 3 / 3 / 3, so mana = 5 / 7 / 7 / 7. Flinger (5) and Tectonic (4) T1; Titan and
// Goliath (7) T2: Fl 5.95 > Ti 5.25 > Tec 4.30 > Har 2.64 > Gol .75 x 3.11 = 2.33. The table (reach 7,
// all at once) had Titan first and Goliath (3.11) above Harbinger -- so it shed the Harbinger first.
// (Without the rocks' mana, Titan / Flinger / Goliath would never be castable at all.)
TEST_CASE("Giants discard D2: kept Sol Ring / Fire Diamond add mana from the turn they can be cast")
{
    const GameState s = MakeGiants({ kSol, kFD, kM, kTi, kFl, kTec, kGol, kHar }, Mountains(3), Lib(20, 0));
    CHECK(GiD(s) == std::vector<int>{ 6, 7, 5, 1, 3, 2, 4, 0 });
    CHECK(Gi(s)  == std::vector<int>{ 7, 6, 5, 1, 4, 2, 3, 0 });   // the table (=0)
}

// D3. Board 6 Mountains, no mana in hand: Har0 Gol1 Ti2 Fl3 Bo4 Bo5 Gr6 Sov7. Every Giant but Goliath (7)
// is castable now. Goliath needs ONE drawn land; with 15 lands in a 20-card library P(by T1..T4) =
// 1 - C(5,t)/C(20,t) = .75, .947368, .991228, .998968, so castability = .75 + .197368 x .75 + .043860 x
// .5625 + .007740 x .421875 + .001032 x .006328 = .925969 and EV = 3.1059 x .925969 = 2.876 > Harbinger
// 2.640: the Harbinger is the first spare. With no land in the library Goliath is stranded and sheds
// first -- as under the table, which priced it 1 short at 0.70 (2.174).
TEST_CASE("Giants discard D3: the library's land density decides how far one land short really is")
{
    const std::vector<std::string> hand = { kHar, kGol, kTi, kFl, kBo, kBo, kGr, kSov };
    const GameState rich = MakeGiants(hand, Mountains(6), Lib(20, 15));
    CHECK(GiD(rich) == std::vector<int>{ 0, 1, 5, 4, 7, 6, 3, 2 });
    CHECK(Gi(rich)  == std::vector<int>{ 1, 0, 5, 4, 7, 6, 3, 2 });   // the table (=0)
    const GameState dry = MakeGiants(hand, Mountains(6), Lib(20, 0));
    CHECK(GiD(dry)  == std::vector<int>{ 1, 0, 5, 4, 7, 6, 3, 2 });
}

// D4. Board 3 Mountains + a Daredevil, hand Ti0 Ti1 Fl2 Har3 Gol4 M5 M6 Bo7, no land in the library.
// Target 4 -> need 1, soft 2: both Mountains kept, mana 4 / 5 / 5 / 5. The model is given the cost AFTER
// the Daredevil ({2} off) and CUMULATIVE for the 2nd Titan in hand: Titan 4 (T1, 7), Flinger 3 (T1,
// 5.95), Harbinger 3 (T1, 2.64), Goliath 5 (T2, .75 x 3.11 = 2.33), 2nd Titan 8 (never, .044). Kept Ti,
// Fl, Har; spares: the 2nd Titan, then Goliath. The table (reach 5) had Goliath castable (3.11) over
// Harbinger and so kept it.
TEST_CASE("Giants discard D4: the model prices the Daredevil-discounted, cumulative cost")
{
    std::vector<std::string> board = Mountains(3);
    board.push_back(kDd);
    const GameState s = MakeGiants({ kTi, kTi, kFl, kHar, kGol, kM, kM, kBo }, board, Lib(20, 0));
    CHECK(GiD(s) == std::vector<int>{ 1, 4, 6, 7, 3, 2, 0, 5 });
    CHECK(Gi(s)  == std::vector<int>{ 1, 3, 6, 7, 4, 2, 0, 5 });   // the table (=0)
}

// ---- Giant Harbinger's tutor width (MTG_GIANTS_TUTOR_WIDTH, default ON) -------------------------------
namespace
{
int GiantsWidth(int arm)
{
    heurarm::t_arm[heurarm::GIANTS_TUTOR_WIDTH] = arm;
    const int w = GiantsProvider().TutorSearchWidth();
    heurarm::t_arm[heurarm::GIANTS_TUTOR_WIDTH] = -1;
    return w;
}
}   // namespace

TEST_CASE("Giants tutor T1: TutorSearchWidth is 7 (one per Giant name); =0 restores the base 6")
{
    CHECK(GiantsWidth(1) == 7);
    CHECK(GiantsWidth(0) == 6);
    CHECK(GenericProvider().TutorSearchWidth() == 6);   // the base it widens -- moot if that ever passes 7
}

// The decklist's mainboard holds exactly seven distinct Giant names (cards.json: Inferno Titan, Sunrise
// Sovereign, Giant Harbinger, Borderland Behemoth, Hamletback Goliath, Surtland Flinger, Tectonic Giant;
// Stinkdrinker Daredevil is a Goblin Rogue). With the whole mainboard as the library, Giant Harbinger's
// candidate list is those seven names -- all inside width 7, one outside the base 6.
TEST_CASE("Giants tutor T2: every Giant name Giant Harbinger can fetch is inside the width")
{
    EnsureCardsGd();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Giants/Giants.cod"));
    GameState s;
    s.active_player_index = 0;
    for (const Card& c : deck.mainboard) { s.players[0].library.push_back(c); }
    const CardDefinition* harbinger = CardDatabase::Instance().Lookup(kHar);
    REQUIRE(harbinger != nullptr);
    REQUIRE(harbinger->params.tutor_to_top);
    const std::vector<std::string> cands = GiantsProvider().TutorCandidates(s, 0, harbinger->params);
    std::vector<std::string> sorted = cands;
    std::sort(sorted.begin(), sorted.end());
    const std::vector<std::string> want = { kBeh, kHar, kGol, kTi, kSov, kFl, kTec };   // alphabetical
    CHECK(sorted == want);
    CHECK(static_cast<int>(cands.size()) <= GiantsWidth(1));
    CHECK(static_cast<int>(cands.size()) > GiantsWidth(0));   // the hole the override closes
}
