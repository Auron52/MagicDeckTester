// Unit tests for the Anti-Lifegain tutor narrowing: AntiLifegainProvider::TutorCandidates (USER
// 2026-10-10: "we should have a narrowing tutor for anti-lifegain"). Idyllic Tutor / Enlightened Tutor
// fetch ONE card -- the lifegain->loss enabler (Tainted Remedy) while no enabler is active or castable
// from hand, else the cast payoff (Aria of Flame) -- both when the only enabler held is unaffordable,
// and every legal target when the zone holds neither. MTG_ANTILIFE_TUTOR_BRANCH (default OFF) is the
// proof arm: the enabler AND the payoff, ranked. The body moved from the free TutorCandidates in
// src/core/SpellEffects.h (docs/design/antilifegain-tutor-single-pick-defect.md); these cases pin it.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsAt()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Library / hand cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhAt(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeAt(const std::vector<std::string>& library, const std::vector<std::string>& board,
                 const std::vector<std::string>& hand = {})
{
    EnsureCardsAt();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    int num = 1;
    for (const std::string& n : library) { s.players[0].library.push_back(PhAt(n, num++)); }
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhAt(n, num++)); }
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

const CardParams& TutorParams(const char* name)
{
    EnsureCardsAt();
    return CardDatabase::Instance().Lookup(name)->params;
}

// arm: -1 = the env default (OFF), 0 / 1 = MTG_ANTILIFE_TUTOR_BRANCH pinned.
std::vector<std::string> At(const GameState& s, const char* tutor, int arm = -1)
{
    heurarm::t_arm[heurarm::ANTILIFE_TUTOR_BRANCH] = static_cast<std::int8_t>(arm);
    std::vector<std::string> v = AntiLifegainProvider().TutorCandidates(s, 0, TutorParams(tutor));
    heurarm::t_arm[heurarm::ANTILIFE_TUTOR_BRANCH] = -1;
    return v;
}

using Names = std::vector<std::string>;
const char* kIdyllic = "Idyllic Tutor";
const char* kEnl     = "Enlightened Tutor";
const char* kRemedy  = "Tainted Remedy";
const char* kAria    = "Aria of Flame";
const char* kDrone   = "Plague Drone";   // {3}{B} creature enabler (not tutorable)
const char* kDS      = "Doubling Season";
const char* kSol     = "Sol Ring";
const char* kGodless = "Godless Shrine";

}   // namespace

TEST_CASE("Anti-Lifegain tutor A1: no enabler anywhere -> the enabler alone (proof arm: enabler, payoff)")
{
    const GameState s = MakeAt({ kAria, kDS, kRemedy }, { kGodless });
    CHECK(At(s, kIdyllic)    == Names{ kRemedy });
    CHECK(At(s, kIdyllic, 0) == Names{ kRemedy });
    CHECK(At(s, kIdyllic, 1) == Names{ kRemedy, kAria });   // Doubling Season is never offered
}

TEST_CASE("Anti-Lifegain tutor A2: an enabler in play -> the payoff alone (proof arm: payoff, enabler)")
{
    const GameState s = MakeAt({ kRemedy, kAria }, { kGodless, kRemedy });
    CHECK(At(s, kIdyllic)    == Names{ kAria });
    CHECK(At(s, kIdyllic, 1) == Names{ kAria, kRemedy });
}

TEST_CASE("Anti-Lifegain tutor A3: an enabler in hand we can pay for counts as ready")
{
    // Plague Drone is {3}{B}: four sources in play pay for it.
    const GameState s = MakeAt({ kRemedy, kAria }, { kGodless, kGodless, kGodless, kGodless }, { kDrone });
    CHECK(At(s, kIdyllic) == Names{ kAria });
}

TEST_CASE("Anti-Lifegain tutor A4: the only enabler held is unaffordable -> both, enabler first")
{
    const GameState s = MakeAt({ kAria, kRemedy }, { kGodless }, { kDrone });
    CHECK(At(s, kIdyllic)    == Names{ kRemedy, kAria });
    CHECK(At(s, kIdyllic, 1) == Names{ kRemedy, kAria });
}

TEST_CASE("Anti-Lifegain tutor A5: each falls back to the other")
{
    const GameState no_remedy = MakeAt({ kAria, kDS }, { kGodless });
    CHECK(At(no_remedy, kIdyllic)    == Names{ kAria });
    CHECK(At(no_remedy, kIdyllic, 1) == Names{ kAria });
    const GameState no_aria = MakeAt({ kRemedy, kDS }, { kGodless, kRemedy });
    CHECK(At(no_aria, kIdyllic)      == Names{ kRemedy });
}

TEST_CASE("Anti-Lifegain tutor A6: Enlightened Tutor -- an artifact is pruned while an enabler or payoff exists")
{
    const GameState s = MakeAt({ kSol, kAria, kRemedy }, { kGodless });
    CHECK(At(s, kEnl)    == Names{ kRemedy });
    CHECK(At(s, kEnl, 1) == Names{ kRemedy, kAria });
}

TEST_CASE("Anti-Lifegain tutor A7: neither in the zone -> every legal target (no shuffle-order pick)")
{
    // The old free function took the FIRST legal card in library order (Sol Ring here); the zone now
    // goes to the search whole, exactly the generic list.
    const GameState s = MakeAt({ kSol, kDS }, { kGodless });
    const Names generic = GenericProvider().TutorCandidates(s, 0, TutorParams(kEnl));
    CHECK(generic == Names{ kSol, kDS });
    CHECK(At(s, kEnl)    == generic);
    CHECK(At(s, kEnl, 1) == generic);
    CHECK(At(s, kIdyllic) == Names{ kDS });   // Idyllic sees enchantments only
}
