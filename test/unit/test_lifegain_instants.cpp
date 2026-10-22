// Unit tests for the two lifegain instants added for the SelesnyaLifegain screen (2026-10-22):
//
// HEROES' REUNION {G}{W} -- "Target player gains 7 life."
// REST FOR THE WEARY {1}{W} -- "Target player gains 4 life. Landfall -- If you had a land enter the
//   battlefield under your control this turn, that player gains 8 life instead."
//
// Both resolve through Feed the Clan's shared ApplyCastLifegain, which the executor
// (EffectHandler::ResolveImpl) and the rollout (TurnSolver::apply_one) both call. What must hold:
//  1. Each gain is ONE life-gain EVENT (CR 119.10; Rest's ruling 5 "only the landfall-based effect
//     happens"). The discriminating witness is a Nykthos Paragon: it acts ONCE per turn per copy, so
//     one event of 8 puts +8/+8 on the team while 4-then-4 would put only +4/+4.
//  2. Rest's condition is "a land ENTERED under your control this turn", read AS IT RESOLVES -- a
//     played land, a land PUT by an effect (Genesis Wave, no drop used), AND a land that has since
//     LEFT (ruling 3: "It doesn't matter if a land that entered ... is still on the battlefield").
//     The last is the case the board cannot show; Player::land_entered_this_turn carries it.
//  3. An OPPONENT's land entering does not count ("under your control").
//  4. The executor and the rollout realise the SAME amount for the same plan, in every case above.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLi()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLi(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshLi()
{
    EnsureCardsLi();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < 20; ++k)   // a library of lands: no draw in this test can find a spell
    {
        Card c = DefLi("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

Permanent& PutLi(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = DefLi(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandLi(GameState& s, const std::string& name, int number)
{
    Card c = DefLi(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

int PlusCountersLi(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_number != number) { continue; }
        int n = 0;
        for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
        return n;
    }
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return -1;
}

Action CastLi(const GameState& s, const std::string& name, int number)
{
    const CardDefinition& d = DefLi(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d.card.m_name;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.card_mv        = d.card.m_mana_cost.ManaValue();
    a.is_noncreature = true;
    a.hand_index     = -1;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_number == number) { a.hand_index = i; } }
    return a;
}

void ResolveAllLi(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// The rollout world: TurnSolver::ApplyPlan (apply_one -> ApplyCastLifegain).
GameState RollLi(const GameState& s, const TurnSolver::Plan& p)
{
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    return t;
}
// The executor world: a committed plan executed by AIEngine::TakeTurn, every cast resolved through
// EffectHandler (ResolveImpl -> ApplyCastLifegain), GameEngine::MainPhase's shape.
GameState ExecLi(const GameState& s, const TurnSolver::Plan& p)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState t = s;
    eng.TakeTurn(t, true, ResolveAllLi);
    return t;
}

// A board that can pay {G}{W} or {1}{W}, a Nykthos Paragon as the one-event witness (one use per turn,
// "that many" counters on each creature we control, itself included), and the spell in hand.
GameState BoardWithSpell(const std::string& spell)
{
    GameState s = FreshLi();
    PutLi(s, "Plains", 1);
    PutLi(s, "Forest", 2);
    PutLi(s, "Plains", 3);
    PutLi(s, "Nykthos Paragon", 10);
    HandLi(s, spell, 50);
    return s;
}

TurnSolver::Plan PlanLi(const GameState& s, const std::string& spell, const std::string& land = "")
{
    TurnSolver::Plan p;
    p.actions.push_back(CastLi(s, spell, 50));
    p.land_decided = true;      // play exactly land_to_play ("" == no drop), never the greedy drop
    p.land_to_play = land;
    return p;
}

// By NAME: the rollout's zone move does not carry the hand copy's per-card number into the graveyard.
bool InGraveyardLi(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

// Both worlds, one plan: the spell resolved, the life gained, and the Paragon's ONE-EVENT witness.
void CheckBothWorlds(const GameState& s, const TurnSolver::Plan& p, const std::string& spell, int expect_gain)
{
    const int life0 = s.players[0].life;
    for (int world = 0; world < 2; ++world)
    {
        CAPTURE(world);   // 0 = rollout (ApplyPlan), 1 = executor (AIEngine + EffectHandler)
        const GameState t = world == 0 ? RollLi(s, p) : ExecLi(s, p);
        REQUIRE(InGraveyardLi(t, spell));                         // it was cast and it resolved
        CHECK(t.players[0].life == life0 + expect_gain);
        CHECK(t.players[0].life_gained_this_turn == expect_gain);
        CHECK(t.players[1].life == s.players[1].life);         // "target player" is us, never them
        CHECK(PlusCountersLi(t, 10) == expect_gain);           // ONE event: the Paragon's single use
    }
}

}   // namespace

TEST_CASE("Heroes' Reunion / Rest for the Weary: the card data is the printed card")
{
    EnsureCardsLi();
    const CardDefinition& hr = DefLi("Heroes' Reunion");
    CHECK(hr.card.IsInstant());
    CHECK(hr.card.m_mana_cost.ManaValue() == 2);
    CHECK(hr.card.m_mana_cost.green == 1);
    CHECK(hr.card.m_mana_cost.white == 1);
    CHECK(hr.params.cast_lifegain == 7);
    CHECK(hr.params.cast_lifegain_landfall == 0);
    CHECK(hr.params.cast_lifegain_ferocious == 0);
    CHECK(hr.params.cast_lifegain_target_player);   // "TARGET PLAYER gains" -- a real target

    const CardDefinition& rw = DefLi("Rest for the Weary");
    CHECK(rw.card.IsInstant());
    CHECK(rw.card.m_mana_cost.ManaValue() == 2);
    CHECK(rw.card.m_mana_cost.generic == 1);
    CHECK(rw.card.m_mana_cost.white == 1);
    CHECK(rw.params.cast_lifegain == 4);
    CHECK(rw.params.cast_lifegain_landfall == 8);
    CHECK(rw.params.cast_lifegain_ferocious == 0);
    CHECK(rw.params.cast_lifegain_target_player);
    // A CONDITION, not a landfall TRIGGER: it must not flip the per-definition watcher byte (which
    // would switch Genesis Wave to its landfall trigger order and arm the landfall dispatch walk).
    CHECK_FALSE(rw.landfall_watcher);
}

TEST_CASE("ApplyCastLifegain: Heroes' Reunion is ONE event of 7")
{
    GameState s = BoardWithSpell("Heroes' Reunion");
    const int life0 = s.players[0].life;
    ApplyCastLifegain(s, 0, DefLi("Heroes' Reunion"));
    CHECK(s.players[0].life == life0 + 7);
    CHECK(s.players[0].life_gained_this_turn == 7);
    CHECK(PlusCountersLi(s, 10) == 7);   // seven events of 1 would be +1: the Paragon acts once
}

TEST_CASE("ApplyCastLifegain: Rest for the Weary reads the landfall condition AS IT RESOLVES")
{
    const CardDefinition& rw = DefLi("Rest for the Weary");

    SUBCASE("no land entered this turn: 4")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        REQUIRE_FALSE(LandEnteredUnderControlThisTurn(s, 0));
        ApplyCastLifegain(s, 0, rw);
        CHECK(s.players[0].life_gained_this_turn == 4);
        CHECK(PlusCountersLi(s, 10) == 4);
    }
    SUBCASE("a land drop was used: 8, as ONE event (4-then-4 would leave the Paragon at +4)")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        s.players[0].lands_played_this_turn = 1;
        ApplyCastLifegain(s, 0, rw);
        CHECK(s.players[0].life_gained_this_turn == 8);
        CHECK(PlusCountersLi(s, 10) == 8);
    }
    SUBCASE("a land PUT by an effect, no drop used: 8")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        EnterLand(s, DefLi("Forest"), 60);
        REQUIRE(s.players[0].lands_played_this_turn == 0);
        ApplyCastLifegain(s, 0, rw);
        CHECK(s.players[0].life_gained_this_turn == 8);
    }
    SUBCASE("an OPPONENT's land entering does not count")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        PutLi(s, "Forest", 70, /*controller=*/1).entered_this_turn = true;
        OnLandEntered(s, 1, 70, nullptr);
        CHECK(s.players[1].land_entered_this_turn);
        CHECK_FALSE(s.players[0].land_entered_this_turn);
        ApplyCastLifegain(s, 0, rw);
        CHECK(s.players[0].life_gained_this_turn == 4);
    }
    SUBCASE("a second land gives nothing more (ruling 2)")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        s.players[0].lands_played_this_turn = 1;
        EnterLand(s, DefLi("Forest"), 61);
        ApplyCastLifegain(s, 0, rw);
        CHECK(s.players[0].life_gained_this_turn == 8);
    }
}

// The case the board cannot show: a land that ENTERED this turn and is gone again, with no drop used.
// Reached here through the real route the deck has -- a Genesis Wave that puts a Selesnya Sanctuary as
// our ONLY land, whose mandatory bounce must then return the Sanctuary itself.
TEST_CASE("Rest for the Weary: a land that entered and LEFT still counts (ruling 3)")
{
    GameState s = FreshLi();
    PutLi(s, "Llanowar Elves", 20);                 // no land on the battlefield at all
    PutLi(s, "Nykthos Paragon", 10);
    s.players[0].library = Library();
    {
        Card raw;                                    // a library card is a NAME-ONLY placeholder
        raw.m_name   = DefLi("Selesnya Sanctuary").card.m_name;
        raw.m_number = 101;
        raw.RehashName();
        s.players[0].library.push_back(raw);
    }
    PerformGenesisWave(s, 0, 1, "Genesis Wave");

    // The Sanctuary entered and returned ITSELF: no land on the battlefield, no drop used -- so the two
    // facts the engine always tracked both say "no land entered", and only the history bit knows.
    int lands_on_bf = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand()) { ++lands_on_bf; } }
    REQUIRE(lands_on_bf == 0);
    bool sanctuary_in_hand = false;
    for (const Card& c : s.players[0].hand) { if (c.m_number == 101) { sanctuary_in_hand = true; } }
    REQUIRE(sanctuary_in_hand);
    REQUIRE(s.players[0].lands_played_this_turn == 0);
    CHECK(s.players[0].land_entered_this_turn);
    CHECK(LandEnteredAndGoneThisTurn(s, 0));
    CHECK(LandEnteredUnderControlThisTurn(s, 0));

    ApplyCastLifegain(s, 0, DefLi("Rest for the Weary"));
    CHECK(s.players[0].life_gained_this_turn == 8);
    CHECK(PlusCountersLi(s, 10) == 8);
}

TEST_CASE("Landfall-history fold gate: only the vanished-land residual, only with a reader in hand")
{
    GameState s = BoardWithSpell("Heroes' Reunion");
    CHECK_FALSE(HandReadsLandEnteredThisTurn(s.players[0]));   // Heroes' Reunion reads no land history
    HandLi(s, "Rest for the Weary", 51);
    CHECK(HandReadsLandEnteredThisTurn(s.players[0]));

    // A drop used, or a land still here with entered_this_turn: NOT the residual (already in the key).
    s.players[0].land_entered_this_turn = true;
    s.players[0].lands_played_this_turn = 1;
    CHECK_FALSE(LandEnteredAndGoneThisTurn(s, 0));
    s.players[0].lands_played_this_turn = 0;
    PutLi(s, "Forest", 80).entered_this_turn = true;
    CHECK_FALSE(LandEnteredAndGoneThisTurn(s, 0));
    s.battlefield.pop_back();
    CHECK(LandEnteredAndGoneThisTurn(s, 0));
    s.players[0].land_entered_this_turn = false;
    CHECK_FALSE(LandEnteredAndGoneThisTurn(s, 0));
}

// ---- Both worlds: the rollout's apply_one and the executor's EffectHandler realise the same gain ----

TEST_CASE("Heroes' Reunion: executor and rollout both gain 7 as ONE event")
{
    GameState s = BoardWithSpell("Heroes' Reunion");
    s.players[0].lands_played_this_turn = 1;   // drop spent: the plan is the cast alone
    CheckBothWorlds(s, PlanLi(s, "Heroes' Reunion"), "Heroes' Reunion", 7);
}

TEST_CASE("Rest for the Weary: executor and rollout agree on 4 / 8 in every landfall case")
{
    SUBCASE("no land entered this turn, none in hand: 4")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        CheckBothWorlds(s, PlanLi(s, "Rest for the Weary"), "Rest for the Weary", 4);
    }
    SUBCASE("the plan's own land drop comes first: 8")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        HandLi(s, "Plains", 40);
        CheckBothWorlds(s, PlanLi(s, "Rest for the Weary", "Plains"), "Rest for the Weary", 8);
    }
    SUBCASE("a land drop taken earlier this turn: 8")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        s.players[0].lands_played_this_turn = 1;
        CheckBothWorlds(s, PlanLi(s, "Rest for the Weary"), "Rest for the Weary", 8);
    }
    SUBCASE("a land PUT earlier this turn (no drop used) and still on the battlefield: 8")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        EnterLand(s, DefLi("Forest"), 61);
        s.battlefield.back().tapped = true;   // its mana is not what pays for the Rest
        REQUIRE(s.players[0].lands_played_this_turn == 0);
        CheckBothWorlds(s, PlanLi(s, "Rest for the Weary"), "Rest for the Weary", 8);
    }
    SUBCASE("a land PUT earlier this turn and GONE again (no drop used): 8")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        EnterLand(s, DefLi("Blighted Steppe"), 62);
        s.battlefield.pop_back();             // sacrificed for its life the same turn
        REQUIRE(s.players[0].lands_played_this_turn == 0);
        REQUIRE(LandEnteredAndGoneThisTurn(s, 0));
        CheckBothWorlds(s, PlanLi(s, "Rest for the Weary"), "Rest for the Weary", 8);
    }
}

// ---- "TARGET PLAYER": a real choice, searched then pruned (USER 2026-10-22) ------------------------
//  5. The opponent target gives the OPPONENT the life, through the opponent-gain lane: it is not OUR
//     life-gain event (no life_gained_this_turn, no Paragon), the landfall condition still reads the
//     CASTER's lands, a Tainted Remedy flips it to a loss, and in 2HG the shared team total rises ONCE.
//  6. Both worlds resolve the opponent variant identically (Action::player_target -> CastRiders).
//  7. The ENGINE enumerates both targets; the default provider PRUNES the opponent one; a provider that
//     admits it gets the variant back. 8. A human is asked at resolution, default = the plan's target.

namespace
{
// A deck provider that admits the opponent target (the shape a Tainted Remedy deck would override).
struct AdmitOppLifegainProvider : GenericProvider
{
    bool OffersLifegainOpponentTarget(const GameState&, const CardDefinition&) const override { return true; }
};

int CountPlansCasting(const std::vector<TurnSolver::Plan>& plans, const std::string& spell, int player_target)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == spell
                && a.player_target == player_target) { ++n; break; }
        }
    }
    return n;
}
}   // namespace

TEST_CASE("Target player = the OPPONENT: their life, not our life-gain event")
{
    SUBCASE("Heroes' Reunion: the opponent gains 7; we gain nothing and no watcher of ours fires")
    {
        GameState s = BoardWithSpell("Heroes' Reunion");
        const int us0 = s.players[0].life, opp0 = s.players[1].life;
        ApplyCastLifegain(s, 0, DefLi("Heroes' Reunion"), /*player_target=*/1);
        CHECK(s.players[1].life == opp0 + 7);
        CHECK(s.players[0].life == us0);
        CHECK(s.players[0].life_gained_this_turn == 0);
        CHECK(PlusCountersLi(s, 10) == 0);   // Nykthos Paragon watches OUR gains only
    }
    SUBCASE("Rest for the Weary: the landfall condition reads the CASTER's lands -- 8 to the opponent")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        s.players[0].lands_played_this_turn = 1;
        const int opp0 = s.players[1].life;
        ApplyCastLifegain(s, 0, DefLi("Rest for the Weary"), 1);
        CHECK(s.players[1].life == opp0 + 8);
        CHECK(s.players[0].life_gained_this_turn == 0);
    }
    SUBCASE("...and 4 when WE had no land enter, whatever the opponent's lands did")
    {
        GameState s = BoardWithSpell("Rest for the Weary");
        s.players[1].land_entered_this_turn = true;
        const int opp0 = s.players[1].life;
        ApplyCastLifegain(s, 0, DefLi("Rest for the Weary"), 1);
        CHECK(s.players[1].life == opp0 + 4);
    }
    SUBCASE("a Tainted Remedy flips the opponent's gain into a LOSS (the OpponentGainsLife lane)")
    {
        GameState s = BoardWithSpell("Heroes' Reunion");
        PutLi(s, "Tainted Remedy", 30);
        const int opp0 = s.players[1].life;
        ApplyCastLifegain(s, 0, DefLi("Heroes' Reunion"), 1);
        CHECK(s.players[1].life == opp0 - 7);
    }
    SUBCASE("Two-Headed Giant: ONE player gains, the shared team total rises by 7 -- not x heads")
    {
        const gamesetup::Setup saved = gamesetup::t_setup;
        gamesetup::t_setup.opponent_heads = 2;
        gamesetup::t_setup.starting_life  = 30;
        REQUIRE(gamesetup::OpponentHeads() == 2);
        GameState s = BoardWithSpell("Heroes' Reunion");
        s.players[1].life = 30;
        ApplyCastLifegain(s, 0, DefLi("Heroes' Reunion"), 1);
        CHECK(s.players[1].life == 37);
        gamesetup::t_setup = saved;
    }
    SUBCASE("a 'you gain' card ignores the target channel entirely (Feed the Clan)")
    {
        GameState s = BoardWithSpell("Feed the Clan");
        const int opp0 = s.players[1].life;
        ApplyCastLifegain(s, 0, DefLi("Feed the Clan"), 1);
        CHECK(s.players[1].life == opp0);
        CHECK(s.players[0].life_gained_this_turn == 10);   // ferocious: the 4/6 Paragon
    }
}

TEST_CASE("Target player = the opponent: executor and rollout resolve the variant identically")
{
    for (const char* spell : { "Heroes' Reunion", "Rest for the Weary" })
    {
        CAPTURE(spell);
        GameState s = BoardWithSpell(spell);
        s.players[0].lands_played_this_turn = 1;   // Rest: landfall on -> 8
        TurnSolver::Plan p = PlanLi(s, spell);
        p.actions[0].player_target = 1;
        const int expect = std::string(spell) == "Heroes' Reunion" ? 7 : 8;
        for (int world = 0; world < 2; ++world)
        {
            CAPTURE(world);
            const GameState t = world == 0 ? RollLi(s, p) : ExecLi(s, p);
            REQUIRE(InGraveyardLi(t, spell));
            CHECK(t.players[1].life == s.players[1].life + expect);
            CHECK(t.players[0].life == s.players[0].life);
            CHECK(t.players[0].life_gained_this_turn == 0);
            CHECK(PlusCountersLi(t, 10) == 0);
        }
    }
}

TEST_CASE("Target player: the ENGINE enumerates both targets; the default provider prunes to self")
{
    GameState s = BoardWithSpell("Heroes' Reunion");
    s.players[0].lands_played_this_turn = 1;

    SUBCASE("default (GenericProvider): only the self-target cast is offered")
    {
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountPlansCasting(plans, "Heroes' Reunion", 0) >= 1);
        CHECK(CountPlansCasting(plans, "Heroes' Reunion", 1) == 0);
        CHECK_FALSE(GenericProvider().OffersLifegainOpponentTarget(s, DefLi("Heroes' Reunion")));
    }
    SUBCASE("a provider that admits the opponent gets BOTH targets, as distinct plans")
    {
        static const AdmitOppLifegainProvider admit;
        s.m_provider = &admit;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountPlansCasting(plans, "Heroes' Reunion", 0) >= 1);
        CHECK(CountPlansCasting(plans, "Heroes' Reunion", 1) >= 1);
    }
}

TEST_CASE("Target player: a HUMAN is asked at resolution over both players, default = the plan's target")
{
    int asked = 0, asked_max = -1, asked_amount = -1;
    std::vector<ChosenTarget> asked_default;
    int answer_player = 1;   // the human picks the opponent
    TargetChooser chooser = [&](const GameState&, const CardDefinition& d, int controller, int max_targets,
                                int per_target, const std::vector<ChosenTarget>& heur) -> std::vector<ChosenTarget>
    {
        ++asked; asked_max = max_targets; asked_amount = per_target; asked_default = heur;
        CHECK(d.params.cast_lifegain_target_player);
        CHECK(controller == 0);
        return { ChosenTarget{ 0, answer_player, 0 } };
    };
    TargetChooser* const saved = g_play_target_chooser;
    g_play_target_chooser = &chooser;

    GameState s = BoardWithSpell("Rest for the Weary");
    s.players[0].lands_played_this_turn = 1;
    const int opp0 = s.players[1].life;
    ApplyCastLifegain(s, 0, DefLi("Rest for the Weary"), /*player_target=*/0);
    CHECK(asked == 1);
    CHECK(asked_max == 1);
    CHECK(asked_amount == 8);                          // the prompt reads "gains 8 life"
    REQUIRE(asked_default.size() == 1);
    CHECK(asked_default[0].kind == 0);
    CHECK(asked_default[0].index == 0);                // the plan's target: us
    CHECK(s.players[1].life == opp0 + 8);              // ...and the human's pick is honoured
    CHECK(s.players[0].life_gained_this_turn == 0);

    // Picking ourselves gains us the life as our own event; a "you gain" card is never asked.
    answer_player = 0;
    GameState u = BoardWithSpell("Heroes' Reunion");
    ApplyCastLifegain(u, 0, DefLi("Heroes' Reunion"), 0);
    CHECK(u.players[0].life_gained_this_turn == 7);
    const int before = asked;
    GameState f = BoardWithSpell("Feed the Clan");
    ApplyCastLifegain(f, 0, DefLi("Feed the Clan"), 0);
    CHECK(asked == before);

    g_play_target_chooser = saved;
}
