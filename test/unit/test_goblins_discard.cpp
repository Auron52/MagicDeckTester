// Unit tests for the Goblins cleanup-discard BUCKETS the USER settled on 2026-10-09:
// GoblinsProvider::CleanupDiscardCandidates, implementing the "#### Goblins: SETTLED" block of
// docs/design/goblins-discard-policy-proposal.md. Each case covers one clause of that block; the
// expected orders were derived BY HAND from the design text (and, where the EV discount decides, from
// the documented MinotaurProvider model: P = {1, .70, .35, .10, .03} by sources short, value =
// 6 x 0.85^position in the full creature order). Every case asserts that every non-staged hand index
// is named exactly once, and the boards that pin it assert the GENERIC (max-MV) index 0 the control
// arm must differ from; the first case proves MTG_GOBLIN_BUCKET_DISCARD=0 is exactly the generic rule.
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

std::vector<int> GobArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = GoblinsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::GOBLIN_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Gob(const GameState& s) { return GobArm(s, 1); }
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
    // NET OF BOARD: a Skirk Prospector in play fills the engine slot, so the hand Lackey is surplus too.
    const GameState nb = MakeGob(hand, { kMtn, kMtn, kSk }, 3);
    CHECK(Gob(nb) == std::vector<int>{ 3, 0, 1, 2, 7, 5, 6, 4 });
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
