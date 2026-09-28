// Unit tests for the Prevent Damage cleanup-discard BUCKET policy (analyze-deck 5i, 2026-09-28):
// PreventDamageProvider::CleanupDiscardCandidates, implementing
// docs/design/prevent-damage-discard-policy-proposal.md. Boards T1-T11 are the proposal's section 10
// table, verbatim; each asserts the full shed vector where the proposal derives one, otherwise index
// 0 plus "a permutation of every non-staged index" (no fall-through to max-MV). Every board also
// asserts the GENERIC fallback's index 0 -- the control arm that MUST differ -- and T10 proves the
// MTG_PD_BUCKET_DISCARD hatch restores exactly the generic ranking.
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

void EnsureCardsPdd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands,
// libraries and the sideboard that way so a policy reading the card's own masks would fail here too.
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const std::vector<std::string> kSideboard = {
    "Rhox Faithmender", "Rhox Faithmender", "Tamanoa", "Purity", "Brushland", "Battlefield Forge",
    "Dimir House Guard", "Shriekmaw", "Vexing Shusher", "Vito, Thorn of the Dusk Rose",
    "Timeless Witness", "Bilbo, Birthday Celebrant", "Dina, Soul Steeper", "Dina, Soul Steeper",
    "Acidic Slime",
};

std::vector<std::string> DefaultLibrary()
{
    std::vector<std::string> lib = { "Tamanoa", "Dina, Soul Steeper", "Vito, Thorn of the Dusk Rose",
                                     "Rhox Faithmender", "Rolling Earthquake" };
    const char* lands[] = { "City of Brass", "Tarnished Citadel", "Grand Coliseum", "Ancient Tomb",
                            "Battlefield Forge" };
    for (int k = 0; k < 15; ++k) { lib.push_back(lands[k % 5]); }
    return lib;
}

GameState MakePd(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                 const std::vector<std::string>& library = DefaultLibrary(),
                 const std::vector<std::string>& opp_board = {})
{
    EnsureCardsPdd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand)      { s.players[0].hand.push_back(Ph(n, num++)); }
    for (const std::string& n : library)   { s.players[0].library.push_back(Ph(n, num++)); }
    for (const std::string& n : kSideboard) { s.players[0].sideboard.push_back(Ph(n, num++)); }
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
    return s;
}

std::vector<int> Pd(const GameState& s)  { return PreventDamageProvider().CleanupDiscardCandidates(s, nullptr); }
std::vector<int> Gen(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every non-staged hand index exactly once, staged ones last (tier C of the shared ranking).
void CheckPermutation(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kVito = "Vito, Thorn of the Dusk Rose";
const char* kDina = "Dina, Soul Steeper";
const char* kBilbo = "Bilbo, Birthday Celebrant";

}   // namespace

TEST_CASE("PD discard T1: fuel overflow -- Pyrohemia is the lone overflow, Beseech is kept")
{
    const GameState s = MakePd(
        { kVito, "Manabarbs", "Spellshock", "Pyrohemia", "Beseech the Queen", "Tarnished Citadel",
          "Reflecting Pool", "Rhox Faithmender" },
        { "City of Brass", "Battlefield Forge", "Ancient Tomb", "Tamanoa" });
    CHECK(Pd(s) == std::vector<int>{ 3, 2, 7, 1, 4, 0, 6, 5 });
    CHECK(Gen(s).front() == 4);   // generic max-MV sheds Beseech (MV 6)
}

TEST_CASE("PD discard T2: a legend already on our battlefield is dead (S0)")
{
    const GameState s = MakePd(
        { "Rolling Earthquake", kVito, kDina, "Green Sun's Zenith", "Living Wish", "Tarnished Citadel",
          "Manabarbs", "Rhox Faithmender" },
        { "City of Brass", "Grand Coliseum", "Karplusan Forest", "Brushland", kVito, "Tamanoa" });
    const std::vector<int> o = Pd(s);
    CHECK(o.front() == 1);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 6);
}

TEST_CASE("PD discard T3: flood -- every hand land is surplus, Pool first")
{
    const GameState s = MakePd(
        { "Grand Coliseum", "Reflecting Pool", "Karplusan Forest", "City of Brass", kVito, "Spellshock",
          "Beseech the Queen", "Living Wish" },
        { "City of Brass", "Ancient Tomb", "Tarnished Citadel", "Brushland", "Battlefield Forge",
          "Tamanoa", kDina });
    CHECK(Pd(s) == std::vector<int>{ 1, 0, 2, 3, 6, 5, 7, 4 });
    CHECK(Gen(s).front() == 6);
}

TEST_CASE("PD discard T4: no engine in hand -- tutors fill GAIN1 / DRAIN1 as wildcards")
{
    const GameState s = MakePd(
        { "Beseech the Queen", "Green Sun's Zenith", "Manabarbs", "Spellshock", "Pyrohemia",
          "Rolling Earthquake", "Grand Coliseum", "Battlefield Forge" },
        { "Ancient Tomb", "City of Brass" });
    CHECK(Pd(s) == std::vector<int>{ 4, 3, 2, 5, 0, 6, 1, 7 });
    CHECK(Gen(s).front() == 0);
}

TEST_CASE("PD discard T5: a Reflecting Pool with no non-reflecting land anywhere is dead")
{
    const GameState s = MakePd(
        { "Reflecting Pool", "Tamanoa", kVito, kDina, "Living Wish", "Spellshock", "Rolling Earthquake",
          "Beseech the Queen" },
        { "Reflecting Pool" });
    const std::vector<int> o = Pd(s);
    CHECK(o.front() == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 7);
}

TEST_CASE("PD discard T6: the same-name drainer backup sheds before the AMP overflow")
{
    const GameState s = MakePd(
        { kVito, kVito, "Rhox Faithmender", kBilbo, "Rolling Earthquake", "Living Wish",
          "Karplusan Forest", "Spellshock" },
        { "City of Brass", "Tarnished Citadel", "Ancient Tomb", "Battlefield Forge", "Tamanoa", kDina });
    CHECK(Pd(s) == std::vector<int>{ 1, 3, 7, 2, 4, 5, 0, 6 });
    CHECK(Gen(s).front() == 2);
}

TEST_CASE("PD discard T7: Acidic Slime is NEG (S1) -- its ETB can only hit our side")
{
    const GameState s = MakePd(
        { "Acidic Slime", kVito, "Manabarbs", "Grand Coliseum", "Living Wish", "Beseech the Queen",
          "Rolling Earthquake", "Rhox Faithmender" },
        { "City of Brass", "Brushland", "Ancient Tomb", "Tamanoa" });
    const std::vector<int> o = Pd(s);
    CHECK(o.front() == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 5);
}

TEST_CASE("PD discard T8: colour cover before mana amount -- Karplusan (G) then Tomb, Brushland surplus")
{
    const GameState s = MakePd(
        { "Tamanoa", kDina, kVito, "Karplusan Forest", "Brushland", "Ancient Tomb", "Spellshock",
          "Manabarbs" },
        { "Ancient Tomb", "Battlefield Forge", "Reflecting Pool" });
    CHECK(Pd(s) == std::vector<int>{ 4, 6, 7, 1, 2, 5, 0, 3 });
    CHECK(Gen(s).front() == 7);
}

TEST_CASE("PD discard T9: a tutor with no legal target is dead (S0)")
{
    std::vector<std::string> lands_only;
    const char* lands[] = { "City of Brass", "Tarnished Citadel", "Grand Coliseum", "Ancient Tomb" };
    for (int k = 0; k < 20; ++k) { lands_only.push_back(lands[k % 4]); }
    const GameState s = MakePd(
        { "Green Sun's Zenith", kVito, "Tamanoa", "Manabarbs", "Spellshock", "Living Wish",
          "Karplusan Forest", "Rolling Earthquake" },
        { "City of Brass", "Tarnished Citadel", "Grand Coliseum", "Ancient Tomb", "Tamanoa", "Tamanoa",
          kDina },
        lands_only);
    const std::vector<int> o = Pd(s);
    CHECK(o.front() == 0);
    CheckPermutation(s, o);
    CHECK(Gen(s).front() == 3);
}

TEST_CASE("PD discard T10: MTG_PD_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakePd(
        { kVito, "Manabarbs", "Spellshock", "Pyrohemia", "Beseech the Queen", "Tarnished Citadel",
          "Reflecting Pool", "Rhox Faithmender" },
        { "City of Brass", "Battlefield Forge", "Ancient Tomb", "Tamanoa" });
    heurarm::t_arm[heurarm::PD_BUCKET_DISCARD] = 0;
    const std::vector<int> off = Pd(s);
    heurarm::t_arm[heurarm::PD_BUCKET_DISCARD] = 1;
    const std::vector<int> on = Pd(s);
    heurarm::t_arm[heurarm::PD_BUCKET_DISCARD] = -1;
    CHECK(off == Gen(s));
    CHECK(off.front() == 4);        // the old max-MV answer: Beseech
    CHECK(on.front() == 3);         // the policy: Pyrohemia
    CHECK(on != off);               // the control that MUST differ
}

TEST_CASE("PD discard T11: a staged card is never named by the policy and sorts last")
{
    GameState s = MakePd(
        { kVito, "Manabarbs", "Spellshock", "Pyrohemia", "Beseech the Queen", "Tarnished Citadel",
          "Reflecting Pool", "Rhox Faithmender" },
        { "City of Brass", "Battlefield Forge", "Ancient Tomb", "Tamanoa" });
    s.players[0].hand[4].m_is_staged = true;
    const std::vector<int> o = Pd(s);
    REQUIRE(o.size() == 8);
    CHECK(o.back() == 4);
    CHECK(std::find(o.begin(), o.end() - 1, 4) == o.end() - 1);
    CHECK(o.front() == 3);
}

TEST_CASE("PD discard: Shriekmaw is NEG only while the opponent has no creature")
{
    const std::vector<std::string> hand = { "Shriekmaw", kVito, "Manabarbs", "Grand Coliseum",
                                            "Living Wish", "Beseech the Queen", "Rolling Earthquake",
                                            "Rhox Faithmender" };
    const std::vector<std::string> board = { "City of Brass", "Brushland", "Ancient Tomb", "Tamanoa" };
    const GameState empty_opp = MakePd(hand, board);
    CHECK(Pd(empty_opp).front() == 0);
    // With an opposing creature Shriekmaw has a real target: it is OTHER (S3), still unprotected,
    // but no longer NEG. Here it remains the first card shed (no S0/S1/S2 card in this hand).
    const GameState with_opp = MakePd(hand, board, DefaultLibrary(), { "Tamanoa" });
    const std::vector<int> o = Pd(with_opp);
    CheckPermutation(with_opp, o);
    CHECK(o.front() == 0);
}
