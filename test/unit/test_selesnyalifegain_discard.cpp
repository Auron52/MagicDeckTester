// Unit tests for the SelesnyaLifegain cleanup-discard BUCKETS:
// SelesnyaLifegainProvider::CleanupDiscardCandidates, implementing the USER-settled design in
// docs/design/selesnyalifegain-discard-policy.md ("SelesnyaLifegain: SETTLED", 2026-10-09):
//   LANDS 3 counting board, TWO W sources while holding a Paragon, dead Sanctuary first;
//   ELVES the primary mana toward the mana target, counting their output, Priest > Archdruid >
//   Llanowar = Mystic > Alchemist; LIFEGAIN 1 (Feed > Wellwisher) while a lifegain payoff is held or in
//   play; PAYOFFS 2 + soft 3rd by castability x enablement, equal ones Bogbeast > Paragon > Craterhoof >
//   Ageless > Avatar, Paragon / Ageless enabled only with a lifegain source, one Paragon kept when
//   reachable soon and enabled, Genesis Wave only with 8+ mana within 2 turns;
//   SHED dead Sanctuary -> surplus lands -> lifegain with no payoff -> payoffs not reachable / not
//   enabled (Wave first) -> surplus Elves -> keep tail.
// Each expected order is derived BY HAND from that text plus the agent readings recorded at the
// implementation (the two-turn mana projection M1 / M2 is worked in each comment). Every board pins the
// GENERIC fallback's index 0 -- the control arm that must differ -- and one case proves the
// MTG_SELESNYA_BUCKET_DISCARD=0 hatch restores the generic ranking exactly.
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

void EnsureCardsSelesnyaDiscard()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Hand cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeSel(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsSelesnyaDiscard();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(Ph(n, num++)); }
    for (int k = 0; k < 20; ++k)      { s.players[0].library.push_back(Ph("Forest", num++)); }
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

// `dist`: the MTG_DISCARD_DIST_MODEL arm -- 0 = the previous two-turn projection (the S-cases were derived
// for it, and pin that =0 still reproduces it), 1 = the shared discard_dist model (the M-cases), -1 = env.
std::vector<int> SelArmDist(const GameState& s, int arm, int dist)
{
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = arm;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]      = dist;
    std::vector<int> v = SelesnyaLifegainProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]      = -1;
    return v;
}
std::vector<int> SelArm(const GameState& s, int arm) { return SelArmDist(s, arm, -1); }
std::vector<int> Sel(const GameState& s)  { return SelArmDist(s, 1, 0); }   // the =0 distance arm
std::vector<int> SelM(const GameState& s) { return SelArmDist(s, 1, 1); }   // the shared model
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

// No land in play and only a Sanctuary in hand: it would return ITSELF -- dead, first. M1 = M2 = 0, so
// no payoff is reachable -- and poor mana no longer empties the bucket (USER 2026-10-10: "keep the
// cheapest effective payoffs if mana is really poor"): Bogbeast (5), Paragon (6) hard, Craterhoof (8)
// soft. The uncastable Elves (farthest first) shed before them. Ladder: Bogbeast, Paragon, Feed (a
// lifegain payoff is held), Craterhoof. Under the model every payoff is stranded (0 lands, W
// impossible), so it is also cheapest-first and the order is the same in both arms.
// (Before the ruling all three payoffs shed first: [0, 7, 4, 6, 3, 2, 1, 5].)
TEST_CASE("Selesnya discard S1: a dead Selesnya Sanctuary is shed first; poor mana keeps the payoffs")
{
    const GameState s = MakeSel(
        { "Selesnya Sanctuary", "Llanowar Elves", "Priest of Titania", "Elvish Archdruid",
          "Nykthos Paragon", "Feed the Clan", "Blossoming Bogbeast", "Craterhoof Behemoth" },
        {});
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 3, 2, 1, 7, 5, 4, 6 });
    CHECK(SelM(s) == o);
    CHECK(Gen(s).front() == 7);   // generic max-MV sheds Craterhoof
    CheckPermutation(s, o);
}

// Board 2 Forests -> 1 more land for LANDS 3, but a Paragon is held and the board has no W source, so
// TWO W lands are kept (Brushland, Branchloft's W face) and both Forests are surplus.
// M1 = 2 + 1 = 3; Priest + Llanowar deploy within 3; M2 = 2 + 2 + Priest(2 Elves) + 1 = 7.
// Paragon (6, WW) is distance 2 with w2 = 2; Bogbeast (5) distance 2; equal -> Bogbeast > Paragon.
// Target = 6 -> both Elves kept. LANDS 3 needs ONE land, so Brushland takes the 1st land slot and
// Branchloft is the W land kept PAST the quota, laddered after the 3rd land slot (review fix S-4: it
// used to take the 2nd slot). Ladder: Brushland, Priest, Bogbeast, Llanowar, Paragon, Branchloft, Feed.
TEST_CASE("Selesnya discard S2: two W sources while holding a Paragon; surplus Forests go first")
{
    const GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 5, 3, 4, 7, 8, 6, 2 });
    CHECK(Gen(s).front() == 4);   // generic sheds the Paragon
}

TEST_CASE("Selesnya discard S2b: MTG_SELESNYA_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    const std::vector<int> off = SelArm(s, 0);
    const std::vector<int> on  = SelArm(s, 1);
    CHECK(off == Gen(s));
    CHECK(off.front() == 4);
    CHECK(on.front() == 1);
    CHECK(on != off);   // the control that MUST differ
    // ...and the one-consultation cleanup is restored with it (the generic ranking is prefix-stable).
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = 0;
    CHECK(SelesnyaLifegainProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = 1;
    CHECK_FALSE(SelesnyaLifegainProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::SELESNYA_BUCKET_DISCARD] = -1;
}

// A held Bogbeast is a LIFEGAIN PAYOFF (its pump is the life gained this turn -- review fix S-5), so
// LIFEGAIN 1 keeps Feed the Clan and only Wellwisher is surplus lifegain. Board 3 lands -> the hand
// Forest is surplus. M1 = 3, M2 = 3 + 1 + 1 = 5: Wave (8) below 8 sheds first among payoffs, Avatar
// (7, 2 short) next; Bogbeast (5) kept. Ladder: Llanowar, Bogbeast, Mystic, Feed.
TEST_CASE("Selesnya discard S3: a held Bogbeast keeps a Feed the Clan; Wave below 8 mana first")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Feed the Clan", "Blossoming Bogbeast", "Llanowar Elves", "Elvish Mystic",
          "Forest", "Wellwisher", "Verdant Sun's Avatar" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 6, 0, 7, 1, 4, 2, 3 });
    CHECK(Gen(s).front() == 7);   // generic sheds the Avatar (MV 7)
}

// No lifegain payoff held or in play (Craterhoof / Avatar / Wave only) -> Feed the Clan and Wellwisher
// are "lifegain with no payoff", Wellwisher first (Feed > Wellwisher). Nothing is reachable (M2 = 5):
// the Wave below 8 is not effective and sheds; Avatar (7) and Craterhoof (8) are effective and now KEEP
// the bucket, cheapest first (USER 2026-10-10). Target 8 -> both Elves. Ladder: Llanowar, Avatar,
// Mystic, Craterhoof. (Before the ruling they shed: [5, 6, 1, 0, 2, 7, 4, 3].)
TEST_CASE("Selesnya discard S3b: lifegain with no payoff goes right after the surplus land")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Feed the Clan", "Craterhoof Behemoth", "Llanowar Elves", "Elvish Mystic",
          "Forest", "Wellwisher", "Verdant Sun's Avatar" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 6, 1, 0, 2, 4, 7, 3 });
    CHECK(Gen(s).front() == 2);   // generic sheds Craterhoof
}

// A Paragon is held but only ONE W source exists (Brushland), so Paragon is not reachable; Wave below 8
// is the FIRST shed, then Craterhoof and the Paragon (unreachable, the USER's order reversed). Feed the
// Clan is the ONLY lifegain source and enables the kept Ageless, so it is laddered right after it.
// M1 = 3 + 1 = 4, M2 = 3 + 1 + 1 + 1 = 6; target 5 -> Llanowar kept, Mystic surplus. Board 3 lands ->
// LANDS 3 is met, so Brushland is a W land kept PAST the quota and sits after the land slots (S-4).
// Ladder: Llanowar, Ageless, Feed, Brushland. Unreachable order: Craterhoof (2 short) > Paragon (1 W).
TEST_CASE("Selesnya discard S4: Genesis Wave below 8 mana within 2 turns is shed first")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Ageless Entity", "Feed the Clan", "Llanowar Elves", "Elvish Mystic",
          "Nykthos Paragon", "Brushland", "Craterhoof Behemoth" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 7, 5, 4, 6, 2, 1, 3 });
    CHECK(Gen(s).front() == 7);   // generic sheds Craterhoof
}

// Board 3 Forests + Priest + Llanowar (Priest taps for 2): M1 = 6; Archdruid + Mystic deploy, and M2 =
// 3 + Priest(4) + 1 + Archdruid(4) + 1 = 13 >= 8, so the Wave IS castable within 2 turns. Ranking by
// castability: Bogbeast, Ageless (distance 1; Ageless enabled by the Bogbeast / Avatar in hand), then
// Craterhoof > Wave > Avatar at distance 2 -- the 4th and 5th are surplus. Target 8 -> Archdruid alone
// reaches it (M2 = 10), Mystic is a surplus Elf.
TEST_CASE("Selesnya discard S5: castability first, Wave with 8+ mana is a real payoff, Elves scale")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Forest", "Elvish Archdruid", "Elvish Mystic", "Blossoming Bogbeast",
          "Verdant Sun's Avatar", "Craterhoof Behemoth", "Ageless Entity" },
        { "Forest", "Forest", "Forest", "Priest of Titania", "Llanowar Elves" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 5, 0, 3, 6, 7, 4, 2 });
    CHECK(Gen(s).front() == 6);   // generic sheds Craterhoof
}

// "Keep one Paragon when reachable soon and enabled": castability alone ranks Bogbeast, Ageless
// (distance 1, M1 = 5) above the Paragon (distance 2, WW from Brushland + Branchloft); the Paragon takes
// the 2nd HARD slot and Ageless becomes the soft 3rd. Target 6 -> Priest kept, Llanowar / Mystic surplus.
// Board 4 lands -> Branchloft is kept only for the WW, past LANDS 3 (after the land slots, S-4).
// Ladder: Priest, Bogbeast, Paragon, Branchloft, Feed, Ageless (soft).
TEST_CASE("Selesnya discard S6: a reachable, enabled Paragon is promoted to a hard slot")
{
    const GameState s = MakeSel(
        { "Nykthos Paragon", "Ageless Entity", "Blossoming Bogbeast", "Llanowar Elves",
          "Priest of Titania", "Elvish Mystic", "Branchloft Pathway", "Feed the Clan" },
        { "Forest", "Brushland", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 3, 1, 7, 6, 0, 2, 4 });
    CHECK(Gen(s).front() == 0);   // generic sheds the Paragon
}

// No lifegain source in hand or play: Paragon and Ageless are reachable but NOT enabled -> shed in the
// payoff group (both 0 short, so Ageless first by the USER's order reversed). Craterhoof is the one live
// payoff (M2 = 10: the best deployment is Priest + Llanowar + Mystic); target 8 keeps Priest +
// Archdruid (M2 = 9), Llanowar and Mystic are surplus Elves. Branchloft (W, past LANDS 3) after the
// land slots. Ladder: Priest, Craterhoof, Archdruid, Branchloft.
TEST_CASE("Selesnya discard S7: Paragon / Ageless with no lifegain source are not enabled")
{
    const GameState s = MakeSel(
        { "Nykthos Paragon", "Ageless Entity", "Craterhoof Behemoth", "Llanowar Elves",
          "Priest of Titania", "Elvish Mystic", "Branchloft Pathway", "Elvish Archdruid" },
        { "Forest", "Brushland", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 5, 3, 6, 7, 2, 4 });
    CHECK(Gen(s).front() == 2);   // generic sheds Craterhoof
}

// USER 2026-10-10 (creature quotas are filled from the hand): a Wellwisher in play no longer fills
// LIFEGAIN 1, so the hand Feed the Clan is KEPT (a lifegain payoff is held). M1 = 3, M2 = 7 (Priest +
// Llanowar); Bogbeast, Ageless distance 2; target 5 -> Priest alone. Ladder: Priest, Bogbeast,
// Ageless, Feed; backwards after the surplus Forest and Elves (Mystic, Llanowar, Archdruid).
// (Netting against the board shed the Feed right after the Forest: [5, 0, 6, 2, 7, 1, 4, 3].)
TEST_CASE("Selesnya discard S8: a Wellwisher in play does not fill LIFEGAIN 1")
{
    const GameState s = MakeSel(
        { "Feed the Clan", "Ageless Entity", "Llanowar Elves", "Priest of Titania", "Blossoming Bogbeast",
          "Forest", "Elvish Mystic", "Elvish Archdruid" },
        { "Forest", "Forest", "Forest", "Wellwisher" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 5, 6, 2, 7, 0, 1, 4, 3 });
    CHECK(Gen(s).front() == 1);   // generic sheds Ageless (MV 5, first)
}

TEST_CASE("Selesnya discard S9: a staged card is never named by the policy and sorts last")
{
    GameState s = MakeSel(
        { "Forest", "Forest", "Brushland", "Branchloft Pathway", "Nykthos Paragon", "Feed the Clan",
          "Priest of Titania", "Llanowar Elves", "Blossoming Bogbeast" },
        { "Forest", "Forest" });
    s.players[0].hand[4].m_is_staged = true;   // the Paragon: no W rule, no lifegain payoff held
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 0, 3, 5, 7, 8, 6, 2, 4 });
    CHECK(std::find(o.begin(), o.end() - 1, 4) == o.end() - 1);
}

// WHY CleanupDiscardShedStable() is false: "held" is literal, so an unreachable Paragon (no W source)
// keeps its Feed the Clan -- but once that Paragon is shed the Feed is lifegain with no payoff and jumps
// to the front. A batched prefix would have shed the Llanowar next; the rule applied card by card sheds
// the Feed. Board: 3 Forests + Priest + Llanowar (M1 = 6, M2 = 16); Craterhoof and Avatar are the kept
// payoffs, target 8 -> Archdruid alone (M2 = 10), Llanowar / Mystic surplus.
TEST_CASE("Selesnya discard S10: the order is not prefix-stable, which is why the engine re-asks")
{
    GameState s = MakeSel(
        { "Nykthos Paragon", "Feed the Clan", "Elvish Mystic", "Craterhoof Behemoth", "Elvish Archdruid",
          "Forest", "Verdant Sun's Avatar", "Llanowar Elves" },
        { "Forest", "Forest", "Forest", "Priest of Titania", "Llanowar Elves" });
    CHECK(Sel(s) == std::vector<int>{ 5, 0, 7, 2, 1, 6, 3, 4 });
    s.players[0].hand.erase(s.players[0].hand.begin() + 5);   // shed the surplus Forest
    CHECK(Sel(s) == std::vector<int>{ 0, 6, 2, 1, 5, 3, 4 });  // still the prefix
    s.players[0].hand.erase(s.players[0].hand.begin() + 0);   // shed the Paragon
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 0, 5, 1, 4, 2, 3 });          // Feed first, not the Llanowar
    CHECK(o != std::vector<int>{ 5, 1, 0, 4, 2, 3 });          // the batched remainder
}

// THE USER'S POOR-MANA RULING (2026-10-10): "keep the cheapest effective payoffs if mana is really
// poor". Board 3 Forests, no land in hand: M1 = 3, M2 = 4 (Archdruid alone; the Alchemists cost 4) --
// NOTHING is reachable. The quota is kept anyway, cheapest effective first: Bogbeast (5) and Ageless (5)
// hard, Paragon (6) soft; Craterhoof (8) is the surplus 4th and sheds first. Then the uncastable
// Alchemists. Target 6 -> Archdruid. Ladder: Archdruid, Bogbeast, Ageless, Feed, Paragon.
// (Before the ruling every payoff shed, farthest first: [1, 6, 0, 7, 4, 5, 2, 3].)
TEST_CASE("Selesnya discard S11: poor mana keeps the cheapest effective payoffs")
{
    const GameState s = MakeSel(
        { "Ageless Entity", "Craterhoof Behemoth", "Feed the Clan", "Elvish Archdruid",
          "Accomplished Alchemist", "Accomplished Alchemist", "Nykthos Paragon", "Blossoming Bogbeast" },
        { "Forest", "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 1, 4, 5, 6, 2, 0, 7, 3 });
    CHECK(Gen(s).front() == 1);
}

// REVIEW FIX S-1 (=0 arm): an out-of-reach payoff is ordered by its DISTANCE to castable, the USER's tie
// order only on an equal gap. Board 3 Forests + Llanowar: M1 = 4; Archdruid deploys -> M2 = 6. Bogbeast
// and Ageless (5) are reachable and kept; out of reach: Craterhoof 2 short and Paragon 2 (W) -> Craterhoof
// first on the tie; then Avatar (1 short). (The tie order alone would shed the Avatar first.)
TEST_CASE("Selesnya discard S11b: unreachable payoffs shed farthest first")
{
    const GameState s = MakeSel(
        { "Ageless Entity", "Craterhoof Behemoth", "Feed the Clan", "Blossoming Bogbeast", "Nykthos Paragon",
          "Verdant Sun's Avatar", "Forest", "Elvish Archdruid" },
        { "Forest", "Forest", "Forest", "Llanowar Elves" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 6, 1, 4, 5, 2, 0, 3, 7 });
    CHECK(Gen(s).front() == 1);
}

// REVIEW FIX S-3: M2 is the BEST next-turn deployment, not the greedy one in keep order. Board 2
// Forests, one Forest kept: M1 = 3. Greedy Archdruid (3) taps for 1 -> M2 = 4; Llanowar + Mystic
// make M2 = 5, so Ageless and Bogbeast (5) ARE reachable at distance 2 and kept (Bogbeast > Ageless);
// Craterhoof (3 short) is the only unreachable payoff. Target 5 needs all three Elves offered.
// Ladder: Forest, Archdruid, Bogbeast, Llanowar, Ageless, Feed, Mystic.
TEST_CASE("Selesnya discard S12: the mana projection takes the best Elf deployment")
{
    const GameState s = MakeSel(
        { "Forest", "Elvish Archdruid", "Llanowar Elves", "Elvish Mystic", "Ageless Entity",
          "Blossoming Bogbeast", "Feed the Clan", "Craterhoof Behemoth" },
        { "Forest", "Forest" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 7, 3, 6, 4, 2, 5, 1, 0 });
    CHECK(Gen(s).front() == 7);
    CHECK(o != Gen(s));
}

// REVIEW FIX S-5 (the reviewer's board): Blossoming Bogbeast's pump is +X/+X for the life gained THIS
// TURN (cards.json attack_team_pump_per_life_gained), so a held Bogbeast makes Feed the Clan "lifegain
// WITH a payoff" -- kept, no longer the first shed. Board 3 Forests + Priest: M1 = 4; best deployment
// Archdruid + Llanowar -> M2 = 10, so Bogbeast, Craterhoof, Wave (8) and Avatar are all distance 2:
// keep Bogbeast > Craterhoof (hard) and Wave (soft); Avatar is the surplus 4th and sheds first, then
// the surplus Mystic. Target 8 -> Archdruid + Llanowar. Ladder: Archdruid, Bogbeast, Llanowar,
// Craterhoof, Feed, Wave.
TEST_CASE("Selesnya discard S13: a held Bogbeast is a lifegain payoff for Feed the Clan")
{
    const GameState s = MakeSel(
        { "Feed the Clan", "Blossoming Bogbeast", "Verdant Sun's Avatar", "Craterhoof Behemoth",
          "Elvish Archdruid", "Llanowar Elves", "Elvish Mystic", "Genesis Wave" },
        { "Forest", "Forest", "Forest", "Priest of Titania" });
    const std::vector<int> o = Sel(s);
    CHECK(o == std::vector<int>{ 2, 6, 7, 0, 3, 5, 1, 4 });
    CHECK(Gen(s).front() == 3);   // generic sheds Craterhoof
}

// ---- USER 2026-10-10 (a): payoffs in play fill no payoff slot -----------------------------------------
// Board 3 Forests + Bogbeast + Paragon in play. =0 arm: M1 = 3, M2 = 6 (Priest + Llanowar); Ageless (5)
// is the one reachable payoff and now takes a HARD slot (2 + soft 3rd from the hand); Craterhoof (2 short)
// and Avatar (1 short) shed. Target 5 -> Priest, Llanowar. Ladder: Priest, Ageless, Llanowar, Feed.
// Model arm: census 3 lands, Elves add 3 from turn 2 -> by turn 2 there are 6 mana + 2 drawn Forests, so
// Craterhoof (8) and Avatar (7) are also castable on turn 2 and every payoff is kept: ev Craterhoof 0.75 x
// 0.92 = 0.69 > Ageless 0.75 x 0.84 = 0.63 > Avatar 0.75 x 0.80 = 0.60. Target 8 -> all three Elves.
// Ladder: Priest, Craterhoof, Llanowar, Ageless, Feed, Mystic, Avatar.
// (Netting the two payoffs in play left ONE soft slot: [7, 1, 5, 3, 0, 4, 2, 6].)
TEST_CASE("Selesnya discard M1: payoffs in play do not fill the hand's payoff slots")
{
    const GameState s = MakeSel(
        { "Ageless Entity", "Craterhoof Behemoth", "Llanowar Elves", "Elvish Mystic", "Feed the Clan",
          "Verdant Sun's Avatar", "Priest of Titania", "Forest" },
        { "Forest", "Forest", "Forest", "Blossoming Bogbeast", "Nykthos Paragon" });
    CHECK(Sel(s)  == std::vector<int>{ 7, 1, 5, 3, 4, 2, 0, 6 });
    CHECK(SelM(s) == std::vector<int>{ 7, 5, 3, 4, 0, 2, 1, 6 });
    CHECK(Gen(s).front() == 1);
}

// (a) for the "keep one Paragon" rule: a Paragon in play no longer waives the hard slot for a reachable,
// enabled hand Paragon. Board S6's + a Paragon in play: the hand Paragon still takes the 2nd hard slot
// and the result is S6's exactly. (Netting: 1 hard + 1 soft, no promotion -> the Paragon was surplus
// and every Elf too: [0, 5, 3, 4, 1, 7, 6, 2].)
TEST_CASE("Selesnya discard M2: a Paragon in play does not waive the hand Paragon's hard slot")
{
    const GameState s = MakeSel(
        { "Nykthos Paragon", "Ageless Entity", "Blossoming Bogbeast", "Llanowar Elves",
          "Priest of Titania", "Elvish Mystic", "Branchloft Pathway", "Feed the Clan" },
        { "Forest", "Brushland", "Forest", "Forest", "Nykthos Paragon" });
    CHECK(Sel(s) == std::vector<int>{ 5, 3, 1, 7, 6, 0, 2, 4 });
    CHECK(Gen(s).front() == 0);
}

// ---- (c) the shared distance model (default arm). The library is 20 Forests, so every draw is a G land:
// mana by turn t = census (lands + mana creatures at 1) + the projection's extra + kept drops + one drawn
// land per turn with no kept land left; W can come only from the board or a kept land. CastFactor =
// 0.75^(t-1) for the first castable turn t (0.02 x 0.75^4 if none within 4 turns, "stranded").

// S3's board. Census 3 Forests; Llanowar + Mystic deploy next turn -> extra 2 from turn 2. Bogbeast (5):
// turn 2 (5 mana) -> 0.75 x 1.00. Avatar (7, GG): turn 2, 5 mana + 2 drawn Forests -> 0.75 x 0.80 =
// 0.60 -- castable and KEPT, where the =0 projection (no drawn lands) shed it as unreachable. The Wave
// (8 not within 2 turns on its own rule) sheds first among payoffs. Target 7 -> both Elves.
// Ladder: Llanowar, Bogbeast, Mystic, Avatar, Feed.
TEST_CASE("Selesnya discard M3: the model sees drawn land drops -- an Avatar castable on turn 2 is kept")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Feed the Clan", "Blossoming Bogbeast", "Llanowar Elves", "Elvish Mystic",
          "Forest", "Wellwisher", "Verdant Sun's Avatar" },
        { "Forest", "Forest", "Forest" });
    CHECK(SelM(s) == std::vector<int>{ 5, 6, 0, 1, 7, 4, 2, 3 });
    CHECK(Sel(s)  == std::vector<int>{ 5, 6, 0, 7, 1, 4, 2, 3 });   // =0: the previous projection
    CHECK(SelArm(s, 1) == SelM(s));                                   // the model is the default
}

// S5's board. Census 3 Forests + Priest + Llanowar = 5; extra 1 on turn 1 (Priest taps for 2), 8 from
// turn 2 (Archdruid + Mystic: M2 13). Turn 1 has 6 mana + 1 drawn Forest: Bogbeast 1.00, Ageless 0.84,
// Avatar (7) 0.80; turn 2: Craterhoof 0.75 x 0.92 = 0.69, Wave (8+ within 2 turns: effective) 0.75 x 0.88 =
// 0.66. Keep Bogbeast, Ageless (hard), Avatar (soft); Wave then Craterhoof surplus. Target 7 -> Archdruid
// (M2 10), Mystic surplus. Ladder: Archdruid, Bogbeast, Ageless, Avatar.
TEST_CASE("Selesnya discard M4: CastFactor x value -- a turn sooner beats the USER's tie order")
{
    const GameState s = MakeSel(
        { "Genesis Wave", "Forest", "Elvish Archdruid", "Elvish Mystic", "Blossoming Bogbeast",
          "Verdant Sun's Avatar", "Craterhoof Behemoth", "Ageless Entity" },
        { "Forest", "Forest", "Forest", "Priest of Titania", "Llanowar Elves" });
    CHECK(SelM(s) == std::vector<int>{ 1, 0, 6, 3, 5, 7, 4, 2 });
    CHECK(Sel(s)  == std::vector<int>{ 1, 5, 0, 3, 6, 7, 4, 2 });   // =0 (S5)
}

// USER 2026-10-10 under the model: one Forest in play, no land in hand, no Elf -> mana by turn t is 1 + t.
// Bogbeast and Ageless (5) are castable on turn 4 (0.75^3 = 0.42 x 1.00 / 0.84); Paragon (no W anywhere),
// Avatar (7) and Craterhoof (8) are STRANDED and follow, cheapest first -- Paragon (6) takes the soft 3rd,
// and of the surplus the dearest (Craterhoof) sheds first (by value alone the Avatar would). The Wave is
// not effective. Wellwisher is the surplus lifegain. Ladder: Bogbeast, Ageless, Feed, Paragon.
TEST_CASE("Selesnya discard M5: stranded payoffs keep the quota cheapest first")
{
    const GameState s = MakeSel(
        { "Craterhoof Behemoth", "Verdant Sun's Avatar", "Feed the Clan", "Wellwisher", "Nykthos Paragon",
          "Genesis Wave", "Blossoming Bogbeast", "Ageless Entity" },
        { "Forest" });
    CHECK(SelM(s) == std::vector<int>{ 3, 5, 0, 1, 4, 2, 7, 6 });
    CHECK(Gen(s).front() == 0);
}
