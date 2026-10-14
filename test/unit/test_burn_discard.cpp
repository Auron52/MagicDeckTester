// Unit tests for the burn cleanup-discard buckets (USER-settled 2026-10-09,
// docs/design/burn-discard-policy-proposal.md "burn: SETTLED"): BurnProvider::CleanupDiscardCandidates
// plus the Eidolon protection waiver (BurnProvider::CleanupDiscardProtectionWaived). Every expected
// order below was derived BY HAND from the design text (the derivation is in each case's comment),
// not read off the code. Each board also pins the GENERIC fallback's index 0 -- the control arm that
// must differ -- and the hatch cases prove MTG_BURN_BUCKET_DISCARD=0 is exactly the generic ranking
// with every required piece protected.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/MulliganProfileIO.h"
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

void EnsureCardsBurnD()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card PhB(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const char* kGG   = "Goblin Guide";
const char* kSS   = "Monastery Swiftspear";
const char* kEid  = "Eidolon of the Great Revel";
const char* kMtn  = "Mountain";
const char* kBolt = "Lightning Bolt";
const char* kSkull = "Skullcrack";
const char* kVolley = "Shard Volley";
const char* kBlaze = "Searing Blaze";
const char* kBlood = "Searing Blood";
const char* kLuts  = "Light Up the Stage";

// burn.profile.json's mulligan.required_pieces (checked against the file in the routing case).
const std::vector<std::string> kReq = { kGG, kSS, kEid };

const BurnProvider& Prov()
{
    static const BurnProvider p;
    return p;
}

// The waiver is consulted through ResolveProvider(state), so the state carries the burn provider,
// exactly as a real game's state does (m_provider is stamped from the deck).
GameState MakeBurn(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                   const std::vector<std::string>& opp_board = {}, int turn = 3, int opp_life = 20)
{
    EnsureCardsBurnD();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.m_provider          = &Prov();
    s.players[1].life     = opp_life;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhB(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(PhB(kMtn, num++)); }
    auto put = [&](const std::string& n, int ctl)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = ctl;
        p.owner_index       = ctl;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    };
    for (const std::string& n : board)     { put(n, 0); }
    for (const std::string& n : opp_board) { put(n, 1); }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> BurnArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = Prov().CleanupDiscardCandidates(s, &kReq);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Burn(const GameState& s) { return BurnArm(s, 1); }
// The generic ranking AS SHIPPED: the waiver is gated on the same flag, so pin it off too.
std::vector<int> Gen(const GameState& s)
{
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 0;
    std::vector<int> v = GenericProvider().CleanupDiscardCandidates(s, &kReq);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    return v;
}
bool Waived(const GameState& s, int hand_idx)
{
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 1;
    const bool w = Prov().CleanupDiscardProtectionWaived(s, s.players[0].hand[static_cast<std::size_t>(hand_idx)]);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    return w;
}

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

TEST_CASE("burn discard: routing, and the profile's required_pieces the waiver exists for")
{
    EnsureCardsBurnD();
    const DecisionProvider& p =
        DetectDecisionProvider(DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/burn/burn.txt")));
    CHECK(std::string(p.Name()) == "Burn");
    const MulliganProfile prof = LoadDeckProfile(ResolveHeuristicDefaultsPath("decks/burn/burn.profile.json"));
    std::vector<std::string> req = prof.required_pieces;
    std::vector<std::string> want = kReq;
    std::sort(req.begin(), req.end());
    std::sort(want.begin(), want.end());
    CHECK(req == want);
    CHECK(prof.discard_protect == DiscardProtectScope::All);
}

// Proposal section 1's worked hand. T3, 1 Mountain in play, no opponent creature, opponent at 20.
// S2: Blaze (5) then Blood (6) are dead. Blaze is dead, so no +1: land quota 3, board 1 -> both hand
// Mountains kept. Bolt kept (need 20). 3 lands >= 2 -> Light Up the Stage kept. Both Goblin Guides
// kept (quota 2) and protected. Ladder L0 L1 R4 D7 -> tail 7 4 1 0; protected 2 3 last (tier C).
TEST_CASE("burn discard B1: dead Blaze/Blood shed first, Blaze before Blood (generic sheds Light Up the Stage)")
{
    const GameState s = MakeBurn({ kMtn, kMtn, kGG, kGG, kBolt, kBlaze, kBlood, kLuts }, { kMtn });
    const std::vector<int> o = Burn(s);
    CHECK(o == std::vector<int>{ 5, 6, 7, 4, 1, 0, 2, 3 });
    CHECK(Gen(s).front() == 7);
}

// Eidolon on our battlefield: the hand copy (0) is a 2nd Eidolon -> S1, and its protection is waived.
// Lands: board 2, quota 3 -> Mountain 1 kept. Burn Bolt 2 > Skullcrack 3 > Volley 4 (3 lands for 1
// Volley), need 20 - 2 (the Eidolon swings) = 18 -> all kept. LUtS 5 kept. Creatures: board 1 ->
// one slot: Goblin Guide 6 kept, Swiftspear 7 spare (protected). Ladder L1 R2 D5 R3 R4 -> tail
// 4 3 5 2 1; Guide / Swiftspear to the protected tier.
TEST_CASE("burn discard B2a: a 2nd Eidolon (one in play) is shed first and is unprotected")
{
    const GameState s = MakeBurn({ kEid, kMtn, kBolt, kSkull, kVolley, kLuts, kGG, kSS },
                                 { kMtn, kMtn, kEid });
    CHECK(Waived(s, 0));
    CHECK_FALSE(Waived(s, 6));   // Goblin Guide stays protected
    CHECK_FALSE(Waived(s, 7));   // Swiftspear stays protected
    CHECK(Burn(s) == std::vector<int>{ 0, 4, 3, 5, 2, 1, 6, 7 });
    const std::vector<int> g = Gen(s);
    CHECK(g == std::vector<int>{ 5, 3, 2, 4, 1, 0, 6, 7 });   // Eidolon protected: tier C
}

// Two Eidolons in hand, none in play, T2, no other creature: copy 1 is the 2nd Eidolon (S1); copy 0
// is the single one and is NEEDED (early, quota 0 + 0 < 2), so it is placed last. Literal waiver:
// with two in hand neither is "the only Eidolon", so both are unprotected now; once copy 1 is shed
// the survivor is protected -- and the order of the rest must not move (prefix stability).
// Lands: board 1, quota 3 -> both kept. Burn Bolt 4, Bolt 5, Skullcrack 6 kept. LUtS 7 kept.
// Ladder L2 L3 R4 D7 R5 R6 -> tail 6 5 7 4 3 2, then the needed Eidolon 0.
TEST_CASE("burn discard B2b: two Eidolons in hand -- the 2nd sheds, the needed one stays last")
{
    GameState s = MakeBurn({ kEid, kEid, kMtn, kMtn, kBolt, kBolt, kSkull, kLuts }, { kMtn }, {}, 2);
    CHECK(Waived(s, 0));
    CHECK(Waived(s, 1));
    const std::vector<int> o = Burn(s);
    CHECK(o == std::vector<int>{ 1, 6, 5, 7, 4, 3, 2, 0 });
    CHECK(Gen(s).front() == 7);

    // The rollout's batched cleanup (bucket arm declares itself unstable -> re-ranked per shed)
    // sheds the 2nd Eidolon, then Skullcrack -- what the per-shed loop gives.
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 1;
    CHECK_FALSE(Prov().CleanupDiscardShedStable());
    GameState t = s;
    const int done = CleanupDiscardShed(t, &kReq, 2, -1, false, true, nullptr);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    REQUIRE(done == 2);
    REQUIRE(t.players[0].graveyard.size() == 2);
    CHECK(t.players[0].graveyard[0].m_name == kEid);
    CHECK(t.players[0].graveyard[1].m_name == kSkull);

    // After the 2nd copy is gone the survivor is the only Eidolon and needed -> protected, and the
    // remaining order is the old one minus its head: hand is now Eid Mtn Mtn Bolt Bolt Skull LUtS.
    s.players[0].hand.erase(s.players[0].hand.begin() + 1);
    CHECK_FALSE(Waived(s, 0));
    CHECK(Burn(s) == std::vector<int>{ 5, 4, 6, 3, 2, 1, 0 });
}

// Single Eidolon, T4 (not early): not needed -> the FIRST spare creature shed (S7), unprotected.
// S1-S6 are empty: lands board 2 + Mountain 0 = quota 3; Bolt 2/6, Skullcrack 3/7, Volley 4 (3
// lands) all kept (need 20, acc 15); one LUtS (5) kept. Ladder L0 R2 D5 R6 R3 R7 R4 -> tail
// 4 7 3 6 5 2 0. At T3 the same Eidolon is NEEDED (board 0 + hand 0 creatures < 2): protected,
// last; everything else unchanged.
TEST_CASE("burn discard B2c: a single Eidolon -- spare when late, kept (protected) when early")
{
    const std::vector<std::string> hand = { kMtn, kEid, kBolt, kSkull, kVolley, kLuts, kBolt, kSkull };
    const GameState late = MakeBurn(hand, { kMtn, kMtn }, {}, 4);
    CHECK(Waived(late, 1));
    CHECK(Burn(late) == std::vector<int>{ 1, 4, 7, 3, 6, 5, 2, 0 });
    CHECK(Gen(late).front() == 5);

    const GameState early = MakeBurn(hand, { kMtn, kMtn }, {}, 3);
    CHECK_FALSE(Waived(early, 1));
    CHECK(Burn(early) == std::vector<int>{ 4, 7, 3, 6, 5, 2, 0, 1 });
}

// T2, but Goblin Guide + Swiftspear in hand fill the creature quota before Eidolon: not needed ->
// S7, waived. Lands board 1, quota 3 -> 3, 4 kept; Bolt 5, Skullcrack 7, LUtS 6 kept. Ladder L3 L4
// R5 D6 R7 -> tail 7 6 5 4 3; Guide / Swiftspear protected (tier C, index order).
TEST_CASE("burn discard B2d: the quota is not short when Guide and Swiftspear are in hand")
{
    const GameState s = MakeBurn({ kEid, kGG, kSS, kMtn, kMtn, kBolt, kLuts, kSkull }, { kMtn }, {}, 2);
    CHECK(Waived(s, 0));
    CHECK(Burn(s) == std::vector<int>{ 0, 7, 6, 5, 4, 3, 1, 2 });
    CHECK(Gen(s).front() == 6);
}

// Opponent has a 2/2: Blaze and Blood are live. A live Blaze -> land quota 4; board 2 -> two hand
// Mountains kept (0, 1), the other two surplus (S3, worst = last first: 6 then 2). Kept hand land ->
// Blaze deals 3 (rank above Blood); Blood kills the 2/2 (3 face). Order Bolt 4 > Blaze 3 > Blood 7,
// all kept. Ladder L0 L1 R4 R3 R7 -> tail 7 3 4 1 0; Goblin Guide protected last.
// Same hand with NO opponent creature: Blaze/Blood dead (S2, Blaze first), the dead Blaze does not
// raise the quota -> 3: Mountain 0 kept, 6 2 1 surplus; Bolt kept. Tail 4 0.
TEST_CASE("burn discard B3: lands beyond quota, +1 only for a LIVE Blaze")
{
    const std::vector<std::string> hand = { kMtn, kMtn, kMtn, kBlaze, kBolt, kGG, kMtn, kBlood };
    const GameState live = MakeBurn(hand, { kMtn, kMtn }, { kGG });
    CHECK(Burn(live) == std::vector<int>{ 6, 2, 7, 3, 4, 1, 0, 5 });
    CHECK(Gen(live).front() == 3);

    const GameState dead = MakeBurn(hand, { kMtn, kMtn });
    CHECK(Burn(dead) == std::vector<int>{ 3, 7, 6, 2, 1, 4, 0, 5 });
}

// One Mountain in play, none in hand: the 1st Volley (0) has its land, Volleys 1 and 6 do not (S4).
// Below 2 lands -> no Light Up the Stage is kept (S6, last first: 4 then 3). Kept burn Bolt 2 >
// Skullcrack 5 > Volley 0 -> tail 0 5 2; Swiftspear protected last.
TEST_CASE("burn discard B4: one land per Shard Volley; Light Up the Stage needs 2 lands")
{
    const GameState s = MakeBurn({ kVolley, kVolley, kBolt, kLuts, kLuts, kSkull, kVolley, kSS },
                                 { kMtn }, {}, 2);
    CHECK(Burn(s) == std::vector<int>{ 1, 6, 4, 3, 0, 5, 2, 7 });
    CHECK(Gen(s).front() == 3);
}

// Opponent at 5, a Goblin Guide in play (2 power) -> the hand owes 3. Bolt 1 covers it; Bolt 3,
// Skullcrack 2, Volley 4 are beyond lethal (S5, worst first: 4 2 3). Board 3 lands = quota -> both
// hand Mountains surplus (S3: 7 0). One LUtS (5) kept, the 2nd (6) is S6. Ladder R1 D5 -> tail 5 1.
TEST_CASE("burn discard B5: burn beyond lethal sheds after the spare lands, before the 2nd Light Up the Stage")
{
    const GameState s = MakeBurn({ kMtn, kBolt, kSkull, kBolt, kVolley, kLuts, kLuts, kMtn },
                                 { kMtn, kMtn, kMtn, kGG }, {}, 4, 5);
    CHECK(Burn(s) == std::vector<int>{ 7, 0, 4, 2, 3, 6, 5, 1 });
    CHECK(Gen(s).front() == 5);
}

// Opponent has a 3/4 (Blood cannot kill it: 0 face) and we hold no land (Blaze: 1 face). Burn order
// Bolt 3 > Skullcrack 4 > Volley 2 > Blaze-without-land 1 > Blood-that-cannot-kill 0, all kept
// (need 20). Ladder R3 D5 R4 R2 R1 R0 -> tail 0 1 2 4 5 3; Guide 6 / Swiftspear 7 protected.
TEST_CASE("burn discard B6: Shard Volley above the land-less Blaze; a Blood that cannot kill is last")
{
    const GameState s = MakeBurn({ kBlood, kBlaze, kVolley, kBolt, kSkull, kLuts, kGG, kSS },
                                 { kMtn, kMtn, kMtn }, { "Varchild's War-Riders" }, 4);
    CHECK(Burn(s) == std::vector<int>{ 0, 1, 2, 4, 5, 3, 6, 7 });
    CHECK(Gen(s).front() == 5);
}

// MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE (USER-adopted 2026-10-13, default ON). The board is the T5 cleanup of
// the integration's held-out A/B game (burn d3 s6105084 gi 83): one Mountain, Swiftspear + Goblin Guide in
// play (next swing 3), three opponent 2/2s, opponent at 9, no land in hand.
// Without the amendment, by hand: live burn walk Skull 3 > Skull 5 (r0) > Blood 7 (kills a 2/2: r2) >
// Volley 0 > Volley 1 (r3) > Blaze 2 > Blaze 6 (no kept land: r4, 1 dmg). Need 9 - 3 = 6: both Skullcracks
// kept, the rest beyond lethal -- except Volley 1, the 2nd Volley with one land in reach (S4). Eidolon is not
// needed (T5 > 3): S7, waived. -> S4 1, S5 6 2 0 7, S7 4, ladder tail 5 3.
// With it: the two 1-damage Blazes leave S5 and shed in S4 ahead of the land-less Volley -> 2 6 1 0 7 4 5 3.
// With no land-less Volley (B6's board: 3 lands, 1 Volley) the lever changes nothing.
TEST_CASE("burn discard: MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE sheds the 1-damage Blaze before a land-less Volley")
{
    const GameState s = MakeBurn({ kVolley, kVolley, kBlaze, kSkull, kEid, kSkull, kBlaze, kBlood },
                                 { kMtn, kSS, kGG }, { kGG, kGG, kGG }, 5, 9);
    auto amend = [](const GameState& g, int on)
    {
        heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_OVER_DRY_BLAZE] = static_cast<std::int8_t>(on);
        std::vector<int> v = Burn(g);
        heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_OVER_DRY_BLAZE] = -1;
        return v;
    };
    CHECK(Burn(s) == std::vector<int>{ 2, 6, 1, 0, 7, 4, 5, 3 });       // unset = ON (the default)
    CHECK(amend(s, 1) == std::vector<int>{ 2, 6, 1, 0, 7, 4, 5, 3 });
    CHECK(amend(s, 0) == std::vector<int>{ 1, 6, 2, 0, 7, 4, 5, 3 });   // =0: the design without it
    CheckPermutation(s, amend(s, 1));

    // A 2-card cleanup (re-ranked per shed: the bucket arm is not prefix-stable) sheds both dry Blazes.
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 1;
    GameState t = s;
    const int done = CleanupDiscardShed(t, &kReq, 2, -1, false, true, nullptr);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    REQUIRE(done == 2);
    REQUIRE(t.players[0].graveyard.size() == 2);
    CHECK(t.players[0].graveyard[0].m_name == kBlaze);
    CHECK(t.players[0].graveyard[1].m_name == kBlaze);

    // No land-less Volley -> the order is the design's exactly (B6), on either arm.
    const GameState b6 = MakeBurn({ kBlood, kBlaze, kVolley, kBolt, kSkull, kLuts, kGG, kSS },
                                  { kMtn, kMtn, kMtn }, { "Varchild's War-Riders" }, 4);
    CHECK(amend(b6, 1) == std::vector<int>{ 0, 1, 2, 4, 5, 3, 6, 7 });
    CHECK(amend(b6, 0) == std::vector<int>{ 0, 1, 2, 4, 5, 3, 6, 7 });
}

// ---- the land-light Shard Volley variants (PROPOSED 2026-10-14, all default OFF) -----------------------------
// USER 2026-10-14: "that rule is probably too aggressive when we are land-light. At least 1 could always be
// kept and 2 possibly. ... It could be second ranking, 'shard volley with no land' that loses to more of the
// potential options." Every expected order below is derived by hand from the variant's definition
// (DecisionProviders.cpp, at the readers); tier C (protected Guide / Swiftspear) is MV desc, then index.
namespace
{
struct VolleyArm
{
    int k1 = -1, k2 = -1, floor = -1, rank = -1, late = -1;
};
std::vector<int> BurnVolley(const GameState& s, VolleyArm a)
{
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP1]       = static_cast<std::int8_t>(a.k1);
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP2]       = static_cast<std::int8_t>(a.k2);
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_FLOOR]  = static_cast<std::int8_t>(a.floor);
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_RANK] = static_cast<std::int8_t>(a.rank);
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_LATE] = static_cast<std::int8_t>(a.late);
    std::vector<int> v = Burn(s);
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP1]       = -1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP2]       = -1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_FLOOR]  = -1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_RANK] = -1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_LATE] = -1;
    return v;
}
const VolleyArm kCur{ 0, 0, 0, 0, 0 };
const VolleyArm kK1{ 1, 0, 0, 0, 0 };
const VolleyArm kK2{ 0, 1, 0, 0, 0 };
const VolleyArm kF1{ 1, 0, 1, 0, 0 };
const VolleyArm kF2{ 0, 1, 1, 0, 0 };
const VolleyArm kRank{ 0, 0, 0, 1, 0 };
const VolleyArm kLate{ 0, 0, 0, 0, 1 };
const VolleyArm kK1Late{ 1, 0, 0, 0, 1 };
const VolleyArm kK2Late{ 0, 1, 0, 0, 1 };
}   // namespace

// d0 seed 9322621 gi 12620's T2 cleanup: no land anywhere, no opponent creature. Bolt 0, Skullcrack 4 (r0) kept;
// the Volley 1 has no land (0 lands < 1) -> S4. Below 2 lands no Light Up the Stage is kept (S6: 7 then 2).
// Creatures GG 6, Swiftspear 3 kept, Swiftspear 5 spare (all protected -> tier C 3 5 6). Ladder R0 R4.
//   current: S4 1, S6 7 2, tail 4 0                  -> 1 7 2 4 0 | 3 5 6   (sheds the Volley; generic: LUtS 2)
//   every variant: the lone Volley is kept (K1 / K2 / F1 / F2: 0 + K >= 1; RANK: never S4; LATE: after S6),
//   so the unkept Light Up the Stage goes first. K / RANK: ladder R0 R4 R1 -> 7 2 1 4 0; LATE: S6 7 2, late 1,
//   tail 4 0 -> the same order.
TEST_CASE("burn discard: land-light Volley variants keep the lone land-less Volley over an unkept Light Up the Stage")
{
    const GameState s = MakeBurn({ kBolt, kVolley, kLuts, kSS, kSkull, kSS, kGG, kLuts }, {}, {}, 2);
    CHECK(BurnVolley(s, kCur) == std::vector<int>{ 1, 7, 2, 4, 0, 3, 5, 6 });
    CHECK(Burn(s) == BurnVolley(s, kCur));   // all unset = all off
    CHECK(Gen(s).front() == 2);
    const std::vector<int> keep = { 7, 2, 1, 4, 0, 3, 5, 6 };
    for (const VolleyArm& a : { kK1, kK2, kF1, kF2, kRank, kLate, kK1Late, kK2Late })
    {
        CHECK(BurnVolley(s, a) == keep);
        CheckPermutation(s, BurnVolley(s, a));
    }
}

// d0 seed 9318293 gi 8292's T3 cleanup: no land anywhere; Volleys 0, 4; Bolt 2, Skullcrack 6; GG 7, Swiftspears
// 1 / 5, Eidolon 3. Eidolon is NOT needed (GG + 2 Swiftspears fill the quota): S7, waived (MV 2, unprotected).
// Burn walk Bolt 2 > Skullcrack 6 > Volley 0 > Volley 4. Kept creatures GG 7, Swiftspear 1; Swiftspear 5 spare.
//   current / F-less LATE (nothing in S5 / S6): S4 0 4, S7 3, tail 6 2      -> 0 4 3 6 2 | 1 5 7
//   K1 = F1 = K1+LATE: Volley 0 ordinary (ladder R2 R6 R0), S4 4            -> 4 3 0 6 2 | 1 5 7
//   K2 = F2 = RANK = K2+LATE: both ordinary (ladder R2 R6 R0 R4), S7 3      -> 3 4 0 6 2 | 1 5 7
//   generic (Eidolon protected): Skullcrack 6 first.
TEST_CASE("burn discard: land-light Volley variants on two land-less Volleys and a spare Eidolon")
{
    const GameState s = MakeBurn({ kVolley, kSS, kBolt, kEid, kVolley, kSS, kSkull, kGG }, {}, {}, 3);
    CHECK(Waived(s, 3));
    CHECK(Gen(s).front() == 6);
    const std::vector<int> cur = { 0, 4, 3, 6, 2, 1, 5, 7 };
    const std::vector<int> one = { 4, 3, 0, 6, 2, 1, 5, 7 };
    const std::vector<int> two = { 3, 4, 0, 6, 2, 1, 5, 7 };
    CHECK(BurnVolley(s, kCur) == cur);
    CHECK(BurnVolley(s, kLate) == cur);
    CHECK(BurnVolley(s, kK1) == one);
    CHECK(BurnVolley(s, kF1) == one);
    CHECK(BurnVolley(s, kK1Late) == one);
    CHECK(BurnVolley(s, kK2) == two);
    CHECK(BurnVolley(s, kF2) == two);
    CHECK(BurnVolley(s, kRank) == two);
    CHECK(BurnVolley(s, kK2Late) == two);
}

// The board that separates every arm. T4, one Mountain in play, none in hand, no opponent creature: Volleys
// 0 1 2, Light Up the Stage 3, Bolt 4, Skullcrack 5, GG 6, Eidolon 7 (T4: not needed -> S7, waived, MV 2).
// Lands 1 -> the 1st Volley holds it. Below 2 lands the Light Up the Stage is unkept (S6). Burn walk Bolt 4 >
// Skullcrack 5 > Volley 0 > 1 > 2; GG kept (protected, tier C).
//   current (= F1: max(1, 1) = 1):  S4 1 2, S6 3, S7 7, tail 0 5 4             -> 1 2 3 7 0 5 4 6
//   K1 (= F2: max(1, 2) = 2):       Volley 1 ordinary; S4 2, S6 3, S7 7, tail 1 0 5 4 -> 2 3 7 1 0 5 4 6
//   K2 (lands + 2 = 3) = RANK:      all ordinary; S6 3, S7 7, tail 2 1 0 5 4  -> 3 7 2 1 0 5 4 6
//   LATE:                           S6 3, late 1 2, S7 7, tail 0 5 4           -> 3 1 2 7 0 5 4 6
//   K1+LATE:                        S6 3, late 2, S7 7, tail 1 0 5 4           -> 3 2 7 1 0 5 4 6
//   RANK supersedes the others: RANK+K1, RANK+LATE = RANK.
TEST_CASE("burn discard: land-light Volley variants -- slack vs floor, rank vs late")
{
    const GameState s = MakeBurn({ kVolley, kVolley, kVolley, kLuts, kBolt, kSkull, kGG, kEid }, { kMtn }, {}, 4);
    CHECK(Waived(s, 7));
    const std::vector<int> cur  = { 1, 2, 3, 7, 0, 5, 4, 6 };
    const std::vector<int> k1   = { 2, 3, 7, 1, 0, 5, 4, 6 };
    const std::vector<int> all  = { 3, 7, 2, 1, 0, 5, 4, 6 };
    const std::vector<int> late = { 3, 1, 2, 7, 0, 5, 4, 6 };
    CHECK(BurnVolley(s, kCur) == cur);
    CHECK(BurnVolley(s, kF1) == cur);
    CHECK(BurnVolley(s, kK1) == k1);
    CHECK(BurnVolley(s, kF2) == k1);
    CHECK(BurnVolley(s, kK2) == all);
    CHECK(BurnVolley(s, kRank) == all);
    CHECK(BurnVolley(s, kLate) == late);
    CHECK(BurnVolley(s, kK1Late) == std::vector<int>{ 3, 2, 7, 1, 0, 5, 4, 6 });
    CHECK(BurnVolley(s, kK2Late) == all);
    CHECK(BurnVolley(s, VolleyArm{ 1, 0, 0, 1, 0 }) == all);
    CHECK(BurnVolley(s, VolleyArm{ 0, 1, 1, 1, 1 }) == all);
    for (const VolleyArm& a : { kCur, kK1, kK2, kF1, kF2, kRank, kLate, kK1Late, kK2Late })
    { CheckPermutation(s, BurnVolley(s, a)); }

    // The 2-card cleanup re-ranks per shed: LATE sheds the Light Up the Stage, then a land-less Volley.
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD]     = 1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_LATE] = 1;
    GameState t = s;
    const int done = CleanupDiscardShed(t, &kReq, 2, -1, false, true, nullptr);
    heurarm::t_arm[heurarm::BURN_VOLLEY_NOLAND_LATE] = -1;
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD]     = -1;
    REQUIRE(done == 2);
    REQUIRE(t.players[0].graveyard.size() == 2);
    CHECK(t.players[0].graveyard[0].m_name == kLuts);
    CHECK(t.players[0].graveyard[1].m_name == kVolley);
}

// The amendment (MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE, ON) under the variants. Board A: one Mountain, an
// opponent 2/2 (the Blaze is live but has no kept land: 1 damage, KEPT -- need 20), T4. Volleys 0 1, Blaze 2,
// LUtS 3 (unkept, S6), Bolt 4, GG 5, Eidolon 6 (S7), Skullcrack 7. Walk Bolt 4 > Skullcrack 7 > Volley 0 (the
// land) > Volley 1 (none) > Blaze 2 (r4).
//   current: the dry Blaze moves up to S4 ahead of Volley 1: S4 2 1, S6 3, S7 6, tail 0 7 4  -> 2 1 3 6 0 7 4 5
//   LATE:    S6 3, then the KEPT dry Blaze moves up with the late Volley: 2 1, S7 6         -> 3 2 1 6 0 7 4 5
//   K1 = RANK: no land-less Volley, the amendment is idle; the dry Blaze is the lowest kept burn, behind the
//            unkept LUtS and the spare Eidolon: S6 3, S7 6, tail 2 1 0 7 4                -> 3 6 2 1 0 7 4 5
//   F1 (max(1, 1) = 1) = current.
// Board B is the amendment's own board (gi 83, lethal assembled): both dry Blazes are BEYOND lethal (S5), so
// under LATE they shed in S5 where they are, still ahead of the late Volley 1: S5 6 2 0 7, late 1, S7 4.
TEST_CASE("burn discard: land-light Volley variants keep the 1-damage Blaze below a land-less Volley")
{
    const GameState a = MakeBurn({ kVolley, kVolley, kBlaze, kLuts, kBolt, kGG, kEid, kSkull }, { kMtn }, { kGG }, 4);
    CHECK(BurnVolley(a, kCur) == std::vector<int>{ 2, 1, 3, 6, 0, 7, 4, 5 });
    CHECK(BurnVolley(a, kF1) == std::vector<int>{ 2, 1, 3, 6, 0, 7, 4, 5 });
    CHECK(BurnVolley(a, kLate) == std::vector<int>{ 3, 2, 1, 6, 0, 7, 4, 5 });
    CHECK(BurnVolley(a, kK1) == std::vector<int>{ 3, 6, 2, 1, 0, 7, 4, 5 });
    CHECK(BurnVolley(a, kRank) == std::vector<int>{ 3, 6, 2, 1, 0, 7, 4, 5 });

    const GameState b = MakeBurn({ kVolley, kVolley, kBlaze, kSkull, kEid, kSkull, kBlaze, kBlood },
                                 { kMtn, kSS, kGG }, { kGG, kGG, kGG }, 5, 9);
    CHECK(BurnVolley(b, kCur) == std::vector<int>{ 2, 6, 1, 0, 7, 4, 5, 3 });   // the amendment test's order
    CHECK(BurnVolley(b, kLate) == std::vector<int>{ 6, 2, 0, 7, 1, 4, 5, 3 });
    // K1 / RANK: both Volleys hold a place in the walk, beyond lethal; no land-less Volley, amendment idle.
    CHECK(BurnVolley(b, kK1) == std::vector<int>{ 6, 2, 1, 0, 7, 4, 5, 3 });
    CHECK(BurnVolley(b, kRank) == std::vector<int>{ 6, 2, 1, 0, 7, 4, 5, 3 });
}

TEST_CASE("burn discard: MTG_BURN_BUCKET_DISCARD=0 is exactly the generic ranking, protection intact")
{
    const std::vector<GameState> boards = {
        MakeBurn({ kMtn, kMtn, kGG, kGG, kBolt, kBlaze, kBlood, kLuts }, { kMtn }),
        MakeBurn({ kEid, kEid, kMtn, kMtn, kBolt, kBolt, kSkull, kLuts }, { kMtn }, {}, 2),
        MakeBurn({ kEid, kMtn, kBolt, kSkull, kVolley, kLuts, kGG, kSS }, { kMtn, kMtn, kEid }),
        MakeBurn({ kMtn, kEid, kBolt, kSkull, kVolley, kLuts, kBolt, kSkull }, { kMtn, kMtn }, {}, 4),
    };
    for (const GameState& s : boards)
    {
        const std::vector<int> off = BurnArm(s, 0);
        CHECK(off == Gen(s));
        GameState raw = s;            // the default provider: no waiver hook at all
        raw.m_provider = nullptr;
        CHECK(off == GenericProvider().CleanupDiscardCandidates(raw, &kReq));
        CHECK(off != Burn(s));        // the control arm differs
        heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 0;
        for (std::size_t i = 0; i < s.players[0].hand.size(); ++i)
        { CHECK_FALSE(Prov().CleanupDiscardProtectionWaived(s, s.players[0].hand[i])); }
        CHECK(Prov().CleanupDiscardShedStable());
        heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    }
}

TEST_CASE("burn discard: every non-staged index named exactly once; a staged card goes last")
{
    GameState s = MakeBurn({ kMtn, kMtn, kGG, kEid, kBolt, kBlaze, kBlood, kLuts, kSkull, kVolley },
                           { kMtn, kSS }, { kGG }, 3, 14);
    s.players[0].hand[8].m_is_staged = true;   // a Light Up the Stage exile: not in hand for CR 514.1
    const std::vector<int> o = Burn(s);
    CheckPermutation(s, o);
    CHECK(o.back() == 8);
    for (const GameState& b : { MakeBurn({ kVolley, kVolley, kBolt, kLuts, kLuts, kSkull, kVolley, kSS }, { kMtn }, {}, 2),
                                MakeBurn({ kMtn, kBolt, kSkull, kBolt, kVolley, kLuts, kLuts, kMtn }, { kMtn, kMtn, kMtn, kGG }, {}, 4, 5),
                                MakeBurn({ kBlood, kBlaze, kVolley, kBolt, kSkull, kLuts, kGG, kSS }, { kMtn, kMtn, kMtn }, { "Varchild's War-Riders" }, 4) })
    { CheckPermutation(b, Burn(b)); }
}
