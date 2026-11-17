// Sea Creatures I4 (2026-11-17): Hullbreaker Horror + Brinelin, the Moon Kraken (logs/sea-research/bounce.md).
//   * ONE optional targeted bounce body (FireBounceTriggerInstance -> ReturnPermanentToOwnersHand + one orphaned-Aura
//     sweep), fired from FireOnCastTriggers (Hullbreaker every spell; Brinelin MV >= 6) and from Brinelin's ETB
//     (recorded by the enter cascade, drained AFTER the legend rule in both worlds).
//   * Searched: the per-instance rank digit shared with Raggadragga (ordinal renamed cast_target_trigger_ordinal);
//     options = DECLINE first + one per identity class of OUR nonland permanents (the opponent's side folded away).
//   * SeaCreaturesProvider declines every instance (USER 2026-11-16): no digit, no ordinal bump.
//   * Human play: the board-click target chooser over the full legal set, (no target) declines.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
const char* const kHull = "Hullbreaker Horror";
const char* const kBrin = "Brinelin, the Moon Kraken";

const CardDefinition& BDef(const std::string& name)
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
const SeaCreaturesProvider& BSea()
{
    static const SeaCreaturesProvider p;
    return p;
}
const GenericProvider& BPlain()   // the Generic surface: BounceTriggerDeclineOnly false -> searched
{
    static const GenericProvider p;
    return p;
}
GameState BBoard(const DecisionProvider* prov)
{
    GameState s;
    s.m_provider          = prov;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.deck_has_cast_target_trigger = true;
    for (int k = 0; k < 10; ++k)
    {
        Card c = BDef("Island").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}
int BPut(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = BDef(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
int BSpawn(GameState& s, int k)
{
    Permanent p;
    p.card.m_name = "3/3 Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = 3;
    p.card.m_toughness = 3;
    p.card.m_number    = GameState::kFirstOppSpawnNumber + k;
    p.controller_index = 1;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
bool OnBf(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return true; } }
    return false;
}
bool InHand(const GameState& s, int num, int who = 0)
{
    for (const Card& c : s.players[who].hand) { if (c.m_number == num) { return true; } }
    return false;
}
bool InGy(const GameState& s, int num, int who = 0)
{
    for (const Card& c : s.players[who].graveyard) { if (c.m_number == num) { return true; } }
    return false;
}
int CountName(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}
// Resolve a creature spell through the executor (EffectHandler::Resolve: the legend rule, then the drains).
void ExecResolve(GameState& s, const std::string& name, int number)
{
    const CardDefinition& def = BDef(name);
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = def.card;
    e.source.m_number  = number;
    e.controller_index = 0;
    EffectHandler::Resolve(s, e, def);
}
Action BCast(const std::string& name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &BDef(name);
    a.hand_index     = -1;
    a.cost           = BDef(name).card.m_mana_cost;
    a.is_noncreature = !BDef(name).card.IsCreature();
    return a;
}
struct TargetChooserScope
{
    TargetChooser chooser;
    TargetChooser* saved;
    explicit TargetChooserScope(TargetChooser fn) : chooser(std::move(fn)), saved(g_play_target_chooser)
    { g_play_target_chooser = &chooser; }
    ~TargetChooserScope() { g_play_target_chooser = saved; }
};
}   // namespace

TEST_CASE("Sea I4: the cards -- costs, types, params")
{
    const CardDefinition& h = BDef(kHull);
    CHECK(h.card.m_mana_cost.ManaValue() == 7);
    CHECK(h.card.IsCreature());
    CHECK(h.card.m_power == 7);
    CHECK(h.card.m_toughness == 8);
    CHECK(h.card.HasKeyword(Keyword::Flash));
    CHECK(h.params.cant_be_countered);
    CHECK(h.params.cast_trigger_bounce_nonland);
    CHECK(h.params.cast_trigger_bounce_min_mv == 0);
    CHECK(h.params.cast_trigger_bounce_spell_opponent);
    CHECK_FALSE(h.params.etb_bounce_nonland);
    CHECK_FALSE(h.hand_gain_audited);
    const CardDefinition& b = BDef(kBrin);
    CHECK(b.card.m_mana_cost.ManaValue() == 8);
    CHECK(b.card.HasSupertype(Supertype::Legendary));
    CHECK(b.card.m_power == 6);
    CHECK(b.card.m_toughness == 8);
    CHECK(b.params.cast_trigger_bounce_nonland);
    CHECK(b.params.cast_trigger_bounce_min_mv == 6);
    CHECK(b.params.etb_bounce_nonland);
    CHECK_FALSE(b.params.cast_trigger_bounce_spell_opponent);
    CHECK_FALSE(b.hand_gain_audited);
}

TEST_CASE("Sea I4: Hullbreaker's cast trigger -- searched per instance, decline first, either side, a new object")
{
    auto board = [] {
        GameState s = BBoard(&BPlain());
        BPut(s, kHull, 10);
        const int ring = BPut(s, "Sol Ring", 11);
        s.battlefield[static_cast<std::size_t>(ring)].tapped = true;
        BPut(s, "Island", 12);
        BSpawn(s, 0);
        return s;
    };
    const CardDefinition& wave = BDef("Whelming Wave");
    SUBCASE("no pin: rank 0 = decline; one instance bumps the ordinal")
    {
        GameState s = board();
        FireOnCastTriggers(s, wave, 4);
        CHECK(OnBf(s, 10));
        CHECK(OnBf(s, 11));
        CHECK(OnBf(s, GameState::kFirstOppSpawnNumber));
        CHECK(s.cast_target_trigger_ordinal == 1);
    }
    SUBCASE("digit 1 = the first own nonland class (Hullbreaker), 2 = the tapped Sol Ring; the spawn is folded away")
    {
        for (int d = 0; d < 6; ++d)
        {
            GameState s = board();
            ScriptedCastTriggerTarget pin(UntapPumpRankPack({ d }, 3), s.TurnSeq(), s.cast_target_trigger_ordinal);
            FireOnCastTriggers(s, wave, 4);
            CHECK(OnBf(s, GameState::kFirstOppSpawnNumber));   // never offered in search
            CHECK(OnBf(s, 12));                                // a land is never legal
            if (d == 1) { CHECK_FALSE(OnBf(s, 10)); CHECK(InHand(s, 10)); CHECK(OnBf(s, 11)); }
            else if (d >= 2) { CHECK_FALSE(OnBf(s, 11)); CHECK(InHand(s, 11)); CHECK(OnBf(s, 10)); }   // clamps
            else { CHECK(OnBf(s, 10)); CHECK(OnBf(s, 11)); }
            CHECK(s.permanent_left_under_control_this_turn == (d >= 1));
        }
    }
    SUBCASE("two Hullbreakers: two instances, independent digits")
    {
        GameState s = board();
        BPut(s, kHull, 13);
        // Order: decline, Hullbreaker 10 / 13 (one identity class), Sol Ring. Instance 0 returns Sol Ring, instance 1
        // then sees decline, Hullbreaker class -> returns one Hullbreaker.
        ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 2, 1 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
        FireOnCastTriggers(s, wave, 4);
        CHECK(s.cast_target_trigger_ordinal == 2);
        CHECK(InHand(s, 11));
        CHECK(CountName(s, kHull) == 1);
    }
    SUBCASE("Hullbreaker never triggers off its own cast")
    {
        GameState s = BBoard(&BPlain());
        BPut(s, "Sol Ring", 11);
        ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 1 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
        FireOnCastTriggers(s, BDef(kHull), 7);
        CHECK(OnBf(s, 11));
        CHECK(s.cast_target_trigger_ordinal == 0);
    }
}

TEST_CASE("Sea I4: a returned token ceases to exist; a returned Aura leaves its land; the orphan sweep runs")
{
    GameState s = BBoard(&BPlain());
    BPut(s, kHull, 10);
    const int forest = BPut(s, "Forest", 20);
    const int sprawl = BPut(s, "Utopia Sprawl", 21);
    s.battlefield[static_cast<std::size_t>(sprawl)].aura_attached_to = s.battlefield[static_cast<std::size_t>(forest)].card.m_number;
    CreateTokens(s, 0, 1, 8, 8, { "Kraken" }, "U", { "Hexproof" });
    int token = -1;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { token = p.card.m_number; } }
    REQUIRE(token != -1);
    // Order: decline, Hullbreaker, Utopia Sprawl, the token.
    {
        ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 3 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
        FireOnCastTriggers(s, BDef("Whelming Wave"), 4);
    }
    CHECK_FALSE(OnBf(s, token));
    CHECK(s.players[0].hand.empty());   // a token does not reach the hand
    {
        ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 2 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
        FireOnCastTriggers(s, BDef("Whelming Wave"), 4);
    }
    CHECK_FALSE(OnBf(s, 21));
    CHECK(InHand(s, 21));
    CHECK(OnBf(s, 20));
}

TEST_CASE("Sea I4: Brinelin's MV gate reads the printed mana value; it sees Hullbreaker's cast, not its own")
{
    GameState s = BBoard(&BPlain());
    BPut(s, kBrin, 30);
    BPut(s, "Sol Ring", 11);
    ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 2, 2 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
    FireOnCastTriggers(s, BDef("Whelming Wave"), 4);   // MV 4: no trigger
    CHECK(s.cast_target_trigger_ordinal == 0);
    CHECK(OnBf(s, 11));
    FireOnCastTriggers(s, BDef(kHull), 0);             // MV 7 (free cast: the MV gate is not the mana spent)
    CHECK(s.cast_target_trigger_ordinal == 1);
    CHECK(InHand(s, 11));
}

TEST_CASE("Sea I4: SeaCreaturesProvider declines every bounce instance (no digit, no ordinal bump)")
{
    GameState s = BBoard(&BSea());
    BPut(s, kHull, 10);
    BPut(s, kBrin, 30);
    BPut(s, "Sol Ring", 11);
    ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 3, 3 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
    FireOnCastTriggers(s, BDef("Serpent of Yawning Depths"), 6);   // Hullbreaker + Brinelin both trigger
    CHECK(OnBf(s, 10));
    CHECK(OnBf(s, 30));
    CHECK(OnBf(s, 11));
    CHECK(s.cast_target_trigger_ordinal == 0);
    CHECK(BSea().BounceTriggerDeclineOnly(s, 0, BDef(kHull)));
    CHECK(BSea().BounceTriggerDeclineOnly(s, 0, BDef(kBrin)));
    CHECK_FALSE(BPlain().BounceTriggerDeclineOnly(s, 0, BDef(kHull)));
}

TEST_CASE("Sea I4: Brinelin's ETB resolves after the resolution (executor) and may return Brinelin itself")
{
    GameState s = BBoard(&BPlain());
    BPut(s, "Sol Ring", 11);
    // Order at the drain: decline, Sol Ring, Brinelin (newest).
    ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 2 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
    ExecResolve(s, kBrin, 30);
    CHECK(g_pending_etb_bounces.empty());
    CHECK_FALSE(OnBf(s, 30));
    CHECK(InHand(s, 30));
    CHECK(OnBf(s, 11));
    CHECK(s.cast_target_trigger_ordinal == 1);
}

TEST_CASE("Sea I4: a second Brinelin -- the ETB drains AFTER the legend rule, in the executor AND the rollout")
{
    // Brinelin #1 (30) on the battlefield; cast #2 (31). Instance 0 = #1's MV>=6 cast trigger (declined), instance 1 =
    // #2's ETB, after the legend rule: the only nonland permanent left is the kept copy, and digit 1 returns it. The
    // doomed copy is never an option. Result in both worlds: no Brinelin on the battlefield, one in hand, one in the
    // graveyard.
    const int k = UntapPumpRankPack({ 0, 1 }, 2);
    GameState ex = BBoard(&BPlain());
    BPut(ex, kBrin, 30);
    {
        ScriptedCastTriggerTarget pin(k, ex.TurnSeq(), ex.cast_target_trigger_ordinal);
        FireOnCastTriggers(ex, BDef(kBrin), 8);
        ExecResolve(ex, kBrin, 31);
    }
    CHECK(CountName(ex, kBrin) == 0);
    CHECK((InHand(ex, 30) ? 1 : 0) + (InHand(ex, 31) ? 1 : 0) == 1);
    CHECK((InGy(ex, 30) ? 1 : 0) + (InGy(ex, 31) ? 1 : 0) == 1);
    CHECK(ex.cast_target_trigger_ordinal == 2);

    GameState ro = BBoard(&BPlain());
    BPut(ro, kBrin, 30);
    for (int i = 0; i < 8; ++i) { BPut(ro, "Island", 100 + i); }
    Card c = BDef(kBrin).card;
    c.m_number = 31;
    ro.players[0].hand.push_back(c);
    TurnSolver::Plan plan;
    plan.actions = { BCast(kBrin) };
    plan.cast_trigger_target_choice = k;
    TurnSolver::ApplyPlan(ro, plan, /*is_pre_combat=*/true);
    CHECK(g_pending_etb_bounces.empty());
    CHECK(CountName(ro, kBrin) == 0);
    CHECK((InHand(ro, 30) ? 1 : 0) + (InHand(ro, 31) ? 1 : 0) == 1);
    CHECK((InGy(ro, 30) ? 1 : 0) + (InGy(ro, 31) ? 1 : 0) == 1);
}

TEST_CASE("Sea I4: Brinelin #1 returns ITSELF off #2's cast -- no legend rule, #2's ETB still fires")
{
    // Instance 0 (#1's cast trigger): decline, Brinelin #1 -> digit 1 returns #1. Instance 1 (#2's ETB): decline,
    // Sol Ring, #2 -> digit 1 returns the Sol Ring.
    GameState s = BBoard(&BPlain());
    BPut(s, kBrin, 30);
    ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 1, 1 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
    FireOnCastTriggers(s, BDef(kBrin), 8);
    CHECK(InHand(s, 30));
    BPut(s, "Sol Ring", 11);
    ExecResolve(s, kBrin, 31);
    CHECK(OnBf(s, 31));
    CHECK(InHand(s, 11));
    CHECK_FALSE(InGy(s, 31));
}

TEST_CASE("Sea I4: human play -- the full legal set (both sides) and (no target) declines")
{
    GameState s = BBoard(&BSea());   // the provider's prune never applies to the human
    BPut(s, kHull, 10);
    BPut(s, "Sol Ring", 11);
    BPut(s, "Island", 12);
    BSpawn(s, 0);
    int seen_legal = -1;
    SUBCASE("pick the opponent's spawn")
    {
        TargetChooserScope scope([&](const GameState& st, const CardDefinition& def, int, int max_t, int,
                                     const std::vector<ChosenTarget>& heur) -> std::vector<ChosenTarget> {
            CHECK(def.card.m_name.str() == kHull);
            CHECK(max_t == 1);
            CHECK(heur.empty());   // the engine's default: decline
            int bi = -1;
            for (int i = 0; i < static_cast<int>(st.battlefield.size()); ++i)
            { if (st.battlefield[static_cast<std::size_t>(i)].card.m_number == GameState::kFirstOppSpawnNumber) { bi = i; } }
            seen_legal = bi;
            return { ChosenTarget{ 1, bi, 0 } };
        });
        FireOnCastTriggers(s, BDef("Whelming Wave"), 4);
        CHECK(seen_legal >= 0);
        CHECK_FALSE(OnBf(s, GameState::kFirstOppSpawnNumber));
        CHECK(s.players[1].hand.empty());   // a spawn ceases to exist
    }
    SUBCASE("(no target)")
    {
        TargetChooserScope scope([&](const GameState&, const CardDefinition&, int, int, int,
                                     const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget> { return {}; });
        FireOnCastTriggers(s, BDef("Whelming Wave"), 4);
        CHECK(OnBf(s, 10));
        CHECK(OnBf(s, 11));
        CHECK(OnBf(s, GameState::kFirstOppSpawnNumber));
    }
}

TEST_CASE("Sea I4: Raggadragga and a searched bounce share the instance ordinal, in firing order")
{
    GameState s = BBoard(&BPlain());
    BPut(s, "Raggadragga, Goreguts Boss", 40);
    BPut(s, kHull, 10);
    BPut(s, "Sol Ring", 11);
    // One cast spending 8: instance 0 = Raggadragga's untap-pump (digit 0), instance 1 = Hullbreaker's bounce (digit 3:
    // decline, Raggadragga, Hullbreaker, Sol Ring).
    ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 0, 3 }, 2), s.TurnSeq(), s.cast_target_trigger_ordinal);
    FireOnCastTriggers(s, BDef("End-Raze Forerunners"), 8);
    CHECK(s.cast_target_trigger_ordinal == 2);
    CHECK(InHand(s, 11));
}
