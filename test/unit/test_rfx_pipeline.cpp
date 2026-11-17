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
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
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
    CHECK(combat_pins == std::set<std::vector<int>>{ { dmgrx::PlayerId(1) }, { 1603 } });
    int doll_lines = 0, face_lines = 0;
    for (const auto& p : plans)
    {
        const std::vector<int> pins = dmgrx::CombatPinsOf(p.reflect_targets);
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
    CHECK(combat_pins == std::set<std::vector<int>>{ { dmgrx::PlayerId(1) }, { 1803 }, { 1804 } });
    for (const auto& p : plans)
    {
        const std::vector<int> pins = dmgrx::CombatPinsOf(p.reflect_targets);
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
