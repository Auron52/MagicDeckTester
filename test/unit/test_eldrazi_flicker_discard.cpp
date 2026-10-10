// Unit tests for the EldraziDisplacerFlicker cleanup-discard buckets -- the USER-settled design of
// 2026-10-09 (docs/design/eldrazi-flicker-discard-policy-proposal.md, "EldraziDisplacerFlicker:
// SETTLED"): EldraziFlickerProvider::CleanupDiscardCandidates behind MTG_EDF_BUCKET_DISCARD.
//
//   KEEP: flicker source -> untapper -> win con (Living Wish fills a missing one) -> 2 lands (W + U)
//         -> Auras / mana up to the target -> Training Grounds (once mana reaches 4 / 5) -> extra
//         Living Wish -> 3rd-4th land.
//   SHED: Vexing Shusher -> duplicate legends -> 2nd Training Grounds -> hostless Aura / self-bouncing
//         Chancery -> Eladamri's Call with both pieces covered -> lands past the 4th -> Auras past the
//         target (Overgrowth > Trace > Fertile > Wild Growth) -> spare flicker sources / untappers
//         (less suitable first) -> spare win cons -> Training Grounds below its threshold -> extra
//         Living Wish -> protected.
//
// Every expected vector below was derived BY HAND from that text (and the agent readings R1-R10 the
// implementation's header lists), board by board, in the comment above each CHECK -- not read off
// the code's output. Every board also asserts the GENERIC fallback's index 0, the control arm that
// must differ, and E11 proves the =0 hatch restores exactly the generic ranking.
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

void EnsureCardsEdf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build the hand,
// the library and the sideboard that way so a policy reading the card's own masks would fail here.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// The deck's sideboard (decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod).
const std::vector<std::string> kSideboard = {
    "Azorius Chancery", "Adarkar Wastes", "Cloud of Faeries", "Eldrazi Displacer",
    "Mariposa Military Base", "Vexing Shusher", "Essence Depleter", "Dimensional Infiltrator",
};

std::vector<std::string> DefaultLibrary()
{
    return { "Eldrazi Displacer", "Emiel the Blessed", "Peregrine Drake", "Cloud of Faeries",
             "Brushland", "Conservatory", "Living Wish", "Wild Growth", "Yavimaya Coast", "Kitchen" };
}

// One board permanent: a card name, optionally an Aura attached to the board permanent at `host`
// (an index into the same board list, -1 = none).
struct Bp { std::string name; int host = -1; };

GameState MakeEdf(const std::vector<std::string>& hand, const std::vector<Bp>& board,
                  const std::vector<std::string>& library = DefaultLibrary(),
                  const std::vector<std::string>& sideboard = kSideboard)
{
    EnsureCardsEdf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.opponent_library_dealt = true;   // as in a real game (OpponentDeck), so the Infiltrator is live
    s.opponent_decked        = false;
    s.deck_has_land_aura     = true;
    int num = 1;
    for (const std::string& nm : hand)      { s.players[0].hand.push_back(Ph(nm, num++)); }
    for (const std::string& nm : library)   { s.players[0].library.push_back(Ph(nm, num++)); }
    for (const std::string& nm : sideboard) { s.players[0].sideboard.push_back(Ph(nm, num++)); }
    std::vector<int> numbers;
    for (const Bp& b : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(b.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (b.host >= 0) { p.aura_attached_to = numbers[static_cast<std::size_t>(b.host)]; }
        numbers.push_back(p.card.m_number);
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> EdfArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::EDF_BUCKET_DISCARD] = arm;
    std::vector<int> v = EldraziFlickerProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::EDF_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Edf(const GameState& s) { return EdfArm(s, 1); }
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones, if any, land last via the shared ranking's tier C).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

}   // namespace

TEST_CASE("EDF discard E1: pieces then lands -- Shusher first, the 4th land and the extra Wish before the Auras; generic sheds the Drake")
{
    // Board: Brushland, Yavimaya (2 lands, {W}{U} both covered, two {C} sources). No piece in play.
    // Flicker = Displacer (2+ {C}), untapper = Drake (the only one), win con = Conservatory (a draw
    // land), Wish = extra. Lands: board 2 + Conservatory = 3, so Adarkar is the 4th (land34).
    // Target = Displacer 3 + 3 = 6; mana = 2 board + Conservatory 1 = 3 -> Wild Growth (4) then
    // Overgrowth (6) are both kept. SHED: Shusher | land34 Adarkar | extra Wish | Overgrowth, Wild
    // Growth | Conservatory | Drake | Displacer.
    const GameState s = MakeEdf(
        { "Eldrazi Displacer", "Peregrine Drake", "Living Wish", "Vexing Shusher", "Overgrowth",
          "Wild Growth", "Adarkar Wastes", "Conservatory" },
        { {"Brushland"}, {"Yavimaya Coast"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 3, 6, 2, 4, 5, 7, 1, 0 });
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 1);   // generic max-MV sheds Peregrine Drake (MV 5), the deck's engine
    CHECK(o == EldraziFlickerProvider().CleanupDiscardCandidates(s, nullptr));   // default ON
}

TEST_CASE("EDF discard E2: duplicate legend, 2nd Training Grounds, Call with both pieces covered, the less suitable spare untapper")
{
    // Board: Emiel + Training Grounds + Brushland + Adarkar. Emiel in hand is a dead duplicate legend;
    // Training Grounds in hand is the inert 2nd copy. Untapper: TG available -> Cloud (cheapest viable)
    // over Drake; the Drake is the less suitable spare. Win con: Mariposa (draw land). Call: flicker and
    // untapper both covered. Target = Emiel 4 + max(1, 3-2) = 5; mana 2 + Mariposa 1 = 3 -> Fertile kept.
    // SHED: Shusher | Emiel | TG | Call | Drake | Fertile | Mariposa | Cloud.
    const GameState s = MakeEdf(
        { "Emiel the Blessed", "Training Grounds", "Vexing Shusher", "Cloud of Faeries", "Peregrine Drake",
          "Mariposa Military Base", "Fertile Ground", "Eladamri's Call" },
        { {"Emiel the Blessed"}, {"Training Grounds"}, {"Brushland"}, {"Adarkar Wastes"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 2, 0, 1, 7, 4, 6, 5, 3 });
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 4);   // generic: Drake
}

TEST_CASE("EDF discard E3: hostless Auras and a self-bouncing Chancery are blanks; Training Grounds below its threshold")
{
    // Empty board. Chancery has nothing to return (no land in play, no other non-karoo land in hand)
    // -> blank; then no host exists for the two Auras -> blanks. Win con: Essence Depleter (the only
    // one). Flicker: Displacer (the only one, though 0 {C} sources prefer Emiel). Untapper: Cloud (TG in
    // hand makes it viable). Target with TG = 3 + 1 = 4; mana 0 -> TG below threshold. Wish = extra.
    // SHED: Chancery, Wild Growth, Overgrowth (blanks, hand order) | TG | Wish | Depleter | Cloud |
    // Displacer.
    const GameState s = MakeEdf(
        { "Azorius Chancery", "Wild Growth", "Overgrowth", "Eldrazi Displacer", "Cloud of Faeries",
          "Living Wish", "Essence Depleter", "Training Grounds" },
        {});
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 1, 2, 7, 5, 6, 4, 3 });
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 2);   // generic: Overgrowth (MV 3, first of the MV-3 cards)
}

TEST_CASE("EDF discard E4: flicker source follows the {C} count -- Emiel with <2 {C}, Displacer with 2+")
{
    const std::vector<std::string> hand = {
        "Eldrazi Displacer", "Emiel the Blessed", "Peregrine Drake", "Wild Growth", "Fertile Ground",
        "Trace of Abundance", "Overgrowth", "Living Wish" };
    // (a) Board Conservatory + Kitchen: draw sources (win con filled), NO {C}. Emiel kept, Displacer
    // the less suitable spare. Target = Emiel 7; mana 2 -> WG 3, Fertile 4, Trace 5, Overgrowth 7: all
    // kept. SHED: Displacer | extra Wish | Overgrowth, Trace, Fertile, WG | Drake | Emiel.
    {
        const GameState s = MakeEdf(hand, { {"Conservatory"}, {"Kitchen"} });
        heurarm::t_arm[heurarm::EDF_OUTLET_PIPS] = 0;   // the rule as first built
        CHECK(Edf(s) == std::vector<int>{ 0, 7, 6, 5, 4, 3, 2, 1 });
        heurarm::t_arm[heurarm::EDF_OUTLET_PIPS] = -1;
        // MTG_EDF_OUTLET_PIPS (default ON): one white source (Conservatory) leaves Emiel {W}{W} one
        // short (key 0 + 2) and Displacer {2}{W} covered (key 1), so Displacer is the kept outlet and
        // Emiel the spare that sheds first. Target 6: the four Auras still all keep (2 -> 3, 4, 5, 7).
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 1, 7, 6, 5, 4, 3, 2, 0 });
        CHECK(Gen(s).front() == 2);   // generic: Drake
    }
    // (b) Board Mariposa + Brushland: two {C} sources (Mariposa is also the win con). Displacer kept,
    // Emiel the spare. Target = 6; mana 2 -> 3, 4, 5, then Overgrowth (5 < 6) -> all kept.
    {
        const GameState s = MakeEdf(hand, { {"Mariposa Military Base"}, {"Brushland"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 1, 7, 6, 5, 4, 3, 2, 0 });
        CHECK(Gen(s).front() == 2);
    }
}

// MTG_EDF_OUTLET_PIPS (ADOPTED default ON, USER 2026-10-10): the A/B game that lost a turn -- one {C}
// source (Mariposa), no white source, two Displacers and an Emiel in hand, no land in hand. The USER's
// rule as built keeps Emiel and sheds both Displacers; Emiel {2}{W}{W} is two white sources short and
// Displacer {2}{W} one, so the amendment keeps a Displacer and sheds Emiel first among the spares.
TEST_CASE("EDF discard E4b: the outlet-pips amendment keeps the outlet closer to castable")
{
    auto pips = [](const GameState& st, int lever)
    {
        heurarm::t_arm[heurarm::EDF_OUTLET_PIPS] = static_cast<std::int8_t>(lever);
        std::vector<int> v = Edf(st);
        heurarm::t_arm[heurarm::EDF_OUTLET_PIPS] = -1;
        return v;
    };
    auto pos = [](const std::vector<int>& o, int idx)
    { return static_cast<int>(std::find(o.begin(), o.end(), idx) - o.begin()); };
    const std::vector<std::string> hand = {
        "Eldrazi Displacer", "Eldrazi Displacer", "Emiel the Blessed", "Living Wish", "Peregrine Drake",
        "Overgrowth", "Trace of Abundance", "Training Grounds" };
    const GameState s = MakeEdf(hand, { {"Mariposa Military Base"}, {"Kitchen"}, {"Wild Growth", 1} });
    const std::vector<int> off = pips(s, 0);
    CHECK(pos(off, 0) < pos(off, 2));
    CHECK(pos(off, 1) < pos(off, 2));   // both Displacers shed before the kept Emiel
    const std::vector<int> on = pips(s, 1);
    CHECK(Edf(s) == on);                // default ON
    CHECK(pos(on, 2) < pos(on, 1));     // Emiel is the worst spare outlet ...
    CHECK(pos(on, 1) < pos(on, 0));     // ... the 2nd Displacer next; the first is the kept outlet
    CheckPermutation(s, on);
    // Two white sources on the board: Emiel is castable, so the amendment changes nothing.
    const GameState w2 = MakeEdf(hand, { {"Mariposa Military Base"}, {"Conservatory"}, {"Conservatory"} });
    CHECK(pips(w2, 1) == pips(w2, 0));
}

TEST_CASE("EDF discard E5: untapper -- Drake on plain lands, Cloud once its two lands make 4+ (Auras included)")
{
    const std::vector<std::string> hand = {
        "Cloud of Faeries", "Peregrine Drake", "Eldrazi Displacer", "Living Wish", "Living Wish",
        "Wild Growth", "Fertile Ground", "Kitchen" };
    // (a) Four plain lands (Brushland, Adarkar, Yavimaya, Mariposa = the win con). Cloud's top-2 untap
    // refunds 2 < 4 and no TG -> not viable; the Drake's top-5 refunds 4 -> the Drake is kept, the
    // Cloud the less suitable spare. Kitchen = the 5th land (surplus). Target 6; mana 4 -> WG 5,
    // Fertile 6 kept. SHED: Kitchen | Cloud | Wish, Wish | Fertile, WG | Drake | Displacer.
    {
        const GameState s = MakeEdf(hand, { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"},
                                            {"Mariposa Military Base"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 7, 0, 4, 3, 6, 5, 1, 2 });
        CHECK(Gen(s).front() == 1);   // generic: Drake
    }
    // (b) Same, but an Overgrowth on the Brushland: Cloud's two lands make 3 + 1 = 4 -> Cloud kept, the
    // Drake the less suitable spare. Board mana 6 already meets the target 6 -> both hand Auras are past
    // it, Fertile before Wild Growth. SHED: Kitchen | Fertile, WG | Drake | Wish, Wish | Cloud | Displacer.
    {
        const GameState s = MakeEdf(hand, { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"},
                                            {"Mariposa Military Base"}, {"Overgrowth", 0} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 7, 6, 5, 1, 4, 3, 0, 2 });
        CHECK(Gen(s).front() == 1);
    }
    // (c) Plain lands again, but Training Grounds in play: "or Training Grounds is available" -> Cloud.
    {
        const GameState s = MakeEdf(hand, { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"},
                                            {"Mariposa Military Base"}, {"Training Grounds"} });
        const std::vector<int> o = Edf(s);
        // Target with TG = 3 + 1 = 4; board mana 4 -> both Auras past it.
        // SHED: Kitchen | Fertile, WG | Drake | Wish, Wish | Cloud | Displacer.
        CHECK(o == std::vector<int>{ 7, 6, 5, 1, 4, 3, 0, 2 });
    }
}

TEST_CASE("EDF discard E6: tutors fill the missing slots -- Wish for the win con; Call for the flicker, Wish for the untapper")
{
    // (a) Board Displacer + Brushland + Adarkar; no win con anywhere -> the Wish fills it (Essence
    // Depleter in the sideboard). Call: both creature slots covered -> shed. Untapper: Drake (95)
    // over Cloud (98) on two plain lands. Yavimaya = the 3rd land. Target 6, mana 2 -> Fertile 3,
    // Trace 4, Overgrowth 6 kept. SHED: Call | Cloud | Yavimaya | Overgrowth, Trace, Fertile | Wish |
    // Drake.
    {
        const GameState s = MakeEdf(
            { "Living Wish", "Peregrine Drake", "Yavimaya Coast", "Fertile Ground", "Overgrowth",
              "Eladamri's Call", "Cloud of Faeries", "Trace of Abundance" },
            { {"Eldrazi Displacer"}, {"Brushland"}, {"Adarkar Wastes"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 5, 6, 2, 4, 7, 3, 0, 1 });
        CHECK(Gen(s).front() == 1);   // generic: Drake
    }
    // (b) Board Brushland + Yavimaya + Mariposa (win con). No outlet and no untapper anywhere: the Call
    // fills FLICKER (a Displacer in the library), the first Wish fills UNTAPPER (Cloud in the
    // sideboard), the second Wish is extra. Adarkar = the 4th land, Conservatory the 5th (surplus).
    // Target = Displacer 6 (what the Call fetches); mana 3 -> WG 4, Overgrowth 6 kept.
    // SHED: Shusher | Conservatory | Adarkar | Wish (extra) | Overgrowth, WG | Wish (untapper) | Call.
    {
        const GameState s = MakeEdf(
            { "Eladamri's Call", "Living Wish", "Living Wish", "Wild Growth", "Adarkar Wastes",
              "Vexing Shusher", "Overgrowth", "Conservatory" },
            { {"Brushland"}, {"Yavimaya Coast"}, {"Mariposa Military Base"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 5, 7, 4, 2, 6, 3, 1, 0 });
        CHECK(Gen(s).front() == 6);   // generic: Overgrowth
    }
}

TEST_CASE("EDF discard E7: Training Grounds kept once mana reaches the outlet + one reduced flicker")
{
    // Board Brushland + Adarkar + Mariposa (win con), mana 3. TG in hand -> target 3 + 1 = 4 and TG's
    // threshold 4. Lands: Yavimaya = 4th ({C} first), Kitchen surplus. Untapper: Cloud (TG) over Drake.
    // Mana 3 -> Wild Growth 4 -> TG (4 >= 4) kept. SHED: Kitchen | Drake | Yavimaya | Wish | TG | WG |
    // Cloud | Displacer.
    const GameState s = MakeEdf(
        { "Training Grounds", "Eldrazi Displacer", "Cloud of Faeries", "Wild Growth", "Living Wish",
          "Yavimaya Coast", "Kitchen", "Peregrine Drake" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Mariposa Military Base"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 6, 7, 5, 4, 0, 3, 2, 1 });
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 7);   // generic: Drake
}

TEST_CASE("EDF discard E8: Training Grounds below its threshold sheds after the spares, before the extra Wish")
{
    // Board Brushland + Mariposa (win con; two {C}), mana 2. Displacer kept (2 {C}), Emiel the less
    // suitable spare; Cloud (TG in hand) kept, Drake the less suitable spare; Infiltrator a spare win
    // con. TG threshold 4 > mana 2 -> below. SHED: Shusher | Emiel, Drake | Infiltrator | TG | Wish |
    // Cloud | Displacer.
    const GameState s = MakeEdf(
        { "Training Grounds", "Emiel the Blessed", "Cloud of Faeries", "Living Wish", "Peregrine Drake",
          "Eldrazi Displacer", "Dimensional Infiltrator", "Vexing Shusher" },
        { {"Brushland"}, {"Mariposa Military Base"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 7, 1, 4, 6, 0, 3, 2, 5 });
    CHECK(Gen(s).front() == 4);   // generic: Drake
}

TEST_CASE("EDF discard E9: lands -- 2 covering {W}+{U}, the 3rd-4th, the rest past the 4th")
{
    // Board Conservatory (G W; the win con). Need {U}: Aether Hub (in hand: {C} + every colour) wins the
    // cover ({C}-capable, most colours). Then the 3rd-4th: Brushland and Yavimaya ({C}, two colours;
    // hand order). Surplus, worst first: Chancery (tapped, no {C}, index 7) > Kitchen > Mariposa.
    // Flicker Displacer (3 {C} kept), untapper Drake. No Auras.
    // SHED: Chancery, Kitchen, Mariposa | Yavimaya, Brushland | Hub | Drake | Displacer.
    const GameState s = MakeEdf(
        { "Brushland", "Kitchen", "Mariposa Military Base", "Aether Hub", "Yavimaya Coast",
          "Eldrazi Displacer", "Peregrine Drake", "Azorius Chancery" },
        { {"Conservatory"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 7, 1, 2, 4, 0, 3, 6, 5 });
    CHECK(Gen(s).front() == 6);   // generic: Drake
}

TEST_CASE("EDF discard E10: a missing {U} is covered even as a 5th land; the other lands are past the 4th")
{
    // Board Brushland, Conservatory, Mariposa, Brushland: 4 lands, no {U}. Yavimaya ({C}, G U) covers it
    // and is kept in the 2-lands step; Kitchen and Adarkar are past the 4th (Kitchen worst). Only Emiel
    // can be the flicker; Cloud the untapper; two extra Wishes. Target = Emiel 7; mana 4 + Yavimaya 1 =
    // 5 -> Wild Growth kept. SHED: Kitchen, Adarkar | Wish, Wish | WG | Yavimaya | Cloud | Emiel.
    const GameState s = MakeEdf(
        { "Kitchen", "Yavimaya Coast", "Adarkar Wastes", "Cloud of Faeries", "Emiel the Blessed",
          "Living Wish", "Living Wish", "Wild Growth" },
        { {"Brushland"}, {"Conservatory"}, {"Mariposa Military Base"}, {"Brushland"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 2, 6, 5, 7, 1, 3, 4 });
    CHECK(Gen(s).front() == 4);   // generic: Emiel (MV 4)
}

TEST_CASE("EDF discard E11: Auras past the target shed Overgrowth > Trace > Fertile > Wild Growth; =0 is the generic ranking")
{
    // Board: Brushland + Adarkar, each with an Overgrowth (3 + 3), Mariposa (win con), Displacer and Drake
    // in play: board mana 7 >= target 6, so every hand Aura is past it. Spare Displacer and spare Cloud
    // (its two lands make 6) are both suitable -> outlets first. Yavimaya = the 4th land; Wish extra.
    // SHED: Overgrowth, Trace, Fertile, WG | Displacer, Cloud | Yavimaya | Wish.
    const GameState s = MakeEdf(
        { "Overgrowth", "Wild Growth", "Trace of Abundance", "Fertile Ground", "Eldrazi Displacer",
          "Cloud of Faeries", "Living Wish", "Yavimaya Coast" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Mariposa Military Base"}, {"Overgrowth", 0},
          {"Overgrowth", 1}, {"Eldrazi Displacer"}, {"Peregrine Drake"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 2, 3, 1, 4, 5, 7, 6 });
    // The =0 hatch: exactly the generic ranking (here its index 0 happens to agree; E1-E10 differ).
    CHECK(EdfArm(s, 0) == Gen(s));
    const GameState s1 = MakeEdf(
        { "Eldrazi Displacer", "Peregrine Drake", "Living Wish", "Vexing Shusher", "Overgrowth",
          "Wild Growth", "Adarkar Wastes", "Conservatory" },
        { {"Brushland"}, {"Yavimaya Coast"} });
    CHECK(EdfArm(s1, 0) == Gen(s1));
    CHECK(EdfArm(s1, 0) != Edf(s1));   // the control arm MUST differ
}

TEST_CASE("EDF discard E12: Shivan Gorge with red / without; finisher vs draw land; spare win cons")
{
    // (a) A Fertile Ground in play makes red (and black) reachable. Win con: Depleter (castable,
    // finisher, first by hand order among finishers) over the Gorge and Kitchen; the Infiltrator is a
    // spare win con. Lands: board 3 -> the Gorge ({C}) is the 4th, Kitchen past it. Emiel is the less
    // suitable spare outlet (Displacer in play, 4 {C}); Cloud the less suitable spare untapper (its two
    // lands make 3; the Drake in play makes 4). Target 6, mana 4 -> Wild Growth kept.
    // SHED: Kitchen | Emiel, Cloud | Infiltrator | Gorge | Wish | WG | Depleter.
    {
        const GameState s = MakeEdf(
            { "Shivan Gorge", "Essence Depleter", "Dimensional Infiltrator", "Living Wish", "Kitchen",
              "Emiel the Blessed", "Cloud of Faeries", "Wild Growth" },
            { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Fertile Ground", 0},
              {"Eldrazi Displacer"}, {"Peregrine Drake"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 4, 5, 6, 2, 0, 3, 7, 1 });
        CHECK(Gen(s).front() == 5);   // generic: Emiel (MV 4)
    }
    // (b) No red and no black anywhere: the Gorge is just a {C} land and the Depleter is a distance off.
    // Win con: Infiltrator (castable) over Conservatory (tie on distance 0, finisher first) and over the
    // Depleter (distance 1). Lands: Yavimaya then the Gorge = the 3rd-4th, Conservatory past the 4th.
    // Untapper: Drake (95) over Cloud (98). SHED: Conservatory | Cloud | Depleter | Gorge, Yavimaya |
    // Infiltrator | Drake | Displacer.
    {
        const GameState s = MakeEdf(
            { "Shivan Gorge", "Essence Depleter", "Conservatory", "Cloud of Faeries", "Eldrazi Displacer",
              "Dimensional Infiltrator", "Peregrine Drake", "Yavimaya Coast" },
            { {"Brushland"}, {"Adarkar Wastes"} });
        const std::vector<int> o = Edf(s);
        CHECK(o == std::vector<int>{ 2, 3, 1, 0, 7, 5, 6, 4 });
        CHECK(Gen(s).front() == 6);   // generic: Drake
    }
}

TEST_CASE("EDF discard E13: a second hand copy of a legend is dead; the first is the kept flicker source")
{
    // Board Conservatory + Kitchen (win con, no {C}); Brushland = the 3rd land -> 1 {C} -> Emiel
    // preferred. Emiel #2 is a duplicate legend. Target 7, mana 2 -> WG 3, Fertile 4 kept.
    // SHED: Shusher | Emiel #2 | Brushland | Wish | Fertile, WG | Drake | Emiel #1.
    const GameState s = MakeEdf(
        { "Emiel the Blessed", "Emiel the Blessed", "Peregrine Drake", "Wild Growth", "Living Wish",
          "Vexing Shusher", "Brushland", "Fertile Ground" },
        { {"Conservatory"}, {"Kitchen"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 5, 1, 6, 4, 7, 3, 2, 0 });
    CHECK(Gen(s).front() == 2);   // generic: Drake
}

TEST_CASE("EDF discard E14: a karoo with another land to return is live, and hosts the Auras; Wish fills the win con")
{
    // Empty board. Chancery is NOT self-bouncing (Brushland in hand); both are kept as the 2 lands,
    // Chancery first (it covers {W} AND {U}). No win con in hand -> the Wish fills it. Target 6, mana
    // 0 + 2 + 1 = 3 -> WG 4, Overgrowth 6 kept. SHED: Shusher | Overgrowth, WG | Brushland, Chancery |
    // Wish | Cloud | Displacer.
    const GameState s = MakeEdf(
        { "Azorius Chancery", "Brushland", "Wild Growth", "Eldrazi Displacer", "Cloud of Faeries",
          "Living Wish", "Vexing Shusher", "Overgrowth" },
        {});
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 6, 7, 2, 1, 0, 5, 4, 3 });
    CHECK(Gen(s).front() == 3);   // generic: Displacer (first MV-3 card)
}

TEST_CASE("EDF discard E15: a Wish with nothing left to fetch sheds with the covered Call")
{
    // Board Displacer + Drake + Brushland + Adarkar + Mariposa: every slot filled. Sideboard EMPTY ->
    // the Wish has no legal target. Yavimaya = the 4th land, Conservatory past it. Target 6, mana 3 ->
    // WG 4, Fertile 5, Trace 6 kept, Overgrowth past it. Cloud (98) is less suitable than the Drake in
    // play (95). SHED: Wish | Conservatory | Overgrowth | Cloud | Yavimaya | Trace, Fertile, WG.
    const GameState s = MakeEdf(
        { "Living Wish", "Wild Growth", "Yavimaya Coast", "Cloud of Faeries", "Fertile Ground",
          "Conservatory", "Overgrowth", "Trace of Abundance" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Mariposa Military Base"}, {"Eldrazi Displacer"},
          {"Peregrine Drake"} },
        DefaultLibrary(), {});
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 5, 6, 3, 2, 7, 4, 1 });
    CHECK(Gen(s).front() == 6);   // generic: Overgrowth
}

TEST_CASE("EDF discard E16: a staged card is never named by the policy; every index still appears once")
{
    // E1's hand with the Shusher staged: the policy's order is E1's minus index 3, and the shared
    // ranking's tier C appends the staged card last.
    GameState s = MakeEdf(
        { "Eldrazi Displacer", "Peregrine Drake", "Living Wish", "Vexing Shusher", "Overgrowth",
          "Wild Growth", "Adarkar Wastes", "Conservatory" },
        { {"Brushland"}, {"Yavimaya Coast"} });
    s.players[0].hand[3].m_is_staged = true;
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 6, 2, 4, 5, 7, 1, 0, 3 });
    CheckPermutation(s, o);
}

// ---- USER ruling 2026-10-10 on reading R8: "For EDF that is fine as long as we keep our only
// win-con/draw source even if it is a land." A SPARE draw land sheds as a land; the ONLY win con /
// draw source (hand + board) holds the size-1 WIN CON slot even when it is a land -- it is never in
// the land pool, so the land count, the {W}/{U} cover and a Living Wish cannot push it out. It sheds
// only at the win-con rung of the backwards ladder (just before the untapper and the flicker source).

namespace
{
// Position of hand index `i` in a shed order.
int PosOf(const std::vector<int>& order, int i)
{ return static_cast<int>(std::find(order.begin(), order.end(), i) - order.begin()); }
}   // namespace

TEST_CASE("EDF discard W1: the only draw source is kept past the 4th land even though it is the WORST land by the keep key")
{
    // Board: 4 lands (Brushland x2, Adarkar, Yavimaya), no draw source, no finisher. Kitchen is the
    // only win con -- and by the land keep key it is the worst land in hand (no {C}, enters tapped).
    // It holds the WIN CON slot, so it is not in the land pool: the three better hand lands are the
    // ones past the 4th. Displacer (4 {C}) / Drake (top-5 = 4) fill the pieces; the Wish is extra.
    // Target 6; mana 4 + Kitchen 1 = 5 -> Wild Growth kept.
    // SHED: Yavimaya, Adarkar, Brushland (past the 4th, worst first) | Wish | WG | Kitchen | Drake |
    // Displacer.
    const GameState s = MakeEdf(
        { "Kitchen", "Brushland", "Adarkar Wastes", "Yavimaya Coast", "Eldrazi Displacer", "Peregrine Drake",
          "Living Wish", "Wild Growth" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Brushland"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 3, 2, 1, 6, 7, 0, 5, 4 });
    for (int land : { 1, 2, 3 }) { CHECK(PosOf(o, land) < PosOf(o, 0)); }
    CHECK(Gen(s).front() == 5);   // generic: Drake
}

TEST_CASE("EDF discard W2: the only draw source is kept when ANOTHER land supplies the missing {U}")
{
    // Board: Brushland only (G W C). Mariposa ({C} only, covers no colour) is the only draw source ->
    // WIN CON slot, and it counts as the 2nd land. {U} is still missing, so the cover takes Yavimaya
    // (ties Adarkar on every key, earlier in hand); Adarkar is the 4th land; Brushland is past it.
    // Emiel is the only outlet, Cloud the only untapper; Wish extra. Target = Emiel 7; mana 1 +
    // Mariposa 1 + Yavimaya 1 = 3 -> Overgrowth kept.
    // SHED: Brushland | Adarkar | Wish | Overgrowth | Yavimaya | Mariposa | Cloud | Emiel.
    const GameState s = MakeEdf(
        { "Yavimaya Coast", "Mariposa Military Base", "Adarkar Wastes", "Brushland", "Emiel the Blessed",
          "Cloud of Faeries", "Living Wish", "Overgrowth" },
        { {"Brushland"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 3, 2, 6, 7, 0, 1, 5, 4 });
    CHECK(Gen(s).front() == 4);   // generic: Emiel (MV 4)
}

TEST_CASE("EDF discard W3: the only draw source is kept over two Living Wishes that could fetch a finisher")
{
    // Board: 4 lands, no win con. Conservatory is the only draw source -> WIN CON slot (a real card
    // fills the slot before any tutor is asked). Both Wishes could fetch Essence Depleter / Dimensional
    // Infiltrator, but no slot is missing, so both are EXTRA -- and they shed before the Conservatory.
    // Yavimaya is past the 4th. Target 6; mana 4 + Conservatory 1 = 5 -> Fertile kept.
    // SHED: Shusher | Yavimaya | Wish, Wish | Fertile | Conservatory | Drake | Displacer.
    const GameState s = MakeEdf(
        { "Living Wish", "Conservatory", "Living Wish", "Eldrazi Displacer", "Peregrine Drake",
          "Yavimaya Coast", "Fertile Ground", "Vexing Shusher" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Brushland"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 7, 5, 2, 0, 6, 1, 4, 3 });
    CHECK(PosOf(o, 0) < PosOf(o, 1));
    CHECK(PosOf(o, 2) < PosOf(o, 1));
    CHECK(Gen(s).front() == 4);   // generic: Drake
}

TEST_CASE("EDF discard W4: a finisher that is not live does not make the draw land a spare")
{
    // No opponent library dealt -> Dimensional Infiltrator is not a finisher (IsTlessFinisher), so
    // Mariposa is the only win con / draw source: WIN CON slot, and the Infiltrator is a (non-live)
    // spare win con. Board 4 lands -> Adarkar and Yavimaya are past the 4th (Yavimaya, later in hand,
    // first). Cloud (98) is the only untapper. Target 6; mana 4 + 1 = 5 -> Wild Growth kept.
    // SHED: Yavimaya, Adarkar | Infiltrator | Wish | WG | Mariposa | Cloud | Displacer.
    GameState s = MakeEdf(
        { "Dimensional Infiltrator", "Mariposa Military Base", "Adarkar Wastes", "Eldrazi Displacer",
          "Cloud of Faeries", "Living Wish", "Wild Growth", "Yavimaya Coast" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Brushland"} });
    s.opponent_library_dealt = false;
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 7, 2, 0, 5, 6, 1, 4, 3 });
    CHECK(Gen(s).front() == 3);   // generic: Displacer (MV 3)
}

TEST_CASE("EDF discard W5: Shivan Gorge with red is the only win con -- kept past the 4th land")
{
    // A Fertile Ground on the board makes red: the Gorge is a win con, the only one, so it holds the
    // slot; the two other hand lands are past the 4th. Board mana 5 + Gorge 1 = 6 meets the target ->
    // Overgrowth is past it. SHED: Shusher | Adarkar, Brushland | Overgrowth | Wish | Gorge | Drake |
    // Displacer.
    const GameState s = MakeEdf(
        { "Shivan Gorge", "Brushland", "Adarkar Wastes", "Eldrazi Displacer", "Peregrine Drake",
          "Living Wish", "Overgrowth", "Vexing Shusher" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Brushland"}, {"Fertile Ground", 0} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 7, 2, 1, 6, 5, 0, 4, 3 });
    CHECK(Gen(s).front() == 4);   // generic: Drake
}

TEST_CASE("EDF discard W6: with a draw source already in PLAY, the hand's draw land is a spare and sheds as a land (R8)")
{
    // Mariposa in play fills the WIN CON slot net of board, so the Conservatory in hand is the 5th land
    // and sheds first. Drake (top-5 = 4) over Cloud (98, less suitable). Target 6; mana 4 -> WG 5,
    // Fertile 6; Trace past it. SHED: Conservatory | Trace | Cloud | Wish | Fertile, WG | Drake |
    // Displacer.
    const GameState s = MakeEdf(
        { "Conservatory", "Eldrazi Displacer", "Peregrine Drake", "Living Wish", "Wild Growth",
          "Fertile Ground", "Cloud of Faeries", "Trace of Abundance" },
        { {"Mariposa Military Base"}, {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 3, 5, 4, 2, 1 });
    CHECK(Gen(s).front() == 2);   // generic: Drake
}

TEST_CASE("EDF discard W7: a castable finisher in hand holds the slot -- the draw land is then a spare land")
{
    // "Our ONLY win-con/draw source" = the size-1 WIN CON bucket, which a finisher and a draw land fill
    // alike -- CONFIRMED by the USER 2026-10-10: "the win-con and draw source take the same slot". With
    // a castable Dimensional Infiltrator AND a Mariposa in hand, the Infiltrator holds the slot
    // (distance 0, finisher first on the tie) and the Mariposa is a spare: a land, past the 4th here.
    // SHED: Mariposa, Brushland (past the 4th, worst first) | Wish | Fertile, WG | Infiltrator | Drake |
    // Displacer.
    const GameState s = MakeEdf(
        { "Mariposa Military Base", "Dimensional Infiltrator", "Eldrazi Displacer", "Peregrine Drake",
          "Wild Growth", "Living Wish", "Brushland", "Fertile Ground" },
        { {"Brushland"}, {"Adarkar Wastes"}, {"Yavimaya Coast"}, {"Brushland"} });
    const std::vector<int> o = Edf(s);
    CHECK(o == std::vector<int>{ 0, 6, 5, 7, 4, 1, 3, 2 });
    CHECK(Gen(s).front() == 3);   // generic: Drake
}
