#include <doctest/doctest.h>
#include <set>

#include "cards/CardDatabase.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/OpponentDeck.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"

namespace {
void EnsureMillCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card Named(const char* name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}
}

TEST_CASE("Tasha exiles real top cards until their printed mana values total twenty")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    state.players[1].library.push_back(Named("Island", 100001));
    for (int i = 0; i < 4; ++i)
    { state.players[1].library.push_back(Named("Shivan Dragon", 100002 + i)); }
    state.players[1].library.push_back(Named("Island", 100006));
    CHECK(ResolveOpponentLibrarySpell(state, 0, 20) == 5);
    CHECK(state.exile.size() == 5u);
    CHECK(state.players[1].library.size() == 1u);
    CHECK_FALSE(state.opponent_decked);
}

TEST_CASE("Glimpse can mill the caster and Ruin Crab mills the opponent")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    for (int i = 0; i < 12; ++i)
    { state.players[0].library.push_back(Named("Island", i + 1)); }
    for (int i = 0; i < 5; ++i)
    { state.players[1].library.push_back(Named("Swamp", 100001 + i)); }
    const CardDefinition* glimpse = CardDatabase::Instance().Lookup("Glimpse the Unthinkable");
    REQUIRE(glimpse != nullptr);
    REQUIRE(glimpse->params.spell_mill_target_player);
    CHECK(ResolveOpponentLibrarySpell(state, 10, 0, 0, true, 0, glimpse) == 10);
    CHECK(state.players[0].graveyard.size() == 10u);
    CHECK(state.players[1].library.size() == 5u);

    const CardDefinition* crab = CardDatabase::Instance().Lookup("Ruin Crab");
    REQUIRE(crab != nullptr);
    Permanent watcher;
    watcher.card = crab->card;
    watcher.card.m_number = 30;
    watcher.controller_index = 0;
    state.battlefield.push_back(watcher);
    Permanent land;
    land.card = CardDatabase::Instance().Lookup("Island")->card;
    land.card.m_number = 31;
    land.controller_index = 0;
    state.battlefield.push_back(land);
    OnLandEntered(state, 0, 31);
    CHECK(state.players[1].graveyard.size() == 3u);
    CHECK(state.players[1].library.size() == 2u);
}

TEST_CASE("Hedron Crab target policy can mill its controller")
{
    EnsureMillCards();
    GameState state;
    MillProvider provider;
    state.m_provider = &provider;
    state.opponent_library_dealt = true;
    for (int i = 0; i < 5; ++i)
    { state.players[0].library.push_back(Named("Island", i + 1)); }
    const CardDefinition* crab = CardDatabase::Instance().Lookup("Hedron Crab");
    REQUIRE(crab != nullptr);
    REQUIRE(crab->params.landfall_mill_target_player);
    Permanent watcher;
    watcher.card = crab->card;
    watcher.card.m_number = 40;
    watcher.controller_index = 0;
    state.battlefield.push_back(watcher);
    Permanent land;
    land.card = CardDatabase::Instance().Lookup("Island")->card;
    land.card.m_number = 41;
    land.controller_index = 0;
    state.battlefield.push_back(land);
    for (int i = 0; i < 5; ++i)
    { state.players[1].library.push_back(Named("Swamp", 100001 + i)); }
    // First trigger selects self, second selects opponent.
    ScriptedLandfallTarget pin(0x101, state.TurnSeq());
    OnLandEntered(state, 0, 41);
    CHECK(state.players[0].graveyard.size() == 3u);
    CHECK(state.players[0].library.size() == 2u);
    OnLandEntered(state, 0, 41);
    CHECK(state.players[0].graveyard.size() == 3u);
    CHECK(state.players[1].graveyard.size() == 3u);
}

TEST_CASE("Mill fetches one legal target in Watery Grave Island Swamp order")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.m_provider = &provider;
    auto& library = state.players[0].library;
    library.push_back(Named("Swamp", 1));
    library.push_back(Named("Island", 2));
    library.push_back(Named("Watery Grave", 3));
    const CardDefinition* delta = CardDatabase::Instance().Lookup("Polluted Delta");
    const CardDefinition* tarn = CardDatabase::Instance().Lookup("Scalding Tarn");
    REQUIRE(delta != nullptr);
    REQUIRE(tarn != nullptr);
    CHECK(provider.FetchCandidates(state, 0, delta->params) == std::vector<std::string>{"Watery Grave"});
    CHECK(provider.FetchCandidates(state, 0, tarn->params) == std::vector<std::string>{"Watery Grave"});
    library.erase(library.begin() + 2);
    CHECK(provider.FetchCandidates(state, 0, delta->params) == std::vector<std::string>{"Island"});
    CHECK(provider.FetchCandidates(state, 0, tarn->params) == std::vector<std::string>{"Island"});
    library.erase(library.begin() + 1);
    CHECK(provider.FetchCandidates(state, 0, delta->params) == std::vector<std::string>{"Swamp"});
    CHECK(provider.FetchCandidates(state, 0, tarn->params).empty());
}

TEST_CASE("Mill omits control spells from autonomous casts")
{
    EnsureMillCards();
    MillProvider provider;
    for (const char* name : {"Counterspell", "Drown in the Loch", "Fatal Push"})
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        CHECK(provider.NeverCast(*def));
    }
    const CardDefinition* mill = CardDatabase::Instance().Lookup("Glimpse the Unthinkable");
    REQUIRE(mill != nullptr);
    CHECK_FALSE(provider.NeverCast(*mill));
}

TEST_CASE("Hedron's sixteen policies exhaust all four-trigger player-target sequences")
{
    EnsureMillCards();
    MillProvider provider;
    const CardDefinition* crab = CardDatabase::Instance().Lookup("Hedron Crab");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(crab != nullptr);
    REQUIRE(island != nullptr);
    for (int mask = 0; mask < 16; ++mask)
    {
        GameState state;
        state.m_provider = &provider;
        state.opponent_library_dealt = true;
        for (int i = 0; i < 16; ++i)
        {
            state.players[0].library.push_back(Named("Island", i + 1));
            state.players[1].library.push_back(Named("Swamp", 100001 + i));
        }
        Permanent watcher;
        watcher.card = crab->card;
        watcher.card.m_number = 40;
        watcher.controller_index = 0;
        state.battlefield.push_back(watcher);
        Permanent land;
        land.card = island->card;
        land.card.m_number = 41;
        land.controller_index = 0;
        state.battlefield.push_back(land);
        ScriptedLandfallTarget pin(0x100 | mask, state.TurnSeq());
        for (int ordinal = 0; ordinal < 4; ++ordinal)
        {
            const std::size_t own_before = state.players[0].graveyard.size();
            const std::size_t opp_before = state.players[1].graveyard.size();
            OnLandEntered(state, 0, 41);
            CHECK(state.players[0].graveyard.size() == own_before + (((mask >> ordinal) & 1) ? 3u : 0u));
            CHECK(state.players[1].graveyard.size() == opp_before + (((mask >> ordinal) & 1) ? 0u : 3u));
        }
        const std::size_t own_before = state.players[0].graveyard.size();
        const std::size_t opp_before = state.players[1].graveyard.size();
        OnLandEntered(state, 0, 41);
        CHECK(state.players[0].graveyard.size() == own_before + ((mask & 1) ? 3u : 0u));
        CHECK(state.players[1].graveyard.size() == opp_before + ((mask & 1) ? 0u : 3u));
    }
}

TEST_CASE("Mill folds Hedron masks only when a plan cannot trigger landfall")
{
    EnsureMillCards();
    struct FullHedronMill : MillProvider
    {
        bool FoldsZeroTriggerHedronPolicies() const override { return false; }
    };
    static const MillProvider folded;
    static const FullHedronMill full;
    const CardDefinition* crab = CardDatabase::Instance().Lookup("Hedron Crab");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    const CardDefinition* grave = CardDatabase::Instance().Lookup("Watery Grave");
    REQUIRE(crab != nullptr);
    REQUIRE(island != nullptr);
    REQUIRE(grave != nullptr);

    auto setup = [&]() {
        GameState s;
        s.active_player_index = 0;
        s.turn_number = 2;
        s.phase = Phase::PreCombatMain;
        s.players[0].life = 20;
        s.players[1].life = 20;
        s.deck_has_landfall = true;
        s.opponent_library_dealt = true;
        s.players[0].hand.push_back(Named("Glimpse the Unthinkable", 50));
        for (int i = 0; i < 20; ++i)
        { s.players[0].library.push_back(Named("Island", 100 + i)); }
        for (int i = 0; i < 10; ++i)
        { s.players[1].library.push_back(Named("Swamp", 100001 + i)); }
        for (int i = 0; i < 3; ++i)
        {
            Permanent land;
            land.card = (i == 0 ? grave : island)->card;
            land.card.m_number = 10 + i;
            land.controller_index = 0;
            s.battlefield.push_back(land);
        }
        Permanent watcher;
        watcher.card = crab->card;
        watcher.card.m_number = 40;
        watcher.controller_index = 0;
        s.battlefield.push_back(watcher);
        return s;
    };
    auto glimpse_plans = [&](const DecisionProvider& provider) {
        GameState s = setup();
        s.m_provider = &provider;
        std::vector<TurnSolver::Plan> result;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (!p.land_to_play.empty() || p.actions.size() != 1
                || p.actions[0].card_name.str() != "Glimpse the Unthinkable"
                || p.actions[0].player_target != 1) { continue; }
            result.push_back(p);
        }
        return result;
    };
    const auto full_plans = glimpse_plans(full);
    const auto folded_plans = glimpse_plans(folded);
    REQUIRE(full_plans.size() == 16u);
    REQUIRE(folded_plans.size() == 1u);

    auto after = [&](const DecisionProvider& provider, const TurnSolver::Plan& p) {
        GameState s = setup();
        s.m_provider = &provider;
        TurnSolver::ApplyPlan(s, p, true);
        std::vector<int> signature;
        signature.push_back(OpponentHasLost(s) ? 1 : 0);
        signature.push_back(s.opponent_decked ? 1 : 0);
        signature.push_back(static_cast<int>(s.players[0].library.size()));
        signature.push_back(static_cast<int>(s.players[1].library.size()));
        for (const Card& c : s.players[0].graveyard) { signature.push_back(c.m_number); }
        signature.push_back(-1);
        for (const Card& c : s.players[1].graveyard) { signature.push_back(c.m_number); }
        return signature;
    };
    const auto expected = after(folded, folded_plans[0]);
    CHECK(expected[3] == 0); // The library is empty; loss waits for the next draw.
    for (const auto& p : full_plans) { CHECK(after(full, p) == expected); }
}

TEST_CASE("Mill Hedron prefix masks preserve every reachable landfall outcome")
{
    EnsureMillCards();
    struct FullHedronMill : MillProvider
    {
        bool FoldsZeroTriggerHedronPolicies() const override { return false; }
        bool FoldsUnusedHedronPolicyBits() const override { return false; }
    };
    static const MillProvider folded;
    static const FullHedronMill full;
    const CardDefinition* crab = CardDatabase::Instance().Lookup("Hedron Crab");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(crab != nullptr);
    REQUIRE(island != nullptr);

    for (int n_crabs = 1; n_crabs <= 3; ++n_crabs)
    {
        auto setup = [&]() {
            GameState s;
            s.active_player_index = 0;
            s.turn_number = 3;
            s.phase = Phase::PreCombatMain;
            s.players[0].life = 20;
            s.players[1].life = 20;
            s.deck_has_landfall = true;
            s.opponent_library_dealt = true;
            s.hedron_mill_trigger_ordinal = 3; // The used bits wrap from 3 to 0, then 1.
            s.players[0].hand.push_back(Named("Island", 50));
            for (int i = 0; i < 20; ++i)
            {
                s.players[0].library.push_back(Named("Island", 100 + i));
                s.players[1].library.push_back(Named("Swamp", 100001 + i));
            }
            for (int i = 0; i < n_crabs; ++i)
            {
                Permanent watcher;
                watcher.card = crab->card;
                watcher.card.m_number = 40 + i;
                watcher.controller_index = 0;
                s.battlefield.push_back(watcher);
            }
            return s;
        };
        auto outcomes = [&](const DecisionProvider& provider, int& plans_seen) {
            GameState start = setup();
            start.m_provider = &provider;
            std::set<std::vector<int>> result;
            plans_seen = 0;
            for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(start, true))
            {
                if (p.land_to_play != "Island" || !p.actions.empty()) { continue; }
                ++plans_seen;
                GameState s = start;
                TurnSolver::ApplyPlan(s, p, true);
                std::vector<int> signature;
                signature.push_back(OpponentHasLost(s) ? 1 : 0);
                signature.push_back(static_cast<int>(s.players[0].library.size()));
                signature.push_back(static_cast<int>(s.players[1].library.size()));
                for (const Card& c : s.players[0].graveyard) { signature.push_back(c.m_number); }
                signature.push_back(-1);
                for (const Card& c : s.players[1].graveyard) { signature.push_back(c.m_number); }
                result.insert(std::move(signature));
            }
            return result;
        };
        int full_count = 0, folded_count = 0;
        const auto full_outcomes = outcomes(full, full_count);
        const auto folded_outcomes = outcomes(folded, folded_count);
        CHECK(full_count == 16);
        CHECK(folded_count == (1 << n_crabs));
        // Equal three-card mills commute: outcomes depend on the count sent to
        // each player, while the plan surface still contains every bit pattern.
        CHECK(full_outcomes.size() == static_cast<std::size_t>(n_crabs + 1));
        CHECK(folded_outcomes == full_outcomes);
    }
}

TEST_CASE("Mill fetch hold and crack-on-play retain exactly the used Hedron bits")
{
    EnsureMillCards();
    struct FullHedronMill : MillProvider
    {
        bool FoldsZeroTriggerHedronPolicies() const override { return false; }
        bool FoldsUnusedHedronPolicyBits() const override { return false; }
    };
    static const MillProvider folded;
    static const FullHedronMill full;
    const CardDefinition* crab = CardDatabase::Instance().Lookup("Hedron Crab");
    const CardDefinition* tarn = CardDatabase::Instance().Lookup("Scalding Tarn");
    REQUIRE(crab != nullptr);
    REQUIRE(tarn != nullptr);
    auto setup = [&](bool held) {
        GameState s;
        s.active_player_index = 0;
        s.turn_number = 3;
        s.phase = Phase::PreCombatMain;
        s.players[0].life = 20;
        s.players[1].life = 20;
        s.deck_has_landfall = true;
        s.deck_has_land_search = true;
        s.opponent_library_dealt = true;
        s.hedron_mill_trigger_ordinal = 3;
        for (int i = 0; i < 20; ++i)
        {
            s.players[0].library.push_back(Named("Island", 100 + i));
            s.players[1].library.push_back(Named("Swamp", 100001 + i));
        }
        Permanent watcher;
        watcher.card = crab->card;
        watcher.card.m_number = 40;
        watcher.controller_index = 0;
        s.battlefield.push_back(watcher);
        if (held)
        {
            Permanent fetch;
            fetch.card = tarn->card;
            fetch.card.m_number = 41;
            fetch.controller_index = 0;
            s.battlefield.push_back(fetch);
        }
        else { s.players[0].hand.push_back(Named("Scalding Tarn", 41)); }
        return s;
    };
    for (bool held : {true, false})
    {
        auto outcomes = [&](const DecisionProvider& provider, int& plans_seen) {
            GameState start = setup(held);
            start.m_provider = &provider;
            std::set<std::vector<int>> result;
            plans_seen = 0;
            for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(start, true))
            {
                if (held)
                {
                    if (!p.land_to_play.empty() || p.actions.size() != 1
                        || !TurnSolver::IsLandSearchActivation(p.actions[0])) { continue; }
                }
                else if (p.land_to_play != "Scalding Tarn" || p.fetch_target.empty()
                         || !p.actions.empty()) { continue; }
                ++plans_seen;
                GameState s = start;
                TurnSolver::ApplyPlan(s, p, true);
                std::vector<int> signature;
                signature.push_back(OpponentHasLost(s) ? 1 : 0);
                signature.push_back(static_cast<int>(s.players[0].library.size()));
                signature.push_back(static_cast<int>(s.players[1].library.size()));
                for (const Card& c : s.players[0].graveyard) { signature.push_back(c.m_number); }
                signature.push_back(-1);
                for (const Card& c : s.players[1].graveyard) { signature.push_back(c.m_number); }
                result.insert(std::move(signature));
            }
            return result;
        };
        int full_count = 0, folded_count = 0;
        const auto full_outcomes = outcomes(full, full_count);
        const auto folded_outcomes = outcomes(folded, folded_count);
        CHECK(full_count == 16);
        CHECK(folded_count == (held ? 2 : 4));
        CHECK(full_outcomes.size() == static_cast<std::size_t>(held ? 2 : 4));
        CHECK(folded_outcomes == full_outcomes);
    }
}

TEST_CASE("Visions threshold reads either graveyard and Fractured cycle mills four")
{
    EnsureMillCards();
    const CardDefinition* visions = CardDatabase::Instance().Lookup("Visions of Beyond");
    const CardDefinition* fractured = CardDatabase::Instance().Lookup("Fractured Sanity");
    REQUIRE(visions != nullptr);
    REQUIRE(fractured != nullptr);
    GameState state;
    CHECK(SpellDrawCount(state, visions->params) == 1);
    for (int i = 0; i < 20; ++i)
    { state.players[1].graveyard.push_back(Named("Island", 100001 + i)); }
    CHECK(SpellDrawCount(state, visions->params) == 3);
    state.opponent_library_dealt = true;
    for (int i = 0; i < 6; ++i)
    { state.players[1].library.push_back(Named("Swamp", 100101 + i)); }
    OnCardCycled(state, 0, fractured->params.cycling_mill_opponent);
    CHECK(state.players[1].library.size() == 2u);
    CHECK(state.players[1].graveyard.size() == 24u);
}

TEST_CASE("Oboro returns itself to hand without tapping or sacrificing")
{
    EnsureMillCards();
    const CardDefinition* def = CardDatabase::Instance().Lookup("Oboro, Palace in the Clouds");
    REQUIRE(def != nullptr);
    REQUIRE(def->params.self_bounce_cost.has_value());
    CHECK(def->params.self_bounce_cost->ManaValue() == 1);
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::SelfBounce));

    GameState state;
    Permanent oboro;
    oboro.card = def->card;
    oboro.card.m_number = 31;
    oboro.controller_index = 0;
    oboro.owner_index = 0;
    oboro.tapped = true; // The printed ability has no {T}; a tapped Oboro can return.
    state.battlefield.push_back(oboro);
    REQUIRE(PermAbilitySourceLive(state, 0, 31, PermAbilityMode::SelfBounce));
    ApplyPermAbility(state, 0, 31, PermAbilityMode::SelfBounce);
    CHECK(state.battlefield.empty());
    REQUIRE(state.players[0].hand.size() == 1u);
    CHECK(state.players[0].hand.front().m_name == "Oboro, Palace in the Clouds");
    CHECK(state.players[0].graveyard.empty());
}

TEST_CASE("Mill offers Oboro bounce only when no other land is held")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.m_provider = &provider;
    const CardDefinition* oboro = CardDatabase::Instance().Lookup("Oboro, Palace in the Clouds");
    REQUIRE(oboro != nullptr);
    CHECK(provider.OffersSelfBounce(state, *oboro));
    state.players[0].hand.push_back(Named("Island", 1));
    CHECK_FALSE(provider.OffersSelfBounce(state, *oboro));
    state.players[0].hand.clear();
    state.players[0].hand.push_back(Named("Glimpse the Unthinkable", 2));
    CHECK(provider.OffersSelfBounce(state, *oboro));
    GenericProvider generic;
    CHECK(generic.OffersSelfBounce(state, *oboro));
}

TEST_CASE("Mill holds modern Tarn and Delta, while other decks keep immediate fetches")
{
    EnsureMillCards();
    static const MillProvider mill;
    GameState state;
    state.m_provider = &mill;
    for (const char* name : {"Scalding Tarn", "Polluted Delta"})
    {
        const CardDefinition* fetch = CardDatabase::Instance().Lookup(name);
        REQUIRE(fetch != nullptr);
        REQUIRE(fetch->params.land_search_activation_cost.has_value());
        CHECK(fetch->params.land_search_activation_cost->ManaValue() == 0);
        CHECK(fetch->params.land_search_activation_life_cost == 1);
        CHECK(fetch->params.land_search_activation_sacrifice_self);
        CHECK(fetch->params.land_search_activation_taps);
        CHECK(mill.ConsiderFetchHold(state, *fetch));
        CHECK_FALSE(FetchResolvesOnPlay(state, *fetch));
        CHECK(FetchCrackOnPlayOffered(state, *fetch));

        // `produces` is an in-hand fixing proxy, not an on-board mana ability.
        CHECK_FALSE(EffectiveProduces(state, 0, *fetch, true).empty());
        Permanent held;
        held.card = fetch->card;
        held.card.m_number = 700;
        held.controller_index = 0;
        state.battlefield.push_back(held);
        CHECK(EffectiveProducesFor(state, 0, *fetch, &held).empty());
        CHECK_FALSE(IsBaselineManaSource(*fetch));
        CHECK(AvailableManaPool(state).Total() == 0);
        CHECK(HumanPreTapOfferFor(state, state.battlefield.back()).route
              == HumanPreTapOffer::Route::None);
        CHECK(PermAbilitySourceLive(state, 0, 700, PermAbilityMode::SacLandSearch));
        state.battlefield.clear();

        state.m_provider = &DefaultProvider();
        CHECK(FetchResolvesOnPlay(state, *fetch));
        CHECK_FALSE(FetchCrackOnPlayOffered(state, *fetch));
        state.m_provider = &mill;
    }
}

TEST_CASE("Surgical Extraction requires a nonbasic graveyard target and honors the chosen subset")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    state.players[1].graveyard.push_back(Named("Shivan Dragon", 100001));
    state.players[1].hand.push_back(Named("Shivan Dragon", 100002));
    state.players[1].library.push_back(Named("Shivan Dragon", 100003));
    state.players[1].library.push_back(Named("Island", 100004));
    CHECK_FALSE(SurgicalTargetLegal(state, 1, "Island"));
    CHECK(SurgicalTargetLegal(state, 1, "Shivan Dragon"));
    const int prior_searches = state.search_count;
    ResolveSurgicalExtraction(state, 0, "1:Shivan Dragon|100003,");
    CHECK(state.players[1].graveyard.empty());
    CHECK(state.players[1].hand.size() == 1u);
    CHECK(state.players[1].library.size() == 1u);
    REQUIRE(state.exile.size() == 2u);
    CHECK(state.exile[0].m_number == 100001);
    CHECK(state.exile[1].m_number == 100003);
    CHECK(state.search_count == prior_searches + 1);
    CHECK_FALSE(state.opponent_searched_library_this_turn); // We searched their library.
}

TEST_CASE("Archive Trap alternative cost needs an opponent library search this turn")
{
    EnsureMillCards();
    const CardDefinition* trap = CardDatabase::Instance().Lookup("Archive Trap");
    REQUIRE(trap != nullptr);
    REQUIRE(trap->params.archive_trap_free_if_opponent_searched);
    GameState state;
    CHECK_FALSE(IsArchiveTrapAlternative(*trap, state, "archive_trap_zero"));
    state.opponent_searched_library_this_turn = true;
    CHECK(IsArchiveTrapAlternative(*trap, state, "archive_trap_zero"));
    CHECK_FALSE(IsArchiveTrapAlternative(*trap, state, ""));

    state.players[0].hand.push_back(Named("Archive Trap", 42));
    bool found_free = false;
    for (const Action& a : TurnSolver::CollectActionsForTest(state, true))
    {
        if (a.card_name.str() == "Archive Trap" && a.tutor_target.str() == "archive_trap_zero")
        { found_free = true; CHECK(a.cost.ManaValue() == 0); }
    }
    CHECK(found_free);
}

TEST_CASE("Surgical library-only selection preserves cards already milled to graveyard")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    state.players[1].graveyard.push_back(Named("Shadowborn Apostle", 110001));
    state.players[1].graveyard.push_back(Named("Shadowborn Apostle", 110002));
    state.players[1].library.push_back(Named("Shadowborn Apostle", 110003));
    state.players[1].library.push_back(Named("Island", 110004));
    ResolveSurgicalExtraction(state, 0, "1:Shadowborn Apostle|@library");
    REQUIRE(state.players[1].graveyard.size() == 1u);
    CHECK(state.players[1].graveyard[0].m_number == 110002);
    CHECK(state.players[1].library.size() == 1u);
    REQUIRE(state.exile.size() == 2u);
    CHECK(state.exile[0].m_number == 110001);
    CHECK(state.exile[1].m_number == 110003);
}

TEST_CASE("Surgical always exiles its target but leaves unchosen graveyard copies")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    state.players[0].graveyard.push_back(Named("Shivan Dragon", 120001));
    state.players[0].graveyard.push_back(Named("Shivan Dragon", 120002));
    state.players[0].library.push_back(Named("Shivan Dragon", 120003));
    ResolveSurgicalExtraction(state, 0, "0:Shivan Dragon|");
    REQUIRE(state.exile.size() == 1u);
    CHECK(state.exile[0].m_number == 120001);
    REQUIRE(state.players[0].graveyard.size() == 1u);
    CHECK(state.players[0].graveyard[0].m_number == 120002);
    REQUIRE(state.players[0].library.size() == 1u);
    CHECK(state.players[0].library[0].m_number == 120003);
}

TEST_CASE("Mill Surgical search keeps one optional exile mode for its chosen target")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.m_provider = &provider;
    state.opponent_library_dealt = true;
    state.players[0].hand.push_back(Named("Surgical Extraction", 7));
    state.players[0].graveyard.push_back(Named("Shivan Dragon", 120011));
    state.players[1].graveyard.push_back(Named("Shivan Dragon", 120012));
    state.players[0].library.push_back(Named("Shivan Dragon", 120013));
    state.players[1].library.push_back(Named("Shivan Dragon", 120014));
    std::set<std::string> own, opponent;
    int deferred = 0;
    for (const Action& action : TurnSolver::CollectActionsForTest(state, true))
    {
        const std::string pin = action.tutor_target.str();
        if (pin == "@mill_surgical") { ++deferred; }
        if (pin.rfind("0:Shivan Dragon|", 0) == 0)
        {
            own.insert(pin);
            if (!DecisionUnpruned(UnprunedGate::SurgicalSubset))
            { CHECK(pin == "0:Shivan Dragon|"); }
        }
        if (pin.rfind("1:Shivan Dragon|", 0) == 0)
        {
            opponent.insert(pin);
            if (!DecisionUnpruned(UnprunedGate::SurgicalSubset))
            { CHECK(pin == "1:Shivan Dragon|@library"); }
        }
    }
    if (provider.DefersSurgicalTarget())
    {
        CHECK(deferred == 1);
        CHECK(own.empty());
        CHECK(opponent.empty());
        CHECK(provider.SurgicalCastTarget(state, "@mill_surgical") == "1:Shivan Dragon|@library");
    }
    else
    {
        CHECK(deferred == 0);
        CHECK(own.size() == (DecisionUnpruned(UnprunedGate::SurgicalTarget)
            ? (DecisionUnpruned(UnprunedGate::SurgicalSubset) ? 4u : 1u) : 0u));
        CHECK(opponent.size() == (DecisionUnpruned(UnprunedGate::SurgicalSubset) ? 4u : 1u));
    }
}

TEST_CASE("Mill Surgical chooses from public graveyards when the cast is reached")
{
    EnsureMillCards();
    MillProvider provider;
    if (!provider.DefersSurgicalTarget()) { return; } // Unpruned audit uses explicit target pins.
    GameState state;
    state.game_seed = 0; // Both are printed four-ofs; Staff costs more than Living Wish.
    CHECK(provider.SurgicalCastTarget(state, "@mill_surgical").empty());
    state.players[1].graveyard.push_back(Named("Staff of Domination", 100001));
    state.players[1].graveyard.push_back(Named("Living Wish", 100002));
    CHECK(provider.SurgicalCastTarget(state, "@mill_surgical") == "1:Staff of Domination|@library");
    state.players[1].graveyard.push_back(Named("Staff of Domination", 100003));
    CHECK(provider.SurgicalCastTarget(state, "@mill_surgical") == "1:Living Wish|@library");
    CHECK(provider.SurgicalCastTarget(state, "1:Crab Umbra|@library") == "1:Crab Umbra|@library");
}

TEST_CASE("Surgical target variant exists before a same-main mill makes it legal")
{
    EnsureMillCards();
    GameState state;
    state.opponent_library_dealt = true;
    state.players[0].hand.push_back(Named("Surgical Extraction", 7));
    state.players[1].library.push_back(Named("Shivan Dragon", 100001));
    CHECK_FALSE(SurgicalTargetLegal(state, 1, "Shivan Dragon"));
    std::string target_pin;
    for (const Action& a : TurnSolver::CollectActionsForTest(state, true))
    {
        if (a.card_name.str() == "Surgical Extraction"
            && a.tutor_target.str().rfind("1:Shivan Dragon|", 0) == 0)
        { target_pin = a.tutor_target.str(); break; }
    }
    REQUIRE_FALSE(target_pin.empty());
    CHECK(ResolveOpponentLibrarySpell(state, 1, 0) == 1);
    CHECK(SurgicalTargetLegal(state, 1, target_pin.substr(2)));
    ResolveSurgicalExtraction(state, 0, target_pin);
    CHECK(state.players[1].graveyard.size() <= 1u);
    CHECK(state.search_count == 1);
}

TEST_CASE("Surgical plan needs a graveyard source but keeps same-main Glimpse")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.m_provider = &provider;
    state.opponent_library_dealt = true;
    state.players[0].hand.push_back(Named("Surgical Extraction", 7));
    state.players[1].library.push_back(Named("Shivan Dragon", 100001));
    auto put_land = [&](const char* name, int number) {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        Permanent land;
        land.card = def->card;
        land.card.m_number = number;
        land.controller_index = 0;
        state.battlefield.push_back(land);
    };
    put_land("Island", 1);
    put_land("Swamp", 2);
    auto count_surgical = [&](const GameState& s, bool require_glimpse) {
        int count = 0;
        for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(s, true))
        {
            bool surgical = false, glimpse = false;
            for (const Action& action : plan.actions)
            {
                surgical |= action.card_name.str() == "Surgical Extraction";
                glimpse |= action.card_name.str() == "Glimpse the Unthinkable";
            }
            if (surgical && (!require_glimpse || glimpse)) { ++count; }
        }
        return count;
    };
    CHECK(count_surgical(state, false) == 0);
    state.players[0].hand.push_back(Named("Glimpse the Unthinkable", 8));
    CHECK(count_surgical(state, true) > 0);
}

TEST_CASE("A first Surgical can supply a second Surgical self-target")
{
    EnsureMillCards();
    GenericProvider provider;
    GameState state;
    state.m_provider = &provider;
    state.opponent_library_dealt = true;
    state.players[0].hand.push_back(Named("Surgical Extraction", 7));
    state.players[0].hand.push_back(Named("Surgical Extraction", 8));
    state.players[1].graveyard.push_back(Named("Shivan Dragon", 100001));
    state.players[1].library.push_back(Named("Island", 100002));
    const CardDefinition* swamp = CardDatabase::Instance().Lookup("Swamp");
    REQUIRE(swamp != nullptr);
    for (int number = 1; number <= 2; ++number)
    {
        Permanent land;
        land.card = swamp->card;
        land.card.m_number = number;
        land.controller_index = 0;
        state.battlefield.push_back(land);
    }
    bool found = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
    {
        bool first = false, second = false;
        for (const Action& action : plan.actions)
        {
            const std::string pin = action.tutor_target.str();
            first |= pin.rfind("1:Shivan Dragon|", 0) == 0;
            second |= pin.rfind("0:Surgical Extraction|", 0) == 0;
        }
        found |= first && second;
    }
    CHECK(found);
}

TEST_CASE("Fatal Push restricts creature mana value and revolt raises its cap")
{
    EnsureMillCards();
    const CardDefinition* push = CardDatabase::Instance().Lookup("Fatal Push");
    REQUIRE(push != nullptr);
    CHECK(push->params.destroy_target_creature_max_mv == 2);
    CHECK(push->params.revolt_destroy_max_mv == 4);
    GameState state;
    state.active_player_index = 0;
    const char* names[] = {"Eidolon of the Great Revel", "Acclaimed Contender",
                           "Valiant Knight", "Siege-Gang Commander"};
    for (int i = 0; i < 4; ++i)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(names[i]);
        REQUIRE(def != nullptr);
        Permanent target;
        target.card = def->card;
        target.card.m_number = 100 + i;
        target.controller_index = 1;
        state.battlefield.push_back(target);
    }
    CHECK(MillRemovalTargetLegal(state, 0, *push, state.battlefield[0]));
    CHECK_FALSE(MillRemovalTargetLegal(state, 0, *push, state.battlefield[1]));
    CHECK_FALSE(MillRemovalTargetLegal(state, 0, *push, state.battlefield[2]));
    state.permanent_left_under_control_this_turn = true;
    CHECK(MillRemovalTargetLegal(state, 0, *push, state.battlefield[1]));
    CHECK(MillRemovalTargetLegal(state, 0, *push, state.battlefield[2]));
    CHECK_FALSE(MillRemovalTargetLegal(state, 0, *push, state.battlefield[3]));

    // The same restriction is checked at resolution, after the target has been chosen.
    state.permanent_left_under_control_this_turn = false;
    ResolveCreatureRemovalAt(state, 1, *push, 0, -1);
    CHECK(state.battlefield.size() == 4u);
    state.permanent_left_under_control_this_turn = true;
    ResolveCreatureRemovalAt(state, 1, *push, 0, -1);
    CHECK(state.battlefield.size() == 3u);
}

TEST_CASE("Drown destroy cap follows the target controller's graveyard")
{
    EnsureMillCards();
    const CardDefinition* drown = CardDatabase::Instance().Lookup("Drown in the Loch");
    const CardDefinition* creature = CardDatabase::Instance().Lookup("Acclaimed Contender");
    REQUIRE(drown != nullptr);
    REQUIRE(creature != nullptr);
    REQUIRE(drown->params.modal_counter_or_destroy_by_controller_graveyard);
    GameState state;
    Permanent target;
    target.card = creature->card;
    target.card.m_number = 210;
    target.controller_index = 1;
    state.battlefield.push_back(target);
    for (int i = 0; i < 4; ++i)
    { state.players[0].graveyard.push_back(Named("Island", 300 + i)); }
    CHECK_FALSE(MillRemovalTargetLegal(state, 0, *drown, state.battlefield[0]));
    for (int i = 0; i < 3; ++i)
    { state.players[1].graveyard.push_back(Named("Island", 400 + i)); }
    CHECK(MillRemovalTargetLegal(state, 0, *drown, state.battlefield[0]));
    state.players[1].graveyard.pop_back();
    ResolveCreatureRemovalAt(state, 0, *drown, 0, -1);
    CHECK(state.battlefield.size() == 1u);
    state.players[1].graveyard.push_back(Named("Island", 500));
    ResolveCreatureRemovalAt(state, 0, *drown, 0, -1);
    CHECK(state.battlefield.empty());
}

TEST_CASE("Surgical large-copy policy matches exhaustive Visions and Tasha objective on eight copies")
{
    EnsureMillCards();
    const CardDefinition* visions = CardDatabase::Instance().Lookup("Visions of Beyond");
    REQUIRE(visions != nullptr);
    GameState base;
    base.opponent_library_dealt = true;
    base.players[1].graveyard.push_back(Named("Shadowborn Apostle", 100));
    base.players[1].graveyard.push_back(Named("Shadowborn Apostle", 101));
    for (int i = 0; i < 19; ++i)
    { base.players[1].graveyard.push_back(Named("Island", 200 + i)); }
    for (int i = 0; i < 6; ++i)
    { base.players[1].library.push_back(Named("Shadowborn Apostle", 102 + i)); }
    for (int i = 0; i < 4; ++i)
    { base.players[1].library.push_back(Named("Shivan Dragon", 301 + i)); }

    int exhaustive_best = -1;
    for (int mask = 0; mask < 256; ++mask)
    {
        GameState state = base;
        std::string pin = "1:Shadowborn Apostle|";
        for (int bit = 0; bit < 8; ++bit)
        { if ((mask >> bit) & 1) { pin += std::to_string(100 + bit) + ","; } }
        ResolveSurgicalExtraction(state, 0, pin);
        // Follow Surgical by Visions and Tasha: chosen graveyard copies can
        // lower Visions below twenty, while chosen library copies shorten the
        // opponent's library before Tasha exiles by printed mana value.
        const int draw = SpellDrawCount(state, visions->params);
        ResolveOpponentLibrarySpell(state, 0, 20);
        const int removed = static_cast<int>(base.players[1].library.size()
                                             - state.players[1].library.size());
        const int objective = 10 * removed + draw;
        exhaustive_best = std::max(exhaustive_best, objective);
    }
    GameState policy = base;
    ResolveSurgicalExtraction(policy, 0, "1:Shadowborn Apostle|@library");
    const int policy_draw = SpellDrawCount(policy, visions->params);
    ResolveOpponentLibrarySpell(policy, 0, 20);
    const int policy_removed = static_cast<int>(base.players[1].library.size()
                                                - policy.players[1].library.size());
    CHECK(10 * policy_removed + policy_draw == exhaustive_best);
}

TEST_CASE("Mill two-plus-copy Surgical policy retains best Tasha and Visions line in small states")
{
    EnsureMillCards();
    MillProvider provider;
    CHECK(provider.NarrowsSurgicalSubset(1)
          == !DecisionUnpruned(UnprunedGate::SurgicalSubset));
    CHECK(provider.NarrowsSurgicalSubset(2)
          == !DecisionUnpruned(UnprunedGate::SurgicalSubset));
    CHECK(provider.NarrowsSurgicalSubset(3)
          == !DecisionUnpruned(UnprunedGate::SurgicalSubset));
    CHECK(provider.NarrowsSurgicalSubset(4)
          == !DecisionUnpruned(UnprunedGate::SurgicalSubset));
    const CardDefinition* visions = CardDatabase::Instance().Lookup("Visions of Beyond");
    REQUIRE(visions != nullptr);
    for (int variant = 0; variant < 4; ++variant)
    {
        GameState base;
        base.game_seed = 100 + variant;
        base.opponent_library_dealt = true;
        base.players[1].graveyard.push_back(Named("Shadowborn Apostle", 100));
        base.players[1].graveyard.push_back(Named("Shadowborn Apostle", 101));
        for (int i = 0; i < 19; ++i)
        { base.players[1].graveyard.push_back(Named("Island", 200 + i)); }
        for (int i = 0; i < 2; ++i)
        { base.players[1].library.push_back(Named("Shadowborn Apostle", 102 + (i + variant) % 2)); }
        for (int i = 0; i < 4; ++i)
        { base.players[1].library.push_back(Named("Shivan Dragon", 301 + i)); }
        int exhaustive_best = -1;
        for (int mask = 0; mask < 16; ++mask)
        {
            GameState state = base;
            std::string pin = "1:Shadowborn Apostle|";
            for (int bit = 0; bit < 4; ++bit)
            { if ((mask >> bit) & 1) { pin += std::to_string(100 + bit) + ","; } }
            ResolveSurgicalExtraction(state, 0, pin);
            const int draw = SpellDrawCount(state, visions->params);
            ResolveOpponentLibrarySpell(state, 0, 20);
            const int removed = static_cast<int>(base.players[1].library.size()
                                                 - state.players[1].library.size());
            const int objective = 10 * removed + draw;
            exhaustive_best = std::max(exhaustive_best, objective);
        }
        GameState policy = base;
        ResolveSurgicalExtraction(policy, 0, "1:Shadowborn Apostle|@library");
        const int policy_draw = SpellDrawCount(policy, visions->params);
        ResolveOpponentLibrarySpell(policy, 0, 20);
        const int policy_removed = static_cast<int>(base.players[1].library.size()
                                                    - policy.players[1].library.size());
        CHECK(10 * policy_removed + policy_draw == exhaustive_best);
    }
}

TEST_CASE("Mill Surgical prefers one public copy then highest mana value")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.active_player_index = 0;
    state.game_seed = 0; // Accomplished Alchemist: Living Wish precedes costlier Staff.
    using Option = DecisionProvider::SurgicalTargetOption;
    const std::vector<Option> candidates = {
        {1, "Staff of Domination", 1, 0, 0, 100, false},
        {1, "Living Wish", 1, 0, 0, 0, false},
        {1, "Accomplished Alchemist", 1, 0, 3, 100, false},
        {0, "Surgical Extraction", 1, 0, 0, 0, false},
    };
    std::vector<Option> targets = candidates;
    provider.SurgicalTargetCandidates(state, targets);
    if (!DecisionUnpruned(UnprunedGate::SurgicalTarget))
    {
        REQUIRE(targets.size() == 1u);
        CHECK(targets[0].name == "Staff of Domination");
        targets = candidates;
        targets[0].graveyard_copies = 2;
        provider.SurgicalTargetCandidates(state, targets);
        REQUIRE(targets.size() == 1u);
        CHECK(targets[0].name == "Living Wish");
    }
    else { CHECK(targets.size() == candidates.size()); }
}

TEST_CASE("Mill Surgical with Tasha estimates removable mana value")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.active_player_index = 0;
    state.game_seed = 0;
    state.players[0].hand.push_back(Named("Tasha's Hideous Laughter", 7));
    using Option = DecisionProvider::SurgicalTargetOption;
    std::vector<Option> targets = {
        {1, "Staff of Domination", 2, 0, 0, 0, false}, // 2 remaining × MV 3 = 6
        {1, "Crab Umbra", 1, 0, 0, 0, false},         // 3 remaining × MV 1 = 3
    };
    provider.SurgicalTargetCandidates(state, targets);
    if (!DecisionUnpruned(UnprunedGate::SurgicalTarget))
    {
        REQUIRE(targets.size() == 1u);
        CHECK(targets[0].name == "Staff of Domination");
    }
    else { CHECK(targets.size() == 2u); }

    targets = {
        {1, "Living Wish", 1, 0, 0, 0, false},           // 3 remaining × MV 2 = 6
        {1, "Accomplished Alchemist", 1, 0, 0, 0, false}, // 2 remaining × MV 4 = 8
    };
    provider.SurgicalTargetCandidates(state, targets);
    if (!DecisionUnpruned(UnprunedGate::SurgicalTarget))
    {
        REQUIRE(targets.size() == 1u);
        CHECK(targets[0].name == "Accomplished Alchemist");
    }
    else { CHECK(targets.size() == 2u); }
}

TEST_CASE("Mill Surgical uses the life-paid Phyrexian cast when legal")
{
    EnsureMillCards();
    MillProvider provider;
    GameState state;
    state.m_provider = &provider;
    state.opponent_library_dealt = true;
    state.players[0].life = 20;
    state.players[0].hand.push_back(Named("Surgical Extraction", 7));
    state.players[1].graveyard.push_back(Named("Living Wish", 100001));
    int life_casts = 0, mana_casts = 0;
    auto count = [&] {
        life_casts = mana_casts = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(state, true))
        {
            if (a.card_name.str() != "Surgical Extraction") { continue; }
            if (a.phyrexian_life == 2) { ++life_casts; }
            else { ++mana_casts; }
        }
    };
    count();
    CHECK(life_casts > 0);
    CHECK(mana_casts == (DecisionUnpruned(UnprunedGate::SurgicalCost) ? life_casts : 0));
    state.players[0].life = 2;
    count();
    CHECK(life_casts == 0);
    CHECK(mana_casts > 0);
}
