// Unit tests for the Goblins cleanup-discard BUCKETS the USER settled on 2026-10-09:
// GoblinsProvider::CleanupDiscardCandidates, implementing the "#### Goblins: SETTLED" block of
// docs/design/goblins-discard-policy-proposal.md. Each case covers one clause of that block; the
// expected orders were derived BY HAND from the design text (and, where the EV discount decides, from
// the documented MinotaurProvider model: P = {1, .70, .35, .10, .03} by sources short, value =
// 6 x 0.85^position in the full creature order). Those cases pin MTG_DISCARD_DIST_MODEL=0 -- the arm
// that must reproduce that table exactly -- via Gob(); the shared distance model (the default) has its
// own hand-derived cases via GobM(). Every case asserts that every non-staged hand index is named
// exactly once, and the boards that pin it assert the GENERIC (max-MV) index 0 the control arm must
// differ from; the first case proves MTG_GOBLIN_BUCKET_DISCARD=0 is exactly the generic rule. The
// USER's 2026-10-10 rulings (hand Lackey surplus, the lord cap) and the searched-tutor Vial fix have
// their own cases at the end.
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

void EnsureCardsGob()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands that
// way so a policy reading the card's own masks would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const char* kMux  = "Muxus, Goblin Grandee";
const char* kSg   = "Siege-Gang Commander";
const char* kKr   = "Krenko, Mob Boss";
const char* kPa   = "Pashalik Mons";
const char* kMwm  = "Mogg War Marshal";
const char* kMa   = "Goblin Matron";
const char* kTs   = "Twinshot Sniper";
const char* kCw   = "Goblin Chainwhirler";
const char* kBolt = "Lightning Bolt";
const char* kSt   = "Stingscourger";
const char* kLa   = "Goblin Lackey";
const char* kWc   = "Goblin Warchief";
const char* kSk   = "Skirk Prospector";
const char* kVial = "Aether Vial";
const char* kCh   = "Goblin Chieftain";
const char* kRh   = "Rundvelt Hordemaster";
const char* kPd   = "Goblin Piledriver";
const char* kKing = "Goblin King";
const char* kMtn  = "Mountain";
const char* kCav  = "Cavern of Souls";
const char* kTtc  = "Three Tree City";

// `board` entries are real Permanents (they carry card data, unlike hand cards). `vial_charge` is put
// on every Aether Vial on the board; `staged` marks those hand indices as impulse-exiled.
GameState MakeGob(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                  int turn, int vial_charge = 0, const std::vector<int>& staged = {})
{
    EnsureCardsGob();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (int i : staged) { s.players[0].hand[static_cast<std::size_t>(i)].m_is_staged = true; }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(Ph(k % 2 ? kMtn : kMwm, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (n == kVial) { p.charge_counters = vial_charge; }
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> GobArm(const GameState& s, int arm, int dist_model = 0)
{
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]    = static_cast<std::int8_t>(dist_model);
    std::vector<int> v = GoblinsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]    = -1;
    return v;
}
std::vector<int> Gob(const GameState& s)  { return GobArm(s, 1, 0); }   // Minotaur's table (=0)
std::vector<int> GobM(const GameState& s) { return GobArm(s, 1, 1); }   // the shared model (default)
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones are left to the shared ranking's tier C).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

int PosOf(const std::vector<int>& order, int idx)
{
    return static_cast<int>(std::find(order.begin(), order.end(), idx) - order.begin());
}

}   // namespace

TEST_CASE("Goblins discard: MTG_GOBLIN_BUCKET_DISCARD=0 is exactly the generic max-MV ranking")
{
    // Board 3 Mountains + Lackey (a cheat route); hand MV 6,5,4,3,2,3,2,1.
    const GameState s = MakeGob({ kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kBolt },
                                { kMtn, kMtn, kMtn, kLa }, 4);
    const std::vector<int> gen = Gen(s);
    CHECK(gen == std::vector<int>{ 0, 1, 2, 3, 5, 4, 6, 7 });   // descending MV, ties keep the earlier
    CHECK(GobArm(s, 0) == gen);
    const std::vector<int> o = Gob(s);
    CheckPermutation(s, o);
    CHECK(o.front() != gen.front());   // the control arm must differ: generic sheds Muxus first
}

TEST_CASE("Goblins discard: a dead Three Tree City is the first shed; a live one is land1")
{
    const std::vector<std::string> hand = { kTtc, kMux, kSg, kKr, kPa, kMa, kSt, kLa };
    // No other red source and < 3 Goblins in play -> dead.
    const GameState dead = MakeGob(hand, {}, 2);
    const std::vector<int> od = Gob(dead);
    CHECK(od.front() == 0);
    CheckPermutation(dead, od);
    CHECK(Gen(dead).front() == 1);   // generic sheds Muxus
    // Three Goblins in play make its scaled mode live: it is the hand's only land, so it is land1 --
    // the top of the keep ladder, i.e. the LAST card shed.
    const GameState live = MakeGob(hand, { kMwm, kMa, kSt }, 2);
    const std::vector<int> ol = Gob(live);
    CHECK(ol.front() != 0);
    CHECK(ol.back() == 0);
    CheckPermutation(live, ol);
}

TEST_CASE("Goblins discard: a Muxus in play does not kill the hand copy, but a 2nd hand Muxus goes")
{
    // 0 Muxus, 1 Muxus, 2 Mountain, 3 Siege-Gang, 4 Bolt, 5 Mogg War Marshal, 6 Stingscourger, 7 King.
    const GameState s = MakeGob({ kMux, kMux, kMtn, kSg, kBolt, kMwm, kSt, kKing },
                                { kMtn, kMtn, kMtn, kMux, kLa }, 4);
    const std::vector<int> o = Gob(s);
    // SHED: (no dead TTC) -> 2nd Muxus in hand -> surplus mana (3 lands in play: the Mountain) -> ...
    // and the first Muxus, exempt from the legend drop and routed by the Lackey in play (one put next
    // combat erases its distance), is creature1: the most protected card. Siege-Gang (two sources
    // short) is the lowest EV; King is the one lord the crowd (2 + 3) allows.
    CHECK(o == std::vector<int>{ 1, 2, 3, 6, 4, 5, 7, 0 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("Goblins discard: with no cheat route a lone Muxus is the FIRST creature shed")
{
    const std::vector<std::string> hand = { kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kBolt };
    // No Goblin Lackey in play or in hand: Muxus first. (Krenko, no Goblin in play, is gated below
    // Mogg War Marshal; Siege-Gang two short and Krenko one short are discounted.)
    const GameState none = MakeGob(hand, { kMtn, kMtn, kMtn }, 4);
    const std::vector<int> on = Gob(none);
    CHECK(on == std::vector<int>{ 0, 1, 6, 2, 7, 5, 4, 3 });
    // A Lackey IN PLAY is the route, and its put erases Muxus's distance: Muxus is the last card shed.
    const GameState board = MakeGob(hand, { kMtn, kMtn, kMtn, kLa }, 4);
    const std::vector<int> ob = Gob(board);
    CHECK(ob == std::vector<int>{ 1, 6, 7, 5, 2, 4, 3, 0 });
    CHECK(Gen(board).front() == 0);
}

TEST_CASE("Goblins discard: a Lackey in hand (even a staged one) is a route too -- agent reading")
{
    // The hand Lackey takes the engine slot and its put (one turn late: distance <= 1) keeps Muxus
    // just under a castable Pashalik instead of sinking it on mana distance alone.
    const GameState hl = MakeGob({ kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kLa }, { kMtn, kMtn, kMtn }, 4);
    const std::vector<int> o = Gob(hl);
    CHECK(o == std::vector<int>{ 1, 2, 6, 5, 4, 0, 7, 3 });
    CHECK(PosOf(o, 0) > PosOf(o, 6));   // Muxus outlives Stingscourger
    // A STAGED (impulse-exiled) Lackey is playable, so it routes Muxus as well; the staged card itself
    // is never named by the policy and falls to the shared ranking's last tier.
    const GameState st = MakeGob({ kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kBolt, kLa },
                                 { kMtn, kMtn, kMtn }, 4, 0, { 8 });
    const std::vector<int> os = Gob(st);
    CHECK(os == std::vector<int>{ 1, 6, 2, 7, 5, 4, 0, 3, 8 });
    CHECK(os.front() != 0);
    CheckPermutation(st, os);
}

TEST_CASE("Goblins discard: surplus mana sheds colourless land, then lands, then the Vial (after T1)")
{
    // 0 Siege-Gang, 1 Mountain, 2 Aether Vial, 3 Three Tree City (live: Mountains in play), 4 Mogg,
    // 5 Pashalik, 6 Krenko, 7 Matron. Three lands in play = the whole "3 flat" quota.
    const GameState s = MakeGob({ kSg, kMtn, kVial, kTtc, kMwm, kPa, kKr, kMa }, { kMtn, kMtn, kMtn }, 3);
    const std::vector<int> o = Gob(s);
    CHECK(o == std::vector<int>{ 3, 1, 2, 0, 6, 7, 4, 5 });
    CHECK(Gen(s).front() == 0);   // generic sheds Siege-Gang
}

TEST_CASE("Goblins discard: an Aether Vial is the engine only at the end of turn 1; a 2nd Vial always goes")
{
    // 0 Vial, 1 Mountain, 2 Mountain, 3 Siege-Gang, 4 Krenko, 5 Mogg, 6 Stingscourger, 7 Matron.
    const std::vector<std::string> hand = { kVial, kMtn, kMtn, kSg, kKr, kMwm, kSt, kMa };
    const GameState t1 = MakeGob(hand, { kMtn }, 1);
    const std::vector<int> o1 = Gob(t1);
    // Ladder land1 > creature1 > land2 > ENGINE > lord1 > creature2 ..: read backwards the Vial sits
    // right before land2, creature1 and land1.
    CHECK(o1 == std::vector<int>{ 3, 4, 6, 7, 0, 2, 5, 1 });
    CHECK(o1[4] == 0);
    // The same hand at the end of turn 2: "surplus after turn 1".
    const GameState t2 = MakeGob(hand, { kMtn }, 2);
    const std::vector<int> o2 = Gob(t2);
    CHECK(o2.front() == 0);
    CheckPermutation(t2, o2);
    CHECK(Gen(t2).front() == 3);
    // Turn 1 with a Vial already in play: the hand copy is a 2nd Vial.
    const GameState dup = MakeGob(hand, { kMtn, kVial }, 1);
    const std::vector<int> od = Gob(dup);
    CHECK(od.front() == 0);
    CheckPermutation(dup, od);
}

TEST_CASE("Goblins discard: ENGINES 1 -- Lackey > Warchief > Skirk, surplus copies join the creatures")
{
    // 0 Skirk, 1 Warchief, 2 Lackey, 3 Stingscourger, 4 Mountain, 5 Mogg, 6 Pashalik, 7 Bolt.
    const std::vector<std::string> hand = { kSk, kWc, kLa, kSt, kMtn, kMwm, kPa, kBolt };
    const GameState s = MakeGob(hand, { kMtn, kMtn }, 3);
    const std::vector<int> o = Gob(s);
    // Lackey holds the engine slot; Warchief and Skirk are surplus bodies just above Stingscourger
    // (agent reading), so Stingscourger is still the first creature shed.
    CHECK(o == std::vector<int>{ 3, 0, 1, 7, 5, 2, 6, 4 });
    CHECK(Gen(s).front() == 1);   // generic sheds Warchief (MV 3)
    // NET OF BOARD: a Skirk Prospector in play fills the engine slot, so the hand Warchief and Skirk are
    // surplus -- but NOT the hand Lackey (USER 2026-10-10: "a hand lackey is only really surplus in the
    // later game where you have nothing expensive in hand"; turn 3 is not late). It stays at the engine
    // position: backwards Stingscourger, Skirk, Warchief, creature3 Bolt, creature2 Mogg, ENGINE
    // Lackey, creature1 Pashalik, land1. (Until 2026-10-10 the Lackey was surplus: 3, 0, 1, 2, 7, 5, 6, 4.)
    const GameState nb = MakeGob(hand, { kMtn, kMtn, kSk }, 3);
    CHECK(Gob(nb) == std::vector<int>{ 3, 0, 1, 7, 5, 2, 6, 4 });
}

TEST_CASE("Goblins discard: the keep ladder land1 > creature1 > land2 > engine > lord1 > creature2 > land3 > creature3 > lord2")
{
    // 0-2 Mountain, 3 Lackey, 4 Chieftain, 5 King, 6 Mogg, 7 Stingscourger, 8 Pashalik; empty board.
    // crowd = 0 + min(3, 4 bodies) = 3 -> two lords; all creatures castable off the three lands.
    const GameState s = MakeGob({ kMtn, kMtn, kMtn, kLa, kCh, kKing, kMwm, kSt, kPa }, {}, 2);
    const std::vector<int> o = Gob(s);
    // Backwards: lord2 King, creature3 Stingscourger, land3, creature2 Mogg, lord1 Chieftain, engine
    // Lackey, land2, creature1 Pashalik, land1.
    CHECK(o == std::vector<int>{ 5, 7, 2, 6, 4, 3, 1, 8, 0 });
    CHECK(Gen(s).front() == 4);
}

TEST_CASE("Goblins discard: MULTIPLIERS capped by crowd, King first, Piledriver by turn")
{
    // Crowd 0 (no Goblin in play, no non-lord Goblin in hand): every lord is over the cap and goes
    // right after the surplus mana, King first.
    // 0 King, 1 Chieftain, 2 Hordemaster, 3 Piledriver, 4 Bolt, 5 Bolt, 6 Mountain, 7 Mountain.
    const GameState c0 = MakeGob({ kKing, kCh, kRh, kPd, kBolt, kBolt, kMtn, kMtn }, { kMtn, kMtn, kMtn }, 3);
    CHECK(Gob(c0) == std::vector<int>{ 7, 6, 0, 3, 2, 1, 5, 4 });
    CHECK(Gen(c0).front() == 0);
    // Crowd 2 (a Lackey in play + Mogg in hand): ONE lord, Chieftain. From turn 3 Hordemaster is kept
    // over Piledriver, so Piledriver goes before it.
    // 0 King, 1 Chieftain, 2 Hordemaster, 3 Piledriver, 4 Bolt, 5 Mountain, 6 Mogg, 7 Bolt.
    const std::vector<std::string> hand = { kKing, kCh, kRh, kPd, kBolt, kMtn, kMwm, kBolt };
    const GameState t3 = MakeGob(hand, { kMtn, kMtn, kMtn, kLa }, 3);
    CHECK(Gob(t3) == std::vector<int>{ 5, 0, 3, 2, 7, 4, 1, 6 });
    // Through turn 2 Piledriver is kept first, so Hordemaster goes before it.
    const GameState t2 = MakeGob(hand, { kMtn, kMtn, kMtn, kLa }, 2);
    CHECK(Gob(t2) == std::vector<int>{ 5, 0, 2, 3, 7, 4, 1, 6 });
}

TEST_CASE("Goblins discard: haste does not stack -- with Warchief in play Chieftain is a plain lord")
{
    // 0 Chieftain, 1 Hordemaster, 2 King, 3 Bolt, 4 Mountain, 5 Mogg, 6 Bolt, 7 Mountain; crowd 2 -> 1 lord.
    const std::vector<std::string> hand = { kCh, kRh, kKing, kBolt, kMtn, kMwm, kBolt, kMtn };
    const GameState haste = MakeGob(hand, { kMtn, kMtn, kMtn, kWc }, 3);
    const std::vector<int> oh = Gob(haste);
    CHECK(oh == std::vector<int>{ 7, 4, 2, 0, 6, 3, 1, 5 });   // Hordemaster kept, Chieftain over the cap
    // Control: a Lackey in play grants no haste, so Chieftain is the lord kept.
    const GameState plain = MakeGob(hand, { kMtn, kMtn, kMtn, kLa }, 3);
    CHECK(Gob(plain) == std::vector<int>{ 7, 4, 2, 1, 6, 3, 0, 5 });
}

TEST_CASE("Goblins discard: the CREATURE order, and Krenko's crowd gate")
{
    // Everything castable off four Mountains, so the order is the design's own:
    // 0 Krenko, 1 Mogg, 2 Matron, 3 Stingscourger, 4 Bolt, 5 Pashalik, 6 Twinshot, 7 Chainwhirler.
    const std::vector<std::string> hand = { kKr, kMwm, kMa, kSt, kBolt, kPa, kTs, kCw };
    // A Goblin in play: Krenko > Pashalik > Mogg > Matron > Twinshot > Chainwhirler > Bolt > Sting.
    const GameState crowd = MakeGob(hand, { kMtn, kMtn, kMtn, kMtn, kLa }, 4);
    CHECK(Gob(crowd) == std::vector<int>{ 3, 4, 7, 6, 2, 1, 5, 0 });
    // Empty board: Krenko falls behind the 2-drop Mogg War Marshal.
    const GameState empty = MakeGob(hand, { kMtn, kMtn, kMtn, kMtn }, 4);
    const std::vector<int> oe = Gob(empty);
    CHECK(oe == std::vector<int>{ 3, 4, 7, 6, 2, 0, 1, 5 });
    CHECK(Gen(empty).front() == 0);   // generic sheds Krenko (MV 4)
    // The top of the order with six lands (Muxus and Siege-Gang castable):
    // 0 Sting, 1 Bolt, 2 Chainwhirler, 3 Twinshot, 4 Matron, 5 Mogg, 6 Siege-Gang, 7 Muxus.
    const GameState top = MakeGob({ kSt, kBolt, kCw, kTs, kMa, kMwm, kSg, kMux },
                                  { kMtn, kMtn, kMtn, kMtn, kMtn, kMtn, kLa }, 6);
    CHECK(Gob(top) == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 });
    CHECK(Gen(top).front() == 7);
}

TEST_CASE("Goblins discard: Cavern of Souls is no red source for Lightning Bolt")
{
    // 0 Bolt, 1 Stingscourger, 2 Mogg, 3 Pashalik, 4 Matron, 5 Chainwhirler, 6 Siege-Gang, 7 Krenko.
    const std::vector<std::string> hand = { kBolt, kSt, kMwm, kPa, kMa, kCw, kSg, kKr };
    // Caverns only: Bolt is a source short, so it sheds ahead of the equal-value Stingscourger.
    const GameState cav = MakeGob(hand, { kCav, kCav, kCav }, 4);
    const std::vector<int> oc = Gob(cav);
    CHECK(oc == std::vector<int>{ 0, 1, 6, 7, 5, 4, 2, 3 });
    // Mountains: Bolt is castable and Stingscourger is the floor again.
    const GameState mtn = MakeGob(hand, { kMtn, kMtn, kMtn }, 4);
    const std::vector<int> om = Gob(mtn);
    CHECK(om == std::vector<int>{ 1, 6, 0, 7, 5, 4, 2, 3 });
    CHECK(Gen(mtn).front() == 6);
}

TEST_CASE("Goblins discard: the distance erasers -- Twinshot's channel, a Vial at charge, Warchief, Skirk")
{
    // Twinshot's {1}{R} channel makes it distance 0 at two lands (its cast is two short), so it is
    // kept over Matron, Bolt and Stingscourger.
    // 0 Twinshot, 1 Bolt, 2 Sting, 3 Mogg, 4 Pashalik, 5 Matron, 6 Chainwhirler.
    const GameState ts = MakeGob({ kTs, kBolt, kSt, kMwm, kPa, kMa, kCw }, { kMtn, kMtn }, 4);
    const std::vector<int> ot = Gob(ts);
    CHECK(ot == std::vector<int>{ 6, 2, 1, 5, 0, 4, 3 });
    CHECK(Gen(ts).front() == 0);

    // A Vial at 3 charge counters puts the best MV-3 creature (Pashalik) next turn: distance 0.
    // 0 Matron, 1 Sting, 2 Bolt, 3 Mogg, 4 Chainwhirler, 5 Twinshot, 6 Pashalik, 7 Krenko; one land.
    const std::vector<std::string> vh = { kMa, kSt, kBolt, kMwm, kCw, kTs, kPa, kKr };
    const GameState v0 = MakeGob(vh, { kMtn, kVial }, 4, 0);
    const GameState v3 = MakeGob(vh, { kMtn, kVial }, 4, 3);
    const std::vector<int> o0 = Gob(v0), o3 = Gob(v3);
    CHECK(o0 == std::vector<int>{ 7, 4, 0, 1, 6, 5, 2, 3 });
    CHECK(o3 == std::vector<int>{ 7, 4, 0, 1, 5, 2, 3, 6 });
    CHECK(PosOf(o3, 6) > PosOf(o0, 6));

    // A resolved Warchief takes {1} off Siege-Gang (two short -> one short), lifting it over Matron.
    // 0 Siege-Gang, 1 Mogg, 2 Matron, 3 Bolt, 4 Sting, 5 Pashalik, 6 Twinshot, 7 Chainwhirler.
    const std::vector<std::string> wh = { kSg, kMwm, kMa, kBolt, kSt, kPa, kTs, kCw };
    const GameState nowc = MakeGob(wh, { kMtn, kMtn, kMtn, kSt }, 4);
    const GameState wc   = MakeGob(wh, { kMtn, kMtn, kMtn, kWc }, 4);
    const std::vector<int> onw = Gob(nowc), ow = Gob(wc);
    CHECK(PosOf(ow, 0) > PosOf(onw, 0));
    CHECK(PosOf(ow, 0) > PosOf(ow, 2));
    CheckPermutation(wc, ow);

    // Skirk Prospector's fodder is red mana: three expendable Goblins in play put Siege-Gang in reach.
    // 0 Siege-Gang, 1 Mogg, 2 Bolt, 3 Pashalik, 4 Sting, 5 Matron, 6 Twinshot, 7 Chainwhirler.
    const std::vector<std::string> sh = { kSg, kMwm, kBolt, kPa, kSt, kMa, kTs, kCw };
    const GameState sk  = MakeGob(sh, { kMtn, kMtn, kSk, kSt, kMa }, 4);
    const GameState nsk = MakeGob(sh, { kMtn, kMtn, kCw, kSt, kMa }, 4);
    CHECK(Gob(sk).back() == 0);
    CHECK(Gob(nsk).front() == 0);
}

TEST_CASE("Goblins discard: a duplicate legend other than Muxus is dead")
{
    // Krenko in play: the hand Krenko dies to the legend rule, so it sheds first.
    const GameState s = MakeGob({ kMwm, kKr, kPa, kMa, kSt, kBolt, kTs, kCw }, { kMtn, kMtn, kMtn, kKr }, 4);
    const std::vector<int> o = Gob(s);
    CHECK(o.front() == 1);
    CheckPermutation(s, o);
}

TEST_CASE("Goblins discard: a no-route Muxus is SURPLUS -- never a ladder slot, the first creature shed")
{
    // Review finding (2026-10-09): with <= 3 other creatures the no-route Muxus used to take a creature
    // ladder slot (creature3, above lord2), so Hordemaster went first. SETTLED: "a lone Muxus is the
    // FIRST creature shed" -- after dead TTC / 2nd Muxus / surplus mana / lords over the cap, none
    // of which this hand has.
    // 0 Muxus, 1 Siege-Gang, 2 Matron, 3 Chieftain, 4 Hordemaster, 5 Warchief, 6 Mountain, 7 Mountain.
    const GameState s = MakeGob({ kMux, kSg, kMa, kCh, kRh, kWc, kMtn, kMtn }, { kMtn, kMwm }, 3);
    const std::vector<int> o = Gob(s);
    // Then the ladder backwards: lord2 Hordemaster, creature2 Siege-Gang (two short), lord1 Chieftain,
    // engine Warchief, land2, creature1 Matron, land1.
    CHECK(o == std::vector<int>{ 0, 4, 1, 3, 5, 7, 2, 6 });
    CheckPermutation(s, o);
}

TEST_CASE("Goblins discard: the rule is NOT prefix-stable, so a multi-card cleanup re-ranks per shed")
{
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = 1;
    CHECK_FALSE(GoblinsProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = 0;
    CHECK(GoblinsProvider().CleanupDiscardShedStable());   // the generic max-MV rule is prefix-stable
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = -1;

    // Why. The review's board (two hand Lackeys: shedding the spare re-priced the rest) no longer
    // diverges, because a hand Lackey is no longer spare before the late game (USER 2026-10-10). The
    // EV still re-prices on every shed: value is 6 x 0.85^position, so a far card ABOVE the shed card
    // keeps its value while a castable one BELOW it gains 1/0.85. Board 3 Mountains, T4:
    // 0 Siege-Gang (2 short: .35 x 6 = 2.10), 1 Pashalik, 2 Mogg, 3 Matron, 4 Matron (the 2nd pays 6:
    // 3 short, .10 x 3.13), 5 Chainwhirler, 6 Bolt, 7 Stingscourger (1.92). One ranking sheds the 2nd
    // Matron, then Stingscourger; re-ranked after the Matron, Stingscourger is worth 2.26 and the
    // Siege-Gang goes instead.
    GameState s = MakeGob({ kSg, kPa, kMwm, kMa, kMa, kCw, kBolt, kSt }, { kMtn, kMtn, kMtn }, 4);
    auto name_at = [&](int i) { return s.players[0].hand[static_cast<std::size_t>(i)].m_name.str(); };
    const std::vector<int> once = Gob(s);
    CHECK(once == std::vector<int>{ 4, 7, 0, 6, 5, 3, 2, 1 });
    std::vector<std::string> batched, reranked;
    for (int k = 0; k < 2; ++k) { batched.push_back(name_at(once[static_cast<std::size_t>(k)])); }
    for (int k = 0; k < 2; ++k)
    {
        const int first = Gob(s).front();
        reranked.push_back(name_at(first));
        s.players[0].hand.erase(s.players[0].hand.begin() + first);
    }
    CHECK(batched == std::vector<std::string>{ kMa, kSt });
    CHECK(reranked == std::vector<std::string>{ kMa, kSg });
}

// ---- USER rulings 2026-10-10 -----------------------------------------------------------------------

TEST_CASE("Goblins discard: a hand Lackey is surplus only late with nothing expensive (USER 2026-10-10)")
{
    // A Warchief in play fills the engine slot, but both hand Lackeys stay at the engine position
    // (turn 3 is not late). 0 Lackey, 1 Lackey, 2 Mogg, 3 Pashalik, 4 Stingscourger, 5 Bolt, 6 Matron,
    // 7 Mountain (surplus: three lands in play). Backwards: Stingscourger, Bolt, creature3 Matron,
    // creature2 Mogg, the engine position (Lackey 1, Lackey 0), creature1 Pashalik.
    const GameState two = MakeGob({ kLa, kLa, kMwm, kPa, kSt, kBolt, kMa, kMtn }, { kMtn, kMtn, kMtn, kWc }, 3);
    CHECK(Gob(two) == std::vector<int>{ 7, 4, 5, 6, 2, 1, 0, 3 });
    CHECK(GobM(two) == std::vector<int>{ 7, 4, 5, 6, 2, 1, 0, 3 });   // everything castable next turn

    // LATE (turn >= 5) and nothing expensive: next turn's mana is 2 lands + the hand land + Skirk's 1
    // fodder = 4, and no hand Goblin costs more -- the Lackey is a surplus body, just above the other
    // surplus engines. 0 Skirk, 1 Warchief, 2 Lackey, 3 Sting, 4 Mountain, 5 Mogg, 6 Pashalik, 7 Bolt.
    const std::vector<std::string> hand = { kSk, kWc, kLa, kSt, kMtn, kMwm, kPa, kBolt };
    const GameState late = MakeGob(hand, { kMtn, kMtn, kSk }, 5);
    CHECK(Gob(late) == std::vector<int>{ 3, 0, 1, 2, 7, 5, 6, 4 });

    // Late, but a Siege-Gang (5 > 4) is expensive: the Lackey is kept at the engine position. The
    // Siege-Gang is one source short and the hand Lackey's put is a turn late: .70 x 6 = 4.2, under
    // Mogg (4.335).
    const GameState exp = MakeGob({ kSk, kWc, kLa, kSt, kMtn, kMwm, kPa, kSg }, { kMtn, kMtn, kSk }, 5);
    CHECK(Gob(exp) == std::vector<int>{ 3, 0, 1, 7, 5, 2, 6, 4 });
}

TEST_CASE("Goblins discard: the lord cap does not subtract lords in play (USER 2026-10-10)")
{
    // Chieftain + Lackey in play, Mogg / Pashalik / Stingscourger in hand: crowd 2 + 3 = 5, cap 2, and
    // BOTH hand lords are kept (until 2026-10-10 the Chieftain in play took one of the two slots, so
    // King was shed right after the Mountain: 4, 0, 6, 7, 3, 2, 1, 5).
    // 0 King, 1 Hordemaster, 2 Mogg, 3 Bolt, 4 Mountain, 5 Pashalik, 6 Stingscourger, 7 Bolt.
    // Ladder: creature1 Pashalik, lord1 Hordemaster, creature2 Mogg, creature3 Bolt, lord2 King,
    // then Bolt (the 2nd pays 2) and Stingscourger.
    const GameState s = MakeGob({ kKing, kRh, kMwm, kBolt, kMtn, kPa, kSt, kBolt },
                                { kMtn, kMtn, kMtn, kCh, kLa }, 3);
    CHECK(Gob(s) == std::vector<int>{ 4, 6, 7, 0, 3, 2, 1, 5 });
    CHECK(GobM(s) == std::vector<int>{ 4, 6, 7, 0, 3, 2, 1, 5 });
}

TEST_CASE("Goblins discard: the shared distance model (MTG_DISCARD_DIST_MODEL) replaces the table")
{
    // Library 20 cards, 10 Mountains (MakeGob). Three Mountains in play, no land in hand, so a card
    // k mana short needs k DRAWN lands, at most one a turn:
    //   Krenko (4, 1 short): P_by = .5, 145/190, 1020/1140, 4635/4845 -> factor .7978 (table .70)
    //   Siege-Gang (5, 2 short): P_by = 0, 45/190, .5, 3435/4845          -> factor .4157 (table .35)
    // 0 Siege-Gang, 1 Pashalik, 2 Mogg, 3 Krenko (gated: no Goblin in play), 4 Matron, 5 Bolt, 6 Sting.
    // Values 6, 5.1, 4.335, 3.685 (Krenko), 3.132 (Matron), 2.662 (Bolt), 2.263 (Sting):
    //   model  Pa 5.1 > Mogg 4.335 > Matron 3.132 > Krenko 2.940 > Bolt 2.662 > SG 2.494 > Sting 2.263
    //   table  Pa > Mogg > Matron > Bolt 2.662 > Krenko 2.579 > Sting 2.263 > SG 2.10
    const GameState s = MakeGob({ kSg, kPa, kMwm, kKr, kMa, kBolt, kSt }, { kMtn, kMtn, kMtn }, 4);
    CHECK(GobM(s) == std::vector<int>{ 6, 0, 5, 3, 4, 2, 1 });
    CHECK(Gob(s)  == std::vector<int>{ 0, 6, 3, 5, 4, 2, 1 });   // =0 reproduces the table exactly
    // Default (unpinned) is the model.
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = 1;
    CHECK(GoblinsProvider().CleanupDiscardCandidates(s, nullptr) == std::vector<int>{ 6, 0, 5, 3, 4, 2, 1 });
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = -1;

    // KEPT hand lands are played one a turn: one Mountain in play, two kept. Matron (3) is castable
    // only on our 2nd turn (factor .75 x 5.1 = 3.825), so Bolt (4.335) now outranks it; the table
    // counted both hand lands as reach at once. 0 Mountain, 1 Mountain, 2 Mogg, 3 Matron, 4 Sting, 5 Bolt.
    const GameState kl = MakeGob({ kMtn, kMtn, kMwm, kMa, kSt, kBolt }, { kMtn }, 2);
    CHECK(GobM(kl) == std::vector<int>{ 4, 3, 5, 1, 2, 0 });
    CHECK(Gob(kl)  == std::vector<int>{ 4, 5, 3, 1, 2, 0 });

    // A Lackey in play puts the top Goblin next turn (castability 1); Krenko (a Goblin in play now,
    // so ungated) and Siege-Gang take the model's factors.
    // 0 Muxus, 1 SG, 2 Krenko, 3 Pashalik, 4 Mogg, 5 Matron, 6 Sting, 7 Bolt.
    const GameState lk = MakeGob({ kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kBolt }, { kMtn, kMtn, kMtn, kLa }, 4);
    CHECK(GobM(lk) == std::vector<int>{ 6, 1, 7, 5, 4, 2, 3, 0 });

    // A Lackey in HAND makes the card certain from our 2nd turn: Muxus .75 x 6 = 4.5, now above
    // Pashalik (4.335). 0 Muxus, 1 SG, 2 Krenko (gated), 3 Pashalik, 4 Mogg, 5 Matron, 6 Sting, 7 Lackey.
    const GameState hl = MakeGob({ kMux, kSg, kKr, kPa, kMwm, kMa, kSt, kLa }, { kMtn, kMtn, kMtn }, 4);
    CHECK(GobM(hl) == std::vector<int>{ 1, 6, 2, 5, 4, 3, 7, 0 });
}

TEST_CASE("Goblins discard: colour beats the land count -- the only red source for a kept Bolt stays (USER 2026-10-10)")
{
    // Cavern, Cavern, Three Tree City in play: three lands, so "3 flat" makes a hand Mountain surplus --
    // but it is the only source of red a Lightning Bolt can use (Cavern's colour pays creatures only,
    // Three Tree City is colourless). It is kept right behind the Bolt and gives way just before it.
    // 0 Mountain, 1 Bolt, 2 Mogg, 3 Pashalik, 4 Matron, 5 Sting, 6 Krenko (gated), 7 Siege-Gang.
    // With the Mountain kept (reach 4): Pashalik > Mogg > Siege-Gang (one short: .70 x 6 = 4.2; model
    // .6728 x 6 = 4.04) > Krenko > Matron > Bolt > Sting; the Mountain sits behind the Bolt.
    // (Until 2026-10-10 the Mountain was the first card shed.)
    const std::vector<std::string> board = { kCav, kCav, kTtc };
    const GameState s = MakeGob({ kMtn, kBolt, kMwm, kPa, kMa, kSt, kKr, kSg }, board, 4);
    CHECK(Gob(s)  == std::vector<int>{ 5, 0, 1, 4, 6, 7, 2, 3 });
    CHECK(GobM(s) == std::vector<int>{ 5, 0, 1, 4, 6, 7, 2, 3 });
    // Twinshot Sniper's channel asks the same: the Mountain sheds right before the Twinshot.
    const GameState ts = MakeGob({ kMtn, kTs, kMwm, kPa, kMa, kSt, kKr, kSg }, board, 4);
    const std::vector<int> ot = GobM(ts);
    CHECK(ot.front() != 0);
    CHECK(PosOf(ot, 0) + 1 == PosOf(ot, 1));
    CheckPermutation(ts, ot);
    // Controls: with no non-creature red spell kept (Chainwhirler is a creature: Cavern pays it), or
    // with a Mountain already in play, the hand Mountain is surplus mana and goes first.
    CHECK(GobM(MakeGob({ kMtn, kCw, kMwm, kPa, kMa, kSt, kKr, kSg }, board, 4)).front() == 0);
    CHECK(GobM(MakeGob({ kMtn, kBolt, kMwm, kPa, kMa, kSt, kKr, kSg }, { kCav, kCav, kMtn }, 4)).front() == 0);
}

TEST_CASE("Goblins discard: under the model Cavern of Souls is still no red source for Lightning Bolt")
{
    // The Cavern caveat board: with three Caverns Bolt needs a DRAWN red source (factor .7978 x 2.263 =
    // 1.805 < Stingscourger 1.923); with three Mountains it is castable and Stingscourger is the floor.
    const std::vector<std::string> hand = { kBolt, kSt, kMwm, kPa, kMa, kCw, kSg, kKr };
    CHECK(GobM(MakeGob(hand, { kCav, kCav, kCav }, 4)) == std::vector<int>{ 0, 1, 6, 5, 7, 4, 2, 3 });
    CHECK(GobM(MakeGob(hand, { kMtn, kMtn, kMtn }, 4)) == std::vector<int>{ 1, 0, 6, 5, 7, 4, 2, 3 });
}

namespace
{
// A Goblin Matron resolving: the tutor ranking over `library` (MTG_GOBLIN_TUTOR_VIAL_EXACT pinned).
GameState MakeTut(const std::vector<std::string>& library, const std::vector<std::string>& board,
                  int vial_charge, bool vial_tapped, int opp_life)
{
    EnsureCardsGob();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    int num = 1;
    s.players[0].hand.push_back(Ph(kMa, num++));
    for (const std::string& n : library) { s.players[0].library.push_back(Ph(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (n == kVial)      { p.charge_counters = vial_charge; p.tapped = vial_tapped; }
        else if (n == kMtn)  { p.tapped = true; }              // the lands paid for the Matron
        else                 { p.entered_this_turn = true; }   // a creature cast this turn: sick
        s.battlefield.push_back(p);
    }
    s.players[1].life = opp_life;
    return s;
}
std::vector<std::string> Tut(const GameState& s, int exact)
{
    heurarm::t_arm[heurarm::GOBLIN_TUTOR_VIAL_EXACT] = static_cast<std::int8_t>(exact);
    std::vector<std::string> v =
        GoblinsProvider().TutorCandidates(s, 0, CardDatabase::Instance().Lookup(kMa)->params);
    heurarm::t_arm[heurarm::GOBLIN_TUTOR_VIAL_EXACT] = -1;
    return v;
}
}   // namespace

TEST_CASE("Goblins tutor: an Aether Vial puts only a creature whose mana value EQUALS its charge")
{
    // A Vial at 3 charge counters can never put a 1- or 2-drop (the charge only rises), so with only
    // 1- and 2-drops to fetch an untapped Vial must rank exactly like a tapped one (which the ranking
    // ignores). The old `max(0, mv - charge)` credited all of them as deployable now.
    const std::vector<std::string> small = { kLa, kSk, kPd, kRh, kMwm, kSt, kMtn, kMtn, kMtn, kMtn };
    const GameState up = MakeTut(small, { kVial }, 3, /*vial_tapped=*/false, 20);
    const GameState tp = MakeTut(small, { kVial }, 3, /*vial_tapped=*/true, 20);
    CHECK(Tut(up, 1) == Tut(tp, 1));
    CHECK(Tut(up, 0) != Tut(tp, 0));   // the old estimate moved the ranking
    CHECK(Tut(tp, 1) == Tut(tp, 0));   // no Vial credit at all: the two arms agree

    // The THIS-turn burn closer: opponent at 1, three tapped Mountains, a summoning-sick Mogg (nothing
    // swings this turn; next turn's board is already lethal, so the next-turn closer stays out of it),
    // and a Vial at 4. Chainwhirler (MV 3) cannot be Vialed in, so its ETB 1 does not close this turn;
    // the old `charge >= mv` read credited the put and ranked it first on a guaranteed kill. Every card
    // here is MV <= 3, so the exact Vial ranks exactly like a tapped one.
    const std::vector<std::string> burn = { kCw, kKing, kCh, kMwm, kLa, kPa, kRh, kMtn, kMtn };
    const GameState up4 = MakeTut(burn, { kMtn, kMtn, kMtn, kMwm, kVial }, 4, false, 1);
    const GameState tp4 = MakeTut(burn, { kMtn, kMtn, kMtn, kMwm, kVial }, 4, true, 1);
    CHECK(Tut(up4, 1) == Tut(tp4, 1));
    CHECK(Tut(up4, 0).front() == kCw);
    CHECK(Tut(up4, 0) != Tut(up4, 1));
}
