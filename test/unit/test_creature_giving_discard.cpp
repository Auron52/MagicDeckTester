// Unit tests for the Creature Giving cleanup-discard buckets (USER-settled 2026-10-09,
// docs/design/creature-giving-discard-policy-proposal.md "Creature Giving: SETTLED"):
// CreatureGivingProvider::CleanupDiscardCandidates. Every expected order below was derived BY HAND
// from the design text (the derivation is in each case's comment), not read off the code. The A/B
// is THREE-armed: MTG_CG_BUCKET_DISCARD=1 (the buckets), =0 (the Defense-of-the-Heart-last patch
// that shipped 2026-08-06), and the generic max-MV ranking (GenericProvider's own hook).
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

void EnsureCardsCgD()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhC(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const char* kOrch  = "Forbidden Orchard";
const char* kCity  = "City of Brass";
const char* kHeath = "Windswept Heath";
const char* kTG    = "Temple Garden";
const char* kBP    = "Breeding Pool";
const char* kSG    = "Stomping Ground";
const char* kOT    = "Overgrown Tomb";
const char* kFor   = "Forest";
const char* kTree  = "Tree of Tales";
const char* kPool  = "Reflecting Pool";
const char* kChan  = "Azorius Chancery";
const char* kBirds = "Birds of Paradise";
const char* kCrop  = "Crop Rotation";
const char* kScry  = "Sylvan Scrying";
const char* kSP    = "Suture Priest";
const char* kWurm  = "Massacre Wurm";
const char* kHP    = "Hunted Phantasm";
const char* kWR    = "Varchild's War-Riders";
const char* kDotH  = "Defense of the Heart";
const char* kET    = "Enlightened Tutor";
const char* kSW    = "Soul Warden";
const char* kEW    = "Essence Warden";

const CreatureGivingProvider& Prov()
{
    static const CreatureGivingProvider p;
    return p;
}

std::vector<std::string> LibWithDoth()
{ return { kDotH, kDotH, kHP, kWurm, kOrch, kFor, kTG, kBP, kOT, kSG, kTree, kSP, kWR }; }
std::vector<std::string> LibWithoutDoth()
{ return { kHP, kWurm, kOrch, kFor, kTG, kBP, kOT, kSG, kTree, kSP, kWR }; }

// An opponent creature (any 1/1 body; the goldfish's gifts are 1/1 tokens).
const char* kOppBody = "Soul Warden";

GameState MakeCg(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                 const std::vector<std::string>& opp_board = {},
                 const std::vector<std::string>& library = LibWithDoth(), int war_rider_age = 0)
{
    EnsureCardsCgD();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &Prov();
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhC(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhC(n, num++)); }
    auto put = [&](const std::string& n, int ctl)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = ctl;
        p.owner_index       = ctl;
        p.entered_this_turn = false;
        if (n == kWR && ctl == 0) { p.age_counters = war_rider_age; }
        s.battlefield.push_back(p);
    };
    for (const std::string& n : board)     { put(n, 0); }
    for (const std::string& n : opp_board) { put(n, 1); }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> CgArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = arm;
    std::vector<int> v = Prov().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Cg(const GameState& s)       { return CgArm(s, 1); }
std::vector<int> DothLast(const GameState& s) { return CgArm(s, 0); }
std::vector<int> Gen(const GameState& s)      { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

TEST_CASE("CG discard: the deck routes to CreatureGivingProvider")
{
    EnsureCardsCgD();
    const DecisionProvider& p = DetectDecisionProvider(
        DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Creature Giving/Creature Giving.cod")));
    CHECK(std::string(p.Name()) == "CreatureGiving");
}

// Proposal worked example 1. Board Forest + Temple Garden (sources 2: W1 G2). Hand 0 Heath, 1
// Breeding Pool, 2 Overgrown Tomb, 3 Soul Warden, 4 Birds, 5 Suture Priest, 6 Defense, 7 Wurm.
// Colour need (Warden excluded): W1 B3 G1 (Wurm BBB). Cover: Heath (B, tier 2) beats Tomb (B, tier 3)
// on the tie, then Tomb (B), then Breeding Pool covers nothing -> land order 0 2 1. 5 - 2 = 3 sources
// owed: L0, Suture 5, Birds 4, Defense 6, L2 -> sources full. Shed: Warden 3 (blank, before spare
// lands), the spare Breeding Pool 1, Wurm 7, then the tail 2 6 4 5 0.
TEST_CASE("CG discard C1: the Warden is shed before a spare land (generic and DotH-last shed the Wurm)")
{
    const GameState s = MakeCg({ kHeath, kBP, kOT, kSW, kBirds, kSP, kDotH, kWurm }, { kFor, kTG });
    CHECK(Cg(s) == std::vector<int>{ 3, 1, 7, 2, 6, 4, 5, 0 });
    CHECK(Gen(s) == std::vector<int>{ 7, 6, 5, 3, 4, 0, 1, 2 });
    CHECK(DothLast(s) == std::vector<int>{ 7, 5, 3, 4, 0, 1, 2, 6 });
}

// Proposal worked example 2. Board one Forbidden Orchard (1 source, every colour). Hand 0 Forest,
// 1 Birds, 2 Suture, 3 Defense, 4 Crop Rotation (live: a land in play), 5 Phantasm, 6 Wurm, 7
// Enlightened Tutor. Opp projected = 0 + 1 Orchard = 1 < 3 -> the FUEL slot is open. Ladder L0,
// Suture 2, Birds 1, Defense 3, tutor 4, fuel 5. Shed: spare Enlightened Tutor 7 (a Defense is
// kept), Wurm 6, then the tail 5 4 3 1 2 0 -- Defense 4th from the end.
TEST_CASE("CG discard C2: Enlightened Tutor is spare beside a kept Defense of the Heart")
{
    const GameState s = MakeCg({ kFor, kBirds, kSP, kDotH, kCrop, kHP, kWurm, kET }, { kOrch });
    CHECK(Cg(s) == std::vector<int>{ 7, 6, 5, 4, 3, 1, 2, 0 });
    CHECK(Gen(s).front() == 6);
    CHECK(DothLast(s).front() == 6);
}

// Proposal worked example 4. No land in play; the hand's only land is Azorius Chancery, which must
// bounce itself -> dead, shed first. Essence Warden 7 next. Fuel: reach 0, both givers far -> burst
// first: Phantasm 2 > War-Riders 5. Ladder Suture 1, Defense 3, Scrying 4 (no land needed),
// Phantasm 2. Shed 0 7, surplus fuel 5, Wurm 6, tail 2 4 3 1.
TEST_CASE("CG discard C3: a self-bouncing Chancery is dead")
{
    const GameState s = MakeCg({ kChan, kSP, kHP, kDotH, kScry, kWR, kWurm, kEW }, {});
    CHECK(Cg(s) == std::vector<int>{ 0, 7, 5, 6, 2, 4, 3, 1 });
    CHECK(Gen(s).front() == 6);
    CHECK(DothLast(s).front() == 6);
}

// Board: 2 Orchards + our Suture Priest; opponent has one creature -> projected 1 + 2 = 3: FUEL is
// FILLED, and the board's Priest fills DRAIN. Hand 0 Phantasm, 1 War-Riders, 2 Suture, 3 Defense,
// 4 Defense, 5 City, 6 Wurm, 7 Enlightened Tutor. City covers the missing B -> L5; Defense 3 fills
// ENGINE. Shed: spare ET 7, surplus fuel (worst first: War-Riders 1, Phantasm 0), Wurm 6, 2nd Suture
// 2, the extra Defense 4 (copies stack -- last of the overflow), then tail 3 5.
TEST_CASE("CG discard C4: fuel filled by the board; overflow order spare ET > fuel > Wurm > Priest > extra Defense")
{
    const GameState s = MakeCg({ kHP, kWR, kSP, kDotH, kDotH, kCity, kWurm, kET },
                               { kOrch, kOrch, kSP }, { kOppBody });
    CHECK(Cg(s) == std::vector<int>{ 7, 1, 0, 6, 2, 4, 3, 5 });
    CHECK(Gen(s).front() == 6);
    CHECK(DothLast(s).front() == 6);
}

// Board: a Birds only (no land). Reflecting Pool 0 with no non-reflecting land anywhere -> lone,
// dead. Crop Rotation 1 is dead with no land in play -> Scrying 2 takes the tutor slot and Crop
// Rotation is a surplus land tutor. Birds already in play -> the hand Birds 3 is surplus mana.
// Ladder Suture 5, Defense 7, Scrying 2, Phantasm 6. Shed 0, Warden 4, surplus Birds 3, surplus
// tutor 1, then the tail 6 2 7 5.
TEST_CASE("CG discard C5: lone Reflecting Pool, Birds already in play, Crop Rotation without a land")
{
    const GameState s = MakeCg({ kPool, kCrop, kScry, kBirds, kSW, kSP, kHP, kDotH }, { kBirds });
    CHECK(Cg(s) == std::vector<int>{ 0, 4, 3, 1, 6, 2, 7, 5 });
    CHECK(Gen(s).front() == 7);
    CHECK(DothLast(s).front() == 6);
}

// Colours first. Board Forest, Temple Garden, Stomping Ground, Overgrown Tomb (W1 B1 R1 G4); 5 - 4 =
// ONE source owed. The hand's spells need U2 (Phantasm); Breeding Pool (tier 3) makes U, the fetch
// (tier 2: W/B/R/G) does not -> Breeding Pool 1 is kept and the better-tier Heath 0 is the surplus.
// The 5 sources are met (4 + Breeding Pool) before the land-tutor slot, so Crop Rotation 7 is
// SURPLUS MANA (review fix 2026-10-09), shed after the surplus Heath. Ladder L1, Suture 3, Defense 4,
// Phantasm 2 (U: 1 kept source, deficit 1 -> near). Shed Warden 6, Heath 0, Crop Rotation 7,
// War-Riders 5, tail 2 4 3 1.
TEST_CASE("CG discard C6: a land covering a missing colour beats a better-tier land")
{
    const GameState s = MakeCg({ kHeath, kBP, kHP, kSP, kDotH, kWR, kEW, kCrop }, { kFor, kTG, kSG, kOT });
    CHECK(Cg(s) == std::vector<int>{ 6, 0, 7, 5, 2, 4, 3, 1 });
    CHECK(Gen(s).front() == 4);       // generic sheds Defense of the Heart (MV 4)
    CHECK(DothLast(s).front() == 2);  // the patch moves it back and sheds Phantasm
}

// Distance inside FUEL. Board Forest + Stomping Ground (G2 R1, no blue). Hand lands Forest 2, Tree 6
// add no blue: Phantasm {1}{U}{U} is 2 blue short -> FAR; War-Riders is near -> War-Riders 1 takes
// the slot, Phantasm 0 is surplus fuel. Ladder L2, Suture 3, Defense 4, L6, War-Riders 1. Shed
// Wardens 5 7, Phantasm 0, tail 1 6 4 3 2.
TEST_CASE("CG discard C7: a far Hunted Phantasm gives way to a castable War-Riders")
{
    const GameState s = MakeCg({ kHP, kWR, kFor, kSP, kDotH, kSW, kTree, kEW }, { kFor, kSG });
    CHECK(Cg(s) == std::vector<int>{ 5, 7, 0, 1, 6, 4, 3, 2 });
    CHECK(Gen(s).front() == 4);
    CHECK(DothLast(s).front() == 0);
}

// FUEL quota counts the board: Orchard 1 + War-Riders age 2 = 3 -> filled, so the hand Phantasm 0 is
// surplus fuel and sheds before the Wurm 7. With age 0 (projected 1) the slot is open, Phantasm is
// kept (ladder end) and the Wurm sheds first. Ladder L1, Suture 2, Birds 5, Defense 3, L4, Crop 6.
TEST_CASE("CG discard C8: the fuel slot is filled by opponent creatures + Orchards + War-Riders counters")
{
    const std::vector<std::string> hand = { kHP, kFor, kSP, kDotH, kFor, kBirds, kCrop, kWurm };
    const GameState filled = MakeCg(hand, { kOrch, kWR }, {}, LibWithDoth(), 2);
    CHECK(Cg(filled) == std::vector<int>{ 0, 7, 6, 4, 3, 5, 2, 1 });
    const GameState open = MakeCg(hand, { kOrch, kWR }, {}, LibWithDoth(), 0);
    CHECK(Cg(open) == std::vector<int>{ 7, 0, 6, 4, 3, 5, 2, 1 });
    CHECK(Gen(open).front() == 7);
}

// No Defense in hand or play: the first Enlightened Tutor fills ENGINE while a Defense is left in
// the library; the second is spare. Board Forest + Temple Garden; Heath covers the missing B. Ladder
// L3, Suture 2, ET 0, Scrying 7, Phantasm 4. Shed Warden 5, ET 1, Wurm 6, tail 4 7 0 2 3. With no
// Defense left in the library neither tutor can find the engine: both are spare (1 then 0).
TEST_CASE("CG discard C9: Enlightened Tutor fills the engine slot only while it can find a Defense")
{
    const std::vector<std::string> hand = { kET, kET, kSP, kHeath, kHP, kEW, kWurm, kScry };
    const GameState live = MakeCg(hand, { kFor, kTG });
    CHECK(Cg(live) == std::vector<int>{ 5, 1, 6, 4, 7, 0, 2, 3 });
    const GameState dead = MakeCg(hand, { kFor, kTG }, {}, LibWithoutDoth());
    CHECK(Cg(dead) == std::vector<int>{ 5, 1, 0, 6, 4, 7, 2, 3 });
}

// ---- review fixes (2026-10-09) ------------------------------------------------------------------
// FIX 1 -- COLOURS FIRST BEATS THE COUNT (integrator reading): a land supplying a colour a KEPT spell
// needs, that no board / already-kept source supplies, is kept past the 5-source quota; the fuel
// distance counts only kept sources. FIX 2 -- the land tutor is MANA: surplus once 5 sources are met.

// The reviewer's board. Five lands in play (W1 U0 B1 R1 G5), so no hand land is owed. Old code: the
// hand's only blue source, Breeding Pool 0, was surplus mana and index 0, while Phantasm 1 held the
// fuel slot as "near" only because the distance counted that surplus Pool. Now: Suture 3 (white on
// board), Defense 4; the land tutors are surplus mana (FIX 2: 5 sources met), worst first Scrying 7
// then Crop Rotation 5; fuel Phantasm needs blue, none kept -> it would keep the Pool -> 1 blue
// source, deficit 1 -> near, takes the slot and keeps Breeding Pool behind it. Shed 7 5, War-Riders 2,
// Wurm 6, tail 0 1 4 3.
TEST_CASE("CG discard R1: the reviewer's board -- the only blue source is kept for the kept Phantasm")
{
    const GameState s = MakeCg({ kBP, kHP, kWR, kSP, kDotH, kCrop, kWurm, kScry },
                               { kFor, kTG, kSG, kOT, kTree });
    const std::vector<int> o = Cg(s);
    CHECK(o == std::vector<int>{ 7, 5, 2, 6, 0, 1, 4, 3 });
    CHECK(o.front() != 0);   // old code: Breeding Pool at index 0
}

// FIX 1 alone (no land tutor in hand). Same board; hand Breeding Pool 0, Phantasm 1, War-Riders 2,
// Suture 3, Defense 4, Wurm 5, Wurm 6, Suture 7. Ladder Suture 3, Defense 4, Phantasm 1 + its
// Breeding Pool 0. Shed surplus fuel War-Riders 2, Wurms 5 6, 2nd Suture 7, tail 0 1 4 3.
// (Old: Breeding Pool 0 first -- 0 2 5 6 7 1 4 3.)
TEST_CASE("CG discard R2: a colour land for the kept fuel is kept past the quota")
{
    const GameState s = MakeCg({ kBP, kHP, kWR, kSP, kDotH, kWurm, kWurm, kSP },
                               { kFor, kTG, kSG, kOT, kTree });
    CHECK(Cg(s) == std::vector<int>{ 2, 5, 6, 7, 0, 1, 4, 3 });
}

// FIX 1 for a non-fuel spell. Five lands in play with NO white (Forest, Stomping Ground, Overgrown
// Tomb, Breeding Pool, Tree); hand Temple Garden 0, Suture 1, Defense 2, Wurm 3, Wurm 4, Suture 5,
// Defense 6, War-Riders 7. The kept Suture Priest needs W and nothing kept makes it -> Temple Garden
// is kept behind it. Ladder Suture 1, Temple Garden 0, Defense 2, War-Riders 7. Shed Wurms 3 4, 2nd
// Suture 5, extra Defense 6, tail 7 2 0 1. (Old: Temple Garden 0 first -- 0 3 4 5 6 7 2 1.)
TEST_CASE("CG discard R3: the only white source is kept for the kept Suture Priest")
{
    const GameState s = MakeCg({ kTG, kSP, kDotH, kWurm, kWurm, kSP, kDotH, kWR },
                               { kFor, kSG, kOT, kBP, kTree });
    CHECK(Cg(s) == std::vector<int>{ 3, 4, 5, 6, 7, 2, 0, 1 });
}

// FIX 2, the reviewer's board. Five lands + our Suture Priest in play, three opponent creatures
// (fuel filled). Hand Crop Rotation 0, Wurm 1, Wurm 2, Defense 3, Defense 4, Defense 5, Suture 6,
// Suture 7. The 5 sources are met -> Crop Rotation is SURPLUS MANA, shed first, ahead of the Wurms,
// the 2nd Priests and the extra Defenses. Ladder Defense 3. Shed 0, 1 2, 7 6, 5 4, tail 3.
// (Old: Wurm 1 first and Crop Rotation kept in the ladder -- 1 2 7 6 5 4 0 3.)
TEST_CASE("CG discard R4: the land tutor is surplus mana once the 5 sources are met")
{
    const GameState s = MakeCg({ kCrop, kWurm, kWurm, kDotH, kDotH, kDotH, kSP, kSP },
                               { kFor, kTG, kSG, kOT, kTree, kSP }, { kOppBody, kOppBody, kOppBody });
    CHECK(Cg(s) == std::vector<int>{ 0, 1, 2, 7, 6, 5, 4, 3 });
}

// FIX 2's reading pinned (not a regression: old and new agree): "met" is read at the tutor's place
// in the ladder. Board Forest + Temple Garden (2 sources); hand Stomping Ground 0, Overgrown Tomb 1,
// Breeding Pool 2, Crop Rotation 3, Suture 4, Defense 5, Phantasm 6, Wurm 7. Land order (colour
// first: B, then U) 1 2 0. L1 Overgrown Tomb, Suture, Defense, L2 Breeding Pool -> 4 sources, the
// tutor slot is still open -> Crop Rotation kept; L3 Stomping Ground -> 5; Phantasm (1 kept blue,
// near). Shed Wurm 7, tail 6 0 3 2 5 4 1.
TEST_CASE("CG discard R5: the land tutor is kept while the sources are short at its slot")
{
    const GameState s = MakeCg({ kSG, kOT, kBP, kCrop, kSP, kDotH, kHP, kWurm }, { kFor, kTG });
    CHECK(Cg(s) == std::vector<int>{ 7, 6, 0, 3, 2, 5, 4, 1 });
}

TEST_CASE("CG discard: =0 is exactly the shipped DotH-last patch; the three arms differ")
{
    const std::vector<GameState> boards = {
        MakeCg({ kHeath, kBP, kOT, kSW, kBirds, kSP, kDotH, kWurm }, { kFor, kTG }),
        MakeCg({ kHeath, kBP, kHP, kSP, kDotH, kWR, kEW, kCrop }, { kFor, kTG, kSG, kOT }),
        MakeCg({ kPool, kCrop, kScry, kBirds, kSW, kSP, kHP, kDotH }, { kBirds }),
        MakeCg({ kHP, kWR, kSP, kDotH, kDotH, kCity, kWurm, kET }, { kOrch, kOrch, kSP }, { kOppBody }),
    };
    for (const GameState& s : boards)
    {
        // The patch: the generic ranking with every Defense of the Heart moved to the back.
        std::vector<int> want, doth;
        for (int i : Gen(s))
        { (s.players[0].hand[static_cast<std::size_t>(i)].m_name == kDotH ? doth : want).push_back(i); }
        want.insert(want.end(), doth.begin(), doth.end());
        CHECK(DothLast(s) == want);
        CHECK(Cg(s) != DothLast(s));
        CHECK(Cg(s) != Gen(s));
        heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = 0;
        CHECK(Prov().CleanupDiscardShedStable());
        heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = 1;
        CHECK_FALSE(Prov().CleanupDiscardShedStable());
        heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = -1;
    }
    // A board where all three disagree at index 0 (C6): Wardens / Defense / Phantasm.
    const GameState& c6 = boards[1];
    CHECK(Cg(c6).front() == 6);
    CHECK(Gen(c6).front() == 4);
    CHECK(DothLast(c6).front() == 2);
    // The third arm at RUNTIME: buckets off AND MTG_CG_DOTH_LAST=0 -> exactly the generic ranking,
    // so a pooled batch can run generic / patch / buckets as per-job flags.
    for (const GameState& s : boards)
    {
        const std::vector<int> want_gen = Gen(s), want_patch = DothLast(s);   // before pinning the arms
        heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = 0;
        heurarm::t_arm[heurarm::CG_DOTH_LAST]      = 0;
        const std::vector<int> got_gen = Prov().CleanupDiscardCandidates(s, nullptr);
        heurarm::t_arm[heurarm::CG_DOTH_LAST]      = 1;
        const std::vector<int> got_patch = Prov().CleanupDiscardCandidates(s, nullptr);
        heurarm::t_arm[heurarm::CG_DOTH_LAST]      = -1;
        heurarm::t_arm[heurarm::CG_BUCKET_DISCARD] = -1;
        CHECK(got_gen == want_gen);
        CHECK(got_patch == want_patch);
    }
}

TEST_CASE("CG discard: every non-staged index named exactly once; a staged card goes last")
{
    GameState s = MakeCg({ kOrch, kChan, kPool, kBirds, kBirds, kCrop, kScry, kSP, kSP, kHP, kWR,
                           kDotH, kDotH, kET, kSW, kWurm }, { kFor }, { kOppBody, kOppBody });
    s.players[0].hand[4].m_is_staged = true;
    const std::vector<int> o = Cg(s);
    CheckPermutation(s, o);
    CHECK(o.back() == 4);
    for (const GameState& b : { MakeCg({ kChan, kSP, kHP, kDotH, kScry, kWR, kWurm, kEW }, {}),
                                MakeCg({ kHP, kWR, kFor, kSP, kDotH, kSW, kTree, kEW }, { kFor, kSG }),
                                MakeCg({ kET, kET, kSP, kHeath, kHP, kEW, kWurm, kScry }, { kFor, kTG }) })
    { CheckPermutation(b, Cg(b)); }
}
