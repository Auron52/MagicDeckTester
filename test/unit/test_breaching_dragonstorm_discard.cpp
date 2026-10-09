// Unit tests for the USER-settled BreachingDragonstorm cleanup-discard buckets (2026-10-09):
// BreachingDragonstormProvider::CleanupDiscardCandidates, implementing
// docs/design/breaching-dragonstorm-discard-policy-proposal.md "### BreachingDragonstorm: SETTLED".
//   KEEP: only red source -> enabler 1 -> mana to 5 (flat, counting the board) -> enabler 2 ->
//         castable payoff 1 -> payoff 2.
//   SHED: Maelstrom Wanderer duplicates -> payoffs whose colours can't be paid -> lands beyond the
//         target -> extra castable payoffs -> spare enabler (Breaching Dragonstorm before Creative
//         Technique) -> protected.
//   Payoff order: Boarding Party > Sakashima's Protege > Maelstrom Wanderer > Call Forth the Tempest >
//   Annoyed Altisaur. Enablers: Creative Technique > Breaching Dragonstorm.
// Every expected order was derived BY HAND from that text (each case's comment), and every board
// asserts the GENERIC fallback too -- the control arm that must differ somewhere.
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

void EnsureCardsBds()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhBd(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const BreachingDragonstormProvider& BdProv()
{
    static const BreachingDragonstormProvider p;
    return p;
}

const char* kCT    = "Creative Technique";
const char* kBD    = "Breaching Dragonstorm";
const char* kBP    = "Boarding Party";
const char* kProt  = "Sakashima's Protege";
const char* kWand  = "Maelstrom Wanderer";
const char* kCFT   = "Call Forth the Tempest";
const char* kAlti  = "Annoyed Altisaur";
const char* kMtn   = "Mountain";
const char* kRuins = "Dwarven Ruins";
const char* kSkerry = "Saprazzan Skerry";
const char* kBog   = "Peat Bog";
const char* kFarm  = "Remote Farm";
const char* kFerr  = "Ferrous Lake";

GameState MakeBd(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsBds();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &BdProv();
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhBd(n, num++)); }
    for (const char* n : { kCT, kBD, kWand, kMtn, kBog, kAlti })
    { s.players[0].library.push_back(PhBd(n, num++)); }
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

std::vector<int> BdArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::BREACH_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = BdProv().CleanupDiscardCandidates(s, nullptr);   // required_pieces: [] here
    heurarm::t_arm[heurarm::BREACH_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Bd(const GameState& s)    { return BdArm(s, 1); }
std::vector<int> GenBd(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermBd(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

TEST_CASE("BreachingDragonstorm discard: the deck routes to BreachingDragonstormProvider")
{
    EnsureCardsBds();
    const Decklist d = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/BreachingDragonstorm/BreachingDragonstorm.cod"));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "BreachingDragonstorm");
}

// The proposal's worked hand 1. Board Mountain + Peat Bog + Dwarven Ruins = 4 mana; red 3 (Mountain 1 +
// the Ruins' RR crack), black 2, no blue / green. The 2nd Wanderer in hand is a duplicate legend.
// KEEP: (board has red) -> CT #0 -> no land to add -> E2 prefers the other name: BD -> P1 Boarding
// Party (R covered); Protege (UU), Wanderer (G), Altisaur (GG) are stranded; no P2.
// SHED: Wanderer #2 -> stranded worst first: Altisaur, Wanderer, Protege -> spare CT -> BP, BD, CT.
TEST_CASE("BreachingDragonstorm discard B1: the worked hand -- duplicate Wanderer, then the stranded payoffs")
{
    const GameState s = MakeBd({ kCT, kCT, kBD, kWand, kWand, kAlti, kBP, kProt }, { kMtn, kBog, kRuins });
    CHECK(Bd(s) == std::vector<int>{ 4, 5, 3, 7, 1, 6, 2, 0 });
    CHECK(GenBd(s) == std::vector<int>{ 3, 4, 5, 6, 7, 0, 1, 2 });
}

// The proposal's worked hand 2 -- colour screw: Peat Bog + Remote Farm + Peat Bog = 6 mana with no
// R / U / G. Every payoff is stranded, so none is kept (USER: "castable payoff 1"); they shed in the
// USER's order reversed: Altisaur x2, Call Forth, Wanderer, Protege, Boarding Party; then BD, CT.
TEST_CASE("BreachingDragonstorm discard B2: colour screw -- Altisaur first, where max-MV sheds Call Forth")
{
    const GameState s = MakeBd({ kCT, kBD, kBP, kProt, kCFT, kWand, kAlti, kAlti }, { kBog, kFarm, kBog });
    CHECK(Bd(s) == std::vector<int>{ 7, 6, 4, 5, 3, 2, 1, 0 });
    CHECK(GenBd(s).front() == 4);
}

TEST_CASE("BreachingDragonstorm discard B2b: MTG_BREACH_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeBd({ kCT, kBD, kBP, kProt, kCFT, kWand, kAlti, kAlti }, { kBog, kFarm, kBog });
    const std::vector<int> off = BdArm(s, 0);
    const std::vector<int> on  = BdArm(s, 1);
    CHECK(off == GenBd(s));
    CHECK(off == std::vector<int>{ 4, 5, 6, 7, 2, 3, 0, 1 });
    CHECK(on.front() == 7);
    CHECK(on != off);
}

// A Wanderer on the battlefield makes the one in hand dead (the Angels legend ruling). Board: 2
// Mountains + Peat Bog = 4 mana, red 2. KEEP: CT -> the hand Mountain (mana 5, red 3) -> BD -> P1
// Boarding Party, P2 Call Forth ({R}{R}{R} now covered). Protege (UU) and Altisaur (GG) stranded.
TEST_CASE("BreachingDragonstorm discard B3: a Wanderer already in play makes the held one index 0")
{
    const GameState s = MakeBd({ kWand, kAlti, kCT, kBD, kMtn, kBP, kProt, kCFT }, { kMtn, kMtn, kBog, kWand });
    CHECK(Bd(s) == std::vector<int>{ 0, 1, 6, 7, 5, 3, 4, 2 });
    CHECK(GenBd(s).front() == 0);   // max-MV happens to agree (MV 8, the earlier copy)
}

// USER: a SECOND ENABLER is kept ahead of a castable payoff, and stranded payoffs go BEFORE surplus land.
// Board 2 Mountains + Peat Bog = 4 mana, red 2. Hand lands by burst: Remote Farm (2) > Mountain >
// Ferrous Lake (live filter). KEEP: CT -> Farm (mana 6) -> BD -> P1 Boarding Party. Call Forth needs
// RRR: the board's 2 plus the KEPT lands' 0 -> stranded (agent reading (d)); Altisaur stranded.
// SHED: Altisaur, Call Forth -> surplus Ferrous, Mountain -> BP -> BD -> Farm -> CT.
TEST_CASE("BreachingDragonstorm discard B4: stranded payoffs before surplus lands; the 2nd enabler outlasts the payoff")
{
    const GameState s = MakeBd({ kCT, kBD, kBP, kCFT, kMtn, kAlti, kFarm, kFerr }, { kMtn, kMtn, kBog });
    CHECK(Bd(s) == std::vector<int>{ 5, 3, 7, 4, 2, 1, 6, 0 });
    CHECK(GenBd(s).front() == 3);   // max-MV: Call Forth (MV 8)
}

// The hand's ONLY red source is the most protected card. Board Peat Bog + Remote Farm = 4 mana, no red.
// KEEP: R1 the Mountain (the only red-making land; mana 5) -> CT -> BD -> P1 Boarding Party (R 1).
// The burst lands (Peat Bog, Skerry) are surplus once the target is met; Protege's UU is then
// unpayable (Skerry not kept), Altisaur stranded. The Mountain is the LAST card.
TEST_CASE("BreachingDragonstorm discard B5: the only red source outlasts every burst land")
{
    const GameState s = MakeBd({ kMtn, kBog, kCT, kBD, kBP, kAlti, kProt, kSkerry }, { kBog, kFarm });
    CHECK(Bd(s) == std::vector<int>{ 5, 6, 7, 1, 4, 3, 2, 0 });
    CHECK(GenBd(s) == std::vector<int>{ 5, 4, 6, 2, 3, 0, 1, 7 });
}

// A feederless filter (Ferrous Lake as the ONLY land anywhere) is a blank: no red, no mana, the FIRST
// land shed -- behind the stranded payoffs (USER: stranded payoffs before surplus land).
TEST_CASE("BreachingDragonstorm discard B6: a filter with no feeder is the first land shed")
{
    const GameState s = MakeBd({ kFerr, kCT, kCT, kBD, kBP, kBP, kProt, kAlti }, {});
    CHECK(Bd(s) == std::vector<int>{ 7, 6, 5, 4, 0, 2, 3, 1 });
    CHECK(GenBd(s).front() == 7);
}

// Lands beyond the target -> extra castable payoffs -> spare enablers. Board 2 Mountains + Peat Bog +
// Skerry = 6 mana (R 2, B 2, U 2). KEEP: CT #3 -> BD (the other name) -> P1/P2 the two Boarding
// Parties; Protege (UU covered) is the 3rd castable payoff -> extra. SHED: the surplus Mountain ->
// Protege -> spare CTs -> BP, BP, BD, CT.
TEST_CASE("BreachingDragonstorm discard B7: surplus land -> extra castable payoff -> spare enablers")
{
    const GameState s = MakeBd({ kBP, kBP, kProt, kCT, kCT, kCT, kBD, kMtn }, { kMtn, kMtn, kBog, kSkerry });
    CHECK(Bd(s) == std::vector<int>{ 7, 2, 5, 4, 1, 0, 6, 3 });
    CHECK(GenBd(s).front() == 0);   // max-MV: a Boarding Party
}

// "spare enabler (Dragonstorm before Creative Technique)". Board 3 Mountains + Peat Bog = 5 mana.
// KEEP: CT #0 -> (mana met) -> BD #2 -> P1 Boarding Party. SHED: Altisaur, Protege (stranded) -> the
// surplus Farm -> spare BD #3 BEFORE spare CT #1 -> BP, BD, CT.
TEST_CASE("BreachingDragonstorm discard B8: a spare Breaching Dragonstorm goes before a spare Creative Technique")
{
    const GameState s = MakeBd({ kCT, kCT, kBD, kBD, kBP, kFarm, kAlti, kProt }, { kMtn, kMtn, kMtn, kBog });
    CHECK(Bd(s) == std::vector<int>{ 6, 7, 5, 3, 1, 4, 2, 0 });
    CHECK(GenBd(s).front() == 6);
}

TEST_CASE("BreachingDragonstorm discard: a staged card is never named and sorts last")
{
    GameState s = MakeBd({ kCT, kCT, kBD, kWand, kWand, kAlti, kBP, kProt }, { kMtn, kBog, kRuins });
    s.players[0].hand[5].m_is_staged = true;
    const std::vector<int> o = Bd(s);
    REQUIRE(o.size() == 8);
    CHECK(o.back() == 5);
    CHECK(o.front() == 4);
    CheckPermBd(s, o);
}

TEST_CASE("BreachingDragonstorm discard: every hand index is named exactly once on every board")
{
    const std::vector<std::vector<std::string>> hands = {
        { kCT, kCT, kBD, kWand, kWand, kAlti, kBP, kProt },
        { kMtn, kMtn, kMtn, kFerr, kFerr, kSkerry, kBog, kFarm, kRuins },
        { kWand, kWand, kWand, kCFT, kCFT, kAlti, kBD, kBD, kBD },
    };
    for (const auto& h : hands)
    {
        const GameState s = MakeBd(h, { kMtn });
        const std::vector<int> o = Bd(s);
        CheckPermBd(s, o);
        CHECK(o.size() == h.size());
    }
}
