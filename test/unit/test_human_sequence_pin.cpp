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
#include "core/GameState.h"

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
    // Skirk Prospector's sacrifice is MANA (USER 2026-10-22): its click is a payment, not a step -- the
    // action stays implicit (laid out after the steps; ApplyPlanDirect runs it before the first one).
    CHECK(p.human_seq_len == 6);
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

// ---- REVIEW 2026-10-22: WHICH play, never a guess (items 1, 4, 5) -------------------------------

namespace
{
Action ActOn(Action::Kind k, const std::string& name, int src, int victim = 0)
{
    Action a = Act(k, name);
    a.sac_source_id = src;
    a.sac_victim_id = victim;
    return a;
}
Permanent PermOf(const std::string& name, int number)
{
    Permanent p;
    p.card.m_name      = name;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    return p;
}
}   // namespace

TEST_CASE("human sequence: `#src` takes the clicked copy, in click order")
{
    TurnSolver::Plan p = PlanOf("", { ActOn(K::TapForTokens, "Krenko, Mob Boss", 5),
                                      ActOn(K::TapForTokens, "Krenko, Mob Boss", 7) });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "act=Krenko, Mob Boss#7", "act=Krenko, Mob Boss#5" }, nullptr, &errors);
    CHECK(errors.empty());
    REQUIRE(p.actions.size() == 2);
    CHECK(p.actions[0].sac_source_id == 7);
    CHECK(p.actions[1].sac_source_id == 5);
}

TEST_CASE("human sequence: a step that could mean two different plays is REFUSED, not picked")
{
    // No `#src` and two DIFFERENT Krenkos (one carries a counter): the engine does not choose.
    GameState s;
    s.battlefield.push_back(PermOf("Krenko, Mob Boss", 5));
    s.battlefield.push_back(PermOf("Krenko, Mob Boss", 7));
    s.battlefield.back().spore_counters = 1;
    TurnSolver::Plan p = PlanOf("", { ActOn(K::TapForTokens, "Krenko, Mob Boss", 5),
                                      ActOn(K::TapForTokens, "Krenko, Mob Boss", 7) });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "act=Krenko, Mob Boss" }, &s, &errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("could mean 2 different permanents") != std::string::npos);
    CHECK(p.human_seq_len == 0);
    CHECK(p.actions.empty());   // and NEITHER candidate runs (re-review item D): no "unqueued" fallback

    // ...but two INDISTINGUISHABLE permanents are the same play: no choice, no error.
    s.battlefield.back().spore_counters = 0;
    TurnSolver::Plan q = PlanOf("", { ActOn(K::TapForTokens, "Krenko, Mob Boss", 5),
                                      ActOn(K::TapForTokens, "Krenko, Mob Boss", 7) });
    std::vector<std::string> e2;
    TurnSolver::ApplyHumanSequencePin(q, { "act=Krenko, Mob Boss", "act=Krenko, Mob Boss" }, &s, &e2);
    CHECK(e2.empty());
    CHECK(q.human_seq_len == 2);
}

TEST_CASE("human sequence: the twin test is conservative -- counter CONTENTS, P/T, temporary grants")
{
    // Third review item 4: a false twin plays the wrong permanent, so every per-object field that can
    // affect play must agree. Each difference below makes the two Krenkos DIFFERENT permanents, so an
    // unstated `#src` is refused rather than resolved to the first.
    auto refused_with = [](auto tweak) {
        GameState s;
        s.battlefield.push_back(PermOf("Krenko, Mob Boss", 5));
        s.battlefield.push_back(PermOf("Krenko, Mob Boss", 7));
        tweak(s.battlefield.back());
        TurnSolver::Plan p = PlanOf("", { ActOn(K::TapForTokens, "Krenko, Mob Boss", 5),
                                          ActOn(K::TapForTokens, "Krenko, Mob Boss", 7) });
        std::vector<std::string> errors;
        TurnSolver::ApplyHumanSequencePin(p, { "act=Krenko, Mob Boss" }, &s, &errors);
        // (twins leave the second Krenko's action unqueued, which is reported too -- look for the REFUSAL)
        for (const std::string& e : errors) { if (e.find("could mean") != std::string::npos) { return true; } }
        return false;
    };
    CHECK(refused_with([](Permanent& p) { p.counters.Add(Counter::Type::PlusOnePlusOne, 1); }));
    CHECK(refused_with([](Permanent& p) { p.temp_haste = true; }));
    CHECK(refused_with([](Permanent& p) { p.exile_at_end = true; }));
    CHECK(refused_with([](Permanent& p) { p.card.m_power = 4; }));
    CHECK(refused_with([](Permanent& p) { p.chosen_color = 2; }));
    CHECK_FALSE(refused_with([](Permanent&) {}));   // identical: twins, no refusal
    // Same counter COUNT, different contents: still different.
    CHECK([&] {
        GameState s;
        s.battlefield.push_back(PermOf("Krenko, Mob Boss", 5));
        s.battlefield.push_back(PermOf("Krenko, Mob Boss", 7));
        s.battlefield[0].counters.Add(Counter::Type::PlusOnePlusOne, 1);
        s.battlefield[1].counters.Add(Counter::Type::PlusOnePlusOne, 2);
        TurnSolver::Plan p = PlanOf("", { ActOn(K::TapForTokens, "Krenko, Mob Boss", 5),
                                          ActOn(K::TapForTokens, "Krenko, Mob Boss", 7) });
        std::vector<std::string> errors;
        TurnSolver::ApplyHumanSequencePin(p, { "act=Krenko, Mob Boss" }, &s, &errors);
        return !errors.empty() && errors[0].find("could mean") != std::string::npos;
    }());
}

TEST_CASE("human sequence: an equip names its Equipment and its host")
{
    TurnSolver::Plan p = PlanOf("", { ActOn(K::Equip, "Bonesplitter", 2, 9), ActOn(K::Equip, "Bonesplitter", 3, 9),
                                      ActOn(K::Equip, "Bonesplitter", 3, 11) });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "equip=Bonesplitter#3@11", "equip=Bonesplitter#2@9", "equip=Bonesplitter#3@9" },
                                      nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.actions[0].sac_source_id == 3);
    CHECK(p.actions[0].sac_victim_id == 11);
    CHECK(p.actions[1].sac_source_id == 2);
}

TEST_CASE("human sequence: an Aura cast's `@` is the permanent it enchants")
{
    // A cast's target is Action::enchant_target (an activation's is sac_victim_id): Wild Growth dragged
    // onto the Azorius Chancery being played names the Chancery's number.
    Action onA = Act(K::CastFromHand, "Wild Growth");
    onA.enchant_target = 40;
    Action onB = Act(K::CastFromHand, "Wild Growth");
    onB.enchant_target = 41;
    TurnSolver::Plan p = PlanOf("", { onA, onB });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Wild Growth@41", "cast=Wild Growth@40" }, nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.actions[0].enchant_target == 41);
    CHECK(p.actions[1].enchant_target == 40);
}

TEST_CASE("human sequence: kicked and unkicked copies are different steps")
{
    Action kicked = Act(K::CastFromHand, "Saproling Migration");
    kicked.kicked = true;
    TurnSolver::Plan p = PlanOf("", { Act(K::CastFromHand, "Saproling Migration"), kicked });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Saproling Migration+kicked", "cast=Saproling Migration" },
                                      nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.actions[0].kicked);
    CHECK_FALSE(p.actions[1].kicked);
}

TEST_CASE("human sequence: an unmatched step is an ERROR, never an implicit action")
{
    // Item 5: an unmatched queued step used to become "unqueued" -- an implicit action -- and a convoke
    // cast laid out that way could have its bodies tapped twice. Now it is reported.
    TurnSolver::Plan p = PlanOf("Forest", { Act(K::CastFromHand, "A") });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "act=A", "cast=Nope", "land=Island" }, nullptr, &errors);
    REQUIRE(errors.size() == 4);
    CHECK(errors[0].find("matches nothing") != std::string::npos);    // act= is not a hand cast
    CHECK(errors[2].find("is not the committed plan's land") != std::string::npos);
    // ...and the cast nobody claimed does NOT run after the last step as though it were unqueued.
    CHECK(errors[3].find("is not in your queue -- it was NOT applied") != std::string::npos);
    CHECK(p.actions.empty());
}

TEST_CASE("human sequence: hand copies differing only in the variant's sub-decisions are interchangeable")
{
    // Re-review item A: two Stoneforge Mystics whose plan variant fetches Jitte with one and Bonesplitter
    // with the other. The token names the card, never the copy and never the fetch -- the human chose
    // the fetches by choosing the variant -- so the steps take the plan's copies in order and nothing is
    // refused (refusing them shrank the variant menu, 3 -> 2).
    Action a = Act(K::CastFromHand, "Stoneforge Mystic");
    a.tutor_target = "Umezawa's Jitte";
    Action b = Act(K::CastFromHand, "Stoneforge Mystic");
    b.tutor_target = "Bonesplitter";
    Action x5 = Act(K::CastFromHand, "Fireball");
    x5.chosen_x = 5;
    Action x2 = Act(K::CastFromHand, "Fireball");
    x2.chosen_x = 2;
    TurnSolver::Plan p = PlanOf("", { a, b, x5, x2 });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Stoneforge Mystic", "cast=Fireball", "cast=Stoneforge Mystic",
                                           "cast=Fireball" }, nullptr, &errors);
    CHECK(errors.empty());
    REQUIRE(p.actions.size() == 4);
    CHECK(p.actions[0].tutor_target.str() == "Umezawa's Jitte");
    CHECK(p.actions[1].chosen_x == 5);
    CHECK(p.actions[2].tutor_target.str() == "Bonesplitter");
    CHECK(p.actions[3].chosen_x == 2);
}

TEST_CASE("human sequence: a stated permanent matches its indistinguishable twin")
{
    // Re-review item C: two identical Clue Tokens; the plan cracks #1, the human clicked #2.
    GameState s;
    s.battlefield.push_back(PermOf("Clue Token", 1));
    s.battlefield.push_back(PermOf("Clue Token", 2));
    TurnSolver::Plan p = PlanOf("", { ActOn(K::ActivatePermAbility, "Clue Token", 1) });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "act=Clue Token#2" }, &s, &errors);
    CHECK(errors.empty());
    CHECK(p.human_seq_len == 1);
    // ...but a DIFFERENT permanent is not a twin.
    s.battlefield.back().tapped = true;
    TurnSolver::Plan q = PlanOf("", { ActOn(K::ActivatePermAbility, "Clue Token", 1) });
    std::vector<std::string> e2;
    TurnSolver::ApplyHumanSequencePin(q, { "act=Clue Token#2" }, &s, &e2);
    CHECK(e2.size() == 2);   // the step matches nothing, and the plan's crack of #1 is not run either
}

TEST_CASE("human sequence: each Land's Edge step NAMES the land it discards")
{
    Action le = Act(K::DiscardToLandsEdge, "Land's Edge");
    le.discard_lands = 2;
    TurnSolver::Plan p = PlanOf("Island", { le });
    TurnSolver::ApplyHumanSequencePin(p, { "le=Mountain", "land=Island", "le=Forest" });
    REQUIRE(p.actions.size() == 2);
    CHECK(p.actions[0].discard_name.str() == "Mountain");
    CHECK(p.actions[1].discard_name.str() == "Forest");
    CHECK(p.human_land_step == 1);
}

// ---- THIRD REVIEW 2026-10-22: actions a token claims WITHOUT a step of its own -------------------

TEST_CASE("human sequence: a MANA sac outlet's clicks are payment, never steps")
{
    // USER 2026-10-22: Skirk Prospector's sacrifices are MANA -- they belong to the payment. One plan
    // action carries both (sac_count 2); the two `sacout=` clicks claim nothing as steps, and the action
    // stays implicit (run before the first step, so its {R}{R} pays for the casts that need it).
    Action burst = ActOn(K::SacForMana, "Skirk Prospector", 3);
    burst.sac_count = 2;
    TurnSolver::Plan p = PlanOf("", { burst, Act(K::CastFromHand, "Krenko, Mob Boss") });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "sacout=Skirk Prospector", "sacout=Skirk Prospector",
                                           "cast=Krenko, Mob Boss" }, nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.human_seq_len == 1);
    CHECK(Names(p) == std::vector<std::string>{ "Krenko, Mob Boss", "Skirk Prospector" });
    CHECK(p.actions[1].sac_count == 2);
}

TEST_CASE("human sequence: two single activations of a NON-mana outlet are two steps")
{
    Action one = ActOn(K::SacCreatureOutlet, "Carrion Feeder", 4);
    one.sac_count = 1;
    TurnSolver::Plan r = PlanOf("", { one, one });
    std::vector<std::string> e3;
    TurnSolver::ApplyHumanSequencePin(r, { "sacout=Carrion Feeder", "sacout=Carrion Feeder" }, nullptr, &e3);
    CHECK(e3.empty());
    CHECK(r.human_seq_len == 2);
    // ...and a third click has nothing left to claim.
    TurnSolver::Plan q = PlanOf("", { one, one });
    std::vector<std::string> e2;
    TurnSolver::ApplyHumanSequencePin(q, { "sacout=Carrion Feeder", "sacout=Carrion Feeder",
                                           "sacout=Carrion Feeder" }, nullptr, &e2);
    CHECK(e2.size() == 1);
}

TEST_CASE("human sequence: a sac LOOP absorbs any number of clicks (its K is the variant's)")
{
    Action loop = ActOn(K::SacCreatureOutlet, "Carrion Feeder", 4);
    loop.sac_count = 8;
    TurnSolver::Plan p = PlanOf("", { loop });
    std::vector<std::string> tokens(11, "sacout=Carrion Feeder");
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, tokens, nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.human_seq_len == 1);
}

TEST_CASE("human sequence: cast=Remand claims the Remand riding on the cast it counters")
{
    Action ponder = Act(K::CastFromHand, "Ponder");
    ponder.remand_by = "Remand";
    TurnSolver::Plan p = PlanOf("", { ponder });
    std::vector<std::string> errors;
    TurnSolver::ApplyHumanSequencePin(p, { "cast=Ponder", "cast=Remand" }, nullptr, &errors);
    CHECK(errors.empty());
    CHECK(p.human_seq_len == 1);
    // ...once: a second Remand has no rider left to claim.
    TurnSolver::Plan q = PlanOf("", { ponder });
    std::vector<std::string> e2;
    TurnSolver::ApplyHumanSequencePin(q, { "cast=Ponder", "cast=Remand", "cast=Remand" }, nullptr, &e2);
    CHECK(e2.size() == 1);
}

TEST_CASE("human sequence: EVERY action kind is claimable by the token the viewer writes for it")
{
    // TurnSolver::HumanStepVerbFor is one entry per kind behind a static_assert on the enum's end, so a
    // new kind cannot reach a "**" line without a verb; this claims one action of every kind through it,
    // so a verb that does not actually claim its kind (the orphan rule would drop it) fails here.
    // The LAST enumerator (ActivateUntap, appended after AuraSwap upstream): a new kind already fails to
    // compile in HumanStepVerbFor (-Wswitch is an error there); extend this bound with it.
    for (int ki = 0; ki <= static_cast<int>(K::ActivateUntap); ++ki)
    {
        const K k = static_cast<K>(ki);
        const std::string verb = TurnSolver::HumanStepVerbFor(k);
        CAPTURE(ki);
        CAPTURE(verb);
        if (verb.empty())
        {
            CHECK((k == K::DigDraw || k == K::ComboRoute || k == K::PlayLand || k == K::SacForMana));
            if (k == K::SacForMana)
            {
                // MANA: never a step, never an orphan -- an unclaimed one stays in the plan, implicit.
                TurnSolver::Plan p = PlanOf("", { Act(k, "X") });
                std::vector<std::string> errors;
                TurnSolver::ApplyHumanSequencePin(p, {}, nullptr, &errors);
                CHECK(errors.empty());
                CHECK(p.actions.size() == 1);
            }
            continue;
        }
        Action a = Act(k, "X");
        if (k == K::DiscardToLandsEdge) { a.discard_lands = 1; }
        TurnSolver::Plan p = PlanOf("", { a });
        std::vector<std::string> errors;
        TurnSolver::ApplyHumanSequencePin(p, { verb == "le" ? std::string("le=Mountain") : verb + "=X" }, nullptr, &errors);
        CHECK(errors.empty());
        CHECK(p.human_seq_len == 1);
    }
}

TEST_CASE("human sequence: an untapper's single activation and its LOOP are different steps")
{
    // untapact= is one activation, untaploop= the closed-form loop (chosen_x >= kUntapLoopToNeed) -- the
    // CheckLine rule, so a click on one never claims the other.
    Action one = ActOn(K::ActivateUntap, "Staff of Domination", 4, 9);
    one.chosen_x = 1;
    Action loop = ActOn(K::ActivateUntap, "Staff of Domination", 4, 9);
    loop.chosen_x = kUntapLoopToNeed;
    {
        TurnSolver::Plan p = PlanOf("", { loop });
        std::vector<std::string> errors;
        TurnSolver::ApplyHumanSequencePin(p, { "untapact=Staff of Domination#4@9" }, nullptr, &errors);
        CHECK(!errors.empty());
        CHECK(p.human_seq_len == 0);
    }
    {
        TurnSolver::Plan p = PlanOf("", { one, loop });
        std::vector<std::string> errors;
        TurnSolver::ApplyHumanSequencePin(p, { "untaploop=Staff of Domination#4@9", "untapact=Staff of Domination#4@9" },
                                          nullptr, &errors);
        CHECK(errors.empty());
        REQUIRE(p.human_seq_len == 2);
        CHECK(p.actions[0].chosen_x == kUntapLoopToNeed);
        CHECK(p.actions[1].chosen_x == 1);
    }
}
