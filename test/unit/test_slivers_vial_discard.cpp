// Unit tests for the USER-settled slivers_vial cleanup-discard rule (2026-10-09):
// VialProvider::CleanupDiscardCandidates, implementing the "slivers_vial: SETTLED" block of
// docs/design/slivers-vial-discard-policy-proposal.md --
//   KEEP  coloured land -> anthem -> coloured land -> first unique grant -> curve fill (lords first)
//         -> 3rd land -> rest of the fill -> soft 4th land
//   SHED  dominated Ziggurat / uncastable Vial -> Vial after T1 -> lands past the 4th (Ziggurat,
//         duals, Mutavault last) -> creatures outside the fill (1-power first) -> soft 4th land ->
//         fill reversed.
// Every expected order is derived BY HAND from that text (the derivation is in each case's comment);
// every case also checks the GENERIC fallback's index 0 -- the control arm that must differ.
// vial_target_mv is 2, as GoldFishRunner stamps it for this decklist (25 of 35 creatures are MV 2).
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

void EnsureCardsSv()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeSv(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                 int turn = 3, int vial_target_mv = 2)
{
    EnsureCardsSv();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.vial_target_mv      = vial_target_mv;
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

std::vector<int> Sv(const GameState& s, int arm = 1)
{
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = VialProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::SLIVERS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kZig      = "Ancient Ziggurat";
const char* kCavern   = "Cavern of Souls";
const char* kCourt    = "Secluded Courtyard";
const char* kTerr     = "Unclaimed Territory";
const char* kHive     = "Sliver Hive";
const char* kMuta     = "Mutavault";
const char* kVial     = "Aether Vial";
const char* kPool     = "Thrumming Hivepool";
const char* kCloud    = "Cloudshredder Sliver";
const char* kCrystal  = "Crystalline Sliver";
const char* kGale     = "Galerider Sliver";
const char* kHatch    = "Hatchery Sliver";
const char* kHiber    = "Hibernation Sliver";
const char* kLeech    = "Leeching Sliver";
const char* kMuscle   = "Muscle Sliver";
const char* kPlated   = "Plated Sliver";
const char* kPred     = "Predatory Sliver";
const char* kSinew    = "Sinew Sliver";
const char* kStrike   = "Striking Sliver";

}   // namespace

TEST_CASE("slivers discard: the proposal's hand -- Vial after T1 first, Ziggurat is the soft 4th")
{
    // Board Cavern + Courtyard: both coloured, so "2 of them coloured" is met. The 3rd land is the
    // Cavern (a dual beats the dominated Ziggurat for every slot) and the soft 4th is the Ziggurat --
    // the only land left, so not dominated. Turn 3: the Vial (5) is surplus -> index 0. Mana 3 / 4 / 4
    // (the Ziggurat pays from turn 2): the anthem slot takes Muscle (2); the grant slot takes the
    // Hivepool (6) -- double strike + haste, held even though it costs 6 with no Sliver in play; the
    // fill around them casts the other Muscle and both 1-drops. Nothing is outside the fill, so the
    // soft 4th (0) is next; then the fill reversed: rest of the fill latest-scheduled first (Plated
    // 7 on turn 2, Galerider 4 on turn 1), the 3rd land (1), the fill lord (Muscle 3), the first
    // grant (Hivepool 6), the anthem (Muscle 2).
    const GameState s = MakeSv({ kZig, kCavern, kMuscle, kMuscle, kGale, kVial, kPool, kPlated },
                               { kCavern, kCourt });
    CHECK(Sv(s) == std::vector<int>{ 5, 0, 7, 4, 1, 3, 6, 2 });
    CHECK(Gen(s).front() == 6);   // generic sheds the Hivepool (MV 6)

    // The same with vial_target_mv unstamped: the dominant MV is derived from the hand (2) instead.
    const GameState t = MakeSv({ kZig, kCavern, kMuscle, kMuscle, kGale, kVial, kPool, kPlated },
                               { kCavern, kCourt }, 3, 0);
    CHECK(Sv(t) == std::vector<int>{ 5, 0, 7, 4, 1, 3, 6, 2 });
}

TEST_CASE("slivers discard: a Ziggurat past the 4th is dominated -> first; Mutavault is the last surplus land")
{
    // Board 4 duals: the soft 4th is met, every hand land is surplus. The Ziggurat goes first (S0),
    // then the surplus lands with Mutavault LAST among them.
    const GameState s = MakeSv({ kMuscle, kZig, kSinew, kPlated, kGale, kMuta, kPred, kCrystal },
                               { kCavern, kCourt, kTerr, kHive }, 4);
    const std::vector<int> o = Sv(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 1);
    CHECK(o[1] == 5);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 0);

    const GameState t = MakeSv({ kMuta, kCavern, kZig, kSinew, kMuscle, kPlated, kGale, kStrike },
                               { kCavern, kCourt, kTerr, kHive }, 4);
    const std::vector<int> u = Sv(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 2);   // Ziggurat
    CHECK(u[1] == 1);   // the dual
    CHECK(u[2] == 0);   // Mutavault last among the surplus lands
    CHECK(Gen(t).front() == 3);
}

TEST_CASE("slivers discard: LORDS FIRST -- three anthems over a wider 1-drop curve")
{
    // Board Cavern + Courtyard: 2 mana a turn, 6 in the window. Every 6-mana set fills the curve, and
    // the most lords is three -- Muscle, Predatory, Sinew (the Knights rule would keep the five
    // 1-drops instead). Muscle (0) is the anthem slot. Outside the fill, 1-power first (later hand
    // card first among equals): Galerider 7, Plated 6, Galerider 4, Plated 3, then the 2-power
    // Crystalline 5. The fill lords latest-scheduled first: Sinew 2 (turn 3), Predatory 1 (turn 2);
    // the anthem last.
    const GameState s = MakeSv({ kMuscle, kPred, kSinew, kPlated, kGale, kCrystal, kPlated, kGale },
                               { kCavern, kCourt });
    CHECK(Sv(s) == std::vector<int>{ 7, 6, 4, 3, 5, 2, 1, 0 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("slivers discard: the first Hatchery counts as a lord; a 2nd is a plain 2-power body")
{
    // Board Cavern + Courtyard + a Muscle Sliver: 2 mana a turn. No anthem or unique grant in hand.
    // The fill fills the curve with three 2-drops: the live Hatchery (0, the lord), then Leeching (4)
    // > 2-power -- the 2nd Hatchery (1) beats Crystalline / Hibernation on hand order. Outside,
    // 1-power first: Striking 6, Galerider 3, Plated 2, then 2-power Hibernation 7, Crystalline 5.
    // The fill reversed: rest of the fill latest-first (Hatchery 1 turn 3, Leeching 4 turn 2), then
    // the lord (Hatchery 0).
    const GameState s = MakeSv({ kHatch, kHatch, kPlated, kGale, kLeech, kCrystal, kStrike, kHiber },
                               { kCavern, kCourt, kMuscle });
    CHECK(Sv(s) == std::vector<int>{ 6, 3, 2, 7, 5, 1, 4, 0 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("slivers discard: a grant already in play makes Cloudshredder a plain 1-power body")
{
    // Haste is on the battlefield (a Cloudshredder): the hand Cloudshredder (0) is a plain 1/1 and
    // sheds with the 1-drops (outside the 3-anthem fill), last of them by hand order.
    const GameState s = MakeSv({ kCloud, kMuscle, kPlated, kGale, kStrike, kSinew, kPlated, kPred },
                               { kCavern, kCourt, kCloud });
    CHECK(Sv(s) == std::vector<int>{ 6, 4, 3, 2, 0, 7, 5, 1 });
    CHECK(Gen(s).front() == 0);

    // No haste in play: Cloudshredder is the first unique grant -- slotted after the fill lords, and
    // the fill around the anthem + grant keeps Sinew (2 lords), so Predatory (7) is outside.
    const GameState t = MakeSv({ kCloud, kMuscle, kPlated, kGale, kStrike, kSinew, kPlated, kPred },
                               { kCavern, kCourt });
    CHECK(Sv(t) == std::vector<int>{ 6, 4, 3, 2, 7, 5, 0, 1 });
    CHECK(Gen(t).front() == 0);
}

TEST_CASE("slivers discard: the Vial is kept only as a turn-1 engine; a 2nd is always shed")
{
    // Turn 1, nothing in play. The 2nd Vial (6) -> index 0; the first is kept (agent reading: right
    // behind the anthem). One land (Cavern, coloured land #1) -> 1 mana a turn: neither anthem fits
    // the window; Muscle (2) still takes the anthem slot, Sinew (3) is outside the fill. The fill is
    // the three 1-drops, latest-scheduled first (Striking 7, Galerider 5, Plated 4). Then the Vial (0),
    // the anthem (2), the coloured land (1).
    const GameState s = MakeSv({ kVial, kCavern, kMuscle, kSinew, kPlated, kGale, kVial, kStrike },
                               {}, 1);
    CHECK(Sv(s) == std::vector<int>{ 6, 3, 7, 5, 4, 0, 2, 1 });
    CHECK(Gen(s).front() == 2);

    // Turn 3: every Vial is surplus.
    const GameState t = MakeSv({ kVial, kCavern, kMuscle, kSinew, kPlated, kGale, kVial, kStrike },
                               {}, 3);
    const std::vector<int> u = Sv(t);
    REQUIRE(u.size() == 8);
    CHECK(u[0] == 0);
    CHECK(u[1] == 6);
    CheckPermutation(t, u);
}

TEST_CASE("slivers discard: a Vial nothing can pay for (only a Ziggurat) is dead even on turn 1")
{
    // Ancient Ziggurat's mana is creature-spell-only, so it cannot pay the Vial's {1}: the Vial is
    // uncastable -> first. (The Ziggurat itself is coloured land #1 here -- the only land -- so it is
    // not dominated.)
    const GameState s = MakeSv({ kVial, kZig, kMuscle, kSinew, kPlated, kGale, kStrike, kPred }, {}, 1);
    const std::vector<int> o = Sv(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 2);

    // With a Cavern instead the Vial is castable and kept as the turn-1 engine; the lowest outside
    // card (Predatory 7, an anthem the 1-mana curve cannot fit) leads.
    const GameState t = MakeSv({ kVial, kCavern, kMuscle, kSinew, kPlated, kGale, kStrike, kPred }, {}, 1);
    CHECK(Sv(t).front() == 7);
}

TEST_CASE("slivers discard: the land targets are DERIVED from the dominant creature MV, not constants")
{
    // Board Cavern; hand Courtyard + Territory. Dominant MV 2 -> 3 lands + a soft 4th: both are kept,
    // so neither leads. Dominant MV 1 (a hypothetical other Vial deck) -> 2 lands, soft 2: the
    // Territory is surplus -> index 0.
    const std::vector<std::string> hand = { kCourt, kTerr, kMuscle, kSinew, kPlated, kGale, kStrike, kPred };
    const GameState s = MakeSv(hand, { kCavern }, 3, 2);
    const std::vector<int> o = Sv(s);
    REQUIRE(o.size() == 8);
    CHECK(o[0] != 1);
    CheckPermutation(s, o);
    const GameState t = MakeSv(hand, { kCavern }, 3, 1);
    CHECK(Sv(t).front() == 1);
}

TEST_CASE("slivers discard: MTG_SLIVERS_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeSv({ kZig, kCavern, kMuscle, kMuscle, kGale, kVial, kPool, kPlated },
                               { kCavern, kCourt });
    CHECK(Sv(s, 0) == Gen(s));
    CHECK(Sv(s, 1) != Gen(s));
    CHECK(VialProvider().CleanupDiscardShedStable());   // the shared base keeps the batched path
}

TEST_CASE("slivers discard: a staged card is never named by the rule and sorts last")
{
    GameState s = MakeSv({ kMuscle, kPred, kSinew, kPlated, kGale, kCrystal, kPlated, kGale },
                         { kCavern, kCourt });
    s.players[0].hand[2].m_is_staged = true;
    const std::vector<int> o = Sv(s);
    REQUIRE(o.size() == 8);
    CHECK(o.back() == 2);
    CheckPermutation(s, o);
}
