// Unit tests for the Angels cleanup-discard policy -- the USER's design (2026-10-09),
// AngelsProvider::CleanupDiscardCandidates, docs/design/angels-discard-policy-proposal.md
// "Angels: SETTLED":
//   MANA 4 sources counting the board, >= 2 white, 5th soft; Sol Ring (while none in play) and one
//   Giada protected OUTSIDE the ladder; ENABLERS Bishop of Wings, 1; ANGELS 3-5, variety first ->
//   >= 1 cheap + >= 1 payoff -> 5-drops shed first -> measured value order.
//   KEEP:  white, white, Angel, Angel, Angel, 3rd source, Bishop, 4th source, 4th Angel, 5th Angel, 5th source
//   SHED:  Unexpectedly Absent, Swords -> duplicate legends -> surplus lands -> Lightning Greaves ->
//          surplus Bishop -> Angel overflow -> the ladder backwards -> Sol Ring -> Giada
// Every expected order below was derived BY HAND from that text plus the agent readings the
// implementation lists (each test names the reading it pins), not read off the code's output. Every
// board also checks the GENERIC fallback's index 0 -- the control arm, which must differ -- and that
// every non-staged index is named exactly once; the hatch case proves MTG_ANGELS_BUCKET_DISCARD=0
// restores exactly the generic ranking, and the stability case proves the order is prefix-stable.
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

void EnsureCardsAngels()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands that
// way so a policy reading the card's own masks would fail here too. Board permanents carry real data.
Card PhAngels(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeAngels(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsAngels();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhAngels(n, num++)); }
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
    return s;
}

std::vector<int> Ang(const GameState& s, int arm = 1)
{
    heurarm::t_arm[heurarm::ANGELS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = AngelsProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::ANGELS_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> GenAngels(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones land last, in the shared ranking's tier C).
void CheckPermutationAngels(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

std::vector<std::string> NamesAngels(const GameState& s, const std::vector<int>& order, std::size_t from = 0)
{
    std::vector<std::string> out;
    for (std::size_t k = from; k < order.size(); ++k)
    { out.push_back(s.players[0].hand[static_cast<std::size_t>(order[k])].m_name.str()); }
    return out;
}

// PREFIX STABILITY (DecisionProvider::CleanupDiscardShedStable defaults to true, so the batched cleanup
// reads several sheds off ONE consultation): shed index 0, re-rank the smaller hand, and the rest of
// the order must come back unchanged -- all the way down to one card.
void CheckPrefixStableAngels(GameState s)
{
    std::vector<int> order = Ang(s);
    while (s.players[0].hand.size() > 1)
    {
        const std::vector<std::string> rest = NamesAngels(s, order, 1);
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Ang(s);
        CHECK(NamesAngels(s, order) == rest);
    }
}

const char* kLyraDawn  = "Lyra, Archangel of Dawn";
const char* kGiada     = "Giada, Font of Hope";
const char* kSerra     = "Serra the Benevolent";
const char* kThune     = "Archangel of Thune";
const char* kDawnbr    = "Lyra Dawnbringer";
const char* kRV        = "Righteous Valkyrie";
const char* kYV        = "Youthful Valkyrie";
const char* kLI        = "Lightstall Inquisitor";
const char* kBishop    = "Bishop of Wings";
const char* kResp      = "Resplendent Angel";
const char* kUA        = "Unexpectedly Absent";
const char* kStP       = "Swords to Plowshares";
const char* kGreaves   = "Lightning Greaves";
const char* kSanct     = "Seraph Sanctuary";
const char* kFarm      = "Remote Farm";

GameState BoardT1()
{
    return MakeAngels({ kThune, kUA, kStP, kLI, kRV, "Plains", kBishop, kDawnbr },
                      { "Plains", "Plains", "Plains", "Plains" });
}

}   // namespace

TEST_CASE("Angels discard T1: Unexpectedly Absent then Swords shed first; the generic fallback pitches Thune")
{
    // Board 4 Plains: white and sources 1-4 are full. Angels (variety, 5-drops last, value):
    // Righteous Valkyrie, Lightstall, Thune, Dawnbringer -- the first three fill Angel x3, Dawnbringer
    // the 4th Angel; the Bishop slot takes the Bishop; the hand Plains is the 5th (soft) source.
    // Shed: UA, Swords, then the ladder backwards: Plains, Dawnbringer, Bishop, Thune, Lightstall, RV.
    const GameState s = BoardT1();
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 1, 2, 5, 7, 6, 0, 3, 4 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 0);   // generic max-MV sheds Archangel of Thune (MV 5)
}

TEST_CASE("Angels discard T2: duplicate legends (name in play, or a 2nd copy in hand) shed early; one Giada is protected to the very end")
{
    // Lyra ArchDawn in play -> the hand Lyra is dead; the 2nd Giada and the 2nd Serra are dead. The
    // first Giada counts toward the 4 sources (board 2 Plains + Giada = 3), so the Sanctuary is the
    // 4th source and the Plains the 5th -- "Sanctuary over a spare Plains" puts the Plains first.
    const GameState s = MakeAngels(
        { kLyraDawn, kGiada, kGiada, kSerra, kSerra, kResp, kSanct, "Plains" },
        { "Plains", "Plains", kLyraDawn });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 0, 2, 4, 7, 6, 3, 5, 1 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 3);   // generic sheds the first Serra (MV 4)
}

TEST_CASE("Angels discard T3: variety WINS over 5-drop-first -- the 2nd copies are the overflow, Thune and Dawnbringer stay")
{
    // Board 5 Plains (every source slot full). First copies: RV, Lightstall, Resplendent, then the
    // 5-drops Thune, Dawnbringer -> Angel x5. The repeats (RV, Lightstall, Resplendent) overflow and
    // shed worst first: Resplendent, Lightstall, RV.
    const GameState s = MakeAngels(
        { kRV, kRV, kLI, kLI, kThune, kDawnbr, kResp, kResp },
        { "Plains", "Plains", "Plains", "Plains", "Plains" });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 7, 3, 1, 5, 4, 6, 2, 0 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 4);   // generic sheds Thune
}

TEST_CASE("Angels discard T4: '>= 1 cheap and >= 1 payoff' outranks 5-drop-first -- Thune is the 2nd Angel kept")
{
    // Empty board. White: Plains, Plains (untapped first). Angels: Lightstall, then the payoff the
    // mix rule pulls up (Thune, the hand's only payoff), then Youthful Valkyrie. 3rd source: the
    // Sanctuary (over Remote Farm), Bishop, 4th source: Remote Farm. Nothing is surplus, so the shed
    // is the ladder backwards: Farm, Bishop, Sanctuary, Youthful, Thune, Lightstall, Plains, Plains.
    const GameState s = MakeAngels(
        { kLI, kYV, kThune, "Plains", "Plains", kBishop, kFarm, kSanct }, {});
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 6, 5, 7, 1, 2, 0, 4, 3 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 2);   // generic sheds Thune first
}

TEST_CASE("Angels discard T5: Sol Ring is protected outside the ladder and counts as a source; surplus lands go before Lightning Greaves")
{
    // Board 3 Plains + the hand Sol Ring = 4 sources (agent reading (d)). RV, Lightstall fill Angels;
    // Bishop; the 5th source is the Sanctuary (over a spare Plains). Shed: the two surplus Plains
    // (higher index first), Greaves, then Sanctuary, Bishop, Lightstall, RV -- and Sol Ring last.
    const GameState s = MakeAngels(
        { "Sol Ring", "Plains", "Plains", kLI, kBishop, kGreaves, kRV, kSanct },
        { "Plains", "Plains", "Plains" });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 2, 1, 5, 7, 4, 3, 6, 0 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 6);   // generic sheds Righteous Valkyrie (MV 3)
}

TEST_CASE("Angels discard T6: a Bishop in play makes the hand Bishop surplus; Greaves sheds before it; the mix orders the ladder")
{
    // Board 4 Plains + Bishop. Angels: RV, Lyra ArchDawn are both payoffs, so the mix pulls Lightstall
    // to 2nd: RV, Lightstall, Lyra | 4th Youthful, 5th Resplendent; the hand Plains is the 5th source.
    const GameState s = MakeAngels(
        { kBishop, kLI, kYV, kRV, kLyraDawn, kResp, "Plains", kGreaves },
        { "Plains", "Plains", "Plains", "Plains", kBishop });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 7, 0, 6, 5, 2, 4, 1, 3 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 3);   // generic sheds the first MV-3 card, Righteous Valkyrie

    // Without the Bishop in play the hand Bishop takes the ENABLERS slot (between the 3rd and 4th
    // source), so after Greaves the next shed is the Plains (5th source), and the Bishop outlives the
    // 4th and 5th Angels.
    const GameState t = MakeAngels(
        { kBishop, kLI, kYV, kRV, kLyraDawn, kResp, "Plains", kGreaves },
        { "Plains", "Plains", "Plains", "Plains" });
    CHECK(Ang(t) == std::vector<int>{ 7, 6, 5, 2, 0, 4, 1, 3 });
}

TEST_CASE("Angels discard T7: a Remote Farm in play is the USER's '2 white' -- the floor is full, so a Plains is the spare beside the Sanctuary")
{
    // Board Remote Farm + 2 Seraph Sanctuary: 3 sources, white 2 (agent reading (a)). No white slot
    // is open, so beyond the floor the hand Sanctuary is the 4th source and a Plains the 5th; the
    // other Plains is surplus. Angels: RV, Lightstall (the mix), Lyra | Resplendent, Serra.
    const GameState s = MakeAngels(
        { "Plains", "Plains", kSanct, kLI, kRV, kLyraDawn, kResp, kSerra },
        { kFarm, kSanct, kSanct });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 1, 0, 7, 6, 2, 5, 3, 4 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 7);   // generic sheds Serra (MV 4)
}

TEST_CASE("Angels discard T8: the white slots take an UNTAPPED white land first -- the Plains outlives the Remote Farm")
{
    // Empty board. White #1 = Plains (untapped), white #2 = Remote Farm; Angels RV, Lightstall,
    // Youthful; 3rd source Sanctuary; Bishop. UA sheds first, then the ladder backwards.
    const GameState s = MakeAngels(
        { kFarm, "Plains", kSanct, kLI, kYV, kBishop, kRV, kUA }, {});
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 7, 5, 2, 4, 3, 6, 0, 1 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 6);   // generic sheds Righteous Valkyrie
}

TEST_CASE("Angels discard T9: a Giada in play is a source (not white) and makes the hand Giada a duplicate")
{
    // Board 3 Plains + Giada = 4 sources, white 3. The hand Giada is dead (shed first); the 5th
    // source is the Sanctuary, so the hand Plains is surplus. Angels: RV, Lightstall (mix), Lyra,
    // Resplendent; the Bishop sits between Angel #3 and Angel #4.
    const GameState s = MakeAngels(
        { kGiada, "Plains", kSanct, kLI, kRV, kLyraDawn, kResp, kBishop },
        { "Plains", "Plains", "Plains", kGiada });
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 0, 1, 2, 6, 7, 5, 3, 4 });
    CheckPermutationAngels(s, o);
    CHECK(GenAngels(s).front() == 4);   // generic sheds Righteous Valkyrie
}

TEST_CASE("Angels discard: MTG_ANGELS_BUCKET_DISCARD=0 restores exactly the generic ranking; the default is the policy")
{
    const GameState s = BoardT1();
    CHECK(Ang(s, 0) == GenAngels(s));
    CHECK(Ang(s, 0) != Ang(s, 1));
    CHECK(AngelsProvider().CleanupDiscardCandidates(s, nullptr) == Ang(s, 1));   // the default (arm unset)
}

TEST_CASE("Angels discard: a staged card is never in the policy's order and lands last")
{
    GameState s = MakeAngels(
        { "Sol Ring", "Plains", "Plains", kLI, kBishop, kGreaves, kRV, kSanct, kThune },
        { "Plains", "Plains", "Plains" });
    s.players[0].hand[8].m_is_staged = true;
    const std::vector<int> o = Ang(s);
    CHECK(o == std::vector<int>{ 2, 1, 5, 7, 4, 3, 6, 0, 8 });   // T5's order, then the staged Thune
    CheckPermutationAngels(s, o);
}

TEST_CASE("Angels discard: the shed order is prefix-stable shed by shed")
{
    CheckPrefixStableAngels(BoardT1());
    CheckPrefixStableAngels(MakeAngels({ kLyraDawn, kGiada, kGiada, kSerra, kSerra, kResp, kSanct, "Plains" },
                                       { "Plains", "Plains", kLyraDawn }));
    CheckPrefixStableAngels(MakeAngels({ kRV, kRV, kLI, kLI, kThune, kDawnbr, kResp, kResp },
                                       { "Plains", "Plains", "Plains", "Plains", "Plains" }));
    CheckPrefixStableAngels(MakeAngels({ kLI, kYV, kThune, "Plains", "Plains", kBishop, kFarm, kSanct }, {}));
    CheckPrefixStableAngels(MakeAngels({ "Sol Ring", "Plains", "Plains", kLI, kBishop, kGreaves, kRV, kSanct },
                                       { "Plains", "Plains", "Plains" }));
    CheckPrefixStableAngels(MakeAngels({ "Plains", "Plains", kSanct, kLI, kRV, kLyraDawn, kResp, kSerra },
                                       { kFarm, kSanct, kSanct }));
    CheckPrefixStableAngels(MakeAngels({ kFarm, "Plains", kSanct, kLI, kYV, kBishop, kRV, kUA, kStP, kGiada, "Plains" }, {}));
}
