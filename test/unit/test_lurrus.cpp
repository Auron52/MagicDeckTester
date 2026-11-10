// Lurrus of the Dream-Den (Martyr Lifegain, 2026-11-10): "Once during each of your turns, you may cast a
// permanent spell with mana value 2 or less from your graveyard." (CardParams::gy_cast_permanent_max_mv)
//
// The permission is ONE legality test (SpellEffects.h GyCastPermissionLegal / GyCastOptionsNow) read by
// the enumeration, the site-9 activation key, the rollout's apply_one, the executor's CastSpellFromHand
// and the viewer. These tests drive both worlds -- the rollout (TurnSolver::ApplyPlan) and the executor
// (AIEngine::TakeTurn through the committed-plan seam) -- on the same plans and require the same board.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/Dominance.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& LuDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// A graveyard card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder): every type
// and cost test must read the definition.
Card LuZone(const std::string& name, int number)
{
    LuDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int LuPut(GameState& s, const std::string& name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card = LuDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void LuGy(GameState& s, const std::string& name, int number) { s.players[0].graveyard.push_back(LuZone(name, number)); }

const Permanent* LuFind(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool LuInGy(const GameState& s, int number)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

int LuCount(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && p.card.m_name.str() == name) { ++n; } }
    return n;
}

GameState LuState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(LuZone("Plains", 900 + k)); }
    return s;
}

std::vector<const Action*> GyCasts(const std::vector<Action>& acts)
{
    std::vector<const Action*> out;
    for (const Action& a : acts) { if (a.kind == Action::Kind::CastFromGraveyard) { out.push_back(&a); } }
    return out;
}

void LuResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState LuRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, LuResolveAll);
    return exec;
}

// A plan holding exactly the given actions (no land).
TurnSolver::Plan LuPlan(const std::vector<Action>& acts)
{
    TurnSolver::Plan p;
    p.actions = acts;
    return p;
}

// The board every apply test starts from: Lurrus (#1, not sick), three untapped Plains, and the graveyard.
GameState LuBoard()
{
    GameState s = LuState();
    LuPut(s, "Lurrus of the Dream-Den", 1);
    LuPut(s, "Plains", 101);
    LuPut(s, "Plains", 102);
    LuPut(s, "Plains", 103);
    return s;
}
}   // namespace

TEST_CASE("Lurrus: the legality test -- nonland permanent cards of mana value 2 or less only")
{
    CHECK(GyCastSpellLegal(2, LuDef("Martyr of Sands")));
    CHECK(GyCastSpellLegal(2, LuDef("Squadron Hawk")));
    CHECK(GyCastSpellLegal(2, LuDef("Umezawa's Jitte")));
    CHECK(GyCastSpellLegal(2, LuDef("Thraben Inspector")));
    CHECK_FALSE(GyCastSpellLegal(2, LuDef("Ranger-Captain of Eos")));     // MV 3
    CHECK_FALSE(GyCastSpellLegal(2, LuDef("Lurrus of the Dream-Den")));   // MV 3
    CHECK_FALSE(GyCastSpellLegal(2, LuDef("Ranger of Eos")));             // MV 4
    CHECK_FALSE(GyCastSpellLegal(2, LuDef("Swords to Plowshares")));      // not a permanent
    CHECK_FALSE(GyCastSpellLegal(2, LuDef("Plains")));                    // a land is never cast
    CHECK_FALSE(GyCastSpellLegal(0, LuDef("Martyr of Sands")));           // no permission
    CHECK(LuDef("Lurrus of the Dream-Den").params.gy_cast_permanent_max_mv == 2);
    CHECK(LuDef("Lurrus of the Dream-Den").card.HasKeyword(Keyword::Lifelink));
}

TEST_CASE("Lurrus: enumeration offers exactly the legal graveyard names, one family, once per turn")
{
    GameState s = LuBoard();
    LuGy(s, "Martyr of Sands", 30);
    LuGy(s, "Martyr of Sands", 31);            // a second copy is the same cast
    LuGy(s, "Umezawa's Jitte", 32);
    LuGy(s, "Ranger-Captain of Eos", 33);
    LuGy(s, "Plains", 34);
    LuGy(s, "Swords to Plowshares", 35);
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const std::vector<const Action*> gc = GyCasts(acts);
    std::vector<std::string> names;
    for (const Action* a : gc)
    {
        names.push_back(a->card_name.str());
        CHECK(a->gy_cast_source_id == 1);
        CHECK(a->discard_lands == 0);
        CHECK(a->hand_index == -1);
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{ "Martyr of Sands", "Umezawa's Jitte" });
    // The viewer's list is the same one.
    std::vector<std::string> view;
    for (const GyCastOption& o : GyCastOptionsNow(s, 0)) { view.push_back(o.card->m_name.str()); }
    std::sort(view.begin(), view.end());
    CHECK(view == names);

    // The use is spent: nothing offered.
    GameState used = s;
    used.battlefield[0].once_per_turn_used = true;
    CHECK(GyCasts(TurnSolver::CollectActionsForTest(used, true)).empty());
    // No Lurrus: nothing offered.
    GameState none = s;
    none.battlefield.erase(none.battlefield.begin());
    CHECK(GyCasts(TurnSolver::CollectActionsForTest(none, true)).empty());
    // An opponent's Lurrus grants us nothing.
    GameState opp = s;
    opp.battlefield[0].controller_index = opp.battlefield[0].owner_index = 1;
    CHECK(GyCasts(TurnSolver::CollectActionsForTest(opp, true)).empty());
    // A deck the stamp proves has no permission skips the whole scan.
    GameState gated = s;
    gated.deck_gy_readers = GyR_CreatureNames;
    CHECK(GyCasts(TurnSolver::CollectActionsForTest(gated, true)).empty());
}

TEST_CASE("Lurrus: the deck stamp sets GyR_GyCastNames exactly for a permission deck")
{
    Decklist with_card;
    with_card.mainboard.push_back(LuDef("Lurrus of the Dream-Den").card);
    with_card.mainboard.push_back(LuDef("Martyr of Sands").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(with_card) & GyR_GyCastNames) != 0);
    Decklist without;
    without.mainboard.push_back(LuDef("Martyr of Sands").card);
    without.mainboard.push_back(LuDef("Plains").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(without) & GyR_GyCastNames) == 0);
}

TEST_CASE("Lurrus: a graveyard cast is a REAL cast in both worlds (cost paid, permission spent, ETB fires)")
{
    // Thraben Inspector from the graveyard: {W} paid, the card on the battlefield, its Clue made.
    GameState s = LuBoard();
    LuGy(s, "Thraben Inspector", 40);
    const std::vector<const Action*> gc = GyCasts(TurnSolver::CollectActionsForTest(s, true));
    REQUIRE(gc.size() == 1);
    const TurnSolver::Plan plan = LuPlan({ *gc[0] });

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = LuRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(LuFind(*g, 40) != nullptr);              // Inspector entered
        CHECK_FALSE(LuInGy(*g, 40));                   // ...from the graveyard
        CHECK(LuCount(*g, "Clue Token") == 1);         // its ETB investigated
        REQUIRE(LuFind(*g, 1) != nullptr);
        CHECK(LuFind(*g, 1)->once_per_turn_used);      // the permission is spent
        int tapped = 0;
        for (const Permanent& p : g->battlefield) { if (p.card.m_name.str() == "Plains" && p.tapped) { ++tapped; } }
        CHECK(tapped == 1);                            // {W} paid
        CHECK(g->spells_cast_this_turn == s.spells_cast_this_turn + 1);   // a cast, counted
    }
}

TEST_CASE("Lurrus: two casts off one permission -- the second is stranded in both worlds")
{
    GameState s = LuBoard();
    LuGy(s, "Martyr of Sands", 30);
    LuGy(s, "Thraben Inspector", 40);
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const std::vector<const Action*> gc = GyCasts(acts);
    REQUIRE(gc.size() == 2);
    // The enumerator groups them as ONE family (a plan never holds both)...
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { CHECK(GyCasts(p.actions).size() <= 1); }
    // ...and a hand-built plan that does is stranded at its second cast, in both worlds.
    const TurnSolver::Plan plan = LuPlan({ *gc[0], *gc[1] });
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = LuRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        const int back = (LuFind(*g, 30) ? 1 : 0) + (LuFind(*g, 40) ? 1 : 0);
        CHECK(back == 1);
        CHECK(g->players[0].graveyard.size() == 1);
    }
    // The two worlds agree on WHICH one.
    CHECK((LuFind(roll, 30) != nullptr) == (LuFind(exec, 30) != nullptr));
}

TEST_CASE("Lurrus: a Lurrus that left is no permission (the cast strands, nothing paid)")
{
    GameState s = LuBoard();
    LuGy(s, "Martyr of Sands", 30);
    const std::vector<const Action*> gc = GyCasts(TurnSolver::CollectActionsForTest(s, true));
    REQUIRE(gc.size() == 1);
    const Action a = *gc[0];
    s.battlefield.erase(s.battlefield.begin());   // Lurrus gone before the cast
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, LuPlan({ a }), true);
    CHECK(LuInGy(roll, 30));
    for (const Permanent& p : roll.battlefield) { CHECK_FALSE(p.tapped); }
}

TEST_CASE("Lurrus: the site-9 key -- a card reaching the graveyard, or Lurrus entering, is a NEW option")
{
    // Martyr on the battlefield, Lurrus's use open: no graveyard key yet.
    GameState s = LuBoard();
    LuPut(s, "Martyr of Sands", 30);
    const std::vector<uint64_t> before = TurnSolver::SnapshotActivatableAbilities(s);
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(s, before));
    // Martyr sacrificed: it is in the graveyard and castable -> new.
    GameState after = s;
    for (std::size_t i = 0; i < after.battlefield.size(); ++i)
    {
        if (after.battlefield[i].card.m_number != 30) { continue; }
        after.battlefield.erase(after.battlefield.begin() + static_cast<long>(i));
        break;
    }
    LuGy(after, "Martyr of Sands", 30);
    CHECK(TurnSolver::PostEntryActivationPending(after, before));
    // ...but not once the permission is spent.
    GameState spent = after;
    spent.battlefield[0].once_per_turn_used = true;
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(spent, before));
    // A castable name that was there at plan start (a declined cast) is not new.
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(after, TurnSolver::SnapshotActivatableAbilities(after)));
    // Lurrus entering over a graveyard that holds a legal card: new.
    GameState pre = LuState();
    LuPut(pre, "Plains", 101);
    LuPut(pre, "Plains", 102);
    LuGy(pre, "Martyr of Sands", 30);
    const std::vector<uint64_t> k0 = TurnSolver::SnapshotActivatableAbilities(pre);
    GameState landed = pre;
    LuPut(landed, "Lurrus of the Dream-Den", 1, /*sick=*/true);
    CHECK(TurnSolver::PostEntryActivationPending(landed, k0));
}

TEST_CASE("Lurrus: an open permission is a death payoff for a self-only outlet (Kami / Farm Animals / Martyr)")
{
    GameState s = LuState();
    LuPut(s, "Kami of False Hope", 50);
    CHECK_FALSE(SelfSacOutletUseful(s, 0, 50));
    GameState l = s;
    LuPut(l, "Lurrus of the Dream-Den", 1);
    CHECK(SelfSacOutletUseful(l, 0, 50));
    GameState used = l;
    for (Permanent& p : used.battlefield) { if (p.card.m_number == 1) { p.once_per_turn_used = true; } }
    CHECK_FALSE(SelfSacOutletUseful(used, 0, 50));
    GameState in_hand = s;
    in_hand.players[0].hand.push_back(LuZone("Lurrus of the Dream-Den", 1));
    CHECK(SelfSacOutletUseful(in_hand, 0, 50));
    // A deck without the permission: the clause is gated off.
    GameState gated = l;
    gated.deck_gy_readers = GyR_None;
    CHECK_FALSE(SelfSacOutletUseful(gated, 0, 50));
    // Ranger-Captain of Eos (MV 3) is not a recursion target even with Lurrus out.
    GameState rc = LuState();
    LuPut(rc, "Ranger-Captain of Eos", 51);
    LuPut(rc, "Lurrus of the Dream-Den", 1);
    CHECK_FALSE(SelfSacOutletUseful(rc, 0, 51));
}

TEST_CASE("Lurrus: the dominance key sees which permanent cards sit in the graveyard")
{
    auto snap = [](const std::string& gy_card, std::uint32_t readers)
    {
        GameState s = LuState();
        s.step = Step::Untap;
        s.deck_gy_readers = readers;
        LuGy(s, gy_card, 30);
        return dominance::Build(s, DefaultProvider());
    };
    const dominance::DomSnap a = snap("Martyr of Sands", GyR_GyCastNames);
    const dominance::DomSnap b = snap("Squadron Hawk", GyR_GyCastNames);
    REQUIRE(a.comparable);
    REQUIRE(b.comparable);
    CHECK(a.exact != b.exact);
    // A nonpermanent is not a possible cast: invisible to the projection.
    CHECK(snap("Swords to Plowshares", GyR_GyCastNames).exact == snap("Unexpectedly Absent", GyR_GyCastNames).exact);
    CHECK(snap("Martyr of Sands", GyR_None).exact == snap("Squadron Hawk", GyR_None).exact);
}

TEST_CASE("Lurrus: the line spec's gycast= verb matches the graveyard cast (CheckLine)")
{
    GameState s = LuBoard();
    LuGy(s, "Martyr of Sands", 30);
    TurnSolver::LineSpec ls;
    ls.retrace_casts.push_back("Martyr of Sands");   // what ParseLineSpec makes of `gycast=Martyr of Sands`
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::LineCheck lc = TurnSolver::CheckLine(s, true, ls, &plans);
    INFO("reason: " << lc.reason);
    REQUIRE(lc.verdict == TurnSolver::LineCheck::Verdict::Accept);
    REQUIRE(lc.plan_index >= 0);
    const std::vector<const Action*> gc = GyCasts(plans[static_cast<std::size_t>(lc.plan_index)].actions);
    REQUIRE(gc.size() == 1);
    CHECK(gc[0]->card_name.str() == "Martyr of Sands");
    CHECK(gc[0]->gy_cast_source_id == 1);
}

TEST_CASE("Lurrus: the core loop in one main -- sacrifice Martyr, recast it, sacrifice again (site 9 + re-open)")
{
    // Lurrus, Martyr, Serra (none sick), 3 Plains; two white cards in hand; life 18.
    GameState s = LuState();
    s.players[0].life = 18;
    LuPut(s, "Lurrus of the Dream-Den", 1);
    LuPut(s, "Martyr of Sands", 30);
    LuPut(s, "Serra Ascendant", 31);
    LuPut(s, "Plains", 101);
    LuPut(s, "Plains", 102);
    LuPut(s, "Plains", 103);
    s.players[0].hand.push_back(LuZone("Ranger of Eos", 60));
    s.players[0].hand.push_back(LuZone("Ranger of Eos", 61));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* sac = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::SacCreatureOutlet
            && p.actions[0].sac_source_id == 30) { sac = &p; break; }
    }
    REQUIRE(sac != nullptr);
    TurnSolver::Plan all = *sac;
    all.bp_choice = 0;
    all.bp_at     = 0;
    all.bp_all    = true;
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, all, true);
    INFO("life " << roll.players[0].life);
    CHECK(roll.players[0].life == 30);
    CHECK(LuFind(roll, 30) == nullptr);
    CHECK(LuInGy(roll, 30));
    REQUIRE(LuFind(roll, 1) != nullptr);
    CHECK(LuFind(roll, 1)->once_per_turn_used);
}
