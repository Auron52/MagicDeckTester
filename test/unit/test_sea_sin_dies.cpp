// Sin, Unending Cataclysm's death clause (2026-11-18; ledger Q9, USER: "Sin dies might be worth modeling just
// because it is something that could come up if we changed the list."):
//   "When Sin dies, put its counters on target creature you control, then shuffle this card into its owner's
//   library."
//   * ONE body (ResolveDiesCountersToOwnCreature) in the shared OnCreatureDies: the executor and the rollout both run it.
//   * "Its counters" = the same number of EACH KIND Sin had as it left (last-known information, captured at the death
//     site by DyingCountersTotal; Scryfall ruling 2025-06-06). A PUT: -1/-1 annihilate, a doubler applies.
//   * No legal target (CR 603.3d) -> nothing happens, Sin stays in the graveyard (ruling 2025-06-06).
//   * A token copy puts its counters but is not a card: nothing is shuffled (CR 111.7).
//   * Target = DecisionProvider::DiesCountersTargetOrder's front; human play re-asks on the board.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kSin    = "Sin, Unending Cataclysm";
const char* const kKraken = "Spawning Kraken";

const CardDefinition& SdDef(const std::string& name)
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

const SeaCreaturesProvider& SdProv()
{
    static const SeaCreaturesProvider p;
    return p;
}

struct SdStripArm   // MTG_SEA_SIN_STRIP_SAGAS on for the scope: the second Sin harvests nothing (no Saga out)
{
    SdStripArm() { heurarm::t_arm[heurarm::SEA_SIN_STRIP_SAGAS] = 1; }
    ~SdStripArm() { heurarm::t_arm[heurarm::SEA_SIN_STRIP_SAGAS] = -1; }
};

GameState SdBoard()
{
    GameState s;
    s.m_provider          = &SdProv();
    s.active_player_index = 0;
    s.turn_number         = 7;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.game_seed           = 4242;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 10; ++i)
    {
        Card c = SdDef("Island").card;
        c.m_number = 900 + i;
        s.players[0].library.push_back(c);
    }
    return s;
}

int SdPut(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card             = SdDef(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int SdIndex(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

const Permanent* SdFind(const GameState& s, int number)
{
    const int i = SdIndex(s, number);
    return i < 0 ? nullptr : &s.battlefield[static_cast<std::size_t>(i)];
}

bool SdInLibrary(const GameState& s, int number)
{
    for (const Card& c : s.players[0].library) { if (c.m_number == number) { return true; } }
    return false;
}

bool SdInGraveyard(const GameState& s, int number)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}
}  // namespace

TEST_CASE("Sin dies: card data carries the clause")
{
    CHECK(SdDef(kSin).params.dies_put_counters_on_own_creature_shuffle_self);
    CHECK_FALSE(SdDef(kKraken).params.dies_put_counters_on_own_creature_shuffle_self);
}

TEST_CASE("Sin dies: every kind of counter goes to our creature, then the card shuffles into the library")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 6);
    s.battlefield[static_cast<std::size_t>(sin)].lore_counters   = 2;   // a dedicated counter int
    s.battlefield[static_cast<std::size_t>(sin)].charge_counters = 1;
    SdPut(s, kKraken, 600);
    const std::size_t lib_before = s.players[0].library.size();
    const int ord_before = s.search_count;
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    REQUIRE(SdFind(s, 600) != nullptr);
    CHECK(PlusCountersOn(*SdFind(s, 600)) == 6);
    CHECK(SdFind(s, 600)->lore_counters == 2);
    CHECK(SdFind(s, 600)->charge_counters == 1);
    CHECK(SdFind(s, 4242) == nullptr);
    CHECK_FALSE(SdInGraveyard(s, 4242));
    CHECK(SdInLibrary(s, 4242));
    CHECK(s.players[0].library.size() == lib_before + 1);
    CHECK(s.search_count == ord_before + 1);   // shuffled on the search-shuffle ordinal
    CHECK(g_dies_counter_lki.empty());         // the snapshot was consumed
}

TEST_CASE("Sin dies: the shuffle is deterministic per seed and ordinal")
{
    auto run = []()
    {
        GameState s = SdBoard();
        const int sin = SdPut(s, kSin, 4242);
        s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 2);
        SdPut(s, kKraken, 600);
        SacrificePermanentAt(s, 0, SdIndex(s, 4242));
        std::vector<int> order;
        for (const Card& c : s.players[0].library) { order.push_back(c.m_number); }
        return order;
    };
    CHECK(run() == run());
}

TEST_CASE("Sin dies: no creature to target -- the ability does nothing, Sin stays in the graveyard")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 4);
    const std::size_t lib_before = s.players[0].library.size();
    const int ord_before = s.search_count;
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    CHECK(SdInGraveyard(s, 4242));
    CHECK_FALSE(SdInLibrary(s, 4242));
    CHECK(s.players[0].library.size() == lib_before);
    CHECK(s.search_count == ord_before);
}

TEST_CASE("Sin dies: an opponent's creature is never a target")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 4);
    SdPut(s, kKraken, 700, /*controller=*/1);
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    CHECK(PlusCountersOn(*SdFind(s, 700)) == 0);
    CHECK(SdInGraveyard(s, 4242));   // no legal target: nothing happens
}

TEST_CASE("Sin dies: -1/-1 counters annihilate with the target's +1/+1 (CR 704.5r)")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::MinusOneMinusOne, 2);
    const int kr = SdPut(s, kKraken, 600);
    s.battlefield[static_cast<std::size_t>(kr)].counters.Add(Counter::Type::PlusOnePlusOne, 5);
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    CHECK(PlusCountersOn(*SdFind(s, 600)) == 3);
    CHECK(MinusCountersOn(*SdFind(s, 600)) == 0);
}

TEST_CASE("Sin dies: a token copy puts its counters but shuffles nothing")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].is_token = true;
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 3);
    SdPut(s, kKraken, 600);
    const std::size_t lib_before = s.players[0].library.size();
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    CHECK(PlusCountersOn(*SdFind(s, 600)) == 3);
    CHECK(s.players[0].library.size() == lib_before);
}

TEST_CASE("Sin dies: no snapshot -- the counts the death site passed stand in")
{
    GameState s = SdBoard();
    SdPut(s, kKraken, 600);
    Card dead = SdDef(kSin).card;
    dead.m_number = 4242;
    s.players[0].graveyard.push_back(dead);
    g_dies_counter_lki.clear();
    OnCreatureDies(s, 0, dead, /*dead_was_token=*/false, /*minus=*/0, nullptr, /*total=*/5);
    CHECK(PlusCountersOn(*SdFind(s, 600)) == 5);
    CHECK(SdInLibrary(s, 4242));
}

TEST_CASE("Sin dies: the provider orders the targets -- a creature that can attack now first")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 4);
    const int sick = SdPut(s, kKraken, 601);
    s.battlefield[static_cast<std::size_t>(sick)].entered_this_turn = true;   // summoning-sick: second
    SdPut(s, "Island", 602);   // not a creature: never a target
    SdPut(s, kKraken, 603);    // can attack: the front
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    CHECK(PlusCountersOn(*SdFind(s, 603)) == 4);
    CHECK(PlusCountersOn(*SdFind(s, 601)) == 0);
}

TEST_CASE("Sin dies: human play -- the target click offers every creature we control, the front preselected")
{
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 4);
    const int sick = SdPut(s, kKraken, 601);
    s.battlefield[static_cast<std::size_t>(sick)].entered_this_turn = true;
    SdPut(s, kKraken, 603);
    SdPut(s, kKraken, 700, /*controller=*/1);
    std::vector<int> seen;
    int seen_heur = -1;
    LoyaltyTargetChooser chooser = [&](const GameState& st, int, const std::string&, const std::string&,
                                       const std::vector<int>& legal, int heur)
    {
        for (int i : legal) { seen.push_back(st.battlefield[static_cast<std::size_t>(i)].card.m_number); }
        seen_heur = heur;
        return static_cast<int>(legal.size()) - 1;   // the human picks the LAST offered (the sick Kraken)
    };
    LoyaltyTargetChooser* saved = g_play_loyalty_chooser;
    g_play_loyalty_chooser = &chooser;
    SacrificePermanentAt(s, 0, SdIndex(s, 4242));
    g_play_loyalty_chooser = saved;
    CHECK(seen == std::vector<int>{ 603, 601 });   // ours only, provider order
    CHECK(seen_heur == 0);
    CHECK(PlusCountersOn(*SdFind(s, 601)) == 4);
    CHECK(PlusCountersOn(*SdFind(s, 603)) == 0);
}

TEST_CASE("Sin dies: executor -- a second Sin's legend rule kills the old one and its counters move")
{
    SdStripArm strip;
    GameState s = SdBoard();
    const int sin = SdPut(s, kSin, 4242);
    s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 6);
    SdPut(s, kKraken, 600);
    const CardDefinition& def = SdDef(kSin);
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = def.card;
    e.source.m_number = 4243;
    e.controller_index = 0;
    e.tutor_target = "@H";
    e.legend_keep = 2;   // keep the newest: the old Sin dies
    EffectHandler::Resolve(s, e, def);
    CHECK(SdFind(s, 4242) == nullptr);
    REQUIRE(SdFind(s, 4243) != nullptr);
    CHECK(PlusCountersOn(*SdFind(s, 600)) == 6);    // the Kraken can attack: the front
    CHECK(PlusCountersOn(*SdFind(s, 4243)) == 0);
    CHECK(SdInLibrary(s, 4242));
    CHECK_FALSE(SdInGraveyard(s, 4242));
}

TEST_CASE("Sin dies: rollout -- ApplyPlan's legend rule runs the same trigger (lockstep with the executor)")
{
    // The cast's legend_keep rider (1 keep-oldest / 2 keep-newest; the searched twins of TurnSolver's LEGEND-KEEP PASS,
    // whose reachability test_boars_i5b covers) decides which Sin dies; ApplyPlan must then run the same death clause
    // the executor test above runs. The plan is built directly (the Boars "rider decides" shape).
    SdStripArm strip;
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        GameState s = SdBoard();
        const int sin = SdPut(s, kSin, 4242);
        s.battlefield[static_cast<std::size_t>(sin)].counters.Add(Counter::Type::PlusOnePlusOne, 6);
        SdPut(s, kKraken, 600);
        for (int i = 0; i < 7; ++i) { SdPut(s, "Breeding Pool", 100 + i); }
        s.players[0].hand.push_back(SdDef(kSin).card);
        s.players[0].hand.back().m_number = 4243;
        Action a;
        a.kind         = Action::Kind::CastFromHand;
        a.card_name    = kSin;
        a.def          = &SdDef(kSin);
        a.hand_index   = -1;
        a.cost         = SdDef(kSin).card.m_mana_cost;
        a.card_mv      = SdDef(kSin).card.m_mana_cost.ManaValue();
        a.tutor_target = std::string("@H");   // harvest nothing (the old Sin's counters stay on it until it dies)
        a.legend_keep  = static_cast<std::int8_t>(keep);
        TurnSolver::Plan p;
        p.actions = { a };
        TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
        if (keep == 2)
        {
            // The old Sin (six counters) dies: the Kraken gets them, the card shuffles away.
            CHECK(SdFind(s, 4242) == nullptr);
            REQUIRE(SdFind(s, 4243) != nullptr);
            CHECK(PlusCountersOn(*SdFind(s, 600)) == 6);
            CHECK(SdInLibrary(s, 4242));
            CHECK_FALSE(SdInGraveyard(s, 4242));
        }
        else
        {
            // The new Sin (no counters) dies: nothing is put, the new card shuffles away, the old keeps its six.
            CHECK(SdFind(s, 4243) == nullptr);
            REQUIRE(SdFind(s, 4242) != nullptr);
            CHECK(PlusCountersOn(*SdFind(s, 600)) == 0);
            CHECK(PlusCountersOn(*SdFind(s, 4242)) == 6);
            CHECK(SdInLibrary(s, 4243));
            CHECK_FALSE(SdInGraveyard(s, 4243));
        }
    }
}
