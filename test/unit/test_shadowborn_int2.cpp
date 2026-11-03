// Shadowborn Apostle onboarding, integrator INT-2 (2026-11-03; docs/design/analysis-Shadowborn
// Apostle.md): Shadowborn Apostle's "{B}, Sacrifice six creatures named Shadowborn Apostle: put a Demon
// onto the battlefield" (PermAbilityMode::SacNamedTutorPut), the four Demons (Abhorrent Overlord's
// devotion tokens + searched upkeep sacrifice, Reaper from the Abyss's searched morbid destroy, Sire of
// Insanity's end-step discard, Jetmir's tiered count anthem) and the second-main hook.
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kApostle = "Shadowborn Apostle";

void EnsureCardsSb2()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card FullCardSb2(const std::string& name, int number)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Card c     = def->card;
    c.m_number = number;
    return c;
}

Permanent PermSb2(const std::string& name, int number, bool fresh = false, bool tapped = false, int ctrl = 0)
{
    Permanent p;
    p.card              = FullCardSb2(name, number);
    p.controller_index  = ctrl;
    p.owner_index       = ctrl;
    p.entered_this_turn = fresh;
    p.tapped            = tapped;
    return p;
}

// A definition-less opponent creature, the shape GoldFishRunner's scheduled spawns have.
Permanent SpawnSb2(int number, int pw, int tg)
{
    Permanent p;
    p.card.m_name = std::to_string(pw) + "/" + std::to_string(tg) + " Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = pw;
    p.card.m_toughness = tg;
    p.card.m_number    = number;
    p.controller_index = 1;
    p.owner_index      = 1;
    p.def_absent       = true;
    p.is_token         = true;
    return p;
}

// INT-4 restricted the deck's Demon list to Abhorrent Overlord then Jetmir (USER 2026-11-03). These INT-2
// tests exercise the activation's MECHANICS for every Demon and the fail-to-find, so they run on the
// deck provider with the engine's generic Demon list (no SacNamedPutTargets restriction); the USER's
// list itself is pinned in test_shadowborn_int4.cpp.
struct ShadowbornGenericDemonsSb2 : ShadowbornProvider
{
    bool SacNamedPutTargets(const GameState&, int, const CardParams&, std::vector<std::string>&) const override
    { return false; }
};
const ShadowbornProvider& ProvSb2() { static const ShadowbornGenericDemonsSb2 p; return p; }

GameState BaseSb2()
{
    EnsureCardsSb2();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.m_provider          = &ProvSb2();
    return s;
}

// Library: the four Demons + Swamp filler.
void DemonLibrary(GameState& s)
{
    int n = 300;
    for (const char* d : { "Abhorrent Overlord", "Reaper from the Abyss", "Sire of Insanity", "Jetmir, Nexus of Revels" })
    { s.players[0].library.push_back(FullCardSb2(d, n++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(FullCardSb2("Swamp", n++)); }
}

int CountOnBf(const GameState& s, const std::string& name, int ctrl = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == ctrl && p.card.m_name.str() == name) { ++n; } }
    return n;
}
int CountTokens(const GameState& s, int ctrl = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == ctrl && p.is_token) { ++n; } }
    return n;
}

std::vector<Action> Activations(const GameState& s)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut)
        { out.push_back(a); }
    }
    return out;
}

// Apply ONE action as a plan through the shared apply (the rollout's apply_one -- the viewer's path too).
void ApplyOne(GameState& s, const Action& a)
{
    TurnSolver::Plan p;
    p.actions.push_back(a);
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
}

const Action* FindAct(const std::vector<Action>& acts, const std::string& target, int code = -1)
{
    for (const Action& a : acts)
    {
        if (a.tutor_target.str() != target) { continue; }
        if (code >= 0 && a.sac_victim_id != code) { continue; }
        return &a;
    }
    return nullptr;
}
}   // namespace

TEST_CASE("Shadowborn INT-2: routing keys on the Apostle param and the second main is enabled")
{
    EnsureCardsSb2();
    const Decklist deck = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Shadowborn Apostle/Shadowborn Apostle.cod"));
    CHECK(std::string(DetectDecisionProvider(deck).Name()) == "ShadowbornApostle");
    CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
    // The hook is the Apostle's own param: a list with no Apostle (and no other second-main card) does not get it.
    Decklist plain;
    for (int k = 0; k < 20; ++k) { plain.mainboard.push_back(FullCardSb2("Swamp", k + 1)); }
    plain.mainboard.push_back(FullCardSb2("Reaper from the Abyss", 40));
    CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(plain));
}

TEST_CASE("Apostle activation: six SUMMONING-SICK Apostles pay the cost; the Demon enters with its ETB")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 8; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k, /*fresh=*/true)); }
    DemonLibrary(s);
    const std::vector<Action> acts = Activations(s);
    // Four Demons + fail-to-find; eight sick copies give exactly one readiness code (six U copies).
    CHECK(acts.size() == 5);
    const Action* ov = FindAct(acts, "Abhorrent Overlord");
    REQUIRE(ov != nullptr);
    CHECK(SacNamedSpecS(ov->sac_victim_id) == 0);
    CHECK(SacNamedSpecT(ov->sac_victim_id) == 0);
    CHECK(FindAct(acts, "Jetmir, Nexus of Revels") != nullptr);   // legal: put, not cast
    CHECK(FindAct(acts, kSacNamedNoFetch) != nullptr);
    GameState t = s;
    const std::size_t lib_before = t.players[0].library.size();
    ApplyOne(t, *ov);
    CHECK(CountOnBf(t, kApostle) == 2);
    CHECK(CountOnBf(t, "Abhorrent Overlord") == 1);
    // Devotion to black at resolution: the Overlord's {B}{B} + two Apostles left = 4 Harpies.
    CHECK(CountTokens(t) == 4);
    CHECK(t.players[0].library.size() == lib_before - 1);
    CHECK(t.players[0].graveyard.size() == 6);
    CHECK(t.creature_died_this_turn);   // the six died through the shared cascade (morbid)
    CHECK(t.battlefield[0].tapped);     // the {B} was paid
}

TEST_CASE("Apostle activation: the readiness code searches how many attack-ready copies are given up")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k)); }                  // attack-ready
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(PermSb2(kApostle, 20 + k, /*fresh=*/true)); }  // sick
    DemonLibrary(s);
    // Two strong copies must go (4 weak + 2 strong); a code giving up more strong copies while a weak one
    // is left over is dominated and not emitted.
    for (const Action& a : Activations(s)) { CHECK(SacNamedSpecS(a.sac_victim_id) == 2); }
    const Action* rp = FindAct(Activations(s), "Reaper from the Abyss");
    REQUIRE(rp != nullptr);
    GameState t = s;
    ApplyOne(t, *rp);
    int strong_left = 0;
    for (const Permanent& p : t.battlefield)
    { if (p.card.m_name.str() == kApostle && !p.entered_this_turn) { ++strong_left; } }
    CHECK(strong_left == 2);   // the two survivors are attack-ready copies
    CHECK(CountOnBf(t, "Reaper from the Abyss") == 1);
    // A same-plan cast adds sick copies: with two Apostles in hand, the code that relies on them is offered.
    GameState h = BaseSb2();
    h.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 6; ++k) { h.battlefield.push_back(PermSb2(kApostle, 10 + k)); }
    h.players[0].hand.push_back(FullCardSb2(kApostle, 50));
    h.players[0].hand.push_back(FullCardSb2(kApostle, 51));
    DemonLibrary(h);
    std::vector<int> s_vals;
    for (const Action& a : Activations(h))
    { if (a.tutor_target.str() == "Sire of Insanity") { s_vals.push_back(SacNamedSpecS(a.sac_victim_id)); } }
    std::sort(s_vals.begin(), s_vals.end());
    CHECK(s_vals == std::vector<int>{ 4, 5, 6 });
}

TEST_CASE("Apostle activation: two activations in one turn fetch two Demons; the second is a fresh activation")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Swamp", 1));
    s.battlefield.push_back(PermSb2("Swamp", 2));
    for (int k = 0; k < 12; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k, /*fresh=*/true)); }
    DemonLibrary(s);
    const Action* first = FindAct(Activations(s), "Abhorrent Overlord");
    REQUIRE(first != nullptr);
    ApplyOne(s, *first);
    CHECK(CountOnBf(s, kApostle) == 6);
    CHECK(CountTokens(s) == 8);   // Overlord FIRST: 2 + six Apostles still out
    const Action* second = FindAct(Activations(s), "Reaper from the Abyss");
    REQUIRE(second != nullptr);
    ApplyOne(s, *second);
    CHECK(CountOnBf(s, kApostle) == 0);
    CHECK(CountOnBf(s, "Abhorrent Overlord") == 1);
    CHECK(CountOnBf(s, "Reaper from the Abyss") == 1);
    // The order is a searched choice: Overlord second sees no Apostles left.
    GameState r = BaseSb2();
    r.battlefield.push_back(PermSb2("Swamp", 1));
    r.battlefield.push_back(PermSb2("Swamp", 2));
    for (int k = 0; k < 12; ++k) { r.battlefield.push_back(PermSb2(kApostle, 10 + k, true)); }
    DemonLibrary(r);
    ApplyOne(r, *FindAct(Activations(r), "Reaper from the Abyss"));
    ApplyOne(r, *FindAct(Activations(r), "Abhorrent Overlord"));
    CHECK(CountTokens(r) == 5);   // 2 (Overlord) + 3 (Reaper's {B}{B}{B})
}

TEST_CASE("Apostle activation: fail to find sacrifices six, puts nothing and still shuffles")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k, true)); }
    DemonLibrary(s);
    const Action* none = FindAct(Activations(s), kSacNamedNoFetch);
    REQUIRE(none != nullptr);
    const int sc = s.search_count;
    const std::size_t lib = s.players[0].library.size();
    ApplyOne(s, *none);
    CHECK(CountOnBf(s, kApostle) == 0);
    CHECK(s.players[0].library.size() == lib);
    CHECK(s.search_count > sc);   // the library was searched and shuffled
    // Five Apostles: not activatable.
    GameState f = BaseSb2();
    f.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 5; ++k) { f.battlefield.push_back(PermSb2(kApostle, 10 + k, true)); }
    DemonLibrary(f);
    CHECK(Activations(f).empty());
}

TEST_CASE("Abhorrent Overlord: the upkeep sacrifice takes the pinned class; unpinned takes the front")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Abhorrent Overlord", 5));
    s.battlefield.push_back(PermSb2(kApostle, 10));
    CreateTokens(s, 0, 2, 1, 1, { "Harpy" }, "B", { "Flying" });
    // Classes in branch order: the Harpy (token first), the Apostle, the Overlord.
    REQUIRE(DemonTriggerClassReps(s, 0, false, std::string{}).size() == 3);
    GameState a = s;
    PerformUpkeepSacrificeCreature(a);   // unpinned: the front (a Harpy)
    CHECK(CountTokens(a) == 1);
    CHECK(CountOnBf(a, kApostle) == 1);
    CHECK(a.scripted_demon_trigger == -1);
    // INT-5 THE OVERLORD VICTIM FOLD (SpellEffects.h UpkeepSacFoldReps): the Harpy class drops the Apostle
    // class it dominates, so the ranks index [Harpy, Overlord]; with no Harpy the Apostle is rank 0.
    GameState b = s;
    b.scripted_demon_trigger = static_cast<std::int16_t>(1 << kDemonOverlordShift);   // rank 1: itself
    PerformUpkeepSacrificeCreature(b);
    CHECK(CountTokens(b) == 2);
    CHECK(CountOnBf(b, kApostle) == 1);
    CHECK(CountOnBf(b, "Abhorrent Overlord") == 0);
    CHECK(b.creature_died_this_turn);
    CHECK(b.scripted_demon_trigger == -1);   // consumed
    GameState c = s;
    c.battlefield.erase(std::remove_if(c.battlefield.begin(), c.battlefield.end(),
                                       [](const Permanent& q) { return q.is_token; }), c.battlefield.end());
    c.scripted_demon_trigger = -1;   // no Harpy: the front is the Apostle
    PerformUpkeepSacrificeCreature(c);
    CHECK(CountOnBf(c, kApostle) == 0);
    CHECK(CountOnBf(c, "Abhorrent Overlord") == 1);
}

TEST_CASE("Reaper from the Abyss: morbid destroys OUR non-Demon creature when the opponent has none")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Reaper from the Abyss", 5));
    s.battlefield.push_back(PermSb2(kApostle, 10));
    s.battlefield.push_back(PermSb2("Sire of Insanity", 6));
    CHECK(SnapshotEndStepMorbidTriggers(s) == 0);   // nothing died: no trigger
    s.creature_died_this_turn = true;
    REQUIRE(SnapshotEndStepMorbidTriggers(s) == 1);
    // Only the Apostle is a legal target (both Demons are excluded): it is forced.
    PerformEndStepMorbidDestroy(s, 1);
    CHECK(CountOnBf(s, kApostle) == 0);
    CHECK(CountOnBf(s, "Reaper from the Abyss") == 1);
    CHECK(CountOnBf(s, "Sire of Insanity") == 1);
    // With an opponent spawn out, rank 0 hits the spawn and rank 1 our Apostle.
    GameState o = BaseSb2();
    o.battlefield.push_back(PermSb2("Reaper from the Abyss", 5));
    o.battlefield.push_back(PermSb2(kApostle, 10));
    o.battlefield.push_back(SpawnSb2(500000, 2, 2));
    o.creature_died_this_turn = true;
    GameState o0 = o;
    PerformEndStepMorbidDestroy(o0, SnapshotEndStepMorbidTriggers(o0));
    CHECK(CountOnBf(o0, kApostle) == 1);
    CHECK(o0.battlefield.size() == 2);
    GameState o1 = o;
    o1.scripted_demon_trigger = 1;
    PerformEndStepMorbidDestroy(o1, SnapshotEndStepMorbidTriggers(o1));
    CHECK(CountOnBf(o1, kApostle) == 0);
    CHECK(o1.battlefield.size() == 2);
    CHECK(o1.scripted_demon_trigger == -1);
    // The end step consumes only the low byte: an Overlord pin survives to the next upkeep.
    GameState o2 = o;
    o2.scripted_demon_trigger = static_cast<std::int16_t>(1 | (2 << kDemonOverlordShift));
    PerformEndStepMorbidDestroy(o2, 1);
    CHECK(o2.scripted_demon_trigger == (2 << kDemonOverlordShift));
}

TEST_CASE("Sire of Insanity: our end step discards the hand; staged cards stay")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Sire of Insanity", 5));
    s.players[0].hand.push_back(FullCardSb2("Swamp", 40));
    s.players[0].hand.push_back(FullCardSb2(kApostle, 41));
    Card staged = FullCardSb2("Dark Ritual", 42);
    staged.m_is_staged = true;
    s.players[0].hand.push_back(staged);
    PerformEndStepDiscardHands(s, 0);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand[0].m_number == 42);
    CHECK(s.players[0].graveyard.size() == 2);
    // No Sire: nothing happens.
    GameState n = BaseSb2();
    n.players[0].hand.push_back(FullCardSb2("Swamp", 40));
    PerformEndStepDiscardHands(n, 0);
    CHECK(n.players[0].hand.size() == 1);
}

TEST_CASE("Jetmir: +1/+2/+3 power at 3/6/9 creatures; vigilance at 3; double strike at 9 doubles combat damage")
{
    auto board = [](int bodies)
    {
        GameState s = BaseSb2();
        s.battlefield.push_back(PermSb2("Jetmir, Nexus of Revels", 5));
        for (int k = 0; k < bodies - 1; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k)); }
        return s;
    };
    const Card& apostle = FullCardSb2(kApostle, 99);
    CHECK(ComputeLordBonus(apostle, board(2), 0).first == 0);
    CHECK(ComputeLordBonus(apostle, board(3), 0).first == 1);
    CHECK(ComputeLordBonus(apostle, board(6), 0).first == 2);
    CHECK(ComputeLordBonus(apostle, board(9), 0).first == 3);
    {
        GameState s = board(3);
        CHECK(CreatureHasVigilance(s.battlefield[1], s));
        CHECK_FALSE(CreatureHasDoubleStrike(s.battlefield[1], s));
        const BoardSources bs = GatherBoardSources(s.battlefield, 0);
        CHECK(bs.own_creatures == 3);
        CHECK(ComputeLordBonus(apostle, s, 0, false, nullptr, &bs.lords, &bs.anthems, bs.own_creatures).first == 1);
        CHECK(CreatureHasVigilance(s.battlefield[1], s, &bs.vigilance));
    }
    {
        GameState s = board(9);
        CHECK(CreatureHasDoubleStrike(s.battlefield[1], s));
        const BoardSources bs = GatherBoardSources(s.battlefield, 0);
        CHECK(CreatureHasDoubleStrike(s.battlefield[1], s, &bs.ds));
        // One Apostle swinging: (1 + 3) x 2 = 8. Below nine creatures the same body deals 1 + 2 = 3.
        CHECK(ResolveCombatDamage(s, { 1 }, 0, false).total_damage == 8);
        GameState e = board(8);
        CHECK(ResolveCombatDamage(e, { 1 }, 0, false).total_damage == 3);
    }
    // A deck without the card (stamped false) never runs the pass.
    GameState off = board(9);
    off.deck_has_count_anthem = false;
    CHECK(ComputeLordBonus(apostle, off, 0).first == 0);
}

TEST_CASE("Demon-trigger axis: a plan that puts Reaper with six dying gets searched target variants")
{
    GameState s = BaseSb2();
    s.battlefield.push_back(PermSb2("Swamp", 1));
    for (int k = 0; k < 7; ++k) { s.battlefield.push_back(PermSb2(kApostle, 10 + k, true)); }
    s.battlefield.push_back(SpawnSb2(500000, 1, 1));
    DemonLibrary(s);
    int reaper_plans = 0, reaper_variants = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool puts_reaper = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut
                && a.tutor_target.str() == "Reaper from the Abyss") { puts_reaper = true; }
        }
        if (!puts_reaper) { continue; }
        if (p.demon_trigger_choice < 0) { ++reaper_plans; } else { ++reaper_variants; }
    }
    CHECK(reaper_plans >= 1);
    CHECK(reaper_variants >= 1);   // rank 1 = our Apostle instead of the spawn
}
