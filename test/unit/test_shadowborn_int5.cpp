// Shadowborn Apostle onboarding, integrator INT-5 (2026-11-03; docs/design/analysis-Shadowborn Apostle.md):
// the Stage 2d review's findings (logs/shadowborn/review-2d.md) and the USER's amended tutor table.
//   1. a ripple FREE tutor keeps the hard tutor's searched pick (save / restore around it);
//   2. the executor's ripple trigger knows its own spell (card number), so a ripple_mask != 0 chain plays the
//      same in the executor as in the rollout;
//   3. the Overlord victim fold (a vanilla token class drops every clean keeper class it is dominated by);
//   4. the free tutor / victim pins reach every packed ordinal in the deep wave;
//   5. "reveal every trigger" (kRippleRevealAll) tops the per-name budget menu;
//   6. an ordinal past the emitted mask / decline menu is answered by policy and COUNTED, never read as a pin;
//   9. ShadowbornReachableMana reads suspend arrivals on TurnSeq();
//   the tutor table (USER 2026-11-03): two orders keyed on a granter on our battlefield, Lotus Bloom never,
//   the Culling-vs-Ritual order with the expendable-target reservations.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kAp5      = "Shadowborn Apostle";
const char* const kStone5   = "Thrumming Stone";
const char* const kIntent5  = "Diabolic Intent";
const char* const kOver5    = "Abhorrent Overlord";
const char* const kJet5     = "Jetmir, Nexus of Revels";
const char* const kReaper5  = "Reaper from the Abyss";
const char* const kSire5    = "Sire of Insanity";
const char* const kLake5    = "Lake of the Dead";
const char* const kCull5    = "Culling the Weak";
const char* const kRit5     = "Dark Ritual";
const char* const kBloom5   = "Lotus Bloom";

void Ensure5()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& Def5(const std::string& n)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card Card5(const std::string& name, int number)
{
    Card c     = Def5(name).card;
    c.m_number = number;
    return c;
}
Permanent Perm5(const std::string& name, int number, bool fresh = false, bool tapped = false)
{
    Permanent p;
    p.card              = Card5(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    p.tapped            = tapped;
    return p;
}
const ShadowbornProvider& Prov5() { static const ShadowbornProvider p; return p; }
GameState Base5(int swamps)
{
    Ensure5();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.m_provider          = &Prov5();
    for (int k = 0; k < swamps; ++k) { s.battlefield.push_back(Perm5("Swamp", 1 + k)); }
    return s;
}
void Lib5(GameState& s, const std::vector<std::string>& names, int first = 500)
{
    for (const std::string& n : names) { s.players[0].library.push_back(Card5(n, first++)); }
}
void Fill5(GameState& s, const std::string& name, int k, int first = 800)
{
    for (int i = 0; i < k; ++i) { s.players[0].library.push_back(Card5(name, first + i)); }
}
int CountBf5(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}
int CountTokens5(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && p.controller_index == 0) { ++n; } }
    return n;
}
std::vector<std::string> HandNames(const GameState& s)
{
    std::vector<std::string> o;
    for (const Card& c : s.players[0].hand) { o.push_back(c.m_name.str()); }
    std::sort(o.begin(), o.end());
    return o;
}
std::vector<int> Nums5(const std::vector<Card>& v) { std::vector<int> o; for (const Card& c : v) { o.push_back(c.m_number); } return o; }
std::vector<int> LibNums5(const GameState& s) { std::vector<int> o; for (const Card& c : s.players[0].library) { o.push_back(c.m_number); } return o; }
std::vector<int> Sorted5(std::vector<int> v) { std::sort(v.begin(), v.end()); return v; }
std::vector<int> NonLandBf5(const GameState& s)
{
    std::vector<int> o;
    for (const Permanent& p : s.battlefield) { if (!p.card.IsLand()) { o.push_back(p.card.m_number); } }
    std::sort(o.begin(), o.end());
    return o;
}

struct Pins5
{
    std::int8_t   reveals  = -1;
    std::int8_t   take_cap = -1;
    std::uint8_t  mask     = 0;
    std::uint8_t  decline  = 0;
    std::uint16_t ft       = 0;
    std::uint16_t fv       = 0;
};
Action CastAct5(const GameState& s, const std::string& name, const Pins5& p, const std::string& tutor, int victim)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &Def5(name);
    a.cost      = EffectiveSpellCost(Def5(name), s, 1);
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[i].m_name == name) { a.hand_index = i; break; } }
    a.tutor_target          = tutor;
    a.soulfire_own_targets  = victim;
    a.ripple_pinned         = true;
    a.ripple_reveals        = p.reveals;
    a.ripple_take_cap       = p.take_cap;
    a.ripple_mask           = p.mask;
    a.ripple_decline        = p.decline;
    a.ripple_free_tutor     = p.ft;
    a.ripple_free_victim    = p.fv;
    return a;
}
// THE ROLLOUT: one cast through the shared plan apply, with the plan's searched tutor index.
void RolloutCast5(GameState& s, const Action& a, int tutor_choice = -1)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    p.tutor_choice = tutor_choice;
    p.actions.push_back(a);
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
}
// THE EXECUTOR: the hard cast as AIEngine::CastSpellFromHand stacks it (the cast copy's NUMBER passed to
// PushCastTriggers, as there), then the stack resolved LIFO -- inside the plan's ScriptedTutor scope.
void ExecutorCast5(GameState& s, const std::string& name, const Pins5& p, const std::string& tutor, int victim,
                   int tutor_choice = -1)
{
    ScriptedTutor st(tutor_choice);
    const CardDefinition& d = Def5(name);
    auto& hand = s.players[0].hand;
    auto it = std::find_if(hand.begin(), hand.end(), [&](const Card& c) { return c.m_name == name; });
    REQUIRE(it != hand.end());
    const Card c = *it;
    hand.erase(it);
    if (SacCreatureCostFoldsByReadiness(d.params))
    { PerformSacrificeCreatureCost(s, name, d.params.sac_additional_creature_color, victim, true); }
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = d.card;
    e.source.m_number  = c.m_number;
    e.controller_index = 0;
    e.tutor_target     = tutor;
    ++s.spells_cast_this_turn;
    s.stack.push_back(e);
    StampRippleScript(s, d, true, p.reveals, p.take_cap, p.mask, p.decline, p.ft, p.fv);
    EffectHandler::PushCastTriggers(s, d, 0, c.m_number);
    while (!s.stack.empty())
    {
        StackEntry top = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* td = CardDatabase::Instance().LookupCached(top.source);
        REQUIRE(td != nullptr);
        EffectHandler::Resolve(s, top, *td);
    }
}
void CheckLockstep5(const GameState& ex, const GameState& ro)
{
    CHECK(NonLandBf5(ex) == NonLandBf5(ro));
    CHECK(LibNums5(ex) == LibNums5(ro));
    CHECK(Sorted5(Nums5(ex.players[0].hand)) == Sorted5(Nums5(ro.players[0].hand)));
    CHECK(Sorted5(Nums5(ex.players[0].graveyard)) == Sorted5(Nums5(ro.players[0].graveyard)));
    CHECK(ex.spells_cast_this_turn == ro.spells_cast_this_turn);
}
std::vector<std::string> Tut5(const GameState& s, const std::string& tutor = "Demonic Tutor")
{
    return RankedTutorCandidates(Prov5(), s, 0, Def5(tutor).params);
}
using VS5 = std::vector<std::string>;
}   // namespace

// ---- 1. the free tutor keeps the hard tutor's pick ---------------------------------------------------------
TEST_CASE("INT-5 ripple: a free Diabolic Intent resolves on its own pick; the hard Intent keeps the plan's index")
{
    // Stone out, two Swamps (short of 3: the mana tier is live), no Apostle in hand. The hard Intent's ripple
    // reveals a second Intent (cast free); the free one resolves FIRST (it is on top).
    //   free Intent's list:  [Apostle, Lake, Dark Ritual]      (an Apostle first: none in hand)
    //   hard Intent's list:  [Lake, Dark Ritual, Apostle]      (an Apostle now in hand -> "second Apostle")
    GameState s = Base5(2);
    s.battlefield.push_back(Perm5(kStone5, 90));
    s.battlefield.push_back(Perm5(kAp5, 60));
    s.battlefield.push_back(Perm5(kAp5, 61));
    s.players[0].hand.push_back(Card5(kIntent5, 100));
    Lib5(s, { kIntent5, kSire5, kSire5, kSire5, kAp5, kLake5, kRit5 });
    Fill5(s, kSire5, 10);
    REQUIRE(Tut5(s, kIntent5) == VS5{ kAp5, kLake5, kRit5 });
    for (const std::uint16_t ft : { std::uint16_t{1}, std::uint16_t{0} })   // pinned rank 0, and unpinned (the front)
    {
        Pins5 p; p.ft = ft; p.fv = 1;
        GameState ro = s, ex = s;
        RolloutCast5(ro, CastAct5(ro, kIntent5, p, "", 60), /*tutor_choice=*/1);
        ExecutorCast5(ex, kIntent5, p, "", 60, /*tutor_choice=*/1);
        // The free Intent: the front (Apostle). The hard one: index 1 of ITS list = Dark Ritual. Before INT-5 the
        // free tutor wiped the global (pinned: the hard one took the front, Lake; unpinned: the free one ate index 1).
        CHECK(HandNames(ro) == VS5{ kRit5, kAp5 });
        CHECK(HandNames(ex) == VS5{ kRit5, kAp5 });
        CHECK(ro.spells_cast_this_turn == 2);
        CheckLockstep5(ex, ro);
    }
}

// ---- 2. executor vs rollout with ripple_mask != 0 --------------------------------------------------------------
TEST_CASE("INT-5 ripple: executor and rollout agree with ripple_mask != 0 (the trigger knows its own spell)")
{
    // DI1 (hand) reveals DI_b, DI_c (both cast). Stack [DI1, DI_b, DI_c, R_b, R_c]: R_c and R_b are not directly
    // above their own spells, so each reads a mask ordinal (R_c = 0, R_b = 1); DI1's own trigger IS (F2: it
    // always reveals, consuming none). Mask 0b01: R_c does not reveal, R_b does (and finds DI_d).
    GameState s = Base5(4);
    s.battlefield.push_back(Perm5(kStone5, 90));
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(Perm5(kAp5, 60 + k)); }
    s.players[0].hand.push_back(Card5(kIntent5, 100));
    Lib5(s, { kIntent5, kIntent5, kSire5, kSire5, kIntent5, kSire5, kSire5, kSire5 });
    Fill5(s, kSire5, 20);
    for (const std::uint8_t mask : { std::uint8_t{0x01}, std::uint8_t{0x02}, std::uint8_t{0x03} })
    {
        CAPTURE(static_cast<int>(mask));
        Pins5 p; p.mask = mask; p.ft = 0x111; p.fv = 0x111;
        GameState ro = s, ex = s;
        RolloutCast5(ro, CastAct5(ro, kIntent5, p, "", 60));
        ExecutorCast5(ex, kIntent5, p, "", 60);
        CHECK(ro.spells_cast_this_turn >= 3);   // DI1's own trigger revealed in BOTH worlds (F2)
        CheckLockstep5(ex, ro);
    }
}

// ---- 3. the Overlord victim fold -------------------------------------------------------------------------------
TEST_CASE("INT-5 Overlord fold: a Harpy class drops the Apostle class; the Overlord, Demons and drawbacks stay")
{
    GameState s = Base5(2);
    s.battlefield.push_back(Perm5(kOver5, 5));
    s.battlefield.push_back(Perm5(kAp5, 10));
    CreateTokens(s, 0, 2, 1, 1, { "Harpy" }, "B", { "Flying" });
    for (Permanent& p : s.battlefield) { p.entered_this_turn = false; }   // our upkeep: nothing is sick
    Lib5(s, { kReaper5, "Swamp", "Swamp" });   // an audited pool (Reaper: the Demon exclusion)
    REQUIRE(DemonTriggerClassReps(s, 0, false, std::string{}).size() == 3);   // Harpy, Apostle, Overlord
    const std::vector<int> folded = UpkeepSacFoldReps(s, 0);
    REQUIRE(folded.size() == 2);                                            // Harpy, Overlord
    CHECK(s.battlefield[static_cast<std::size_t>(folded[0])].is_token);
    CHECK(s.battlefield[static_cast<std::size_t>(folded[1])].card.m_name.str() == kOver5);
    for (int rank = 0; rank < 4; ++rank)
    {
        GameState g = s;
        g.scripted_demon_trigger = static_cast<std::int16_t>(rank << kDemonOverlordShift);
        PerformUpkeepSacrificeCreature(g);
        CHECK(CountBf5(g, kAp5) == 1);   // never the Apostle while a Harpy is there
        CHECK((rank == 0 ? CountTokens5(g) == 1 : CountBf5(g, kOver5) == 0));
    }
    // A Demon keeper (Jetmir) with Reaper in the pool is NOT folded (it changes Reaper's forced target);
    // without any Reaper in the pool, Jetmir is a clean keeper and folds like the Apostle.
    GameState j = s;
    j.battlefield.push_back(Perm5(kJet5, 7));
    CHECK(UpkeepSacFoldReps(j, 0).size() == 3);   // Harpy, Jetmir, Overlord
    GameState jn = j;
    jn.players[0].library = Library{};
    Lib5(jn, { "Swamp", "Swamp" });
    CHECK(UpkeepSacFoldReps(jn, 0).size() == 2);  // Harpy, Overlord
    // Drawback creatures stay: Sire (discards our hand) is not a keeper.
    GameState si = s;
    si.battlefield.push_back(Perm5(kSire5, 8));
    CHECK(UpkeepSacFoldReps(si, 0).size() == 3);  // Harpy, Overlord, Sire
    // No Harpy: nothing folds (the Apostle is the front).
    GameState nh = s;
    nh.battlefield.erase(std::remove_if(nh.battlefield.begin(), nh.battlefield.end(),
                                        [](const Permanent& q) { return q.is_token; }), nh.battlefield.end());
    CHECK(UpkeepSacFoldReps(nh, 0).size() == 2);  // Apostle, Overlord
    // An UNAUDITED card anywhere in our zones disables the fold.
    GameState ua = s;
    Lib5(ua, { "Lightning Bolt" }, 700);   // direct_damage: not an audited template
    CHECK(UpkeepSacFoldReps(ua, 0).size() == 3);
    // A tapped Apostle at upkeep (impossible after untap; the live premise) is not "as ready" as the Harpy.
    GameState tp = s;
    for (Permanent& p : tp.battlefield) { if (p.card.m_name.str() == kAp5) { p.tapped = true; } }
    CHECK(UpkeepSacFoldReps(tp, 0).size() == 3);
}

TEST_CASE("INT-5 Overlord fold: the enumerator's victim rank bound shrinks to D + max(H, K)")
{
    // Seven Apostles, B open, Overlord in the library: the activation plan puts the Overlord (2+ Harpies).
    // Classes at the upkeep: Harpy (H), Apostle (K), Overlord (D) -> the fold leaves 2 -> ranks 0..1.
    GameState s = Base5(2);
    for (int k = 0; k < 7; ++k) { s.battlefield.push_back(Perm5(kAp5, 10 + k, /*fresh=*/true)); }
    Lib5(s, { kOver5, kReaper5, "Swamp", "Swamp", "Swamp" });
    int max_rank = -1, variants = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool puts_over = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut
                && a.tutor_target.str() == kOver5) { puts_over = true; }
        }
        if (!puts_over || p.demon_trigger_choice < 0) { continue; }
        ++variants;
        max_rank = std::max(max_rank, (p.demon_trigger_choice >> kDemonOverlordShift) & kDemonPinMask);
    }
    CHECK(variants >= 1);
    CHECK(max_rank == 1);   // INT-4: 2 (Harpy, Apostle, Overlord)
}

// ---- 4 / 5 / 6. the ripple pin menus ---------------------------------------------------------------------------
TEST_CASE("INT-5 ripple pins: ALL tops the Apostle budget; the late wave adds the finite budgets past the dry run")
{
    GameState s = Base5(2);
    s.battlefield.push_back(Perm5(kStone5, 90));
    s.players[0].hand.push_back(Card5(kAp5, 100));
    s.players[0].hand.push_back(Card5(kAp5, 101));
    Lib5(s, { kAp5, "Swamp", "Swamp", "Swamp" });
    Fill5(s, "Swamp", 8);
    Lib5(s, { kAp5, kAp5 }, 900);   // two copies the dry run cannot reach (below the reveals)
    auto budgets = [](const std::vector<Action>& acts)
    {
        std::vector<int> b;
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kAp5 || a.ripple_reveals == kRippleContinue) { continue; }
            b.push_back(a.ripple_reveals);
        }
        return b;
    };
    // Below the late wave: ALL, then the dry run's T-1 .. 0 (T = 3).
    CHECK(budgets(TurnSolver::CollectActionsForTest(s, true)) == std::vector<int>{ kRippleRevealAll, 2, 1, 0 });
    // Unlimited (the late wave): Tmax = 2 hand + 3 library copies = 5 -> ALL, 4 .. 0.
    const std::vector<Action> unl = TurnSolver::CollectActionsUnderRootForTest(s, true, nullptr);
    CHECK(budgets(unl) == std::vector<int>{ kRippleRevealAll, 4, 3, 2, 1, 0 });
    // A revealed Apostle is ALWAYS cast (USER 2026-11-03: "We should heuristically prune the decline of
    // Apostles. I can't see an advantage."): no take cap at any budget, unlimited included.
    for (const Action& a : unl)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kAp5) { CHECK(a.ripple_take_cap == -1); } }
    // ALL keeps revealing past any count: the stamped script reveals every trigger.
    GameState g = s;
    StampRippleScript(g, Def5(kAp5), true, kRippleRevealAll, -1, 0, 0, 0, 0);
    for (int k = 0; k < 9; ++k) { CHECK(RippleRevealDecision(g, 0, Def5(kAp5), 4, true)); }
}

TEST_CASE("INT-5 ripple pins: free tutor / victim ordinals reach 4 in the deep wave, 2 below it")
{
    GameState s = Base5(4);
    s.battlefield.push_back(Perm5(kStone5, 90));
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(Perm5(kAp5, 60 + k)); }
    s.players[0].hand.push_back(Card5(kIntent5, 100));
    Lib5(s, { kSire5, kSire5, kSire5, kSire5, kIntent5, kIntent5, kIntent5, kAp5, kRit5 });
    auto max_ord = [](const std::vector<Action>& acts)
    {
        int m = 0;
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kIntent5) { continue; }
            for (int o = 0; o < kRipplePinOrdinals; ++o)
            {
                if (((a.ripple_free_tutor >> (o * kRipplePinBits)) & 0xF) != 0) { m = std::max(m, o + 1); }
                if (((a.ripple_free_victim >> (o * kRipplePinBits)) & 0xF) != 0) { m = std::max(m, o + 1); }
            }
        }
        return m;
    };
    SearchBudget late(200LL * SearchBudget::NODES_PER_VIRTUAL_MS);   // late wave (>= 100), below the deep (1000)
    CHECK(max_ord(TurnSolver::CollectActionsUnderRootForTest(s, true, &late)) == 2);
    CHECK(max_ord(TurnSolver::CollectActionsUnderRootForTest(s, true, nullptr)) == 3);   // 3 library copies
}

TEST_CASE("INT-5 ripple: an ordinal past the emitted menu is answered by policy, not read as a pin")
{
    CHECK(RippleMenuCovers(0x00, 7));                        // hand-built: every bit a pin
    CHECK_FALSE(RippleMenuCovers(0x00, 8));
    CHECK(RippleMenuCovers(RippleMenuByte(0, 2), 1));
    CHECK_FALSE(RippleMenuCovers(RippleMenuByte(0, 2), 2));  // ordinal 2 was never fanned
    CHECK_FALSE(RippleMenuCovers(RippleMenuByte(0, 0), 0));
    // A Dark Ritual chain whose menu fanned NO decline bit, revealing a match anyway (the plan moved the
    // library): the take is the policy (take), and the script does NOT consume it as "bit 0 = take".
    GameState s = Base5(2);
    s.battlefield.push_back(Perm5(kStone5, 90));
    s.players[0].hand.push_back(Card5(kRit5, 100));
    Lib5(s, { kRit5, "Swamp", "Swamp", "Swamp" });
    Fill5(s, "Swamp", 6);
    GameState g = s;
    StampRippleScript(g, Def5(kRit5), true, 1, -1, 0, RippleMenuByte(0, 0), 0, 0);
    CHECK(RippleTakeDecision(g, 0, Def5(kRit5), Def5(kRit5)));   // policy: take
    GameState h = s;
    StampRippleScript(h, Def5(kRit5), true, 1, -1, 0, RippleMenuByte(1, 1), 0, 0);
    CHECK_FALSE(RippleTakeDecision(h, 0, Def5(kRit5), Def5(kRit5)));   // fanned: bit 0 = decline
}

// ---- 9. suspend arrivals on TurnSeq -----------------------------------------------------------------------------
TEST_CASE("INT-5: ShadowbornReachableMana reads suspend arrivals on TurnSeq(), not turn_number")
{
    GameState s = Base5(1);
    s.extra_turns_taken = 2;   // TurnSeq() = turn_number + 2
    SuspendedCard sc; sc.card = Card5(kBloom5, 70); sc.arrive_turn = s.TurnSeq() + 1;
    s.players[0].suspended_cards.push_back(sc);
    CHECK(ShadowbornProvider::ShadowbornReachableMana(s, 0) == 1 + 3);   // arriving next upkeep: counted
    s.players[0].suspended_cards[0].arrive_turn = s.TurnSeq() + 2;
    CHECK(ShadowbornProvider::ShadowbornReachableMana(s, 0) == 1);
}

// ---- the USER's tutor table (2026-11-03) --------------------------------------------------------------------------
TEST_CASE("INT-5 tutor table: no granter on the battlefield -- Stone > mana < 6 > Apostle > extra mana; no Bloom")
{
    GameState s = Base5(2);
    Lib5(s, { kStone5, kLake5, kCull5, kRit5, kBloom5, kAp5, kSire5, "Swamp" });
    CHECK(Tut5(s) == VS5{ kStone5, kLake5, kRit5, kAp5 });              // short of 6: mana before the Apostle
    GameState held = s;
    held.players[0].hand.push_back(Card5(kStone5, 40));                  // a Stone in HAND: no Stone, same order
    CHECK(Tut5(held) == VS5{ kLake5, kRit5, kAp5 });
    GameState rich = s;                                                  // 6 reachable: the Apostle, then extra mana
    for (int k = 0; k < 4; ++k) { rich.battlefield.push_back(Perm5("Swamp", 20 + k)); }
    CHECK(ShadowbornProvider::ShadowbornReachableMana(rich, 0) == 6);
    CHECK(Tut5(rich) == VS5{ kStone5, kAp5, kLake5, kRit5 });
    GameState ap = rich;                                                 // an Apostle in hand: one tier anyway
    ap.players[0].hand.push_back(Card5(kAp5, 41));
    CHECK(Tut5(ap) == VS5{ kStone5, kAp5, kLake5, kRit5 });
}

TEST_CASE("INT-5 tutor table: a granter on the battlefield -- Apostle if none > mana < 3 > second Apostle > extra")
{
    GameState s = Base5(2);
    s.battlefield.push_back(Perm5(kStone5, 90));
    Lib5(s, { kStone5, kLake5, kRit5, kBloom5, kAp5, kSire5 });
    CHECK(ShadowbornProvider::ShadowbornReachableMana(s, 0) == 2);
    CHECK(Tut5(s) == VS5{ kAp5, kLake5, kRit5 });                         // no Apostle in hand: it first
    GameState ap = s;
    ap.players[0].hand.push_back(Card5(kAp5, 41));
    CHECK(Tut5(ap) == VS5{ kLake5, kRit5, kAp5 });                        // short of 3: mana, then the second
    GameState three = ap;
    three.battlefield.push_back(Perm5("Swamp", 30));
    CHECK(Tut5(three) == VS5{ kAp5, kLake5, kRit5 });                     // 3 reachable: the 4th mana is "extra"
    GameState none = three;
    none.players[0].hand.clear();
    CHECK(Tut5(none) == VS5{ kAp5, kLake5, kRit5 });
}

TEST_CASE("INT-5 tutor table: Culling ahead of Dark Ritual only with an expendable target AND a turn earlier")
{
    // No granter out: bar 6. Five untapped Swamps (Lake excluded: one in hand) + seven Apostles out (spare).
    // This turn: 5 -> Ritual (+2) = 7, Culling (+3) = 8: both reach 6 THIS turn -> Ritual first.
    GameState s = Base5(5);
    for (int k = 0; k < 7; ++k) { s.battlefield.push_back(Perm5(kAp5, 10 + k)); }
    s.players[0].hand.push_back(Card5(kLake5, 45));
    Lib5(s, { kStone5, kCull5, kRit5, kAp5 });
    CHECK(Tut5(s) == VS5{ kStone5, kRit5, kCull5, kAp5 });   // short of 6 (5): both reach 6 this turn -> Ritual ahead
    // Three untapped Swamps: Ritual -> 5 this turn (short), Culling -> 6 this turn: Culling a turn earlier.
    GameState e = Base5(3);
    for (int k = 0; k < 7; ++k) { e.battlefield.push_back(Perm5(kAp5, 10 + k)); }
    e.players[0].lands_played_this_turn = 1;   // no drop left this turn; next turn: 3 + 1 drop (none in hand) = 3
    Lib5(e, { kStone5, kCull5, kRit5, kAp5 });
    CHECK(ShadowbornProvider::ShadowbornReachableMana(e, 0, /*this_turn_only=*/true) == 3);
    CHECK(Tut5(e) == VS5{ kStone5, kCull5, kRit5, kAp5 });
    // The reservations: the Stone is missing and an Intent is in hand -> one creature is kept for it.
    // Seven Apostles out still leave a target -> Culling stays ahead.
    GameState i = e;
    i.players[0].hand.push_back(Card5(kIntent5, 46));
    CHECK(Tut5(i) == VS5{ kStone5, kCull5, kRit5, kAp5 });
    // ...but with no creature out (the spare Apostles in HAND) and the Stone in hand: one Apostle in hand is
    // reserved for the Stone, the castable rest (6) remain -> still a target. With a single Apostle in hand it
    // is the reserved one -> nothing expendable -> Ritual ahead (Culling still offered after it).
    GameState h = Base5(3);
    h.players[0].lands_played_this_turn = 1;
    h.players[0].hand.push_back(Card5(kStone5, 47));
    for (int k = 0; k < 7; ++k) { h.players[0].hand.push_back(Card5(kAp5, 50 + k)); }
    Lib5(h, { kCull5, kRit5, kAp5 });
    CHECK(Tut5(h) == VS5{ kCull5, kRit5, kAp5 });
    GameState h1 = h;
    h1.players[0].hand.erase(h1.players[0].hand.begin() + 2, h1.players[0].hand.end());   // Stone + ONE Apostle
    h1.battlefield.push_back(Perm5(kAp5, 70));
    for (int k = 0; k < 6; ++k) { h1.battlefield.push_back(Perm5(kAp5, 71 + k)); }   // spare (8 > 6) but all out
    CHECK(Tut5(h1) == VS5{ kCull5, kRit5, kAp5 });   // a battlefield Apostle is expendable: the hand one is reserved
    GameState h2 = Base5(3);
    h2.players[0].lands_played_this_turn = 1;
    h2.players[0].hand.push_back(Card5(kStone5, 47));
    for (int k = 0; k < 7; ++k) { h2.players[0].hand.push_back(Card5(kAp5, 50 + k)); }
    Lib5(h2, { kCull5, kRit5, kAp5 });
    for (Permanent& p : h2.battlefield) { p.tapped = true; }   // no mana this turn: no hand creature castable
    CHECK(Tut5(h2) == VS5{ kRit5, kCull5, kAp5 });
}

TEST_CASE("INT-5 tutor fallback: exactly one -- a basic land, then a Demon the activation does not fetch; else no cast")
{
    // Stone and an Apostle out, six Swamps (not short), no Apostle / mana card left: every tier is empty.
    GameState s = Base5(6);
    s.battlefield.push_back(Perm5(kStone5, 90));
    s.battlefield.push_back(Perm5(kAp5, 60));
    GameState a = s;
    Lib5(a, { kSire5, kReaper5, "Blood Crypt", "Swamp", kBloom5, kJet5 });
    CHECK(Tut5(a) == VS5{ "Swamp" });
    GameState b = s;
    Lib5(b, { kSire5, kReaper5, "Blood Crypt", kBloom5, kJet5 });
    CHECK(Tut5(b) == VS5{ kReaper5 });   // Reaper before Sire (Sire discards our hand); never Jetmir / Overlord
    GameState c = s;
    Lib5(c, { kSire5, "Blood Crypt", kBloom5, kOver5 });
    CHECK(Tut5(c) == VS5{ kSire5 });
    // Nothing useful: an EMPTY list, and the tutor is NOT CAST -- no hand cast is emitted, and a revealed
    // copy is not castable off a ripple (it goes to the bottom).
    GameState d = s;
    d.players[0].hand.push_back(Card5("Demonic Tutor", 100));
    d.players[0].hand.push_back(Card5(kIntent5, 101));
    Lib5(d, { "Blood Crypt", kBloom5, kJet5, kOver5 });
    CHECK(Tut5(d).empty());
    for (const Action& act : TurnSolver::CollectActionsForTest(d, true))
    {
        CHECK_FALSE((act.kind == Action::Kind::CastFromHand && act.card_name.str() == "Demonic Tutor"));
        CHECK_FALSE((act.kind == Action::Kind::CastFromHand && act.card_name.str() == kIntent5));
    }
    CHECK_FALSE(RippleMatchCastable(d, 0, Def5(kIntent5), 1, -1));
    GameState e = d;
    Lib5(e, { "Swamp" }, 700);
    CHECK(RippleMatchCastable(e, 0, Def5(kIntent5), 1, -1));
    bool cast_seen = false;
    for (const Action& act : TurnSolver::CollectActionsForTest(e, true))
    { if (act.kind == Action::Kind::CastFromHand && act.card_name.str() == "Demonic Tutor") { cast_seen = true; } }
    CHECK(cast_seen);
}
