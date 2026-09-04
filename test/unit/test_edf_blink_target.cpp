// Unit tests for EldraziFlickerProvider::BlinkTargetCandidates -- the blink-target heuristic the
// user stated on 2026-09-04:
//
//   "In the search Drake should always be targeted if available."
//   ... clarified: "we should target Cloud of Faeries if Drake is not there, it is just that the
//   drake has the strictly better ETB ability."
//
// The rule is therefore BEST UNTAPPER, not Drake, and the case that separates the two readings is
// a board holding a Cloud of Faeries and NO Drake: a name-shaped implementation offers nothing
// there (or falls back to the attacker), while the params-shaped one offers the Cloud. That case
// is the reason this file exists -- the Drake-present case passes under either reading, so testing
// only it would have been vacuous.
//
// A unit test rather than a scenario fixture because what is under test is the CANDIDATE SET, not
// an outcome. Blinking a Cloud of Faeries is mana-NEGATIVE on its own ({3} for two untaps), so no
// reachable win turn or life total distinguishes "the Cloud was offered" from "the Cloud was
// offered and correctly declined" -- an outcome fixture would be silent on exactly the thing the
// user asked for. Here the set is read directly.
//
// The provider is called on a hand-built battlefield. That is faithful: BlinkTargetCandidates
// reads only s.battlefield, the source permanent, and card params -- no zones, no mana, no turn
// state -- so a constructed board exercises the same code the search does.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Distinct m_number per copy: the candidate list IS a list of m_numbers, and the source is excluded
// by number, so colliding ids would make the test lie.
int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return number;
}

// Emiel is the outlet in every case here: its blink is {3} with no {T}, it is blink_own_only, and
// it is itself the +1/+1 counter watcher -- which is what opens the `best_attacker` branch. Using
// Emiel therefore tests the untapper preference against a LIVE alternative rather than an empty
// one, which is the only way the preference can be shown to bite.
const Permanent& Emiel(const GameState& s) { return s.battlefield.front(); }

std::vector<int> Targets(const GameState& s)
{
    EldraziFlickerProvider prov;
    return prov.BlinkTargetCandidates(s, Emiel(s));
}

}   // namespace

TEST_CASE("EDF blink target: Drake outranks Cloud of Faeries when both are on the board")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Emiel the Blessed", 0, 100);        // the outlet (and the counter watcher)
    const int drake = Put(s, "Peregrine Drake", 0, 101);   // untaps 5
    Put(s, "Cloud of Faeries", 0, 102);                    // untaps 2
    Put(s, "Eldrazi Displacer", 0, 103);                   // 3/3 -- the best attacker

    // Only the Drake. Not the Cloud (a worse untapper), not the Displacer (the biggest body).
    CHECK(Targets(s) == std::vector<int>{drake});
}

TEST_CASE("EDF blink target: Cloud of Faeries IS the target when no Drake is available")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Emiel the Blessed", 0, 100);
    const int cloud = Put(s, "Cloud of Faeries", 0, 102);
    Put(s, "Eldrazi Displacer", 0, 103);       // 3/3, strictly bigger than the 1/1 Cloud

    // The user's clarification. The Cloud is a WORSE untapper and a WORSE attacker than the
    // Displacer sharing the board with it, and it is still the pick -- because untapping is the
    // only ETB that pays anything back.
    CHECK(Targets(s) == std::vector<int>{cloud});
}

TEST_CASE("EDF blink target: with no untapper at all the attacker branch is still offered")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Emiel the Blessed", 0, 100);
    const int displacer = Put(s, "Eldrazi Displacer", 0, 103);   // 3/3
    Put(s, "Essence Depleter", 0, 104);                          // 2/3

    // The preference SUPPRESSES the attacker, it does not delete it: with nothing to untap, growing
    // the biggest body is the only blink worth proposing and it must still be reachable.
    CHECK(Targets(s) == std::vector<int>{displacer});
}

TEST_CASE("EDF blink target: blink_own_only excludes the opponent's untapper")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Emiel the Blessed", 0, 100);
    Put(s, "Peregrine Drake", 1, 201);          // THEIRS -- Emiel is blink_own_only
    const int cloud = Put(s, "Cloud of Faeries", 0, 102);

    // Guards the interaction between the two rules: "best untapper" must be read within the legal
    // target set, or the preference would name a Drake that cannot be targeted and suppress the
    // attacker branch in favour of nothing.
    CHECK(Targets(s) == std::vector<int>{cloud});
}

// The blink-target preference and the go-off recognizer must AGREE about which untapper is the
// payload, because BlinkActivationCounts only proposes the go-off iteration count when
// `loop.payload_id == target` -- and the target is whatever BlinkTargetCandidates offered.
//
// They did not agree, and the disagreement was INSERTION-ORDER DEPENDENT. FlickerTopLandYields sums
// the top-N land yields, so the refund SATURATES at the land count: on two lands a Drake (untap 5)
// and a Cloud (untap 2) refund the same and tie on net. RecogniseFlickerLoop kept the first-seen of
// an equal-net tie, so with the Cloud earlier on the battlefield it named the Cloud as payload while
// the target preference offered only the Drake -- and the go-off count was then offered for
// neither. Measured 2026-09-04 on the board below: swapping ONLY the order of the two creatures
// moved the offered counts from `1 2 3` to `1 2 3 40`, i.e. the deck's kill was reachable or not
// according to which untapper happened to be added first.
//
// Both orders are asserted deliberately. A single-order test would have PASSED throughout the bug.
namespace
{

// Two lands, one of them enchanted (Forest 1 + Overgrowth 2, Forest 1 = 4 mana). Two is fewer than
// the Drake's five, which is what makes the refund saturate and the tie exist at all; 4 > Emiel's
// {3} is what keeps net positive so a loop is recognised. Essence Depleter is the {T}-less drain
// sink -- without a sink the go-off count is 0 for a legitimate reason and the test would be blind.
void BuildSaturatedRefundBoard(GameState& s, bool drake_first)
{
    s.active_player_index = 0;
    s.players[1].life     = 20;

    Put(s, "Emiel the Blessed", 0, 100);
    if (drake_first) { Put(s, "Peregrine Drake", 0, 101); Put(s, "Cloud of Faeries", 0, 102); }
    else             { Put(s, "Cloud of Faeries", 0, 102); Put(s, "Peregrine Drake", 0, 101); }
    Put(s, "Essence Depleter", 0, 104);
    Put(s, "Forest", 0, 300);
    Put(s, "Overgrowth", 0, 301);
    s.battlefield.back().aura_attached_to = 300;
    Put(s, "Forest", 0, 302);
}

// The counts BlinkActivationCounts offers for the target the preference actually picked.
std::vector<int> CountsForPreferredTarget(const GameState& s)
{
    EldraziFlickerProvider prov;
    const std::vector<int> tg = prov.BlinkTargetCandidates(s, Emiel(s));
    REQUIRE(tg.size() == 1);
    const Permanent* tp = nullptr;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_number == tg[0]) { tp = &p; } }
    REQUIRE(tp != nullptr);
    return prov.BlinkActivationCounts(s, Emiel(s), *tp, /*max_affordable=*/40);
}

}   // namespace

TEST_CASE("EDF go-off count survives a saturated-refund tie, in EITHER battlefield order")
{
    EnsureCardsLoaded();

    for (const bool drake_first : {true, false})
    {
        CAPTURE(drake_first);
        GameState s;
        BuildSaturatedRefundBoard(s, drake_first);

        const std::vector<int> counts = CountsForPreferredTarget(s);
        // The generic small counts are always there; what the tie used to eat is the ONE proposed
        // "finish it" count above them, which is the only way ~20 iterations is ever reachable.
        REQUIRE(counts.size() >= 4);
        CHECK(counts.back() > 3);
    }
}
