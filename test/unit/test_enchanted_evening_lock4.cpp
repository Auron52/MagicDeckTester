// Enchanted Evening lock cluster, STEP 4 (2026-11-26; docs/design/enchanted-evening-lock-design.md §4.4 / §4.4.1):
// Parallax Wave -- Fading 5; "Remove a fade counter from this enchantment: Exile target creature"; the linked exile
// list returned when it leaves; the self-exile loop under Opalescence (CR 607.2a: the Wave returns as a new object with
// five counters, activations resolving after it left exile for good); Sterling Grove's shroud modelled as targeting
// legality (one Grove must be exiled first, two or more make the loop impossible); and the USER's ruling-7 deck
// default, PerformWaveOpalSweep, at the opponent's end step in BOTH worlds.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/RevelationPackages.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace
{
const char* const kWave  = "Parallax Wave";
const char* const kOpal  = "Opalescence";
const char* const kEE    = "Enchanted Evening";
const char* const kGrove = "Sterling Grove";
const char* const kExpl  = "Exploration";
const char* const kPres  = "Enchantress's Presence";

const CardDefinition& L4Def(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card L4Card(const std::string& name, int number)
{
    Card c = L4Def(name).card;
    c.m_number = number;
    return c;
}

int L4Put(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L4Card(name, number);
    p.controller_index = p.owner_index = controller;
    if (L4Def(name).params.fading_counters > 0) { p.fade_counters = L4Def(name).params.fading_counters; }
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int L4Spawn(GameState& s, int pw, int number, bool hexproof = false)
{
    Card c;
    c.m_name = std::to_string(pw) + "/" + std::to_string(pw) + (hexproof ? " Hexproof" : "") + " Creature";
    c.RehashName();
    c.AddType(CardType::Creature);
    if (hexproof) { c.AddKeyword(Keyword::Hexproof); }
    c.m_power = pw;
    c.m_toughness = pw;
    c.m_number = number;
    Permanent p;
    p.card = c;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState L4Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_float_wild_exact = true;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L4Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L4Card("Plains", 100900 + k)); }
    return s;
}

Permanent* L4Find(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}
const Permanent* L4Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int L4Index(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

const Card* L4InExile(const GameState& s, int number)
{
    for (const Card& c : s.exile) { if (c.m_number == number) { return &c; } }
    return nullptr;
}

int L4Count(const GameState& s, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == controller) { ++n; } }
    return n;
}

bool L4LegalHas(const GameState& s, const std::vector<int>& legal, int number)
{
    for (int i : legal) { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return true; } }
    return false;
}

// Every single-activation plan of the Wave numbered `wave`: its pinned target numbers.
std::vector<int> L4WaveTargets(const std::vector<TurnSolver::Plan>& plans, int wave)
{
    std::vector<int> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::FadeExileCreature
            && a.sac_source_id == wave)
        { out.push_back(a.sac_victim_id); }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<TurnSolver::Plan> L4WavePlan(const std::vector<TurnSolver::Plan>& plans, int wave, int target)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::FadeExileCreature
            && a.sac_source_id == wave && a.sac_victim_id == target)
        { return p; }
    }
    return std::nullopt;
}

struct L4Worlds
{
    AIEngine   eng{ MulliganProfile::DefaultProfile(), 3, 20 };
    GameEngine ge{ eng };
    GameState  roll;
    GameState  exec;
    void Run(const GameState& s, const TurnSolver::Plan& plan)
    {
        roll = s;
        TurnSolver::ApplyPlan(roll, plan, true);
        MtgTestSeam::CommitPhasePlan(eng, plan, true);
        exec = s;
        eng.TakeTurn(exec, true, [this](GameState& st) { MtgTestSeam::GeResolveStack(ge, st); });
    }
    // Our end step, the opponent's turn (its end-step window: the sweep), our untap and upkeep -- in both worlds.
    void EndTurn(const GameState& s)
    {
        roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        exec = s;
        MtgTestSeam::GeRunTurnFromEnd(ge, exec);
        exec.phase = Phase::Beginning;
        MtgTestSeam::GeUntapStep(ge, exec);
        MtgTestSeam::GeUpkeepStep(ge, exec);
    }
    void Check(const std::function<void(const GameState&)>& f) const
    {
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
            f(*g);
        }
    }
};

// A board for the end-step sweep: Wave (5 counters) + Opalescence + `groves` Sterling Groves, and `spawns` opponent 3/3
// spawn bodies (tokens). The end step of our turn.
GameState L4SweepBoard(int groves, int spawns)
{
    GameState s = L4Board();
    s.phase = Phase::Ending;
    s.deck_has_wave_opal_sweep = true;
    L4Put(s, kWave, 60);
    L4Put(s, kOpal, 54);
    for (int g = 0; g < groves; ++g) { L4Put(s, kGrove, 70 + g); }
    for (int k = 0; k < spawns; ++k) { L4Spawn(s, 3, 700001 + k); }
    RefreshTypeStatics(s);
    return s;
}
}   // namespace

TEST_CASE("Parallax Wave: cost, type, params; routed to the deck's provider; the sweep stamp needs Wave + Opalescence + the rule")
{
    const CardDefinition& w = L4Def(kWave);
    CHECK(w.card.IsEnchantment());
    CHECK(w.card.m_mana_cost.ManaValue() == 4);
    CHECK(w.card.m_mana_cost.white == 2);
    CHECK(w.params.fading_counters == 5);
    CHECK(w.params.fade_exile_target_creature);
    CHECK(revpkg::CardCanEndOwnEnchantment(w.params));
    Decklist only;
    only.mainboard.push_back(L4Card(kWave, 1));
    only.mainboard.push_back(L4Card("Forest", 2));
    CHECK(std::string(DetectDecisionProvider(only).Name()) == "EnchantedEvening");
    CHECK(DetectDecisionProvider(only).WaveOpalSweepRule());
    Decklist plain;
    plain.mainboard.push_back(L4Card("Forest", 2));
    CHECK_FALSE(DetectDecisionProvider(plain).WaveOpalSweepRule());   // a deck rule: no other provider adopts it
    // The stamp: the provider rule AND both params in the list.
    GameState a;
    a.m_provider = &DetectDecisionProvider(only);
    GoldFishRunner::StampDeckTraits(a, only);
    CHECK_FALSE(a.deck_has_wave_opal_sweep);   // no Opalescence in the list
    Decklist both = only;
    both.mainboard.push_back(L4Card(kOpal, 3));
    GameState b;
    b.m_provider = &DetectDecisionProvider(both);
    GoldFishRunner::StampDeckTraits(b, both);
    CHECK(b.deck_has_wave_opal_sweep);
    GameState c;   // no provider attached: never stamped
    GoldFishRunner::StampDeckTraits(c, both);
    CHECK_FALSE(c.deck_has_wave_opal_sweep);
}

TEST_CASE("Parallax Wave fading: five counters on a cast (both worlds) and on a put; upkeep removes one; sacrificed at zero")
{
    GameState s = L4Board();
    for (int k = 0; k < 4; ++k) { L4Put(s, "Plains", 101 + k); }
    s.players[0].hand.push_back(L4Card(kWave, 60));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::optional<TurnSolver::Plan> cast;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions.front().kind == Action::Kind::CastFromHand
            && p.actions.front().card_name.str() == kWave) { cast = p; break; }
    }
    REQUIRE(cast.has_value());
    L4Worlds w;
    w.Run(s, *cast);
    w.Check([](const GameState& g) {
        const Permanent* wave = L4Find(g, 60);
        REQUIRE(wave != nullptr);
        CHECK(wave->fade_counters == 5);   // put once, never twice (the cast site, then the cascade skips it)
    });
    // A put through the shared enter cascade (the route a Wave's own leave trigger returns it by).
    GameState p = L4Board();
    PutCardOntoBattlefield(p, 0, L4Card(kWave, 61), "test");
    REQUIRE(L4Find(p, 61) != nullptr);
    CHECK(L4Find(p, 61)->fade_counters == 5);
    // Upkeep: one off; at zero, the next upkeep sacrifices it.
    L4Find(p, 61)->fade_counters = 1;
    PerformUpkeepFading(p);
    REQUIRE(L4Find(p, 61) != nullptr);
    CHECK(L4Find(p, 61)->fade_counters == 0);
    PerformUpkeepFading(p);
    CHECK(L4Find(p, 61) == nullptr);
    bool in_gy = false;
    for (const Card& c : p.players[0].graveyard) { if (c.m_number == 61) { in_gy = true; } }
    CHECK(in_gy);
}

TEST_CASE("Parallax Wave activation: one searched plan per target class; a token ceases to exist; linked cards return when it leaves")
{
    // Without Opalescence: the Wave is no creature, so the opponent's spawns are the only targets. Two identical 2/2s
    // fold into one class; a 3/3 is its own.
    GameState s = L4Board();
    L4Put(s, kWave, 60);
    L4Spawn(s, 2, 700001);
    L4Spawn(s, 2, 700002);
    L4Spawn(s, 3, 700003);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<int> targets = L4WaveTargets(plans, 60);
    CHECK(targets.size() == 2);
    CHECK(std::find(targets.begin(), targets.end(), 700003) != targets.end());
    // "Do nothing more" stays offered: the enumeration holds only the activations (the drop is spent, no hand), and
    // every search host appends the pass when the list has none (IdlePassMissing -> AppendIdlePassAxes); element 0
    // of that pass is apply-empty.
    const std::vector<TurnSolver::Plan> pass = TurnSolver::IdlePassWithAxes(s);
    REQUIRE_FALSE(pass.empty());
    CHECK(pass.front().actions.empty());
    const std::optional<TurnSolver::Plan> hit = L4WavePlan(plans, 60, 700003);
    REQUIRE(hit.has_value());
    L4Worlds w;
    w.Run(s, *hit);
    w.Check([](const GameState& g) {
        CHECK(L4Find(g, 700003) == nullptr);
        CHECK(L4InExile(g, 700003) == nullptr);   // a token: it ceases to exist
        REQUIRE(L4Find(g, 60) != nullptr);
        CHECK(L4Find(g, 60)->fade_counters == 4);
        CHECK(L4Find(g, 60)->linked_exile_number == 0);   // nothing linked
    });

    // Under Opalescence: the opponent's Enchantress's Presence (a 3/3) and our Exploration (a 1/1) are linked; when the
    // Wave leaves (fading's sacrifice), each returns under its OWNER.
    GameState t = L4Board();
    L4Put(t, kWave, 60);
    L4Put(t, kOpal, 54);
    L4Put(t, kExpl, 40);
    L4Put(t, kPres, 100041, 1);
    RefreshTypeStatics(t);
    REQUIRE(L4Find(t, 100041)->card.IsCreature());
    ResolveFadeExileCreature(t, 0, 60, 100041);
    const int d1 = L4Find(t, 60)->linked_exile_number;
    CHECK(d1 >= 0x40000000);
    ResolveFadeExileCreature(t, 0, 60, 40);
    const int d2 = L4Find(t, 60)->linked_exile_number;
    CHECK(d2 >= 0x40000000);
    CHECK(d2 != d1);   // the digest separates the two linked sets
    REQUIRE(L4InExile(t, 100041) != nullptr);
    CHECK(CardIsExileUntilLeaves(*L4InExile(t, 100041)));
    CHECK(L4Find(t, 60)->fade_counters == 3);
    L4Find(t, 60)->fade_counters = 0;
    PerformUpkeepFading(t);
    CHECK(L4Find(t, 60) == nullptr);
    CHECK(t.exile.empty());
    REQUIRE(L4Find(t, 100041) != nullptr);
    CHECK(L4Find(t, 100041)->controller_index == 1);
    REQUIRE(L4Find(t, 40) != nullptr);
    CHECK(L4Find(t, 40)->controller_index == 0);
}

TEST_CASE("Parallax Wave self-exile under Opalescence: it returns as a new object with five counters, its linked cards with it")
{
    GameState s = L4Board();
    L4Put(s, kWave, 60);
    L4Put(s, kOpal, 54);
    L4Put(s, kExpl, 40);
    L4Spawn(s, 3, 700001);
    RefreshTypeStatics(s);
    L4Find(s, 60)->fade_counters = 3;
    // The self target is in the searched menu (the Wave is a creature and unshrouded).
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<int> targets = L4WaveTargets(plans, 60);
    CHECK(std::find(targets.begin(), targets.end(), 60) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), 40) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), 54) == targets.end());   // Opalescence does not animate itself
    // Explicit activations resolve one at a time: Exploration (linked), then the Wave itself.
    ResolveFadeExileCreature(s, 0, 60, 40);
    REQUIRE(L4Find(s, 40) == nullptr);
    ResolveFadeExileCreature(s, 0, 60, 60);
    REQUIRE(L4Find(s, 60) != nullptr);
    CHECK(L4Find(s, 60)->fade_counters == 5);
    CHECK(L4Find(s, 60)->linked_exile_number == 0);
    CHECK(L4Find(s, 60)->entered_this_turn);   // a new object
    CHECK(L4Find(s, 40) != nullptr);           // linked to the old object: it came back
    CHECK(s.exile.empty());
    // Both worlds through the plan: the self-target plan.
    GameState u = L4Board();
    L4Put(u, kWave, 60);
    L4Put(u, kOpal, 54);
    RefreshTypeStatics(u);
    L4Find(u, 60)->fade_counters = 1;
    const std::optional<TurnSolver::Plan> self = L4WavePlan(TurnSolver::EnumerateMainPlans(u, true), 60, 60);
    REQUIRE(self.has_value());
    L4Worlds w;
    w.Run(u, *self);
    w.Check([](const GameState& g) {
        REQUIRE(L4Find(g, 60) != nullptr);
        CHECK(L4Find(g, 60)->fade_counters == 5);
        CHECK(g.exile.empty());
    });
}

TEST_CASE("Parallax Wave self-loop legality: legal with 0 or 1 Sterling Grove (the Grove goes first), impossible with 2")
{
    for (int groves = 0; groves <= 2; ++groves)
    {
        CAPTURE(groves);
        GameState s = L4SweepBoard(groves, 0);
        const int wi = L4Index(s, 60);
        const std::vector<int> legal = FadeExileLegalTargets(s, 0, 60, /*fold=*/false);
        std::vector<int> en;
        const bool ok = WaveSelfTargetEnablers(s, 0, wi, en);
        if (groves == 0)
        {
            CHECK(L4LegalHas(s, legal, 60));
            CHECK(ok);
            CHECK(en.empty());
        }
        else if (groves == 1)
        {
            CHECK_FALSE(L4LegalHas(s, legal, 60));   // the Grove shrouds the Wave
            CHECK(L4LegalHas(s, legal, 70));         // a Grove does not shroud itself
            CHECK(ok);
            CHECK(en == std::vector<int>{ 70 });
        }
        else
        {
            CHECK_FALSE(L4LegalHas(s, legal, 60));
            CHECK_FALSE(L4LegalHas(s, legal, 70));   // each Grove shrouds the other
            CHECK_FALSE(L4LegalHas(s, legal, 71));
            CHECK_FALSE(ok);
        }
    }
}

TEST_CASE("Wave + Opalescence end-step sweep (USER ruling 7) with 0, 1 and 4 Groves, in both worlds")
{
    for (int groves : { 0, 1, 4 })
    {
        CAPTURE(groves);
        const GameState s = L4SweepBoard(groves, groves == 4 ? 3 : 6);
        L4Worlds w;
        w.EndTurn(s);
        w.Check([groves](const GameState& g) {
            REQUIRE(L4Find(g, 60) != nullptr);
            if (groves == 4)
            {
                // Impossible loop: the finite tail spends one counter per opponent creature (the orchestrator's
                // ruling-7 default) -- 5 - 3 = 2, then our upkeep took one; the Wave never left.
                CHECK(L4Count(g, 1) == 0);
                CHECK(L4Find(g, 60)->fade_counters == 1);
                CHECK_FALSE(L4Find(g, 60)->entered_this_turn);
                CHECK(g.exile.empty());                       // tokens cease to exist
            }
            else
            {
                CHECK(L4Count(g, 1) == 0);                    // every opponent creature is gone for good
                CHECK(L4Find(g, 60)->fade_counters == 4);     // returned with five, then our upkeep took one
                CHECK(g.exile.empty());                       // tokens; and the Wave / Groves came back
            }
            int gs = 0;
            for (const Permanent& p : g.battlefield) { if (p.card.m_name.str() == kGrove) { ++gs; } }
            CHECK(gs == groves);                              // the Grove enabler returned
            CHECK(L4Find(g, 54) != nullptr);
        });
        // Both worlds reach the same board.
        CHECK(L4Count(w.roll, 0) == L4Count(w.exec, 0));
        CHECK(L4Count(w.roll, 1) == L4Count(w.exec, 1));
    }
}

TEST_CASE("Wave sweep, no loop possible (2 Groves, or too few counters): linked exiles until the counters run out")
{
    // ORCHESTRATOR DEFAULT (pending the USER): ruling 7 literally -- every targetable opponent creature, one counter
    // each, LINKED (the Wave stays on the battlefield, CR 607.2a); the rest stay when the counters run out.
    GameState s = L4SweepBoard(2, 0);
    L4Put(s, kPres, 100041, 1);   // two opponent CARD creatures first in battlefield order, then four tokens
    L4Put(s, kExpl, 100042, 1);
    for (int k = 0; k < 4; ++k) { L4Spawn(s, 3, 700001 + k); }
    RefreshTypeStatics(s);
    REQUIRE(L4Count(s, 1) == 6);
    {
        GameState t = s;
        PerformWaveOpalSweep(t);
        REQUIRE(L4Find(t, 60) != nullptr);
        CHECK(L4Find(t, 60)->fade_counters == 0);          // all five spent
        CHECK(L4Count(t, 1) == 1);                          // the sixth stays
        CHECK(L4Find(t, 700004) != nullptr);
        const Card* c = L4InExile(t, 100041);
        REQUIRE(c != nullptr);
        CHECK(CardIsExileUntilLeaves(*c));                  // linked to the Wave
        CHECK(ExileUntilLeavesSource(*c) == 60);
        CHECK(L4Find(t, 60)->linked_exile_number != 0);
        CHECK(L4Find(t, 70) != nullptr);                    // the Groves never left
        CHECK(L4Find(t, 71) != nullptr);
        CHECK_FALSE(L4Find(t, 60)->entered_this_turn);
    }
    // Both worlds through our upkeep: the Wave at zero cannot lose a counter and is sacrificed (CR 702.32a); its
    // linked cards return to the opponent, the exiled tokens do not.
    L4Worlds w;
    w.EndTurn(s);
    w.Check([](const GameState& g) {
        CHECK(L4Find(g, 60) == nullptr);
        CHECK(L4Find(g, 100041) != nullptr);
        CHECK(L4Find(g, 100042) != nullptr);
        CHECK(L4Find(g, 700004) != nullptr);
        CHECK(L4Count(g, 1) == 3);
        CHECK(L4InExile(g, 100041) == nullptr);
    });
    {
        GameState t = L4SweepBoard(1, 2);   // one Grove but one counter: the loop needs two -> one linked exile
        L4Find(t, 60)->fade_counters = 1;
        PerformWaveOpalSweep(t);
        REQUIRE(L4Find(t, 60) != nullptr);
        CHECK(L4Find(t, 60)->fade_counters == 0);
        CHECK(L4Count(t, 1) == 1);
        CHECK(L4Find(t, 70) != nullptr);
    }
}

TEST_CASE("Wave sweep: opponent CARDS are exiled for good (never linked), a hexproof creature is skipped")
{
    GameState s = L4SweepBoard(0, 2);
    L4Put(s, kPres, 100041, 1);          // an opponent card creature under Opalescence (3/3)
    L4Spawn(s, 3, 700050, /*hexproof=*/true);
    RefreshTypeStatics(s);
    L4Worlds w;
    w.EndTurn(s);
    w.Check([](const GameState& g) {
        CHECK(L4Find(g, 700001) == nullptr);
        CHECK(L4Find(g, 700002) == nullptr);
        CHECK(L4Find(g, 100041) == nullptr);
        const Card* c = L4InExile(g, 100041);
        REQUIRE(c != nullptr);
        CHECK_FALSE(CardIsExileUntilLeaves(*c));   // permanent: its activation resolved after the old Wave left
        CHECK(L4Find(g, 700050) != nullptr);       // hexproof: the Wave cannot target it
        REQUIRE(L4Find(g, 60) != nullptr);
        CHECK(L4Find(g, 60)->linked_exile_number == 0);
    });
}

TEST_CASE("Wave sweep with Enchanted Evening: every opponent permanent is exiled (or dies as a 0/0 land)")
{
    GameState s = L4SweepBoard(1, 2);
    L4Put(s, kEE, 51);
    L4Put(s, kPres, 100041, 1);
    L4Put(s, kExpl, 100042, 1);
    L4Put(s, "Plains", 100001, 1);
    RefreshTypeStatics(s);
    TypeStaticsSbaCheckpoint(s);   // the opponent's Plains and its MV-0 spawns die as 0/0s
    REQUIRE(L4Count(s, 1) == 2);   // its Presence (3/3) and Exploration (1/1) remain
    L4Worlds w;
    w.EndTurn(s);
    w.Check([](const GameState& g) {
        CHECK(L4Count(g, 1) == 0);
        CHECK(L4InExile(g, 100041) != nullptr);
        CHECK(L4InExile(g, 100042) != nullptr);
        REQUIRE(L4Find(g, 60) != nullptr);
        CHECK(L4Find(g, 70) != nullptr);   // the Grove came back (a 2/2 under EE + Opalescence: it survives)
        CHECK(L4Find(g, 51) != nullptr);
    });
}

TEST_CASE("Wave sweep: no opponent creature -> a Wave below five is topped up; never with something linked; never unstamped")
{
    GameState s = L4SweepBoard(1, 0);
    L4Find(s, 60)->fade_counters = 2;
    {
        GameState t = s;
        PerformWaveOpalSweep(t);
        REQUIRE(L4Find(t, 60) != nullptr);
        CHECK(L4Find(t, 60)->fade_counters == 5);
        CHECK(L4Find(t, 70) != nullptr);
        CHECK(t.exile.empty());
    }
    {
        GameState t = s;   // one counter short of Grove + self: nothing happens
        L4Find(t, 60)->fade_counters = 1;
        PerformWaveOpalSweep(t);
        CHECK(L4Find(t, 60)->fade_counters == 1);
        CHECK_FALSE(L4Find(t, 60)->entered_this_turn);
    }
    {
        GameState t = s;   // our Grove linked by an earlier activation: left alone
        L4Find(t, 60)->fade_counters = 4;
        ResolveFadeExileCreature(t, 0, 60, 70);
        REQUIRE(L4Find(t, 60)->fade_counters == 3);
        REQUIRE(L4Find(t, 60)->linked_exile_number != 0);
        PerformWaveOpalSweep(t);
        CHECK(L4Find(t, 60)->fade_counters == 3);
        CHECK(L4InExile(t, 70) != nullptr);
    }
    {
        GameState t = s;
        t.deck_has_wave_opal_sweep = false;
        L4Spawn(t, 3, 700001);
        PerformWaveOpalSweep(t);
        CHECK(L4Find(t, 700001) != nullptr);
        CHECK(L4Find(t, 60)->fade_counters == 2);
    }
}
