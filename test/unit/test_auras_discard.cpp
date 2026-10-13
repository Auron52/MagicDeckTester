// Unit tests for the Auras cleanup-discard buckets: AurasProvider::CleanupDiscardCandidates, the USER's
// settled design (2026-10-09, docs/design/auras-discard-policy-proposal.md "#### Auras: SETTLED"):
//
//   KEEP: 3 lands (net of board, W+G covered) -> 1 creature (Light-Paws > Kor > Bogle/Scout) ->
//         Light-Paws if none in play -> every useful Aura (best live power per mana first) ->
//         if room: the 4th land, Kor, a spare plain creature.
//   SHED: dup Light-Paws -> blank 0-power Aura / dead Ancestral Mask -> lands beyond the 4th -> spare
//         plain creatures -> Coronet / Lion Umbra with no creature in play or hand -> spare Kor ->
//         the 4th land -> useful Auras (worst rate first) -> protected.
//         Lands: tapped Razorverge -> Plains/Forest -> Branchloft -> Brushland -> untapped Razorverge
//         -> Horizon Canopy last.
//
// Every expected order below was derived BY HAND from that text (and the agent readings listed in the
// definition's comment), not read off the code. Each board also asserts that every non-staged index is
// named exactly once, and most assert the GENERIC fallback's index 0 -- the control arm that must
// differ. The =0 hatch must return exactly the generic ranking, and the rule must be PREFIX-STABLE
// (CleanupDiscardShedStable's default): shedding its own first pick and re-ranking leaves the rest in
// the same order.
//
// Every board runs BOTH arms of MTG_DISCARD_DIST_MODEL (the shared distance-to-castable model, USER
// 2026-10-10): =0 must give the 2026-10-09 order exactly; the model (the default) may reorder only
// within the USER's tiers. Boards 21-23 are where it does, with the factors derived by hand.
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

void EnsureCardsAud()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); hands are built
// that way so a policy reading a hand card's own masks would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// A battlefield entry: `on` = the index (in this board list) of the creature an Aura is attached to.
struct Bf
{
    std::string name;
    int on = -1;
};

const char* kPaws = "Light-Paws, Emperor's Voice";

const std::vector<std::string> kLib6 = { "Rancor", "Ethereal Armor", "Plains", "Slippery Bogle", "Forest",
                                         "Hyena Umbra" };
// The distance-model boards' library: 10 cards, 2 of them Plains (the only lands, the only W sources).
const std::vector<std::string> kLib10 = { "Plains", "Plains", "Rancor", "Rancor", "Rancor", "Rancor",
                                          "Hyena Umbra", "Hyena Umbra", "Hyena Umbra", "Hyena Umbra" };

GameState MakeAu(const std::vector<std::string>& hand, const std::vector<Bf>& board,
                 const std::vector<std::string>& library = kLib6)
{
    EnsureCardsAud();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(Ph(n, num++)); }
    for (const Bf& b : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(b.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (b.on >= 0) { p.aura_attached_to = s.battlefield[static_cast<std::size_t>(b.on)].card.m_number; }
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;   // a cleanup: the turn's land drop is spent
    return s;
}

// arm: MTG_AURA_BUCKET_DISCARD; dist: MTG_DISCARD_DIST_MODEL (-1 = unset, i.e. the default).
std::vector<int> AuArm(const GameState& s, int arm, int dist = -1)
{
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = arm;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]  = dist;
    std::vector<int> v = AurasProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]  = -1;
    return v;
}
std::vector<int> Au(const GameState& s, int dist = -1) { return AuArm(s, 1, dist); }
std::vector<int> AuOld(const GameState& s)             { return AuArm(s, 1, 0); }   // the =0 proxies
std::vector<int> AuNew(const GameState& s)             { return AuArm(s, 1, 1); }   // the model
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones fall to tier C at the back).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

// PREFIX STABILITY: shed the rule's own first pick, re-rank the smaller hand, and the remaining cards
// must come back in the same order -- all the way down to an empty hand.
void CheckPrefixStable(GameState s, int dist)
{
    std::vector<int> order = Au(s, dist);
    while (!s.players[0].hand.empty())
    {
        std::vector<int> rest_numbers;
        for (std::size_t k = 1; k < order.size(); ++k)
        { rest_numbers.push_back(s.players[0].hand[static_cast<std::size_t>(order[k])].m_number); }
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Au(s, dist);
        std::vector<int> got_numbers;
        for (int i : order) { got_numbers.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_number); }
        CHECK(got_numbers == rest_numbers);
    }
}

// `want_old`: the order with MTG_DISCARD_DIST_MODEL=0 (the 2026-10-09 rate / power proxies -- the hatch
// must reproduce it exactly). `want_new`: with the distance model (the default); omitted = the same
// order, i.e. a board on which every modelled card is castable next turn and the model changes nothing.
void CheckBoard(const GameState& s, const std::vector<int>& want_old, std::vector<int> want_new = {})
{
    if (want_new.empty()) { want_new = want_old; }
    const std::vector<int> old_got = AuOld(s);
    const std::vector<int> new_got = AuNew(s);
    CHECK(old_got == want_old);
    CHECK(new_got == want_new);
    CHECK(Au(s) == new_got);   // the model is the default
    CheckPermutation(s, old_got);
    CheckPermutation(s, new_got);
    CheckPrefixStable(s, 0);
    CheckPrefixStable(s, 1);
}

}   // namespace

// ---- useful Auras: live power per mana, with the scaler credit -----------------------------------

TEST_CASE("Auras discard 1: surplus lands before a spare body; useful Auras worst rate first; Light-Paws protected")
{
    // Board: 4 lands, a Bogle wearing Ethereal Armor (scale credit +1). Rates: Spirit Link 1/1, Mask
    // (2x1 + 1)/3 = 1, Coronet (3 + 1)/2 = 2, Rancor (2 + 1)/1 = 3; Link and Mask tie -> the non-scaler
    // sheds first. Light-Paws (none in play) is protected though the Bogle fills the creature slot.
    const GameState s = MakeAu(
        { "Plains", "Forest", "Ancestral Mask", "Daybreak Coronet", "Rancor", kPaws, "Slippery Bogle", "Spirit Link" },
        { { "Brushland" }, { "Brushland" }, { "Forest" }, { "Plains" }, { "Slippery Bogle" }, { "Ethereal Armor", 4 } });
    CheckBoard(s, { 0, 1, 6, 7, 2, 3, 4, 5 });
    CHECK(Gen(s).front() == 2);   // generic max-MV sheds the Mask
}

TEST_CASE("Auras discard 2: a second Light-Paws is dead; tapped Razorverge beyond the 4th; Light-Paws in play: no blanks")
{
    // Board: 3 lands, Light-Paws wearing Ethereal Armor + Daybreak Coronet (2 enchantments, credit +1).
    // Soft 4th land = Forest (Razorverge would enter tapped). Rates: Link 1, Mask (2x2 + 1)/3, Hyena 2,
    // Rancor 3.
    const GameState s = MakeAu(
        { "Rancor", kPaws, "Forest", "Ancestral Mask", "Slippery Bogle", "Spirit Link", "Hyena Umbra", "Razorverge Thicket" },
        { { "Brushland" }, { "Horizon Canopy" }, { "Plains" }, { kPaws }, { "Ethereal Armor", 3 }, { "Daybreak Coronet", 3 } });
    CheckBoard(s, { 1, 7, 4, 2, 5, 3, 6, 0 });
    CHECK(Gen(s).front() == 3);
}

TEST_CASE("Auras discard 3: two copies of Light-Paws in hand -- the second is dead, the first fills the creature slot")
{
    const GameState s = MakeAu(
        { kPaws, kPaws, "Rancor", "Hyena Umbra", "Plains", "Forest", "Spirit Mantle", "Audacity" },
        { { "Plains" }, { "Forest" }, { "Brushland" } });
    // dup 1; beyond Forest; 4th land Plains; Mantle 0.5 < Hyena 1 < Rancor 2 = Audacity 2; slot Light-Paws
    CheckBoard(s, { 1, 5, 4, 6, 3, 2, 7, 0 });
    CHECK(Gen(s).front() == 0);   // generic sheds the live Light-Paws
}

// ---- blank 0-power Auras / dead Mask ------------------------------------------------------------

TEST_CASE("Auras discard 4: 0-power Auras with no scaler in play or hand are BLANK -- shed first, pricier first")
{
    // Board: 2 lands, a bare Scout. Branchloft is the 3rd land (hard), Plains the 4th (soft).
    const GameState s = MakeAu(
        { "Alpha Authority", "Spirit Link", "Rancor", "Hyena Umbra", "Daybreak Coronet", "Branchloft Pathway",
          "Plains", "Slippery Bogle" },
        { { "Forest" }, { "Plains" }, { "Gladecover Scout" } });
    CheckBoard(s, { 0, 1, 7, 6, 3, 4, 2, 5 });
}

TEST_CASE("Auras discard 5: a scaler KEPT in hand makes the 0-power Auras useful (rate 0, cheaper kept)")
{
    const GameState s = MakeAu(
        { "Alpha Authority", "Spirit Link", "Ethereal Armor", "Rancor", "Daybreak Coronet", "Branchloft Pathway",
          "Plains", "Slippery Bogle" },
        { { "Forest" }, { "Plains" }, { "Gladecover Scout" } });
    // spare Bogle, 4th land Plains, then Alpha 0 / Link 0 (no Light-Paws: the pricier sheds first),
    // Armor 1, Coronet 1.5, Rancor 2; Branchloft is the hard 3rd land
    CheckBoard(s, { 7, 6, 0, 1, 2, 4, 3, 5 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("Auras discard 6: Light-Paws in play -- no Aura is a blank, and of two equal Auras the PRICIER is kept")
{
    // Board: 3 lands, Light-Paws wearing Rancor (1 enchantment, no scaler). Rates: Link 0/1, Alpha 0/2,
    // Mask 2/3, Hyena 1, Gryff's Boon 1. Canopy is the soft 4th land, Forest beyond.
    const GameState s = MakeAu(
        { "Spirit Link", "Hyena Umbra", "Ancestral Mask", "Alpha Authority", "Gryff's Boon", "Horizon Canopy",
          "Forest", "Slippery Bogle" },
        { { "Brushland" }, { "Razorverge Thicket" }, { "Plains" }, { kPaws }, { "Rancor", 3 } });
    CheckBoard(s, { 6, 7, 5, 0, 3, 2, 1, 4 });
    CHECK(Gen(s).front() == 2);
}

TEST_CASE("Auras discard 7: the same hand without Light-Paws in play -- the cheaper of two equal Auras is kept")
{
    const GameState s = MakeAu(
        { "Spirit Link", "Hyena Umbra", "Ancestral Mask", "Alpha Authority", "Gryff's Boon", "Horizon Canopy",
          "Forest", "Slippery Bogle" },
        { { "Brushland" }, { "Razorverge Thicket" }, { "Plains" }, { "Slippery Bogle" }, { "Rancor", 3 } });
    CheckBoard(s, { 6, 7, 5, 3, 0, 2, 1, 4 });
}

TEST_CASE("Auras discard 8: Ancestral Mask with no enchantment around is DEAD; lands beyond the 4th in the USER's order")
{
    // Board: 3 lands, a bare Bogle, no enchantments. Lands beyond the 4th: tapped Razorverge ->
    // Branchloft -> Brushland; Canopy is the 4th. Then the spare Scout, the spare Kor, the 4th land;
    // Light-Paws protected last.
    const GameState s = MakeAu(
        { "Ancestral Mask", "Gladecover Scout", "Kor Spiritdancer", "Horizon Canopy", "Razorverge Thicket",
          "Branchloft Pathway", "Brushland", kPaws },
        { { "Plains" }, { "Forest" }, { "Brushland" }, { "Slippery Bogle" } });
    CheckBoard(s, { 0, 4, 5, 6, 1, 2, 3, 7 });
    CHECK(Gen(s) == std::vector<int>{ 0, 2, 7, 1, 3, 4, 5, 6 });   // same index 0, different order
}

TEST_CASE("Auras discard 9: the SOLE enabler of a waiting Coronet is kept (the cheapest 0-power Aura); the other is blank")
{
    const GameState s = MakeAu(
        { "Spirit Link", "Alpha Authority", "Daybreak Coronet", "Forest", "Plains", "Brushland", "Razorverge Thicket",
          "Gladecover Scout" },
        { { "Plains" }, { "Forest" }, { "Brushland" }, { "Horizon Canopy" }, { "Slippery Bogle" } });
    // blank Alpha; 4 lands in play: every hand land is beyond the 4th (tapped Razorverge first);
    // spare Scout; useful Link 0, Coronet 1.5
    CheckBoard(s, { 1, 6, 3, 4, 5, 7, 0, 2 });
}

TEST_CASE("Auras discard 10: a dressed host needs no enabler -- both 0-power Auras are blank")
{
    const GameState s = MakeAu(
        { "Spirit Link", "Alpha Authority", "Daybreak Coronet", "Forest", "Plains", "Brushland", "Razorverge Thicket",
          "Gladecover Scout" },
        { { "Plains" }, { "Forest" }, { "Brushland" }, { "Horizon Canopy" }, { "Slippery Bogle" }, { "Rancor", 4 } });
    CheckBoard(s, { 1, 0, 6, 3, 4, 5, 7, 2 });
}

TEST_CASE("Auras discard 11: Kor in play -- no blanks, and a hand Light-Paws stays protected (agent reading (a))")
{
    const GameState s = MakeAu(
        { kPaws, "Spirit Link", "Alpha Authority", "Hyena Umbra", "Gladecover Scout", "Forest", "Plains", "Brushland" },
        { { "Plains" }, { "Forest" }, { "Brushland" }, { "Kor Spiritdancer" }, { "Rancor", 3 } });
    // beyond Forest, Plains; spare Scout; 4th land Brushland; Alpha 0/2, Link 0/1, Hyena 1; Light-Paws
    CheckBoard(s, { 5, 6, 4, 7, 2, 1, 3, 0 });
    CHECK(Gen(s).front() == 0);   // generic sheds Light-Paws
}

// ---- stranded restricted Auras ------------------------------------------------------------------

TEST_CASE("Auras discard 12: Coronet / Lion Umbra with no creature in play or hand are stranded")
{
    const GameState s = MakeAu(
        { "Daybreak Coronet", "Lion Umbra", "Rancor", "Ethereal Armor", "Plains", "Forest", "Brushland", "Spirit Mantle" },
        { { "Horizon Canopy" }, { "Plains" }, { "Forest" } });
    // beyond Plains, Forest; stranded Coronet, Lion Umbra; 4th land Brushland; Mantle 0.5, Armor 1, Rancor 2
    CheckBoard(s, { 4, 5, 0, 1, 6, 7, 3, 2 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("Auras discard 13: a creature in hand un-strands them and fills the creature slot")
{
    const GameState s = MakeAu(
        { "Daybreak Coronet", "Lion Umbra", "Rancor", "Ethereal Armor", "Plains", "Forest", "Brushland", "Slippery Bogle" },
        { { "Horizon Canopy" }, { "Plains" }, { "Forest" } });
    CheckBoard(s, { 4, 5, 6, 3, 0, 1, 2, 7 });
}

// ---- lands: 3 hard (W+G covered), the 4th soft, then beyond in the USER's order ---------------------

TEST_CASE("Auras discard 14: one land in play -- the G cover and 2 hard lands, the 4th soft, the rest beyond")
{
    const GameState s = MakeAu(
        { "Forest", "Plains", "Razorverge Thicket", "Branchloft Pathway", "Brushland", "Horizon Canopy", "Rancor",
          "Slippery Bogle", "Ethereal Armor" },
        { { "Plains" } });
    // keep Canopy (cover) + untapped Razorverge (hard), Brushland (4th); beyond Forest, Plains, Branchloft;
    // the Bogle is the creature slot
    CheckBoard(s, { 0, 1, 3, 4, 8, 6, 7, 2, 5 });
    CHECK(Gen(s).front() == 6);
}

TEST_CASE("Auras discard 15: three Plains in play -- the only G source is a HARD keep, above every Aura")
{
    const GameState s = MakeAu(
        { "Plains", "Forest", "Rancor", "Hyena Umbra", "Audacity", "Gryff's Boon", "Spirit Mantle", "Armadillo Cloak" },
        { { "Plains" }, { "Plains" }, { "Plains" }, { "Slippery Bogle" } });
    CheckBoard(s, { 0, 6, 7, 3, 5, 2, 4, 1 });
    CHECK(Gen(s).front() == 7);
}

TEST_CASE("Auras discard 16: lands shed tapped Razorverge -> Forest -> Branchloft -> Brushland; Canopy is the 4th")
{
    const GameState s = MakeAu(
        { "Razorverge Thicket", "Branchloft Pathway", "Brushland", "Horizon Canopy", "Forest", "Rancor", "Audacity",
          "Spirit Mantle" },
        { { "Plains" }, { "Plains" }, { "Forest" }, { "Gladecover Scout" } });
    CheckBoard(s, { 0, 4, 1, 2, 3, 7, 5, 6 });
    CHECK(Gen(s).front() == 7);
}

TEST_CASE("Auras discard 17: an untapped Razorverge outranks Brushland; Horizon Canopy is kept last")
{
    const GameState s = MakeAu(
        { "Razorverge Thicket", "Brushland", "Horizon Canopy", "Plains", "Branchloft Pathway", "Rancor", "Hyena Umbra",
          "Ethereal Armor", "Audacity" },
        { { "Plains" }, { "Forest" }, { "Slippery Bogle" } });
    // hard Canopy, soft Razorverge; beyond Plains -> Branchloft -> Brushland; Hyena = Armor at 1 (the
    // non-scaler first), Rancor = Audacity at 2
    CheckBoard(s, { 3, 4, 1, 0, 6, 7, 5, 8, 2 });
    CHECK(Gen(s).front() == 5);
}

// ---- creatures: the slot, Light-Paws, the leftovers -------------------------------------------------

TEST_CASE("Auras discard 18: no creature in play -- Light-Paws fills the slot; spare bodies, then Kor, then the 4th land")
{
    const GameState s = MakeAu(
        { "Kor Spiritdancer", "Slippery Bogle", kPaws, "Gladecover Scout", "Rancor", "Spirit Mantle", "Armadillo Cloak",
          "Plains" },
        { { "Brushland" }, { "Horizon Canopy" }, { "Razorverge Thicket" } });
    CheckBoard(s, { 1, 3, 0, 7, 5, 6, 4, 2 });
    CHECK(Gen(s).front() == 6);
}

TEST_CASE("Auras discard 19: a creature in play -- Light-Paws stays protected, Kor is the spare engine")
{
    const GameState s = MakeAu(
        { kPaws, "Kor Spiritdancer", "Rancor", "Ethereal Armor", "Hyena Umbra", "Gladecover Scout", "Forest", "Plains" },
        { { "Plains" }, { "Forest" }, { "Brushland" }, { "Horizon Canopy" }, { "Slippery Bogle" } });
    CheckBoard(s, { 6, 7, 5, 1, 4, 3, 2, 0 });
    CHECK(Gen(s).front() == 0);   // generic sheds Light-Paws
}

TEST_CASE("Auras discard 20: a card the design has no class for (a land Aura) sheds right after the blanks")
{
    const GameState s = MakeAu(
        { "Alpha Authority", "Spirit Link", "Rancor", "Wild Growth", "Daybreak Coronet", "Branchloft Pathway",
          "Plains", "Slippery Bogle" },
        { { "Forest" }, { "Plains" }, { "Gladecover Scout" } });
    CheckBoard(s, { 0, 1, 3, 7, 6, 4, 2, 5 });
}

// ---- distance to castable: the shared discard_dist model (MTG_DISCARD_DIST_MODEL, USER 2026-10-10) ----
// Library kLib10: 10 cards, 2 Plains (the only lands, the only W sources). Hand-derived factors, with
// P_t = P(castable by our t-th next turn) and factor = sum (P_t - P_t-1) 0.75^(t-1) + (1 - P_4) 0.02 0.75^4:
//   one W pip, no W source in play or kept: P_t = 1 - C(8,t)/C(10,t) = 0.2, 17/45, 8/15, 2/3
//     -> 0.2 + 0.13333 + 0.0875 + 0.05625 + 0.00211 = 0.47919
//   {W}{W}, no W source:      P_t = 0, 1/45, 1/15, 2/15  -> 0.01667 + 0.025 + 0.02813 + 0.00548 = 0.07528
//   {W}{W}, one kept Plains:  P_t = 0, 17/45, 8/15, 2/3  -> 0.28333 + 0.0875 + 0.05625 + 0.00211 = 0.42919
//   a {G} card with Forests in play, or a {W} one with the kept Plains: P_1 = 1 -> 1.

TEST_CASE("Auras discard 21 (distance): no white source anywhere -- Daybreak Coronet's {W}{W} is the first useful Aura to go")
{
    // Board: Forest, Forest, Branchloft (G only), a Bogle wearing Rancor. The hand Forest is the soft 4th
    // land, so no land is kept hard. EV = rate x factor: Coronet 1.5 x 0.0753 = 0.113 < Mantle 0.5 x 0.479
    // = 0.240 < Cloak (2/3) x 0.479 = 0.319 < Hyena 1 x 0.479 = 0.479 < Spider 1 < Audacity 2.
    // Without the model (rate only): Mantle 0.5, Cloak 0.67, Spider 1 = Hyena 1 (hand order), Coronet
    // 1.5, Audacity 2.
    const GameState s = MakeAu(
        { "Daybreak Coronet", "Spider Umbra", "Audacity", "Gladecover Scout", "Forest", "Hyena Umbra",
          "Armadillo Cloak", "Spirit Mantle" },
        { { "Forest" }, { "Forest" }, { "Branchloft Pathway" }, { "Slippery Bogle" }, { "Rancor", 3 } }, kLib10);
    CheckBoard(s, { 3, 4, 7, 6, 1, 5, 0, 2 },
                  { 3, 4, 0, 7, 6, 5, 1, 2 });
    CHECK(Gen(s).front() == 6);
}

TEST_CASE("Auras discard 22 (distance): a KEPT Plains brings Coronet within reach -- above Mantle, still below Spider and Hyena")
{
    // Board: Forest, Forest, a Bogle wearing Rancor. The hand Plains covers W (a hard keep); the Forest
    // is the soft 4th. Coronet 1.5 x 0.429 = 0.644; Mantle 0.5 x 1; Spider 1; Hyena 1 x 1 (the Plains
    // pays it next turn); Audacity 2.
    const GameState s = MakeAu(
        { "Daybreak Coronet", "Spirit Mantle", "Plains", "Spider Umbra", "Hyena Umbra", "Gladecover Scout",
          "Forest", "Audacity" },
        { { "Forest" }, { "Forest" }, { "Slippery Bogle" }, { "Rancor", 2 } }, kLib10);
    CheckBoard(s, { 5, 6, 1, 3, 4, 0, 7, 2 },
                  { 5, 6, 1, 0, 3, 4, 7, 2 });
}

TEST_CASE("Auras discard 23 (distance): the creature slot weighs power x castability -- a castable 1/1 over a {W} 2/1 with no white")
{
    // Board: Forest, Forest, Branchloft (G only), no creature. Plain tier: Bogle 1 x 1 = 1 > Dauntless
    // Bodyguard 2 x 0.479 = 0.958 -> the Bogle is the creature slot, the Bodyguard the spare body. (Without
    // the model: higher power -> the Bodyguard.) Auras: Hyena 0.479 = Armor 0.479 (non-scaler first) <
    // Spider 1 < Rancor 2 = Audacity 2.
    const GameState s = MakeAu(
        { "Slippery Bogle", "Dauntless Bodyguard", "Rancor", "Spider Umbra", "Audacity", "Forest", "Hyena Umbra",
          "Ethereal Armor" },
        { { "Forest" }, { "Forest" }, { "Branchloft Pathway" } }, kLib10);
    CheckBoard(s, { 0, 5, 3, 6, 7, 2, 4, 1 },
                  { 1, 5, 6, 7, 3, 2, 4, 0 });
}

// ---- PROPOSED AMENDMENT MTG_AURA_RATE_MARGINAL (default OFF; the USER decides) ---------------------------
// A useful Aura's rate numerator becomes its MARGINAL power in the kept set K (the useful Auras still in
// hand + the board), symmetric between scalers and the Auras they count:
//   M(a) = marg(a) + pw(a) x #(K \ a) [a scaler] + sum pw(s) over the scalers s in K \ a
// (Ethereal Armor pw 1, Ancestral Mask pw 2; marg = the USER's live power + board scaler credit), each term
// weighted by the discard_dist factor of the Auras in it under the model; the order is iterated removal of
// the worst rate. The lever unset (every board above) is the USER design; =0 must be identical.

namespace
{
// marg: MTG_AURA_RATE_MARGINAL (-1 = unset = OFF); dist: MTG_DISCARD_DIST_MODEL.
std::vector<int> AuMarg(const GameState& s, int marg, int dist)
{
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = static_cast<std::int8_t>(marg);
    std::vector<int> v = Au(s, dist);
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = -1;
    return v;
}

// Both model arms: the lever's =0 equals unset (the USER design, `user_old` / `user_new`), and =1 gives
// `marg_old` / `marg_new`. Each lever-on order is a permutation, and the iterated removal really is the
// per-shed sequence on these boards (shed its own first pick, re-rank: the rest come back in order).
void CheckMarginal(const GameState& s, const std::vector<int>& user_old, const std::vector<int>& user_new,
                   const std::vector<int>& marg_old, const std::vector<int>& marg_new)
{
    CHECK(AuOld(s) == user_old);
    CHECK(AuNew(s) == user_new);
    CHECK(AuMarg(s, 0, 0) == user_old);
    CHECK(AuMarg(s, 0, 1) == user_new);
    CHECK(AuMarg(s, 1, 0) == marg_old);
    CHECK(AuMarg(s, 1, 1) == marg_new);
    CHECK(AuMarg(s, 1, -1) == marg_new);   // the model is the default under the lever too
    CheckPermutation(s, marg_old);
    CheckPermutation(s, marg_new);
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = 1;
    CheckPrefixStable(s, 0);
    CheckPrefixStable(s, 1);
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = -1;
}
}   // namespace

TEST_CASE("Auras discard: proposed MTG_AURA_RATE_MARGINAL -- the gi1634 board: Armor and Mask rate above Alpha Authority")
{
    // The T4 cleanup of the held-out A/B game Auras d0 s6051635 gi 1634: Branchloft Pathway (G face) +
    // Forest, no creature, no enchantment. Light-Paws (0) is the creature slot; a scaler is in hand, so no
    // Aura is blank, and the Mask has Auras around, so it is not dead: all seven Auras are useful.
    //
    // USER design (both model arms): rates Rancor 2/1, Alpha 0/2, Armor 1/1, Mask 0/3, Link 0/1, Cloak 2/3,
    // Boon 1/1 -> zeros Alpha, Link (non-scalers, pricier first), Mask; Cloak; Boon = Armor (non-scaler
    // first); Rancor; Light-Paws. -> 2 5 4 6 7 3 1 0. (Model: Cloak 0.25, Boon = Armor 0.46: same order.)
    //
    // Marginal, MTG_DISCARD_DIST_MODEL=0 (every factor 1). K = all 7; the scalers' sum pw = 3.
    //   P(K) = Rancor 2 + Cloak 2 + Boon 1 + Armor 7 (it counts all seven) + Mask 12 (6 others x 2) = 24.
    //   M: Rancor 2+3 = 5, Alpha 0+3 = 3, Armor 1+6+2 = 9, Mask 0+12+1 = 13, Link 3, Cloak 2+3 = 5, Boon 4.
    //   (Check: P(K \ Alpha) = 5 + 6 + 10 = 21 -> 3; P(K \ Armor) = 5 + 10 = 15 -> 9; P(K \ Mask) = 5 + 6
    //   = 11 -> 13.)  Rates: Rancor 5, Alpha 1.5, Armor 9, Mask 4.33, Link 3, Cloak 1.67, Boon 4.
    //   1. Alpha (1.5).  K = 6: Rancor 5, Armor 8, Mask 11/3, Link 3, Cloak 5/3, Boon 4.
    //   2. Cloak (1.67). K = 5: Rancor 5, Armor 7, Mask 9/3 = 3, Link 3, Boon 4.
    //   3. Link (3 = Mask's 3: the non-scaler first).  K = 4: Rancor 5, Armor 6, Mask 7/3, Boon 4.
    //   4. Mask (2.33).  K = {Rancor, Armor, Boon}: Rancor 2+1 = 3, Armor 1+2 = 3, Boon 1+1 = 2.
    //   5. Boon (2).     K = {Rancor, Armor}: Rancor 3, Armor 1+1 = 2.   6. Armor.   7. Rancor.
    //   -> 2 6 5 4 7 3 1 0. Alpha still goes first -- 3 power for 2 mana is the worst return here -- but
    //   now on its own merits: the Armor is worth 9 and the Mask 13, not 1 and 0.
    //
    // Marginal, with the model (the default). Library kLib6 (6 cards: one Plains, one Forest); no hand land.
    //   Factors: Rancor {G} 1, Alpha {1}{G} 1; Armor / Link / Boon {W}: P_t = t/6 -> 0.4578; Mask {2}{G}
    //   (one more land: P_t = 1/3, 3/5, 4/5, 14/15) -> 0.7025; Cloak {1}{G}{W} (P_t = 1/18, 1/5, 2/5, 28/45)
    //   -> 0.3725. E[M(a)] = fac(a) x (marg(a) + pw(a) x sum fac(K \ a) + sum fac(s) pw(s) over K \ a).
    //   K = 7: sum fac 4.4486, sum fac x pw 1.8628 (Armor 0.4578 + Mask 1.4050). EV = E[M] / mv:
    //     Rancor 3.86, Alpha 1.86/2 = 0.93, Armor 0.4578 x (1 + 3.9907 + 1.4050) = 2.93,
    //     Mask 0.7025 x (2 x 3.7460 + 0.4578)/3 = 1.86, Link 0.4578 x 1.8628 = 0.85,
    //     Cloak 0.3725 x 3.8628/3 = 0.48, Boon 0.4578 x 2.8628 = 1.31.        1. Cloak (0.48).
    //   K = 6 (sum fac 4.0760): Alpha 0.93, Link 0.85, Boon 1.31, Mask 1.69, Armor 2.76.  2. Link (0.85).
    //   K = 5 (3.6182): Alpha 0.93, Boon 1.31, Mask 1.47, Armor 2.55.                      3. Alpha (0.93).
    //   K = 4 (2.6182): Boon 1.31, Mask 0.7025 x (2 x 1.9157 + 0.4578)/3 = 1.00, Armor 2.09. 4. Mask.
    //   K = {Rancor, Armor, Boon}: Rancor 2.46, Armor 0.4578 x 2.4578 = 1.13, Boon 0.4578 x 1.4578 = 0.67.
    //   5. Boon.  6. Armor (0.92 < Rancor 2.46).  7. Rancor.  -> 6 5 2 4 7 3 1 0.
    GameState s = MakeAu(
        { kPaws, "Rancor", "Alpha Authority", "Ethereal Armor", "Ancestral Mask", "Spirit Link", "Armadillo Cloak",
          "Gryff's Boon" },
        { { "Branchloft Pathway" }, { "Forest" } });
    s.turn_number = 4;
    CheckMarginal(s, { 2, 5, 4, 6, 7, 3, 1, 0 }, { 2, 5, 4, 6, 7, 3, 1, 0 },
                     { 2, 6, 5, 4, 7, 3, 1, 0 }, { 6, 5, 2, 4, 7, 3, 1, 0 });
    // Armor and Mask are rated above Alpha Authority on both arms (they shed after it).
    for (int dist : { 0, 1 })
    {
        const std::vector<int> o = AuMarg(s, 1, dist);
        auto pos = [&](int i) { return std::find(o.begin(), o.end(), i) - o.begin(); };
        CHECK(pos(3) > pos(2));
        CHECK(pos(4) > pos(2));
    }

    // Shed stability is declared false only while the lever is on (and the buckets are).
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = 1;
    CHECK_FALSE(AurasProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = 0;
    CHECK(AurasProvider().CleanupDiscardShedStable());   // the generic arm: the default
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = 0;
    CHECK(AurasProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::AURA_RATE_MARGINAL] = -1;
    CHECK(AurasProvider().CleanupDiscardShedStable());   // unset = OFF
}

TEST_CASE("Auras discard: proposed MTG_AURA_RATE_MARGINAL -- an uncastable Daybreak Coronet does not out-rate castable Auras")
{
    // Board: Forest, Forest, a bare Bogle (so the Coronet is not stranded). Library kLib10 (2 Plains, the
    // only W sources). Factors (board 21's): Coronet {W}{W} 0.0753, Mask {2}{G} (one more land, 2 in 10)
    // 0.4792, Alpha / Rancor / Spider Umbra 1. No enchantment out: the Mask's live power is 0, but a
    // scaler is in hand, so nothing is blank and the Mask (Auras around) is not dead.
    //
    // USER design. Model: EV Coronet 3/2 x 0.0753 = 0.113, Mask 0, Alpha 0, Rancor 2, Spider 1 -> Alpha,
    // Mask (the zeros, the non-scaler first), Coronet, Spider, Rancor -> 2 1 0 4 3. =0 proxies: Coronet
    // 1.5 > Spider 1 -> 2 1 4 0 3.
    //
    // Marginal, with the model (the default): K = 5, sum fac 3.5545, the Mask's weighted credit 2 x 0.4792
    // = 0.9584 to every other Aura.
    //   Coronet 0.0753 x (3 + 0.9584)/2 = 0.149 -- the Mask's +2 reaches it at its own 0.0753;
    //   Mask 0.4792 x 2 x (3.5545 - 0.4792)/3 = 0.98 -- the Coronet adds 0.0753 of a unit to its count,
    //   not 1; Alpha 0.9584/2 = 0.48; Spider 1.96; Rancor 2.96.                   1. Coronet (0.149).
    //   K = {Mask, Alpha, Rancor, Spider}: Mask 0.4792 x 2 x 3/3 = 0.96, Alpha 0.48.  2. Alpha.
    //   K = {Mask, Rancor, Spider}: Mask 0.4792 x 4/3 = 0.64.                        3. Mask.
    //   4. Spider (1 < Rancor 2).  5. Rancor.  -> 0 2 1 4 3.
    // Marginal, =0 proxies (no castability on that arm, exactly as the USER rate there has none): every
    //   factor is 1, so the Coronet is credited in full: Coronet (3+2)/2 = 2.5, Mask 2 x 4/3 = 2.67,
    //   Alpha 2/2 = 1, Spider 3, Rancor 4 -> Alpha; then Mask 6/3 = 2 < Coronet 2.5 -> Mask; no scaler
    //   left: Spider 1 < Coronet 1.5 < Rancor 2 -> 2 1 4 0 3 (the USER =0 order).
    const GameState s = MakeAu(
        { "Daybreak Coronet", "Ancestral Mask", "Alpha Authority", "Rancor", "Spider Umbra" },
        { { "Forest" }, { "Forest" }, { "Slippery Bogle" } }, kLib10);
    CheckMarginal(s, { 2, 1, 4, 0, 3 }, { 2, 1, 0, 4, 3 },
                     { 2, 1, 4, 0, 3 }, { 0, 2, 1, 4, 3 });
    CHECK(AuMarg(s, 1, -1).front() == 0);   // the default model: the Coronet goes first
}

TEST_CASE("Auras discard: proposed MTG_AURA_RATE_MARGINAL -- two Ethereal Armors: the second is not shed before Gryff's Boon")
{
    // Board: Plains, Forest, a bare Bogle; every Aura castable next turn (all factors 1, so both model
    // arms agree). Hand: Armor, Armor, Gryff's Boon, Rancor.
    //   USER design: Armor 1/1 = Armor 1/1 = Boon 1/1 (the non-scaler first), Rancor 2 -> 2 0 1 3.
    //   Marginal: K = 4, the scalers' sum pw 2. P(K) = Armor 4 + Armor 4 + Boon 1 + Rancor 2 = 11.
    //     Armor 1 + 3 (the three others) + 1 (the other Armor counts it) = 5 (P(K \ Armor) = 3 + 1 + 2 = 6);
    //     Boon 1 + 2 = 3; Rancor 2 + 2 = 4.                                     1. Boon (3).
    //     K = {Armor, Armor, Rancor}: Armor 1 + 2 + 1 = 4 = Rancor 2 + 2 = 4 (the non-scaler first).
    //                                                                           2. Rancor.
    //     K = {Armor, Armor}: 1 + 1 + 1 = 3 each (hand order).                  3. Armor 0.  4. Armor 1.
    //   -> 2 3 0 1. The rejected one-sided credit (c013e52d: every Aura + the hand scalers' 2, a scaler
    //   minus its own 1) read Armor 2, Boon 3, Rancor 4 and shed BOTH Armors before the Boon: the
    //   self-credit asymmetry this lever removes.
    const GameState s = MakeAu(
        { "Ethereal Armor", "Ethereal Armor", "Gryff's Boon", "Rancor" },
        { { "Plains" }, { "Forest" }, { "Slippery Bogle" } });
    CheckMarginal(s, { 2, 0, 1, 3 }, { 2, 0, 1, 3 },
                     { 2, 3, 0, 1 }, { 2, 3, 0, 1 });
}

// ---- the contract ------------------------------------------------------------------------------------

TEST_CASE("Auras discard: a staged card is never in the preference; it falls to the back")
{
    GameState s = MakeAu(
        { "Plains", "Forest", "Ancestral Mask", "Daybreak Coronet", "Rancor", kPaws, "Slippery Bogle", "Spirit Link" },
        { { "Brushland" }, { "Brushland" }, { "Forest" }, { "Plains" }, { "Slippery Bogle" }, { "Ethereal Armor", 4 } });
    s.players[0].hand[5].m_is_staged = true;   // the Light-Paws
    const std::vector<int> o = Au(s);
    CHECK(o == std::vector<int>{ 0, 1, 6, 7, 2, 3, 4, 5 });   // board 1's order, Light-Paws now via tier C
    CheckPermutation(s, o);
}

TEST_CASE("Auras discard: MTG_AURA_BUCKET_DISCARD=0 returns exactly the generic ranking; the default is ON")
{
    const GameState s = MakeAu(
        { "Plains", "Forest", "Ancestral Mask", "Daybreak Coronet", "Rancor", kPaws, "Slippery Bogle", "Spirit Link" },
        { { "Brushland" }, { "Brushland" }, { "Forest" }, { "Plains" }, { "Slippery Bogle" }, { "Ethereal Armor", 4 } });
    CHECK(AuArm(s, 0) == Gen(s));
    CHECK(AuArm(s, 0) != Au(s));                                  // the control arm differs
    CHECK(AurasProvider().CleanupDiscardCandidates(s, nullptr) == Au(s));   // unset arm = default ON
    // An empty hand defers to the generic rule.
    GameState e = s;
    e.players[0].hand.clear();
    CHECK(Au(e).empty());
}
