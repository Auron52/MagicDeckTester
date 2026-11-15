#include <doctest/doctest.h>

#include "ai/LandPlay.h"
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "runner/GoldFishRunner.h"
#include "core/SpellEffects.h"
#include "core/EffectHandler.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <vector>

namespace
{
void LoadMerfolkCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

void AddPermanent(GameState& state, const char* name, int number, uint16_t chosen = 0)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE(def != nullptr);
    Permanent p;
    p.card = def->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    p.chosen_subtype_id = chosen;
    state.battlefield.push_back(p);
}

bool HasPermanent(const GameState& state, int number)
{
    return std::any_of(state.battlefield.begin(), state.battlefield.end(),
        [number](const Permanent& p) { return p.card.m_number == number; });
}

bool ExiledUnder(const GameState& state, int number, int source)
{
    return std::any_of(state.exile.begin(), state.exile.end(),
        [number, source](const Card& c) {
            return c.m_number == number && IsMerfolkChampionExile(c)
                && MerfolkChampionSource(c) == source;
        });
}

GameState NestedProphetsSetup()
{
    GameState state;
    state.active_player_index = 0;
    state.deck_has_prophets = true;
    AddPermanent(state, "Rishadan Dockhand", 601); // C
    AddPermanent(state, "Tidal Warrior", 605);     // alternative victim
    AddPermanent(state, "Wanderwine Prophets", 602); // B
    {
        const std::vector<int> pins{0};
        ScriptedProphetsChampion choice(pins);
        ResolveMerfolkChampion(state, 0, 2);
    }
    AddPermanent(state, "Wanderwine Prophets", 603); // A
    {
        const std::vector<int> pins{1};
        ScriptedProphetsChampion choice(pins);
        ResolveMerfolkChampion(state, 0, 2);
    }
    return state;
}
}

TEST_CASE("Three Tree City counts the chosen type of each source copy")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Island", 101);
    AddPermanent(state, "Island", 102);
    const auto& types = SubtypeRegistry::Instance();
    AddPermanent(state, "Three Tree City", 103, types.Id("Goblin"));
    AddPermanent(state, "Three Tree City", 104, types.Id("Merfolk"));
    AddPermanent(state, "Goblin Lackey", 105);
    AddPermanent(state, "Goblin Lackey", 106);
    AddPermanent(state, "Goblin Lackey", 107);
    AddPermanent(state, "Rishadan Dockhand", 108);
    const CardDefinition* city = CardDatabase::Instance().Lookup("Three Tree City");
    REQUIRE(city != nullptr);
    CHECK(ScaledManaCreatureCount(state, &state.battlefield[2]) == 3);
    CHECK(ScaledManaCreatureCount(state, &state.battlefield[3]) == 1);
    CHECK(ScaledManaNetYield(state, *city, &state.battlefield[2]) == 1);
    CHECK(ScaledManaNetYield(state, *city, &state.battlefield[3]) == 0);
}

TEST_CASE("Three Tree City asks for its type on a normal land drop and a put")
{
    LoadMerfolkCards();
    const CardDefinition* city = CardDatabase::Instance().Lookup("Three Tree City");
    REQUIRE(city != nullptr);
    const auto& types = SubtypeRegistry::Instance();
    EtbTypeChooser chooser = [](const GameState&, int, const std::string& source,
                                const std::vector<std::string>& menu, int) {
        CHECK(source == "Three Tree City");
        const auto it = std::find(menu.begin(), menu.end(), "Goblin");
        CHECK(it != menu.end());
        return it == menu.end() ? 0 : static_cast<int>(it - menu.begin());
    };
    EtbTypeChooser* prior = g_play_etb_type_chooser;
    g_play_etb_type_chooser = &chooser;
    GameState dropped;
    dropped.active_player_index = 0;
    Card hand = city->card;
    hand.m_number = 801;
    dropped.players[0].hand.push_back(hand);
    const bool played = PlayLandFromHand(dropped, 0, *city, LandPlayOptions{});
    GameState put;
    put.active_player_index = 0;
    EnterLand(put, *city, 802);
    g_play_etb_type_chooser = prior;
    REQUIRE(played);
    REQUIRE(dropped.battlefield.size() == 1);
    REQUIRE(put.battlefield.size() == 1);
    CHECK(dropped.battlefield[0].chosen_subtype_id == types.Id("Goblin"));
    CHECK(put.battlefield[0].chosen_subtype_id == types.Id("Goblin"));
}

TEST_CASE("Seahunter searches Merfolk permanents and rejects Merfolk spells")
{
    LoadMerfolkCards();
    const CardDefinition* prophets = CardDatabase::Instance().Lookup("Wanderwine Prophets");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(dockhand != nullptr);
    CHECK(SeahunterLibraryCandidate(dockhand->card));
    Card instant = dockhand->card;
    instant.RemoveType(CardType::Creature);
    instant.AddType(CardType::Instant);
    CHECK_FALSE(SeahunterLibraryCandidate(instant));
    if (prophets != nullptr) { CHECK(SeahunterLibraryCandidate(prophets->card)); }
}

TEST_CASE("Merfolk Seahunter fetches a champion victim before Prophets")
{
    LoadMerfolkCards();
    MerfolkTurnsProvider provider;
    GameState state;
    state.active_player_index = 0;
    std::vector<std::string> names{"Wanderwine Prophets", "Svyelun of Sea and Sky",
                                   "Tidal Warrior"};
    CHECK(provider.SeahunterCandidates(state, names));
    CHECK(names == std::vector<std::string>{"Svyelun of Sea and Sky"});

    const CardDefinition* emperor = CardDatabase::Instance().Lookup("Emperor Mihail II");
    REQUIRE(emperor != nullptr);
    state.ActivePlayer().hand.push_back(emperor->card);
    names = {"Wanderwine Prophets", "Svyelun of Sea and Sky", "Tidal Warrior"};
    CHECK_FALSE(provider.SeahunterCandidates(state, names));
    CHECK(names.size() == 3);
    state.ActivePlayer().hand.clear();

    AddPermanent(state, "Tidal Warrior", 200);
    names = {"Wanderwine Prophets", "Svyelun of Sea and Sky", "Tidal Warrior"};
    CHECK(provider.SeahunterCandidates(state, names));
    CHECK(names == std::vector<std::string>{"Wanderwine Prophets"});

    AddPermanent(state, "Wanderwine Prophets", 201);
    names = {"Wanderwine Prophets", "Svyelun of Sea and Sky", "Tidal Warrior"};
    CHECK_FALSE(provider.SeahunterCandidates(state, names));
    CHECK(names.size() == 3);

    state.battlefield.clear();
    state.turn_number = 5;
    state.Opponent().life = 2;
    AddPermanent(state, "Tidal Warrior", 202);
    AddPermanent(state, "Seahunter", 203);
    names = {"Wanderwine Prophets", "Merfolk Sovereign"};
    CHECK_FALSE(provider.SeahunterCandidates(state, names));
    CHECK(names.size() == 2);
}

TEST_CASE("Tidal Warrior keeps nonbasic blue conversion after planned blue spends")
{
    LoadMerfolkCards();
    MerfolkTurnsProvider provider;
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Island", 220);
    AddPermanent(state, "Island", 221);
    AddPermanent(state, "Rogue's Passage", 222);
    AddPermanent(state, "Tidal Warrior", 223);
    const CardDefinition* banneret = CardDatabase::Instance().Lookup("Stonybrook Banneret");
    REQUIRE(banneret != nullptr);
    state.ActivePlayer().hand.push_back(banneret->card);
    std::vector<int> targets{2};
    provider.TapTargetPumpTargets(state, state.battlefield[3], true, targets);
    CHECK(targets == std::vector<int>{2});
}

TEST_CASE("Seahunter puts the pinned library Merfolk through the shared battlefield path")
{
    LoadMerfolkCards();
    const CardDefinition* hunter = CardDatabase::Instance().Lookup("Seahunter");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(hunter != nullptr);
    REQUIRE(dockhand != nullptr);
    REQUIRE(island != nullptr);
    GameState state;
    state.active_player_index = 0;
    Card merfolk = dockhand->card;
    merfolk.m_number = 201;
    Card land = island->card;
    land.m_number = 202;
    state.players[0].library.push_back(land);
    state.players[0].library.push_back(merfolk);

    PerformTutorToBattlefield(state, 0, hunter->params, 1,
                              {"Rishadan Dockhand"}, "Seahunter");
    REQUIRE(state.battlefield.size() == 1);
    CHECK(state.battlefield[0].card.m_number == 201);
    CHECK(state.battlefield[0].entered_this_turn);
    CHECK(state.players[0].library.size() == 1);

    PerformTutorToBattlefield(state, 0, hunter->params, 1,
                              {kTutorDeclineTarget}, "Seahunter");
    CHECK(state.battlefield.size() == 1);
    CHECK(state.players[0].library.size() == 1);
}

TEST_CASE("Seahunter pinned activation remains live with a compact library card")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Seahunter", 210);
    Card compact;
    compact.m_name = "Rishadan Dockhand";
    compact.m_number = 211;
    state.players[0].library.push_back(compact);
    SacNamedPinScope pin(0, &state.players[0].library[0].m_name.str());
    CHECK(PermAbilitySourceLive(state, 0, 210, PermAbilityMode::TapTutorPut));
}

TEST_CASE("Emperor Mihail II sees the private top only under its controller's permission")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    Card top = merfolk->card;
    top.m_number = 301;
    state.players[0].library.push_back(top);
    CHECK(PlayableTopMerfolkNumber(state) == 0);
    AddPermanent(state, "Emperor Mihail II", 302);
    CHECK(MayCastTopMerfolk(state));
    CHECK(PlayableTopMerfolkNumber(state) == 301);
    CHECK_FALSE(LibraryTopVisible(state));
    state.battlefield[0].controller_index = 1;
    CHECK(PlayableTopMerfolkNumber(state) == 0);
}

TEST_CASE("Emperor Mihail II pays each cast trigger independently")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Emperor Mihail II", 401);
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    state.floating_mana.colorless = 2;
    state.scripted_mihail_pay_choice.Pin(2);
    state.scripted_mihail_pay_choice.bits[0] = 1; // pay first, decline second
    FireOnCastTriggers(state, *merfolk);
    FireOnCastTriggers(state, *merfolk);
    CHECK(state.scripted_mihail_trigger_ordinal == 2);
    CHECK(state.floating_mana.colorless == 1);
    int tokens = 0;
    for (const Permanent& p : state.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, "Merfolk")) { ++tokens; } }
    CHECK(tokens == 1);

    GameState two;
    two.active_player_index = 0;
    AddPermanent(two, "Emperor Mihail II", 411);
    AddPermanent(two, "Emperor Mihail II", 412);
    two.floating_mana.colorless = 2;
    two.scripted_mihail_pay_choice.Pin(2);
    two.scripted_mihail_pay_choice.bits[1] = 1; // decline first copy, pay second
    FireOnCastTriggers(two, *merfolk);
    CHECK(two.scripted_mihail_trigger_ordinal == 2);
    tokens = 0;
    for (const Permanent& p : two.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, "Merfolk")) { ++tokens; } }
    CHECK(tokens == 1);
}

TEST_CASE("Mihail put by Vial or Seahunter can be paid for on a later cast")
{
    LoadMerfolkCards();
    const CardDefinition* mihail = CardDatabase::Instance().Lookup("Emperor Mihail II");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(mihail != nullptr);
    REQUIRE(dockhand != nullptr);
    REQUIRE(island != nullptr);
    for (bool vial : {true, false})
    {
        CAPTURE(vial);
        GameState state;
        state.active_player_index = 0;
        state.turn_number = 4;
        state.players[0].lands_played_this_turn = 1;
        AddPermanent(state, vial ? "Aether Vial" : "Seahunter", 450);
        if (vial) { state.battlefield.back().charge_counters = 3; }
        for (int i = 0; i < (vial ? 2 : 5); ++i)
        { AddPermanent(state, "Island", 451 + i); }
        Card hand_merfolk = dockhand->card;
        hand_merfolk.m_number = 470;
        state.players[0].hand.push_back(hand_merfolk);
        Card source_mihail = mihail->card;
        source_mihail.m_number = 471;
        if (vial) { state.players[0].hand.push_back(source_mihail); }
        else { state.players[0].library.push_back(source_mihail); }
        for (int i = 0; i < 12; ++i)
        {
            Card filler = island->card;
            filler.m_number = 480 + i;
            state.players[0].library.push_back(filler);
        }
        bool offered_paid_line = false;
        bool offered_declined_line = false;
        bool offered_late_line = false;
        TurnSolver::Plan paid_plan, declined_plan;
        TurnSolver::Plan late_plan;
        int put_lines = 0, cast_lines = 0, both_lines = 0, paid_lines = 0;
        std::string both_order;
        for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
        {
            int put_at = -1, cast_at = -1;
            for (std::size_t i = 0; i < plan.actions.size(); ++i)
            {
                const Action& a = plan.actions[i];
                if (vial && a.kind == Action::Kind::ActivateVial
                    && a.card_name == "Emperor Mihail II") { put_at = static_cast<int>(i); }
                if (!vial && a.kind == Action::Kind::ActivatePermAbility
                    && a.ability_mode == Action::AbilityMode::TapTutorPut
                    && a.tutor_target == "Emperor Mihail II") { put_at = static_cast<int>(i); }
                if (a.kind == Action::Kind::CastFromHand
                    && a.card_name == "Rishadan Dockhand") { cast_at = static_cast<int>(i); }
            }
            put_lines += put_at >= 0;
            cast_lines += cast_at >= 0;
            both_lines += put_at >= 0 && cast_at >= 0;
            paid_lines += plan.mihail_pay_choice.Paid(0);
            if (put_at >= 0 && cast_at >= 0)
            { both_order = std::to_string(put_at) + "," + std::to_string(cast_at); }
            if (put_at >= 0 && cast_at > put_at && plan.mihail_pay_choice.Paid(0))
            { offered_paid_line = true; paid_plan = plan; }
            if (put_at >= 0 && cast_at > put_at && !plan.mihail_pay_choice.Paid(0))
            { offered_declined_line = true; declined_plan = plan; }
            if (put_at > cast_at && cast_at >= 0 && (!vial || plan.searched_order)
                && !plan.mihail_pay_choice.Paid(0))
            { offered_late_line = true; late_plan = plan; }
            if (offered_paid_line && offered_declined_line && offered_late_line) { break; }
        }
        INFO(put_lines, " ", cast_lines, " ", both_lines, " ", paid_lines, " ", both_order);
        CHECK(offered_paid_line);
        CHECK(offered_declined_line);
        CHECK(offered_late_line);
        if (!offered_paid_line || !offered_declined_line || !offered_late_line) { continue; }
        auto tokens = [](const GameState& s)
        {
            return std::count_if(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& p)
                { return p.is_token && CardHasSubtype(p.card, "Merfolk"); });
        };
        auto mihails = [](const GameState& s)
        {
            return std::count_if(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& p)
                { return p.card.m_name == "Emperor Mihail II"; });
        };
        auto dockhands = [](const GameState& s)
        {
            return std::count_if(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& p)
                { return p.card.m_name == "Rishadan Dockhand" && !p.is_token; });
        };
        for (const TurnSolver::Plan* plan : {&paid_plan, &declined_plan, &late_plan})
        {
            GameState rollout = state;
            TurnSolver::ApplyPlan(rollout, *plan, true);
            AIEngine engine(MulliganProfile::DefaultProfile(), 3, 20);
            MtgTestSeam::CommitPhasePlan(engine, *plan, true);
            GameState executed = state;
            engine.TakeTurn(executed, true, [](GameState& s)
            {
                while (!s.stack.empty())
                {
                    const StackEntry entry = s.stack.back();
                    s.stack.pop_back();
                    const CardDefinition* def = CardDatabase::Instance().LookupCached(entry.source);
                    if (def) { EffectHandler::Resolve(s, entry, *def); }
                }
            });
            const int expected = plan == &paid_plan ? 1 : 0;
            CHECK(tokens(rollout) == expected);
            CHECK(tokens(executed) == expected);
            CHECK(mihails(rollout) == 1);
            CHECK(mihails(executed) == 1);
            CHECK(dockhands(rollout) == 1);
            CHECK(dockhands(executed) == 1);
            CHECK(rollout.scripted_mihail_trigger_ordinal == executed.scripted_mihail_trigger_ordinal);
            CHECK(rollout.scripted_mihail_trigger_ordinal == (plan == &late_plan ? 0 : 1));
            CHECK(TurnSolver::DedupKeyOf(rollout) == TurnSolver::DedupKeyOf(executed));
        }
    }
}

TEST_CASE("Mihail Vial and Seahunter puts retain a between-casts timing")
{
    LoadMerfolkCards();
    const CardDefinition* mihail = CardDatabase::Instance().Lookup("Emperor Mihail II");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    const CardDefinition* warrior = CardDatabase::Instance().Lookup("Tidal Warrior");
    REQUIRE(mihail != nullptr);
    REQUIRE(dockhand != nullptr);
    REQUIRE(warrior != nullptr);
    for (bool vial : {true, false})
    {
        CAPTURE(vial);
        GameState state;
        state.active_player_index = 0;
        state.turn_number = 4;
        state.players[0].lands_played_this_turn = 1;
        AddPermanent(state, vial ? "Aether Vial" : "Seahunter", 550);
        if (vial) { state.battlefield.back().charge_counters = 3; }
        for (int i = 0; i < (vial ? 4 : 7); ++i)
        { AddPermanent(state, "Island", 551 + i); }
        Card first = dockhand->card;
        first.m_number = 570;
        state.players[0].hand.push_back(first);
        Card second = warrior->card;
        second.m_number = 571;
        state.players[0].hand.push_back(second);
        Card source = mihail->card;
        source.m_number = 572;
        if (vial) { state.players[0].hand.push_back(source); }
        else { state.players[0].library.push_back(source); }
        bool before = false, between = false, after = false;
        TurnSolver::Plan middle;
        for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
        {
            int put_at = -1, cast_count = 0, earlier = 0;
            for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
            {
                const Action& a = plan.actions[static_cast<std::size_t>(i)];
                if ((vial && a.kind == Action::Kind::ActivateVial && a.card_name == "Emperor Mihail II")
                    || (!vial && a.kind == Action::Kind::ActivatePermAbility
                        && a.ability_mode == Action::AbilityMode::TapTutorPut
                        && a.tutor_target == "Emperor Mihail II")) { put_at = i; }
                if (a.kind == Action::Kind::CastFromHand
                    && (a.card_name == "Rishadan Dockhand" || a.card_name == "Tidal Warrior"))
                { ++cast_count; }
            }
            if (put_at < 0 || cast_count != 2 || (vial && !plan.searched_order)) { continue; }
            for (int i = 0; i < put_at; ++i)
            {
                const Action& a = plan.actions[static_cast<std::size_t>(i)];
                earlier += a.kind == Action::Kind::CastFromHand
                    && (a.card_name == "Rishadan Dockhand" || a.card_name == "Tidal Warrior");
            }
            if (earlier == 0) { before = true; }
            if (earlier == 2) { after = true; }
            if (earlier == 1 && plan.mihail_pay_choice.Paid(0))
            { between = true; middle = plan; }
        }
        CHECK(before);
        CHECK(between);
        CHECK(after);
        if (!between) { continue; }
        GameState rollout = state;
        TurnSolver::ApplyPlan(rollout, middle, true);
        AIEngine engine(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(engine, middle, true);
        GameState executed = state;
        engine.TakeTurn(executed, true, [](GameState& s)
        {
            while (!s.stack.empty())
            {
                const StackEntry entry = s.stack.back();
                s.stack.pop_back();
                const CardDefinition* def = CardDatabase::Instance().LookupCached(entry.source);
                if (def) { EffectHandler::Resolve(s, entry, *def); }
            }
        });
        for (const GameState* result : {&rollout, &executed})
        {
            CHECK(result->scripted_mihail_trigger_ordinal == 1);
            CHECK(std::count_if(result->battlefield.begin(), result->battlefield.end(), [](const Permanent& p)
                { return p.is_token && CardHasSubtype(p.card, "Merfolk"); }) == 1);
        }
        CHECK(TurnSolver::DedupKeyOf(rollout) == TurnSolver::DedupKeyOf(executed));
    }
}

TEST_CASE("Mihail ordered choices retain high ordinals and distinct plan keys")
{
    MihailPayChoices first, second;
    first.Pin(65);
    second.Pin(65);
    first.bits[30] = 1;
    second.bits[64] = 1;
    CHECK(first.Paid(30));
    CHECK_FALSE(first.Paid(64));
    CHECK(second.Paid(64));
    CHECK(first != second);
    auto key = [](const MihailPayChoices& choice) {
        std::string result;
        choice.FoldValues([&](std::uint64_t v) { result += std::to_string(v) + ","; });
        return result;
    };
    CHECK(key(first) != key(second));
    MihailPayChoices unpinned;
    CHECK(key(unpinned) != key(first));
    CHECK(unpinned != first);
    MihailPayChoices decline_all;
    decline_all.Pin(65);
    CHECK(key(unpinned) != key(decline_all));
    CHECK(key(first) != key(decline_all));
    MihailPayChoices carry;
    carry.Pin(31);
    std::fill(carry.bits.begin(), carry.bits.begin() + 30, 1);
    CHECK(carry.Next());
    CHECK(carry.Paid(30));
    CHECK_FALSE(carry.Paid(0));

    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Emperor Mihail II", 420);
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    state.floating_mana.colorless = 1;
    state.scripted_mihail_pay_choice = first;
    state.scripted_mihail_trigger_ordinal = 30;
    FireOnCastTriggers(state, *merfolk);
    CHECK(state.scripted_mihail_trigger_ordinal == 31);
    CHECK(state.floating_mana.colorless == 0);
}

TEST_CASE("Mihail human choice overrides the scripted choice for each trigger")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Emperor Mihail II", 430);
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    state.floating_mana.colorless = 2;
    state.scripted_mihail_pay_choice.Pin(2);
    state.scripted_mihail_pay_choice.bits[1] = 1;
    int prompts = 0;
    DigChooser chooser = [&](const GameState&, int, const std::string& source,
                             const std::vector<Card>&, const std::vector<int>&, int) {
        CHECK(source.find("Merfolk cast pay") != std::string::npos);
        return prompts++ == 0 ? 0 : -1;
    };
    DigChooser* saved = g_play_dig_chooser;
    g_play_dig_chooser = &chooser;
    FireOnCastTriggers(state, *merfolk);
    FireOnCastTriggers(state, *merfolk);
    g_play_dig_chooser = saved;
    CHECK(prompts == 2);
    CHECK(state.floating_mana.colorless == 1);
}

TEST_CASE("Wanderwine Prophets champions a selected copy and returns it under its owner")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Rishadan Dockhand", 501);
    AddPermanent(state, "Tidal Warrior", 502);
    AddPermanent(state, "Wanderwine Prophets", 503);
    state.battlefield[1].owner_index = 1;
    const std::vector<int> pin{1};
    {
        ScriptedProphetsChampion choice(pin);
        ResolveMerfolkChampion(state, 0, 2);
    }
    REQUIRE(state.battlefield.size() == 2);
    CHECK(state.battlefield[1].card.m_number == 503);
    REQUIRE(state.exile.size() == 1);
    CHECK(state.exile[0].m_number == 502);
    CHECK(state.battlefield[1].linked_exile_number == 502);
    Card left = state.battlefield[1].card;
    state.battlefield.erase(state.battlefield.begin() + 1);
    FireLeavesBattlefieldTriggers(state, 0, left);
    REQUIRE(state.battlefield.size() == 2);
    CHECK(state.battlefield[1].card.m_number == 502);
    CHECK(state.battlefield[1].controller_index == 1);
    CHECK(state.battlefield[1].owner_index == 1);
    CHECK(state.battlefield[1].entered_this_turn);
    CHECK(state.exile.empty());
}

TEST_CASE("Nested Prophets links D to A before B returns and champions D")
{
    LoadMerfolkCards();
    GameState state = NestedProphetsSetup();
    REQUIRE(HasPermanent(state, 603));
    REQUIRE(HasPermanent(state, 601)); // B leaving returned C
    REQUIRE(ExiledUnder(state, 602, 603));
    AddPermanent(state, "Wanderwine Prophets", 604); // D
    const std::vector<int> pins{1, 2, 0}; // D -> A, returned B -> D, returned A -> 605
    int prompts = 0;
    BounceChooser chooser = [&](const GameState& during, int, const std::string& source,
                                const std::vector<int>& legal, int proposed) {
        CHECK(source == "Wanderwine Prophets champion");
        if (++prompts == 2)
        {
            CHECK(ExiledUnder(during, 603, 604));
            CHECK(HasPermanent(during, 604));
            CHECK(HasPermanent(during, 602));
        }
        if (prompts == 3)
        { CHECK(during.battlefield[legal[0]].card.m_number == 605); CHECK(proposed == 0); }
        return proposed;
    };
    BounceChooser* saved = g_play_flicker_chooser;
    g_play_flicker_chooser = &chooser;
    {
        ScriptedProphetsChampion choice(pins);
        ResolveMerfolkChampion(state, 0, 3);
    }
    g_play_flicker_chooser = saved;
    CHECK(prompts == 3);
    CHECK(HasPermanent(state, 603)); // D's linked return found A
    CHECK(HasPermanent(state, 602));
    CHECK_FALSE(HasPermanent(state, 604));
    CHECK_FALSE(HasPermanent(state, 605));
    CHECK(ExiledUnder(state, 604, 602));
    CHECK(ExiledUnder(state, 605, 603));
    CHECK_FALSE(ExiledUnder(state, 603, 604));
}

TEST_CASE("Returned B's nested champion choice may select another victim or decline")
{
    LoadMerfolkCards();
    const GameState setup = NestedProphetsSetup();
    for (const std::vector<int>& pins : {std::vector<int>{1, 0}, std::vector<int>{1, -2}})
    {
        GameState state = setup;
        AddPermanent(state, "Wanderwine Prophets", 604);
        {
            ScriptedProphetsChampion choice(pins);
            ResolveMerfolkChampion(state, 0, 3);
        }
        CHECK(HasPermanent(state, 604));
        CHECK(HasPermanent(state, 602) == (pins[1] == 0));
        CHECK(ExiledUnder(state, 603, 604));
        if (pins[1] == 0) { CHECK(ExiledUnder(state, 605, 602)); }
        else { CHECK_FALSE(ExiledUnder(state, 605, 602)); }
    }
}

TEST_CASE("Autonomous Vial plans expose the returned Prophets' second champion choice")
{
    LoadMerfolkCards();
    MerfolkTurnsProvider provider;
    GameState state = NestedProphetsSetup();
    state.m_provider = &provider;
    AddPermanent(state, "Aether Vial", 606);
    state.battlefield.back().charge_counters = 6;
    const CardDefinition* prophets = CardDatabase::Instance().Lookup("Wanderwine Prophets");
    REQUIRE(prophets != nullptr);
    Card d = prophets->card;
    d.m_number = 604;
    state.players[0].hand.push_back(d);
    const auto plans = TurnSolver::EnumerateMainPlans(state, false);
    bool choose_d = false, choose_other = false, decline = false, replayed = false;
    bool third_c = false, third_b = false, third_decline = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1 || !std::any_of(p.actions.begin(), p.actions.end(), [](const Action& a) {
            return a.kind == Action::Kind::ActivateVial
                && a.card_name == "Wanderwine Prophets";
        }) || p.champion_pins.size() < 2 || p.champion_pins[0] != 1) { continue; }
        choose_d |= p.champion_pins[1] == 2;
        choose_other |= p.champion_pins[1] == 0;
        decline |= p.champion_pins[1] == -2;
        if (p.champion_pins.size() >= 3 && p.champion_pins[1] == 2)
        {
            third_c |= p.champion_pins[2] == 1;
            third_b |= p.champion_pins[2] == 2;
            third_decline |= p.champion_pins[2] == -2;
        }
        if (!replayed && p.champion_pins.size() >= 3
            && p.champion_pins[1] == 2 && p.champion_pins[2] == 0)
        {
            GameState replay = state;
            TurnSolver::ApplyPlan(replay, p, false);
            CHECK(HasPermanent(replay, 603));
            CHECK(HasPermanent(replay, 602));
            CHECK(ExiledUnder(replay, 604, 602));
            CHECK(ExiledUnder(replay, 605, 603));
            replayed = true;
        }
    }
    CHECK(choose_d);
    CHECK(choose_other);
    CHECK(decline);
    CHECK(third_c);
    CHECK(third_b);
    CHECK(third_decline);
    CHECK(replayed);
}

TEST_CASE("Two new Prophets preserve the choice of an initially unlinked return")
{
    LoadMerfolkCards();
    MerfolkTurnsProvider provider;
    GameState state;
    state.active_player_index = 0;
    state.deck_has_prophets = true;
    state.m_provider = &provider;
    AddPermanent(state, "Wanderwine Prophets", 630); // A, no champion link yet
    AddPermanent(state, "Aether Vial", 631);
    AddPermanent(state, "Aether Vial", 632);
    state.battlefield[1].charge_counters = 6;
    state.battlefield[2].charge_counters = 6;
    const CardDefinition* prophets = CardDatabase::Instance().Lookup("Wanderwine Prophets");
    REQUIRE(prophets != nullptr);
    for (int number : {633, 634})
    {
        Card card = prophets->card;
        card.m_number = number;
        state.players[0].hand.push_back(card);
    }
    bool has_third_pin = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
    {
        int puts = 0;
        for (const Action& a : plan.actions)
        { puts += a.kind == Action::Kind::ActivateVial && a.card_name == "Wanderwine Prophets"; }
        if (puts == 2 && plan.champion_pins.size() >= 3)
        { has_third_pin = true; break; }
    }
    CHECK(has_third_pin);
}

TEST_CASE("Wanderwine Prophets token champion ceases to exist; decline sacrifices Prophets")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Rishadan Dockhand", 511);
    state.battlefield[0].is_token = true;
    AddPermanent(state, "Wanderwine Prophets", 512);
    ResolveMerfolkChampion(state, 0, 1);
    REQUIRE(state.battlefield.size() == 1);
    CHECK(state.battlefield[0].card.m_number == 512);
    CHECK(state.exile.empty());

    AddPermanent(state, "Rishadan Dockhand", 513);
    AddPermanent(state, "Wanderwine Prophets", 514);
    const std::vector<int> decline{-2};
    {
        ScriptedProphetsChampion choice(decline);
        ResolveMerfolkChampion(state, 0, 2);
    }
    REQUIRE(state.battlefield.size() == 2);
    CHECK(state.battlefield[0].card.m_number == 512);
    CHECK(state.battlefield[1].card.m_number == 513);
}

TEST_CASE("Merfolk land target abilities can identify every passive opponent land")
{
    LoadMerfolkCards();
    Decklist deck;
    for (const char* name : {"Tidal Warrior", "Rishadan Dockhand"})
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        deck.mainboard.push_back(def->card);
    }
    GameState state = GoldFishRunner::SetupGame(deck, 987453, 2);
    std::vector<int> ids;
    const CardDefinition* warrior = CardDatabase::Instance().Lookup("Tidal Warrior");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(warrior != nullptr);
    REQUIRE(dockhand != nullptr);
    for (const Permanent& q : state.battlefield)
    {
        if (q.controller_index != 1 || !q.card.IsLand()) { continue; }
        CHECK(q.card.m_number != 0);
        CHECK(TapTargetPumpTargetLegalFor(state, q, 0, *warrior));
        CHECK(TapTargetPumpTargetLegalFor(state, q, 0, *dockhand));
        ids.push_back(q.card.m_number);
    }
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] != ids[1]);
    CHECK(ids[1] != ids[2]);
    CHECK(ids[0] != ids[2]);
}

TEST_CASE("islandized Three Tree City pays blue pips and exposes blue casts")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    state.turn_number = 3;
    state.deck_has_islandize = true;
    state.players[0].lands_played_this_turn = 1;
    AddPermanent(state, "Three Tree City", 720);
    state.battlefield.back().islandize_target = 720;
    AddPermanent(state, "Rogue's Passage", 721);
    AddPermanent(state, "Rogue's Passage", 722);
    AddPermanent(state, "Tidal Warrior", 723);
    state.battlefield.back().tapped = true;
    int hand_id = 724;
    for (const char* name : {"Tidal Warrior", "Deeproot Pilgrimage"})
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        Card c = def->card;
        c.m_number = hand_id++;
        state.players[0].hand.push_back(c);
    }
    CHECK(LandTypeChangedToIsland(state, state.battlefield[0]));
    bool tidal = false, pilgrimage = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
    {
        for (const Action& a : plan.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            tidal |= a.card_name == "Tidal Warrior";
            pilgrimage |= a.card_name == "Deeproot Pilgrimage";
        }
    }
    CHECK(tidal);
    CHECK(pilgrimage);
}

TEST_CASE("Three Tree City converts two colorless feeders into blue at zero net mana")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    state.turn_number = 4;
    state.players[0].lands_played_this_turn = 1;
    AddPermanent(state, "Rogue's Passage", 740);
    AddPermanent(state, "Rogue's Passage", 741);
    AddPermanent(state, "Three Tree City", 742, SubtypeRegistry::Instance().Id("Merfolk"));
    AddPermanent(state, "Tidal Warrior", 743);
    AddPermanent(state, "Rishadan Dockhand", 744);
    const CardDefinition* city = CardDatabase::Instance().Lookup("Three Tree City");
    const CardDefinition* pilgrimage = CardDatabase::Instance().Lookup("Deeproot Pilgrimage");
    REQUIRE(city != nullptr);
    REQUIRE(pilgrimage != nullptr);
    CHECK(ScaledManaNetYield(state, *city, &state.battlefield[2]) == 0);
    CHECK(ScaledManaCreatureCount(state, &state.battlefield[2]) == 2);
    CHECK(ScaledManaFeederMana(state) >= 2);
    Card card = pilgrimage->card;
    card.m_number = 745;
    state.players[0].hand.push_back(card);
    bool offered = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(state, true))
    {
        for (const Action& a : plan.actions)
        { offered |= a.kind == Action::Kind::CastFromHand && a.card_name == "Deeproot Pilgrimage"; }
    }
    CHECK(offered);
}

TEST_CASE("Wanderwine Prophets token sacrifice ceases to exist after its trigger")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Wanderwine Prophets", 610);
    AddPermanent(state, "Rishadan Dockhand", GameState::kFirstTokenNumber);
    state.battlefield.back().is_token = true;
    state.scripted_prophets_sac = {1};
    ResolveProphetsCombatDamage(state, 0, {610});
    CHECK(state.pending_extra_turns == 1);
    REQUIRE(state.battlefield.size() == 1);
    CHECK(state.battlefield[0].card.m_number == 610);
    CHECK(state.players[0].graveyard.empty());
}

TEST_CASE("Wanderwine Prophets asks no sacrifice after lethal combat damage")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    state.players[1].life = 4;
    AddPermanent(state, "Wanderwine Prophets", 750);
    AddPermanent(state, "Rishadan Dockhand", 751);
    state.scripted_prophets_sac = {1};
    ResolveCombatDamage(state, {0}, 0, false);
    CHECK(state.players[1].life <= 0);
    CHECK(state.pending_extra_turns == 0);
    CHECK(state.scripted_prophets_sac_cursor == 0);
    CHECK(HasPermanent(state, 751));
}

TEST_CASE("Wanderwine Prophets combat trigger sacrifices each chosen Merfolk and queues extra turns")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Wanderwine Prophets", 521);
    AddPermanent(state, "Rishadan Dockhand", 522);
    AddPermanent(state, "Tidal Warrior", 523);
    state.scripted_prophets_sac = {1, 1};
    ResolveProphetsCombatDamage(state, 0, {521, 521});
    CHECK(state.pending_extra_turns == 2);
    REQUIRE(state.battlefield.size() == 1);
    CHECK(state.battlefield[0].card.m_number == 521);
    CHECK(state.players[0].graveyard.size() == 2);

    GameState decline;
    decline.active_player_index = 0;
    AddPermanent(decline, "Wanderwine Prophets", 531);
    decline.scripted_prophets_sac = {-2};
    ResolveProphetsCombatDamage(decline, 0, {531});
    CHECK(decline.pending_extra_turns == 0);
    CHECK(decline.battlefield.size() == 1);
}

TEST_CASE("Wanderwine Prophets without fodder sacrifices itself and may sacrifice itself in combat")
{
    LoadMerfolkCards();
    GameState enters;
    enters.active_player_index = 0;
    AddPermanent(enters, "Wanderwine Prophets", 541);
    ResolveMerfolkChampion(enters, 0, 0);
    CHECK(enters.battlefield.empty());
    REQUIRE(enters.players[0].graveyard.size() == 1);
    CHECK(enters.players[0].graveyard[0].m_number == 541);

    GameState combat;
    combat.active_player_index = 0;
    AddPermanent(combat, "Wanderwine Prophets", 542);
    combat.scripted_prophets_sac = {0};
    ResolveProphetsCombatDamage(combat, 0, {542});
    CHECK(combat.pending_extra_turns == 1);
    CHECK(combat.battlefield.empty());

    GameState default_choice;
    default_choice.active_player_index = 0;
    AddPermanent(default_choice, "Wanderwine Prophets", 543);
    ResolveProphetsCombatDamage(default_choice, 0, {543});
    CHECK(default_choice.pending_extra_turns == 0);
    CHECK(HasPermanent(default_choice, 543));
}

TEST_CASE("Wanderwine Prophets sacrifice priority keeps its engines")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    for (const char* name : {"Wanderwine Prophets", "Emperor Mihail II",
                             "Svyelun of Sea and Sky", "Merfolk Sovereign",
                             "Silvergill Douser", "Rishadan Dockhand"})
    { AddPermanent(state, name, 560 + static_cast<int>(state.battlefield.size())); }
    AddPermanent(state, "Tidal Warrior", 566);
    state.battlefield.back().is_token = true;
    int previous = INT_MAX;
    for (const Permanent& p : state.battlefield)
    {
        const int rank = ProphetsSacrificePriority(p);
        CHECK(rank < previous);
        previous = rank;
    }
    ResolveProphetsCombatDamage(state, 0, {560});
    CHECK(state.pending_extra_turns == 1);
    CHECK_FALSE(HasPermanent(state, 566));
    CHECK(HasPermanent(state, 560));
}

TEST_CASE("Merfolk Turns counts a fresh Prophets combat event only with Greaves on that card")
{
    MerfolkTurnsProvider provider;
    GameState state;
    Action equip;
    equip.kind = Action::Kind::Equip;
    equip.card_name = InternedName("Lightning Greaves");
    equip.sac_victim_id = 700;
    const std::vector<int> fresh{701, 702};
    CHECK(provider.ProphetsFreshCombatEventCap(state, {}, fresh) == 0);
    CHECK(provider.ProphetsFreshCombatEventCap(state, {equip}, fresh) == 0);
    equip.sac_victim_id = 701;
    CHECK(provider.ProphetsFreshCombatEventCap(state, {equip}, fresh) == 1);
    CHECK(provider.ProphetsFreshCombatEventCap(state, {equip}, {0}) == 1);
    equip.card_name = InternedName("Skullclamp");
    CHECK(provider.ProphetsFreshCombatEventCap(state, {equip}, fresh) == 0);
}

TEST_CASE("Merfolk Turns bounds Prophets victims by its actual token sources")
{
    LoadMerfolkCards();
    MerfolkTurnsProvider provider;
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Deeproot Pilgrimage", 710);
    AddPermanent(state, "Emperor Mihail II", 711);
    AddPermanent(state, "Rishadan Dockhand", 712);
    Action cast;
    cast.kind = Action::Kind::CastFromHand;
    cast.card_name = InternedName("Wanderwine Prophets");
    Action equip;
    equip.kind = Action::Kind::Equip;
    equip.card_name = InternedName("Lightning Greaves");
    CHECK(provider.ProphetsMerfolkVictimWidth(state, {cast, cast, equip}, 2, 2) == 7);
    Action tap;
    tap.kind = Action::Kind::ActivatePermAbility;
    tap.card_name = InternedName("Rishadan Dockhand");
    CHECK(provider.ProphetsMerfolkVictimWidth(state, {cast, cast, equip, tap}, 2, 2) == 8);
    Action put;
    put.kind = Action::Kind::ActivatePermAbility;
    put.ability_mode = Action::AbilityMode::TapTutorPut;
    put.tutor_target = InternedName("Emperor Mihail II");
    CHECK(provider.ProphetsMerfolkVictimWidth(state, {put, cast}, 2, 0) == 8);
}
