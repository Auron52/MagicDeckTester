// Unit tests for the USER-settled Knights cleanup-discard rule (2026-10-09):
// KnightsProvider::CleanupDiscardCandidates, implementing the "Knights: SETTLED" block of
// docs/design/knights-discard-policy-proposal.md -- lands to 3 counting the board (4 while a Haytham
// is held, >= 1 fixer for a held blue/red card that no Vial reaches, surplus Plains before fixers),
// Vial surplus after turn 1, ONE creature pool kept by a 3-turn curve fill (most creatures cast, then
// more lords, then value), and the shed order
//   2nd Haytham -> Vial after T1 -> surplus lands -> creatures outside the fill (lowest value first)
//   -> remaining creatures, latest-scheduled first -> lands.
// Every expected order below is derived BY HAND from that text (the derivation is in each case's
// comment); every case also checks the GENERIC fallback's index 0, the control arm that must differ.
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

void EnsureCardsKn()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands that
// way so a rule reading the card's own masks instead of its definition would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct BoardCard
{
    std::string name;
    int charge = 0;   // Aether Vial charge counters
};

GameState MakeKn(const std::vector<std::string>& hand, const std::vector<BoardCard>& board, int turn = 4)
{
    EnsureCardsKn();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.vial_target_mv      = 3;   // Knights' dominant creature MV (unused by this rule; real-play shape)
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const BoardCard& b : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(b.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.charge_counters   = b.charge;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> Kn(const GameState& s, int arm = 1)
{
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = KnightsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones included -- the shared builder's tier C puts them last).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kHaytham  = "Haytham Kenway";
const char* kAdeline  = "Adeline, Resplendent Cathar";
const char* kVen      = "Venerable Knight";
const char* kGuard    = "Dauntless Bodyguard";
const char* kWorthy   = "Worthy Knight";
const char* kCont     = "Acclaimed Contender";
const char* kExemplar = "Knight Exemplar";
const char* kBenalish = "Benalish Marshal";
const char* kVeteran  = "Inspiring Veteran";
const char* kZhalfir  = "Marshal of Zhalfir";
const char* kVial     = "Aether Vial";
const char* kTerr     = "Unclaimed Territory";

}   // namespace

TEST_CASE("Knights discard: the curve fill keeps 2 three-drops, not 4 -- lords first, then value")
{
    // Board 3 Plains (3 W a turn, the land target 3 is met) -> both hand Plains are surplus and go
    // first, worst-first = reverse hand order among equals: 7, 6. Window mana 3+3+3 = 9: the most
    // creatures castable is FOUR (Venerable 1 + Worthy 2 on one turn, a 3-drop on each of the other
    // two); five would cost 12. USER: "We should not keep 4 3-mana cards whether they are lords or not."
    // Ties -> more lords: two of {Exemplar, Benalish, Exemplar}; then value: Benalish > Exemplar, and
    // of the two Exemplars the earlier hand card. So Contender (no Knight in play: below Worthy) and the
    // 2nd Exemplar are outside the fill -> shed lowest value first: Contender(2), Exemplar(3). The fill
    // then sheds latest-scheduled first: T3 holds one 3-drop, the less valuable (Exemplar 0), T2
    // Benalish (1), T1 Venerable + Worthy, lowest value first (4, 5).
    const GameState s = MakeKn({ kExemplar, kBenalish, kCont, kExemplar, kVen, kWorthy, "Plains", "Plains" },
                               { {"Plains"}, {"Plains"}, {"Plains"} });
    CHECK(Kn(s) == std::vector<int>{ 7, 6, 2, 3, 0, 1, 4, 5 });
    CHECK(Gen(s).front() == 0);   // generic max-MV sheds the first 3-drop
}

TEST_CASE("Knights discard: the proposal's Vial hand -- held Vial and surplus Plains go first")
{
    // Board Plains, Plains, Unclaimed Territory, Aether Vial on 2. Turn 4 -> the held Vial is surplus
    // (and a 2nd besides) -> index 0. Land target 3 is met by the board -> both Plains surplus next.
    // The Vial on 2 puts both MV-2 off-colour lords for free, so all five creatures fit the window:
    // nothing is outside the fill.
    const GameState s = MakeKn({ "Plains", "Plains", kVen, kVen, kZhalfir, kVeteran, kCont, kVial },
                               { {"Plains"}, {"Plains"}, {kTerr}, {kVial, 2} });
    const std::vector<int> o = Kn(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 7);
    CHECK(o[1] == 1);
    CHECK(o[2] == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 6);   // generic sheds the 3/3 digger
}

TEST_CASE("Knights discard: 4 lands while a Haytham is held, 3 otherwise; the fixer is kept last")
{
    // Board 2 Plains. Haytham held -> target 4 -> the hand owes 2: Unclaimed Territory first (a fixer;
    // "surplus Plains shed before fixers"), then the first Plains; the second Plains (3) is surplus ->
    // index 0. The kept lands go LAST, Plains before the fixer: ..., 1, 2.
    const GameState s = MakeKn({ kHaytham, "Plains", kTerr, "Plains", kVen, kVen, kWorthy, kZhalfir },
                               { {"Plains"}, {"Plains"} }, 3);
    const std::vector<int> o = Kn(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 3);
    CHECK(o[6] == 1);
    CHECK(o[7] == 2);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);   // generic sheds Haytham (MV 4)

    // Same hand with Benalish Marshal for Haytham: target 3 -> the hand owes 1 (the fixer); both
    // Plains surplus, worst first (3, 1); the fixer is the last card.
    const GameState t = MakeKn({ kBenalish, "Plains", kTerr, "Plains", kVen, kVen, kWorthy, kZhalfir },
                               { {"Plains"}, {"Plains"} }, 3);
    const std::vector<int> u = Kn(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 3);
    CHECK(u[1] == 1);
    CHECK(u[7] == 2);
    CheckPermutation(t, u);
}

TEST_CASE("Knights discard: a fixer is kept for a held blue card even past the 3 -- unless a Vial reaches it")
{
    // Board 3 Plains (target met). Marshal of Zhalfir ({W}{U}) is held, the board has no fixer and no
    // Vial: ">= 1 fixer while holding a blue/red card" keeps Unclaimed Territory (agent reading: the
    // clause extends the kept lands by one). The Plains is surplus -> index 0; the Territory is the
    // last card. With 4 mana a turn every creature fits the fill.
    const GameState s = MakeKn({ kZhalfir, kTerr, "Plains", kVen, kVen, kWorthy, kExemplar, kGuard },
                               { {"Plains"}, {"Plains"}, {"Plains"} });
    const std::vector<int> o = Kn(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 2);
    CHECK(o[7] == 1);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 6);   // generic sheds Knight Exemplar

    // No blue/red card held: both lands surplus, Plains first, then the fixer.
    const GameState t = MakeKn({ kVen, kTerr, "Plains", kVen, kVen, kWorthy, kExemplar, kGuard },
                               { {"Plains"}, {"Plains"}, {"Plains"} });
    const std::vector<int> u = Kn(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 2);
    CHECK(u[1] == 1);

    // A Vial in play on 2 reaches Zhalfir (MV 2): no fixer is needed -> both lands surplus again.
    const GameState v = MakeKn({ kZhalfir, kTerr, "Plains", kVen, kWorthy, kExemplar, kBenalish, kVen },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {kVial, 2} });
    const std::vector<int> w = Kn(v);
    REQUIRE(w.size() == 8);
    CHECK(w[0] == 2);
    CHECK(w[1] == 1);
    CHECK(Gen(v).front() == 5);
}

TEST_CASE("Knights discard: the Vial is kept only as a turn-1 engine, and only the first copy")
{
    // Turn 1, nothing in play (no land was played -- why the hand is 8). The first Vial is kept; the
    // second (7) is surplus -> index 0. Both Plains are kept (the hand owes 3). Window mana 1, 2, 2:
    // no 3-drop fits, so Exemplar and Benalish are outside the fill -> lowest value first (6, 5). The
    // fill (Venerable, Worthy) sheds latest-scheduled first: Worthy (turn 2) then Venerable. Then the
    // turn-1 Vial (agent reading: the last nonland card), then the lands, Plains reversed (2, 1).
    const GameState s = MakeKn({ kVial, "Plains", "Plains", kVen, kWorthy, kBenalish, kExemplar, kVial },
                               {}, 1);
    CHECK(Kn(s) == std::vector<int>{ 7, 6, 5, 4, 3, 0, 2, 1 });
    CHECK(Gen(s).front() == 5);

    // The same hand at turn 2 with a Plains in play: every Vial is surplus -> 0 and 7 lead.
    const GameState t = MakeKn({ kVial, "Plains", "Plains", kVen, kWorthy, kBenalish, kExemplar, kVial },
                               { {"Plains"} }, 2);
    const std::vector<int> u = Kn(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 0);
    CHECK(u[1] == 7);
    CheckPermutation(t, u);
}

TEST_CASE("Knights discard: a 2nd Haytham -- in hand or with a copy in play -- is shed first")
{
    // Board 3 Plains + a fixer: 4 mana a turn, one of them wild. The 2nd hand Haytham (2) is dead ->
    // index 0. The first copy is live: the fill casts SIX (Haytham 4 / Benalish + Venerable / Worthy +
    // Venerable + Bodyguard = 12 mana) either with Haytham + Benalish or with Benalish + Exemplar --
    // same count, same two lords, and Haytham is the most valuable card -> Exemplar (5) is the one
    // creature outside the fill -> index 1.
    const GameState s = MakeKn({ kHaytham, kVen, kHaytham, kWorthy, kBenalish, kExemplar, kVen, kGuard },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {kTerr} });
    const std::vector<int> o = Kn(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 2);   // the second hand copy
    CHECK(o[1] == 5);   // the first copy is live and in the fill
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);   // generic sheds the FIRST Haytham (max MV, earlier card)

    // With NO blue source and no Vial the live Haytham cannot be cast in the window either ("off-colour
    // cards with no fixer ... don't fit the curve"): it is the one creature outside the fill -> right
    // after the dead copy.
    const GameState u = MakeKn({ kHaytham, kVen, kHaytham, kWorthy, kBenalish, kExemplar, kVen, kGuard },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {"Plains"} });
    const std::vector<int> p = Kn(u);
    REQUIRE(p.size() == 8);
    CHECK(p[0] == 2);
    CHECK(p[1] == 0);

    const GameState t = MakeKn({ kBenalish, kHaytham, kVen, kWorthy, kExemplar, kVen, kGuard, kCont },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {kHaytham} });
    CHECK(Kn(t).front() == 1);
}

TEST_CASE("Knights discard: Acclaimed Contender ranks above Worthy Knight only with a Knight in play")
{
    // Board 3 Plains + a Venerable Knight. 3 W a turn -> one creature a turn, three in the window;
    // ties -> more lords: Benalish, Benalish, Exemplar(4) form the fill. Outside, lowest value first:
    // with a Knight in play Contender ranks ABOVE Worthy, so Worthy (1) goes before Contender (0),
    // then the spare Exemplar (5). The fill: latest-scheduled first, the least valuable latest
    // (Exemplar 4, Benalish 3, Benalish 2).
    const GameState s = MakeKn({ kCont, kWorthy, kBenalish, kBenalish, kExemplar, kExemplar, "Plains", "Plains" },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {kVen} });
    CHECK(Kn(s) == std::vector<int>{ 7, 6, 1, 0, 5, 4, 3, 2 });
    CHECK(Gen(s).front() == 0);

    // No Knight in play: Contender ranks BELOW Worthy, so it goes first.
    const GameState t = MakeKn({ kCont, kWorthy, kBenalish, kBenalish, kExemplar, kExemplar, "Plains", "Plains" },
                               { {"Plains"}, {"Plains"}, {"Plains"} });
    CHECK(Kn(t) == std::vector<int>{ 7, 6, 0, 1, 5, 4, 3, 2 });
}

TEST_CASE("Knights discard: lord order -- Haytham > Benalish > Veteran = Zhalfir > Exemplar")
{
    // Board 4 fixers: 4 mana a turn, any colour. Haytham held -> target 4, met by the board -> the
    // three hand Plains are surplus (7, 6, 5). Five lords cost 4+3+3+2+2 = 14 > 12, so the fill casts
    // FOUR, and every 4-set needs both 2-drops on one turn (Veteran + Zhalfir) beside two of
    // {Haytham, Benalish, Exemplar}. All four are lords, so value decides: Haytham and Benalish over
    // Exemplar -> Exemplar (0) is outside. The fill, latest-scheduled first: T3 Benalish (4), T2
    // Haytham (2), T1 Veteran + Zhalfir -- equal value, the later hand card (Zhalfir, 3) first.
    const GameState s = MakeKn({ kExemplar, kVeteran, kHaytham, kZhalfir, kBenalish, "Plains", "Plains", "Plains" },
                               { {kTerr}, {kTerr}, {kTerr}, {kTerr} });
    CHECK(Kn(s) == std::vector<int>{ 7, 6, 5, 0, 4, 2, 3, 1 });
    CHECK(Gen(s).front() == 2);   // generic sheds Haytham
}

TEST_CASE("Knights discard: an Adeline duplicate is dead like a Haytham one (the legend rule, any legend)")
{
    const GameState s = MakeKn({ kAdeline, kVen, kWorthy, kBenalish, kExemplar, kVen, kGuard, kCont },
                               { {"Plains"}, {"Plains"}, {"Plains"}, {kAdeline} });
    CHECK(Kn(s).front() == 0);
}

TEST_CASE("Knights discard: MTG_KNIGHTS_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeKn({ kExemplar, kBenalish, kCont, kExemplar, kVen, kWorthy, "Plains", "Plains" },
                               { {"Plains"}, {"Plains"}, {"Plains"} });
    CHECK(Kn(s, 0) == Gen(s));
    CHECK(Kn(s, 1) != Gen(s));
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = 0;
    CHECK(KnightsProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = 1;
    CHECK_FALSE(KnightsProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = -1;
    // The slivers rule on the shared base is NOT what Knights runs, on or off.
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = 0;
    const std::vector<int> a = Kn(s);
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = 1;
    const std::vector<int> b = Kn(s);
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = -1;
    CHECK(a == b);
}

TEST_CASE("Knights discard: a staged card is never named by the rule and sorts last")
{
    GameState s = MakeKn({ kExemplar, kBenalish, kCont, kExemplar, kVen, kWorthy, "Plains", "Plains" },
                         { {"Plains"}, {"Plains"}, {"Plains"} });
    s.players[0].hand[1].m_is_staged = true;   // Benalish is committed to a line (in exile)
    const std::vector<int> o = Kn(s);
    REQUIRE(o.size() == 8);
    CHECK(o.back() == 1);
    CheckPermutation(s, o);
    CHECK(o.front() == 7);
}

TEST_CASE("Knights discard: SoldiersProvider (also a VialProvider) is untouched by the new rules")
{
    EnsureCardsKn();
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.vial_target_mv = 2;
    int num = 1;
    for (const char* n : { "Champion of the Parish", "Field Marshal", "Thalia's Lieutenant", "Plains",
                           "Aether Vial", "Esper Sentinel", "Rick, Steadfast Leader", "Plains" })
    { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const char* n : { "Plains", "Plains", "Plains" })
    {
        Permanent p;
        p.card = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number = num++;
        s.battlefield.push_back(p);
    }
    std::vector<int> base;
    for (int arm : { 0, 1 })
    {
        heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
        heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
        heurarm::t_arm[heurarm::WK_BUCKET_DISCARD]      = static_cast<std::int8_t>(arm);
        const std::vector<int> v = SoldiersProvider().CleanupDiscardCandidates(s, nullptr);
        if (arm == 0) { base = v; } else { CHECK(v == base); }
        CHECK(SoldiersProvider().CleanupDiscardShedStable());
    }
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::KNIGHTS_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::WK_BUCKET_DISCARD]      = -1;
}
