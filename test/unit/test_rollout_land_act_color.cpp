// The rollout's greedy land drop (SimulateLandPlay, reached through ApplyPlan on a plan whose land
// was not searched) supplies a colour that a permanent IN PLAY needs for an activated ability and
// that nothing on the board can make -- MTG_ROLLOUT_LAND_ACT_COLOR, default ON.
//
// The defect (docs/design/discard-round-play-defects-2026-10-10.md section 6): EldraziDisplacerFlicker
// seed 217164 gi 63, the playout of the turn-3 line "Fertile Ground + Cloud of Faeries" -- Essence
// Depleter ({1}{C} drain) in play, no land making {C}, and the playout played an enters-tapped
// Conservatory on turns 7 and 8 over the Mariposa Military Base in hand, because its only rule was
// "multi-colour first". The drain never fired and the line scored no win.
//
// Every board below names what the rule must do AND checks the =0 arm, so a board where the two arms
// agree is a byte-identity case and a board where they differ is the rule firing.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLac()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card Ph(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// One board permanent; `host` attaches it (an Aura) to the board permanent at that index.
struct Bp { std::string name; int host = -1; };

GameState Make(const std::vector<std::string>& hand, const std::vector<Bp>& board)
{
    EnsureCardsLac();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 7;
    s.deck_has_land_aura  = true;
    int num = 1;
    for (const std::string& nm : hand) { s.players[0].hand.push_back(Ph(nm, num++)); }
    std::vector<int> numbers;
    for (const Bp& b : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(b.name)->card;
        p.card.m_number     = 100 + num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (b.host >= 0) { p.aura_attached_to = numbers[static_cast<std::size_t>(b.host)]; }
        numbers.push_back(p.card.m_number);
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 0;
    return s;
}

// Apply an UNSEARCHED pre-combat plan (land_decided == false, no actions): exactly the rollout's
// greedy land drop. Returns the name of the land that left the hand ("" = none). `arm` pins the
// lever (-1 = the env default, i.e. ON), `ranker` pins MTG_ROLLOUT_LAND_RANKER.
std::string Drop(const GameState& s0, int arm, int ranker = -1)
{
    GameState s = s0;
    heurarm::t_arm[heurarm::ROLLOUT_LAND_ACT_COLOR] = static_cast<std::int8_t>(arm);
    heurarm::t_arm[heurarm::ROLLOUT_LAND_RANKER]    = static_cast<std::int8_t>(ranker);
    TurnSolver::Plan plan;   // land_decided == false
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    heurarm::Clear();
    std::string played;
    for (const Card& c : s0.players[0].hand)
    {
        bool still = false;
        for (const Card& h : s.players[0].hand) { if (h.m_number == c.m_number) { still = true; } }
        if (!still) { played = c.m_name.str(); }
    }
    return played;
}

}   // namespace

TEST_CASE("rollout land drop: Essence Depleter's {C} with no {C} source plays Mariposa over the multi-colour Conservatory")
{
    // The gi 63 shape: board lands make G/W and G/U only; Depleter's drain needs {C}.
    const GameState s = Make({ "Conservatory", "Mariposa Military Base" },
                             { {"Conservatory"}, {"Kitchen"}, {"Essence Depleter"} });
    CHECK(UnmetActivationColorMask(s) == (1 << static_cast<int>(Color::Colorless)));
    CHECK(Drop(s, -1) == "Mariposa Military Base");   // default ON
    CHECK(Drop(s, 1)  == "Mariposa Military Base");
    CHECK(Drop(s, 0)  == "Conservatory");             // =0: today's multi-colour-first pick
}

TEST_CASE("rollout land drop: the need already met by a {C} land in play changes nothing")
{
    // Brushland makes {C}: no unmet need, so both arms take the legacy multi-colour pick.
    const GameState s = Make({ "Conservatory", "Mariposa Military Base" },
                             { {"Brushland"}, {"Kitchen"}, {"Essence Depleter"} });
    CHECK(UnmetActivationColorMask(s) == 0);
    CHECK(Drop(s, 1) == "Conservatory");
    CHECK(Drop(s, 0) == "Conservatory");
}

TEST_CASE("rollout land drop: no activated ability in play changes nothing")
{
    const GameState s = Make({ "Conservatory", "Mariposa Military Base" },
                             { {"Conservatory"}, {"Kitchen"} });
    CHECK(UnmetActivationColorMask(s) == 0);
    CHECK(Drop(s, 1) == "Conservatory");
    CHECK(Drop(s, 0) == "Conservatory");
}

TEST_CASE("rollout land drop: Training Grounds leaves the {C} pip and Fertile Ground cannot pay it")
{
    // Training Grounds turns Displacer's {2}{C} into {C} -- the generic half goes, the pip stays.
    // Fertile Ground adds "one mana of any COLOUR", which is never {C} (CR 107.4c).
    const GameState s = Make({ "Kitchen", "Mariposa Military Base" },
                             { {"Conservatory"}, {"Fertile Ground", 0}, {"Training Grounds"},
                               {"Eldrazi Displacer"} });
    CHECK(UnmetActivationColorMask(s) == (1 << static_cast<int>(Color::Colorless)));
    CHECK(Drop(s, 1) == "Mariposa Military Base");
    CHECK(Drop(s, 0) == "Kitchen");
}

TEST_CASE("rollout land drop: a COLOURED pip -- Shivan Gorge's {2}{R} with no red source plays the Mountain")
{
    const GameState s = Make({ "Conservatory", "Mountain" },
                             { {"Conservatory"}, {"Shivan Gorge"} });
    CHECK(UnmetActivationColorMask(s) == (1 << static_cast<int>(Color::Red)));
    CHECK(Drop(s, 1) == "Mountain");
    CHECK(Drop(s, 0) == "Conservatory");

    // ...but a Fertile Ground in play makes any colour, red included: the need is met.
    const GameState t = Make({ "Conservatory", "Mountain" },
                             { {"Conservatory"}, {"Fertile Ground", 0}, {"Shivan Gorge"} });
    CHECK(UnmetActivationColorMask(t) == 0);
    CHECK(Drop(t, 1) == "Conservatory");
}

TEST_CASE("rollout land drop: a Phyrexian pip is not a hard need (Birthing Pod's {G/P} pays with life)")
{
    // Legacy pick on two mono-colour lands is the first in hand order (Plains). Were {G/P} read as a
    // hard {G} the rule would play the Forest; it must not.
    const GameState s = Make({ "Plains", "Forest" }, { {"Plains"}, {"Birthing Pod"} });
    CHECK(UnmetActivationColorMask(s) == 0);
    CHECK(Drop(s, 1) == "Plains");
    CHECK(Drop(s, 0) == "Plains");
}

TEST_CASE("rollout land drop: under MTG_ROLLOUT_LAND_RANKER the shared ranker's pick is replaced only when it misses the need")
{
    // Two untapped mono lands: the four-pass ranker (and the legacy loop) take the Forest, first in
    // hand order. Depleter needs {C}: the rule takes Mariposa in both rollout arms.
    const GameState s = Make({ "Forest", "Mariposa Military Base" },
                             { {"Forest"}, {"Essence Depleter"} });
    CHECK(Drop(s, 1, /*ranker=*/1) == "Mariposa Military Base");
    CHECK(Drop(s, 0, /*ranker=*/1) == "Forest");
    CHECK(Drop(s, 1, /*ranker=*/0) == "Mariposa Military Base");
    CHECK(Drop(s, 0, /*ranker=*/0) == "Forest");

    // The ranker's own pick already supplies {C} (an untapped Brushland ahead of the tapped
    // Conservatory): kept as is, even though the rule's two-pass order alone would also take it.
    const GameState t = Make({ "Conservatory", "Brushland", "Mariposa Military Base" },
                             { {"Forest"}, {"Essence Depleter"} });
    CHECK(Drop(t, 1, /*ranker=*/1) == "Brushland");
    CHECK(Drop(t, 0, /*ranker=*/1) == "Brushland");
    // Legacy two-pass: Conservatory (first multi) misses {C}; the rule takes Brushland (multi, {C}).
    CHECK(Drop(t, 1, /*ranker=*/0) == "Brushland");
    CHECK(Drop(t, 0, /*ranker=*/0) == "Conservatory");
}
