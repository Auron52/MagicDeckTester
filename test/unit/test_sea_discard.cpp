// Unit tests for the Sea Creatures cleanup-discard buckets: SeaCreaturesProvider::CleanupDiscardCandidates, the USER's
// rule and its refinements of 2026-11-18 (docs/design/sea-creatures-discard-policy-proposal.md, "USER RULED").
//
// Every expected order was derived BY HAND from the rule as built (the header block above the implementation in
// DecisionProviders.cpp). Values: P + T (Serpent / Leviathan / Kraken 12, Hullbreaker 15, Kiora's token 16, Koma 20,
// Lorthos 16, Sin 10 + its Saga line, Trench Gorger 2 x library lands at >= 6); castability by mana short of the kept
// reach {1, .70, .35, .10, .03, .02}. Shed order: Wave > dead legend > hostless land Aura > mana past 8 > 4th+ threats >
// rest > mana between the floor and 8 > the 3rd threat > floor mana backwards > T2 > T1.
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

void EnsureCardsSd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Hand cards are NAME-ONLY placeholders in real play; build them that way.
Card PhSd(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct BoardCard { std::string name; int lore = 0; };

GameState MakeSd(const std::vector<std::string>& hand, const std::vector<BoardCard>& board,
                 const std::vector<std::string>& library = {}, int opp_life = 20)
{
    EnsureCardsSd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhSd(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhSd(n, num++)); }
    int first_land = 0;
    for (const BoardCard& b : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(b.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.lore_counters     = b.lore;
        if (p.card.IsLand() && first_land == 0) { first_land = p.card.m_number; }
        const CardDefinition* d = CardDatabase::Instance().Lookup(b.name);
        if (d->params.is_land_aura) { p.aura_attached_to = first_land; }
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> SeaArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::SEA_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = SeaCreaturesProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::SEA_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Sd(const GameState& s)   { return SeaArm(s, 1); }
std::vector<int> GenSd(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermSd(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kFo  = "Forest";
const char* kIs  = "Island";
const char* kSol = "Sol Ring";
const char* kWG  = "Wild Growth";
const char* kOG  = "Overgrowth";
const char* kPS  = "Worn Powerstone";
const char* kSer = "Serpent of Yawning Depths";
const char* kLev = "Summon: Leviathan";
const char* kKr  = "Spawning Kraken";
const char* kHb  = "Hullbreaker Horror";
const char* kKo  = "Koma, World-Eater";
const char* kKi  = "Kiora Bests the Sea God";
const char* kSin = "Sin, Unending Cataclysm";
const char* kLo  = "Lorthos, the Tidemaker";
const char* kGo  = "Trench Gorger";
const char* kWa  = "Whelming Wave";

std::vector<BoardCard> Forests(int k) { return std::vector<BoardCard>(static_cast<std::size_t>(k), BoardCard{ kFo, 0 }); }
std::vector<std::string> LibForests(int k) { return std::vector<std::string>(static_cast<std::size_t>(k), kFo); }

}   // namespace

// Wave first; the =0 hatch is exactly the generic max-MV ranking (which sheds Hullbreaker, MV 7, first).
TEST_CASE("Sea discard: Whelming Wave is shed first; MTG_SEA_BUCKET_DISCARD=0 restores the generic ranking")
{
    const GameState s = MakeSd({ kWa, kFo, kSol, kSer, kHb, kIs, kWG, kOG }, Forests(2));
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    REQUIRE(!v.empty());
    CHECK(v.front() == 0);
    CHECK(GenSd(s).front() == 4);
    CHECK(SeaArm(s, 0) == GenSd(s));
}

// Board 6 Forests. p0 = Forest, Sol, Island: reach 10. Kraken T1, Koma T2 (refinement 10: Kraken -> Koma when held);
// need 7, later. Rest on merit: Gorger 40 (T3), Lorthos 16 (MV 8 covered, no uncovered alternative), Kiora 16. Mana:
// Forest (floor, t 7), Sol (to 9), Island past 8. Shed: Island, Kiora, Lorthos, Sol, Gorger, Forest, Koma, Kraken.
TEST_CASE("Sea discard: Spawning Kraken and Koma are auto-keeps over bigger threats")
{
    const GameState s = MakeSd({ kKr, kKo, kGo, kLo, kKi, kFo, kIs, kSol }, Forests(6), LibForests(20));
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 6, 4, 3, 7, 2, 5, 1, 0 });
}

TEST_CASE("Sea discard: a second Koma is shed right after the Wave")
{
    std::vector<BoardCard> board = Forests(5);
    board.push_back({ kKo, 0 });
    const GameState s = MakeSd({ kKo, kFo, kSer, kHb, kSol, kIs, kWG, kKi }, board);
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v.front() == 0);

    const GameState s2 = MakeSd({ kKo, kFo, kSer, kKo, kSol, kIs, kWG, kHb }, Forests(5));
    const std::vector<int> v2 = Sd(s2);
    CheckPermSd(s2, v2);
    CHECK(v2.front() == 3);    // the later copy is the dead one
    CHECK(v2.back() == 0);     // the first is the auto-kept T1
}

// Board 6. Sol / Overgrowth / Powerstone + Forest: t 6,7,9,11,13 -> floor 1 card (Forest), mid Sol, past 8 Overgrowth
// and Powerstone. Pair (relaxed, reach 13): Kiora + Hullbreaker 31 is same-MV; Kiora + Serpent 28 >= 0.8 x 31 wins
// on the mix of costs. need 7, later; Hullbreaker T3, Leviathan T4.
TEST_CASE("Sea discard: mana past 8 sheds before the 3rd threat; the mix of costs keeps a 6 beside a 7")
{
    const GameState s = MakeSd({ kFo, kSol, kOG, kPS, kSer, kLev, kHb, kKi }, Forests(6));
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 3, 2, 5, 1, 6, 0, 4, 7 });
}

// Empty board, 7 lands + Serpent + Hullbreaker (shed 2): the floor of 2 threats holds; the 6th and 7th lands go.
TEST_CASE("Sea discard: the floor of two threats is kept")
{
    const GameState s = MakeSd({ kFo, kFo, kFo, kFo, kIs, kIs, kIs, kSer, kHb }, {});
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 6, 5, 4, 3, 2, 1, 0, 8, 7 });
}

// Refinement 2 (USER: "5 mana and 2 threats ... can shift to 4 and 3 when we have more mana out"). Same hand: five
// lands + three 6-drops (need 6). Board 1: 1 + 4 = 5 < 6 -> 5/2, the 3rd threat goes first. Board 2: 2 + 4 = 6 -> 4/3,
// the 5th mana card goes first.
TEST_CASE("Sea discard: 5 mana + 2 threats early, 4 + 3 once board + 4 mana cards reach 6")
{
    const std::vector<std::string> hand = { kFo, kIs, kFo, kIs, kFo, kSer, kLev, kSer };
    const GameState early = MakeSd(hand, Forests(1));
    const std::vector<int> ve = Sd(early);
    CheckPermSd(early, ve);
    CHECK(ve == std::vector<int>{ 7, 4, 3, 2, 1, 0, 6, 5 });

    const GameState later = MakeSd(hand, Forests(2));
    const std::vector<int> vl = Sd(later);
    CheckPermSd(later, vl);
    CHECK(vl == std::vector<int>{ 4, 7, 3, 2, 1, 0, 6, 5 });
}

// Refinement 3 (USER: "we may need to keep 7 mana before we start making more than 2 spots for theats"). Kept pair
// Hullbreaker + Serpent -> need 7. Board 2: 2 + 4 = 6 < 7 stays 5/2 (Leviathan, the 3rd threat, first). Board 3:
// 3 + 4 = 7 -> 4/3 (the 5th land first).
TEST_CASE("Sea discard: with a kept 7-drop the 3rd threat slot opens only at 7 mana")
{
    const std::vector<std::string> hand = { kFo, kIs, kFo, kIs, kFo, kHb, kSer, kLev };
    const GameState at6 = MakeSd(hand, Forests(2));
    const std::vector<int> v6 = Sd(at6);
    CheckPermSd(at6, v6);
    CHECK(v6 == std::vector<int>{ 7, 4, 3, 2, 1, 0, 6, 5 });

    const GameState at7 = MakeSd(hand, Forests(3));
    const std::vector<int> v7 = Sd(at7);
    CheckPermSd(at7, v7);
    CHECK(v7 == std::vector<int>{ 4, 7, 3, 2, 1, 0, 6, 5 });
}

// Refinement 4 (USER: "aim for a 6 and a 7 drop to start, but this isn't required if ... already nearing 8 mana").
// Board 1 + four lands: reach 5. The pair is restricted to a 6 and a 7: Serpent + Hullbreaker (13.65) -- on merit
// alone Leviathan (8.4) would beat Hullbreaker (5.25). Gorger (40 x .10) is the 4th. Board 3: reach 7, relaxed: the
// best pair is Gorger (40 x .7 = 28) + Hullbreaker 15, need 8 (still 5/2), Serpent T3, Leviathan T4.
TEST_CASE("Sea discard: a 6 and a 7 to start; at 7+ mana a bigger 8 may take a slot")
{
    const std::vector<std::string> hand = { kFo, kIs, kFo, kIs, kSer, kLev, kHb, kGo };
    const GameState low = MakeSd(hand, Forests(1), LibForests(20));
    const std::vector<int> vl = Sd(low);
    CheckPermSd(low, vl);
    CHECK(vl == std::vector<int>{ 7, 5, 3, 2, 1, 0, 6, 4 });

    const GameState high = MakeSd(hand, Forests(3), LibForests(20));
    const std::vector<int> vh = Sd(high);
    CheckPermSd(high, vh);
    CHECK(vh == std::vector<int>{ 5, 4, 3, 2, 1, 0, 6, 7 });
}

// Refinement 5 (Sin is conditional). Kiora on the battlefield at lore 1 (survives to next main 2 at lore 2): Sin =
// 10 + 2 x 2 x 2 + 16 = 34 -> T1 with Serpent (mix), Hullbreaker T3 sheds before it. No Saga: Sin = 10 -> the pair is
// Hullbreaker + Serpent and Sin is the 3rd threat.
TEST_CASE("Sea discard: Sin is kept over a comparable threat with Kiora on the battlefield, shed before it without")
{
    const std::vector<std::string> hand = { kFo, kIs, kFo, kIs, kSin, kHb, kSer, kFo };
    std::vector<BoardCard> with_kiora = Forests(4);
    with_kiora.push_back({ kKi, 1 });
    const GameState sk = MakeSd(hand, with_kiora);
    const std::vector<int> vk = Sd(sk);
    CheckPermSd(sk, vk);
    CHECK(vk == std::vector<int>{ 7, 3, 5, 2, 1, 0, 6, 4 });

    const GameState sn = MakeSd(hand, Forests(4));
    const std::vector<int> vn = Sd(sn);
    CheckPermSd(sn, vn);
    CHECK(vn == std::vector<int>{ 7, 3, 4, 2, 1, 0, 6, 5 });
}

// Refinement 6 (USER: "Summon: Leviathan -> Sin ..."). Two slots, board 2 + four lands (reach 6, trending to 7):
// Leviathan + Sin (12 + (10 + 8 + 12) x .7 = 33) beats Leviathan + Hullbreaker (12 + 10.5). Wave first.
TEST_CASE("Sea discard: Summon: Leviathan -> Sin is kept as the opening pair over another 7-drop")
{
    const GameState s = MakeSd({ kFo, kIs, kFo, kIs, kLev, kSin, kHb, kWa }, Forests(2));
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 7, 6, 3, 2, 1, 0, 4, 5 });
}

// Qualifier "(at 20 life anyway)": Leviathan (6/6) on the battlefield at lore 1. Next turn it attacks (6) and Sin,
// cast in main 2, harvests lore 2 -> a 9/9; the turn after Sin + Leviathan attack for 15: 21 through the turn Sin
// attacks. Opponent at 15 or 20: Sin is the finisher, T3 behind Kraken + Koma (refinement 10 fills T2 with Koma) and
// ahead of Gorger (40). At 2HG's 30 it is not: Gorger is T3 and Sin (30) the 4th.
TEST_CASE("Sea discard: Sin as the finisher reads the opponent's actual life")
{
    const std::vector<std::string> hand = { kKr, kKo, kSin, kGo, kFo, kIs, kFo, kIs };
    std::vector<BoardCard> board = Forests(6);
    board.push_back({ kLev, 1 });
    for (int life : { 15, 20 })
    {
        const GameState fin = MakeSd(hand, board, LibForests(20), life);
        const std::vector<int> vf = Sd(fin);
        CheckPermSd(fin, vf);
        CHECK(vf == std::vector<int>{ 7, 6, 3, 5, 2, 4, 1, 0 });
    }

    const GameState no = MakeSd(hand, board, LibForests(20), 30);
    const std::vector<int> vn = Sd(no);
    CheckPermSd(no, vn);
    CHECK(vn == std::vector<int>{ 7, 6, 2, 5, 3, 4, 1, 0 });
}

// Refinement 9 (USER: "on 20-life it is possible that sin + Summon Leviathan is actually better than Summon Leviathan
// + Koma"). Board 2 Forests + five lands in hand: reach4 6 < need 7 -> 5/2, two threat slots. At 20 the held Summon
// line projects 2 x 6 + 5 + 4 = 21: Summon + Sin take T1/T2 ahead of Koma (the 3rd threat, shed first). At 2HG's 30
// it does not: Koma T1, then (relaxed, reach5 7) Summon 12 on merit over a plain Sin 10; Sin is the 3rd threat.
TEST_CASE("Sea discard: at 20 life Summon + Sin beat Summon + Koma for two slots; at 30 Koma stays")
{
    const std::vector<std::string> hand = { kFo, kIs, kFo, kIs, kFo, kLev, kSin, kKo };
    const GameState s20 = MakeSd(hand, Forests(2), {}, 20);
    const std::vector<int> v20 = Sd(s20);
    CheckPermSd(s20, v20);
    CHECK(v20 == std::vector<int>{ 7, 4, 3, 2, 1, 0, 6, 5 });

    const GameState s30 = MakeSd(hand, Forests(2), {}, 30);
    const std::vector<int> v30 = Sd(s30);
    CheckPermSd(s30, v30);
    CHECK(v30 == std::vector<int>{ 6, 4, 3, 2, 1, 0, 5, 7 });
}

// Refinement 10 (USER: "It's more like we should keep Koma or the biggest 7-drop if we have Spawning Kraken."). Board 2
// Forests + five lands: reach5 7 (relaxed), 5/2. No Koma: the two 7-drops are Kiora (token 16) and Hullbreaker (15) ->
// Kiora is T2, although Gorger (40 x .7 = 28) would win on castability-weighted merit (it is T3 and sheds before the
// 5th land; Hullbreaker the 4th).
TEST_CASE("Sea discard: with Spawning Kraken, no Koma, the biggest 7-drop takes the second slot")
{
    const GameState s = MakeSd({ kFo, kIs, kFo, kIs, kFo, kKr, kHb, kKi, kGo }, Forests(2), LibForests(20));
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 6, 8, 4, 3, 2, 1, 0, 7, 5 });
}

// Refinement 10, conditional value: Kiora on the battlefield at lore 1 makes Sin 10 + 8 + 16 = 34. With Koma held,
// Koma is T2 regardless (Sin the 3rd threat); with Hullbreaker (15) instead, Sin is the biggest 7-drop and is T2.
// (Sin is not a finisher here: no creature on board, 5 + 4 < 20.)
TEST_CASE("Sea discard: with Spawning Kraken, Koma takes the second slot over a bigger 7-drop")
{
    std::vector<BoardCard> board = Forests(2);
    board.push_back({ kKi, 1 });
    const GameState sk = MakeSd({ kFo, kIs, kFo, kIs, kFo, kKr, kKo, kSin }, board);
    const std::vector<int> vk = Sd(sk);
    CheckPermSd(sk, vk);
    CHECK(vk == std::vector<int>{ 7, 4, 3, 2, 1, 0, 6, 5 });

    const GameState sh = MakeSd({ kFo, kIs, kFo, kIs, kFo, kKr, kHb, kSin }, board);
    const std::vector<int> vh = Sd(sh);
    CheckPermSd(sh, vh);
    CHECK(vh == std::vector<int>{ 6, 4, 3, 2, 1, 0, 7, 5 });
}

// Refinements 7 + 8 (cheap acceleration + ONE 3-mana accelerant early; a land drop per turn). Empty board, two
// threats, Forest + Island + Sol / Wild Growth / Overgrowth / Worn Powerstone: core = Sol, Wild Growth, Overgrowth
// (5); need 7 -> 2 lands. Order Forest, Sol, WG, Overgrowth, Island, Powerstone: board + 4 = 6 < 7 -> 5/2, so the
// 2nd MV-3 accelerant (Powerstone) is the one mana card shed.
TEST_CASE("Sea discard: early, the cheap accelerants and one 3-mana accelerant are kept; the 2nd 3-drop sheds")
{
    const GameState s = MakeSd({ kFo, kSol, kWG, kOG, kPS, kSer, kHb, kIs }, {});
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v.front() == 4);
    CHECK(v == std::vector<int>{ 4, 7, 3, 2, 1, 0, 5, 6 });
}

// Three lands, Sol + Wild Growth + Overgrowth, Serpent + Hullbreaker, empty board: board + core = 5, need 7 -> two
// land drops (2 lands kept); the third land is surplus and sheds before any accelerant.
TEST_CASE("Sea discard: a land drop per turn until the threats are castable, clamped to 1-2 lands")
{
    const GameState s = MakeSd({ kFo, kIs, kFo, kSol, kWG, kOG, kSer, kHb }, {});
    const std::vector<int> v = Sd(s);
    CheckPermSd(s, v);
    CHECK(v == std::vector<int>{ 2, 1, 5, 4, 3, 0, 6, 7 });
}
