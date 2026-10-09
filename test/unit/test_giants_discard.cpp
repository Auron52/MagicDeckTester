// Unit tests for the Giants cleanup-discard buckets: GiantsProvider::CleanupDiscardCandidates, the
// USER-settled design of 2026-10-09 (docs/design/giants-discard-policy-proposal.md, "Giants: SETTLED").
//
// Every expected order below was derived BY HAND from the design text -- the keep ladder
//   MANA, GIANT, DAREDEVIL, MANA, GIANT, MANA, GREAVES, DAREDEVIL2, GIANT3, MANA, BOLT, MANA, MANA,
//   MANA7 (soft), BOLT2 (soft)
// and the shed order S0 Pyroclasm / dead rock > S1 mana past the 7th (Fire Diamond, Mountains, Sol Ring)
// > S2 spare Greaves > S3 3rd/4th Bolt > S4 spare Daredevils > S5 spare Giants (lowest discounted value
// first) > the ladder read backwards -- with the Giants valued 7 x 0.85^position in the USER's order
// (Titan 0, Flinger 1, Behemoth 2 [-1 with 2+ Giants in play, 7 with none], Tectonic 3, Sovereign 4,
// Goliath 5, Harbinger 6) times P(play) = {1, .70, .35, .10, .03, .02} by mana deficit. The derivation
// is written next to each case. Every case also asserts the GENERIC fallback's index 0 (the control
// arm), and the hatch case proves MTG_GIANTS_BUCKET_DISCARD=0 restores exactly the generic ranking.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

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

GameState MakeGiants(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsGd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
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

std::vector<int> GiArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::GIANTS_BUCKET_DISCARD] = arm;
    std::vector<int> v = GiantsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::GIANTS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Gi(const GameState& s)  { return GiArm(s, 1); }
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

// A second copy must be paid for on top of the first. reach 7: the 2nd Titan needs 12 (5 short, 0.02 x
// 7 = 0.14) and falls below the Harbinger (2.64). A copy IN PLAY counts the same way.
TEST_CASE("Giants discard G9: duplicates pay cumulative mana, counting the copy in play")
{
    const GameState s = MakeGiants({ kTi, kTi, kHar, kM, kM, kBo, kGr, kFl }, Mountains(5));
    CHECK(Gi(s) == std::vector<int>{ 1, 4, 5, 2, 6, 7, 0, 3 });

    std::vector<std::string> board = Mountains(5);
    board.push_back(kTi);
    const GameState t = MakeGiants({ kTi, kHar, kM, kM, kBo, kGr, kFl, kTec }, board);
    CHECK(Gi(t) == std::vector<int>{ 0, 3, 4, 1, 5, 7, 6, 2 });
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

// No land in play and none in hand: both rocks are blanks (S0, after the Pyroclasm; Fire Diamond
// first), and they fill no mana slot.
TEST_CASE("Giants discard G11: a rock with no land anywhere is a blank")
{
    const GameState s = MakeGiants({ kSol, kFD, kTi, kFl, kDd, kBo, kGr, kPy }, {});
    CHECK(Gi(s) == std::vector<int>{ 7, 1, 0, 5, 6, 3, 4, 2 });
    CHECK(Gen(s).front() == 2);
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
        }
    }
}
