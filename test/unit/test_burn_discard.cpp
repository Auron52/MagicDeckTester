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

// PROPOSED AMENDMENT MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE (default OFF; the USER decides). The board is the
// T5 cleanup of the integration's held-out A/B game (burn d3 s6105084 gi 83): one Mountain, Swiftspear +
// Goblin Guide in play (next swing 3), three opponent 2/2s, opponent at 9, no land in hand.
// USER design, by hand: live burn walk Skull 3 > Skull 5 (r0) > Blood 7 (kills a 2/2: r2) > Volley 0 >
// Volley 1 (r3) > Blaze 2 > Blaze 6 (no kept land: r4, 1 dmg). Need 9 - 3 = 6: both Skullcracks kept, the
// rest beyond lethal -- except Volley 1, the 2nd Volley with one land in reach (S4). Eidolon is not needed
// (T5 > 3): S7, waived. -> S4 1, S5 6 2 0 7, S7 4, ladder tail 5 3.
// Amended: the two 1-damage Blazes leave S5 and shed in S4 ahead of the land-less Volley -> 2 6 1 0 7 4 5 3.
// With no land-less Volley (B6's board: 3 lands, 1 Volley) the lever changes nothing.
TEST_CASE("burn discard: proposed MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE sheds the 1-damage Blaze before a land-less Volley")
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
    CHECK(Burn(s) == std::vector<int>{ 1, 6, 2, 0, 7, 4, 5, 3 });       // unset = OFF = the USER design
    CHECK(amend(s, 0) == std::vector<int>{ 1, 6, 2, 0, 7, 4, 5, 3 });
    CHECK(amend(s, 1) == std::vector<int>{ 2, 6, 1, 0, 7, 4, 5, 3 });
    CheckPermutation(s, amend(s, 1));

    // A 2-card cleanup (re-ranked per shed: the bucket arm is not prefix-stable) sheds both dry Blazes.
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = 1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_OVER_DRY_BLAZE] = 1;
    GameState t = s;
    const int done = CleanupDiscardShed(t, &kReq, 2, -1, false, true, nullptr);
    heurarm::t_arm[heurarm::BURN_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::BURN_VOLLEY_KEEP_OVER_DRY_BLAZE] = -1;
    REQUIRE(done == 2);
    REQUIRE(t.players[0].graveyard.size() == 2);
    CHECK(t.players[0].graveyard[0].m_name == kBlaze);
    CHECK(t.players[0].graveyard[1].m_name == kBlaze);

    // No land-less Volley -> the order is the USER's exactly (B6).
    const GameState b6 = MakeBurn({ kBlood, kBlaze, kVolley, kBolt, kSkull, kLuts, kGG, kSS },
                                  { kMtn, kMtn, kMtn }, { "Varchild's War-Riders" }, 4);
    CHECK(amend(b6, 1) == std::vector<int>{ 0, 1, 2, 4, 5, 3, 6, 7 });
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
