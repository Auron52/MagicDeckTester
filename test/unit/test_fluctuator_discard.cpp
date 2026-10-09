// Unit tests for the Fluctuator cleanup-discard buckets (USER design 2026-10-09):
// FluctuatorProvider::CleanupDiscardCandidates + CleanupDiscardProtectionWaived, implementing
// docs/design/fluctuator-discard-policy-proposal.md "Fluctuator: SETTLED".
//   KEEP: enabler (one) > Stinger > Unearth > R + B source > Hollow One > lands to 3 (4 with no
//         enabler) > 2nd Stinger / 2nd Unearth under close-out failure > W source while a Tutor is kept.
//   SHED: 2nd Fluctuator / spare Tutor (Fluctuator first) > Forsake > Stinger-for-Unearth (tight) >
//         Hollow One (no reachable enabler, < 5 mana) > lands past the count > spare Hollow One >
//         spare Unearth > spare Stinger > the protected tail (the KEEP ladder backwards).
// Every expected order below is derived BY HAND from that text (the derivation is in each case's
// comment); every board also checks "each non-staged index exactly once", and the differ-boards
// assert the =0 arm (generic max-MV + the profile's scope-All protection) puts a different card first.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsFld()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Zone cards are NAME-ONLY placeholders in real play; build hand / library / graveyard that way.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// The deck's profile: required_pieces [Fluctuator, Enlightened Tutor], no discard_protect (scope All).
const std::vector<std::string> kReq = { "Fluctuator", "Enlightened Tutor" };

const FluctuatorProvider& Prov()
{
    static const FluctuatorProvider p;
    return p;
}

const char* kF  = "Fluctuator";
const char* kT  = "Enlightened Tutor";
const char* kS  = "Drannith Stinger";
const char* kU  = "Unearth";
const char* kH  = "Hollow One";
const char* kFW = "Forsake the Worldly";

GameState MakeFl(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                 int library_size = 30, const std::vector<std::string>& graveyard = {})
{
    EnsureCardsFld();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &Prov();   // ResolveProvider -> the waiver is consulted by the ranking
    s.m_required_pieces   = &kReq;
    int num = 1;
    for (const std::string& n : hand)      { s.players[0].hand.push_back(Ph(n, num++)); }
    for (int k = 0; k < library_size; ++k) { s.players[0].library.push_back(Ph("Polluted Mire", 100 + k)); }
    for (const std::string& n : graveyard) { s.players[0].graveyard.push_back(Ph(n, 200 + num++)); }
    num = 300;
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

// The policy under a pinned arm (1 = the USER buckets + waiver; 0 = the hatch).
std::vector<int> FlArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = Prov().CleanupDiscardCandidates(s, &kReq);
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Fl(const GameState& s) { return FlArm(s, 1); }
// The =0 arm as real play sees it: the generic max-MV ranking with the waiver OFF (scope All).
std::vector<int> Gen(const GameState& s)
{
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = 0;
    std::vector<int> v = GenericProvider().CleanupDiscardCandidates(s, &kReq);
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = -1;
    return v;
}
bool Prot(const GameState& s, int i, int arm = 1)
{
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    const bool p = CleanupDiscardProtected(s, s.players[0].hand[static_cast<std::size_t>(i)], &kReq);
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = -1;
    return p;
}

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

// Fillers for the protection boards: nothing here is an enabler.
std::vector<std::string> WithFillers(std::vector<std::string> enablers)
{
    for (const char* f : { "Polluted Mire", "Blasted Landscape", kH, kU, kS, "Drifting Meadow" })
    { enablers.push_back(f); }
    return enablers;
}

}   // namespace

// ---- PROTECTION: exactly ONE of {Fluctuator, Tutor}, Fluctuator first, NONE with a Fluctuator in play

TEST_CASE("Fluct discard protection: {F} -- the lone Fluctuator stays protected (and sheds last)")
{
    const GameState s = MakeFl(WithFillers({ kF }), { "Canyon Slough" });
    CHECK(Prot(s, 0));
    const std::vector<int> o = Fl(s);
    CheckPermutation(s, o);
    CHECK(o.back() == 0);
}

TEST_CASE("Fluct discard protection: {F, T} -- Fluctuator protected, the Tutor waived and shed first")
{
    const GameState s = MakeFl(WithFillers({ kF, kT }), { "Canyon Slough" });
    CHECK(Prot(s, 0));
    CHECK_FALSE(Prot(s, 1));
    CHECK(Prot(s, 1, 0));   // =0: scope All protects the Tutor too
    const std::vector<int> o = Fl(s);
    CheckPermutation(s, o);
    CHECK(o.front() == 1);  // S0: the spare Tutor
    CHECK(o.back() == 0);   // rung 1: the kept Fluctuator
}

TEST_CASE("Fluct discard protection: {F, F} -- both waived, the order keeps exactly one")
{
    const GameState s = MakeFl(WithFillers({ kF, kF }), { "Canyon Slough" });
    CHECK_FALSE(Prot(s, 0));
    CHECK_FALSE(Prot(s, 1));
    CHECK(Prot(s, 0, 0));
    CHECK(Prot(s, 1, 0));
    const std::vector<int> o = Fl(s);
    CheckPermutation(s, o);
    CHECK(o.front() == 0);  // S0: the 2nd Fluctuator (lowest m_number sheds first)
    CHECK(o.back() == 1);   // the kept one, last
}

TEST_CASE("Fluct discard protection: {T, T} -- both waived, the order keeps exactly one")
{
    const GameState s = MakeFl(WithFillers({ kT, kT }), { "Canyon Slough" });
    CHECK_FALSE(Prot(s, 0));
    CHECK_FALSE(Prot(s, 1));
    const std::vector<int> o = Fl(s);
    CheckPermutation(s, o);
    CHECK(o.front() == 0);
    CHECK(o.back() == 1);
}

TEST_CASE("Fluct discard protection: {T} -- the lone Tutor stays protected (and sheds last)")
{
    const GameState s = MakeFl(WithFillers({ kT }), { "Canyon Slough" });
    CHECK(Prot(s, 0));
    const std::vector<int> o = Fl(s);
    CheckPermutation(s, o);
    CHECK(o.back() == 0);
}

TEST_CASE("Fluct discard protection: a Fluctuator in play -- NONE protected; the hand copy sheds first")
{
    const GameState sf = MakeFl(WithFillers({ kF }), { kF, "Canyon Slough" });
    CHECK_FALSE(Prot(sf, 0));
    CHECK(Prot(sf, 0, 0));  // =0: scope All, the old behaviour
    const std::vector<int> of = Fl(sf);
    CheckPermutation(sf, of);
    CHECK(of.front() == 0);
    CHECK(Gen(sf).front() != 0);

    const GameState st = MakeFl(WithFillers({ kT }), { kF, "Canyon Slough" });
    CHECK_FALSE(Prot(st, 0));
    const std::vector<int> ot = Fl(st);
    CheckPermutation(st, ot);
    CHECK(ot.front() == 0);
}

// ---- the ranking -------------------------------------------------------------------------------------

// T1 (proposal worked example 1). No board; enabler held -> count 3. KEEP: F#2 > Unearth > Slough (R+B)
// > Hollow One > Blasted (untapped) > Mire (covered tapped). SHED: S0 F#1, overflow Meadow (no relevant
// colour: no Tutor), then the ladder backwards.
TEST_CASE("Fluct discard T1: a 2nd Fluctuator is the first card shed (generic sheds Hollow One)")
{
    const GameState s = MakeFl({ kF, kF, "Polluted Mire", "Canyon Slough", "Blasted Landscape",
                                 "Drifting Meadow", kH, kU }, {});
    CHECK(Fl(s) == std::vector<int>{ 0, 5, 2, 4, 6, 3, 7, 1 });
    CHECK(Gen(s).front() == 6);
    CHECK(Fl(s) == Prov().CleanupDiscardCandidates(s, &kReq));   // the default arm is ON
}

// T2 (proposal example 2). Going off: Fluctuator + Stinger in play, library 30 >= 20 - swing ->
// HoldFuelWhileComboing -> colours only (board covers R + B). KEEP: Unearth > Hollow One#5. SHED:
// Forsake > overflow lands (Groves no-colour > Pools covered > Blasted untapped > Capital City) >
// spare Hollow One#4 > ladder backwards.
TEST_CASE("Fluct discard T2: going off -- Forsake first, every land is ammunition, City last of them")
{
    const GameState s = MakeFl({ "Blasted Landscape", "Scattered Groves", "Fetid Pools", kH, kH, kU, kFW,
                                 "Capital City" },
                               { kF, kS, "Canyon Slough", "Polluted Mire", "Smoldering Crater" });
    CHECK(Fl(s) == std::vector<int>{ 6, 1, 2, 0, 7, 3, 4, 5 });
    CHECK(Gen(s).front() == 3);
}

// T3 (proposal example 3, under the USER's tightened S2). Board: Polluted Mire. Unearth's {B} is
// payable, the yard is empty, and {1}{R} cannot be paid next turn (Mire + the only untapped drop,
// colourless Blasted) -> S2 sheds the Stinger. No enabler -> count 4: Crater (R) > Hollow One (reach 6,
// so not early) > Blasted > Pools; overflow Meadow, Groves.
TEST_CASE("Fluct discard T3: Stinger-for-Unearth fires when {1}{R} cannot be paid next turn")
{
    const GameState s = MakeFl({ kS, kU, kH, "Smoldering Crater", "Drifting Meadow", "Scattered Groves",
                                 "Fetid Pools", "Blasted Landscape" },
                               { "Polluted Mire" });
    CHECK(Fl(s) == std::vector<int>{ 0, 4, 5, 6, 7, 2, 3, 1 });
    CHECK(Gen(s).front() == 2);
}

// T4: the three S2 conditions, one at a time. Hand: Stinger, Unearth, Hollow One, <untapped land>,
// Meadow, Groves, Farmland, Fluctuator (enabler held -> count 3).
TEST_CASE("Fluct discard T4a: Capital City needs a feed -- Mire + City cannot pay {1}{R} next turn, S2 fires")
{
    const GameState s = MakeFl({ kS, kU, kH, "Capital City", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Polluted Mire" });
    // KEEP: F > Unearth > City (the R fallback: no plain R in hand) > Hollow One > Farmland (count 3)
    CHECK(Fl(s) == std::vector<int>{ 0, 4, 5, 6, 2, 3, 1, 7 });
    CHECK(Gen(s).front() == 2);
}

TEST_CASE("Fluct discard T4b: two lands in play + Capital City CAN pay {1}{R} next turn -> no S2")
{
    const GameState s = MakeFl({ kS, kU, kH, "Capital City", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Polluted Mire", "Fetid Pools" });
    // KEEP: F > Stinger > Unearth > City (R) > Hollow One; board 2 + City = the count.
    CHECK(Fl(s) == std::vector<int>{ 4, 5, 6, 2, 3, 1, 0, 7 });
}

TEST_CASE("Fluct discard T4c: Blasted Landscape is colourless -- Mire + Blasted cannot pay {1}{R}, S2 fires")
{
    const GameState s = MakeFl({ kS, kU, kH, "Blasted Landscape", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Polluted Mire" });
    CHECK(Fl(s) == std::vector<int>{ 0, 4, 5, 6, 3, 2, 1, 7 });
}

TEST_CASE("Fluct discard T4d: Canyon Slough + Blasted CAN pay {1}{R} next turn -> no S2")
{
    const GameState s = MakeFl({ kS, kU, kH, "Blasted Landscape", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Canyon Slough" });
    CHECK(Fl(s) == std::vector<int>{ 4, 5, 6, 3, 2, 1, 0, 7 });
}

TEST_CASE("Fluct discard T4e: a Stinger already in the graveyard -> no S2 (the Unearth has its target)")
{
    const GameState s = MakeFl({ kS, kU, kH, "Blasted Landscape", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Polluted Mire" }, 30, { kS });
    CHECK(Fl(s) == std::vector<int>{ 4, 5, 6, 3, 2, 1, 0, 7 });
}

TEST_CASE("Fluct discard T4f: Unearth's {B} not payable (no black land in play) -> no S2")
{
    const GameState s = MakeFl({ kS, kU, kH, "Blasted Landscape", "Drifting Meadow", "Scattered Groves",
                                 "Irrigated Farmland", kF },
                               { "Drifting Meadow" });
    CHECK(Fl(s) == std::vector<int>{ 4, 5, 6, 3, 2, 1, 0, 7 });
}

// T5: no enabler anywhere, reach 2 + 2 = 4 < 5 -> Hollow One sheds early (S2b), right after Forsake;
// count 4 = Slough + Mire + Crater + Meadow. KEEP: Stinger > Unearth#8 > Crater > Meadow.
TEST_CASE("Fluct discard T5: no reachable enabler and reach < 5 -- every Hollow One sheds early")
{
    const GameState s = MakeFl({ kH, kH, kS, kU, "Smoldering Crater", "Drifting Meadow", kFW, kU },
                               { "Canyon Slough", "Polluted Mire" });
    CHECK(Fl(s) == std::vector<int>{ 6, 0, 1, 3, 5, 4, 7, 2 });
    CHECK(Gen(s).front() == 0);
}

// T6: a Tutor with NO W source anywhere is not a reachable enabler -> Hollow One drops below the
// surplus land. KEEP: Tutor > Stinger#8 > Unearth#7 > Thicket (count 3); overflow Pools; spares.
TEST_CASE("Fluct discard T6: Tutor without a W source -> Hollow One sheds ahead of the surplus land")
{
    const GameState s = MakeFl({ kT, kH, kS, kU, "Fetid Pools", "Sheltered Thicket", kU, kS },
                               { "Canyon Slough", "Polluted Mire" });
    const std::vector<int> o = Fl(s);
    CHECK(o == std::vector<int>{ 1, 4, 3, 2, 5, 6, 7, 0 });
    CHECK(Prot(s, 0));   // the lone Tutor stays protected
}

// T6b: the same with a W source (Glittering Massif) -> the Tutor is reachable, Hollow One is kept above
// the lands; W is relevant (Tutor kept), so the Massif is the "missing colour" land in the count.
TEST_CASE("Fluct discard T6b: Tutor WITH a W source -> Hollow One kept, the W land fills the count")
{
    const GameState s = MakeFl({ kT, kH, kS, kU, "Glittering Massif", "Sheltered Thicket", kU, kS },
                               { "Canyon Slough", "Polluted Mire" });
    CHECK(Fl(s) == std::vector<int>{ 5, 3, 2, 4, 1, 6, 7, 0 });
    CHECK(Gen(s).front() == 1);
}

// T7: the MANA count. Board: Canyon Slough. Hand: X, Unearth, Unearth, Hollow One, Crater, Pools,
// Meadow, Capital City. With no enabler (X = Stinger) the count is 4: City > Pools > Crater (covered,
// ties by m_number) -> only Meadow overflows, BEFORE the spare Unearth. With a Fluctuator the count is
// 3: Crater overflows too and sheds before the spare Unearth.
TEST_CASE("Fluct discard T7a: no enabler anywhere -> 4 lands counting board")
{
    const GameState s = MakeFl({ kS, kU, kU, kH, "Smoldering Crater", "Fetid Pools", "Drifting Meadow",
                                 "Capital City" },
                               { "Canyon Slough" });
    CHECK(Fl(s) == std::vector<int>{ 6, 1, 4, 5, 7, 3, 2, 0 });
}

TEST_CASE("Fluct discard T7b: a Fluctuator held -> 3 lands counting board")
{
    const GameState s = MakeFl({ kF, kU, kU, kH, "Smoldering Crater", "Fetid Pools", "Drifting Meadow",
                                 "Capital City" },
                               { "Canyon Slough" });
    CHECK(Fl(s) == std::vector<int>{ 6, 4, 1, 5, 7, 3, 2, 0 });
}

// A held Tutor counts as held (USER) -> 3, and with the Tutor kept W is relevant: City first, then the
// Meadow as the missing-colour land; Crater and Pools overflow.
TEST_CASE("Fluct discard T7c: a Tutor held counts as held -> 3 lands, the W source is one of them")
{
    const GameState s = MakeFl({ kT, kU, kU, kH, "Smoldering Crater", "Fetid Pools", "Drifting Meadow",
                                 "Capital City" },
                               { "Canyon Slough" });
    CHECK(Fl(s) == std::vector<int>{ 4, 5, 1, 6, 7, 3, 2, 0 });
}

// T8: the count is already met by the board (3 lands); a W source is still kept while the Tutor is
// (rung 8), and so sheds AFTER the spare Hollow One / Unearth. With a Fluctuator instead, the Meadow
// is a no-relevant-colour land and sheds first.
TEST_CASE("Fluct discard T8: W source kept past the count while the Tutor is kept")
{
    const std::vector<std::string> board = { "Canyon Slough", "Polluted Mire", "Smoldering Crater" };
    const GameState st = MakeFl({ kT, kS, kU, kH, "Drifting Meadow", "Blasted Landscape", kU, kH }, board);
    CHECK(Fl(st) == std::vector<int>{ 5, 3, 2, 4, 7, 6, 1, 0 });
    const GameState sf = MakeFl({ kF, kS, kU, kH, "Drifting Meadow", "Blasted Landscape", kU, kH }, board);
    CHECK(Fl(sf) == std::vector<int>{ 4, 5, 3, 2, 7, 6, 1, 0 });
}

// T9: close-out failure. Fluctuator + Stinger in play, library 10: 10 * 1 * 1 < 20 - swing -> a 2nd
// Stinger and a 2nd Unearth (target: a Stinger in hand) join the ladder below Hollow One; the 3rd
// Stinger is spare. Library 30 instead: the chain closes (going off: colours only) and both are spare.
TEST_CASE("Fluct discard T9: close-out failure keeps a 2nd Stinger and a 2nd Unearth")
{
    const std::vector<std::string> hand  = { kS, kS, kU, kU, kH, "Blasted Landscape", "Fetid Pools", kFW };
    const std::vector<std::string> board = { kF, kS, "Canyon Slough", "Polluted Mire", "Smoldering Crater" };
    const GameState s10 = MakeFl(hand, board, 10);
    CHECK(Fl(s10) == std::vector<int>{ 7, 6, 5, 0, 2, 1, 4, 3 });
    const GameState s30 = MakeFl(hand, board, 30);
    CHECK(Fl(s30) == std::vector<int>{ 7, 6, 5, 2, 0, 1, 4, 3 });
}

// T10: a Fluctuator in play -> ENABLER quota 0: the hand Fluctuator sheds first, then the Tutor
// ("Fluctuator first"); count 3 = Slough + Mire + Blasted.
TEST_CASE("Fluct discard T10: Fluctuator in play -- hand Fluctuator, then Tutor, are the first sheds")
{
    const GameState s = MakeFl({ kT, kF, kS, kU, kH, "Drifting Meadow", "Smoldering Crater",
                                 "Blasted Landscape" },
                               { kF, "Canyon Slough", "Polluted Mire" });
    CHECK(Fl(s) == std::vector<int>{ 1, 0, 5, 6, 7, 4, 3, 2 });
    CHECK(Gen(s).front() == 4);
}

// ---- hatch, staging, stability --------------------------------------------------------------------

TEST_CASE("Fluct discard: MTG_FLUCT_BUCKET_DISCARD=0 restores the generic ranking (and scope-All protection)")
{
    const GameState s = MakeFl({ kF, kF, "Polluted Mire", "Canyon Slough", "Blasted Landscape",
                                 "Drifting Meadow", kH, kU }, {});
    const std::vector<int> off = FlArm(s, 0);
    const std::vector<int> on  = FlArm(s, 1);
    CHECK(off == Gen(s));
    CHECK(off.front() == 6);   // max-MV: Hollow One
    CHECK(off.back() == 1);    // both Fluctuators protected -> tier C
    CHECK(off[off.size() - 2] == 0);
    CHECK(on.front() == 0);    // the policy: the 2nd Fluctuator
    CHECK(on != off);          // the control that MUST differ
}

TEST_CASE("Fluct discard: a staged card is never named by the policy and sorts last")
{
    GameState s = MakeFl({ kF, kF, "Polluted Mire", "Canyon Slough", "Blasted Landscape",
                           "Drifting Meadow", kH, kU }, {});
    s.players[0].hand[7].m_is_staged = true;
    const std::vector<int> o = Fl(s);
    REQUIRE(o.size() == 8);
    CHECK(o.back() == 7);
    CHECK(std::find(o.begin(), o.end() - 1, 7) == o.end() - 1);
    CHECK(o == std::vector<int>{ 0, 5, 2, 4, 6, 3, 1, 7 });
}

TEST_CASE("Fluct discard: the shed order is re-ranked per shed while the buckets are on")
{
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = 1;
    CHECK_FALSE(Prov().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = 0;
    CHECK(Prov().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::FLUCT_BUCKET_DISCARD] = -1;
}
