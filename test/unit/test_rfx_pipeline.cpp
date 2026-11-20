// DAMAGE REFLECTION PIPELINE (core/DamageReflection.h, dmgrx::) -- design step 1, the core with no cards.
// docs/design/damage-reflection-pipeline.md section 2 / section 10 step 1.
//
// A symmetric sweep on an ARMED board (GameState::reflect_armed) is ONE damage event of the shared
// pipeline: marked damage, then ONE state-based-action pass that mirrors the executor's
// GameEngine::CheckStateBasedActions (lethal damage kills, indestructible saves, Equipment detaches;
// the card goes to its OWNER's graveyard, CR 400.3) plus what the pipeline needs inline (an orphaned Aura goes to the
// graveyard in the same pass; death watchers fire once per death). The unarmed board keeps the legacy
// PerformDamageAllCreatures body, pinned at the end.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsRfx()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRfx(const std::string& name)
{
    EnsureCardsRfx();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutRfx(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card              = DefRfx(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

// A creature token exactly as the scenario loader / CreateToken shape it (tokens have no definition).
Permanent& TokenRfx(GameState& s, int number, int controller, int pw, int tg, const std::string& sub)
{
    Permanent p;
    p.card.m_name = std::to_string(pw) + "/" + std::to_string(tg) + " " + sub + " Token";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes  = { sub };
    p.card.m_power     = pw;
    p.card.m_toughness = tg;
    p.card.m_number    = number;
    p.is_token          = true;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

GameState ArmedState()
{
    EnsureCardsRfx();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.reflect_armed       = true;
    return s;
}

const Permanent* Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int GyCount(const GameState& s, int player, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("rfx pipeline: simultaneous kill, indestructible survives, survivors keep marked damage")
{
    GameState s = ArmedState();
    TokenRfx(s, 901, 0, 1, 1, "Goblin");
    TokenRfx(s, 902, 1, 2, 2, "Spirit");
    Permanent& ind = TokenRfx(s, 903, 0, 1, 1, "Construct");
    ind.temp_indestructible = true;
    TokenRfx(s, 904, 0, 3, 3, "Minotaur");
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 2);

    CHECK(Find(s, 901) == nullptr);   // 1/1 dies
    CHECK(Find(s, 902) == nullptr);   // 2/2 dies in the SAME pass (simultaneous, CR 608.2)
    REQUIRE(Find(s, 903) != nullptr); // indestructible: lethal damage does not destroy it
    CHECK(Find(s, 903)->damage == 2);
    REQUIRE(Find(s, 904) != nullptr); // 3/3 survives 2 with the damage marked
    CHECK(Find(s, 904)->damage == 2);
    CHECK(dmgrx::t_frame.depth == 0);
    CHECK(dmgrx::t_frame.stack.empty());
}

TEST_CASE("rfx pipeline: a dead creature goes to its OWNER's graveyard (CR 400.3), whoever controls it")
{
    GameState s = ArmedState();
    Permanent& stolen = PutRfx(s, "Boros Reckoner", 911, /*controller=*/0);
    stolen.owner_index = 1;   // the opponent's card, under our control (a steal)
    dmgrx::Sweep(s, 0, DefRfx("Pyroclasm"), 13);
    CHECK(Find(s, 911) == nullptr);
    CHECK(GyCount(s, 1, "Boros Reckoner") == 1);
    CHECK(GyCount(s, 0, "Boros Reckoner") == 0);
}

TEST_CASE("rfx pipeline: an Aura on a dead host goes to the graveyard in the same pass; Equipment detaches")
{
    GameState s = ArmedState();
    PutRfx(s, "Boros Reckoner", 921);
    Permanent& armor = PutRfx(s, "Ethereal Armor", 922);
    armor.aura_attached_to = 921;
    Permanent& eq = PutRfx(s, "Bonesplitter", 923);
    eq.equipped_to = 921;
    dmgrx::Sweep(s, 0, DefRfx("Pyroclasm"), 13);

    CHECK(Find(s, 921) == nullptr);
    CHECK(Find(s, 922) == nullptr);                    // CR 704.5m, inline
    CHECK(GyCount(s, 0, "Ethereal Armor") == 1);
    REQUIRE(Find(s, 923) != nullptr);                  // the Equipment stays ...
    CHECK(Find(s, 923)->equipped_to == 0);             // ... unattached (CR 301.5c)
}

TEST_CASE("rfx pipeline: death watchers fire exactly once per death (OnCreatureDies, FireOppCreatureDies)")
{
    GameState s = ArmedState();
    // Slimefoot (2/3): "Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent
    // and you gain 1 life." Massacre Wurm (6/5): "Whenever a creature an opponent controls dies, that
    // player loses 2 life." Pyroclasm's 2 kills the four 1/1s and neither watcher.
    PutRfx(s, "Slimefoot, the Stowaway", 931);
    PutRfx(s, "Massacre Wurm", 932);
    TokenRfx(s, 933, 0, 1, 1, "Saproling");
    TokenRfx(s, 934, 0, 1, 1, "Saproling");
    TokenRfx(s, 935, 1, 1, 1, "Spirit");
    TokenRfx(s, 936, 1, 1, 1, "Spirit");
    const int me0 = s.players[0].life, opp0 = s.players[1].life;
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 2);

    REQUIRE(Find(s, 931) != nullptr);
    REQUIRE(Find(s, 932) != nullptr);
    CHECK(s.battlefield.size() == 2);
    CHECK(s.players[0].life == me0 + 2);              // Slimefoot: +1 per Saproling death
    CHECK(s.players[1].life == opp0 - 2 - 2 * 2);     // Slimefoot 1+1, Massacre Wurm 2+2
    CHECK(s.creature_died_this_turn);
}

TEST_CASE("rfx pipeline: an UNARMED board keeps the legacy sweep body byte-for-byte")
{
    GameState s = ArmedState();
    s.reflect_armed = false;
    PutRfx(s, "Boros Reckoner", 941);
    Permanent& armor = PutRfx(s, "Ethereal Armor", 942);
    armor.aura_attached_to = 941;
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 13);
    CHECK(Find(s, 941) == nullptr);
    // The legacy body clears the link and leaves the Aura for the next checkpoint (SweepOrphanedAuras'
    // disclosed approximation) -- unchanged, which is the point of the gate.
    REQUIRE(Find(s, 942) != nullptr);
    CHECK(Find(s, 942)->aura_attached_to == 0);
}

// ---- design step 2: reflectors, the sweepers, Blasphemous Act's cost ------------------------------------

TEST_CASE("rfx step 2: a reflector killed by the same Act still deals 13 (LKI); the opponent dies")
{
    GameState s = ArmedState();
    PutRfx(s, "Mogg Maniac", 951);
    PutRfx(s, "Boros Reckoner", 952);
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 953 + i, 1, 1, 1, "Spirit"); }
    const int me0 = s.players[0].life;
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    CHECK(s.battlefield.empty());                      // every creature died to the 13 ...
    CHECK(s.players[1].life == 20 - 13 - 13);          // ... and BOTH reflections still resolved
    CHECK(s.players[0].life == me0);                   // F1: Reckoner's "any target" never picks our face
    CHECK(dmgrx::t_frame.stack.empty());
}

TEST_CASE("rfx step 2: one trigger per creature per EVENT, for the total (rulings)")
{
    GameState s = ArmedState();
    PutRfx(s, "Mogg Maniac", 961);
    dmgrx::Batch b;
    b.src.push_back({ DefRfx("Pyroclasm").card, 0, false });
    b.src.push_back({ DefRfx("Blasphemous Act").card, 0, false });
    b.hits.push_back({ 0, { false, 961 }, 2, false, false });
    b.hits.push_back({ 1, { false, 961 }, 3, false, false });   // two sources, ONE event
    dmgrx::DealDamageBatch(s, b);
    CHECK(Find(s, 961) == nullptr);
    CHECK(s.players[1].life == 20 - 5);                // one trigger, "that much" = 2 + 3
}

TEST_CASE("rfx step 2: Screaming Nemesis's reflection locks the damaged player's lifegain for the game")
{
    GameState s = ArmedState();
    PutRfx(s, "Screaming Nemesis", 971);
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    CHECK(Find(s, 971) == nullptr);                    // it died, and its trigger still resolved
    CHECK(s.players[1].life == 20 - 13);
    CHECK(s.cant_gain_life_rest_of_game_mask == 0b10); // the opponent's bit only
    OpponentGainsLife(s, 0, 4);                         // e.g. Swords to Plowshares' gain
    CHECK(s.players[1].life == 20 - 13);               // ... does not happen, after Nemesis is gone
    GainLife(s, 0, 3);
    CHECK(s.players[0].life == 23);                    // we were never dealt damage: we still gain
}

TEST_CASE("rfx step 2: Chain Reaction's X is the creature count AT RESOLUTION, both sides")
{
    GameState s = ArmedState();
    PutRfx(s, "Mogg Maniac", 981);
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 982 + i, 1, 1, 1, "Spirit"); }
    const CardDefinition& chain = DefRfx("Chain Reaction");
    CHECK(SweepDamageAmount(s, chain) == 5);
    PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));
    CHECK(s.battlefield.empty());
    CHECK(s.players[1].life == 20 - 5);                // the Maniac was dealt 5 and dealt 5
    CHECK(SweepDamageAmount(s, DefRfx("Pyroclasm")) == 2);   // a printed N is unchanged
}

TEST_CASE("rfx step 2: Blasphemous Act costs {1} less per creature on the battlefield, floor {R}")
{
    GameState s = ArmedState();
    const CardDefinition& act = DefRfx("Blasphemous Act");
    ManaCost c = EffectiveSpellCost(act, s);
    CHECK(c.generic == 8);
    CHECK(c.red == 1);
    PutRfx(s, "Mogg Maniac", 991);
    PutRfx(s, "Boros Reckoner", 992);
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 993 + i, 1, 1, 1, "Spirit"); }   // the opponent's count too
    c = EffectiveSpellCost(act, s);
    CHECK(c.generic == 2);                             // 6 creatures -> {2}{R} (design scenario)
    CHECK(c.red == 1);
    for (int i = 0; i < 6; ++i) { TokenRfx(s, 1000 + i, 1, 1, 1, "Spirit"); }  // 12 creatures
    c = EffectiveSpellCost(act, s);
    CHECK(c.generic == 0);                             // "can't reduce the total cost below {R}"
    CHECK(c.red == 1);
    CHECK(c.ManaValue() == 1);
    CHECK(act.card.m_mana_cost.ManaValue() == 9);     // the mana VALUE is the printed cost (ruling)
}

TEST_CASE("rfx step 2: the Act's price is locked when it is priced -- a later board change does not move it")
{
    // "The total cost to cast Blasphemous Act is locked in before you pay that cost": the engine prices
    // a cast with EffectiveSpellCost on the board as it stands, BEFORE any mana ability is activated, and
    // pays that bill. A creature that leaves (sacrificed for mana) or a Spirit created while paying
    // (a Forbidden Orchard tap) changes a LATER pricing, never the bill already computed.
    GameState s = ArmedState();
    const CardDefinition& act = DefRfx("Blasphemous Act");
    for (int i = 0; i < 3; ++i) { TokenRfx(s, 1010 + i, 0, 1, 1, "Goblin"); }
    const ManaCost bill = EffectiveSpellCost(act, s);
    CHECK(bill.generic == 5);                          // the ruling's own example: three creatures -> {5}{R}
    s.battlefield.pop_back();                          // ... one is sacrificed while paying
    TokenRfx(s, 1020, 1, 1, 1, "Spirit");              // ... and an Orchard tap makes a Spirit
    TokenRfx(s, 1021, 1, 1, 1, "Spirit");
    CHECK(bill.generic == 5);                          // the bill already computed is what is paid
    CHECK(EffectiveSpellCost(act, s).generic == 4);    // only a NEW pricing would see the new board
}

TEST_CASE("rfx step 2: an UNARMED deck's reflector is a vanilla creature (Minotaur's Boros Reckoner)")
{
    GameState s = ArmedState();
    s.reflect_armed = false;
    PutRfx(s, "Boros Reckoner", 1031);
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 13);
    CHECK(Find(s, 1031) == nullptr);
    CHECK(s.players[1].life == 20);                    // the legacy body raises no trigger
}

// ---- design step 3: the SEARCHED reflection-target axis (dmgrx::t_pins, Plan::reflect_targets) ----------

namespace
{
// Chain Reaction X = 2 on Boros Reckoner (#1101) + one opponent Spirit (#1102): the Spirit dies, the
// Reckoner survives with 2 marked, and its "any target" trigger has TWO legal targets -- the opponent's
// face (-2) and the Reckoner itself (F1 folds our own face). The step-2 tripwire case.
GameState ChainOnSurvivingReckoner()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 4; ++i) { PutRfx(s, "Mountain", 1110 + i); }
    PutRfx(s, "Boros Reckoner", 1101).entered_this_turn = true;   // summoning-sick: no attack line
    TokenRfx(s, 1102, 1, 1, 1, "Spirit");
    Card chain = DefRfx("Chain Reaction").card;
    chain.m_number = 1120;
    s.players[0].hand.push_back(chain);
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1130 + i; s.players[0].library.push_back(m); }
    return s;
}
void ResolveStackRfx(GameState& s)
{
    while (!s.stack.empty())
    {
        const StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(s, e, *d); }
    }
}
}   // namespace

TEST_CASE("rfx step 3: a CHOICE takes its pin; a singleton takes no ordinal; no pin = the front")
{
    const CardDefinition& chain = DefRfx("Chain Reaction");
    {   // no pins installed: the front (the opponent's face) -- the beyond-horizon / executor default
        GameState s = ChainOnSurvivingReckoner();
        PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));
        REQUIRE(Find(s, 1101) != nullptr);
        CHECK(Find(s, 1101)->damage == 2);
        CHECK(s.players[1].life == 18);
    }
    {   // pin the Reckoner itself: dealt 2 more (4 >= 3) -> it dies; its NEW trigger (LKI) has only the
        // face left -- a singleton, so it consumes no ordinal -- and the 2 still reach the opponent
        GameState s = ChainOnSurvivingReckoner();
        const std::vector<int> pins{ 1101 };
        dmgrx::PinScope scope(pins);
        PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));
        CHECK(Find(s, 1101) == nullptr);
        CHECK(GyCount(s, 0, "Boros Reckoner") == 1);
        CHECK(s.players[1].life == 18);
        CHECK(dmgrx::t_pin_cursor == 1);
    }
    {   // a pin that is not legal falls back to the front
        GameState s = ChainOnSurvivingReckoner();
        const std::vector<int> pins{ 9999 };
        dmgrx::PinScope scope(pins);
        PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));
        REQUIRE(Find(s, 1101) != nullptr);
        CHECK(s.players[1].life == 18);
    }
    {   // the probe sees the choice's full list, front first
        GameState s = ChainOnSurvivingReckoner();
        const std::vector<int> pins;
        std::vector<std::vector<int>> seen;
        dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { seen.push_back(legal); };
        dmgrx::TargetProbeScope probing(probe, pins, 0);
        dmgrx::PinScope scope(pins);
        PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));
        REQUIRE(seen.size() == 1);
        CHECK(seen[0] == std::vector<int>{ dmgrx::PlayerId(1), 1101 });
    }
}

TEST_CASE("rfx step 3: the search BRANCHES a reflection choice (no in-window pick, no tripwire)")
{
    const GameState s = ChainOnSurvivingReckoner();
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<std::vector<int>> pinned;
    for (const auto& p : plans)
    {
        const bool casts_chain = std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
            { return a.card_name.str() == "Chain Reaction"; });
        if (casts_chain) { pinned.insert(p.reflect_targets); }
        else             { CHECK(p.reflect_targets.empty()); }   // no sweeper -> no probe, no pin
    }
    // Exactly the two admitted targets, each its own plan: the face, and the Reckoner itself (whose own
    // re-trigger then has the face alone -- a singleton, not a second pin).
    CHECK(pinned == std::set<std::vector<int>>{ { dmgrx::PlayerId(1) }, { 1101 } });
}

TEST_CASE("rfx step 3: every pinned branch replays identically in the executor (lockstep)")
{
    const GameState initial = ChainOnSurvivingReckoner();
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    int checked = 0;
    for (const auto& p : plans)
    {
        if (p.reflect_targets.empty()) { continue; }
        INFO("reflect pin ", p.reflect_targets.front());
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life == roll.players[1].life);
        CHECK(exec.players[0].life == roll.players[0].life);
        const bool roll_alive = Find(roll, 1101) != nullptr, exec_alive = Find(exec, 1101) != nullptr;
        CHECK(exec_alive == roll_alive);
        CHECK(roll_alive == (p.reflect_targets.front() != 1101));   // the pin is what decided it
        if (roll_alive && exec_alive) { CHECK(Find(exec, 1101)->damage == Find(roll, 1101)->damage); }
        ++checked;
    }
    CHECK(checked == 2);
}

// ---- STEP 4: Stuffy Doll (design 4.6) ---------------------------------------------------------------

namespace
{
// A Doll on the battlefield (chosen player set as the as-enters replacement would) plus `lands` Mountains.
GameState DollBoard(int chosen, bool sick, int lands)
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < lands; ++i) { PutRfx(s, "Mountain", 1210 + i); }
    Permanent& d = PutRfx(s, "Stuffy Doll", 1201);
    d.chosen_player     = static_cast<int8_t>(chosen);
    d.entered_this_turn = sick;
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1230 + i; s.players[0].library.push_back(m); }
    return s;
}
bool PlanPings(const TurnSolver::Plan& p)
{
    return std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
        { return a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SelfPing; });
}
}   // namespace

TEST_CASE("rfx step 4: Stuffy Doll's card data (oracle shape, params, arming)")
{
    const CardDefinition& d = DefRfx("Stuffy Doll");
    CHECK(d.card.IsCreature());
    CHECK(d.card.HasType(CardType::Artifact));
    CHECK(d.card.m_power == 0);
    CHECK(d.card.m_toughness == 1);
    CHECK(d.params.etb_choose_player);
    CHECK(d.params.reflect_dealt_damage == CardParams::ReflectTo::ChosenPlayer);
    CHECK(d.params.self_ping_damage == 1);
    REQUIRE(d.params.self_ping_cost.has_value());
    CHECK(d.params.self_ping_cost->ManaValue() == 0);
    CHECK(dmgrx::DefCanDamageCreatures(d));      // the fan's pre-check sees the self-ping route
    CHECK_FALSE(dmgrx::DefReflectsWithChoice(d)); // a fixed recipient is never a choice
}

TEST_CASE("rfx step 4: a summoning-sick Doll offers no ping; a settled untapped one does")
{
    {
        const GameState s = DollBoard(1, /*sick=*/true, 0);
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const auto& p : plans) { CHECK_FALSE(PlanPings(p)); }
    }
    {
        const GameState s = DollBoard(1, /*sick=*/false, 0);
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(std::any_of(plans.begin(), plans.end(), PlanPings));   // the activation is offered
        for (const auto& p : plans) { CHECK(p.reflect_targets.empty()); }   // never a reflection pin
        // "Don't activate" must stay reachable. The main-1 walk skips the empty selection, so with the drop
        // spent the enumeration's ONLY plan here is the ping; the hosts append the HOLD pass (keep the Doll
        // untapped for later in the turn) -- step 5's root cause of the assertion step 4 dropped.
        CHECK(std::all_of(plans.begin(), plans.end(), PlanPings));
        CHECK(TurnSolver::HoldPassAppendedForTest(s, plans));
        GameState tapped = s;
        for (Permanent& p : tapped.battlefield) { if (p.card.m_number == 1201) { p.tapped = true; } }
        CHECK_FALSE(TurnSolver::HoldPassAppendedForTest(tapped, plans));   // nothing to hold: no extra line
    }
}

TEST_CASE("rfx step 4: the Doll's {T} deals 1 to itself and its reflection deals 1 to the CHOSEN player")
{
    {
        GameState s = DollBoard(/*chosen=*/1, false, 0);
        ApplyPermAbility(s, 0, 1201, PermAbilityMode::SelfPing);
        REQUIRE(Find(s, 1201) != nullptr);            // indestructible: 1 on a 0/1 does not kill it
        CHECK(Find(s, 1201)->damage == 1);
        CHECK(s.players[1].life == 19);
        CHECK(s.players[0].life == 20);
        CHECK(dmgrx::t_frame.stack.empty());
    }
    {
        GameState s = DollBoard(/*chosen=*/0, false, 0);  // a Doll that chose its controller (human play)
        ApplyPermAbility(s, 0, 1201, PermAbilityMode::SelfPing);
        CHECK(s.players[0].life == 19);
        CHECK(s.players[1].life == 20);
    }
}

TEST_CASE("rfx step 4: the ping plan replays identically in the executor (lockstep)")
{
    const GameState initial = DollBoard(1, false, 0);
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    int checked = 0;
    for (const auto& p : plans)
    {
        if (!PlanPings(p)) { continue; }
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(roll.players[1].life == 19);
        CHECK(exec.players[1].life == roll.players[1].life);
        ++checked;
    }
    CHECK(checked >= 1);
}

TEST_CASE("rfx step 4: Blasphemous Act / Chain Reaction reflect the FULL amount to the chosen player")
{
    {
        GameState s = DollBoard(1, false, 0);
        for (int i = 0; i < 4; ++i) { TokenRfx(s, 1250 + i, 1, 1, 1, "Spirit"); }
        PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
        REQUIRE(Find(s, 1201) != nullptr);          // indestructible Doll survives the Act
        CHECK(Find(s, 1201)->damage == 13);
        CHECK(s.players[1].life == 20 - 13);         // "if dealt 13, it deals 13" (ruling)
        for (int i = 0; i < 4; ++i) { CHECK(Find(s, 1250 + i) == nullptr); }
    }
    {
        GameState s = DollBoard(1, false, 0);
        for (int i = 0; i < 2; ++i) { TokenRfx(s, 1260 + i, 1, 1, 1, "Spirit"); }
        const CardDefinition& chain = DefRfx("Chain Reaction");
        PerformDamageAllCreatures(s, 0, chain, SweepDamageAmount(s, chain));   // X = 3 creatures
        CHECK(s.players[1].life == 20 - 3);
        REQUIRE(Find(s, 1201) != nullptr);
        CHECK(Find(s, 1201)->damage == 3);
    }
}

TEST_CASE("rfx step 4: the as-enters choice -- the fold takes the opponent; without it the cast is FANNED")
{
    auto board = [](bool payoff_deck)
    {
        GameState s = ArmedState();
        s.phase = Phase::PreCombatMain;
        s.players[0].lands_played_this_turn = 1;
        s.dmg_events_armed = payoff_deck;   // a deck carrying Purity / Tamanoa breaks the fold (design 4.6)
        for (int i = 0; i < 5; ++i) { PutRfx(s, "Mountain", 1310 + i); }
        Card doll = DefRfx("Stuffy Doll").card;
        doll.m_number = 1301;
        s.players[0].hand.push_back(doll);
        for (int i = 0; i < 10; ++i)
        { Card m = DefRfx("Mountain").card; m.m_number = 1330 + i; s.players[0].library.push_back(m); }
        return s;
    };
    auto doll_casts = [](const std::vector<TurnSolver::Plan>& plans)
    {
        std::set<int> xs;
        for (const auto& p : plans)
            for (const Action& a : p.actions)
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Stuffy Doll") { xs.insert(a.chosen_x); }
        return xs;
    };
    {
        const GameState s = board(false);
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(doll_casts(plans) == std::set<int>{ 0 });   // the SOUND fold: unpinned, no fan
        for (const auto& p : plans)
        {
            if (doll_casts({ p }).empty()) { continue; }
            GameState roll = s;
            TurnSolver::ApplyPlan(roll, p, true);
            const Permanent* d = Find(roll, 1301);
            REQUIRE(d != nullptr);
            CHECK(d->chosen_player == 1);                // the opponent
        }
    }
    {
        const GameState s = board(true);
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(doll_casts(plans) == std::set<int>{ 1, 2 });   // both players, searched
        for (const auto& p : plans)
        {
            const std::set<int> xs = doll_casts({ p });
            if (xs.empty()) { continue; }
            GameState roll = s;
            TurnSolver::ApplyPlan(roll, p, true);
            const Permanent* d = Find(roll, 1301);
            REQUIRE(d != nullptr);
            CHECK(d->chosen_player == (*xs.begin() == 2 ? 0 : 1));
        }
    }
}

TEST_CASE("rfx step 4: human play is ASKED the chosen player, players only, opponent preselected")
{
    GameState s = ArmedState();
    PutRfx(s, "Stuffy Doll", 1401);
    std::vector<int> seen_legal;
    int seen_default = -2;
    TargetChooser chooser = [&](const GameState&, const CardDefinition& def, int, int, int,
                                const std::vector<ChosenTarget>& dflt) -> std::vector<ChosenTarget>
    {
        CHECK(def.params.etb_choose_player);
        REQUIRE(dmgrx::t_viewer_legal != nullptr);
        seen_legal = *dmgrx::t_viewer_legal;
        if (dflt.size() == 1) { seen_default = dflt[0].index; }
        return { ChosenTarget{ 0, 0, 0 } };   // the human picks THEMSELF
    };
    TargetChooser* saved = g_play_target_chooser;
    g_play_target_chooser = &chooser;
    dmgrx::EtbChoosePlayer(s, 0, 0, DefRfx("Stuffy Doll"), 0);
    g_play_target_chooser = saved;
    CHECK(seen_legal == std::vector<int>{ dmgrx::PlayerId(1), dmgrx::PlayerId(0) });
    CHECK(seen_default == 1);
    CHECK(s.battlefield[0].chosen_player == 0);
    // Locked per object: a second call (no re-entry) does not ask again or change it.
    dmgrx::EtbChoosePlayer(s, 0, 0, DefRfx("Stuffy Doll"), 2);
    CHECK(s.battlefield[0].chosen_player == 0);
}

// ---- STEP 5: Guilty Conscience + loops (design 3, 4.4) --------------------------------------------------

namespace
{
// Attach a Guilty Conscience (m_number `num`) to the permanent numbered `host`.
Permanent& AttachGc(GameState& s, int num, int host)
{
    Permanent& g = PutRfx(s, "Guilty Conscience", num);
    g.aura_attached_to = host;
    return g;
}
}   // namespace

TEST_CASE("rfx step 5: Guilty Conscience's card data (aura, param, arming route)")
{
    const CardDefinition& d = DefRfx("Guilty Conscience");
    CHECK(d.card.HasType(CardType::Enchantment));
    CHECK(d.params.is_aura);
    CHECK(d.params.aura_host_damage_back);
    CHECK(dmgrx::DefCanDamageCreatures(d));   // the fan's pre-check sees the GC route
    CHECK_FALSE(dmgrx::DefReflectsWithChoice(d));
}

TEST_CASE("rfx step 5: Doll + GC + one ping kills from 20 (and from 30, the 2HG team pool) and stops at the kill")
{
    for (int life : { 20, 30 })
    {
        INFO("opponent life ", life);
        GameState s = DollBoard(/*chosen=*/1, false, 0);
        s.players[1].life = life;
        AttachGc(s, 1260, 1201);
        ApplyPermAbility(s, 0, 1201, PermAbilityMode::SelfPing);
        CHECK(s.players[1].life == 0);                 // exactly to 0: the loop stops AT the kill
        CHECK(s.players[0].life == 20);
        REQUIRE(Find(s, 1201) != nullptr);             // indestructible
        CHECK(Find(s, 1201)->damage == life);          // the ping's 1, then one GC point per reflected point
        CHECK(dmgrx::t_last_drain.stop == dmgrx::DrainStop::GameOver);
        // 2 resolutions per point after the ping (reflection, GC), far below the 65,536 cap.
        CHECK(dmgrx::t_last_drain.resolutions <= 2 * life + 1);
        CHECK(dmgrx::t_frame.stack.empty());
    }
    {   // a Doll that chose US: the same loop runs against our own life (legal; only a human can choose it)
        GameState s = DollBoard(/*chosen=*/0, false, 0);
        AttachGc(s, 1260, 1201);
        ApplyPermAbility(s, 0, 1201, PermAbilityMode::SelfPing);
        CHECK(s.players[1].life == 20);
        CHECK(s.players[0].life <= 0);
    }
}

TEST_CASE("rfx step 5: GC on a NON-indestructible reflector ends when the creature dies")
{
    GameState s = ArmedState();
    PutRfx(s, "Boros Reckoner", 1301);                 // 3/3, any target (front = the opponent's face)
    AttachGc(s, 1302, 1301);
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 1);
    // dealt 1 -> 1 to the face -> GC 1 back -> ... -> dealt 3 total: dies, GC to the graveyard with it,
    // its LAST (LKI) reflection still deals 1, and with GC gone nothing triggers again.
    CHECK(s.players[1].life == 20 - 3);
    CHECK(Find(s, 1301) == nullptr);
    CHECK(Find(s, 1302) == nullptr);
    CHECK(GyCount(s, 0, "Guilty Conscience") == 1);
    CHECK(GyCount(s, 0, "Boros Reckoner") == 1);
    CHECK(dmgrx::t_last_drain.stop == dmgrx::DrainStop::Drained);
}

TEST_CASE("rfx step 5: combat damage by a GC-enchanted attacker triggers GC (armed only)")
{
    for (const char* name : { "Boros Reckoner", "Screaming Nemesis" })
    {
        INFO(name);
        GameState s = ArmedState();
        s.phase = Phase::Combat;
        PutRfx(s, name, 1401);
        AttachGc(s, 1402, 1401);
        ResolveCombatDamage(s, { 0 }, 0, false);
        // 3 combat damage; GC deals 3 back; the 3/3 dies (GC with it); its LKI reflection deals 3 to the face.
        CHECK(s.players[1].life == 20 - 6);
        CHECK(Find(s, 1401) == nullptr);
        CHECK(GyCount(s, 0, "Guilty Conscience") == 1);
    }
    {   // unarmed (a deck with neither a reflector nor GC): combat is the legacy body, GC never fires
        GameState s = ArmedState();
        s.reflect_armed = false;
        s.phase = Phase::Combat;
        PutRfx(s, "Boros Reckoner", 1401);
        AttachGc(s, 1402, 1401);
        ResolveCombatDamage(s, { 0 }, 0, false);
        CHECK(s.players[1].life == 17);
        CHECK(Find(s, 1401) != nullptr);
    }
}

TEST_CASE("rfx step 5: a loop that cannot progress stops by the NO-PROGRESS rule, not the cap")
{
    // An indestructible Boros Reckoner with GC, every reflection pinned at ITSELF: each resolution deals
    // damage to the Reckoner only -- no life changes, nothing dies. Constructed (the search's pins could
    // offer it; the face is always also offered).
    GameState s = ArmedState();
    Permanent& r = PutRfx(s, "Boros Reckoner", 1501);
    r.temp_indestructible = true;
    AttachGc(s, 1502, 1501);
    const std::vector<int> pins(4096, 1501);
    {
        dmgrx::PinScope scope(pins);
        PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 1);
    }
    CHECK(dmgrx::t_last_drain.stop == dmgrx::DrainStop::NoProgress);
    CHECK(dmgrx::t_last_drain.resolutions == dmgrx::kNoProgressLimit);
    CHECK(s.players[1].life == 20);
    CHECK(s.players[0].life == 20);
    CHECK(dmgrx::t_frame.stack.empty());
    // ...while a PROGRESSING loop never trips it: the Doll test above ends by GameOver after 2L resolutions
    // with every second one idle.
}

TEST_CASE("rfx step 5: the GC host is SEARCHED -- the plan 'GC on the Doll, then ping' kills, and replays in the executor")
{
    GameState initial = DollBoard(1, false, 0);
    PutRfx(initial, "Plains", 1220);
    PutRfx(initial, "Mogg Maniac", 1221);              // a second legal host
    Card gc = DefRfx("Guilty Conscience").card; gc.m_number = 1260;
    initial.players[0].hand.push_back(gc);
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    std::set<int> hosts;
    int killing = 0;
    for (const auto& p : plans)
    {
        for (const Action& a : p.actions) { if (a.card_name.str() == "Guilty Conscience") { hosts.insert(a.enchant_target); } }
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        if (roll.players[1].life > 0) { continue; }
        ++killing;
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life <= 0);
    }
    CHECK(hosts.size() >= 2);    // one cast variant per legal host: the Doll AND the Maniac
    CHECK(killing >= 1);         // the Doll host + ping ordering is enumerated
}

// ---- STEP 5b: the combat-time reflection-target choice is SEARCHED (design section 14) -----------------

namespace
{
// A settled Boros Reckoner with GC attacks; a summoning-sick Stuffy Doll (chosen: the opponent, so its {T}
// line is closed) also carries a GC. Combat: 3 to the face, GC deals 3 back, the Reckoner dies and its LKI
// reflection of 3 is a CHOICE -- the face (opponent at 14) or the GC'd Doll (a loop that kills).
GameState CombatChoiceBoard()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    PutRfx(s, "Boros Reckoner", 1601);
    AttachGc(s, 1602, 1601);
    Permanent& d = PutRfx(s, "Stuffy Doll", 1603);
    d.chosen_player     = 1;
    d.entered_this_turn = true;
    AttachGc(s, 1604, 1603);
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1630 + i; s.players[0].library.push_back(m); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 5b: the combat suffix rides the state; the main-phase apply never reads it")
{
    const std::vector<int> v{ 7, dmgrx::kCombatPinMark, dmgrx::PlayerId(1), 1603 };
    CHECK(dmgrx::MainPinCount(v) == 1);
    CHECK(dmgrx::HasCombatPins(v));
    CHECK(dmgrx::CombatPinsOf(v) == std::vector<int>{ dmgrx::PlayerId(1), 1603 });
    CHECK_FALSE(dmgrx::HasCombatPins({ 7 }));
    TurnSolver::Plan p;
    p.reflect_targets = v;
    GameState s = ArmedState();
    TurnSolver::WritePostApplyStatePins(s, p);
    CHECK(s.scripted_combat_reflect == std::vector<int>{ dmgrx::PlayerId(1), 1603 });
    {   // a pin scope over the packed vector exposes only the prefix
        dmgrx::PinScope scope(v);
        CHECK(dmgrx::t_pins_n == 1);
    }
}

TEST_CASE("rfx step 5b: the search BRANCHES the combat-time reflection target, and the Doll line kills")
{
    const GameState s = CombatChoiceBoard();
    // Nothing to cast: the main-phase list is just the pass (the hosts append it), so feed the axes that.
    TurnSolver::Plan pass;
    pass.land_decided = true;
    const auto plans = TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { pass });
    REQUIRE_FALSE(plans.empty());
    std::set<std::vector<int>> combat_pins;
    for (const auto& p : plans)
    {
        CHECK(dmgrx::HasCombatPins(p.reflect_targets));   // no plan's combat choice is left to the front
        combat_pins.insert(dmgrx::CombatPinsOf(p.reflect_targets));
    }
    // Both admitted targets are branched: the opponent's face and the GC'd Doll. (Our own face is F1-folded.)
    // Step 9c's HOLD of the GC'd Reckoner is not here: since step 9e WHO may stay home is the provider's
    // MayDeclineAttack, and this board runs the generic provider (default: nobody). The step-9c/9e cases check it.
    CHECK(combat_pins == std::set<std::vector<int>>{ { dmgrx::PlayerId(1) }, { 1603 } });
    int doll_lines = 0, face_lines = 0;
    for (const auto& p : plans)
    {
        const std::vector<int> pins = dmgrx::CombatPinsOf(p.reflect_targets);
        if (dmgrx::IsAttackHoldCode(pins.front())) { continue; }
        INFO("combat pin ", pins.front());
        // The rollout: the plan's apply, then the combat the scorer runs.
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        CHECK(roll.scripted_combat_reflect == pins);
        RolloutSimulateCombat(roll);
        CHECK(roll.scripted_combat_reflect.empty());   // consumed
        if (pins.front() == 1603) { ++doll_lines; CHECK(roll.players[1].life <= 0); }
        else                      { ++face_lines; CHECK(roll.players[1].life == 14); }
        // The executor: the committed plan, then GameEngine's real combat -- the same pin, the same result.
        GameState exec = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.scripted_combat_reflect == pins);
        exec.phase = Phase::Combat;
        GameEngine ge(eng);
        ge.PlayOutFrom(exec, exec.turn_number, GameEngine::ResumeAt::Combat);
        CHECK((exec.players[1].life <= 0) == (roll.players[1].life <= 0));
        if (roll.players[1].life > 0) { CHECK(exec.players[1].life == roll.players[1].life); }
    }
    CHECK(doll_lines >= 1);
    CHECK(face_lines >= 1);
}

TEST_CASE("rfx step 5b: no GC anywhere -> no combat probe, no combat pin (the pre-check)")
{
    GameState s = CombatChoiceBoard();
    s.battlefield.erase(std::remove_if(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& q)
                        { return q.card.m_name.str() == "Guilty Conscience"; }), s.battlefield.end());
    TurnSolver::Plan pass;
    pass.land_decided = true;
    const auto plans = TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { pass });
    CHECK(plans.size() == 1);
    for (const auto& p : plans) { CHECK(p.reflect_targets.empty()); }
}

// ---- STEP 6: Spiteful Sliver -- a GRANTED reflection, one instance per granter (design 2.2 step 3, 4.1) ----

TEST_CASE("rfx step 6: Spiteful Sliver's card data (a grant, not an own reflection; arms; a choice kind)")
{
    const CardDefinition& d = DefRfx("Spiteful Sliver");
    CHECK(d.card.IsCreature());
    CHECK(d.card.m_subtypes.size() == 1);
    CHECK(d.card.m_power == 2);
    CHECK(d.card.m_toughness == 2);
    CHECK(d.params.reflect_dealt_damage == CardParams::ReflectTo::None);
    CHECK(d.params.grants_reflect_to_subtype == CardParams::ReflectTo::TargetPlayer);
    CHECK(d.params.subtypes_affected == std::vector<std::string>{ "Sliver" });
    CHECK(dmgrx::DefReflectsWithChoice(d));        // the fan's pre-check sees the granter
    CHECK_FALSE(dmgrx::DefCanDamageCreatures(d));  // "target player": no new route to a creature
}

TEST_CASE("rfx step 6: two Spiteful Slivers under Blasphemous Act -- every Sliver reflects TWICE, LKI grants")
{
    // CR 113.2c: each instance of the granted trigger triggers separately. Both Spiteful Slivers die to the
    // same Act and STILL grant for that event (the snapshot is taken before SBA). Our three Slivers (two
    // Spiteful + a Sliver token) each reflect 13 twice: 3 x 2 x 13 = 78. Not granted: our Goblin token (not
    // a Sliver) and the OPPONENT's Sliver token ("Sliver creatures YOU control").
    GameState s = ArmedState();
    s.players[1].life = 200;
    PutRfx(s, "Spiteful Sliver", 1701);
    PutRfx(s, "Spiteful Sliver", 1702);
    TokenRfx(s, 1703, 0, 1, 1, "Sliver");
    TokenRfx(s, 1704, 0, 1, 1, "Goblin");
    TokenRfx(s, 1705, 1, 1, 1, "Sliver");
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    CHECK(s.battlefield.empty());
    CHECK(s.players[1].life == 200 - 3 * 2 * 13);
    CHECK(s.players[0].life == 20);                // F1: "target player" never picks our own face
    CHECK(dmgrx::t_frame.stack.empty());
    {   // the paper count: two Spiteful + Reckoner under the Act = 2 x 2 x 13 + 13
        GameState t = ArmedState();
        t.players[1].life = 200;
        PutRfx(t, "Spiteful Sliver", 1711);
        PutRfx(t, "Spiteful Sliver", 1712);
        PutRfx(t, "Boros Reckoner", 1713);
        PerformDamageAllCreatures(t, 0, DefRfx("Blasphemous Act"), 13);
        CHECK(t.players[1].life == 200 - 4 * 13 - 13);
    }
}

TEST_CASE("rfx step 6: ONE Spiteful Sliver under Chain Reaction reflects X once")
{
    GameState s = ArmedState();
    PutRfx(s, "Spiteful Sliver", 1721);
    TokenRfx(s, 1722, 1, 1, 1, "Spirit");
    TokenRfx(s, 1723, 1, 1, 1, "Spirit");
    const CardDefinition& chain = DefRfx("Chain Reaction");
    const int x = SweepDamageAmount(s, chain);
    CHECK(x == 3);
    PerformDamageAllCreatures(s, 0, chain, x);
    CHECK(s.battlefield.empty());
    CHECK(s.players[1].life == 20 - 3);
}

TEST_CASE("rfx step 6: the grant ends when the Spiteful Sliver leaves; a lone Sliver token then reflects nothing")
{
    GameState s = ArmedState();
    PutRfx(s, "Spiteful Sliver", 1731);
    TokenRfx(s, 1732, 0, 3, 3, "Sliver");
    auto ping = [&](int number) {
        dmgrx::Batch b;
        b.src.push_back({ DefRfx("Pyroclasm").card, 0, false });
        b.hits.push_back({ 0, { false, number }, 1, false, false });
        dmgrx::DealDamageBatch(s, b);
    };
    ping(1732);
    CHECK(s.players[1].life == 19);                // granted: one instance
    CHECK(dmgrx::GrantedInstances(s, *Find(s, 1732)) == 1);
    s.battlefield.erase(s.battlefield.begin());    // the Spiteful Sliver leaves
    CHECK(dmgrx::GrantedInstances(s, *Find(s, 1732)) == 0);
    ping(1732);
    CHECK(s.players[1].life == 19);                // no grant, no trigger
    REQUIRE(Find(s, 1732) != nullptr);
    CHECK(Find(s, 1732)->damage == 2);
}

TEST_CASE("rfx step 6: a granted reflection with a real choice reaches the viewer as a REFLECTION prompt")
{
    // Purity lifts F1 (our face is then a real option), so the granted "target player" list is {opp, us}
    // and the resolver asks the chooser -- with the damaged Sliver's def, whose OWN reflection is None, so
    // main.cpp keys on t_viewer_reflection, not on the def.
    GameState s = ArmedState();
    s.dmg_events_armed = true;                     // a Purity deck arms the player-damage events
    PutRfx(s, "Spiteful Sliver", 1741);
    PutRfx(s, "Purity", 1742);
    int asked = 0;
    std::vector<int> seen_legal;
    TargetChooser chooser = [&](const GameState&, const CardDefinition& def, int, int, int,
                                const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget>
    {
        ++asked;
        CHECK(def.card.m_name.str() == "Spiteful Sliver");
        CHECK(dmgrx::t_viewer_reflection);
        REQUIRE(dmgrx::t_viewer_legal != nullptr);
        seen_legal = *dmgrx::t_viewer_legal;
        return { ChosenTarget{ 0, 0, 0 } };        // aim it at OURSELVES (Purity turns it into life)
    };
    TargetChooser* saved = g_play_target_chooser;
    g_play_target_chooser = &chooser;
    dmgrx::Batch b;
    b.src.push_back({ DefRfx("Pyroclasm").card, 0, false });
    b.hits.push_back({ 0, { false, 1741 }, 1, false, false });
    dmgrx::DealDamageBatch(s, b);
    g_play_target_chooser = saved;
    CHECK(asked == 1);
    CHECK_FALSE(dmgrx::t_viewer_reflection);
    CHECK(seen_legal == std::vector<int>{ dmgrx::PlayerId(1), dmgrx::PlayerId(0) });
    CHECK(s.players[1].life == 20);
    CHECK(s.players[0].life == 21);                // the pick was honoured (prevented and gained)
}

namespace
{
// A settled Boros Reckoner with GC attacks beside two summoning-sick Spiteful Slivers. Combat: 3 to the face,
// GC deals 3 back, the Reckoner dies and its LKI reflection of 3 is a CHOICE: the face (opponent at 14), or
// a Spiteful Sliver -- an AMPLIFIER (design 4.1, mult 2): it dies to the 3, and its two instances deal 6.
GameState SliverAmplifierBoard()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    PutRfx(s, "Boros Reckoner", 1801);
    AttachGc(s, 1802, 1801);
    PutRfx(s, "Spiteful Sliver", 1803).entered_this_turn = true;
    PutRfx(s, "Spiteful Sliver", 1804).entered_this_turn = true;
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1830 + i; s.players[0].library.push_back(m); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 6: the search BRANCHES the Reckoner's target over the Slivers; a Sliver beats the face")
{
    const GameState s = SliverAmplifierBoard();
    TurnSolver::Plan pass;
    pass.land_decided = true;
    const auto plans = TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { pass });
    REQUIRE_FALSE(plans.empty());
    std::set<std::vector<int>> combat_pins;
    for (const auto& p : plans)
    {
        CHECK(dmgrx::HasCombatPins(p.reflect_targets));
        combat_pins.insert(dmgrx::CombatPinsOf(p.reflect_targets));
    }
    // Every admitted target is its own line: the face and each Spiteful Sliver. The Slivers' own granted
    // "target player" triggers are singletons after F1 and take no pin.
    // No step-9c HOLD here: the generic provider names nobody under MayDeclineAttack (step 9e).
    CHECK(combat_pins == std::set<std::vector<int>>{ { dmgrx::PlayerId(1) }, { 1803 }, { 1804 } });
    for (const auto& p : plans)
    {
        const std::vector<int> pins = dmgrx::CombatPinsOf(p.reflect_targets);
        if (dmgrx::IsAttackHoldCode(pins.front())) { continue; }
        INFO("combat pin ", pins.front());
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        RolloutSimulateCombat(roll);
        const bool sliver = pins.front() != dmgrx::PlayerId(1);
        CHECK(roll.players[1].life == (sliver ? 20 - 3 - 2 * 3 : 20 - 3 - 3));
        if (sliver) { CHECK(Find(roll, pins.front()) == nullptr); }   // the amplifier died to the 3 ...
        // ... and the other Spiteful is untouched
        CHECK(Find(roll, pins.front() == 1803 ? 1804 : 1803) != nullptr);
        GameState exec = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        exec.phase = Phase::Combat;
        GameEngine ge(eng);
        ge.PlayOutFrom(exec, exec.turn_number, GameEngine::ResumeAt::Combat);
        CHECK(exec.players[1].life == roll.players[1].life);
    }
}

// ---- STEP 7: Loran's Escape (design 4.5 / 9 / 10 step 7) --------------------------------------------------

namespace
{
const Permanent* FindByName(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}
// A board where Loran's Escape is cast in main 1 alongside Blasphemous Act: a summoning-sick Boros Reckoner
// (#1901, no attack line) and a summoning-sick Mogg Maniac (#1902) -- the two NON-inert targets -- plus the
// three no-effect targets (Mind Stone #1903, Stuffy Doll #1904 with its printed indestructible, chosen the
// opponent) and two identical opponent Spirits (#1905, #1906). Five creatures: the Act costs 9 - 5 = {3}{R};
// lands: Plains + 6 Mountains (the Escape's {W} and the Act together).
GameState LoranBoard(bool with_act, bool gc_on_reckoner)
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    // GameState defaults this TRUE (the unstamped-conservative value); this board has no spell-target watcher, so
    // stamp it as a deck build would (otherwise the no-effect fold correctly refuses to fold the Doll).
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 1910);
    for (int i = 0; i < 6; ++i) { PutRfx(s, "Mountain", 1911 + i); }
    PutRfx(s, "Boros Reckoner", 1901).entered_this_turn = true;
    PutRfx(s, "Mogg Maniac", 1902).entered_this_turn = true;
    // Tapped: an untapped Mind Stone is a sac-draw DIG source, whose dig_choice twins the test executor
    // (CommitPhasePlan + TakeTurn) does not realise -- not what these tests measure.
    PutRfx(s, "Mind Stone", 1903).tapped = true;
    Permanent& d = PutRfx(s, "Stuffy Doll", 1904);
    d.chosen_player = 1;
    d.entered_this_turn = true;
    d.tapped = true;   // no ping line: keep the plan set to the Loran's / Act axes
    TokenRfx(s, 1905, 1, 1, 1, "Spirit");
    TokenRfx(s, 1906, 1, 1, 1, "Spirit");
    if (gc_on_reckoner) { AttachGc(s, 1907, 1901); }
    Card lor = DefRfx("Loran's Escape").card; lor.m_number = 1920;
    s.players[0].hand.push_back(lor);
    if (with_act) { Card act = DefRfx("Blasphemous Act").card; act.m_number = 1921; s.players[0].hand.insert(s.players[0].hand.begin(), act); }
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1930 + i; s.players[0].library.push_back(m); }
    return s;
}
int LoranTargetOf(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.card_name.str() == "Loran's Escape") { return a.enchant_target; } }
    return 0;
}
}   // namespace

TEST_CASE("rfx step 7: Loran's Escape's card data (artifact-or-creature target, indestructible, inert hexproof, scry 1)")
{
    const CardDefinition& d = DefRfx("Loran's Escape");
    CHECK(d.card.IsInstant());
    CHECK(d.card.m_mana_cost.ManaValue() == 1);
    CHECK(d.params.solo_target_trick);
    CHECK(d.params.trick_targets_artifact_or_creature);
    CHECK_FALSE(d.params.trick_own_target_only);
    CHECK(d.params.grants_temp_indestructible);
    CHECK(d.params.grants_temp_hexproof);
    CHECK(d.params.cast_scry == 1);
    // Legal set: artifacts and creatures, either side; not lands; not an opponent's Escaped (hexproof) creature.
    GameState s = LoranBoard(false, false);
    for (const Permanent& p : s.battlefield)
    {
        INFO(p.card.m_name.str());
        CHECK(ArtifactOrCreatureTrickTargetLegal(s, 0, p) == !p.card.IsLand());
    }
    Permanent* spirit = nullptr;
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 1905) { spirit = &p; } }
    REQUIRE(spirit != nullptr);
    spirit->temp_indestructible = true;
    CHECK_FALSE(ArtifactOrCreatureTrickTargetLegal(s, 0, *spirit));
}

TEST_CASE("rfx step 7: an Escaped Boros Reckoner survives Blasphemous Act and reflects 13; the scry fires")
{
    GameState s = LoranBoard(false, false);
    s.players[1].life = 60;   // room for all 39: at 20 the drain correctly stops at GameOver (after 26)
    const CardDefinition& lor = DefRfx("Loran's Escape");
    // The scry goes through ScryTop: under a top chooser (the viewer's) it is ASKED, with this card as the source.
    int asked = 0;
    TopChooser chooser = [&](const GameState&, const std::string& source, const std::vector<Card>& looked, LookKind kind)
    {
        ++asked;
        CHECK(source == "Loran's Escape");
        CHECK(looked.size() == 1);
        CHECK(kind == LookKind::Scry);
        return TopDisposition{};   // bottom it
    };
    const int top_before = s.players[0].library.front().m_number;
    g_play_top_chooser = &chooser;
    const bool resolved = ResolveSoloTargetTrick(s, 0, lor, 1901, 0, 0);
    g_play_top_chooser = nullptr;
    CHECK(resolved);
    CHECK(asked == 1);
    CHECK(s.players[0].library[s.players[0].library.size() - 1].m_number == top_before);   // bottomed
    REQUIRE(Find(s, 1901) != nullptr);
    CHECK(Find(s, 1901)->temp_indestructible);
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    REQUIRE(Find(s, 1901) != nullptr);                     // survived 13 (CR 702.12b)
    CHECK(Find(s, 1901)->damage == 13);
    CHECK(Find(s, 1904) != nullptr);                       // the Doll (printed indestructible) too
    CHECK(Find(s, 1902) == nullptr);                       // the Maniac died (LKI reflection still dealt)
    CHECK(Find(s, 1905) == nullptr);
    CHECK(Find(s, 1903) != nullptr);                       // Mind Stone is not a creature
    // Reckoner 13 (front: the face) + Maniac 13 + the Doll 13 (chosen: the opponent) -> 60 - 39.
    CHECK(s.players[1].life == 60 - 39);
    {   // the same Act WITHOUT the Escape: the Reckoner dies
        GameState t = LoranBoard(false, false);
        PerformDamageAllCreatures(t, 0, DefRfx("Blasphemous Act"), 13);
        CHECK(Find(t, 1901) == nullptr);
    }
}

TEST_CASE("rfx step 7: an Escaped Reckoner with Guilty Conscience loops to the kill under one sweep, and stops")
{
    for (int life : { 20, 60 })
    {
        INFO("opponent life ", life);
        GameState s = ArmedState();
        s.players[1].life = life;
        PutRfx(s, "Boros Reckoner", 1950);
        AttachGc(s, 1951, 1950);
        REQUIRE(ResolveSoloTargetTrick(s, 0, DefRfx("Loran's Escape"), 1950, 0, 0));
        PerformDamageAllCreatures(s, 0, DefRfx("Chain Reaction"), 1);   // a ping-sized sweep: X = 1
        CHECK(s.players[1].life <= 0);
        CHECK(s.players[1].life > -1 * 1 - 1);    // stops AT the kill: each cycle moves exactly 1
        CHECK(s.players[0].life == 20);
        REQUIRE(Find(s, 1950) != nullptr);
        CHECK(dmgrx::t_last_drain.stop == dmgrx::DrainStop::GameOver);
        CHECK(dmgrx::t_last_drain.resolutions <= 2 * life + 1);
    }
}

TEST_CASE("rfx step 7: the Escape's target is SEARCHED (folds stated), and every branch replays in the executor")
{
    const GameState initial = LoranBoard(false, false);
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    std::set<int> targets;
    for (const auto& p : plans) { const int t = LoranTargetOf(p); if (t != 0) { targets.insert(t); } }
    // Reckoner and Maniac each; ONE opponent Spirit (the identity fold); ONE no-effect representative (Mind
    // Stone and the Doll are the same scry-only line; own battlefield first, in battlefield order).
    CHECK(targets.count(1901) == 1);
    CHECK(targets.count(1902) == 1);
    CHECK(targets.count(1905) + targets.count(1906) == 1);
    CHECK(targets.count(1903) + targets.count(1904) == 1);
    CHECK(targets.size() == 4);
    int checked = 0;
    for (const auto& p : plans)
    {
        const int t = LoranTargetOf(p);
        if (t == 0) { continue; }
        INFO("Loran's target ", t);
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        REQUIRE(Find(roll, t) != nullptr);
        REQUIRE(Find(exec, t) != nullptr);
        CHECK(Find(roll, t)->temp_indestructible);
        CHECK(Find(exec, t)->temp_indestructible);
        for (const Permanent& q : roll.battlefield)
        {
            const Permanent* e = Find(exec, q.card.m_number);
            REQUIRE(e != nullptr);
            CHECK(e->temp_indestructible == q.temp_indestructible);
        }
        // The scry fired in both worlds the same way (same library order).
        REQUIRE(roll.players[0].library.size() == exec.players[0].library.size());
        for (std::size_t i = 0; i < roll.players[0].library.size(); ++i)
        { CHECK(roll.players[0].library[i].m_number == exec.players[0].library[i].m_number); }
        ++checked;
    }
    CHECK(checked >= 4);
}

TEST_CASE("rfx step 7: the Escape before the Act is a reachable plan, and it keeps the Reckoner")
{
    // The Act sits FIRST in hand: the line must not depend on hand order.
    const GameState initial = LoranBoard(true, false);
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    int found = 0;
    for (const auto& p : plans)
    {
        if (LoranTargetOf(p) != 1901) { continue; }
        const bool act = std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
            { return a.card_name.str() == "Blasphemous Act"; });
        if (!act) { continue; }
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        if (Find(roll, 1901) == nullptr) { continue; }
        // (the Reckoner's own 13 is a searched target too: the face line kills; the drain stops at GameOver)
        if (roll.players[1].life <= 0) { ++found; }
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(Find(exec, 1901) != nullptr);
        CHECK(exec.players[1].life == roll.players[1].life);
    }
    CHECK(found >= 1);
}

TEST_CASE("rfx step 7: Guilty Conscience on the Doll BEFORE the Act is a reachable plan, and it loops to the kill")
{
    // The searched rfx order (design 4.8) covers GC too: GC on the (indestructible) Doll before the Act makes the
    // Doll's 13 come back to it, and the loop kills from 20; after the Act it is one 13. The Act sits FIRST in hand
    // and both are noncreature (CastOrderRank ties them), so before the order axis this line was unreachable.
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 1960);
    for (int i = 0; i < 6; ++i) { PutRfx(s, "Mountain", 1961 + i); }
    Permanent& d = PutRfx(s, "Stuffy Doll", 1970);
    d.chosen_player = 1;
    d.entered_this_turn = true;
    d.tapped = true;   // no ping line
    TokenRfx(s, 1971, 1, 1, 1, "Spirit");
    TokenRfx(s, 1972, 1, 1, 1, "Spirit");
    Card act = DefRfx("Blasphemous Act").card; act.m_number = 1980; s.players[0].hand.push_back(act);
    Card gc = DefRfx("Guilty Conscience").card; gc.m_number = 1981; s.players[0].hand.push_back(gc);
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 1990 + i; s.players[0].library.push_back(m); }
    const GameState initial = s;
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    int before = 0, after = 0;
    for (const auto& p : plans)
    {
        int gc_at = -1, act_at = -1;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const std::string n = p.actions[i].card_name.str();
            if (n == "Guilty Conscience" && p.actions[i].enchant_target == 1970) { gc_at = i; }
            if (n == "Blasphemous Act") { act_at = i; }
        }
        if (gc_at < 0 || act_at < 0) { continue; }
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life == roll.players[1].life);
        if (gc_at < act_at) { ++before; CHECK(roll.players[1].life <= 0); }
        else                { ++after;  CHECK(roll.players[1].life == 20 - 13); }
    }
    CHECK(before >= 1);
    CHECK(after >= 1);
}

// ---- STEP 8: Nomads en-Kor redirection shields (design 2.2 step 1, 4.2, 10 step 8) ---------------------

namespace
{
// Nomads en-Kor(s) #2001.. + Stuffy Doll #2010 (chosen the opponent) + four opponent Spirits; opponent at `opp`.
GameState NomadsBoard(int nomads, int opp)
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    s.players[1].life = opp;
    for (int i = 0; i < nomads; ++i) { PutRfx(s, "Nomads en-Kor", 2001 + i); }
    PutRfx(s, "Stuffy Doll", 2010).chosen_player = 1;
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 2020 + i, 1, 1, 1, "Spirit"); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 8: en-Kor card data (the shared {0} shield param; a chooser, not a damage route)")
{
    for (const char* n : { "Nomads en-Kor", "Shaman en-Kor" })
    {
        const CardDefinition& d = DefRfx(n);
        CHECK(d.params.redirect_shield);
        CHECK(dmgrx::DefOffersShieldChoice(d));
        CHECK_FALSE(dmgrx::DefCanDamageCreatures(d));
        CHECK_FALSE(dmgrx::DefReflectsWithChoice(d));
    }
    CHECK(DefRfx("Nomads en-Kor").card.m_power == 1);
    CHECK(DefRfx("Shaman en-Kor").card.m_toughness == 2);
}

TEST_CASE("rfx step 8: Act + Nomads + Doll -- the shields move the Nomads' 13 onto the Doll: ONE event of 26")
{
    GameState s = NomadsBoard(1, 40);
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    REQUIRE(Find(s, 2010) != nullptr);
    CHECK(Find(s, 2010)->damage == 26);
    REQUIRE(Find(s, 2001) != nullptr);              // the Nomads survives: every point moved off it (F4)
    CHECK(Find(s, 2001)->damage == 0);
    CHECK(s.players[1].life == 40 - 26);            // ONE reflection for the total ...
    CHECK(dmgrx::t_last_drain.resolutions == 1);     // ... not 13 + 13
    for (int i = 0; i < 4; ++i) { CHECK(Find(s, 2020 + i) == nullptr); }
    // Two Nomads: 13 + 13 + 13 onto the Doll, still one trigger; identical en-Kors fold to one line.
    GameState t = NomadsBoard(2, 40);
    PerformDamageAllCreatures(t, 0, DefRfx("Blasphemous Act"), 13);
    CHECK(Find(t, 2010)->damage == 39);
    CHECK(t.players[1].life == 1);
    CHECK(dmgrx::t_last_drain.resolutions == 1);
    CHECK(Find(t, 2001) != nullptr);
    CHECK(Find(t, 2002) != nullptr);
}

TEST_CASE("rfx step 8: a shield whose target has left the battlefield does nothing and is not spent (Q-R1)")
{
    GameState s = NomadsBoard(1, 20);
    dmgrx::Batch b;
    b.src.push_back({ DefRfx("Blasphemous Act").card, 0, false });
    b.hits.push_back({ 0, { false, 2001 }, 1, false, false });
    b.shields.push_back({ 2001, 2099 });             // #2099 is not on the battlefield
    dmgrx::DealDamageBatch(s, b);
    CHECK(Find(s, 2001) == nullptr);                 // the 1 stayed on the 1/1 Nomads
    CHECK(s.players[1].life == 20);
    // The same shield list with a live target after the dead one: the dead one is skipped, the live one moves it.
    GameState t = NomadsBoard(1, 20);
    b.shields.push_back({ 2001, 2010 });
    dmgrx::DealDamageBatch(t, b);
    REQUIRE(Find(t, 2001) != nullptr);
    CHECK(Find(t, 2010)->damage == 1);
    CHECK(t.players[1].life == 19);
}

TEST_CASE("rfx step 8: chained shields (E1 -> E2 -> Doll) spend each shield once; the point keeps its source")
{
    GameState s = NomadsBoard(2, 20);
    dmgrx::Batch b;
    b.src.push_back({ DefRfx("Blasphemous Act").card, 0, false });
    b.hits.push_back({ 0, { false, 2001 }, 1, false, false });
    b.shields.push_back({ 2002, 2010 });             // listed FIRST: applies only on the second pass
    b.shields.push_back({ 2001, 2002 });
    b.shields.push_back({ 2002, 2010 });             // a second E2 shield: nothing left for it to move
    dmgrx::DealDamageBatch(s, b);
    REQUIRE(Find(s, 2001) != nullptr);
    REQUIRE(Find(s, 2002) != nullptr);
    CHECK(Find(s, 2001)->damage == 0);
    CHECK(Find(s, 2002)->damage == 0);
    CHECK(Find(s, 2010)->damage == 1);
    CHECK(s.players[1].life == 19);
}

TEST_CASE("rfx step 8: the shield allocation is SEARCHED -- Doll vs Reckoner branch, and each replays in the executor")
{
    // Nomads + Doll + a summoning-sick Boros Reckoner ("any target": its own sink class, not folded into the
    // Doll's face-only one) + 4 Spirits = 7 creatures: the Act costs {1}{R}. Opponent at 60 (no kill: every
    // line is played to its end state).
    GameState s = NomadsBoard(1, 60);
    PutRfx(s, "Boros Reckoner", 2011).entered_this_turn = true;
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 2010) { p.tapped = true; } }   // no ping line
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 2001) { p.entered_this_turn = true; } }   // no attack
    for (int i = 0; i < 2; ++i) { PutRfx(s, "Mountain", 2030 + i); }
    Card act = DefRfx("Blasphemous Act").card; act.m_number = 2040;
    s.players[0].hand.push_back(act);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 2050 + i; s.players[0].library.push_back(m); }
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const int to_doll = dmgrx::ShieldCode(2010, 13), to_reck = dmgrx::ShieldCode(2011, 13);
    std::set<int> first_pins;
    for (const auto& p : plans)
    {
        const bool casts = std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
            { return a.card_name.str() == "Blasphemous Act"; });
        if (!casts) { continue; }
        REQUIRE_FALSE(p.reflect_targets.empty());
        const int pin = p.reflect_targets.front();   // the shield choice resolves BEFORE the reflections
        first_pins.insert(pin);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        const int dest = pin == to_doll ? 2010 : 2011;
        INFO("shield dest ", dest);
        // (the Nomads keeps none of the Act's 13, but the Reckoner's reflection may still be aimed at it)
        if (p.reflect_targets.size() >= 2 && p.reflect_targets[1] == dmgrx::PlayerId(1)) { CHECK(Find(roll, 2001) != nullptr); }
        // (the Reckoner's own reflection is a further searched target -- it may hit the Doll too)
        if (dest == 2010) { CHECK(Find(roll, 2010)->damage >= 26); }
        else { CHECK(Find(roll, 2011) == nullptr); CHECK(Find(roll, 2010)->damage >= 13); }
        GameState exec = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life == roll.players[1].life);
        REQUIRE(Find(exec, 2010) != nullptr);
        CHECK(Find(exec, 2010)->damage == Find(roll, 2010)->damage);
        CHECK((Find(exec, 2001) != nullptr) == (Find(roll, 2001) != nullptr));
    }
    CHECK(first_pins == std::set<int>{ to_doll, to_reck });
}

TEST_CASE("rfx step 8: no sink reflector -> the landmarks, {0} first (the legacy outcome where nothing pins)")
{
    // Nomads + a LIVE Boros Reckoner (3/3) under a 1-damage sweep: no sink with a reflection, so F4 does not hold.
    GameState s = ArmedState();
    PutRfx(s, "Nomads en-Kor", 2101);
    PutRfx(s, "Boros Reckoner", 2102);
    bool f4 = true;
    const auto opts = dmgrx::ShieldOptions(s, *Find(s, 2101), 1, {}, f4);
    CHECK_FALSE(f4);
    REQUIRE(opts.size() == 2);
    CHECK((opts[0].dest == 0 && opts[0].amount == 0));
    CHECK((opts[1].dest == 2102 && opts[1].amount == 1));
    // Unpinned (no plan): the front -- no shield; the Nomads dies, the Reckoner reflects only its own 1.
    const CardDefinition& pyro = DefRfx("Blasphemous Act");
    PerformDamageAllCreatures(s, 0, pyro, 1);
    CHECK(Find(s, 2101) == nullptr);
    REQUIRE(Find(s, 2102) != nullptr);
    CHECK(Find(s, 2102)->damage == 1);
}

TEST_CASE("rfx step 8: human play is ASKED a divide over the en-Kor and our other creatures")
{
    GameState s = NomadsBoard(1, 40);
    int asked = 0;
    std::vector<int> seen_legal;
    int seen_total = 0;
    TargetChooser chooser = [&](const GameState&, const CardDefinition& def, int, int, int per,
                                const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget>
    {
        ++asked;
        CHECK(def.params.redirect_shield);
        REQUIRE(dmgrx::t_viewer_shield != nullptr);
        seen_legal = dmgrx::t_viewer_shield->legal;
        CHECK(dmgrx::t_viewer_shield->defaults == std::vector<int>{ 0, 13 });   // the engine's F4 pick
        seen_total = per;
        // the human keeps 5 on the Nomads and moves 8 onto the Doll
        return { ChosenTarget{ 1, seen_legal[0], 5 }, ChosenTarget{ 1, seen_legal[1], 8 } };
    };
    TargetChooser* saved = g_play_target_chooser;
    g_play_target_chooser = &chooser;
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    g_play_target_chooser = saved;
    CHECK(asked == 1);
    CHECK(seen_total == 13);
    CHECK(seen_legal.size() == 2);                  // the Nomads itself, then the Doll (the Spirits are not ours)
    CHECK(Find(s, 2001) == nullptr);                 // 5 on a 1/1
    CHECK(Find(s, 2010)->damage == 21);
    CHECK(s.players[1].life == 40 - 21);
}

// ---- step 9: Shaman en-Kor's {1}{W} source pull ---------------------------------------------------------------
namespace
{
// Shaman en-Kor #2001 + Stuffy Doll #2010 (chosen the opponent) + four opponent Spirits #2020..; `plains` Plains
// #2030.. and `mountains` Mountains #2040.., untapped; opponent at `opp`.
GameState ShamanBoard(int opp, int plains, int mountains)
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    s.players[1].life = opp;
    PutRfx(s, "Shaman en-Kor", 2001);
    PutRfx(s, "Stuffy Doll", 2010).chosen_player = 1;
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 2020 + i, 1, 1, 1, "Spirit"); }
    for (int i = 0; i < plains; ++i) { PutRfx(s, "Plains", 2030 + i); }
    for (int i = 0; i < mountains; ++i) { PutRfx(s, "Mountain", 2040 + i); }
    return s;
}
int UntappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("rfx step 9: Shaman en-Kor's card data (the {1}{W} pull param beside the {0} shield)")
{
    const CardDefinition& d = DefRfx("Shaman en-Kor");
    REQUIRE(d.params.redirect_pull_cost.has_value());
    CHECK(d.params.redirect_pull_cost->ToString() == "{1}{W}");
    CHECK(d.params.redirect_shield);
    CHECK(dmgrx::DefOffersShieldChoice(d));
    CHECK_FALSE(DefRfx("Nomads en-Kor").params.redirect_pull_cost.has_value());
}

TEST_CASE("rfx step 9: Act + Shaman + Doll + Spirit -- the pull moves the Spirit's 13 onto the Shaman, its shields move "
          "26 on: the Doll is dealt 39 in ONE event, and the {1}{W} is paid")
{
    GameState s = ShamanBoard(60, 1, 1);
    const std::vector<int> pins{ dmgrx::PullCode(2020) };
    {
        dmgrx::PinScope ps(pins);
        PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    }
    REQUIRE(Find(s, 2010) != nullptr);
    CHECK(Find(s, 2010)->damage == 39);              // its own 13 + the Shaman's 13 + the Spirit's 13
    CHECK(s.players[1].life == 60 - 39);
    CHECK(dmgrx::t_last_drain.resolutions == 1);     // ONE reflection for the total
    REQUIRE(Find(s, 2001) != nullptr);               // the Shaman forwarded every point (F4): it survives
    CHECK(Find(s, 2001)->damage == 0);
    REQUIRE(Find(s, 2020) != nullptr);               // the pulled Spirit was dealt nothing
    CHECK(Find(s, 2020)->damage == 0);
    for (int i = 1; i < 4; ++i) { CHECK(Find(s, 2020 + i) == nullptr); }
    CHECK(UntappedLands(s) == 0);                    // the pull's {1}{W} was paid at the sweep
}

TEST_CASE("rfx step 9: without {1}{W} spare the pull is not offered -- the same pin does nothing")
{
    for (int lands : { 0, 1 })   // nothing; a lone Mountain (no {W})
    {
        GameState s = ShamanBoard(60, 0, lands);
        const std::vector<int> pins{ dmgrx::PullCode(2020) };
        std::vector<std::vector<int>> lists;
        dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { lists.push_back(legal); };
        {
            dmgrx::PinScope ps(pins);
            dmgrx::TargetProbeScope tp(probe, pins, 0);
            PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
        }
        CHECK(lists.empty());                         // no choice at all: the pull cannot be paid
        CHECK(Find(s, 2010)->damage == 26);           // step 8's line: the Shaman's own 13 shielded onto the Doll
        CHECK(Find(s, 2020) == nullptr);
        CHECK(UntappedLands(s) == lands);             // nothing tapped
    }
}

TEST_CASE("rfx step 9: the pull's option set -- stop first; folds P1 (own en-Kor), P2 (the Doll), P3 (identical "
          "Spirits); a multiset; the mana bounds the count")
{
    GameState s = ShamanBoard(60, 2, 2);             // {2}{W}{W}: two pulls, not three
    PutRfx(s, "Nomads en-Kor", 2002);
    PutRfx(s, "Mogg Maniac", 2011);
    const std::vector<int> pins{ dmgrx::PullCode(2020) };
    std::vector<std::vector<int>> lists;
    dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { lists.push_back(legal); };
    {
        dmgrx::PinScope ps(pins);
        dmgrx::TargetProbeScope tp(probe, pins, 0);
        PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    }
    REQUIRE(lists.size() >= 2);
    // battlefield order: Shaman, Doll, Spirits, lands, Nomads, Maniac -- the Nomads (P1) and the Doll (P2) are
    // folded out, the four Spirits are one class (P3), the Maniac (a doomed reflector) is kept
    CHECK(lists[0] == std::vector<int>{ dmgrx::PullCode(0), dmgrx::PullCode(2020), dmgrx::PullCode(2011) });
    // after a Spirit: the next Spirit or stop (the Maniac's class comes later in the order, so it is still open)
    CHECK(lists[1] == std::vector<int>{ dmgrx::PullCode(0), dmgrx::PullCode(2021), dmgrx::PullCode(2011) });
    // the second pick is unpinned -> the FRONT (stop): one pull, two lands tapped
    CHECK(UntappedLands(s) == 2);
    REQUIRE(Find(s, 2020) != nullptr);
    CHECK(Find(s, 2021) == nullptr);
}

TEST_CASE("rfx step 9: the pull is SEARCHED -- pull vs no pull branch, and each replays identically in the executor")
{
    // Shaman + Doll + 4 Spirits = 6 creatures: the Act costs {2}{R}; five lands leave exactly one {1}{W}.
    // Opponent at 80 (no kill: every line is played to its end state).
    GameState s = ShamanBoard(80, 3, 2);
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number == 2010) { p.tapped = true; }             // no ping line
        if (p.card.m_number == 2001) { p.entered_this_turn = true; }  // no attack
    }
    Card act = DefRfx("Blasphemous Act").card; act.m_number = 2050;
    s.players[0].hand.push_back(act);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Plains").card; m.m_number = 2060 + i; s.players[0].library.push_back(m); }
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    std::set<int> first_pins;
    int n = 0;
    for (const auto& p : plans)
    {
        const bool casts = std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
            { return a.card_name.str() == "Blasphemous Act"; });
        if (!casts) { continue; }
        ++n;
        REQUIRE_FALSE(p.reflect_targets.empty());
        const int pin = p.reflect_targets.front();   // the pull resolves BEFORE the shields and the reflections
        first_pins.insert(pin);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        INFO("pull pin ", pin);
        if (pin == dmgrx::PullCode(2020))
        {
            CHECK(Find(roll, 2010)->damage == 39);
            CHECK(Find(roll, 2020) != nullptr);
            CHECK(UntappedLands(roll) == 0);
        }
        else { CHECK(Find(roll, 2010)->damage == 26); CHECK(Find(roll, 2020) == nullptr); }
        GameState exec = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life == roll.players[1].life);
        REQUIRE(Find(exec, 2010) != nullptr);
        CHECK(Find(exec, 2010)->damage == Find(roll, 2010)->damage);
        CHECK((Find(exec, 2020) != nullptr) == (Find(roll, 2020) != nullptr));
        CHECK(UntappedLands(exec) == UntappedLands(roll));
    }
    CHECK(n >= 2);
    CHECK(first_pins == std::set<int>{ dmgrx::PullCode(0), dmgrx::PullCode(2020) });
}

TEST_CASE("rfx step 9: human play is ASKED which creatures to pull from (either side, up to what the mana affords)")
{
    GameState s = ShamanBoard(80, 2, 2);
    int asked_pull = 0, asked_shield = 0;
    std::vector<int> seen_legal;
    int seen_max = -1;
    TargetChooser chooser = [&](const GameState& st, const CardDefinition& def, int, int max_targets, int,
                                const std::vector<ChosenTarget>& dflt) -> std::vector<ChosenTarget>
    {
        if (dmgrx::t_viewer_shield != nullptr) { ++asked_shield; return dflt; }   // keep the engine's allocation
        ++asked_pull;
        CHECK(def.params.redirect_pull_cost.has_value());
        REQUIRE(dmgrx::t_viewer_pull != nullptr);
        seen_legal = dmgrx::t_viewer_pull->legal;
        seen_max = max_targets;
        CHECK(dflt.empty());                          // unpinned: the engine's pick is no pull
        std::vector<ChosenTarget> out;
        for (int bi : seen_legal)
        {
            const int num = st.battlefield[static_cast<std::size_t>(bi)].card.m_number;
            if (num == 2020 || num == 2021) { out.push_back({ 1, bi, 0 }); }
        }
        return out;
    };
    TargetChooser* saved = g_play_target_chooser;
    g_play_target_chooser = &chooser;
    PerformDamageAllCreatures(s, 0, DefRfx("Blasphemous Act"), 13);
    g_play_target_chooser = saved;
    CHECK(asked_pull == 1);
    CHECK(seen_max == 2);
    CHECK(seen_legal.size() == 5);                   // the Doll and the four Spirits (not the Shaman itself)
    CHECK(Find(s, 2010)->damage == 13 + 13 + 26);    // the Shaman's 39 forwarded onto the Doll
    CHECK(Find(s, 2020) != nullptr);
    CHECK(Find(s, 2021) != nullptr);
    CHECK(Find(s, 2022) == nullptr);
    CHECK(UntappedLands(s) == 0);
    CHECK(asked_shield == 1);
}

// ---- Step 10: StuffyDollProvider (design section 8) -------------------------------------------------------------------

TEST_CASE("rfx step 10: Stuffy Doll routes to its own provider; the signature survives cutting any ONE card")
{
    EnsureCardsRfx();
    const Decklist d = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Stuffy Doll/Stuffy Doll.cod"));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "StuffyDoll");
    for (const char* cut : {"Stuffy Doll", "Guilty Conscience", "Nomads en-Kor", "Shaman en-Kor", "Spiteful Sliver"})
    {
        Decklist e = d;
        e.mainboard.erase(std::remove_if(e.mainboard.begin(), e.mainboard.end(),
                                         [&](const Card& c) { return c.m_name.str() == cut; }),
                          e.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(e).Name()) == "StuffyDoll", "cutting ", cut);
    }
}

namespace
{
// Our Doll (chose the opponent), Mogg Maniac, Nomads en-Kor, Boros Reckoner, two Spiteful Slivers; one
// opponent Spirit. The ids the engine would offer a Reckoner-style "any target" reflection.
GameState FoldBoard()
{
    GameState s = ArmedState();
    PutRfx(s, "Stuffy Doll", 2101).chosen_player = 1;
    PutRfx(s, "Mogg Maniac", 2102);
    PutRfx(s, "Nomads en-Kor", 2103);
    PutRfx(s, "Boros Reckoner", 2104);
    PutRfx(s, "Spiteful Sliver", 2105);
    PutRfx(s, "Spiteful Sliver", 2106);
    TokenRfx(s, 2107, 1, 1, 1, "Spirit");
    return s;
}
std::vector<int> AllIds() { return { -2, 2101, 2102, 2103, 2104, 2105, 2106, 2107 }; }
}   // namespace

TEST_CASE("rfx step 10: the provider's SOUND folds F2/F3 drop mult-0 and mult-1 own targets; k >= 2 and the face stay")
{
    const GameState s = FoldBoard();
    const StuffyDollProvider p;
    std::vector<int> io = AllIds();
    p.ReflectTargetCandidates(s, 2104, 0, 3, static_cast<int>(CardParams::ReflectTo::AnyTarget), io);
    // Face first; the Doll (F2b), Maniac (F2b), Nomads (F2a, no instance) and Reckoner (F3, no GC on board)
    // are folded; the two Slivers (two granted instances each) stay searched. The opponent Spirit is dropped by
    // MTG_STUFFY_OPP_CREATURE_PRUNE (default ON since 2026-11-20).
    CHECK(io == std::vector<int>{ -2, 2105, 2106 });
}

TEST_CASE("rfx step 10: a GC elsewhere does not lift F2c (the source has none); a GC'd indestructible Doll moves to the FRONT")
{
    GameState s = FoldBoard();
    AttachGc(s, 2110, 2101);   // Guilty Conscience on the Doll: the unbounded amplifier
    const StuffyDollProvider p;
    std::vector<int> io = AllIds();
    p.ReflectTargetCandidates(s, 2104, 0, 3, static_cast<int>(CardParams::ReflectTo::AnyTarget), io);
    // The Reckoner source carries no GC, so routing through itself (F3) only delays the same 3: folded
    // (2026-11-20; before, any GC on the board kept it).
    CHECK(io == std::vector<int>{ 2101, -2, 2105, 2106 });
}

TEST_CASE("rfx F2c: a GC on the SOURCE keeps a one-instance own reflector (S -> T -> S fires the source's GC again)")
{
    GameState s = FoldBoard();
    PutRfx(s, "Screaming Nemesis", 2108);
    AttachGc(s, 2111, 2108);   // Guilty Conscience on the Nemesis that is reflecting
    const StuffyDollProvider p;
    std::vector<int> io = { -2, 2101, 2102, 2103, 2104, 2105, 2106, 2107 };   // "any OTHER target": not 2108
    p.ReflectTargetCandidates(s, 2108, 0, 3, static_cast<int>(CardParams::ReflectTo::AnyOtherTarget), io);
    // Doll / Maniac (F2b) and Nomads (F2a) still fold; the Reckoner stays (it can send the 3 back into the GC'd
    // Nemesis, which then deals damage again); Slivers stay (k = 2); the Spirit is pruned.
    CHECK(io == std::vector<int>{ -2, 2104, 2105, 2106 });
}

TEST_CASE("rfx step 10: with no opponent face in the list, nothing is dominated and nothing is folded")
{
    // Without the opponent's face in the list nothing is dominated by it, so nothing is dropped.
    const GameState s = FoldBoard();
    const StuffyDollProvider p;
    std::vector<int> io = { 2101, 2102, 2103, 2104 };
    p.ReflectTargetCandidates(s, 2105, 0, 3, static_cast<int>(CardParams::ReflectTo::AnyOtherTarget), io);
    CHECK(io == std::vector<int>{ 2101, 2102, 2103, 2104 });
}

TEST_CASE("rfx step 10: ExtraLethalDamage -- the Act's 13 through the Doll, the ping, and GC on a damaged Doll")
{
    GameState s = ArmedState();
    s.players[1].life = 13;
    for (int i = 0; i < 4; ++i) { PutRfx(s, i < 2 ? "Mountain" : "Plains", 2200 + i); }
    PutRfx(s, "Stuffy Doll", 2210).chosen_player = 1;
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 2220 + i, 1, 1, 1, "Spirit"); }
    Card act_c = DefRfx("Blasphemous Act").card; act_c.m_number = 2230; s.players[0].hand.push_back(act_c);
    Card gc_c = DefRfx("Guilty Conscience").card; gc_c.m_number = 2231; s.players[0].hand.push_back(gc_c);
    const StuffyDollProvider p;
    CHECK(p.HasExtraLethalModel());
    const CardDefinition* act  = &DefRfx("Blasphemous Act");
    const CardDefinition* doll = &DefRfx("Stuffy Doll");
    const CardDefinition* gcd  = &DefRfx("Guilty Conscience");
    CHECK(p.ExtraLethalDamage(s, {}) == 0);
    // A Doll def with no copy in hand is the plan's self-ping activation: 1 to the Doll -> 1 to the opponent.
    CHECK(p.ExtraLethalDamage(s, { doll }) == 1);
    // The Act: 13 reflected by the Doll (plus the ping when the plan also pings).
    CHECK(p.ExtraLethalDamage(s, { act }) == 13);
    CHECK(p.ExtraLethalDamage(s, { act, doll }) == 14);
    // GC onto the Doll plus the ping: the unbounded loop. GC with no damage reaching the Doll: nothing.
    CHECK(p.ExtraLethalDamage(s, { gcd, doll }) >= 1000000);
    CHECK(p.ExtraLethalDamage(s, { gcd }) == 0);
    // GC + the Act: the Act's 13 reaches the GC'd Doll -- the loop again.
    CHECK(p.ExtraLethalDamage(s, { gcd, act }) >= 1000000);
    // A tapped Doll cannot ping: the activation is not credited.
    s.battlefield[4].tapped = true;
    CHECK(p.ExtraLethalDamage(s, { doll }) == 0);
}

TEST_CASE("rfx step 10: a Shaman pull on a scheduled opponent spawn (numbered from 500000) encodes and decodes")
{
    const int spawn = GameState::kFirstOppSpawnNumber + 7;
    const int code  = dmgrx::PullCode(spawn);
    CHECK(dmgrx::PullDecode(code) == spawn);
    CHECK(code < dmgrx::ShieldCode((1 << 17) - 1, 1023));   // below every shield code
    CHECK(code > dmgrx::kCombatPinMark);                      // above the combat suffix mark
    CHECK(code < -2);                                         // never a player id
}

// ---- STEP 9b: the en-Kor riders on EVERY damage event; the USER's heuristic prunes (design section 14) ----------
namespace
{
const StuffyDollProvider& StuffyP() { static const StuffyDollProvider p; return p; }
// A reflection's event, built as ResolveTrig builds it: one source (a Boros Reckoner of ours), `amount` to `to`.
dmgrx::Batch ReflectionBatch(const GameState& s, int to, int amount)
{
    (void)s;
    dmgrx::Batch b;
    b.src.push_back({ DefRfx("Boros Reckoner").card, 0, true });
    b.hits.push_back({ 0, { false, to }, amount, false, false });
    b.window = dmgrx::RiderWindow::Reflection;
    return b;
}
int UntappedLands9b(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("rfx step 9b: a reflection aimed at a Nomads en-Kor -- the ENGINE forwards its shields; the Stuffy provider "
          "admits only our sweeper (lifted by MTG_UNPRUNE=redirectwindow)")
{
    // Generic provider (no deck provider attached): every window is admitted. F4: the Doll is the one sink
    // reflector, so the Nomads' 3 go onto it (a singleton list: no choice) and reflect to the opponent.
    {
        GameState s = ArmedState();
        s.players[1].life = 60;
        PutRfx(s, "Nomads en-Kor", 3001);
        PutRfx(s, "Stuffy Doll", 3002).chosen_player = 1;
        dmgrx::DealDamageBatch(s, ReflectionBatch(s, 3001, 3));
        REQUIRE(Find(s, 3001) != nullptr);
        CHECK(Find(s, 3001)->damage == 0);
        CHECK(Find(s, 3002)->damage == 3);
        CHECK(s.players[1].life == 57);
    }
    // The deck's provider: a reflection's event is not our sweeper -> no shield (the legacy outcome), unless lifted.
    {
        GameState s = ArmedState();
        s.m_provider = &StuffyP();
        s.players[1].life = 60;
        PutRfx(s, "Nomads en-Kor", 3001);
        PutRfx(s, "Stuffy Doll", 3002).chosen_player = 1;
        dmgrx::DealDamageBatch(s, ReflectionBatch(s, 3001, 3));
        if (DecisionUnpruned(UnprunedGate::RedirectWindow))
        {
            CHECK(Find(s, 3001) != nullptr);
            CHECK(s.players[1].life == 57);
        }
        else
        {
            CHECK(Find(s, 3001) == nullptr);   // the 1/1 took all 3
            CHECK(s.players[1].life == 60);
        }
    }
    // The hook itself: our sweeper only, every kind under the lifted gate.
    const GameState s = ArmedState();
    const bool open = DecisionUnpruned(UnprunedGate::RedirectWindow);
    CHECK(StuffyP().RedirectWindowAdmitted(s, 0, static_cast<int>(dmgrx::RiderWindow::Sweep), 0));
    CHECK(StuffyP().RedirectWindowAdmitted(s, 0, static_cast<int>(dmgrx::RiderWindow::Sweep), 1) == open);
    for (dmgrx::RiderWindow w : { dmgrx::RiderWindow::Reflection, dmgrx::RiderWindow::AuraHost, dmgrx::RiderWindow::SelfPing })
    { CHECK(StuffyP().RedirectWindowAdmitted(s, 0, static_cast<int>(w), 0) == open); }
}

TEST_CASE("rfx step 9b: a Shaman pull in a REFLECTION's event is a searched choice -- pinned, it moves the Spirit's 3 "
          "onto the Shaman (shields on to the Doll) and pays {1}{W}; under the Stuffy provider nothing is offered")
{
    for (int arm = 0; arm < 2; ++arm)
    {
        GameState s = ShamanBoard(60, 1, 1);
        if (arm == 1) { s.m_provider = &StuffyP(); }
        const std::vector<int> pins{ dmgrx::PullCode(2020) };
        std::vector<std::vector<int>> lists;
        dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { lists.push_back(legal); };
        {
            dmgrx::PinScope ps(pins);
            dmgrx::TargetProbeScope tp(probe, pins, 0);
            dmgrx::DealDamageBatch(s, ReflectionBatch(s, 2020, 3));
        }
        const bool offered = arm == 0 || DecisionUnpruned(UnprunedGate::RedirectWindow);
        INFO("arm ", arm);
        if (offered)
        {
            REQUIRE_FALSE(lists.empty());
            CHECK(lists.front().front() == dmgrx::PullCode(0));   // the FRONT is stop: no pull, no mana
            CHECK(std::find(lists.front().begin(), lists.front().end(), dmgrx::PullCode(2020)) != lists.front().end());
            REQUIRE(Find(s, 2020) != nullptr);
            CHECK(Find(s, 2020)->damage == 0);
            CHECK(Find(s, 2001)->damage == 0);    // the Shaman forwarded the 3 (F4: onto the Doll)
            CHECK(Find(s, 2010)->damage == 3);
            CHECK(s.players[1].life == 57);
            CHECK(UntappedLands9b(s) == 0);       // {1}{W} paid at the event
        }
        else
        {
            CHECK(lists.empty());
            CHECK(Find(s, 2020) == nullptr);
            CHECK(s.players[1].life == 60);
            CHECK(UntappedLands9b(s) == 2);
        }
    }
}

TEST_CASE("rfx step 9b: COMBAT -- Guilty Conscience on an attacking Nomads en-Kor: its trigger's 1 is shielded onto "
          "the Doll (generic); the Stuffy provider keeps the legacy outcome")
{
    for (int arm = 0; arm < 2; ++arm)
    {
        GameState s = ArmedState();
        if (arm == 1) { s.m_provider = &StuffyP(); }
        s.players[1].life = 60;
        PutRfx(s, "Nomads en-Kor", 3101);
        PutRfx(s, "Stuffy Doll", 3102).chosen_player = 1;
        AttachGc(s, 3103, 3101);
        s.players[1].life -= 1;   // the Nomads' combat damage, applied by the combat body
        dmgrx::CombatDamageDealt(s, { 3101 }, { 1 });
        const bool offered = arm == 0 || DecisionUnpruned(UnprunedGate::RedirectWindow);
        INFO("arm ", arm);
        if (offered)
        {
            REQUIRE(Find(s, 3101) != nullptr);
            CHECK(Find(s, 3101)->damage == 0);
            CHECK(Find(s, 3102)->damage == 1);
            CHECK(s.players[1].life == 58);   // 1 combat + 1 reflected by the Doll
        }
        else
        {
            CHECK(Find(s, 3101) == nullptr);   // GC's 1 killed the 1/1
            CHECK(s.players[1].life == 59);
        }
    }
}

namespace
{
// Our hosts for Guilty Conscience: ids as LegalEnchantTargets would list them (battlefield order).
std::vector<int> OwnCreatureIds(const GameState& s)
{
    std::vector<int> v;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0 && p.card.IsCreature()) { v.push_back(p.card.m_number); } }
    return v;
}
std::vector<int> GcHosts(const GameState& s)
{
    std::vector<int> io = OwnCreatureIds(s);
    StuffyP().DamageBackAuraHostCandidates(s, DefRfx("Guilty Conscience"), io);
    return io;
}
GameState HostBoard()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    return s;
}
}   // namespace

TEST_CASE("rfx step 9b: GC host PRUNE -- one host by default, two only in the split case (USER rulings)")
{
    const bool open = DecisionUnpruned(UnprunedGate::GcHost);
    // A Doll that can tap now dominates: ONE host (the Nomads, a vanilla and a settled Reckoner are pruned).
    {
        GameState s = HostBoard();
        PutRfx(s, "Nomads en-Kor", 3201);
        PutRfx(s, "Stuffy Doll", 3202).chosen_player = 1;
        PutRfx(s, "Boros Reckoner", 3203);
        const std::vector<int> all = OwnCreatureIds(s);
        CHECK(GcHosts(s) == (open ? all : std::vector<int>{ 3202 }));
    }
    // SPLIT: the Doll is summoning sick (no ping, no sweeper) but a settled Reckoner can attack now -> both.
    {
        GameState s = HostBoard();
        Permanent& d = PutRfx(s, "Stuffy Doll", 3211); d.chosen_player = 1; d.entered_this_turn = true;
        PutRfx(s, "Boros Reckoner", 3212);
        PutRfx(s, "Nomads en-Kor", 3213);
        const std::vector<int> all = OwnCreatureIds(s);
        CHECK(GcHosts(s) == (open ? all : std::vector<int>{ 3211, 3212 }));
    }
    // No Doll: a settled Mogg Maniac (power 1) can go off now; a summoning-sick Reckoner (power 3) is the bigger
    // wait-a-turn host -> both. With a sick reflector of NO greater power the go-off-now one is alone.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mogg Maniac", 3221);
        PutRfx(s, "Boros Reckoner", 3222).entered_this_turn = true;
        const std::vector<int> all = OwnCreatureIds(s);
        CHECK(GcHosts(s) == (open ? all : std::vector<int>{ 3221, 3222 }));
        GameState t = HostBoard();
        PutRfx(t, "Boros Reckoner", 3231);
        PutRfx(t, "Screaming Nemesis", 3232).entered_this_turn = true;   // 3/3: not bigger than the Reckoner
        CHECK(GcHosts(t) == (open ? OwnCreatureIds(t) : std::vector<int>{ 3231 }));
    }
    // Nothing can go off now: ONE wait-a-turn host -- the Doll if on board, else the highest power.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mogg Maniac", 3241).entered_this_turn = true;
        Permanent& d = PutRfx(s, "Stuffy Doll", 3242); d.chosen_player = 1; d.entered_this_turn = true;
        CHECK(GcHosts(s) == (open ? OwnCreatureIds(s) : std::vector<int>{ 3242 }));
        GameState t = HostBoard();
        PutRfx(t, "Mogg Maniac", 3251).entered_this_turn = true;
        PutRfx(t, "Boros Reckoner", 3252).entered_this_turn = true;
        CHECK(GcHosts(t) == (open ? OwnCreatureIds(t) : std::vector<int>{ 3252 }));
    }
    // Our castable sweeper is a starter for EVERY host: a sick Doll then dominates on its own.
    {
        GameState s = HostBoard();
        Permanent& d = PutRfx(s, "Stuffy Doll", 3261); d.chosen_player = 1; d.entered_this_turn = true;
        PutRfx(s, "Boros Reckoner", 3262);
        for (int i = 0; i < 9; ++i) { PutRfx(s, "Mountain", 3270 + i); }
        Card act = DefRfx("Blasphemous Act").card; act.m_number = 3280; s.players[0].hand.push_back(act);
        CHECK(GcHosts(s) == (open ? OwnCreatureIds(s) : std::vector<int>{ 3261 }));
    }
    // No loop-sustaining creature: the engine's list stands (never emptied).
    {
        GameState s = HostBoard();
        PutRfx(s, "Nomads en-Kor", 3291);
        TokenRfx(s, 3292, 0, 1, 1, "Soldier");
        CHECK(GcHosts(s) == OwnCreatureIds(s));
    }
}

TEST_CASE("rfx step 9b: GC host prune reaches the search -- CollectActions emits only the provider's host")
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    PutRfx(s, "Plains", 3301);
    PutRfx(s, "Stuffy Doll", 3302).chosen_player = 1;
    PutRfx(s, "Mogg Maniac", 3303);
    PutRfx(s, "Nomads en-Kor", 3304);
    Card gc = DefRfx("Guilty Conscience").card; gc.m_number = 3310; s.players[0].hand.push_back(gc);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 3320 + i; s.players[0].library.push_back(m); }
    std::set<int> hosts;
    for (const auto& p : TurnSolver::EnumerateMainPlans(s, true))
    { for (const Action& a : p.actions) { if (a.card_name.str() == "Guilty Conscience") { hosts.insert(a.enchant_target); } } }
    if (DecisionUnpruned(UnprunedGate::GcHost)) { CHECK(hosts.size() == 3); }
    else { CHECK(hosts == std::set<int>{ 3302 }); }   // the settled Doll pings now: ONE host
}

TEST_CASE("rfx step 9b: Loran's Escape target PRUNE -- the GC'd creature; else the highest-power non-Doll reflector; "
          "else the scry-only target")
{
    const bool open = DecisionUnpruned(UnprunedGate::EscapeTarget);
    const CardDefinition& lor = DefRfx("Loran's Escape");
    auto run = [&](const GameState& s, int inert, std::vector<int> io) {
        StuffyP().IndestructibleGrantTargetCandidates(s, lor, inert, io);
        return io;
    };
    GameState s = HostBoard();
    PutRfx(s, "Mogg Maniac", 3401);
    PutRfx(s, "Boros Reckoner", 3402);
    PutRfx(s, "Nomads en-Kor", 3403);
    PutRfx(s, "Stuffy Doll", 3404).chosen_player = 1;   // the no-effect representative (printed indestructible)
    TokenRfx(s, 3405, 1, 1, 1, "Spirit");
    const std::vector<int> io{ 3401, 3402, 3403, 3405, 3404 };
    // No GC: the highest-power reflector other than the Doll (Reckoner 3 > Maniac 1; the Nomads reflects nothing).
    CHECK(run(s, 3404, io) == (open ? io : std::vector<int>{ 3402 }));
    // GC on the Maniac: the Escape goes to it, whatever its power.
    GameState g = s;
    AttachGc(g, 3406, 3401);
    CHECK(run(g, 3404, io) == (open ? io : std::vector<int>{ 3401 }));
    // GC on the Doll (a no-effect target): the engine's no-effect representative stands for it.
    GameState gd = s;
    AttachGc(gd, 3407, 3404);
    CHECK(run(gd, 3404, io) == (open ? io : std::vector<int>{ 3404 }));
    // No reflector besides the Doll: the scry-only target (kept castable).
    GameState n = HostBoard();
    PutRfx(n, "Nomads en-Kor", 3411);
    PutRfx(n, "Stuffy Doll", 3412).chosen_player = 1;
    TokenRfx(n, 3413, 1, 1, 1, "Spirit");
    const std::vector<int> io2{ 3411, 3413, 3412 };
    CHECK(run(n, 3412, io2) == (open ? io2 : std::vector<int>{ 3412 }));
    // Nothing to keep (no reflector, no no-effect target): the engine's list stands.
    const std::vector<int> io3{ 3411, 3413 };
    CHECK(run(n, 0, io3) == io3);
}

TEST_CASE("rfx step 9b: Loran's Escape prune reaches the search -- one target per plan set (GC on the Maniac)")
{
    GameState s = LoranBoard(false, false);
    s.m_provider = &StuffyP();
    AttachGc(s, 1907, 1902);   // Guilty Conscience on the Mogg Maniac
    std::set<int> targets;
    for (const auto& p : TurnSolver::EnumerateMainPlans(s, true)) { const int t = LoranTargetOf(p); if (t != 0) { targets.insert(t); } }
    if (DecisionUnpruned(UnprunedGate::EscapeTarget)) { CHECK(targets.size() >= 4); }
    else { CHECK(targets == std::set<int>{ 1902 }); }
    GameState n = LoranBoard(false, false);
    n.m_provider = &StuffyP();
    targets.clear();
    for (const auto& p : TurnSolver::EnumerateMainPlans(n, true)) { const int t = LoranTargetOf(p); if (t != 0) { targets.insert(t); } }
    if (DecisionUnpruned(UnprunedGate::EscapeTarget)) { CHECK(targets.size() >= 4); }
    else { CHECK(targets == std::set<int>{ 1901 }); }   // the Reckoner (3) over the Maniac (1)
}

// ---- STEP 9c: the searched ATTACK HOLD of a GC-enchanted creature; the USER's GC-before-Escape order ----------
// USER 2026-11-17 ~20:45: "One slightly messy case is if we need to Guilty Conscience and Loren's Escape next turn.
// We need to not always attack in the meantime." ~20:50: "...(Guilty Conscience first?)"

TEST_CASE("rfx step 9c: hold codes -- encoding, AttackHeld reads only the leading codes, the drain skips them")
{
    const int h = dmgrx::AttackHoldCode(1601);
    CHECK(dmgrx::IsAttackHoldCode(h));
    CHECK(h > dmgrx::kCombatPinMark);
    CHECK(h < dmgrx::PullCode(0));                     // below every pull code
    CHECK(h < dmgrx::PullCode((1 << 28) - 1));
    CHECK_FALSE(dmgrx::IsAttackHoldCode(dmgrx::PlayerId(1)));
    CHECK_FALSE(dmgrx::IsAttackHoldCode(dmgrx::kCombatPinMark));
    CHECK_FALSE(dmgrx::IsAttackHoldCode(dmgrx::PullCode((1 << 28) - 1)));
    CHECK(dmgrx::LeadingAttackHolds({ h, dmgrx::AttackHoldCode(7), dmgrx::PlayerId(1) }) == 2);
    GameState s = ArmedState();
    s.scripted_combat_reflect = { h, dmgrx::PlayerId(1) };
    CHECK(dmgrx::AttackHeld(s, 1601));
    CHECK_FALSE(dmgrx::AttackHeld(s, 1602));
    s.scripted_combat_reflect = { dmgrx::PlayerId(1), h };   // not leading: a reflection pin, never a hold
    CHECK_FALSE(dmgrx::AttackHeld(s, 1601));
    s.reflect_armed = false;
    s.scripted_combat_reflect = { h };
    CHECK_FALSE(dmgrx::AttackHeld(s, 1601));                 // unarmed: inert
    // The drain: a GC'd Reckoner (not held) deals 3 in combat with a hold code for ANOTHER creature leading the
    // stream; its reflection (a choice: the face or the Doll -- the Reckoner itself has died to GC's 3 by then) takes
    // the pin after the hold, and the hold stays on the state.
    GameState d = ArmedState();
    PutRfx(d, "Boros Reckoner", 1601);
    AttachGc(d, 1602, 1601);
    PutRfx(d, "Stuffy Doll", 1603).chosen_player = 1;
    d.scripted_combat_reflect = { dmgrx::AttackHoldCode(1999), dmgrx::PlayerId(1) };
    d.players[1].life -= 3;
    dmgrx::CombatDamageDealt(d, { 1601 }, { 3 });
    CHECK(d.players[1].life == 14);                          // 3 combat + 3 reflected at the pinned face
    CHECK(d.scripted_combat_reflect == std::vector<int>{ dmgrx::AttackHoldCode(1999) });   // pin consumed, hold kept
}

namespace
{
// A settled GC'd Boros Reckoner that would attack; nothing in hand, two Mountains.
GameState HoldBoard()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    PutRfx(s, "Mountain", 3601);
    PutRfx(s, "Mountain", 3602);
    PutRfx(s, "Boros Reckoner", 3603);
    AttachGc(s, 3604, 3603);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Plains").card; m.m_number = 3620 + i; s.players[0].library.push_back(m); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 9c: the search BRANCHES attack vs hold for a GC'd Reckoner, and both branches replay in the executor")
{
    for (int arm = 0; arm < 2; ++arm)
    {
        INFO("arm ", arm == 0 ? "generic provider" : "Stuffy provider");
        GameState s = HoldBoard();
        if (arm == 1) { s.m_provider = &StuffyP(); }
        TurnSolver::Plan pass;
        pass.land_decided = true;
        const auto plans = TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { pass });
        int hold_lines = 0, attack_lines = 0;
        for (const auto& p : plans)
        {
            const std::vector<int> pins = dmgrx::CombatPinsOf(p.reflect_targets);
            const bool hold = !pins.empty() && pins.front() == dmgrx::AttackHoldCode(3603);
            INFO("hold ", hold, " pins ", pins.size());
            if (hold) { CHECK(pins.size() == 1); }   // a held Reckoner deals no damage: no reflection choice follows
            GameState roll = s;
            TurnSolver::ApplyPlan(roll, p, true);
            RolloutSimulateCombat(roll);
            GameState exec = s;
            AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
            MtgTestSeam::CommitPhasePlan(eng, p, true);
            eng.TakeTurn(exec, true, ResolveStackRfx);
            exec.phase = Phase::Combat;
            GameEngine ge(eng);
            ge.PlayOutFrom(exec, exec.turn_number, GameEngine::ResumeAt::Combat);
            if (hold)
            {
                ++hold_lines;
                CHECK(roll.players[1].life == 20);
                REQUIRE(Find(roll, 3603) != nullptr);
                CHECK_FALSE(Find(roll, 3603)->tapped);
            }
            else
            {
                ++attack_lines;
                CHECK(roll.players[1].life < 20);
                CHECK(Find(roll, 3603) == nullptr);   // 3 combat, GC's 3 back: the 3/3 dies
            }
            // Lockstep: the executor's real combat made the same declaration and reached the same board.
            CHECK(exec.players[1].life == roll.players[1].life);
            CHECK((Find(exec, 3603) != nullptr) == (Find(roll, 3603) != nullptr));
        }
        // Step 9e: the generic provider names nobody under MayDeclineAttack, so only the Stuffy arm holds.
        CHECK(hold_lines == (arm == 1 ? 1 : 0));
        CHECK(attack_lines >= 1);
    }
}

TEST_CASE("rfx step 9c: no hold axis for an indestructible GC host, a sick one, or without GC; unarmed decks untouched")
{
    TurnSolver::Plan pass;
    pass.land_decided = true;
    // Step 9e: the hold set is the provider's MayDeclineAttack -- every case runs the Stuffy provider.
    auto holds = [&](GameState s) {
        s.m_provider = &StuffyP();
        int n = 0;
        for (const auto& p : TurnSolver::SubdecisionAxesForTest(s, true, { pass }))
        {
            const std::vector<int> c = dmgrx::CombatPinsOf(p.reflect_targets);
            if (!c.empty() && dmgrx::IsAttackHoldCode(c.front())) { ++n; }
        }
        return n;
    };
    {   // the Doll is indestructible: attacking costs it nothing
        GameState s = HoldBoard();
        s.battlefield.erase(s.battlefield.begin() + 2, s.battlefield.end());
        PutRfx(s, "Stuffy Doll", 3605).chosen_player = 1;
        AttachGc(s, 3606, 3605);
        CHECK(holds(s) == 0);
    }
    {   // an Escaped (indestructible this turn) Reckoner: no hold
        GameState s = HoldBoard();
        for (Permanent& q : s.battlefield) { if (q.card.m_number == 3603) { q.temp_indestructible = true; } }
        CHECK(holds(s) == 0);
    }
    {   // summoning sick: it cannot attack, nothing to hold
        GameState s = HoldBoard();
        for (Permanent& q : s.battlefield) { if (q.card.m_number == 3603) { q.entered_this_turn = true; } }
        CHECK(holds(s) == 0);
    }
    {   // no GC
        GameState s = HoldBoard();
        s.battlefield.pop_back();
        CHECK(holds(s) == 0);
    }
    {   // unarmed
        GameState s = HoldBoard();
        s.reflect_armed = false;
        CHECK(holds(s) == 0);
    }
    CHECK(holds(HoldBoard()) == 1);   // the control: the settled GC'd Reckoner is held under the Stuffy provider
    {   // two GC'd attackers: every subset (3 holds beside the natural attack)
        GameState s = HoldBoard();
        s.m_provider = &StuffyP();
        PutRfx(s, "Mogg Maniac", 3607);
        AttachGc(s, 3608, 3607);
        std::set<std::vector<int>> subsets;
        for (const auto& p : TurnSolver::SubdecisionAxesForTest(s, true, { pass }))
        {
            std::vector<int> c = dmgrx::CombatPinsOf(p.reflect_targets);
            c.resize(dmgrx::LeadingAttackHolds(c));
            subsets.insert(c);
        }
        CHECK(subsets == std::set<std::vector<int>>{ {}, { dmgrx::AttackHoldCode(3603) }, { dmgrx::AttackHoldCode(3607) },
                                                     { dmgrx::AttackHoldCode(3603), dmgrx::AttackHoldCode(3607) } });
    }
}

namespace
{
// Pin step 11's two heurarm slots for one test (-1 = the env default), restored on exit.
struct StuffyArm
{
    StuffyArm(int split, int order = -1)
    {
        heurarm::t_arm[heurarm::STUFFY_MAIN_SPLIT] = static_cast<std::int8_t>(split);
        heurarm::t_arm[heurarm::STUFFY_CAST_ORDER] = static_cast<std::int8_t>(order);
    }
    ~StuffyArm() { heurarm::t_arm[heurarm::STUFFY_MAIN_SPLIT] = -1; heurarm::t_arm[heurarm::STUFFY_CAST_ORDER] = -1; }
};
}   // namespace

TEST_CASE("rfx step 9c: CAST ORDER -- Guilty Conscience before Loran's Escape (USER); a plan casting both casts GC first")
{
    const StuffyArm arm(0);   // the step-9c order (step 11's order replaces it by default; its own case below)
    const StuffyDollProvider p;
    const GenericProvider g;
    GameState s = ArmedState();
    const int gc = p.CastOrderRank(s, DefRfx("Guilty Conscience"));
    const int esc = p.CastOrderRank(s, DefRfx("Loran's Escape"));
    CHECK(gc < esc);
    CHECK(esc == g.CastOrderRank(s, DefRfx("Loran's Escape")));
    CHECK(gc < p.CastOrderRank(s, DefRfx("Blasphemous Act")));
    for (const char* n : { "Blasphemous Act", "Chain Reaction", "Boros Reckoner", "Stuffy Doll", "Mind Stone", "Nomads en-Kor" })
    { INFO(n); CHECK(p.CastOrderRank(s, DefRfx(n)) == g.CastOrderRank(s, DefRfx(n))); }
    // A plan that casts both, with the Escape FIRST in hand.
    GameState b = HostBoard();
    b.m_provider = &StuffyP();
    PutRfx(b, "Plains", 3701);
    PutRfx(b, "Plains", 3702);
    PutRfx(b, "Boros Reckoner", 3703);
    Card e = DefRfx("Loran's Escape").card;    e.m_number = 3710; b.players[0].hand.push_back(e);
    Card c = DefRfx("Guilty Conscience").card; c.m_number = 3711; b.players[0].hand.push_back(c);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Plains").card; m.m_number = 3720 + i; b.players[0].library.push_back(m); }
    int both = 0;
    for (const auto& plan : TurnSolver::EnumerateMainPlans(b, true))
    {
        const std::vector<std::string> order = TurnSolver::RealisedNonSacCastOrder(b, plan);
        const auto ig = std::find(order.begin(), order.end(), "Guilty Conscience");
        const auto ie = std::find(order.begin(), order.end(), "Loran's Escape");
        if (ig == order.end() || ie == order.end()) { continue; }
        ++both;
        CHECK(ig < ie);
    }
    CHECK(both >= 1);
}

// ---- STEP 9d: Loran's Escape SAME-PLAN targets; the searched Loran's scry with a StuffyDollProvider verdict ----------
// USER 2026-11-17 ~23:25: "That should be fixed, given the potential creature -> Guilty Conscience -> Loran's Excape ->
// Blasphemous act line." and, on the scry: "It should be searched and typically overridden by a provider ... This is
// the kind of thing worth a prune."

namespace
{
Card HandRfx(GameState& s, const std::string& name, int number)
{
    Card c = DefRfx(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return c;
}
}   // namespace

TEST_CASE("rfx step 9d: Loran's Escape prune admits SAME-PLAN targets (hand reflectors, the same plan's GC hosts)")
{
    const bool open_esc = DecisionUnpruned(UnprunedGate::EscapeTarget);
    const bool open = open_esc || DecisionUnpruned(UnprunedGate::GcHost);   // the GC cases also read the host prune
    const CardDefinition& lor = DefRfx("Loran's Escape");
    auto run = [&](const GameState& s, int inert, std::vector<int> io) {
        StuffyP().IndestructibleGrantTargetCandidates(s, lor, inert, io);
        return io;
    };
    // A hand Reckoner and nothing on the battlefield but the scry-only Mind Stone: the Reckoner (it reflects; the
    // plan casts it first) AND the castable-alone scry line. Step 9b kept the Mind Stone alone.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mind Stone", 4001).tapped = true;
        HandRfx(s, "Boros Reckoner", 4002);
        const std::vector<int> io{ 4002, 4001 };
        CHECK(run(s, 4001, io) == io);
    }
    // No GC anywhere: the best reflector over battlefield + hand. A hand Reckoner (3) beats a settled Maniac (1); the
    // Maniac stays as the castable-alone line. The reverse (battlefield Reckoner, hand Maniac): the Reckoner alone.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mogg Maniac", 4011);
        HandRfx(s, "Boros Reckoner", 4012);
        const std::vector<int> io{ 4011, 4012 };
        CHECK(run(s, 0, io) == io);
        GameState t = HostBoard();
        PutRfx(t, "Boros Reckoner", 4021);
        HandRfx(t, "Mogg Maniac", 4022);
        CHECK(run(t, 0, { 4021, 4022 }) == (open_esc ? std::vector<int>{ 4021, 4022 } : std::vector<int>{ 4021 }));
    }
    // GC in HAND (the same plan may cast it, before the Escape): the creatures it would enchant. The GC-host prune's
    // split case keeps the settled Maniac (can go off now) and the sick Reckoner (bigger, wait a turn) -> both are
    // Escape targets; step 9b kept only the Reckoner (highest power), so "GC on the Maniac, Escape on it" was lost.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mogg Maniac", 4031);
        PutRfx(s, "Boros Reckoner", 4032).entered_this_turn = true;
        PutRfx(s, "Nomads en-Kor", 4033);
        HandRfx(s, "Guilty Conscience", 4034);
        const std::vector<int> io{ 4031, 4032, 4033 };
        CHECK(run(s, 0, io) == (open ? io : std::vector<int>{ 4031, 4032 }));
    }
    // GC in hand, one battlefield host (the Maniac) and a hand Reckoner: GC can go on either (the Reckoner once the
    // plan has cast it) -> both.
    {
        GameState s = HostBoard();
        PutRfx(s, "Mogg Maniac", 4041);
        HandRfx(s, "Boros Reckoner", 4042);
        HandRfx(s, "Guilty Conscience", 4043);
        const std::vector<int> io{ 4041, 4042 };
        CHECK(run(s, 0, io) == io);
    }
    // GC in hand whose host is the indestructible Doll (a Doll with a starter is the ONE host): the no-effect
    // representative stands for it, beside the best reflector.
    {
        GameState s = HostBoard();
        s.deck_has_spell_target_watch = false;
        PutRfx(s, "Stuffy Doll", 4051).chosen_player = 1;   // settled, untapped: its {T} ping is a starter
        PutRfx(s, "Boros Reckoner", 4052);
        PutRfx(s, "Nomads en-Kor", 4053);
        HandRfx(s, "Guilty Conscience", 4054);
        const std::vector<int> io{ 4052, 4053, 4051 };
        CHECK(run(s, 4051, io) == (open ? io : std::vector<int>{ 4052, 4051 }));
    }
}

TEST_CASE("rfx step 9d: the same-plan hand target reaches the search (Reckoner from hand, Escape on it)")
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 4101);
    for (int i = 0; i < 4; ++i) { PutRfx(s, "Mountain", 4102 + i); }
    PutRfx(s, "Mind Stone", 4110).tapped = true;    // the no-effect (scry-only) representative
    HandRfx(s, "Boros Reckoner", 4111);
    HandRfx(s, "Loran's Escape", 4112);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 4120 + i; s.players[0].library.push_back(m); }
    std::set<std::string> hand_targets;
    std::set<int> targets;
    for (const auto& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Loran's Escape") { continue; }
            targets.insert(a.enchant_target);
            if (!a.trick_hand_target.empty()) { hand_targets.insert(a.trick_hand_target); }
        }
    }
    CHECK(hand_targets.count("Boros Reckoner") == 1);
    CHECK(targets.count(4110) == 1);   // the castable-alone scry line stays
}

TEST_CASE("rfx step 9d: StuffyDollProvider ScryVerdict -- keep a missing piece / a land when short, bottom a flood land")
{
    const bool open = DecisionUnpruned(UnprunedGate::StuffyScry);
    const StuffyDollProvider& p = StuffyP();
    auto card = [](const std::string& n) { Card c = DefRfx(n).card; c.m_number = 4299; return c; };
    // Lands: 3 in play + 1 in hand = 4 < 5 -> keep; 5 -> no verdict; 7 -> bottom.
    GameState s = HostBoard();
    for (int i = 0; i < 3; ++i) { PutRfx(s, "Mountain", 4201 + i); }
    HandRfx(s, "Plains", 4210);
    CHECK(p.ScryVerdict(s, card("Plains")) == (open ? -1 : 1));
    PutRfx(s, "Mountain", 4204);
    CHECK(p.ScryVerdict(s, card("Plains")) == -1);
    PutRfx(s, "Mountain", 4205);
    PutRfx(s, "Mountain", 4206);
    CHECK(p.ScryVerdict(s, card("Plains")) == (open ? -1 : 0));
    CHECK_FALSE(p.ScryKeepOnTop(s, card("Plains")));
    // A sweeper: kept while we hold none; no verdict with one in hand.
    CHECK(p.ScryVerdict(s, card("Blasphemous Act")) == (open ? -1 : 1));
    CHECK(p.ScryKeepOnTop(s, card("Blasphemous Act")));
    HandRfx(s, "Chain Reaction", 4211);
    CHECK(p.ScryVerdict(s, card("Blasphemous Act")) == -1);
    // A reflector: kept while we control and hold none; no verdict once one is in play.
    CHECK(p.ScryVerdict(s, card("Boros Reckoner")) == (open ? -1 : 1));
    PutRfx(s, "Mogg Maniac", 4212);
    CHECK(p.ScryVerdict(s, card("Boros Reckoner")) == -1);
    CHECK(p.ScryVerdict(s, card("Stuffy Doll")) == -1);
    // Everything else: no verdict.
    CHECK(p.ScryVerdict(s, card("Mind Stone")) == -1);
    CHECK(p.ScryVerdict(s, card("Guilty Conscience")) == -1);
    CHECK(p.ScryVerdict(s, card("Nomads en-Kor")) == -1);
}

namespace
{
// Plains + 2 Mountains, a settled Reckoner, Loran's Escape in hand, a library whose top is `top`.
GameState ScryBoard(const std::string& top)
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 4301);
    PutRfx(s, "Mountain", 4302);
    PutRfx(s, "Mountain", 4303);
    PutRfx(s, "Boros Reckoner", 4304);
    HandRfx(s, "Loran's Escape", 4310);
    Card t = DefRfx(top).card; t.m_number = 4320; s.players[0].library.push_back(t);
    for (int i = 0; i < 9; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 4321 + i; s.players[0].library.push_back(m); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 9d: Loran's scry is SEARCHED (pinned twin, both worlds) and the verdict prunes a firm card")
{
    // Mind Stone on top: no verdict -> each Escape variant is twinned (scry_pin -1 = the provider's keep, 1 = bottom).
    const GameState initial = ScryBoard("Mind Stone");
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    std::set<int> pins;
    int checked = 0;
    for (const auto& p : plans)
    {
        const Action* esc = nullptr;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Loran's Escape") { esc = &a; } }
        if (esc == nullptr) { continue; }
        pins.insert(esc->scry_pin);
        INFO("scry_pin ", static_cast<int>(esc->scry_pin));
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        const auto& lib = roll.players[0].library;
        REQUIRE(lib.size() == exec.players[0].library.size());
        for (std::size_t i = 0; i < lib.size(); ++i) { CHECK(lib[i].m_number == exec.players[0].library[i].m_number); }
        if (esc->scry_pin == 1) { CHECK(lib[lib.size() - 1].m_number == 4320); }   // bottomed
        else                    { CHECK(lib[0].m_number == 4320); }  // kept (the provider's default for a nonland)
        ++checked;
    }
    CHECK(pins == std::set<int>{ -1, 1 });
    CHECK(checked >= 2);
    // A firm card -- Blasphemous Act while we hold no sweeper: KEEP. The fan is sized off the look COUNT, never off the
    // hidden top (no clairvoyance about the look), so the twin still exists; the verdict applies at RESOLUTION, where it
    // leaves one candidate: the twin's pin clamps to it and the Act stays on top in both variants (unless lifted).
    const GameState firm_board = ScryBoard("Blasphemous Act");
    const int act_num = firm_board.players[0].library[0].m_number;
    std::set<int> firm;
    for (const auto& p : TurnSolver::EnumerateMainPlans(firm_board, true))
    {
        const Action* esc = nullptr;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Loran's Escape") { esc = &a; } }
        if (esc == nullptr) { continue; }
        firm.insert(esc->scry_pin);
        GameState roll = firm_board;
        TurnSolver::ApplyPlan(roll, p, true);
        const auto& lib = roll.players[0].library;
        REQUIRE(!lib.empty());
        const bool bottomed = lib[lib.size() - 1].m_number == act_num;
        CHECK(bottomed == (esc->scry_pin == 1 && DecisionUnpruned(UnprunedGate::StuffyScry)));
    }
    CHECK(firm == std::set<int>{ -1, 1 });
}

// ---- STEP 9e: the attack-hold PROVIDER HOOK (USER 2026-11-18 ~00:37) and human play's "attack with X?" --------
// "I think we can make it an exception to the always attack-rule. Perhaps have a deck heuristic that can be
// consulted to reject attacking?" DecisionProvider::MayDeclineAttack names who may stay home (default: nobody);
// the searched fan (step 9c) and the human prompt (AskHumanAttackHolds) read the same hook.

TEST_CASE("rfx step 9e: MayDeclineAttack -- default false; Stuffy names a GC'd, non-indestructible creature only")
{
    GameState s = HoldBoard();                                   // a settled GC'd Reckoner (3603, GC 3604)
    PutRfx(s, "Stuffy Doll", 3605).chosen_player = 1;
    AttachGc(s, 3606, 3605);                                     // GC'd but indestructible (printed)
    PutRfx(s, "Mogg Maniac", 3607);                              // no GC
    PutRfx(s, "Mogg Maniac", 3608).temp_indestructible = true;   // GC'd, Escaped this turn
    AttachGc(s, 3609, 3608);
    const GenericProvider g;
    for (const Permanent& q : s.battlefield)
    {
        INFO("permanent ", q.card.m_number);
        CHECK_FALSE(g.MayDeclineAttack(s, q));
        CHECK(StuffyP().MayDeclineAttack(s, q) == (q.card.m_number == 3603));
    }
}

namespace
{
struct HumanAttackAsk
{
    int answer = -1;
    std::vector<int> asked;          // examined[0]'s card number per ask
    std::vector<int> defaults;       // heuristic_pick per ask
    std::vector<std::string> sources;
    DigChooser fn;
    DigChooser* prior = nullptr;
    HumanAttackAsk()
    {
        fn = [this](const GameState&, int, const std::string& src, const std::vector<Card>& ex,
                    const std::vector<int>& legal, int def) {
            REQUIRE(ex.size() == 1u);
            CHECK(legal == std::vector<int>{ 0 });
            asked.push_back(ex[0].m_number);
            defaults.push_back(def);
            sources.push_back(src);
            return answer == 99 ? def : answer;
        };
        prior = g_play_dig_chooser;
        g_play_dig_chooser = &fn;
    }
    ~HumanAttackAsk() { g_play_dig_chooser = prior; }
};
bool Declared(const GameState& s, int number)
{
    for (int i : DeclareAttackerIndices(s)) { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return true; } }
    return false;
}
// HoldBoard + a settled Mogg Maniac without a GC (3607): it attacks and is never asked about.
GameState HumanHoldBoard()
{
    GameState s = HoldBoard();
    s.m_provider = &StuffyP();
    PutRfx(s, "Mogg Maniac", 3607);
    return s;
}
}   // namespace

TEST_CASE("rfx step 9e: human play asks 'attack with X?' ONLY for the creature the hook names; 'no' holds it")
{
    {   // "keep it home": asked once, for the GC'd Reckoner, default attack; it does not attack, the Maniac does
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = -1;
        CHECK(AskHumanAttackHolds(s) == 1);
        CHECK(h.asked == std::vector<int>{ 3603 });
        CHECK(h.defaults == std::vector<int>{ 0 });
        CHECK(h.sources[0].find("(may attack)") != std::string::npos);
        CHECK(dmgrx::AttackHeld(s, 3603));
        CHECK_FALSE(Declared(s, 3603));
        CHECK(Declared(s, 3607));
    }
    {   // "attack": no hold written, both attack
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = 0;
        CHECK(AskHumanAttackHolds(s) == 1);
        CHECK(s.scripted_combat_reflect.empty());
        CHECK(Declared(s, 3603));
        CHECK(Declared(s, 3607));
    }
    {   // the REAL declaration (AIEngine::DeclareAttackers) asks and honours the answer
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = -1;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        const std::vector<Permanent*> atk = eng.DeclareAttackers(s);
        CHECK(h.asked == std::vector<int>{ 3603 });
        REQUIRE(atk.size() == 1u);
        CHECK(atk[0]->card.m_number == 3607);
    }
    {   // the generic provider names nobody: no prompt at all (every other deck)
        GameState s = HumanHoldBoard();
        s.m_provider = nullptr;
        HumanAttackAsk h;
        CHECK(AskHumanAttackHolds(s) == 0);
        CHECK(h.asked.empty());
        CHECK(Declared(s, 3603));
    }
    {   // an indestructible GC host (the Doll) and a summoning-sick GC'd creature are not asked about
        GameState s = HumanHoldBoard();
        for (Permanent& q : s.battlefield) { if (q.card.m_number == 3603) { q.entered_this_turn = true; } }
        PutRfx(s, "Stuffy Doll", 3605).chosen_player = 1;
        AttachGc(s, 3606, 3605);
        HumanAttackAsk h;
        CHECK(AskHumanAttackHolds(s) == 0);
    }
    {   // no chooser (autonomous play, every search scope): nothing asked, nothing held
        GameState s = HumanHoldBoard();
        CHECK(AskHumanAttackHolds(s) == 0);
        CHECK(s.scripted_combat_reflect.empty());
    }
}

TEST_CASE("rfx step 9e: a --force-attackers replay -- the prompt's default is the RECORDED declaration")
{
    std::vector<std::string> recorded;
    AttackersChooser pin = [&](int) -> const std::vector<std::string>* { return &recorded; };
    AttackersChooser* prior = g_play_attackers_chooser;
    g_play_attackers_chooser = &pin;
    {   // the recording held the Reckoner (absent from the recorded set): default -1, held
        recorded = { "Mogg Maniac" };
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = 99;   // take the default
        CHECK(AskHumanAttackHolds(s) == 1);
        CHECK(h.defaults == std::vector<int>{ -1 });
        CHECK_FALSE(Declared(s, 3603));
        CHECK(Declared(s, 3607));
    }
    {   // the recording attacked with it: default 0, attacks
        recorded = { "Boros Reckoner", "Mogg Maniac" };
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = 99;
        CHECK(AskHumanAttackHolds(s) == 1);
        CHECK(h.defaults == std::vector<int>{ 0 });
        CHECK(Declared(s, 3603));
    }
    {   // a stream answer "keep it home" outranks a pin that names it (the pinned path reads the hold)
        recorded = { "Boros Reckoner", "Mogg Maniac" };
        GameState s = HumanHoldBoard();
        HumanAttackAsk h;
        h.answer = -1;
        AskHumanAttackHolds(s);
        CHECK_FALSE(Declared(s, 3603));
        CHECK(Declared(s, 3607));
    }
    g_play_attackers_chooser = prior;
}

// ---- STEP 11: the main split (sweepers in main 2 only), the USER's total cast order, Mind Stone's dig at the end of
// main 1. USER 2026-11-17 ~21:48: "...So in that case it would just be sweepers in main 2."; 2026-11-18 00:55: "I want a
// full order with no ties."; 01:00: "Though we should also add mind stone's ability. Perhaps at the end of main 1."

TEST_CASE("rfx step 11: DeckUsesSecondMain -- Stuffy Doll plays a second main by default; =0 restores one main; one reader")
{
    EnsureCardsRfx();
    const Decklist d = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Stuffy Doll/Stuffy Doll.cod"));
    {
        const StuffyArm arm(-1);
        CHECK(StuffyMainSplitOn());
        CHECK(StuffyCastOrderOn());
        CHECK(GoldFishRunner::DeckUsesSecondMain(d));
    }
    {
        const StuffyArm arm(0);
        CHECK_FALSE(StuffyMainSplitOn());
        CHECK_FALSE(StuffyCastOrderOn());   // the sub-flag is inert while the split is off
        CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(d));
    }
    {
        const StuffyArm arm(1, 0);
        CHECK(StuffyMainSplitOn());
        CHECK_FALSE(StuffyCastOrderOn());
        CHECK(GoldFishRunner::DeckUsesSecondMain(d));
    }
    // Sweepers by PARAM.
    CHECK(StuffyIsCreatureSweeper(DefRfx("Blasphemous Act")));
    CHECK(StuffyIsCreatureSweeper(DefRfx("Chain Reaction")));
    for (const char* n : { "Loran's Escape", "Guilty Conscience", "Mogg Maniac", "Stuffy Doll", "Mind Stone" })
    { INFO(n); CHECK_FALSE(StuffyIsCreatureSweeper(DefRfx(n))); }
}

namespace
{
// Four Mountains, a settled Mogg Maniac, four opponent Spirits (Act costs {3}{R}); hand Act, Chain, Boros Reckoner.
GameState SplitBoard()
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.uses_second_main = true;
    for (int i = 0; i < 4; ++i) { PutRfx(s, "Mountain", 4101 + i); }
    PutRfx(s, "Mogg Maniac", 4110);
    for (int i = 0; i < 4; ++i) { TokenRfx(s, 4120 + i, 1, 1, 1, "Spirit"); }
    HandRfx(s, "Blasphemous Act", 4130);
    HandRfx(s, "Chain Reaction", 4131);
    HandRfx(s, "Boros Reckoner", 4132);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 4140 + i; s.players[0].library.push_back(m); }
    return s;
}
std::set<std::string> CastNames(const std::vector<TurnSolver::Plan>& plans)
{
    std::set<std::string> out;
    for (const auto& p : plans)
    { for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { out.insert(a.card_name.str()); } } }
    return out;
}
}   // namespace

TEST_CASE("rfx step 11: main 1 never offers a sweeper; main 2 does; creatures in BOTH; =0 offers the sweeper in main 1")
{
    {
        const StuffyArm arm(-1);
        GameState s = SplitBoard();
        const std::set<std::string> m1 = CastNames(TurnSolver::EnumerateMainPlans(s, true));
        CHECK(m1.count("Blasphemous Act") == 0);
        CHECK(m1.count("Chain Reaction") == 0);
        CHECK(m1.count("Boros Reckoner") == 1);
        s.phase = Phase::PostCombatMain;
        const std::set<std::string> m2 = CastNames(TurnSolver::EnumerateMainPlans(s, false));
        CHECK(m2.count("Blasphemous Act") == 1);
        CHECK(m2.count("Chain Reaction") == 1);
        CHECK(m2.count("Boros Reckoner") == 1);
        // The provider hook itself: main 1 refuses exactly the sweeper casts.
        Action act; act.kind = Action::Kind::CastFromHand; act.def = &DefRfx("Blasphemous Act");
        act.card_name = act.def->card.m_name;
        CHECK_FALSE(StuffyP().CollectedActionAllowed(s, act, true));
        CHECK(StuffyP().CollectedActionAllowed(s, act, false));
        Action cr = act; cr.def = &DefRfx("Boros Reckoner"); cr.card_name = cr.def->card.m_name;
        CHECK(StuffyP().CollectedActionAllowed(s, cr, true));
    }
    {
        const StuffyArm arm(0);
        GameState s = SplitBoard();
        s.uses_second_main = false;
        const std::set<std::string> m1 = CastNames(TurnSolver::EnumerateMainPlans(s, true));
        CHECK(m1.count("Blasphemous Act") == 1);
        CHECK(m1.count("Chain Reaction") == 1);
    }
}

TEST_CASE("rfx step 11: the USER's TOTAL cast order -- every listed card on its own rank, in the USER's sequence")
{
    const StuffyArm arm(-1);
    const StuffyDollProvider p;
    GameState s = ArmedState();
    const char* order[] = { "Mind Stone", "Nomads en-Kor", "Shaman en-Kor", "Mogg Maniac", "Screaming Nemesis",
                            "Spiteful Sliver", "Boros Reckoner", "Stuffy Doll", "Guilty Conscience", "Loran's Escape",
                            "Chain Reaction", "Blasphemous Act" };
    int prev = -1;
    for (const char* n : order)
    {
        INFO(n);
        const int r = p.CastOrderRank(s, DefRfx(n));
        CHECK(r > prev);   // strictly increasing: no ties anywhere
        prev = r;
        // The report's "main" column: only the sweepers are main 2.
        const auto mp = p.MainPhaseOverride(s, DefRfx(n));
        REQUIRE(mp.has_value());
        CHECK((*mp == DecisionProvider::MainPhase::Main2) == StuffyIsCreatureSweeper(DefRfx(n)));
        if (!StuffyIsCreatureSweeper(DefRfx(n))) { CHECK(*mp == DecisionProvider::MainPhase::Both); }
    }
    // An unlisted card keeps its generic tier, after the listed cards of that tier.
    const GenericProvider g;
    const CardDefinition& extra_creature = DefRfx("Venerable Knight");
    CHECK(g.CastOrderRank(s, extra_creature) == 10);
    CHECK(p.CastOrderRank(s, extra_creature) > p.CastOrderRank(s, DefRfx("Stuffy Doll")));
    CHECK(p.CastOrderRank(s, extra_creature) < p.CastOrderRank(s, DefRfx("Guilty Conscience")));
    CHECK(p.OrderOpaqueCastsByRank());
    CHECK(p.FixesTrickBeforeSweeperOrder());
    {
        const StuffyArm off(0);
        CHECK_FALSE(p.OrderOpaqueCastsByRank());
        CHECK_FALSE(p.FixesTrickBeforeSweeperOrder());
        CHECK_FALSE(p.MainPhaseOverride(s, DefRfx("Blasphemous Act")).has_value());
        CHECK(p.CastOrderRank(s, DefRfx("Loran's Escape")) == g.CastOrderRank(s, DefRfx("Loran's Escape")));
    }
}

TEST_CASE("rfx step 11: a main-2 plan casting Loran's Escape AND a sweeper casts the Escape FIRST (no searched reverse)")
{
    GameState b = HostBoard();
    b.m_provider = &StuffyP();
    b.uses_second_main = true;
    b.phase = Phase::PostCombatMain;
    for (int i = 0; i < 3; ++i) { PutRfx(b, "Plains", 4201 + i); }
    for (int i = 0; i < 3; ++i) { PutRfx(b, "Mountain", 4204 + i); }
    PutRfx(b, "Boros Reckoner", 4210);
    PutRfx(b, "Mogg Maniac", 4211);
    for (int i = 0; i < 4; ++i) { TokenRfx(b, 4220 + i, 1, 1, 1, "Spirit"); }
    HandRfx(b, "Blasphemous Act", 4230);   // FIRST in hand
    HandRfx(b, "Loran's Escape", 4231);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Plains").card; m.m_number = 4240 + i; b.players[0].library.push_back(m); }
    for (int arm_order : { -1, 0 })
    {
        INFO("cast-order arm " << arm_order);
        const StuffyArm arm(1, arm_order);
        int both = 0, escape_first = 0, act_first = 0;
        for (const auto& plan : TurnSolver::EnumerateMainPlans(b, false))
        {
            const std::vector<std::string> order = TurnSolver::RealisedNonSacCastOrder(b, plan);
            const auto ia = std::find(order.begin(), order.end(), "Blasphemous Act");
            const auto ie = std::find(order.begin(), order.end(), "Loran's Escape");
            if (ia == order.end() || ie == order.end()) { continue; }
            ++both;
            (ie < ia ? escape_first : act_first) += 1;
        }
        CHECK(both >= 1);
        CHECK(escape_first >= 1);
        if (arm_order < 0) { CHECK(act_first == 0); }   // the USER's order: never the Act first
        else { CHECK(act_first >= 1); }                 // step 7's searched order: both offered
    }
}

TEST_CASE("rfx step 11: Mind Stone's sac-draw -- a dig source in main 1, none in main 2 (autonomous); =0 keeps both")
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.uses_second_main = true;
    PutRfx(s, "Mountain", 4301);
    PutRfx(s, "Mountain", 4302);
    PutRfx(s, "Mind Stone", 4303);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 4310 + i; s.players[0].library.push_back(m); }
    {
        const StuffyArm arm(-1);
        s.phase = Phase::PreCombatMain;
        CHECK(StuffyP().HasAnyDigSource(s));
        s.phase = Phase::PostCombatMain;
        CHECK_FALSE(StuffyP().HasAnyDigSource(s));
    }
    {
        const StuffyArm arm(0);
        s.phase = Phase::PostCombatMain;
        CHECK(StuffyP().HasAnyDigSource(s));
    }
}

// ---- Step 9f: the BEST order of simultaneous triggers; the Shaman pull asked once per chain ----------------------
// USER 2026-11-18 ~07:30, item 2: "The simultaneous triggers using one order is fine, except we need to make sure that
// it is the best order when the order matters." Item 8: "I'm not sure what prompts for pull, but once per loop might
// make sense." Design 4.7 / section 14 step 9f; dmgrx::OrderSimultaneousTriggers, dmgrx::PullChainScope.

namespace
{
// Every pin sequence a damage event can take, depth first (the fan's own probe-and-clone, by hand): the lowest
// opponent life any line reaches. `canonical_order` keeps every ORDER ordinal at its front (the canonical order)
// while every target choice is still enumerated.
int BestOppLife9f(const GameState& base, const std::function<void(GameState&)>& fire, bool canonical_order,
                  int* lines = nullptr, int* order_choices = nullptr)
{
    int best = 1 << 20;
    std::function<void(const std::vector<int>&)> rec = [&](const std::vector<int>& pins) {
        GameState s = base;
        std::vector<std::vector<int>> seen;
        {
            dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { seen.push_back(legal); };
            dmgrx::TargetProbeScope probing(probe, pins, 0);
            dmgrx::PinScope scope(pins);
            fire(s);
        }
        if (seen.size() <= pins.size())
        {
            best = std::min(best, s.players[1].life);
            if (lines) { ++*lines; }
            return;
        }
        const std::vector<int>& nx = seen[pins.size()];
        const bool order = dmgrx::IsOrderCode(nx.front());
        if (order && order_choices) { ++*order_choices; }
        for (int o : nx)
        {
            if (canonical_order && order && o != nx.front()) { continue; }
            std::vector<int> p = pins;
            p.push_back(o);
            rec(p);
        }
    };
    rec({});
    return best;
}

// The contrived ORDER-MATTERS board: two Spiteful Slivers (#2903, #2904 -- every Sliver reflects TWICE while both
// live) and two Boros Reckoners, B (#2902, first on the battlefield) and A (#2901). One event deals B 3 (lethal) and
// A 1 (A survives). B's trigger (3) is canonically first. Best line: A's 1 into a Sliver first (it survives: 2 to the
// face), THEN B's 3 into a Sliver (lethal, still x2 by LKI: 6) = 8. B first can reach only 7: its lethal 3 kills an
// amplifier, so A's 1 is reflected once.
GameState OrderMattersBoard()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    PutRfx(s, "Boros Reckoner", 2902).entered_this_turn = true;
    PutRfx(s, "Boros Reckoner", 2901).entered_this_turn = true;
    PutRfx(s, "Spiteful Sliver", 2903).entered_this_turn = true;
    PutRfx(s, "Spiteful Sliver", 2904).entered_this_turn = true;
    return s;
}
void FireOrderMattersEvent(GameState& s)
{
    dmgrx::Batch b;
    b.src.push_back(dmgrx::SourceRef{ DefRfx("Chain Reaction").card, 0, false });
    b.hits.push_back({ 0, { false, 2902 }, 3, false, false });
    b.hits.push_back({ 0, { false, 2901 }, 1, false, false });
    dmgrx::DealDamageBatch(s, b);
}
// Chain Reaction X = 2 on a Guilty-Conscience-enchanted Boros Reckoner (#2911) and a Screaming Nemesis (#2913): both
// survive with 2 marked, and both "any (other) target" triggers can hit the other creature -- a REAL line where the
// order is not provably inert (the GC breaks every F2 fold), so it is searched.
GameState ChainOnReckonerAndNemesis()
{
    GameState s = ArmedState();
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 4; ++i) { PutRfx(s, "Mountain", 2920 + i); }
    PutRfx(s, "Boros Reckoner", 2911).entered_this_turn = true;
    AttachGc(s, 2912, 2911);
    PutRfx(s, "Screaming Nemesis", 2913).entered_this_turn = true;
    Card chain = DefRfx("Chain Reaction").card;
    chain.m_number = 2930;
    s.players[0].hand.push_back(chain);
    for (int i = 0; i < 10; ++i)
    { Card m = DefRfx("Mountain").card; m.m_number = 2940 + i; s.players[0].library.push_back(m); }
    return s;
}
}   // namespace

TEST_CASE("rfx step 9f: order codes -- encoding, disjoint from every other pin code")
{
    CHECK(dmgrx::IsOrderCode(dmgrx::OrderCode(0)));
    CHECK(dmgrx::IsOrderCode(dmgrx::OrderCode((1u << 24) - 1)));
    CHECK(dmgrx::OrderDecode(dmgrx::OrderCode(7)) == 7);
    CHECK_FALSE(dmgrx::IsOrderCode(dmgrx::kCombatPinMark));
    CHECK_FALSE(dmgrx::IsOrderCode(dmgrx::AttackHoldCode((1 << 27) - 1)));
    CHECK_FALSE(dmgrx::IsAttackHoldCode(dmgrx::OrderCode(0)));
    CHECK_FALSE(dmgrx::IsOrderCode(dmgrx::PullCode((1 << 28) - 1)));
    CHECK_FALSE(dmgrx::IsOrderCode(dmgrx::PlayerId(1)));
}

TEST_CASE("rfx step 9f: where the order CAN matter it is a searched choice, and the searched order is the BEST one")
{
    const GameState s = OrderMattersBoard();
    int lines = 0, order_choices = 0;
    const int searched = BestOppLife9f(s, FireOrderMattersEvent, false, &lines, &order_choices);
    const int canonical = BestOppLife9f(s, FireOrderMattersEvent, true);
    CHECK(order_choices >= 1);
    CHECK(searched == 20 - 8);     // A's 1 first (x2, the Sliver lives), then B's 3 (x2 by LKI)
    CHECK(canonical == 20 - 7);    // the canonical order (B first) cannot reach it
    // The pinned order is honoured: A's trigger (raised index 1) first, then both into Sliver #2903.
    GameState t = s;
    const std::vector<int> pins{ dmgrx::OrderCode(1), 2903, 2903 };
    {
        dmgrx::PinScope scope(pins);
        FireOrderMattersEvent(t);
    }
    CHECK(t.players[1].life == 20 - 8);
    // No pin: the front -- the canonical order, and the face (the legacy outcome).
    GameState u = s;
    FireOrderMattersEvent(u);
    CHECK(u.players[1].life == 20 - 4);
}

TEST_CASE("rfx step 9f: the sound folds -- no order choice where the order provably cannot matter")
{
    auto order_ordinals = [](GameState s, const std::function<void(GameState&)>& fire) {
        int n = 0;
        const std::vector<int> pins;
        dmgrx::TargetProbe probe = [&](const std::vector<int>& legal) { if (dmgrx::IsOrderCode(legal.front())) { ++n; } };
        dmgrx::TargetProbeScope probing(probe, pins, 0);
        dmgrx::PinScope scope(pins);
        fire(s);
        return n;
    };
    {   // O1: two Spiteful Slivers under the Act -- every trigger is a players-only "target player" (F1: the face)
        GameState s = ArmedState();
        PutRfx(s, "Spiteful Sliver", 2951);
        PutRfx(s, "Spiteful Sliver", 2952);
        PutRfx(s, "Mogg Maniac", 2953);
        CHECK(order_ordinals(s, [](GameState& g) { PerformDamageAllCreatures(g, 0, DefRfx("Blasphemous Act"), 13); }) == 0);
        GameState g = s;
        PerformDamageAllCreatures(g, 0, DefRfx("Blasphemous Act"), 13);
        CHECK(g.players[1].life <= 0);   // 13 x 2 per Sliver + the Maniac's 13
    }
    {   // O3: the Act on two Reckoners beside a GC'd Doll that chose the opponent -- either trigger can start the
        // proven loop, so the canonical order already reaches the kill
        GameState s = ArmedState();
        PutRfx(s, "Boros Reckoner", 2961);
        PutRfx(s, "Screaming Nemesis", 2962);
        PutRfx(s, "Stuffy Doll", 2963).chosen_player = 1;
        AttachGc(s, 2964, 2963);
        CHECK(order_ordinals(s, [](GameState& g) { PerformDamageAllCreatures(g, 0, DefRfx("Blasphemous Act"), 13); }) == 0);
    }
    {   // O3: the Doll's own ping with a GC (its reflection + the GC trigger: the loop itself)
        GameState s = ArmedState();
        PutRfx(s, "Stuffy Doll", 2971).chosen_player = 1;
        AttachGc(s, 2972, 2971);
        CHECK(order_ordinals(s, [](GameState& g) { ApplyPermAbility(g, 0, 2971, PermAbilityMode::SelfPing); }) == 0);
        GameState g = s;
        ApplyPermAbility(g, 0, 2971, PermAbilityMode::SelfPing);
        CHECK(g.players[1].life == 0);
    }
    {   // O2: two GC'd Mogg Maniacs attack -- two FIXED chains on different creatures (no choice anywhere)
        GameState s = ArmedState();
        PutRfx(s, "Mogg Maniac", 2981);
        AttachGc(s, 2982, 2981);
        PutRfx(s, "Mogg Maniac", 2983);
        AttachGc(s, 2984, 2983);
        CHECK(order_ordinals(s, [](GameState& g) { dmgrx::CombatDamageDealt(g, { 2981, 2983 }, { 1, 1 }); }) == 0);
    }
}

TEST_CASE("rfx step 9f: the search BRANCHES the order on a real line (Chain Reaction X=2, GC'd Reckoner + Nemesis), "
          "and every branch replays identically in the executor")
{
    const GameState initial = ChainOnReckonerAndNemesis();
    const auto plans = TurnSolver::EnumerateMainPlans(initial, true);
    std::set<int> first_order;
    int checked = 0;
    for (const auto& p : plans)
    {
        const bool casts_chain = std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a)
            { return a.card_name.str() == "Chain Reaction"; });
        if (!casts_chain) { continue; }
        REQUIRE_FALSE(p.reflect_targets.empty());
        const int first = p.reflect_targets.front();
        if (dmgrx::IsOrderCode(first)) { first_order.insert(first); }
        GameState roll = initial;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = initial;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        eng.TakeTurn(exec, true, ResolveStackRfx);
        CHECK(exec.players[1].life == roll.players[1].life);
        CHECK((Find(exec, 2911) != nullptr) == (Find(roll, 2911) != nullptr));
        CHECK((Find(exec, 2913) != nullptr) == (Find(roll, 2913) != nullptr));
        ++checked;
    }
    // Both orders are branched (the Reckoner's trigger first = raised index 0, the Nemesis's = 1); none is picked.
    CHECK(first_order == std::set<int>{ dmgrx::OrderCode(0), dmgrx::OrderCode(1) });
    CHECK(checked >= 2);
}

TEST_CASE("rfx step 9f: human play is asked the order through the dig chooser '(reflection order)'; -1 = the AI's order")
{
    const GameState s = OrderMattersBoard();
    int asked = 0;
    std::vector<std::string> names;
    DigChooser chooser = [&](const GameState&, int, const std::string& source, const std::vector<Card>& examined,
                             const std::vector<int>& legal, int dflt) -> int
    {
        ++asked;
        CHECK(source.find("(reflection order)") != std::string::npos);
        CHECK(legal.size() == examined.size());
        CHECK(dflt == 0);   // the canonical next (nothing pinned)
        names.clear();
        for (const Card& c : examined) { names.push_back(c.m_name.str()); }
        return 1;           // A's trigger (the second tile) resolves first
    };
    DigChooser* saved = g_play_dig_chooser;
    g_play_dig_chooser = &chooser;
    GameState t = s;
    FireOrderMattersEvent(t);
    g_play_dig_chooser = saved;
    CHECK(asked == 1);      // two classes: one answer places both
    CHECK(names == std::vector<std::string>{ "Boros Reckoner", "Boros Reckoner" });
    CHECK(t.players[1].life == 20 - 4);   // targets unpinned: both at the face, in the human's order
}

TEST_CASE("rfx step 9f: the Shaman pull is asked ONCE per damage chain (a Doll + GC loop); 'ask again' re-asks")
{
    auto run = [](bool ask_again, int& asked) {
        GameState s = ShamanBoard(20, 2, 2);
        AttachGc(s, 2050, 2010);
        asked = 0;
        TargetChooser chooser = [&](const GameState&, const CardDefinition& def, int, int, int,
                                    const std::vector<ChosenTarget>& dflt) -> std::vector<ChosenTarget>
        {
            if (dmgrx::t_viewer_pull == nullptr) { return dflt; }
            CHECK(def.params.redirect_pull_cost.has_value());
            ++asked;
            if (ask_again) { return { ChosenTarget{ dmgrx::kPullAskAgainKind, -1, 0 } }; }
            return {};   // decline: for the rest of this chain
        };
        TargetChooser* saved = g_play_target_chooser;
        g_play_target_chooser = &chooser;
        ApplyPermAbility(s, 0, 2010, PermAbilityMode::SelfPing);
        g_play_target_chooser = saved;
        CHECK(s.players[1].life == 0);       // no pull: the loop runs to the kill
        CHECK(UntappedLands(s) == 4);        // and no mana was spent
        return s;
    };
    int once = 0, again = 0;
    run(false, once);
    run(true, again);
    CHECK(once == 1);        // the ping's event asks; every Guilty Conscience event after it is the same chain
    CHECK(again == 20);      // "ask again": the ping + one per Guilty Conscience hit on the Doll (19: the 20th
                             // reflection kills before its GC trigger resolves)
    {   // a NEW chain asks again: two separate top-level events, each asked once
        GameState s = ShamanBoard(20, 2, 2);   // no GC: each ping is one short chain (ping -> reflection)
        int asked = 0;
        TargetChooser chooser = [&](const GameState&, const CardDefinition&, int, int, int,
                                    const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget>
        { if (dmgrx::t_viewer_pull != nullptr) { ++asked; } return {}; };
        TargetChooser* saved = g_play_target_chooser;
        g_play_target_chooser = &chooser;
        ApplyPermAbility(s, 0, 2010, PermAbilityMode::SelfPing);
        s.battlefield[1].tapped = false;   // untap the Doll by hand for a second, separate event
        ApplyPermAbility(s, 0, 2010, PermAbilityMode::SelfPing);
        g_play_target_chooser = saved;
        CHECK(asked == 2);
        CHECK(s.players[1].life == 18);
    }
}


std::uint64_t RfxCycleStateKey(const GameState& state);   // TurnSolver.cpp (dmgrx::CycleFold's configuration key)

TEST_CASE("rfx runaway fix: the zero-progress CYCLE FOLD -- an indestructible Nemesis aimed at a Nomads en-Kor whose "
          "shield returns the point is a cycle; a pinned cycle is reported, a face pick is not")
{
    // Generic provider: every rider window admitted (the MTG_UNPRUNE=redirectwindow shape). The Nemesis is the one
    // sink reflector, so each Nomads' shield list is the F4 singleton {all onto the Nemesis}: no ordinal. The
    // Nemesis's "any other target" list is {opponent, Nomads A, Nomads B} (our face: F1).
    auto run = [](const std::vector<int>& pins, std::size_t& calls) {
        GameState s = ArmedState();
        s.players[1].life = 20;
        PutRfx(s, "Screaming Nemesis", 3301).temp_indestructible = true;
        PutRfx(s, "Nomads en-Kor", 3302);
        PutRfx(s, "Nomads en-Kor", 3303);
        std::size_t seen_calls = 0;
        dmgrx::TargetProbe probe = [&](const std::vector<int>&) { ++seen_calls; };
        dmgrx::CycleFold cyc;
        cyc.state = &s;
        cyc.state_key = &RfxCycleStateKey;
        {
            dmgrx::PinScope pin_scope(pins);
            dmgrx::TargetProbeScope probing(probe, pins, 0);
            const dmgrx::CycleFoldScope cyc_scope(&cyc);
            dmgrx::DealDamageBatch(s, ReflectionBatch(s, 3301, 1));
        }
        calls = seen_calls;
        CHECK(cyc.calls == seen_calls);   // the fold's ordinal is the fan's
        return std::make_pair(cyc.repeat_at, s.players[1].life);
    };
    const std::size_t none = static_cast<std::size_t>(-1);
    std::size_t calls = 0;
    {
        // Nomads A, then Nomads B: back to the first choice's configuration at choice 1 (the Nemesis's marked damage
        // differs and is inert -- it is indestructible). Then the face: 1 to the opponent.
        const auto [rep, life] = run({ 3302, 3303, dmgrx::PlayerId(1) }, calls);
        CHECK(rep == 1);
        CHECK(life == 19);
    }
    {
        const auto [rep, life] = run({ dmgrx::PlayerId(1) }, calls);   // straight to the face: no cycle
        CHECK(rep == none);
        CHECK(life == 19);
        CHECK(calls == 1);
    }
}

// ---- ESCAPE-SCRY ARMS (2026-11-19): Loran's Escape as a bare scry 1 -- arm P (prune, LOSSY, DEFAULT OFF) and arm W
// (move-ordered last, ORDER only, DEFAULT OFF). USER 11-17 ~21:35: "I would prune Loran's Escape even when we have no
// guilty conscience to one creature or perhaps even not castable. It is essentially a Scry 1." USER 11-19 ~06:20: "the
// purpose of that card in the deck is primarily for indestructible and the combo, not for incidental scry 1."
namespace
{
// Pin the escape-scry slots (and optionally the 9b unprune lever) for one test; -1 = the env default, restored on exit.
struct EscArm
{
    EscArm(int prune, int last, int unprune = -1)
    {
        heurarm::t_arm[heurarm::STUFFY_ESCAPE_SCRY_PRUNE] = static_cast<std::int8_t>(prune);
        heurarm::t_arm[heurarm::STUFFY_ESCAPE_SCRY_LAST]  = static_cast<std::int8_t>(last);
        heurarm::t_arm[heurarm::STUFFY_UNPRUNE_9B]        = static_cast<std::int8_t>(unprune);
    }
    ~EscArm()
    {
        heurarm::t_arm[heurarm::STUFFY_ESCAPE_SCRY_PRUNE] = -1;
        heurarm::t_arm[heurarm::STUFFY_ESCAPE_SCRY_LAST]  = -1;
        heurarm::t_arm[heurarm::STUFFY_UNPRUNE_9B]        = -1;
    }
};
Action EscapeAt(int target, const std::string& hand_name = std::string())
{
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.def = &DefRfx("Loran's Escape");
    a.card_name = a.def->card.m_name;
    a.enchant_target = target;
    a.trick_hand_target = hand_name;
    return a;
}
// The seed-5 gi-4 T1 shape: our Plains, the opponent's Orchard Spirit (spawned on the Orchard's play), no creature of
// ours, Loran's Escape in hand.
GameState EscapeSpiritBoard()
{
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 4001);
    TokenRfx(s, 4002, 1, 1, 1, "Spirit");
    Card e = DefRfx("Loran's Escape").card; e.m_number = 4010; s.players[0].hand.push_back(e);
    for (int i = 0; i < 10; ++i) { Card m = DefRfx("Mountain").card; m.m_number = 4020 + i; s.players[0].library.push_back(m); }
    return s;
}
bool AnyEscapePlan(const GameState& s)
{
    for (const auto& p : TurnSolver::EnumerateMainPlans(s, true)) { if (LoranTargetOf(p) != 0) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("escape-scry arm P: the seed-5 shape (only an opponent Spirit to target) -- no Escape under the arm; base keeps it")
{
    const bool open = DecisionUnpruned(UnprunedGate::EscapeTarget);
    {
        const EscArm arm(0, 0);
        CHECK(AnyEscapePlan(EscapeSpiritBoard()));
    }
    {
        const EscArm arm(1, 0);
        CHECK(AnyEscapePlan(EscapeSpiritBoard()) == open);
    }
    {
        const EscArm arm(1, 0, 1);   // MTG_STUFFY_UNPRUNE_9B lifts arm P with the step-9b prunes
        CHECK(AnyEscapePlan(EscapeSpiritBoard()));
    }
}

TEST_CASE("escape-scry arm P: the predicate -- useful only on our non-indestructible creature with damage this turn")
{
    if (DecisionUnpruned(UnprunedGate::EscapeTarget)) { return; }   // the arm is lifted wholesale (case above)
    const EscArm arm(1, 0);
    const StuffyDollProvider& P = StuffyP();
    GameState s = HostBoard();
    s.m_provider = &StuffyP();
    s.deck_has_spell_target_watch = false;
    PutRfx(s, "Plains", 4101);
    for (int i = 0; i < 6; ++i) { PutRfx(s, "Mountain", 4102 + i); }   // 7 mana
    PutRfx(s, "Boros Reckoner", 4110);
    PutRfx(s, "Stuffy Doll", 4111).chosen_player = 1;
    TokenRfx(s, 4112, 1, 1, 1, "Spirit");
    Card e = DefRfx("Loran's Escape").card; e.m_number = 4090; s.players[0].hand.push_back(e);
    // No GC, no sweeper: every target is a bare scry -- refused, in either main.
    CHECK_FALSE(P.CollectedActionAllowed(s, EscapeAt(4110), true));
    CHECK_FALSE(P.CollectedActionAllowed(s, EscapeAt(4110), false));
    CHECK_FALSE(P.CollectedActionAllowed(s, EscapeAt(4111), true));   // the Doll (printed indestructible)
    CHECK_FALSE(P.CollectedActionAllowed(s, EscapeAt(4112), true));   // the opponent's Spirit
    // A non-Escape action is untouched by arm P (post-combat: the main split has nothing to say about it).
    Action other = EscapeAt(4110);
    other.def = &DefRfx("Boros Reckoner");
    other.card_name = other.def->card.m_name;
    CHECK(P.CollectedActionAllowed(s, other, false));
    // Blasphemous Act castable alongside (3 creatures: {5}{R} + the Escape's {W} = 7): the Reckoner is useful.
    GameState a = s;
    Card act = DefRfx("Blasphemous Act").card; act.m_number = 4091; a.players[0].hand.push_back(act);
    CHECK(P.CollectedActionAllowed(a, EscapeAt(4110), true));
    CHECK_FALSE(P.CollectedActionAllowed(a, EscapeAt(4111), true));   // still nothing to gain on the Doll
    CHECK_FALSE(P.CollectedActionAllowed(a, EscapeAt(4112), true));   // nor on theirs
    // ...but not with one mana short: the Act and the Escape do not both fit.
    GameState a6 = a;
    for (Permanent& q : a6.battlefield) { if (q.card.m_number == 4107) { q.tapped = true; } }
    CHECK_FALSE(P.CollectedActionAllowed(a6, EscapeAt(4110), true));
    // An Escaped (already indestructible) Reckoner gains nothing even with the Act in hand.
    GameState ai = a;
    for (Permanent& q : ai.battlefield) { if (q.card.m_number == 4110) { q.temp_indestructible = true; } }
    CHECK_FALSE(P.CollectedActionAllowed(ai, EscapeAt(4110), true));
    // Guilty Conscience on the Reckoner: useful; GC on the Doll alone is no source for the Reckoner.
    GameState g = s;
    AttachGc(g, 4092, 4110);
    CHECK(P.CollectedActionAllowed(g, EscapeAt(4110), true));
    GameState gd = s;
    AttachGc(gd, 4093, 4111);
    CHECK_FALSE(P.CollectedActionAllowed(gd, EscapeAt(4110), true));
    // A Guilty Conscience in hand, castable alongside: useful.
    GameState gh = s;
    Card gc = DefRfx("Guilty Conscience").card; gc.m_number = 4094; gh.players[0].hand.push_back(gc);
    CHECK(P.CollectedActionAllowed(gh, EscapeAt(4110), true));
    // A same-plan HAND target (Mogg Maniac {1}{R}) with the Act: 1 + 2 + (9 - 4 creatures = 5) = 8 > 7 -> refused;
    // one more land -> kept.
    GameState h = a;
    Card mm = DefRfx("Mogg Maniac").card; mm.m_number = 4095; h.players[0].hand.push_back(mm);
    CHECK_FALSE(P.CollectedActionAllowed(h, EscapeAt(4095, "Mogg Maniac"), true));
    PutRfx(h, "Mountain", 4108);
    CHECK(P.CollectedActionAllowed(h, EscapeAt(4095, "Mogg Maniac"), true));
    // Arm OFF: nothing is refused (the main split refuses only sweepers, in main 1).
    const EscArm off(0, 0);
    CHECK(P.CollectedActionAllowed(s, EscapeAt(4112), true));
    CHECK(P.CollectedActionAllowed(s, EscapeAt(4110), true));
}

TEST_CASE("escape-scry arm W: a scry-only Escape plan is move-ordered LAST; nothing is removed")
{
    if (DecisionUnpruned(UnprunedGate::EscapeTarget)) { return; }
    GameState s = EscapeSpiritBoard();
    PutRfx(s, "Plains", 4003);
    // A second, non-Escape option (a hand creature would trip the step-9b target prune's hand rule).
    Card r = DefRfx("Guilty Conscience").card; r.m_number = 4011; s.players[0].hand.push_back(r);
    auto census = [&](int& esc, int& esc_back, int& other, int& other_back) {
        esc = esc_back = other = other_back = 0;
        for (const auto& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            const bool e = LoranTargetOf(p) != 0;
            (e ? esc : other) += 1;
            if (p.move_tier == -1) { (e ? esc_back : other_back) += 1; }
            CHECK(p.move_tier != 1);
        }
    };
    int e0, eb0, o0, ob0, e1, eb1, o1, ob1;
    { const EscArm arm(0, 0); census(e0, eb0, o0, ob0); }
    { const EscArm arm(0, 1); census(e1, eb1, o1, ob1); }
    CHECK(e0 >= 1);
    CHECK(e1 == e0);          // the same plans, every scry line still on the menu
    CHECK(o1 == o0);
    CHECK(eb0 == 0);          // arm off: no tier
    CHECK(ob0 == 0);
    CHECK(eb1 == e1);         // arm on: every scry-only Escape plan is at the back
    CHECK(ob1 == 0);          // and nothing else is
    // The hook itself: a selection holding the scry-only cast is at the back; an empty selection is not.
    const EscArm arm(0, 1);
    const StuffyDollProvider& P = StuffyP();
    std::vector<Action> cands{ EscapeAt(4002) };
    CHECK(P.PlanMoveOrderBack(s, cands, { 0 }, false));
    CHECK_FALSE(P.PlanMoveOrderBack(s, cands, {}, false));
}
