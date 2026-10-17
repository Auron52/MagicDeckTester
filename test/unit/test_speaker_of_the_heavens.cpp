// Speaker of the Heavens (Colossus Hammer onboarding, 2026-10-17):
//   "Vigilance, lifelink. {T}: Create a 4/4 white Angel creature token with flying. Activate only if
//    you have at least 7 life more than your starting life total and only as a sorcery."
//
// It rides Sliver Hive's tap_token_* family (Action::Kind::TapForTokenPay) with an ALL-ZERO cost and
// three new params (tap_token_color / tap_token_keywords / tap_token_min_life_above_start), and ONE
// legality predicate -- TapTokenSourceLive (SpellEffects.h) -- read by the enumeration and re-checked
// at both apply sites. These pin what a game log cannot show:
//   1. the life gate is RELATIVE (StartingLife() + 7: 27 normally, 37 in a 2HG job), and the main-1
//      activation is enumerated once it passes;
//   2. summoning sickness and tapped-ness both close it (CR 302.6) -- including the latent Basri bug
//      the shared predicate fixes, and Sliver Hive (a land) staying offered;
//   3. the {0} cost is payable by BOTH payers and taps nothing;
//   4. the rollout (ApplyPlan) and the executor (AIEngine via the committed-line seam) realise the
//      same board: Speaker tapped, one 4/4 white flying Angel token, summoning sick;
//   5. a stale activation (life fell below the gate after enumeration) pays and makes nothing in
//      either world;
//   6. vigilance + lifelink through the shared combat core: a Colossus Hammer'd Speaker attacks,
//      stays untapped and gains 11;
//   7. the deck provider's restricted post-combat main (MTG_SPEAKER_M2): live only past the gate with a
//      live Speaker, its menu is the activations alone, and the lever closes it;
//   8. the white token is a ONE-colour Dragonfire Blade host (equip {3}), which EquipTargetColorCount
//      read as 0 for every token before.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

constexpr const char* kSpeaker = "Speaker of the Heavens";
constexpr int         kSpeakerNum = 50;

void EnsureCardsSp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSp(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const ColossusHammerProvider& Hammer()
{
    static const ColossusHammerProvider p;
    return p;
}

// Pins MTG_SPEAKER_M2 for one test; restores "use the env default" on exit.
struct ArmSp
{
    explicit ArmSp(int m2) { heurarm::t_arm[heurarm::SPEAKER_M2] = static_cast<std::int8_t>(m2); }
    ~ArmSp() { heurarm::t_arm[heurarm::SPEAKER_M2] = -1; }
};

// Restores the per-job starting life after a 2HG-shaped test.
struct StartLifeSp
{
    explicit StartLifeSp(int v) { gamesetup::t_setup.starting_life = v; }
    ~StartLifeSp() { gamesetup::t_setup.starting_life = -1; }
};

void PutSp(GameState& s, const std::string& name, int number, bool tapped = false, bool sick = false)
{
    Permanent p;
    p.card              = DefSp(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

// Turn 5, our ColossusHammer provider, `plains` untapped Plains (numbered 1..), life `life`, and one
// Speaker (number 50) unless `speaker` is false. The library is 20 Plains so a draw never fails.
GameState BoardSp(int life, int plains = 0, bool speaker = true, bool sick = false, bool tapped = false,
                  const std::vector<std::string>& hand = {})
{
    EnsureCardsSp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.game_seed           = 4242;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    s.m_provider          = &Hammer();
    for (int k = 0; k < plains; ++k) { PutSp(s, "Plains", 1 + k); }
    if (speaker) { PutSp(s, kSpeaker, kSpeakerNum, tapped, sick); }
    int h = 20;
    for (const std::string& n : hand)
    {
        Card c = DefSp(n).card;
        c.m_number = h++;
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefSp("Plains").card;
        c.m_number = 100 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int TapTokenActs(const TurnSolver::Plan& p, const std::string& source)
{
    int n = 0;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::TapForTokenPay && a.card_name == source) { ++n; } }
    return n;
}

bool AnyPlanTapsToken(const std::vector<TurnSolver::Plan>& plans, const std::string& source)
{
    for (const TurnSolver::Plan& p : plans) { if (TapTokenActs(p, source) > 0) { return true; } }
    return false;
}

// The plan whose ONLY action is one activation of `source` (no land), or nullptr.
const TurnSolver::Plan* SoleActivation(const std::vector<TurnSolver::Plan>& plans, const std::string& source)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && TapTokenActs(p, source) == 1 && p.land_to_play.empty()) { return &p; }
    }
    return nullptr;
}

const Permanent* FindSp(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

std::vector<const Permanent*> Tokens(const GameState& s)
{
    std::vector<const Permanent*> out;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { out.push_back(&p); } }
    return out;
}

// What GameEngine::MainPhase does between casts: resolve the stack.
void ResolveStackSp(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

void CheckOneAngel(const GameState& g)
{
    const std::vector<const Permanent*> toks = Tokens(g);
    REQUIRE(toks.size() == 1);
    const Permanent& t = *toks[0];
    CHECK(t.card.m_power.value_or(-1) == 4);
    CHECK(t.card.m_toughness.value_or(-1) == 4);
    CHECK(CardHasSubtype(t.card, "Angel"));
    CHECK(t.card.HasColor(Color::White));
    CHECK(t.card.ColorCount() == 1);
    CHECK(t.card.HasKeyword(Keyword::Flying));
    CHECK(t.is_token);
    CHECK(t.entered_this_turn);
    CHECK(t.controller_index == 0);
    CHECK_FALSE(CanAttackFull(t, g.battlefield, 0));   // summoning sick, no haste
}

}   // namespace

TEST_CASE("Speaker: the life gate is RELATIVE to the starting life, and the main-1 activation is enumerated")
{
    const int start = gamesetup::StartingLife();
    REQUIRE(start == 20);
    // One below the gate: nothing to activate.
    CHECK_FALSE(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(start + 6), true), kSpeaker));
    // At start + 7 (27): offered in main 1 -- the search weighs it against Speaker's own attack.
    CHECK(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(start + 7), true), kSpeaker));
    // The predicate itself agrees.
    {
        const GameState s = BoardSp(start + 7);
        CHECK(TapTokenSourceLive(s, *FindSp(s, kSpeakerNum), DefSp(kSpeaker)));
        const GameState t = BoardSp(start + 6);
        CHECK_FALSE(TapTokenSourceLive(t, *FindSp(t, kSpeakerNum), DefSp(kSpeaker)));
    }
}

TEST_CASE("Speaker: a 2HG job (starting life 30) needs 37, never a literal 27")
{
    StartLifeSp two_headed(30);
    REQUIRE(gamesetup::StartingLife() == 30);
    CHECK_FALSE(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(27), true), kSpeaker));
    CHECK_FALSE(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(36), true), kSpeaker));
    CHECK(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(37), true), kSpeaker));
}

TEST_CASE("Speaker: summoning sickness and tapped-ness both close the activation")
{
    const int life = gamesetup::StartingLife() + 7;
    CHECK_FALSE(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(life, 0, true, /*sick=*/true), true),
                                 kSpeaker));
    CHECK_FALSE(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(BoardSp(life, 0, true, false, /*tapped=*/true),
                                                                true), kSpeaker));
    // Haste lifts the {T} restriction (CR 302.6) -- CanTapNow, the same oracle every {T} ability reads.
    GameState s = BoardSp(life, 0, true, /*sick=*/true);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == kSpeakerNum) { p.temp_haste = true; } }
    CHECK(TapTokenSourceLive(s, *FindSp(s, kSpeakerNum), DefSp(kSpeaker)));
}

TEST_CASE("Speaker: the shared predicate's regression guards -- a sick Basri is closed, Sliver Hive is not")
{
    EnsureCardsSp();
    // Basri, Tomorrow's Champion ({W},{T},Exert): a CREATURE source, so a sick Basri cannot activate.
    // Before TapTokenSourceLive, the human-play menu offered it (autonomous play prunes Basri's exert
    // activation anyway -- OffersExertTokenActivation -- so no autonomous game changes).
    {
        GameState s = BoardSp(20, 1, /*speaker=*/false);
        PutSp(s, "Basri, Tomorrow's Champion", 60, false, /*sick=*/true);
        CHECK_FALSE(TapTokenSourceLive(s, *FindSp(s, 60), DefSp("Basri, Tomorrow's Champion")));
        GameState t = BoardSp(20, 1, false);
        PutSp(t, "Basri, Tomorrow's Champion", 60, false, /*sick=*/false);
        CHECK(TapTokenSourceLive(t, *FindSp(t, 60), DefSp("Basri, Tomorrow's Champion")));
    }
    // Sliver Hive is a LAND: CanTap is true even the turn it entered, so it stays offered exactly as
    // before ({5} from five Plains, Muscle Sliver for the "control a Sliver" gate).
    {
        GameState s = BoardSp(20, 5, /*speaker=*/false);
        PutSp(s, "Sliver Hive", 70, false, /*sick=*/true);
        PutSp(s, "Muscle Sliver", 71, false, /*sick=*/false);
        CHECK(TapTokenSourceLive(s, *FindSp(s, 70), DefSp("Sliver Hive")));
        CHECK(AnyPlanTapsToken(TurnSolver::EnumerateMainPlans(s, true), "Sliver Hive"));
    }
}

TEST_CASE("Speaker: the {0} cost is payable by both payers and taps nothing")
{
    const ManaCost zero = DefSp(kSpeaker).params.tap_token_cost.value();
    CHECK(zero.ManaValue() == 0);
    // No untapped mana at all, and then two untapped Plains: neither payer may tap anything.
    for (int plains : {0, 2})
    {
        CAPTURE(plains);
        GameState s = BoardSp(27, plains);
        if (plains == 0) { PutSp(s, "Plains", 9, /*tapped=*/true); }
        auto tapped_mask = [](const GameState& g)
        {
            std::vector<bool> m;
            for (const Permanent& p : g.battlefield) { m.push_back(p.tapped); }
            return m;
        };
        GameState roll = s;
        CHECK(TapForCostDirect(roll, zero, /*for_creature=*/true));
        CHECK(tapped_mask(roll) == tapped_mask(s));
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        GameState exec = s;
        ManaPool avail = AvailableManaPool(exec);
        CHECK(MtgTestSeam::TapForCost(eng, exec, zero, avail, /*for_creature=*/true));
        CHECK(tapped_mask(exec) == tapped_mask(s));
    }
}

TEST_CASE("Speaker: the rollout and the executor realise the same activation -- one 4/4 white flying Angel")
{
    const GameState s = BoardSp(27, 1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* line = SoleActivation(plans, kSpeaker);
    REQUIRE_MESSAGE(line != nullptr, "the lone Speaker activation was not enumerated");

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *line, /*is_pre_combat=*/true);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackSp);

    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        REQUIRE(FindSp(*g, kSpeakerNum) != nullptr);
        CHECK(FindSp(*g, kSpeakerNum)->tapped);
        CHECK_FALSE(FindSp(*g, 1)->tapped);              // the {0} tapped no land
        CHECK(g->players[0].life == 27);
        CheckOneAngel(*g);
    }
}

TEST_CASE("Speaker: two Speakers past the gate -- one activation EACH, both searched")
{
    GameState s = BoardSp(27);
    PutSp(s, kSpeaker, kSpeakerNum + 1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans) { if (TapTokenActs(p, kSpeaker) == 2) { both = &p; } }
    REQUIRE_MESSAGE(both != nullptr, "the two-activation plan was not enumerated");
    for (const TurnSolver::Plan& p : plans) { CHECK(TapTokenActs(p, kSpeaker) <= 2); }
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    CHECK(Tokens(roll).size() == 2);
    CHECK(FindSp(roll, kSpeakerNum)->tapped);
    CHECK(FindSp(roll, kSpeakerNum + 1)->tapped);
}

TEST_CASE("Speaker: a stale activation (life fell below the gate) pays nothing and makes nothing, in both worlds")
{
    const GameState at_gate = BoardSp(27, 1);
    const TurnSolver::Plan* line = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(at_gate, true);
    line = SoleActivation(plans, kSpeaker);
    REQUIRE(line != nullptr);

    GameState s = at_gate;
    s.players[0].life = 26;   // the precondition no longer holds when the action applies

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *line, true);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackSp);

    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK_FALSE(FindSp(*g, kSpeakerNum)->tapped);
        CHECK(Tokens(*g).empty());
    }
}

TEST_CASE("Speaker: vigilance + lifelink through the shared combat core -- a Colossus Hammer'd Speaker gains 11")
{
    GameState s = BoardSp(gamesetup::StartingLife());
    PutSp(s, "Colossus Hammer", 60);
    s.battlefield.back().equipped_to = kSpeakerNum;
    RolloutSimulateCombat(s);
    REQUIRE(FindSp(s, kSpeakerNum) != nullptr);
    CHECK_FALSE(FindSp(s, kSpeakerNum)->tapped);       // attacked for 11, vigilance: still untapped
    CHECK(s.players[1].life == 20 - 11);
    CHECK(s.players[0].life == gamesetup::StartingLife() + 11);
    // ...which is exactly the board the restricted post-combat main exists for.
    ArmSp arm(1);
    CHECK(ProviderSecondMainLive(s));
}

TEST_CASE("Speaker m2: opens only past the gate with a live Speaker; its menu is the activation alone")
{
    ArmSp arm(1);
    const int life = gamesetup::StartingLife() + 7;
    // Post-combat board: an untapped Plains and Kor Duelist ({W}) in hand -- a cast the phase must refuse.
    GameState s = BoardSp(life, 1, true, false, false, {"Kor Duelist"});
    REQUIRE(Hammer().RestrictedSecondMainLive(s));
    REQUIRE(ProviderSecondMainLive(s));
    bool saw_activation = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
    {
        for (const Action& a : p.actions) { CHECK(a.kind == Action::Kind::TapForTokenPay); }
        CHECK(p.actions.size() <= 1);
        CHECK(p.land_to_play.empty());
        if (TapTokenActs(p, kSpeaker) == 1) { saw_activation = true; }
    }
    CHECK(saw_activation);

    // Closed below the gate, with a sick / tapped Speaker, with no Speaker, and on a full-main-2 deck.
    CHECK_FALSE(Hammer().RestrictedSecondMainLive(BoardSp(life - 1, 1)));
    CHECK_FALSE(Hammer().RestrictedSecondMainLive(BoardSp(life, 1, true, /*sick=*/true)));
    CHECK_FALSE(Hammer().RestrictedSecondMainLive(BoardSp(life, 1, true, false, /*tapped=*/true)));
    CHECK_FALSE(Hammer().RestrictedSecondMainLive(BoardSp(life, 1, /*speaker=*/false)));
    {
        GameState full = BoardSp(life, 1);
        full.uses_second_main = true;
        CHECK_FALSE(ProviderSecondMainLive(full));
    }
    // The admission is the activation's DEFINITION: a Sliver Hive activation is never admitted.
    {
        Action hive;
        hive.kind      = Action::Kind::TapForTokenPay;
        hive.card_name = "Sliver Hive";
        CHECK_FALSE(Hammer().RestrictedSecondMainAdmits(s, hive));
        Action cast;
        cast.kind      = Action::Kind::CastFromHand;
        cast.card_name = "Kor Duelist";
        CHECK_FALSE(Hammer().RestrictedSecondMainAdmits(s, cast));
    }
}

TEST_CASE("Speaker m2: MTG_SPEAKER_M2=0 never opens it, and post-combat enumeration is then unfiltered")
{
    ArmSp arm(0);
    GameState s = BoardSp(gamesetup::StartingLife() + 7, 1, true, false, false, {"Kor Duelist"});
    CHECK_FALSE(Hammer().RestrictedSecondMainLive(s));
    CHECK_FALSE(ProviderSecondMainLive(s));
}

TEST_CASE("Speaker: the white Angel is a one-colour Dragonfire Blade host (equip {3}); a colourless token is not")
{
    GameState s = BoardSp(27);
    CreateToken(s, 0, 4, 4, {"Angel"}, "W", {"Flying"});
    CreateToken(s, 0, 1, 1, {"Sliver"});
    REQUIRE(Tokens(s).size() == 2);
    const int angel  = Tokens(s)[0]->card.m_number;
    const int sliver = Tokens(s)[1]->card.m_number;
    const CardDefinition& blade = DefSp("Dragonfire Blade");
    CHECK(EquipTargetColorCount(s, 0, angel) == 1);
    CHECK(EquipCostGenericNow(s, 0, blade, angel) == 3);
    CHECK(EquipTargetColorCount(s, 0, sliver) == 0);
    CHECK(EquipCostGenericNow(s, 0, blade, sliver) == 4);
    // A nontoken host still resolves through its definition (Speaker: mono-white).
    CHECK(EquipCostGenericNow(s, 0, blade, kSpeakerNum) == 3);
}
