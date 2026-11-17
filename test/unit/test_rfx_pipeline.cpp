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
