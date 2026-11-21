// Blood Sun step 5 (docs/design/blood-sun-pipeline.md §9.1-9.3, §12 step 5): Grim Tutor (lose_life_on_resolve, the
// per-cast named tutor axis, own death at 3 life), Dimir Machinations (LookTopExileReorder, LookKind::ExileThenReorder,
// every disposition reachable through the reorder pin, fold M1, the opponent target moving its land play) and Worldfire
// (exile everything, both life totals 1, no Emrakul shuffle, pending extra turn kept, land drops recomputed).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCards5()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def5(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card5(const std::string& name, int number)
{
    Card c = Def5(name).card;
    c.m_number = number;
    return c;
}

void Put5(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = Card5(name, number);
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
}

// Our main phase on the Blood Sun provider; own death live (the deck's stamp); a dealt opponent library.
GameState Base5(int life = 20)
{
    static const BloodSunProvider bp;
    EnsureCards5();
    GameState s;
    s.m_provider          = &bp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = false;
    s.own_death_live      = true;
    return s;
}

// The Grim Tutor tests pin the card's mechanics on libraries of non-deck cards (Lightning Bolt, basics), so they run
// with the step-6c USER target restriction OFF (MTG_BLOODSUN_USER_PRUNE; test_bloodsun_userprune.cpp tests it ON).
struct Unrestricted5
{
    Unrestricted5()  { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = 0; }
    ~Unrestricted5() { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = -1; }
};
int CountName(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

std::string Outcome(const GameState& s, int player, std::size_t top_n)
{
    std::string o = "top:";
    const Library& lib = s.players[static_cast<std::size_t>(player)].library;
    for (std::size_t i = 0; i < top_n && i < lib.size(); ++i) { o += lib[i].m_name.str() + ","; }
    o += "|exile:";
    std::vector<std::string> ex;
    for (const Card& c : s.exile) { ex.push_back(c.m_name.str()); }
    std::sort(ex.begin(), ex.end());
    for (const std::string& e : ex) { o += e + ","; }
    return o;
}

}   // namespace

TEST_CASE("bloodsun step 5: card params as the design states them; own death and the opponent library armed")
{
    EnsureCards5();
    const CardParams& gt = Def5("Grim Tutor").params;
    CHECK(gt.tutor_to_hand);
    CHECK(gt.lose_life_on_resolve == 3);
    CHECK(Def5("Grim Tutor").upkeep_fold_audited);   // the Overlord whitelist holds lose_life_on_resolve
    const CardParams& dm = Def5("Dimir Machinations").params;
    CHECK(dm.look_top_target_player_exile_reorder == 3);
    CHECK(dm.targeting == Targeting::Player);
    CHECK(dm.transmute_cost.has_value());
    const CardParams& wf = Def5("Worldfire").params;
    CHECK(wf.exile_all_permanents);
    CHECK(wf.exile_all_hands_and_graveyards);
    CHECK(wf.set_all_life_totals == 1);

    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Blood Sun/Blood Sun.cod"));
    CHECK(GoldFishRunner::DeckTouchesOpponentZones(deck));
    // Machinations ALONE reads the opponent's library; Grim Tutor alone / Worldfire alone arm own death.
    auto only = [&](const std::vector<std::string>& keep)
    {
        Decklist d;
        for (const Card& c : deck.mainboard)
        {
            const CardDefinition* cd = CardDatabase::Instance().Lookup(c.m_name.str());
            if (cd != nullptr && cd->card.IsLand()) { d.mainboard.push_back(c); continue; }
            if (std::find(keep.begin(), keep.end(), c.m_name.str()) != keep.end()) { d.mainboard.push_back(c); }
        }
        // A kept card the list no longer holds (Worldfire left the list at 04b85bf21, USER; it stays implemented and
        // generic) is added from the card data, so the per-card arming is still tested.
        for (const std::string& nm : keep)
        {
            const bool held = std::any_of(d.mainboard.begin(), d.mainboard.end(),
                                          [&](const Card& c) { return c.m_name.str() == nm; });
            const CardDefinition* cd = CardDatabase::Instance().Lookup(nm);
            if (!held && cd != nullptr) { d.mainboard.push_back(cd->card); }
        }
        return d;
    };
    CHECK(GoldFishRunner::DeckTouchesOpponentZones(only({ "Dimir Machinations" })));
    CHECK_FALSE(GoldFishRunner::DeckTouchesOpponentZones(only({ "Grim Tutor" })));
    for (const char* nm : { "Grim Tutor", "Worldfire" })
    {
        GameState s;
        GoldFishRunner::StampDeckTraits(s, only({ nm }));
        CHECK_MESSAGE(s.own_death_live, nm);
    }
    {
        GameState s;
        GoldFishRunner::StampDeckTraits(s, only({ "Dimir Machinations" }));
        CHECK_FALSE(s.own_death_live);   // City of Brass alone (a land) still does not arm it
    }
}

TEST_CASE("grim tutor: lose 3 life on resolution; at 3 life it is never offered autonomously (own death live)")
{
    const Unrestricted5 unrestricted;
    GameState s = Base5(/*life=*/4);
    for (int k = 0; k < 3; ++k) { Put5(s, "Swamp", 700 + k); }
    s.players[0].hand.push_back(Card5("Grim Tutor", 800));
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(Card5("Swamp", 900 + k)); }
    auto offered = [](const GameState& st)
    {
        int n = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(st, /*is_pre_combat=*/true))
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Grim Tutor") { ++n; } }
        return n;
    };
    CHECK(offered(s) > 0);
    CHECK_FALSE(CastLoseLifeWouldKill(s, 0, Def5("Grim Tutor")));
    GameState d3 = s;
    d3.players[0].life = 3;
    CHECK(CastLoseLifeWouldKill(d3, 0, Def5("Grim Tutor")));
    CHECK(offered(d3) == 0);
    GameState unarmed = d3;
    unarmed.own_death_live = false;
    CHECK(offered(unarmed) > 0);

    // The life loss itself (both worlds call this one body after PerformTutor): not damage.
    GameState r = s;
    ApplyLoseLifeOnResolve(r, 0, Def5("Grim Tutor"));
    CHECK(r.players[0].life == 1);
    CHECK(r.player_dealt_damage_this_turn[0] == 0);
}

TEST_CASE("grim tutor: the executor resolution fetches the pinned card, shuffles, and costs 3 life")
{
    const Unrestricted5 unrestricted;
    GameState s = Base5(/*life=*/20);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(Card5("Swamp", 900 + k)); }
    s.players[0].library.push_back(Card5("Lightning Bolt", 950));
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = Card5("Grim Tutor", 800);
    e.controller_index = 0;
    e.tutor_target     = "Lightning Bolt";
    EffectHandler::Resolve(s, e, Def5("Grim Tutor"));
    CHECK(CountName(s.players[0].hand, "Lightning Bolt") == 1);
    CHECK(s.players[0].library.size() == 6);
    CHECK(s.players[0].life == 17);
}

TEST_CASE("grim tutor: the per-cast named axis -- every pick of every copy is a plan (multiset over two copies)")
{
    const Unrestricted5 unrestricted;
    GameState s = Base5(/*life=*/20);
    for (int k = 0; k < 6; ++k) { Put5(s, "Swamp", 700 + k); }
    s.players[0].lands_played_this_turn = 1;   // no land drop: the menu is the casts alone
    s.players[0].hand.push_back(Card5("Grim Tutor", 800));
    s.players[0].hand.push_back(Card5("Grim Tutor", 801));
    for (const char* nm : { "Swamp", "Island", "Lightning Bolt" })
    {
        for (int k = 0; k < 2; ++k) { s.players[0].library.push_back(Card5(nm, 900 + static_cast<int>(s.players[0].library.size()))); }
    }
    std::set<std::string> singles, pairs;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        std::vector<std::string> tg;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Grim Tutor") { tg.push_back(a.tutor_target.str()); }
        }
        if (tg.size() == 1) { singles.insert(tg[0]); }
        if (tg.size() == 2) { std::sort(tg.begin(), tg.end()); pairs.insert(tg[0] + "+" + tg[1]); }
        if (!tg.empty()) { CHECK(p.tutor_choice == -1); }   // never the index axis
        for (const std::string& t : tg) { CHECK_FALSE(t.empty()); }
    }
    CHECK(singles == std::set<std::string>{ "Island", "Lightning Bolt", "Swamp" });
    CHECK(pairs.size() == 6);   // C(3+1, 2): every multiset of two picks over three names
}

TEST_CASE("dimir machinations on self: all 16 dispositions are distinct candidates and every one is reachable")
{
    const Unrestricted5 unrestricted;   // step 6e: the USER's B5 prune drops these casts (test_bloodsun_userprune.cpp)
    GameState s = Base5();
    for (int k = 0; k < 3; ++k) { Put5(s, "Swamp", 700 + k); }
    s.players[0].lands_played_this_turn = 1;
    s.players[0].hand.push_back(Card5("Dimir Machinations", 800));
    s.players[0].library.push_back(Card5("Island", 900));
    s.players[0].library.push_back(Card5("Lightning Bolt", 901));
    s.players[0].library.push_back(Card5("Forest", 902));
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(Card5("Swamp", 910 + k)); }
    std::vector<Card> looked(s.players[0].library.begin(), s.players[0].library.begin() + 3);
    CHECK(EnumerateTopDispositions(LookKind::ExileThenReorder, looked).size() == 16);
    CHECK(TopDispositionCandidates(s, looked, LookKind::ExileThenReorder).size() == 16);

    // The primitive under every pin: 16 distinct outcomes.
    std::set<std::string> direct;
    for (int k = 0; k < 16; ++k)
    {
        GameState c = s;
        ScriptedReorder pin(k);
        LookTopExileReorder(c, 0, 3, "Dimir Machinations");
        CHECK(c.players[0].library.size() + c.exile.size() == s.players[0].library.size());
        direct.insert(Outcome(c, 0, 3));
    }
    CHECK(direct.size() == 16);

    // The SEARCH reaches all of them: the plan menu's self-target Machinations plans (the base + the reorder-pin fan),
    // each applied through the rollout's plan apply.
    std::set<std::string> reached;
    int opp_plans = 0;
    for (TurnSolver::Plan p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool mach = false, opp = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Dimir Machinations")
            { mach = true; opp = a.player_target != 0; }
        }
        if (!mach) { continue; }
        if (opp) { ++opp_plans; continue; }
        GameState c = s;
        TurnSolver::ApplyPlanWithRecordsForTest(c, p, /*is_pre_combat=*/true);
        reached.insert(Outcome(c, 0, 3));
    }
    CHECK(reached == direct);
    CHECK(opp_plans == 0);   // no opponent library dealt here: the opponent target is not offered

    // FOLD M1: two same-name copies in the look -> 9 distinct dispositions (ordered sub-multisets of {A, A, B}).
    GameState f = s;
    f.players[0].library[1] = Card5("Island", 901);
    std::vector<Card> lk2(f.players[0].library.begin(), f.players[0].library.begin() + 3);
    CHECK(TopDispositionCandidates(f, lk2, LookKind::ExileThenReorder).size() == 9);
}

TEST_CASE("dimir machinations on the opponent: the look moves the opponent's next land play")
{
    const Unrestricted5 unrestricted;   // step 6e: the USER's B5 prune drops these casts (test_bloodsun_userprune.cpp)
    GameState s = Base5();
    s.opponent_library_dealt = true;
    s.opponent_plays_lands   = true;
    s.opponent_library_read  = true;
    Player& opp = s.players[1];
    opp.library.push_back(Card5("Lightning Bolt", 100001));
    opp.library.push_back(Card5("Lightning Bolt", 100002));
    opp.library.push_back(Card5("Forest", 100003));
    for (int k = 0; k < 6; ++k) { opp.library.push_back(Card5("Lightning Bolt", 100010 + k)); }

    // Untouched: the opponent draws a Bolt and has no land to play.
    {
        GameState c = s;
        opponentdeck::EndOfTurnDraw(c);
        CHECK_FALSE(opponentdeck::PlayLeftmostLand(c).played);
    }
    // Machinations at the opponent (the provider default there: its lands on top, the rest exiled): it draws the
    // Forest and plays it -- the Burgeoning trigger the target exists for.
    {
        GameState c = s;
        LookTopExileReorder(c, 1, 3, "Dimir Machinations");
        CHECK(c.players[1].library.size() == 7);
        CHECK(c.exile.size() == 2);
        opponentdeck::EndOfTurnDraw(c);
        const opponentdeck::LandPlayResult r = opponentdeck::PlayLeftmostLand(c);
        CHECK(r.played);
        CHECK(r.land_name == "Forest");
    }
    // The search offers the opponent target (last), with its own full disposition fan.
    for (int k = 0; k < 3; ++k) { Put5(s, "Swamp", 700 + k); }
    s.players[0].lands_played_this_turn = 1;
    s.players[0].hand.push_back(Card5("Dimir Machinations", 800));
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(Card5("Swamp", 910 + k)); }
    std::set<std::string> reached;
    for (TurnSolver::Plan p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool opp_t = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Dimir Machinations" && a.player_target != 0)
            { opp_t = true; }
        }
        if (!opp_t) { continue; }
        GameState c = s;
        TurnSolver::ApplyPlanWithRecordsForTest(c, p, /*is_pre_combat=*/true);
        reached.insert(Outcome(c, 1, 3));
    }
    CHECK(reached.size() == 9);   // {Bolt, Bolt, Forest}: the M1-folded dispositions, every one reached
}

TEST_CASE("worldfire: everything exiled (no Emrakul shuffle), both totals 1, extra turn kept, drops recomputed")
{
    GameState s = Base5(/*life=*/20);
    s.deck_has_static_land_drops = true;
    s.opponent_library_dealt = true;
    Put5(s, "Emrakul, the Aeons Torn", 600);
    Put5(s, "Exploration", 601);
    for (int k = 0; k < 3; ++k) { Put5(s, "Mountain", 610 + k); }
    Put5(s, "Forest", 100500, /*controller=*/1);
    s.players[0].bonus_land_drops_this_turn = StaticExtraLandDrops(s, 0);
    CHECK(s.players[0].LandDropsAvailable() == 2);
    s.players[0].hand.push_back(Card5("Lightning Bolt", 800));
    s.players[0].graveyard.push_back(Card5("Grim Tutor", 801));
    s.players[1].hand.push_back(Card5("Forest", 100600));
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(Card5("Swamp", 900 + k)); }
    s.pending_extra_turns = 1;
    s.players[1].life = 17;

    GameState ex = s;   // the executor's resolution
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = Card5("Worldfire", 850);
    e.controller_index = 0;
    EffectHandler::Resolve(ex, e, Def5("Worldfire"));
    CHECK(ex.battlefield.empty());
    CHECK(CountName(ex.exile, "Emrakul, the Aeons Torn") == 1);
    CHECK(CountName(ex.exile, "Grim Tutor") == 1);
    CHECK(CountName(ex.exile, "Lightning Bolt") == 1);
    CHECK(ex.players[0].hand.empty());
    CHECK(ex.players[1].hand.empty());
    CHECK(ex.players[0].library.size() == 5);           // no graveyard shuffled in: Emrakul was exiled
    CHECK(CountName(ex.players[0].graveyard, "Worldfire") <= 1);
    CHECK(CountName(ex.players[0].graveyard, "Emrakul, the Aeons Torn") == 0);
    CHECK(ex.players[0].life == 1);
    CHECK(ex.players[1].life == 1);
    CHECK(ex.opponent_lost_life_this_turn);
    CHECK(ex.pending_extra_turns == 1);
    CHECK(ex.players[0].LandDropsAvailable() == 1);       // Exploration left: its drop is gone this turn

    // The rollout's plan apply resolves it identically (lockstep).
    GameState ro = s;
    for (int k = 0; k < 9; ++k) { Put5(ro, "Mountain", 620 + k); }
    ro.players[0].lands_played_this_turn = 1;
    ro.players[0].hand.push_back(Card5("Worldfire", 850));
    bool applied = false;
    for (TurnSolver::Plan p : TurnSolver::EnumerateMainPlans(ro, /*is_pre_combat=*/true))
    {
        if (p.actions.size() != 1 || p.actions[0].card_name.str() != "Worldfire") { continue; }
        GameState c = ro;
        TurnSolver::ApplyPlanWithRecordsForTest(c, p, /*is_pre_combat=*/true);
        CHECK(c.battlefield.empty());
        CHECK(c.players[0].life == 1);
        CHECK(c.players[1].life == 1);
        CHECK(c.pending_extra_turns == 1);
        CHECK(c.players[0].library.size() == 5);
        CHECK(CountName(c.exile, "Emrakul, the Aeons Torn") == 1);
        CHECK(c.players[0].LandDropsAvailable() == 1);
        applied = true;
        break;
    }
    CHECK(applied);
}
