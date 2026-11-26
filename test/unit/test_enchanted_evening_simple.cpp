// Enchanted Evening onboarding, the four "simple" cards (2026-11-25):
//   * Sunpetal Grove: a G/W check land (checkland_subtypes Forest / Plains).
//   * Enchantress's Presence: draw_on_cast_types ["Enchantment"] -- a cast-TYPE draw watcher in
//     FireOnCastTriggers (both cast paths); never sees its own cast; an instant does not trigger it.
//   * Oblivion Ring: etb_exile_nonland_until_leaves -- a MANDATORY both-sides ETB exile, linked to the
//     source (kExileUntilLeavesBase), returned by FireLeavesBattlefieldTriggers under the OWNER.
//   * Sterling Grove: grants_other_enchantments_shroud (every shroud predicate; binds our own targeting)
//     + sac_tutor_top_cost / sac_tutor_top_types -- a searched main-phase activation (SacTutorTop, one
//     variant per candidate) and the upkeep priority window (GameEngine::UpkeepTutorOptions).
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
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
const char* const kPresence = "Enchantress's Presence";
const char* const kORing    = "Oblivion Ring";
const char* const kGrove    = "Sterling Grove";

const CardDefinition& EeDef(const std::string& name)
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

Card EeZone(const std::string& name, int number)
{
    Card c = EeDef(name).card;
    c.m_number = number;
    return c;
}

int EePut(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = EeDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int EeSpawn(GameState& s, int pw, int number)
{
    Card c;
    c.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power = pw;
    c.m_toughness = pw;
    c.m_number = number;
    Permanent p;
    p.card = c;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState EeBoard(int forests, int plains)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    int n = 100;
    for (int k = 0; k < forests; ++k) { EePut(s, "Forest", ++n); }
    for (int k = 0; k < plains; ++k)  { EePut(s, "Plains", ++n); }
    // Library, top first: a land, then the enchantments a tutor can find.
    s.players[0].library.push_back(EeZone("Forest", 900));
    s.players[0].library.push_back(EeZone("Exploration", 901));
    s.players[0].library.push_back(EeZone(kORing, 902));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(EeZone("Plains", 910 + k)); }
    return s;
}

int EeIndex(const GameState& s, const std::string& name, int controller = -1)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        if (s.battlefield[i].card.m_name.str() == name
            && (controller < 0 || s.battlefield[i].controller_index == controller)) { return i; }
    }
    return -1;
}

bool EeInGraveyard(const GameState& s, int player, const std::string& name)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool EeInHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

void EeResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState EeRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, EeResolveAll);
    return exec;
}

void EeCheckWorlds(const GameState& s, const TurnSolver::Plan& plan,
                   const std::function<void(const GameState&)>& check)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = EeRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        check(*g);
    }
}

// The plan whose ONLY action is `kind` of `name` (tutor_target `pin`).
const TurnSolver::Plan* EeFindPlan(const std::vector<TurnSolver::Plan>& plans, Action::Kind kind,
                                   const std::string& name, const std::string& pin = "")
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == kind && a.card_name.str() == name && a.tutor_target.str() == pin) { return &p; }
    }
    return nullptr;
}
}   // namespace

// ---- the cards -----------------------------------------------------------------------------------
TEST_CASE("Enchanted Evening simple cards: costs, types, params")
{
    const CardDefinition& sun = EeDef("Sunpetal Grove");
    CHECK(sun.card.IsLand());
    CHECK(sun.params.checkland_subtypes == std::vector<std::string>{ "Forest", "Plains" });
    const CardDefinition& pres = EeDef(kPresence);
    CHECK(pres.card.m_mana_cost.ManaValue() == 3);
    CHECK(pres.card.IsEnchantment());
    CHECK(pres.params.draw_on_cast_types == std::vector<std::string>{ "Enchantment" });
    CHECK(pres.params.draw_on_cast_subtypes.empty());
    const CardDefinition& ring = EeDef(kORing);
    CHECK(ring.card.m_mana_cost.ManaValue() == 3);
    CHECK(ring.params.etb_exile_nonland_until_leaves);
    const CardDefinition& grove = EeDef(kGrove);
    CHECK(grove.card.m_mana_cost.ManaValue() == 2);
    CHECK(grove.params.grants_other_enchantments_shroud);
    REQUIRE(grove.params.sac_tutor_top_cost.has_value());
    CHECK(grove.params.sac_tutor_top_cost->ManaValue() == 1);
    // The derived search spec; the SOURCE itself is no tutor (its entry must not search).
    REQUIRE(grove.activated_tutor != nullptr);
    CHECK(grove.activated_tutor->tutor_to_top);
    CHECK(grove.activated_tutor->tutor_types == std::vector<std::string>{ "Enchantment" });
    CHECK_FALSE(grove.params.tutor_to_top);
    CHECK(grove.params.tutor_types.empty());
    CHECK(EeDef("Forest").activated_tutor == nullptr);
}

// ---- Enchantress's Presence ------------------------------------------------------------------------
TEST_CASE("Enchantress's Presence: an enchantment cast draws in BOTH worlds; its own cast does not")
{
    GameState s = EeBoard(3, 1);
    EePut(s, kPresence, 50);
    s.players[0].hand.push_back(EeZone("Exploration", 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = EeFindPlan(plans, Action::Kind::CastFromHand, "Exploration");
    REQUIRE(cast != nullptr);
    EeCheckWorlds(s, *cast, [](const GameState& g) {
        CHECK(EeIndex(g, "Exploration", 0) >= 0);
        // The top card (a Forest) was drawn. The rollout may then play it off Exploration's extra drop (its
        // mid-turn hand-entry playout); the committed-plan executor keeps it in hand -- either is a draw.
        int forests = 0;
        for (const Permanent& q : g.battlefield) { if (q.card.m_name.str() == "Forest") { ++forests; } }
        CHECK((EeInHand(g, "Forest") || forests == 4));
        CHECK(g.players[0].library.size() == 8);
        CHECK(g.players[0].cards_drawn_this_turn == 1);
    });

    GameState t = EeBoard(3, 1);                              // no Presence on the battlefield
    t.players[0].hand.push_back(EeZone(kPresence, 31));
    const std::vector<TurnSolver::Plan> plans2 = TurnSolver::EnumerateMainPlans(t, true);
    const TurnSolver::Plan* cast2 = EeFindPlan(plans2, Action::Kind::CastFromHand, kPresence);
    REQUIRE(cast2 != nullptr);
    EeCheckWorlds(t, *cast2, [](const GameState& g) {
        CHECK(EeIndex(g, kPresence, 0) >= 0);
        CHECK(g.players[0].hand.empty());                     // casting Presence draws nothing
    });
}

TEST_CASE("Enchantress's Presence: a non-enchantment spell does not trigger; two Presences draw two")
{
    GameState s = EeBoard(1, 0);
    EePut(s, kPresence, 50);
    FireOnCastTriggers(s, EeDef("Lightning Bolt"), 1);
    CHECK(s.players[0].hand.empty());
    EePut(s, kPresence, 51);
    FireOnCastTriggers(s, EeDef("Wild Growth"), 1);           // an Aura is an enchantment spell
    CHECK(s.players[0].hand.size() == 2);
    // The opponent's Presence would not see OUR cast ("whenever YOU cast").
    GameState t = EeBoard(1, 0);
    EePut(t, kPresence, 60, /*controller=*/1);
    FireOnCastTriggers(t, EeDef("Exploration"), 1);
    CHECK(t.players[0].hand.empty());
    CHECK(t.players[1].hand.empty());
}

// ---- Oblivion Ring ---------------------------------------------------------------------------------
TEST_CASE("Oblivion Ring CAST exiles the opponent's spawn in BOTH worlds, linked; its leave returns it")
{
    GameState s = EeBoard(2, 2);
    EeSpawn(s, 2, 700001);
    EePut(s, "Exploration", 40);
    s.players[0].hand.push_back(EeZone(kORing, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = EeFindPlan(plans, Action::Kind::CastFromHand, kORing);
    REQUIRE(cast != nullptr);
    EeCheckWorlds(s, *cast, [](const GameState& g) {
        const int ri = EeIndex(g, kORing, 0);
        REQUIRE(ri >= 0);
        CHECK(EeIndex(g, "2/2 Creature", 1) < 0);            // the spawn, not our Exploration
        CHECK(EeIndex(g, "Exploration", 0) >= 0);
        CHECK(g.battlefield[ri].linked_exile_number == 700001);
        bool linked = false;
        for (const Card& c : g.exile)
        {
            if (c.m_number != 700001) { continue; }
            linked = CardIsExileUntilLeaves(c) && ExileUntilLeavesSource(c) == 30 && ExileUntilLeavesOwner(c) == 1;
            CHECK_FALSE(CardIsLinkedExile(c));
            CHECK_FALSE(CardIsSkyclaveExile(c));
        }
        CHECK(linked);
        // O-Ring leaves (sacrificed as a cost: the noncreature route) -> the spawn returns under its owner.
        GameState h = g;
        SacrificeSourceAsCost(h, 0, EeIndex(h, kORing, 0));
        CHECK(EeInGraveyard(h, 0, kORing));
        CHECK(EeIndex(h, "2/2 Creature", 1) >= 0);
        CHECK(h.exile.empty());
    });
}

TEST_CASE("Oblivion Ring target is a SEARCHED axis: one cast variant per legal target, each pinned, both worlds")
{
    GameState s = EeBoard(2, 2);
    EeSpawn(s, 2, 700001);
    EePut(s, "Exploration", 40);
    s.players[0].hand.push_back(EeZone(kORing, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    // The base (the live-board pick) plus one variant per legal target -- distinct plans (#OR keeps the
    // dedup from folding them).
    const TurnSolver::Plan* base = EeFindPlan(plans, Action::Kind::CastFromHand, kORing);
    const TurnSolver::Plan* opp  = EeFindPlan(plans, Action::Kind::CastFromHand, kORing, ExileUntilLeavesPin(700001));
    const TurnSolver::Plan* ours = EeFindPlan(plans, Action::Kind::CastFromHand, kORing, ExileUntilLeavesPin(40));
    REQUIRE(base != nullptr);
    REQUIRE(opp != nullptr);
    REQUIRE(ours != nullptr);
    CHECK(ExileUntilLeavesPinTarget(ExileUntilLeavesPin(40)) == 40);
    CHECK(ExileUntilLeavesPinTarget("AX40") == 0);
    // The pinned OWN target is what both worlds exile (not the provider's opponent-first pick).
    EeCheckWorlds(s, *ours, [](const GameState& g) {
        const int ri = EeIndex(g, kORing, 0);
        REQUIRE(ri >= 0);
        CHECK(EeIndex(g, "Exploration", 0) < 0);
        CHECK(EeIndex(g, "2/2 Creature", 1) >= 0);
        CHECK(g.battlefield[ri].linked_exile_number == 40);
    });
    EeCheckWorlds(s, *opp, [](const GameState& g) {
        CHECK(EeIndex(g, "2/2 Creature", 1) < 0);
        CHECK(EeIndex(g, "Exploration", 0) >= 0);
    });
    // A pin whose permanent is gone falls back to the live-board pick (the opponent's spawn).
    GameState z = EeBoard(2, 2);
    EeSpawn(z, 2, 700001);
    EePut(z, kORing, 30);
    ResolveEtbExileNonlandUntilLeaves(z, 0, 30, kORing, ExileUntilLeavesPin(999));
    CHECK(EeIndex(z, "2/2 Creature", 1) < 0);

    // ONE legal target -> no variant (the base takes it).
    GameState t = EeBoard(2, 2);
    EeSpawn(t, 2, 700001);
    t.players[0].hand.push_back(EeZone(kORing, 30));
    const std::vector<TurnSolver::Plan> plans1 = TurnSolver::EnumerateMainPlans(t, true);
    CHECK(EeFindPlan(plans1, Action::Kind::CastFromHand, kORing) != nullptr);
    CHECK(EeFindPlan(plans1, Action::Kind::CastFromHand, kORing, ExileUntilLeavesPin(700001)) == nullptr);
}

TEST_CASE("Oblivion Ring with no opponent target MUST exile one of ours; it returns under us; Grove shroud is honoured")
{
    GameState s = EeBoard(2, 2);
    EePut(s, kPresence, 41);
    const int src = EePut(s, kORing, 30);
    ResolveEtbExileNonlandUntilLeaves(s, 0, 30, kORing);
    CHECK(EeIndex(s, kPresence, 0) < 0);                      // the only other nonland permanent
    CHECK(s.battlefield[EeIndex(s, kORing, 0)].linked_exile_number == 41);
    (void)src;
    GameState h = s;
    const int ri = EeIndex(h, kORing, 0);
    const Card left = h.battlefield[ri].card;
    h.battlefield.erase(h.battlefield.begin() + ri);
    FireLeavesBattlefieldTriggers(h, 0, left);
    const int back = EeIndex(h, kPresence, 0);
    REQUIRE(back >= 0);
    CHECK(h.battlefield[back].entered_this_turn);              // a new object

    // Sterling Grove's grant: our OTHER enchantments are illegal targets; the Grove itself is not shrouded.
    GameState g = EeBoard(2, 2);
    EePut(g, kGrove, 42);
    EePut(g, kPresence, 41);
    EePut(g, kORing, 30);
    const std::vector<int> legal = EtbExileNonlandLegal(g, 0, 30);
    REQUIRE(legal.size() == 1);
    CHECK(g.battlefield[legal[0]].card.m_name.str() == kGrove);
    // No legal target at all -> the trigger is removed (the Grove shrouds the Presence; no Grove case):
    GameState z = EeBoard(2, 2);
    EePut(z, kORing, 30);
    ResolveEtbExileNonlandUntilLeaves(z, 0, 30, kORing);
    CHECK(z.exile.empty());
    CHECK(EeIndex(z, kORing, 0) >= 0);
}

// ---- Sterling Grove --------------------------------------------------------------------------------
TEST_CASE("Sterling Grove: other enchantments we control have shroud; not itself; not the opponent's; behind the stamp")
{
    GameState s = EeBoard(1, 1);
    const int g  = EePut(s, kGrove, 42);
    const int pr = EePut(s, kPresence, 41);
    const int op = EePut(s, "Exploration", 43, /*controller=*/1);
    const int fo = EeIndex(s, "Forest", 0);
    CHECK(EnchantmentShroudFromGrant(s.battlefield[pr], s));
    CHECK(PermanentHasShroud(s.battlefield[pr], s));
    CHECK_FALSE(EnchantmentShroudFromGrant(s.battlefield[g], s));
    CHECK_FALSE(EnchantmentShroudFromGrant(s.battlefield[op], s));
    CHECK_FALSE(LandHasShroud(s.battlefield[fo], s));          // a land is not an enchantment (no EE yet)
    EePut(s, kGrove, 44);                                       // two Groves shroud each other
    CHECK(EnchantmentShroudFromGrant(s.battlefield[g], s));
    s.deck_has_enchantment_shroud = false;                      // a stamped deck without the card
    CHECK_FALSE(EnchantmentShroudFromGrant(s.battlefield[pr], s));
}

TEST_CASE("Sterling Grove main-phase activation: one variant per enchantment; both worlds sacrifice it and stack the pick on top")
{
    GameState s = EeBoard(1, 0);                                // one Forest: exactly the {1}
    EePut(s, kGrove, 42);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* ring = EeFindPlan(plans, Action::Kind::ActivatePermAbility, kGrove, kORing);
    const TurnSolver::Plan* expl = EeFindPlan(plans, Action::Kind::ActivatePermAbility, kGrove, "Exploration");
    REQUIRE(ring != nullptr);
    REQUIRE(expl != nullptr);
    CHECK(EeFindPlan(plans, Action::Kind::ActivatePermAbility, kGrove, "Forest") == nullptr);   // not an enchantment
    CHECK(ring->actions.front().ability_mode == Action::AbilityMode::SacTutorTop);
    EeCheckWorlds(s, *ring, [](const GameState& g) {
        CHECK(EeIndex(g, kGrove, 0) < 0);
        CHECK(EeInGraveyard(g, 0, kGrove));
        REQUIRE_FALSE(g.players[0].library.empty());
        CHECK(g.players[0].library.front().m_name.str() == kORing);
        CHECK(g.players[0].library.size() == 9);
        CHECK(g.battlefield[EeIndex(g, "Forest", 0)].tapped);   // the {1} was paid
    });
}

TEST_CASE("Sterling Grove + Enchantress's Presence: activation then an enchantment cast draws the tutored card")
{
    GameState s = EeBoard(2, 0);
    EePut(s, kGrove, 42);
    EePut(s, kPresence, 50);
    const int gi = EeIndex(s, kGrove, 0);
    REQUIRE(ResolveSacTutorTop(s, 0, gi, kORing));
    REQUIRE(s.players[0].library.front().m_name.str() == kORing);
    FireOnCastTriggers(s, EeDef("Utopia Sprawl"), 1);
    CHECK(EeInHand(s, kORing));
}

TEST_CASE("Sterling Grove upkeep window: an option per candidate, keyed on the source permanent; CastUpkeepTutor activates it")
{
    GameState s = EeBoard(1, 0);
    EePut(s, kGrove, 42);
    const auto opts = GameEngine::UpkeepTutorOptions(s);
    int grove_opts = 0;
    bool ring = false;
    for (const auto& o : opts)
    {
        if (o.first != 42) { continue; }
        ++grove_opts;
        if (o.second == kORing) { ring = true; }
    }
    CHECK(grove_opts == 2);                                     // Exploration, Oblivion Ring
    CHECK(ring);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    REQUIRE(eng.CastUpkeepTutor(s, 42, kORing));
    CHECK(EeIndex(s, kGrove, 0) < 0);
    CHECK(s.players[0].library.front().m_name.str() == kORing);
    // No mana -> no option.
    GameState t = EeBoard(0, 0);
    EePut(t, kGrove, 42);
    for (const auto& o : GameEngine::UpkeepTutorOptions(t)) { CHECK(o.first != 42); }
}

// COMPLETENESS (2026-11-25): "activate Sterling Grove (tutor an enchantment to the top), THEN cast an enchantment
// so Enchantress's Presence draws it" is ONE plan of the same main phase -- the LEAD twin of the activation
// (SacTutorTop, chosen_x == 1) runs in the pre-cast pass of BOTH worlds. EnumerateMainPlans is the root
// enumeration every budget draws from (an unlimited budget explores all of it), so the line is findable at
// --budget-ms 0. The trailing form of the same pair still draws the OLD top card (the order the lead exists for).
TEST_CASE("Sterling Grove LEAD activation: tutor to top, then an Aura cast -- Presence draws the tutored card the same turn")
{
    GameState s = EeBoard(2, 0);                                // {1} for the Grove + {G} for Wild Growth
    EePut(s, kGrove, 42);
    EePut(s, kPresence, 50);
    s.players[0].hand.push_back(EeZone("Wild Growth", 31));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    auto find_pair = [&](int lead) -> const TurnSolver::Plan*
    {
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.actions.size() != 2) { continue; }
            bool act = false, cast = false;
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacTutorTop
                    && a.tutor_target.str() == kORing && a.chosen_x == lead) { act = true; }
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Wild Growth") { cast = true; }
            }
            if (act && cast) { return &p; }
        }
        return nullptr;
    };
    const TurnSolver::Plan* lead  = find_pair(1);
    const TurnSolver::Plan* trail = find_pair(0);
    REQUIRE(lead != nullptr);
    REQUIRE(trail != nullptr);
    CHECK(TurnSolver::IsSacTutorTopLead(lead->actions.front()) != TurnSolver::IsSacTutorTopLead(lead->actions.back()));
    EeCheckWorlds(s, *lead, [](const GameState& g) {
        CHECK(EeIndex(g, kGrove, 0) < 0);
        CHECK(EeIndex(g, "Wild Growth", 0) >= 0);
        CHECK(EeInHand(g, kORing));                             // the Presence draw took the tutored card
        CHECK_FALSE(EeInHand(g, "Forest"));
    });
    EeCheckWorlds(s, *trail, [](const GameState& g) {
        CHECK(EeIndex(g, kGrove, 0) < 0);
        CHECK(EeInHand(g, "Forest"));                           // cast first: the old top card
        REQUIRE_FALSE(g.players[0].library.empty());
        CHECK(g.players[0].library.front().m_name.str() == kORing);
    });
}
