// Creature Burn Stage 2, batch 2 (2026-11-20), in BOTH apply worlds where a world applies:
//   * Goblin Fireslinger / Vulshok Sorcerer / Cinder Pyromancer -- the free {T} pingers (CardParams::
//     tap_ping_damage): Zhur-Taa Druid's end-step sweep (PingDorkEndSweep) and post-combat lethal finish
//     (PingDorkFinish), x1 in 2HG ("target player"), the 0-power attack hold (DecisionProvider::AttackWith).
//   * Cinder Pyromancer's "whenever you cast a red spell, you may untap this creature" (cast_color_untap_self):
//     a tapped copy untaps, an untapped able one pings IN RESPONSE then untaps (TapPingCastUntap), each such
//     ping recorded as withdrawable (GameState::opt_pings).
//   * Browbeat's PRE-PING sub-decision (Action::browbeat_flip -> BrowbeatFlipPrePings): the other opponent
//     outcome at the least cost, in the rollout (TurnSolver::ApplyPlan) and the executor (the stack entry).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& Cb2Def(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, name);
    return *d;
}
Card Cb2Card(const char* name, int number)
{
    Card c = Cb2Def(name).card;
    c.m_number = number;
    return c;
}
Permanent& Cb2Put(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = Cb2Card(name, number);
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState Cb2Board(int mountains, const std::vector<const char*>& hand, int opp_life = 20)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_tap_ping   = true;   // StampDeckTraits' stamp for a list with a tap pinger
    for (int k = 0; k < mountains; ++k) { Cb2Put(s, "Mountain", 70 + k); }
    int n = 1;
    for (const char* h : hand) { s.players[0].hand.push_back(Cb2Card(h, n++)); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(Cb2Card("Mountain", 40 + k)); }
    return s;
}
Action Cb2Cast(const char* name, int flip = 0)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &Cb2Def(name);
    a.hand_index     = -1;
    a.cost           = Cb2Def(name).card.m_mana_cost;
    a.is_noncreature = !Cb2Def(name).card.IsCreature();
    a.browbeat_flip  = static_cast<std::int8_t>(flip);
    return a;
}
TurnSolver::Plan Cb2Plan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
void Cb2ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
void Cb2ExecCast(AIEngine& eng, GameState& s, const char* name, bool flip = false)
{
    auto& hand = s.players[0].hand;
    auto it = std::find_if(hand.begin(), hand.end(), [&](const Card& c) { return c.m_name == name; });
    REQUIRE(it != hand.end());
    ManaPool avail = AvailableManaPool(s);
    if (flip) { MtgTestSeam::CastBrowbeatFlipFromHand(eng, s, *it, avail); }
    else      { MtgTestSeam::CastSpellFromHand(eng, s, *it, avail, 0); }
    Cb2ResolveAll(s);
}
const Permanent* Cb2Find(const GameState& s, const char* name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == name) { return &p; } }
    return nullptr;
}
struct Cb2Heads
{
    int saved;
    explicit Cb2Heads(int n) : saved(gamesetup::t_setup.opponent_heads) { gamesetup::t_setup.opponent_heads = n; }
    ~Cb2Heads() { gamesetup::t_setup.opponent_heads = saved; }
};
}   // namespace

// ---- parse -----------------------------------------------------------------------------------------------

TEST_CASE("Tap pingers: parsed with the dedicated ping params (not a spell `targeting`)")
{
    const CardDefinition& f = Cb2Def("Goblin Fireslinger");
    CHECK(f.params.tap_ping_damage == 1);
    CHECK_FALSE(f.params.tap_ping_any_target);
    CHECK(f.params.cast_color_untap_self == -1);
    CHECK(f.params.targeting == Targeting::None);
    CHECK(f.card.m_mana_cost.ManaValue() == 1);

    const CardDefinition& v = Cb2Def("Vulshok Sorcerer");
    CHECK(v.params.tap_ping_damage == 1);
    CHECK(v.params.tap_ping_any_target);
    CHECK(v.card.HasKeyword(Keyword::Haste));
    CHECK(v.card.m_mana_cost.ManaValue() == 3);

    const CardDefinition& c = Cb2Def("Cinder Pyromancer");
    CHECK(c.params.tap_ping_damage == 1);
    CHECK_FALSE(c.params.tap_ping_any_target);
    CHECK(c.params.cast_color_untap_self == static_cast<int>(Color::Red));
    CHECK(c.card.m_mana_cost.ManaValue() == 3);
    CHECK(c.card.HasColor(Color::Red));
}

// ---- the end-step sweep and the post-combat finish -------------------------------------------------------

TEST_CASE("Tap pingers: the end-step sweep taps every READY pinger once; summoning sickness holds one, haste lifts it")
{
    GameState s = Cb2Board(0, {});
    Cb2Put(s, "Goblin Fireslinger", 11);                 // ready
    Cb2Put(s, "Cinder Pyromancer", 12);                  // ready
    Cb2Put(s, "Vulshok Sorcerer", 13, /*sick=*/true);    // haste: ready
    Cb2Put(s, "Goblin Fireslinger", 14, /*sick=*/true);  // summoning sick: not ready
    CHECK(PingDorkEndSweep(s, 0) == 3);
    CHECK(s.players[1].life == 17);
    CHECK(s.player_dealt_damage_this_turn[1]);
    int tapped = 0;
    for (const Permanent& p : s.battlefield) { if (p.tapped) { ++tapped; } }
    CHECK(tapped == 3);
    CHECK(PingDorkEndSweep(s, 0) == 0);                   // nothing pings twice
    CHECK(s.players[1].life == 17);
}

TEST_CASE("Tap pingers: 2HG -- 'target player' is ONE head, one point of the team's life per ping")
{
    Cb2Heads heads(2);
    GameState s = Cb2Board(0, {}, /*opp_life=*/30);
    Cb2Put(s, "Goblin Fireslinger", 11);
    Cb2Put(s, "Vulshok Sorcerer", 12);
    CHECK(PingDorkEndSweep(s, 0) == 2);
    CHECK(s.players[1].life == 28);
}

TEST_CASE("Tap pingers: the post-combat finish taps them only when their pings alone are lethal")
{
    GameState s = Cb2Board(0, {}, /*opp_life=*/3);
    Cb2Put(s, "Goblin Fireslinger", 11);
    Cb2Put(s, "Cinder Pyromancer", 12);
    PingDorkFinish(s, 0);
    CHECK(s.players[1].life == 3);                        // 2 < 3: kept for the end step
    s.players[1].life = 2;
    PingDorkFinish(s, 0);
    CHECK(s.players[1].life == 0);                        // lethal: fired right after combat damage
}

// ---- Cinder Pyromancer's red-cast untap ------------------------------------------------------------------

TEST_CASE("Cinder Pyromancer: two red casts give two response pings, then the end step a third (both worlds)")
{
    GameState board = Cb2Board(2, {"Lightning Bolt", "Lightning Bolt"});
    Cb2Put(board, "Cinder Pyromancer", 12);

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, Cb2Plan({ Cb2Cast("Lightning Bolt"), Cb2Cast("Lightning Bolt") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 20 - 3 - 3 - 2);
    REQUIRE(Cb2Find(roll, "Cinder Pyromancer") != nullptr);
    CHECK_FALSE(Cb2Find(roll, "Cinder Pyromancer")->tapped);
    CHECK(roll.OptPingsLive() == 2);
    CHECK(PingDorkEndSweep(roll, 0) == 1);
    CHECK(roll.players[1].life == 11);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    Cb2ExecCast(eng, exec, "Lightning Bolt");
    Cb2ExecCast(eng, exec, "Lightning Bolt");
    CHECK(exec.players[1].life == 12);
    CHECK_FALSE(Cb2Find(exec, "Cinder Pyromancer")->tapped);
    CHECK(exec.OptPingsLive() == 2);
}

TEST_CASE("Cinder Pyromancer: a summoning-sick copy cannot ping; a TAPPED copy just untaps")
{
    GameState sick = Cb2Board(1, {"Lightning Bolt"});
    Cb2Put(sick, "Cinder Pyromancer", 12, /*sick=*/true);
    TurnSolver::ApplyPlan(sick, Cb2Plan({ Cb2Cast("Lightning Bolt") }), /*is_pre_combat=*/true);
    CHECK(sick.players[1].life == 17);                    // the Bolt only
    CHECK(sick.OptPingsLive() == 0);

    GameState tapped = Cb2Board(1, {"Lightning Bolt"});
    Cb2Put(tapped, "Cinder Pyromancer", 12).tapped = true;
    TurnSolver::ApplyPlan(tapped, Cb2Plan({ Cb2Cast("Lightning Bolt") }), /*is_pre_combat=*/true);
    CHECK(tapped.players[1].life == 17);                  // no ping in response (it was tapped)
    CHECK_FALSE(Cb2Find(tapped, "Cinder Pyromancer")->tapped);   // ...but it untapped
    CHECK(PingDorkEndSweep(tapped, 0) == 1);              // and pings at the end step
}

TEST_CASE("Cinder Pyromancer: a cast off suspend is a cast (Rift Bolt in the upkeep triggers it)")
{
    GameState s = Cb2Board(0, {});
    Cb2Put(s, "Cinder Pyromancer", 12);
    FireOnCastTriggers(s, Cb2Def("Rift Bolt"), /*mana_spent=*/0);
    CHECK(s.players[1].life == 19);                       // the response ping
    CHECK_FALSE(Cb2Find(s, "Cinder Pyromancer")->tapped);
    // A non-red spell does not trigger it.
    GameState t = Cb2Board(0, {});
    Cb2Put(t, "Cinder Pyromancer", 12);
    CardDefinition colourless = Cb2Def("Rift Bolt");
    colourless.card.m_color_mask = 0;
    FireOnCastTriggers(t, colourless, 0);
    CHECK(t.players[1].life == 20);
}

// ---- the attack hold -------------------------------------------------------------------------------------

TEST_CASE("Tap pingers: a 0-power Pyromancer is held home; a 1-power pinger and a pumped Pyromancer attack")
{
    GameState s = Cb2Board(0, {});
    s.phase = Phase::Combat;
    Cb2Put(s, "Cinder Pyromancer", 12);
    Cb2Put(s, "Goblin Fireslinger", 11);
    const DecisionProvider& prov = ResolveProvider(s);
    CHECK_FALSE(prov.AttackWith(s, *Cb2Find(s, "Cinder Pyromancer")));
    CHECK(prov.AttackWith(s, *Cb2Find(s, "Goblin Fireslinger")));
    for (Permanent& p : s.battlefield) { if (p.card.m_name == "Cinder Pyromancer") { p.temp_power_bonus = 2; } }
    CHECK(prov.AttackWith(s, *Cb2Find(s, "Cinder Pyromancer")));
}

// ---- Browbeat's pre-ping sub-decision --------------------------------------------------------------------

TEST_CASE("Browbeat flip: base TAKE -> fire the fewest pings first and the opponent DECLINES (both worlds)")
{
    // Holding a card, so the opponent takes iff life > 8. At 9 it takes (-> 4); one Fireslinger ping first
    // brings it to 8, and it declines: we draw three.
    GameState board = Cb2Board(3, {"Browbeat", "Lightning Bolt"}, /*opp_life=*/9);
    Cb2Put(board, "Goblin Fireslinger", 11);
    Cb2Put(board, "Goblin Fireslinger", 14);

    GameState base = board;
    TurnSolver::ApplyPlan(base, Cb2Plan({ Cb2Cast("Browbeat") }), /*is_pre_combat=*/true);
    CHECK(base.players[1].life == 4);
    CHECK(base.players[0].hand.size() == 1u);

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, Cb2Plan({ Cb2Cast("Browbeat", 1) }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 8);
    CHECK(roll.players[0].hand.size() == 4u);
    int tapped = 0;
    for (const Permanent& p : roll.battlefield) { if (p.card.m_name == "Goblin Fireslinger" && p.tapped) { ++tapped; } }
    CHECK(tapped == 1);                                   // the FEWEST: one ping, the other stays ready
    CHECK(PingDorkEndSweep(roll, 0) == 1);                // ...and pings at the end step: no damage lost
    CHECK(roll.players[1].life == 7);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    Cb2ExecCast(eng, exec, "Browbeat", /*flip=*/true);
    CHECK(exec.players[1].life == 8);
    CHECK(exec.players[0].hand.size() == 4u);
}

TEST_CASE("Browbeat flip: base DECLINE -> withhold the Pyromancer's response ping and the opponent TAKES (both worlds)")
{
    // Holding a card (threshold 8), opponent at 9. Browbeat is red: the Pyromancer pings in response (9 -> 8), so
    // the base declines (we draw three). The flip is the world in which it did NOT ping: 9 > 8, it takes 5 -> 4.
    GameState board = Cb2Board(3, {"Browbeat", "Lightning Bolt"}, /*opp_life=*/9);
    Cb2Put(board, "Cinder Pyromancer", 12);

    GameState base = board;
    TurnSolver::ApplyPlan(base, Cb2Plan({ Cb2Cast("Browbeat") }), /*is_pre_combat=*/true);
    CHECK(base.players[1].life == 8);
    CHECK(base.players[0].hand.size() == 4u);

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, Cb2Plan({ Cb2Cast("Browbeat", 1) }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 4);
    CHECK(roll.players[0].hand.size() == 1u);
    CHECK(roll.OptPingsLive() == 0);
    CHECK_FALSE(Cb2Find(roll, "Cinder Pyromancer")->tapped);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    Cb2ExecCast(eng, exec, "Browbeat", /*flip=*/true);
    CHECK(exec.players[1].life == 4);
    CHECK(exec.players[0].hand.size() == 1u);
}

TEST_CASE("Browbeat flip: no flip exists -> the variant is the base (too few pingers, nothing to withhold)")
{
    // Opponent at 11 holding a card: two pings are needed to decline, one Fireslinger is ready.
    GameState board = Cb2Board(3, {"Browbeat", "Lightning Bolt"}, /*opp_life=*/11);
    Cb2Put(board, "Goblin Fireslinger", 11);
    GameState a = board, b = board;
    TurnSolver::ApplyPlan(a, Cb2Plan({ Cb2Cast("Browbeat") }), true);
    TurnSolver::ApplyPlan(b, Cb2Plan({ Cb2Cast("Browbeat", 1) }), true);
    CHECK(a.players[1].life == 6);
    CHECK(b.players[1].life == 6);
    CHECK_FALSE(Cb2Find(b, "Goblin Fireslinger")->tapped);
    // Opponent at 5, hand empty after the cast: it declines and there is no response ping to withhold.
    GameState c = Cb2Board(3, {"Browbeat"}, /*opp_life=*/5);
    TurnSolver::ApplyPlan(c, Cb2Plan({ Cb2Cast("Browbeat", 1) }), true);
    CHECK(c.players[1].life == 5);
    CHECK(c.players[0].hand.size() == 3u);
}

TEST_CASE("Browbeat flip: emitted only for a tap-pinger deck with a pinger live, as a distinct plan")
{
    GameState s = Cb2Board(3, {"Browbeat"});
    Cb2Put(s, "Goblin Fireslinger", 11);
    auto flips = [](const GameState& st) {
        int base = 0, flip = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(st, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::CastFromHand || a.card_name != "Browbeat") { continue; }
                (a.browbeat_flip != 0 ? flip : base)++;
            }
        }
        return std::make_pair(base, flip);
    };
    const auto on = flips(s);
    CHECK(on.first > 0);
    CHECK(on.second > 0);
    GameState off = s;
    off.deck_has_tap_ping = false;                        // every other deck
    CHECK(flips(off).second == 0);
    GameState none = Cb2Board(3, {"Browbeat"});           // no pinger anywhere
    CHECK(flips(none).second == 0);
}

// ---- the MANUAL ping (human play only; USER 2026-11-21: "leave the auto-tap, but add the option to manually tap") ----
// HumanPlayActive() is a process-static MTG_HUMAN_PLAY read a unit test cannot switch on, so the human menu itself
// (AppendHumanManualPingPlans: one standalone plan per READY pinger, appended last) and the "tap Pyromancer, cast
// Bolt, tap it again" sequence are pinned by the scenarios (test/scenarios/cb_human_manual_ping_*.json). HERE: the
// autonomous half (the search never emits the mode), the shared readiness gate and resolver the apply runs, and
// the menu-shape helpers the AIEngine segment loop reads.

TEST_CASE("Manual ping: autonomous enumeration never offers a TapPing action (the search keeps its folds)")
{
    GameState s = Cb2Board(2, {"Lightning Bolt"});
    Cb2Put(s, "Goblin Fireslinger", 11);
    Cb2Put(s, "Cinder Pyromancer", 12);
    Cb2Put(s, "Vulshok Sorcerer", 13, /*sick=*/true);   // haste: ready
    REQUIRE_FALSE(HumanPlayActive());
    int pings = 0, plans = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        ++plans;
        if (TurnSolver::IsHumanManualPingPlan(p)) { ++pings; }
        for (const Action& a : p.actions)
        { CHECK_FALSE((a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == PermAbilityMode::TapPing)); }
    }
    CHECK(plans > 0);
    CHECK(pings == 0);
}

TEST_CASE("Manual ping: the activation is live for a READY pinger only (tapped / summoning-sick refused, haste lifts it)")
{
    GameState s = Cb2Board(0, {});
    Cb2Put(s, "Goblin Fireslinger", 11);                       // ready
    Cb2Put(s, "Cinder Pyromancer", 12).tapped = true;          // tapped
    Cb2Put(s, "Goblin Fireslinger", 14, /*sick=*/true);        // summoning sick
    Cb2Put(s, "Vulshok Sorcerer", 13, /*sick=*/true);          // haste
    Cb2Put(s, "Mountain", 15);                                 // not a pinger
    CHECK(PermAbilitySourceLive(s, 0, 11, PermAbilityMode::TapPing));
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 12, PermAbilityMode::TapPing));
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 14, PermAbilityMode::TapPing));
    CHECK(PermAbilitySourceLive(s, 0, 13, PermAbilityMode::TapPing));
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 15, PermAbilityMode::TapPing));
    CHECK_FALSE(PermAbilitySourceLive(s, 1, 11, PermAbilityMode::TapPing));   // not the controller's
    CHECK(PermAbilityTaps(PermAbilityMode::TapPing));                         // {T} is the cost
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapPing, &Cb2Def("Goblin Fireslinger"))
          == "it deals 1 damage to target player or planeswalker");
    CHECK(PermAbilityLabelFor(PermAbilityMode::TapPing, &Cb2Def("Vulshok Sorcerer"))
          == "it deals 1 damage to any target");
}

TEST_CASE("Manual ping: tap Pyromancer, a red cast untaps it, tap it again -- 1 + 1, and the sweep finds it tapped")
{
    GameState s = Cb2Board(0, {});
    Cb2Put(s, "Cinder Pyromancer", 12);
    ApplyPermAbility(s, 0, 12, PermAbilityMode::TapPing);            // the manual ping (no chooser: the face)
    CHECK(s.players[1].life == 19);
    REQUIRE(Cb2Find(s, "Cinder Pyromancer") != nullptr);
    CHECK(Cb2Find(s, "Cinder Pyromancer")->tapped);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 12, PermAbilityMode::TapPing));
    TapPingCastUntap(s, 0, Cb2Def("Lightning Bolt"));                // the red cast's trigger: untap only
    CHECK(s.players[1].life == 19);
    CHECK_FALSE(Cb2Find(s, "Cinder Pyromancer")->tapped);
    CHECK(PermAbilitySourceLive(s, 0, 12, PermAbilityMode::TapPing));
    ApplyPermAbility(s, 0, 12, PermAbilityMode::TapPing);            // tap it again
    CHECK(s.players[1].life == 18);
    CHECK(PingDorkEndSweep(s, 0) == 0);                              // already tapped: the sweep skips it
    CHECK(s.players[1].life == 18);
}

TEST_CASE("Manual ping: the human-menu shape helpers (standalone plan, tagged pass, base-empty test)")
{
    TurnSolver::Plan ping;
    ping.land_decided = true;
    Action a;
    a.kind         = Action::Kind::ActivatePermAbility;
    a.card_name    = "Goblin Fireslinger";
    a.ability_mode = PermAbilityMode::TapPing;
    ping.actions.push_back(a);
    CHECK(TurnSolver::IsHumanManualPingPlan(ping));
    TurnSolver::Plan two = ping;
    two.actions.push_back(Cb2Cast("Lightning Bolt"));
    CHECK_FALSE(TurnSolver::IsHumanManualPingPlan(two));             // never combined with a cast
    TurnSolver::Plan selfping = ping;
    selfping.actions[0].ability_mode = PermAbilityMode::SelfPing;
    CHECK_FALSE(TurnSolver::IsHumanManualPingPlan(selfping));
    TurnSolver::Plan pass;                                           // an ENUMERATED empty plan: untagged
    CHECK_FALSE(TurnSolver::IsHumanSynthPassPlan(pass));
    CHECK(TurnSolver::HumanMenuBaseEmpty({}));
    CHECK(TurnSolver::HumanMenuBaseEmpty({ ping, ping }));
    CHECK_FALSE(TurnSolver::HumanMenuBaseEmpty({ pass, ping }));     // a real (enumerated) pass is a base entry
    CHECK_FALSE(TurnSolver::HumanMenuBaseEmpty({ two }));
}
