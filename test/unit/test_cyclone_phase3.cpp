// Unpredictable Cyclone onboarding, PHASE 3 (2026-10-27): the searched cycle, Unpredictable Cyclone,
// Curator of Mysteries, the Remand rulings.
//
//   * Card data (Scryfall-verified): Unpredictable Cyclone {3}{R}{R}, Curator of Mysteries {2}{U}{U} 4/4.
//   * THE WALK (SpellEffects.h): stops at the first card sharing ANY card type with the cycled card; a land
//     hit is never cast; a whole-library whiff is a random-order put-back with NO draw; a cycled LAND is not
//     replaced; a Cyclone cycled while one is out walks to the next enchantment.
//   * LOCKSTEP: the same cycle through the executor (AIEngine::PerformDigFull + the stack) and the rollout's
//     searched cycle chain (TurnSolver::ApplyPlan with a cycle seed) reaches the same state -- library ORDER
//     included (the random bottom is one shared keyed helper), the free cast's cast trigger (Emrakul's extra
//     turn), the free-cast choices (decline, take-and-Remand).
//   * Curator: ONE trigger per cycle per Curator (the cycle's discard hook stands down), the searched
//     disposition j bottoms the top j, a non-cycle discard triggers it too, and the scry resolves BEFORE the
//     walk -- the deck's core line (bottom a Wraith on top so the walk reaches Emrakul).
//   * The searched cycle action: one seed per DISTINCT cycler with its j / free-cast variants, never two
//     seeds in one plan, never a seed and a cast of the same single copy.
//   * The Remand restrictions (CycloneProvider): no Remand on a free-cast Ulamog; the hand-cast MV ceiling.
//   * Hazard R1: the reactive dig chain does not stop at a replaced (draw-less) cycle.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "ai/ValueArm.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr const char* kCyclone = "Unpredictable Cyclone";
constexpr const char* kCurator = "Curator of Mysteries";
constexpr const char* kWraith  = "Street Wraith";
constexpr const char* kEmrakul = "Emrakul, the Aeons Torn";
constexpr const char* kHunger  = "Ulamog, the Ceaseless Hunger";
constexpr const char* kTestArtifactCreature = "Cyclone Phase3 Test Artifact Creature";
constexpr const char* kTestArtifact         = "Cyclone Phase3 Test Artifact";
constexpr const char* kTestArtifactLand     = "Cyclone Phase3 Test Artifact Land";

void EnsureCards3()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        // Synthetic multi-type cards for the shared-type rule (no card in the pool is an artifact creature).
        const CardDefinition* wraith = CardDatabase::Instance().Lookup(kWraith);
        const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
        if (wraith != nullptr && island != nullptr)
        {
            const CardDefinition wc = *wraith;
            const CardDefinition ic = *island;
            CardDatabase::Instance().Register(kTestArtifactCreature, [wc]()
            {
                CardDefinition d = wc;
                d.card.m_name = InternedName(std::string(kTestArtifactCreature));
                d.card.AddType(CardType::Artifact);
                return d;
            });
            CardDatabase::Instance().Register(kTestArtifact, [ic]()
            {
                CardDefinition d = ic;
                d.card.m_name = InternedName(std::string(kTestArtifact));
                d.card.RemoveType(CardType::Land);
                d.card.AddType(CardType::Artifact);
                d.tmpl = CardTemplate::None;
                d.params = CardParams{};
                return d;
            });
            CardDatabase::Instance().Register(kTestArtifactLand, [ic]()
            {
                CardDefinition d = ic;
                d.card.m_name = InternedName(std::string(kTestArtifactLand));
                d.card.AddType(CardType::Artifact);
                return d;
            });
        }
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def3(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card3(const std::string& name, int number)
{
    Card c     = Def3(name).card;
    c.m_number = number;
    return c;
}

Permanent& Put3(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = Card3(name, number);
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void PutSpawn3(GameState& s)
{
    Card token;
    token.m_name = "2/2 Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power     = 2;
    token.m_toughness = 2;
    token.m_number    = s.next_opp_spawn_number++;
    Permanent p;
    p.card             = token;
    p.controller_index = 1;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
}

const CycloneProvider& Prov3() { static const CycloneProvider cp; return cp; }

// Round 3, the land drop made, `lands` untapped Islands, `top` on top of the library over Islands, the
// stamps a Cyclone deck carries.
GameState Base3(int lands, const std::vector<std::string>& top, int filler = 20)
{
    GameState s;
    s.m_provider          = &Prov3();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_extra_turn = true;
    s.deck_has_gy_shuffle = true;
    s.deck_has_annihilator = true;
    s.deck_has_cast_target_trigger = true;
    s.deck_has_cycle_draw_replacement = true;
    s.deck_has_discard_scry_watcher = true;
    for (int k = 0; k < lands; ++k) { Put3(s, "Island", 40 + k); }
    int num = 500;
    for (const std::string& n : top) { s.players[0].library.push_back(Card3(n, num++)); }
    for (int k = 0; k < filler; ++k) { s.players[0].library.push_back(Card3("Island", 900 + k)); }
    return s;
}

int Count3(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

bool OnBf3(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

std::vector<int> LibNums(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].library) { v.push_back(c.m_number); }
    return v;
}

std::vector<int> ZoneNums(const std::vector<Card>& z)
{
    std::vector<int> v;
    for (const Card& c : z) { v.push_back(c.m_number); }
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<int> BfNums(const GameState& s)
{
    std::vector<int> v;
    for (const Permanent& p : s.battlefield) { v.push_back(p.card.m_number); }
    std::sort(v.begin(), v.end());
    return v;
}

void ResolveAll3(GameState& s)
{
    while (!s.stack.empty())
    {
        StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        REQUIRE(d != nullptr);
        EffectHandler::Resolve(s, e, *d);
    }
}

// A plan whose only action is the cycle seed of `name` with the two pins (the shape CollectActions emits).
TurnSolver::Plan SeedPlan(const std::string& name, int scry_j, int fc)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    Action a;
    a.kind          = Action::Kind::DigDraw;
    a.dig_sacrifice = false;
    a.card_name     = name;
    a.def           = &Def3(name);
    a.hand_index    = -1;
    a.devour_count  = scry_j;
    a.ponder_keep   = fc;
    p.actions.push_back(a);
    return p;
}

// The executor: the cycle, then the stack resolved (as replay_recorded's resolve_now does).
AIEngine::DigOutcome ExecCycle(GameState& s, const std::string& name, int scry_j, int fc)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    const AIEngine::DigOutcome o =
        MtgTestSeam::PerformDigFull(eng, s, name, false, scry_j, fc, [](GameState& g) { ResolveAll3(g); });
    ResolveAll3(s);
    return o;
}

void RolloutCycle(GameState& s, const std::string& name, int scry_j, int fc)
{
    TurnSolver::ApplyPlan(s, SeedPlan(name, scry_j, fc), /*is_pre_combat=*/true);
}

void CheckSameState(const GameState& a, const GameState& b)
{
    CHECK(LibNums(a) == LibNums(b));
    CHECK(ZoneNums(a.players[0].hand) == ZoneNums(b.players[0].hand));
    CHECK(ZoneNums(a.players[0].graveyard) == ZoneNums(b.players[0].graveyard));
    CHECK(BfNums(a) == BfNums(b));
    CHECK(a.players[0].life == b.players[0].life);
    CHECK(a.search_count == b.search_count);
    CHECK(a.spells_cast_this_turn == b.spells_cast_this_turn);
    CHECK(a.pending_extra_turns == b.pending_extra_turns);
    CHECK(a.players[0].cards_drawn_this_turn == b.players[0].cards_drawn_this_turn);
}

}   // namespace

TEST_CASE("Cyclone phase 3: card data for Unpredictable Cyclone and Curator of Mysteries")
{
    EnsureCards3();
    const CardDefinition& c = Def3(kCyclone);
    CHECK(c.card.m_mana_cost.ManaValue() == 5);
    CHECK(c.card.IsEnchantment());
    CHECK(c.params.cycle_draw_exile_shared_type_freecast);
    REQUIRE(c.params.cycling_cost.has_value());
    CHECK(c.params.cycling_cost->ManaValue() == 2);
    const CardDefinition& u = Def3(kCurator);
    CHECK(u.card.m_mana_cost.ManaValue() == 4);
    CHECK(u.card.m_mana_cost.blue == 2);
    CHECK(u.card.m_power == 4);
    CHECK(u.card.m_toughness == 4);
    CHECK(u.card.HasKeyword(Keyword::Flying));
    CHECK(u.params.cycle_or_discard_trigger_scry == 1);
    REQUIRE(u.params.cycling_cost.has_value());
    CHECK(u.params.cycling_cost->blue == 1);
}

TEST_CASE("Cyclone phase 3: the deck now routes to CycloneProvider")
{
    EnsureCards3();
    Decklist deck;
    int num = 1;
    for (const char* n : { kCyclone, kCurator, kWraith, kEmrakul, "Scalding Tarn", "Brainstorm" })
    { for (int k = 0; k < 4; ++k) { deck.mainboard.push_back(Card3(n, num++)); } }
    CHECK(std::string(DetectDecisionProvider(deck).Name()) == "Cyclone");
}

TEST_CASE("Cyclone phase 3: the walk -- shared types, land hits, whiffs, lands and Cyclone cycled")
{
    EnsureCards3();
    {   // A creature cycled walks past an instant and a land to the first creature.
        GameState s = Base3(0, { "Island", "Brainstorm", kEmrakul, "Island" });
        CycleWalk w;
        WalkExileUntilSharedType(s, 0, CycleWalkTypeMask(Def3(kWraith).card), w);
        REQUIRE(w.have);
        CHECK(w.hit.m_name.str() == kEmrakul);
        CHECK(w.rest.size() == 2);
    }
    {   // An ARTIFACT creature stops at the first artifact OR creature -- here the artifact.
        GameState s = Base3(0, { "Island", kTestArtifact, kEmrakul });
        CycleWalk w;
        WalkExileUntilSharedType(s, 0, CycleWalkTypeMask(Def3(kTestArtifactCreature).card), w);
        REQUIRE(w.have);
        CHECK(w.hit.m_name.str() == kTestArtifact);
    }
    {   // An artifact LAND shares the type but is never cast: it goes to the bottom with the rest.
        GameState s = Base3(0, { kTestArtifactLand, "Island" });
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3(kTestArtifactCreature, 101));
        CHECK(ExecCycle(s, kTestArtifactCreature, -1, kCycFcDefault) == AIEngine::DigOutcome::Replaced);
        CHECK_FALSE(OnBf3(s, kTestArtifactLand));
        CHECK(Count3(s.players[0].hand, kTestArtifactLand) == 0);
        CHECK(s.players[0].library.size() == 22);   // nothing drawn, nothing lost
    }
    {   // A WHIFF (no creature left): the whole library is put back in a random order, NO draw.
        GameState s = Base3(0, {}, 12);
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        const std::vector<int> before = LibNums(s);
        const std::uint64_t sc = s.search_count;
        CHECK(ExecCycle(s, kWraith, -1, kCycFcDefault) == AIEngine::DigOutcome::Replaced);
        std::vector<int> after = LibNums(s);
        CHECK(after.size() == before.size());
        CHECK(after != before);                       // a real random order (12 cards)
        std::vector<int> a2 = after, b2 = before;
        std::sort(a2.begin(), a2.end()); std::sort(b2.begin(), b2.end());
        CHECK(a2 == b2);                              // the same multiset
        CHECK(s.players[0].cards_drawn_this_turn == 0);
        CHECK(s.search_count == sc + 1);
        CHECK(s.players[0].hand.empty());
    }
    {   // A cycled LAND is not replaced (Xander's Lounge draws).
        GameState s = Base3(3, { kEmrakul });
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3("Xander's Lounge", 101));
        CHECK(ExecCycle(s, "Xander's Lounge", -1, kCycFcDefault) == AIEngine::DigOutcome::DrewNonland);
        CHECK(Count3(s.players[0].hand, kEmrakul) == 1);
        CHECK_FALSE(OnBf3(s, kEmrakul));
    }
    {   // No Cyclone on the battlefield: Cyclone's own cycling draws.
        GameState s = Base3(2, { kEmrakul });
        s.players[0].hand.push_back(Card3(kCyclone, 101));
        CHECK(ExecCycle(s, kCyclone, -1, kCycFcDefault) == AIEngine::DigOutcome::DrewNonland);
        CHECK(Count3(s.players[0].hand, kEmrakul) == 1);
    }
    {   // A Cyclone cycled while one is out is "another" card: it walks to the next ENCHANTMENT.
        GameState s = Base3(2, { kEmrakul, kCyclone, "Island" });
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3(kCyclone, 101));
        CHECK(ExecCycle(s, kCyclone, -1, kCycFcDefault) == AIEngine::DigOutcome::Replaced);
        int cyclones = 0;
        for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == kCyclone) { ++cyclones; } }
        CHECK(cyclones == 2);                         // the walked-to Cyclone was cast free
        CHECK_FALSE(OnBf3(s, kEmrakul));              // Emrakul was walked past, to the bottom
    }
}

TEST_CASE("Cyclone phase 3: LOCKSTEP -- a Wraith cycle walks to Emrakul in the executor and the rollout")
{
    EnsureCards3();
    auto make = []
    {
        GameState s = Base3(0, { "Island", "Brainstorm", kEmrakul, "Island" });
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        return s;
    };
    GameState ex = make(), ro = make();
    CHECK(ExecCycle(ex, kWraith, -1, kCycFcDefault) == AIEngine::DigOutcome::Replaced);
    RolloutCycle(ro, kWraith, -1, kCycFcDefault);
    CHECK(OnBf3(ex, kEmrakul));
    CHECK(ex.pending_extra_turns == 1);               // a real cast: the extra-turn trigger resolved
    CHECK(ex.players[0].life == 18);                  // Street Wraith's 2 life
    CHECK(ex.players[0].cards_drawn_this_turn == 0);  // the draw was replaced
    CheckSameState(ex, ro);
}

TEST_CASE("Cyclone phase 3: the free cast -- decline (searched, dominance fold) and take-and-Remand")
{
    EnsureCards3();
    {   // Ulamog with NO opposing permanent: its trigger would exile two of OURS -> not dominated, the
        // decline pin declines (Ulamog to the bottom), in both worlds.
        auto make = []
        {
            GameState s = Base3(2, { kHunger, "Island" });
            Put3(s, kCyclone, 30);
            s.players[0].hand.push_back(Card3(kWraith, 101));
            return s;
        };
        GameState ex = make(), ro = make();
        ExecCycle(ex, kWraith, -1, kCycFcDecline);
        RolloutCycle(ro, kWraith, -1, kCycFcDecline);
        CHECK_FALSE(OnBf3(ex, kHunger));
        CHECK(OnBf3(ex, kCyclone));
        CheckSameState(ex, ro);
    }
    {   // With two opposing permanents taking Ulamog is DOMINANT: the decline pin takes.
        GameState s = Base3(2, { kHunger, "Island" });
        PutSpawn3(s);
        PutSpawn3(s);
        Put3(s, kCyclone, 30);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        CHECK(Prov3().FreeCastTakeDominated(s, 0, Def3(kHunger)));
        ExecCycle(s, kWraith, -1, kCycFcDecline);
        CHECK(OnBf3(s, kHunger));
    }
    {   // TAKE-AND-REMAND on a Curator hit: it goes back to hand, Remand draws; {1}{U} paid; both worlds.
        auto make = []
        {
            GameState s = Base3(2, { kCurator, "Island", "Island" });
            Put3(s, kCyclone, 30);
            s.players[0].hand.push_back(Card3(kWraith, 101));
            s.players[0].hand.push_back(Card3("Remand", 102));
            return s;
        };
        GameState ex = make(), ro = make();
        ExecCycle(ex, kWraith, -1, kCycFcTakeRemand);
        RolloutCycle(ro, kWraith, -1, kCycFcTakeRemand);
        CHECK_FALSE(OnBf3(ex, kCurator));
        CHECK(Count3(ex.players[0].hand, kCurator) == 1);
        CHECK(Count3(ex.players[0].graveyard, "Remand") == 1);
        CHECK(ex.players[0].cards_drawn_this_turn == 1);   // Remand's draw
        CHECK(ex.spells_cast_this_turn == 2);              // the free Curator + the Remand
        CheckSameState(ex, ro);
    }
}

TEST_CASE("Cyclone phase 3: CycloneProvider's Remand restrictions")
{
    EnsureCards3();
    GameState s = Base3(2, {});
    CHECK_FALSE(Prov3().AdmitRemandOnFreeCast(s, 0, Def3(kHunger)));   // a counterable targeted cast trigger
    CHECK(Prov3().AdmitRemandOnFreeCast(s, 0, Def3(kEmrakul)));         // can't be countered: a cantrip
    CHECK(Prov3().AdmitRemandOnFreeCast(s, 0, Def3(kWraith)));
    // The hand-cast ceiling (MTG_CYCLONE_REMAND_MAX_MV, default 1; per-job numeric arm).
    CHECK(Prov3().AdmitRemandRider(s, 0, Def3("Ponder")));              // MV 1
    CHECK_FALSE(Prov3().AdmitRemandRider(s, 0, Def3(kCurator)));        // MV 4
    valuearm::t_arm.cyclone_remand_max_mv = -1;
    CHECK_FALSE(Prov3().AdmitRemandRider(s, 0, Def3("Ponder")));
    valuearm::t_arm.cyclone_remand_max_mv = 4;
    CHECK(Prov3().AdmitRemandRider(s, 0, Def3(kCurator)));
    valuearm::t_arm.cyclone_remand_max_mv = valuearm::Arm::kCycloneRemandUnset;
}

TEST_CASE("Cyclone phase 3: Curator -- one trigger per cycle, the searched j, non-cycle discards")
{
    EnsureCards3();
    int asked = 0;
    TopChooser count_chooser = [&asked](const GameState&, const std::string&, const std::vector<Card>& looked,
                                        LookKind)
    {
        ++asked;
        TopDisposition d;
        for (int i = 0; i < static_cast<int>(looked.size()); ++i) { d.top_order.push_back(i); }   // keep
        return d;
    };
    {   // A cycle with ONE Curator out, unpinned: exactly ONE scry (the discard hook stood down).
        GameState s = Base3(0, { "Island" });
        Put3(s, kCurator, 31);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        asked = 0;
        g_play_top_chooser = &count_chooser;
        ExecCycle(s, kWraith, -1, kCycFcDefault);
        g_play_top_chooser = nullptr;
        CHECK(asked == 1);
    }
    {   // Pinned j = 1: the top card is bottomed (no chooser consulted), the next is drawn.
        GameState s = Base3(0, { "Brainstorm", "Ponder" });
        Put3(s, kCurator, 31);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        asked = 0;
        g_play_top_chooser = &count_chooser;
        ExecCycle(s, kWraith, 1, kCycFcDefault);
        g_play_top_chooser = nullptr;
        CHECK(asked == 0);
        CHECK(Count3(s.players[0].hand, "Ponder") == 1);
        const Library& lib = s.players[0].library;
        CHECK(lib[lib.size() - 1].m_name.str() == "Brainstorm");
    }
    {   // A Curator CYCLED from hand cannot trigger itself; a second one on the battlefield triggers.
        GameState s = Base3(1, { "Island" });
        s.players[0].hand.push_back(Card3(kCurator, 101));
        asked = 0;
        g_play_top_chooser = &count_chooser;
        ExecCycle(s, kCurator, -1, kCycFcDefault);
        CHECK(asked == 0);
        GameState t = Base3(1, { "Island" });
        Put3(t, kCurator, 31);
        t.players[0].hand.push_back(Card3(kCurator, 101));
        ExecCycle(t, kCurator, -1, kCycFcDefault);
        g_play_top_chooser = nullptr;
        CHECK(asked == 1);
    }
    {   // A NON-cycle discard (the cleanup discard's hook) triggers it too.
        GameState s = Base3(0, { "Island" });
        Put3(s, kCurator, 31);
        s.players[0].hand.push_back(Card3("Brainstorm", 101));
        asked = 0;
        g_play_top_chooser = &count_chooser;
        std::vector<MadnessPending> mq;
        DiscardCardFromHand(s, 0, 0, &mq, "cleanup");
        g_play_top_chooser = nullptr;
        CHECK(asked == 1);
    }
}

TEST_CASE("Cyclone phase 3: Curator's scry resolves BEFORE the walk -- bottom a Wraith, reach Emrakul")
{
    EnsureCards3();
    auto make = []
    {
        GameState s = Base3(0, { kWraith, kEmrakul, "Island" });
        Put3(s, kCyclone, 30);
        Put3(s, kCurator, 31);
        s.players[0].hand.push_back(Card3(kWraith, 101));
        return s;
    };
    {   // j = 0: the walk hits the Wraith on top.
        GameState s = make();
        ExecCycle(s, kWraith, 0, kCycFcDefault);
        CHECK(OnBf3(s, kWraith));
        CHECK_FALSE(OnBf3(s, kEmrakul));
    }
    {   // j = 1: the scry bottoms the Wraith first, the walk reaches Emrakul -- both worlds.
        GameState ex = make(), ro = make();
        ExecCycle(ex, kWraith, 1, kCycFcDefault);
        RolloutCycle(ro, kWraith, 1, kCycFcDefault);
        CHECK(OnBf3(ex, kEmrakul));
        CHECK(ex.pending_extra_turns == 1);
        CheckSameState(ex, ro);
    }
}

TEST_CASE("Cyclone phase 3: the searched cycle action -- one seed per distinct cycler, with its pins")
{
    EnsureCards3();
    heurarm::Clear();
    GameState s = Base3(2, { kHunger, kEmrakul, kCurator });
    Put3(s, kCyclone, 30);
    Put3(s, kCurator, 31);
    s.players[0].hand.push_back(Card3(kWraith, 101));
    s.players[0].hand.push_back(Card3(kWraith, 102));
    s.players[0].hand.push_back(Card3(kCurator, 103));
    s.players[0].hand.push_back(Card3("Remand", 104));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    int wraith_seeds = 0, curator_seeds = 0, declines = 0, remands = 0, max_j = -1;
    for (const Action& a : acts)
    {
        if (!IsCycleSeed(a)) { continue; }
        if (a.card_name.str() == kWraith)  { ++wraith_seeds; }
        if (a.card_name.str() == kCurator) { ++curator_seeds; }
        if (a.ponder_keep == kCycFcDecline)    { ++declines; }
        if (a.ponder_keep == kCycFcTakeRemand) { ++remands; }
        max_j = std::max(max_j, a.devour_count);
    }
    // Wraith: j in {0,1,2} (the Curator out + the one in hand) x {take, decline (Ulamog with no opposing
    // permanent), take-and-Remand} = 9; Curator: j in {0,1} (it cannot trigger itself) x 3 = 6.
    CHECK(wraith_seeds == 9);
    CHECK(curator_seeds == 6);
    CHECK(declines == 5);
    CHECK(remands == 5);
    CHECK(max_j == 2);
    // The enumeration never puts two seeds in one plan, nor a seed beside the cast of the same single copy.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int seeded = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        int seeds = 0;
        bool casts_curator = false;
        for (const Action& a : p.actions)
        {
            if (IsCycleSeed(a)) { ++seeds; }
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kCurator) { casts_curator = true; }
        }
        CHECK(seeds <= 1);
        if (seeds == 1) { ++seeded; }
        const Action* sd = CycleSeedOf(p.actions);
        if (casts_curator && sd != nullptr) { CHECK(sd->card_name.str() != kCurator); }
    }
    CHECK(seeded > 0);
}

TEST_CASE("Cyclone phase 3: hazard R1 -- the reactive chain goes on past a replaced cycle")
{
    EnsureCards3();
    GameState s = Base3(0, {}, 15);   // no creature in the library: every walk whiffs (no draw)
    Put3(s, kCyclone, 30);
    s.players[0].hand.push_back(Card3(kWraith, 101));
    s.players[0].hand.push_back(Card3(kWraith, 102));
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 20);
    MtgTestSeam::UseSurplusLandAbilities(eng, s, [](GameState& g) { ResolveAll3(g); }, -1, nullptr);
    CHECK(Count3(s.players[0].graveyard, kWraith) == 2);   // both cycled: the first did not end the chain
    CHECK(s.players[0].hand.empty());
    CHECK(s.players[0].life == 16);
}

TEST_CASE("Cyclone phase 3: a stale searched payment pin never cycles a different card")
{
    EnsureCards3();
    struct RestoreArm
    {
        heurarm::Arm saved = heurarm::t_arm;
        ~RestoreArm() { heurarm::t_arm = saved; }
    } restore;
    heurarm::t_arm[static_cast<int>(heurarm::CYCLE_SEED_PAY_SPLIT)] = 1;
    GameState s = Base3(2, {}, 15);   // two Islands, library has no creature: a Wraith walk would whiff
    Put3(s, kCyclone, 30);
    const int mountain0 = static_cast<int>(s.battlefield.size());
    Put3(s, "Mountain", 50);
    const int mountain1 = static_cast<int>(s.battlefield.size());
    Put3(s, "Mountain", 51);
    s.players[0].hand.push_back(Card3(kCyclone, 101));
    s.players[0].hand.push_back(Card3(kWraith, 102));
    const auto pins = SeedPaySplits(s, 0, Def3(kCyclone).params.cycling_cost.value(), Def3(kCyclone));
    REQUIRE_FALSE(pins.empty());
    std::int32_t red_pin = 0;
    const auto red_sources = (1ull << mountain0) | (1ull << mountain1);
    for (const auto pin : pins)
    { if ((SeedPaySplitHoldMask(s, 0, pin) & red_sources) == 0) { red_pin = pin; break; } }
    REQUIRE(red_pin != 0);
    // The allocation was real at collection. A preceding cast then spent its two sources;
    // the seed still exists and is generically payable from the Islands, but this pin is stale.
    s.battlefield[static_cast<std::size_t>(mountain0)].tapped = true;
    s.battlefield[static_cast<std::size_t>(mountain1)].tapped = true;
    Action seed;
    seed.kind = Action::Kind::DigDraw;
    seed.card_name = kCyclone;
    seed.def = &Def3(kCyclone);
    seed.seed_pay_split = red_pin;
    seed.ponder_keep = kCycFcDefault;
    const GameState before = s;
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 20);
    CHECK_FALSE(MtgTestSeam::UseSurplusLandAbilities(eng, s, [](GameState& g) { ResolveAll3(g); }, -1, &seed));
    CHECK(ZoneNums(s.players[0].hand) == ZoneNums(before.players[0].hand));
    CHECK(ZoneNums(s.players[0].graveyard) == ZoneNums(before.players[0].graveyard));
    CHECK(LibNums(s) == LibNums(before));
    CHECK(s.players[0].life == 20);
    CHECK(s.floating_mana == before.floating_mana);
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { CHECK(s.battlefield[i].tapped == before.battlefield[i].tapped); }
    // A real alternative was available: the unchanged NO-SEED playout still cycles Wraith.
    GameState control = before;
    AIEngine playout(MulliganProfile::DefaultProfile(), 0, 20);
    MtgTestSeam::UseSurplusLandAbilities(playout, control, [](GameState& g) { ResolveAll3(g); }, -1, nullptr);
    CHECK(Count3(control.players[0].graveyard, kWraith) == 1);
    CHECK(control.players[0].life == 18);
}
