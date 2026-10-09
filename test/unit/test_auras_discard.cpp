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

GameState MakeAu(const std::vector<std::string>& hand, const std::vector<Bf>& board)
{
    EnsureCardsAud();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    const char* lib[] = { "Rancor", "Ethereal Armor", "Plains", "Slippery Bogle", "Forest", "Hyena Umbra" };
    for (const char* n : lib) { s.players[0].library.push_back(Ph(n, num++)); }
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

std::vector<int> AuArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = arm;
    std::vector<int> v = AurasProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::AURA_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Au(const GameState& s)  { return AuArm(s, 1); }
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
void CheckPrefixStable(GameState s)
{
    std::vector<int> order = Au(s);
    while (!s.players[0].hand.empty())
    {
        std::vector<int> rest_numbers;
        for (std::size_t k = 1; k < order.size(); ++k)
        { rest_numbers.push_back(s.players[0].hand[static_cast<std::size_t>(order[k])].m_number); }
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Au(s);
        std::vector<int> got_numbers;
        for (int i : order) { got_numbers.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_number); }
        CHECK(got_numbers == rest_numbers);
    }
}

void CheckBoard(const GameState& s, const std::vector<int>& want)
{
    const std::vector<int> got = Au(s);
    CHECK(got == want);
    CheckPermutation(s, got);
    CheckPrefixStable(s);
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
