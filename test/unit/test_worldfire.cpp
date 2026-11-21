// WORLDFIRE onboarding (2026-11-21, docs/design/analysis-Worldfire.md): the exile-everything / life-becomes-1
// resolver (ResolveExileAllAndSetLife) in both worlds, the zones it must leave alone (exile: suspended cards,
// staged cards; floating mana), and Keldon Halberdier's suspend haste (CR 702.62a).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
const CardDefinition& WFDef(const std::string& name)
{
    static const bool loaded = [] { CardDatabase::Instance().LoadFromJson(
        ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card WFCard(const std::string& name, int id)
{ Card c = WFDef(name).card; c.m_number = id; return c; }
void WFPut(GameState& s, const std::string& name, int id, int who = 0, bool tapped = false)
{
    Permanent p; p.card = WFCard(name, id); p.controller_index = who; p.owner_index = who;
    p.tapped = tapped; p.entered_this_turn = false; s.battlefield.push_back(p);
}
// A scheduled opponent spawn: a def-less "1/1 Creature" token in the spawn number band.
void WFSpawn(GameState& s)
{
    Card t; t.m_name = "1/1 Creature"; t.RehashName(); t.AddType(CardType::Creature);
    t.m_power = 1; t.m_toughness = 1; t.m_number = s.next_opp_spawn_number++;
    Permanent p; p.card = t; p.controller_index = 1; p.owner_index = 1; s.battlefield.push_back(p);
}
// The board both worlds start from: our nine Mountains (Worldfire's {6}{R}{R}{R}), a Lotus Bloom in play,
// a Sandstone Needle carrying its depletion counters, a creature, one of OUR tokens; the opponent's land
// and a scheduled spawn; cards in every hand and graveyard; a Rift Bolt suspended this turn.
GameState WFBase()
{
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    for (int i = 0; i < 9; ++i) { WFPut(s, "Mountain", 10 + i); }
    WFPut(s, "Lotus Bloom", 20);
    WFPut(s, "Sandstone Needle", 21);
    s.battlefield.back().counters.Add(Counter::Type::Depletion, 2);
    WFPut(s, "Keldon Halberdier", 22);
    {
        Permanent tok; tok.card = WFCard("Keldon Halberdier", s.next_token_number++);
        tok.is_token = true; tok.controller_index = 0; tok.owner_index = 0; s.battlefield.push_back(tok);
    }
    WFPut(s, "Island", 100001, 1);
    WFSpawn(s);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Ponder", 31), WFCard("Rift Bolt", 32)};
    s.players[0].graveyard = {WFCard("Brainstorm", 33)};
    s.players[1].hand = {WFCard("Mountain", 100002), WFCard("Lightning Bolt", 100003)};
    s.players[1].graveyard = {WFCard("Island", 100004)};
    s.players[0].suspended_cards.push_back({WFCard("Rift Bolt", 34), s.TurnSeq() + 1});
    for (int i = 0; i < 5; ++i) { s.players[0].library.push_back(WFCard("Island", 40 + i)); }
    return s;
}
std::vector<std::pair<std::string, int>> WFZone(const std::vector<Card>& z)
{
    std::vector<std::pair<std::string, int>> out;
    for (const Card& c : z) { out.emplace_back(c.m_name.str(), c.m_number); }
    std::sort(out.begin(), out.end());
    return out;
}
bool WFInExile(const GameState& s, int number)
{
    return std::any_of(s.exile.begin(), s.exile.end(), [&](const Card& c) { return c.m_number == number; });
}
}

TEST_CASE("worldfire: executor and rollout reach the same post-Worldfire state")
{
    const GameState base = WFBase();

    GameState real = base;
    AIEngine ai;
    ManaPool available = AvailableManaPool(real, nullptr);
    MtgTestSeam::CastSpellFromHand(ai, real, real.players[0].hand.front(), available, 0);
    REQUIRE(real.stack.size() == 1);
    StackEntry entry = real.stack.back(); real.stack.pop_back();
    EffectHandler::Resolve(real, entry, WFDef("Worldfire"));

    GameState roll = base;
    TurnSolver::Plan plan; Action cast; cast.kind = Action::Kind::CastFromHand;
    cast.card_name = "Worldfire"; cast.hand_index = 0;
    plan.actions.push_back(cast);
    TurnSolver::ApplyPlan(roll, plan, true);

    for (const GameState* g : {&real, &roll})
    {
        const GameState& s = *g;
        CHECK(s.battlefield.empty());
        CHECK(s.players[0].hand.empty());
        CHECK(s.players[1].hand.empty());
        // Worldfire is on the stack while it resolves, so it lands in the graveyard AFTER the exile.
        REQUIRE(s.players[0].graveyard.size() == 1);
        CHECK(s.players[0].graveyard[0].m_name.str() == "Worldfire");
        CHECK(s.players[1].graveyard.empty());
        CHECK(s.players[0].life == 1);
        CHECK(s.players[1].life == 1);
        CHECK(s.opponent_lost_life_this_turn);
        // Every nontoken card that was on the battlefield, in a hand or in a graveyard is in exile ...
        for (int n : {10, 18, 20, 21, 22, 31, 32, 33, 100001, 100002, 100003, 100004}) { CHECK(WFInExile(s, n)); }
        // ... the tokens (ours and the spawn) ceased to exist, and the exiled Needle is its printed card.
        CHECK(s.exile.size() == 9 + 3 + 1 + 2 + 2 + 2);   // battlefield 13, hands 4, graveyards 2
        // The suspended Rift Bolt is untouched: still exiled with its counter, arriving next turn.
        REQUIRE(s.players[0].suspended_cards.size() == 1);
        CHECK(s.players[0].suspended_cards[0].arrive_turn == base.TurnSeq() + 1);
        CHECK(s.players[0].library.size() == 5);
    }
    CHECK(WFZone(real.exile) == WFZone(roll.exile));
    CHECK(WFZone(real.players[0].graveyard) == WFZone(roll.players[0].graveyard));
    CHECK(real.floating_mana.Total() == roll.floating_mana.Total());
}

TEST_CASE("worldfire: a Rift Bolt suspended this turn kills at the next upkeep")
{
    GameState s; s.active_player_index = 0; s.turn_number = 4;
    for (int i = 0; i < 10; ++i) { WFPut(s, "Mountain", 10 + i); }
    s.players[0].hand = {WFCard("Rift Bolt", 30), WFCard("Worldfire", 31)};
    for (int i = 0; i < 5; ++i) { s.players[0].library.push_back(WFCard("Island", 40 + i)); }
    TurnSolver::Plan plan;
    Action sus; sus.kind = Action::Kind::Suspend; sus.card_name = "Rift Bolt"; sus.hand_index = 0;
    sus.cost = WFDef("Rift Bolt").params.suspend_cost.value();
    Action cast; cast.kind = Action::Kind::CastFromHand; cast.card_name = "Worldfire"; cast.hand_index = 1;
    plan.actions = {sus, cast};
    TurnSolver::ApplyPlan(s, plan, true);
    CHECK(s.battlefield.empty());
    CHECK(s.players[1].life == 1);
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK_FALSE(OpponentHasLost(s));
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(s.players[1].life <= 0);
    CHECK(OpponentHasLost(s));
    CHECK(s.players[0].suspended_cards.empty());
}

TEST_CASE("worldfire: exile, staged cards and floating mana are untouched")
{
    GameState s = WFBase();
    s.floating_mana.red = 3; s.floating_mana.blue = 1;
    Card staged = WFCard("Ponder", 50); staged.m_is_staged = true; staged.m_staged_expiry = s.TurnSeq();
    s.players[0].hand.push_back(staged);
    s.players[0].staged_cards.push_back({WFCard("Brainstorm", 51), s.TurnSeq()});
    s.exile.push_back(WFCard("Lightning Bolt", 52));
    ResolveExileAllAndSetLife(s, WFDef("Worldfire").params);
    CHECK(s.floating_mana.red == 3);
    CHECK(s.floating_mana.blue == 1);
    REQUIRE(s.players[0].hand.size() == 1);            // the rollout's merged impulse card is IN exile already
    CHECK(s.players[0].hand[0].m_number == 50);
    CHECK(s.players[0].staged_cards.size() == 1);
    CHECK(WFInExile(s, 52));
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards[0].card.m_name.str() == "Rift Bolt");
    // 2HG: the second head's hand is exiled too.
    GameState h = WFBase();
    h.opponent2_hand = {WFCard("Mountain", 110001), WFCard("Ponder", 110002)};
    ResolveExileAllAndSetLife(h, WFDef("Worldfire").params);
    CHECK(h.opponent2_hand.empty());
    CHECK(WFInExile(h, 110001));
    CHECK(WFInExile(h, 110002));
}

TEST_CASE("worldfire: the deck arms own death, so a 1-life payment at 1 loses in the rollout")
{
    Decklist with;
    with.mainboard.push_back(WFDef("Worldfire").card);
    with.mainboard.push_back(WFDef("Mountain").card);
    GameState a;
    GoldFishRunner::StampDeckTraits(a, with);
    CHECK(a.own_death_live);
    Decklist without;
    without.mainboard.push_back(WFDef("Rift Bolt").card);
    without.mainboard.push_back(WFDef("Mountain").card);
    GameState b;
    GoldFishRunner::StampDeckTraits(b, without);
    CHECK_FALSE(b.own_death_live);
    // At 1 life with own death armed, a win the same instant we die is not a win (OpponentHasLost).
    a.players[0].life = 0; a.players[1].life = 0;
    CHECK_FALSE(OpponentHasLost(a));
}

TEST_CASE("worldfire: Keldon Halberdier arrives off suspend with haste and attacks that turn")
{
    GameState s; s.active_player_index = 0; s.turn_number = 6;
    s.players[0].suspended_cards.push_back({WFCard("Keldon Halberdier", 60), s.TurnSeq()});
    ProcessSuspendArrivals(s, 0);
    REQUIRE(s.battlefield.size() == 1);
    const Permanent& h = s.battlefield[0];
    CHECK(h.entered_this_turn);
    CHECK(h.card.HasKeyword(Keyword::Haste));
    CHECK(h.card.HasKeyword(Keyword::FirstStrike));
    CHECK(h.CanAttack());
    CHECK(s.spells_cast_this_turn == 1);
    // A HARD-CAST Halberdier has no haste (the grant is the suspend cast's).
    CHECK_FALSE(WFDef("Keldon Halberdier").card.HasKeyword(Keyword::Haste));

    // The rollout sees the hasty attack: with the opponent at 4, a Halberdier arriving at the next upkeep wins
    // THAT turn (without haste it would be the turn after).
    GameState r; r.active_player_index = 0; r.turn_number = 3; r.players[1].life = 4;
    r.players[0].suspended_cards.push_back({WFCard("Keldon Halberdier", 61), r.TurnSeq() + 1});
    for (int i = 0; i < 8; ++i) { r.players[0].library.push_back(WFCard("Island", 70 + i)); }
    const int win = TurnSolver::SimulateToEndForTest(r, /*depth=*/1, /*max_turns=*/8, /*cutoff=*/99,
                                                     /*second_main=*/false);
    CHECK(win == 4);
}

TEST_CASE("worldfire: suspending Halberdier is not a cast, so it stays legal after Irencrag Feat")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    WFPut(s, "Mountain", 10);
    s.players[0].hand = {WFCard("Keldon Halberdier", 20)};
    s.casts_remaining_this_turn = 0;   // Irencrag Feat's "only one more spell" already spent
    ApplySuspend(s, 0, "Keldon Halberdier");
    CHECK(s.players[0].hand.empty());
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards[0].arrive_turn == s.TurnSeq() + 4);
    CHECK(s.spells_cast_this_turn == 0);
}

// ---- WorldfireProvider prunes (USER 2026-11-21) ------------------------------------------------------
// The tutors fetch ONLY missing combo pieces; Rift Bolt is held for the combo. See the rulings quoted at
// WorldfireProvider::TutorCandidates / OffersSuspend (DecisionProviders.cpp).
namespace {
GameState WFTutorBase(int mountains)
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    for (int i = 0; i < mountains; ++i) { WFPut(s, "Mountain", 10 + i); }
    int id = 200;
    for (const char* nm : {"Ponder", "Worldfire", "Brainstorm", "Rift Bolt", "Seething Song", "Irencrag Feat",
                           "Preordain", "Memory Lapse"})
    { s.players[0].library.push_back(WFCard(nm, id++)); }
    s.players[1].life = 20;
    return s;
}
std::vector<std::string> WFSorted(std::vector<std::string> v)
{ std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); return v; }
using VS = std::vector<std::string>;
}

TEST_CASE("worldfire provider: the tutors fetch only missing combo pieces")
{
    WorldfireProvider prov;
    const CardParams& mystical = WFDef("Mystical Tutor").params;
    const CardParams& personal = WFDef("Personal Tutor").params;

    SUBCASE("nothing in hand: every piece; Personal Tutor never offers the instant Seething Song")
    {
        GameState s = WFTutorBase(3);
        CHECK(WFSorted(prov.TutorCandidates(s, 0, mystical))
              == VS{"Irencrag Feat", "Rift Bolt", "Seething Song", "Worldfire"});
        CHECK(WFSorted(prov.TutorCandidates(s, 0, personal)) == VS{"Irencrag Feat", "Rift Bolt", "Worldfire"});
    }
    SUBCASE("no duplicate Worldfire / Rift Bolt / Irencrag; acceleration while the mana is short")
    {
        GameState s = WFTutorBase(3);
        s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31)};
        CHECK(WFSorted(prov.TutorCandidates(s, 0, mystical)) == VS{"Irencrag Feat", "Seething Song"});
        s.players[0].hand.push_back(WFCard("Irencrag Feat", 32));   // 3 lands + Feat's net 3 = 6 < 9
        CHECK(WFSorted(prov.TutorCandidates(s, 0, mystical)) == VS{"Seething Song"});
    }
    SUBCASE("everything present: the tutor is not cast")
    {
        GameState s = WFTutorBase(9);
        s.players[0].hand = {WFCard("Worldfire", 30)};
        s.players[0].suspended_cards.push_back({WFCard("Rift Bolt", 31), s.TurnSeq() + 1});
        CHECK(prov.TutorCandidates(s, 0, mystical).empty());
        CHECK(prov.TutorNeedsCandidate(s, 0, mystical));
    }
    SUBCASE("a pending Halberdier makes Rift Bolt low priority, not absent")
    {
        GameState s = WFTutorBase(3);
        s.players[0].suspended_cards.push_back({WFCard("Keldon Halberdier", 31), s.TurnSeq() + 3});
        CHECK(WFSorted(prov.TutorCandidates(s, 0, mystical))
              == VS{"Irencrag Feat", "Seething Song", "Worldfire"});
        GameState t = WFTutorBase(9);
        t.players[0].hand = {WFCard("Worldfire", 30)};
        t.players[0].suspended_cards.push_back({WFCard("Keldon Halberdier", 31), t.TurnSeq() + 3});
        CHECK(WFSorted(prov.TutorCandidates(t, 0, mystical)) == VS{"Rift Bolt"});
    }
}

TEST_CASE("worldfire provider: Rift Bolt is held until the combo; Halberdier is never held")
{
    WorldfireProvider prov;
    const CardDefinition& bolt = WFDef("Rift Bolt");
    const CardDefinition& halb = WFDef("Keldon Halberdier");
    GameState s = WFTutorBase(3);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31)};
    CHECK_FALSE(prov.OffersSuspend(s, 0, bolt));     // 3 mana: not a go-off turn
    CHECK_FALSE(prov.OffersCastNow(s, 0, bolt));     // 3 damage into 20 life
    CHECK(prov.OffersSuspend(s, 0, halb));
    CHECK(prov.OffersCastNow(s, 0, halb));

    GameState g = WFTutorBase(9);                     // 9 lands + a land drop = Worldfire + {R}
    g.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Mountain", 32)};
    CHECK(prov.OffersSuspend(g, 0, bolt));
    g.players[0].lands_played_this_turn = 1;          // drop spent: 9 < 10
    CHECK_FALSE(prov.OffersSuspend(g, 0, bolt));
    g.players[0].hand = {WFCard("Rift Bolt", 31)};    // no Worldfire in hand
    g.players[0].lands_played_this_turn = 0;
    CHECK_FALSE(prov.OffersSuspend(g, 0, bolt));

    GameState after = WFTutorBase(1);                 // post-Worldfire: the opponent is at 1
    after.players[1].life = 1;
    after.players[0].hand = {WFCard("Rift Bolt", 31)};
    CHECK(prov.OffersSuspend(after, 0, bolt));
    CHECK(prov.OffersCastNow(after, 0, bolt));
}

// ---- THE BUCKETED KEEP ORDER (docs/design/worldfire-discard-buckets.md; every consumer DEFAULT OFF) ----
namespace {
struct WFArm
{
    explicit WFArm(std::initializer_list<std::pair<heurarm::Slot, int>> v)
    { for (const auto& [slot, on] : v) { heurarm::t_arm[slot] = static_cast<std::int8_t>(on); } }
    ~WFArm() { heurarm::Clear(); }
};
const WorldfireKeepRank::Entry& WFHandEntry(const WorldfireKeepRank& r, int hand_index)
{
    for (const WorldfireKeepRank::Entry& e : r.entries) { if (e.hand_index == hand_index) { return e; } }
    FAIL("no entry for hand index " << hand_index);
    return r.entries.front();
}
const WorldfireKeepRank::Entry& WFLookEntry(const WorldfireKeepRank& r, int look_index)
{
    for (const WorldfireKeepRank::Entry& e : r.entries) { if (e.look_index == look_index) { return e; } }
    FAIL("no entry for look index " << look_index);
    return r.entries.front();
}
}

TEST_CASE("worldfire keep order: quota-first buckets, dead cards last, Worldfire never shed")
{
    GameState s = WFTutorBase(3);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Memory Lapse", 32),
                         WFCard("Worldfire", 33), WFCard("Island", 34), WFCard("Brainstorm", 35)};
    const WorldfireKeepRank r = WorldfireKeepRank::Of(s, 0, nullptr, false);
    CHECK(r.t_star == -1);                                   // 3 + 1 land: nowhere near 10 by now+4 -> far
    CHECK(r.combo_filled);
    CHECK(r.finisher_filled);
    CHECK(r.dig_quota == 1);                                 // both pieces held, mana far
    const auto& wf = WFHandEntry(r, 0);
    const auto& bolt = WFHandEntry(r, 1);
    const auto& lapse = WFHandEntry(r, 2);
    const auto& wf2 = WFHandEntry(r, 3);
    const auto& island = WFHandEntry(r, 4);
    const auto& bs = WFHandEntry(r, 5);
    CHECK(wf.quota);   CHECK(std::string(wf.bucket) == "combo");
    CHECK(bolt.quota); CHECK(std::string(bolt.bucket) == "finisher");
    CHECK(island.quota);                                     // far: every mana card is quota
    CHECK(bs.quota);   CHECK(std::string(bs.bucket) == "dig");
    CHECK_FALSE(lapse.quota); CHECK_FALSE(wf2.quota);
    CHECK(lapse.key < wf2.key);                              // Memory Lapse sheds first, then the duplicate
    for (const auto& e : r.entries) { if (e.hand_index != 0) { CHECK(e.key < wf.key); } }
    CHECK(wf.key > bolt.key);
    CHECK(bolt.key > island.key);
    CHECK(island.key > bs.key);                              // nothing missing: dig ranks below the mana
}

TEST_CASE("worldfire keep order: the cleanup discard sheds the lowest card, re-ranked per shed (lever-gated)")
{
    WorldfireProvider prov;
    GameState s = WFTutorBase(5);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Rift Bolt", 32),
                         WFCard("Mountain", 33), WFCard("Mountain", 34), WFCard("Mountain", 35),
                         WFCard("Mountain", 36), WFCard("Memory Lapse", 37)};
    {
        // Lever OFF: the generic rule (highest mana value) -- it would shed Worldfire.
        const std::vector<int> off = prov.CleanupDiscardCandidates(s, nullptr);
        REQUIRE_FALSE(off.empty());
        CHECK(off.front() == 0);
        CHECK(prov.CleanupDiscardShedStable());
    }
    WFArm arm{{heurarm::WORLDFIRE_BUCKET_DISCARD, 1}};
    const std::vector<int> c = prov.CleanupDiscardCandidates(s, nullptr);
    REQUIRE(c.size() == 1);
    CHECK(c.front() == 7);                                   // Memory Lapse
    CHECK_FALSE(prov.CleanupDiscardShedStable());
    s.players[0].hand.erase(s.players[0].hand.begin() + 7);
    const std::vector<int> c2 = prov.CleanupDiscardCandidates(s, nullptr);
    REQUIRE(c2.size() == 1);
    CHECK(c2.front() == 2);                                  // the same-type second Rift Bolt (the later copy)
    const std::vector<int> order = prov.CleanupDiscardShedOrder(s, nullptr);
    REQUIRE_FALSE(order.empty());
    CHECK(order.back() == 0);                                // Worldfire is the last card shed
}

TEST_CASE("worldfire keep order: Brainstorm puts back the two lowest, the higher of them on top (lever-gated)")
{
    WorldfireProvider prov;
    GameState s = WFTutorBase(8);
    // Post-draw hand. 8 Mountains + this turn's drop + next turn's: T* = now+1 with the Rift Bolt's {R}.
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Memory Lapse", 32),
                         WFCard("Island", 33), WFCard("Mountain", 34), WFCard("Ponder", 35)};
    CHECK_FALSE(prov.PutBackSingleCandidate(s));
    CHECK_FALSE(prov.PutBackRankingPerCard(s));
    WFArm arm{{heurarm::WORLDFIRE_BRAINSTORM_RULE, 1}};
    const WorldfireKeepRank r = WorldfireKeepRank::Of(s, 0, nullptr, false);
    CHECK(r.t_star == 1);
    CHECK(r.dig_quota == 0);                                 // pieces held and the mana covered: no dig needed
    const std::vector<int> pb = prov.PutBackRanking(s, 0);
    REQUIRE(pb.size() >= 2);
    CHECK(pb[0] == 2);                                       // Memory Lapse goes deepest
    CHECK(pb[1] == 5);                                       // Ponder (dig beyond quota) on top
    CHECK(prov.PutBackRankingPerCard(s));
    CHECK(prov.PutBackSingleCandidate(s));
    WFArm order_only{{heurarm::WORLDFIRE_BRAINSTORM_RULE, 1}, {heurarm::WORLDFIRE_BRAINSTORM_SINGLE, 0}};
    CHECK_FALSE(prov.PutBackSingleCandidate(s));
}

TEST_CASE("worldfire keep order: Ponder keeps a set that fills a missing quota, else shuffles (lever-gated)")
{
    WorldfireProvider prov;
    GameState s = WFTutorBase(5);
    s.players[0].hand = {WFCard("Rift Bolt", 30), WFCard("Mountain", 31)};
    const std::vector<Card> look = {WFCard("Island", 40), WFCard("Memory Lapse", 41), WFCard("Worldfire", 42)};
    CHECK_FALSE(prov.ReorderSingleCandidate(s));
    WFArm arm{{heurarm::WORLDFIRE_PONDER_RULE, 1}};
    CHECK(prov.ReorderSingleCandidate(s));
    CHECK_FALSE(prov.ReorderKeepShuffleOnly(s));
    CHECK(prov.KeepReorderTop(s, look));
    const TopDisposition keep = prov.TopDispositionPick(s, look, LookKind::Reorder, -1);
    CHECK_FALSE(keep.shuffle);
    REQUIRE(keep.top_order.size() == 3);
    CHECK(keep.top_order.front() == 2);                      // Worldfire, the missing piece, drawn now
    CHECK(keep.top_order.back() == 1);                       // Memory Lapse at the bottom of the three

    GameState t = WFTutorBase(5);
    t.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Mountain", 32)};
    const std::vector<Card> junk = {WFCard("Memory Lapse", 40), WFCard("Worldfire", 41), WFCard("Rift Bolt", 42)};
    CHECK_FALSE(prov.KeepReorderTop(t, junk));
    CHECK(prov.TopDispositionPick(t, junk, LookKind::Reorder, -1).shuffle);
    CHECK_FALSE(prov.TopDispositionPick(t, junk, LookKind::Reorder, 1).shuffle);   // a forced keep is honoured

    WFArm ks{{heurarm::WORLDFIRE_PONDER_RULE, 1}, {heurarm::WORLDFIRE_PONDER_SINGLE, 0}};
    CHECK_FALSE(prov.ReorderSingleCandidate(s));
    CHECK(prov.ReorderKeepShuffleOnly(s));                   // keep-in-the-rule's-order vs shuffle is searched
}

TEST_CASE("worldfire keep order: Preordain keeps the quota-fillers best first, bottoms the rest (lever-gated)")
{
    WorldfireProvider prov;
    GameState s = WFTutorBase(4);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Mountain", 32)};
    const std::vector<Card> look = {WFCard("Memory Lapse", 40), WFCard("Seething Song", 41)};
    WFArm arm{{heurarm::WORLDFIRE_PREORDAIN_RULE, 1}};
    const TopDisposition d = prov.TopDispositionPick(s, look, LookKind::Scry, -1);
    REQUIRE(d.top_order.size() == 1);
    CHECK(d.top_order.front() == 1);                         // the Song (short of mana: quota); Memory Lapse bottomed

    const std::vector<Card> look2 = {WFCard("Worldfire", 40), WFCard("Rift Bolt", 41)};
    CHECK(prov.TopDispositionPick(s, look2, LookKind::Scry, -1).top_order.empty());   // both duplicates
}

TEST_CASE("worldfire keep order: Lotus Bloom only where it can pull the go-off forward")
{
    GameState s = WFTutorBase(8);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Lotus Bloom", 32),
                         WFCard("Mountain", 33)};
    const WorldfireKeepRank far = WorldfireKeepRank::Of(s, 0, nullptr, false);
    CHECK(WFHandEntry(far, 2).quota);                        // 9 of 10 without it: the Bloom is the go-off mana
    CHECK(far.t_star == 3);                                  // ... arriving on now+3

    GameState n = WFTutorBase(9);
    n.players[0].hand = s.players[0].hand;
    const WorldfireKeepRank near = WorldfireKeepRank::Of(n, 0, nullptr, false);
    CHECK(near.t_star == 0);                                 // 9 + the land drop = Worldfire + {R} this turn
    const auto& bloom = WFHandEntry(near, 2);
    CHECK_FALSE(bloom.quota);
    CHECK(std::string(bloom.bucket) == "bloom-near");
    for (const auto& e : near.entries)
    { if (e.hand_index != 2) { CHECK(e.key > bloom.key); } }     // the first card shed (nothing DEAD here)
}

TEST_CASE("worldfire keep order: a tutor fills a missing piece; dig is prioritised only for a genuine gap")
{
    GameState s = WFTutorBase(3);
    s.players[0].hand = {WFCard("Mystical Tutor", 30), WFCard("Rift Bolt", 31), WFCard("Ponder", 32),
                         WFCard("Island", 33)};
    const WorldfireKeepRank r = WorldfireKeepRank::Of(s, 0, nullptr, false);
    CHECK(r.combo_filled);
    CHECK(std::string(WFHandEntry(r, 0).bucket) == "combo");
    CHECK(r.dig_quota == 2);                                 // a tutored piece still counts for the dig quota
    CHECK(WFHandEntry(r, 2).quota);
    CHECK(WFHandEntry(r, 2).key < WFHandEntry(r, 3).key);    // ... but that dig ranks below the go-off mana

    GameState g = WFTutorBase(3);
    g.players[0].hand = {WFCard("Rift Bolt", 31), WFCard("Ponder", 32), WFCard("Island", 33)};
    const WorldfireKeepRank q = WorldfireKeepRank::Of(g, 0, nullptr, false);
    CHECK_FALSE(q.combo_filled);
    CHECK(WFHandEntry(q, 1).key > WFHandEntry(q, 2).key);    // genuinely missing: dig slot 1 above every mana card
    CHECK(WFHandEntry(q, 1).key < WFHandEntry(q, 0).key);    // ... and below the finisher
}

TEST_CASE("worldfire keep order: a pending Halberdier pins T* to its arrival - 1; the Rift Bolt is BACKUP")
{
    GameState s = WFTutorBase(7);
    s.players[0].hand = {WFCard("Worldfire", 30), WFCard("Rift Bolt", 31), WFCard("Mountain", 32),
                         WFCard("Mountain", 33)};
    s.players[0].suspended_cards.push_back({WFCard("Keldon Halberdier", 34), s.TurnSeq() + 2});
    const WorldfireKeepRank r = WorldfireKeepRank::Of(s, 0, nullptr, false);
    CHECK(r.t_star == 1);                                    // 7 + two drops = 9, no {R} for a suspend
    CHECK(r.finisher_filled);
    const auto& bolt = WFHandEntry(r, 1);
    CHECK_FALSE(bolt.quota);
    CHECK(std::string(bolt.bucket) == "backup");             // the other-type finisher: kept above dead / surplus
    for (const auto& e : r.entries)
    { if (!e.quota && e.hand_index != 1) { CHECK(e.key < bolt.key); } }
}
