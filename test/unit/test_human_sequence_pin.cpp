// Unit tests for THE HUMAN'S WHOLE SEQUENCE (the viewer's "**" --cast-order list,
// TurnSolver::ApplyHumanSequencePin / EraseHumanSequenceAction), 2026-10-22. USER: "The viewer should
// use exactly the order I give. Same for everything else in the viewer."
//
// The "*" list sequenced casts, Vial puts and board activations only, and the viewer sent it only when
// the matched plan's actions were exactly the queued names. ApplyHumanSequencePin turns the committing
// segment -- one `<verb>=<name>` token per queued step, the land and each Land's Edge discard included,
// manual taps in place -- into a plan whose actions[0, human_seq_len) are the steps in queue order, with
// the land's step and every tap's position counted in those steps. ApplyPlanDirect then walks it
// (human_v2). That walk is gated on HumanPlayActive(), a process-static a unit test cannot switch on, so
// the apply half is pinned by the scenarios (test/scenarios/*_human_seq_*.json); THIS file pins the pure
// half: the matching, the layout, the implicit-action rule and the position bookkeeping.
#include <doctest/doctest.h>

#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "core/Card.h"

#include <string>
#include <vector>

namespace
{

Action Act(Action::Kind k, const std::string& name)
{
    Action a;
    a.kind      = k;
    a.card_name = name;
    return a;
}

TurnSolver::Plan PlanOf(const std::string& land, std::vector<Action> acts)
{
    TurnSolver::Plan p;
    p.land_decided = !land.empty();
    p.land_to_play = land;
    p.actions      = std::move(acts);
    return p;
}

std::vector<std::string> Names(const TurnSolver::Plan& p)
{
    std::vector<std::string> out;
    for (const Action& a : p.actions) { out.push_back(a.card_name.str()); }
    return out;
}

using K = Action::Kind;

}   // namespace

TEST_CASE("human sequence: a land queued after a cast is a step after it (cast Sol Ring, then the Forest)")
{
    TurnSolver::Plan p = PlanOf("Forest", { Act(K::CastFromHand, "Sol Ring") });
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Sol Ring", "land=Forest" });
    CHECK(p.human_sequence);
    CHECK(p.human_action_order);
    CHECK(p.searched_order);
    CHECK(p.human_seq_len == 1);
    CHECK(p.human_land_step == 1);
    CHECK(Names(p) == std::vector<std::string>{ "Sol Ring" });

    TurnSolver::Plan q = PlanOf("Forest", { Act(K::CastFromHand, "Sol Ring") });
    TurnSolver::ApplyHumanSequencePin(q, { "land=Forest", "cast=Sol Ring" });
    CHECK(q.human_land_step == 0);   // queued first: land-first, as always
}

TEST_CASE("human sequence: a land token on a land-less plan is not a step")
{
    TurnSolver::Plan p = PlanOf("", { Act(K::CastFromHand, "A"), Act(K::CastFromHand, "B") });
    TurnSolver::ApplyHumanSequencePin(p, { "land=Forest", "cast=B", "tap=Forest#3:G", "cast=A" });
    CHECK(p.human_land_step == -1);
    CHECK(Names(p) == std::vector<std::string>{ "B", "A" });
    REQUIRE(p.human_pre_taps.size() == 1);
    CHECK(p.human_pre_taps[0].position == 1);   // after B: the land counted nothing
}

TEST_CASE("human sequence: the verb picks the KIND -- the Augur in play vs the copy in hand")
{
    TurnSolver::Plan p = PlanOf("", { Act(K::CastFromHand, "Frost Augur"),
                                      Act(K::ActivatePermAbility, "Frost Augur") });
    TurnSolver::ApplyHumanSequencePin(p, { "act=Frost Augur", "cast=Frost Augur" });
    REQUIRE(p.actions.size() == 2);
    CHECK(p.actions[0].kind == K::ActivatePermAbility);
    CHECK(p.actions[1].kind == K::CastFromHand);

    TurnSolver::Plan q = PlanOf("", { Act(K::CastFromHand, "Frost Augur"),
                                      Act(K::ActivatePermAbility, "Frost Augur") });
    TurnSolver::ApplyHumanSequencePin(q, { "cast=Frost Augur", "act=Frost Augur" });
    CHECK(q.actions[0].kind == K::CastFromHand);
    CHECK(q.actions[1].kind == K::ActivatePermAbility);
}

TEST_CASE("human sequence: every queueable kind is a step at its own position")
{
    // The kinds the "*" list could not sequence: a sac-land cast, a retrace, Krenko's tap, a declared
    // sac-for-mana, a suspend, a Vial put -- queued here in an order no canonical pass produces.
    Action sacland = Act(K::CastFromHand, "Shard Volley");
    sacland.sacrifice_land = true;
    TurnSolver::Plan p = PlanOf("Mountain", {
        Act(K::SacForMana, "Skirk Prospector"),
        Act(K::Suspend, "Lotus Bloom"),
        Act(K::CastFromHand, "Lightning Bolt"),
        sacland,
        Act(K::CastFromGraveyard, "Throes of Chaos"),
        Act(K::TapForTokens, "Krenko, Mob Boss"),
        Act(K::ActivateVial, "Goblin Lackey") });
    TurnSolver::ApplyHumanSequencePin(p, { "act=Krenko, Mob Boss", "cast=Shard Volley", "vial=Goblin Lackey",
                                           "land=Mountain", "retrace=Throes of Chaos", "cast=Lightning Bolt",
                                           "suspend=Lotus Bloom", "sacout=Skirk Prospector" });
    CHECK(Names(p) == std::vector<std::string>{ "Krenko, Mob Boss", "Shard Volley", "Goblin Lackey",
                                                "Throes of Chaos", "Lightning Bolt", "Lotus Bloom",
                                                "Skirk Prospector" });
    CHECK(p.human_seq_len == 7);
    CHECK(p.human_land_step == 3);
    CHECK(p.actions[1].sacrifice_land);
}

TEST_CASE("human sequence: each Land's Edge discard is ONE unit at its own position")
{
    Action le = Act(K::DiscardToLandsEdge, "Land's Edge");
    le.discard_lands = 3;
    TurnSolver::Plan p = PlanOf("", { le, Act(K::CastFromHand, "Lightning Bolt") });
    TurnSolver::ApplyHumanSequencePin(p, { "le=Mountain", "cast=Lightning Bolt", "le=Island" });
    REQUIRE(p.actions.size() == 4);
    CHECK(p.human_seq_len == 3);
    CHECK(p.actions[0].kind == K::DiscardToLandsEdge);
    CHECK(p.actions[0].discard_lands == 1);
    CHECK(p.actions[1].card_name.str() == "Lightning Bolt");
    CHECK(p.actions[2].kind == K::DiscardToLandsEdge);
    CHECK(p.actions[2].discard_lands == 1);
    // The unit the queue did not place runs as an implicit action after the last step.
    CHECK(p.actions[3].kind == K::DiscardToLandsEdge);
    CHECK(p.actions[3].discard_lands == 1);
}

TEST_CASE("human sequence: an action nobody queued is implicit -- after the steps, never between them")
{
    // The (C) case: a plan with an implicit self-sacrifice mana source (Lotus Bloom's crack, which the
    // viewer deliberately offers no click for). The old viewer dropped to a cast-only pin here, and the
    // activation order with it; now the queued steps keep their order and the implicit action is laid
    // out after them (ApplyPlanDirect runs an implicit mana / suspend action in the pre-cast pass).
    TurnSolver::Plan p = PlanOf("", { Act(K::SacForMana, "Lotus Bloom"),
                                      Act(K::CastFromHand, "Clue Maker"),
                                      Act(K::ActivateBlink, "Emiel the Blessed"),
                                      Act(K::ActivatePermAbility, "Clue Token") });
    TurnSolver::ApplyHumanSequencePin(p, { "act=Clue Token", "blink=Emiel the Blessed", "cast=Clue Maker" });
    CHECK(Names(p) == std::vector<std::string>{ "Clue Token", "Emiel the Blessed", "Clue Maker", "Lotus Bloom" });
    CHECK(p.human_seq_len == 3);
}

TEST_CASE("human sequence: a token naming nothing in the plan is no step and counts no position")
{
    TurnSolver::Plan p = PlanOf("", { Act(K::CastFromHand, "A") });
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Not In This Plan", "tap=Forest#1:G", "cast=A" });
    CHECK(p.human_seq_len == 1);
    REQUIRE(p.human_pre_taps.size() == 1);
    CHECK(p.human_pre_taps[0].position == 0);
}

TEST_CASE("human sequence: taps sit between the steps they were queued between, the land counted")
{
    TurnSolver::Plan p = PlanOf("Forest", { Act(K::CastFromHand, "A"), Act(K::CastFromHand, "B") });
    TurnSolver::ApplyHumanSequencePin(p, { "tap=Plains#1:W", "cast=A", "land=Forest", "tap=Forest#9:G",
                                           "cast=B", "tap=Island#2:U", "need=G" });
    CHECK(p.human_land_step == 1);
    REQUIRE(p.human_pre_taps.size() == 3);
    CHECK(p.human_pre_taps[0].position == 0);   // before A
    CHECK(p.human_pre_taps[1].position == 2);   // after the land, before B
    CHECK(p.human_pre_taps[2].position == 3);   // after everything: a float for the next segment
    CHECK(p.human_pre_taps[1].name == "Forest");
    CHECK(p.human_untap_need == (1 << static_cast<int>(Color::Green)));
}

TEST_CASE("human sequence: erasing a step keeps the land and the taps on the steps they named")
{
    // DeferHumanAuraSwapToCombat erases the swap from the plan; the land's step and the taps' positions
    // must keep naming the same steps.
    TurnSolver::Plan p = PlanOf("Forest", { Act(K::AuraSwap, "Arcanum Wings"), Act(K::CastFromHand, "A") });
    TurnSolver::ApplyHumanSequencePin(p, { "auraswap=Arcanum Wings", "land=Forest", "tap=Forest#9:G",
                                           "cast=A" });
    REQUIRE(p.human_land_step == 1);
    REQUIRE(p.human_pre_taps.size() == 1);
    REQUIRE(p.human_pre_taps[0].position == 2);
    TurnSolver::EraseHumanSequenceAction(p, 0);
    CHECK(Names(p) == std::vector<std::string>{ "A" });
    CHECK(p.human_seq_len == 1);
    CHECK(p.human_land_step == 0);
    CHECK(p.human_pre_taps[0].position == 1);

    // An action AFTER the land: the land's step is unchanged.
    TurnSolver::Plan q = PlanOf("Forest", { Act(K::CastFromHand, "A"), Act(K::CastFromHand, "B") });
    TurnSolver::ApplyHumanSequencePin(q, { "cast=A", "land=Forest", "cast=B" });
    TurnSolver::EraseHumanSequenceAction(q, 1);
    CHECK(q.human_land_step == 1);
    CHECK(q.human_seq_len == 1);

    // Not a human sequence: a plain erase.
    TurnSolver::Plan r = PlanOf("", { Act(K::CastFromHand, "A"), Act(K::CastFromHand, "B") });
    TurnSolver::EraseHumanSequenceAction(r, 0);
    CHECK(Names(r) == std::vector<std::string>{ "B" });
    CHECK(r.human_seq_len == 0);
}
