#include <doctest/doctest.h>
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/HeuristicDefaults.h"
#include "core/HandEntry.h"
#include "core/SpellEffects.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaOracle.h"
#include <algorithm>

namespace {
void EnsureCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& Def(const char* name);
Card Copy(const char* name, int id);
void Land(GameState& s, int id, int owner = 0);

TEST_CASE("Temporal Mastery miracle offers exact first draw and exiles after paid cast")
{
    EnsureCards();
    GameState s;
    s.deck_has_miracle = true;
    s.turn_number = 3;
    Land(s, 1);
    Land(s, 2);
    s.players[0].library.push_back(Copy("Temporal Mastery", 77));
    REQUIRE(DrawIntoHand(s, 0, 1, HandEntryReason::Draw) == 1);
    CHECK(s.players[0].first_draw_card_number_this_turn == 77);
    CHECK(s.players[0].pending_miracle_card_number == 77);
    bool prompted = false;
    FreeCastChooser take = [&](const GameState&, int, const std::string& source,
                               const std::vector<Card>& candidates, int,
                               const std::vector<std::string>&) {
        prompted = true;
        CHECK(source == "Temporal Mastery (miracle)");
        REQUIRE(candidates.size() == 1);
        CHECK(candidates[0].m_number == 77);
        REQUIRE(g_play_free_cast_paid != nullptr);
        CHECK(g_play_free_cast_paid->cost == "{1}{U}");
        return 0;
    };
    FreeCastChooser* prior = g_play_free_cast_chooser;
    g_play_free_cast_chooser = &take;
    const bool cast = ResolvePendingMiracle(s);
    g_play_free_cast_chooser = prior;
    CHECK(cast);
    CHECK(prompted);
    CHECK(s.pending_extra_turns == 1);
    CHECK(s.players[0].hand.empty());
    REQUIRE(s.exile.size() == 1);
    CHECK(s.exile[0].m_number == 77);
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
}

TEST_CASE("Draw-step miracle surplus does not enter main-phase mana pool")
{
    EnsureCards();
    GameState s;
    s.deck_has_miracle = true;
    s.turn_number = 3;
    Land(s, 1);
    for (int i = 0; i < 2; ++i)
    {
        Permanent lens;
        lens.card = Copy("Extraplanar Lens", 10 + i);
        lens.controller_index = 0;
        lens.owner_index = 0;
        lens.lens_imprinted_land = Def("Snow-Covered Island").card.m_name;
        s.battlefield.push_back(lens);
    }
    s.players[0].library.push_back(Copy("Temporal Mastery", 77));
    s.scripted_miracle_choice = 1;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(s.pending_extra_turns == 1);
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("Temporal Mastery miracle decline leaves card in hand and consumes offer")
{
    EnsureCards();
    GameState s;
    s.deck_has_miracle = true;
    Land(s, 1);
    Land(s, 2);
    s.players[0].library.push_back(Copy("Temporal Mastery", 77));
    s.players[0].library.push_back(Copy("Temporal Mastery", 78));
    REQUIRE(DrawIntoHand(s, 0, 1, HandEntryReason::Draw) == 1);
    FreeCastChooser decline = [](const GameState&, int, const std::string&,
                                 const std::vector<Card>&, int,
                                 const std::vector<std::string>&) { return -1; };
    FreeCastChooser* prior = g_play_free_cast_chooser;
    g_play_free_cast_chooser = &decline;
    CHECK_FALSE(ResolvePendingMiracle(s));
    CHECK(s.players[0].pending_miracle_card_number == 0);
    REQUIRE(DrawIntoHand(s, 0, 1, HandEntryReason::Draw) == 1);
    CHECK(s.players[0].pending_miracle_card_number == 0);
    CHECK_FALSE(ResolvePendingMiracle(s));
    g_play_free_cast_chooser = prior;
    CHECK(s.pending_extra_turns == 0);
    CHECK(s.players[0].hand.size() == 2);
    CHECK(s.exile.empty());
    CHECK_FALSE(s.battlefield[0].tapped);
    CHECK_FALSE(s.battlefield[1].tapped);
}

TEST_CASE("Miracle search fans both answers when a shuffle can expose Mastery")
{
    EnsureCards();
    GameState s;
    s.deck_has_miracle = true;
    s.turn_number = 1;
    Land(s, 1);
    Land(s, 2);
    s.players[0].library.push_back(Copy("Snow-Covered Island", 20));
    s.players[0].library.push_back(Copy("Temporal Mastery", 21));
    TurnSolver::SetSearchedPlay(true);
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    TurnSolver::SetSearchedPlay(false);
    CHECK_FALSE(plans.empty());
    CHECK(std::any_of(plans.begin(), plans.end(), [](const auto& p)
                      { return p.miracle_choice == 0; }));
    CHECK(std::any_of(plans.begin(), plans.end(), [](const auto& p)
                      { return p.miracle_choice == 1; }));
    // A fetch/shuffle can reorder the two cards before an effect draw. The
    // recorded search answers must control that newly eligible miracle.
    GameState decline = s;
    decline.scripted_miracle_choice = 0;
    std::swap(decline.players[0].library.RawAt(0), decline.players[0].library.RawAt(1));
    REQUIRE(DrawIntoHand(decline, 0, 1, HandEntryReason::Draw) == 1);
    CHECK_FALSE(ResolvePendingMiracle(decline));
    CHECK(decline.players[0].hand.front().m_number == 21);
    CHECK(decline.pending_extra_turns == 0);
    GameState accept = s;
    accept.scripted_miracle_choice = 1;
    std::swap(accept.players[0].library.RawAt(0), accept.players[0].library.RawAt(1));
    REQUIRE(DrawIntoHand(accept, 0, 1, HandEntryReason::Draw) == 1);
    CHECK(ResolvePendingMiracle(accept));
    CHECK(accept.pending_extra_turns == 1);
    REQUIRE(accept.exile.size() == 1);
    CHECK(accept.exile.front().m_number == 21);
}
const CardDefinition& Def(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card Copy(const char* name, int id)
{
    Card c = Def(name).card;
    c.m_number = id;
    return c;
}
StackEntry Spell(const char* name, int id)
{
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = Copy(name, id);
    e.controller_index = 0;
    return e;
}
void Land(GameState& s, int id, int owner)
{
    Permanent p;
    p.card = Copy("Snow-Covered Island", id);
    p.controller_index = owner;
    p.owner_index = owner;
    s.battlefield.push_back(p);
}
}

TEST_CASE("Nexus of Fate resolves into library with physical identity")
{
    EnsureCards();
    GameState s;
    s.players[0].library.push_back(Copy("Snow-Covered Island", 9));
    CHECK(EffectHandler::Resolve(s, Spell("Nexus of Fate", 77), Def("Nexus of Fate")));
    CHECK(s.pending_extra_turns == 1);
    CHECK(s.players[0].graveyard.empty());
    CHECK(s.players[0].library.size() == 2);
    CHECK(std::any_of(s.players[0].library.begin(), s.players[0].library.end(),
                      [](const Card& c) { return c.m_number == 77; }));
}

TEST_CASE("Nexus of Fate discard replaces graveyard from hand")
{
    EnsureCards();
    GameState s;
    s.players[0].hand.push_back(Copy("Nexus of Fate", 81));
    CHECK(DiscardCardFromHand(s, 0, 0, nullptr, "cleanup") == DiscardDest::LibraryShuffle);
    CHECK(s.players[0].hand.empty());
    CHECK(s.players[0].graveyard.empty());
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library.front().m_number == 81);
}

TEST_CASE("Expropriate viewer separates the vote from the permanent taken")
{
    // Human play exposes every vote and money pick; autonomous enumeration
    // deliberately keeps a smaller search menu.
    if (!HumanPlayActive()) { return; }
    EnsureCards();
    GameState s;
    s.turn_number = 10;
    for (int i = 1; i <= 9; ++i) { Land(s, i); }
    Land(s, 41, 1);
    Land(s, 42, 1);
    s.players[0].hand.push_back(Copy("Expropriate", 77));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    TurnSolver::LineSpec line;
    line.casts.push_back("Expropriate");
    const auto match = TurnSolver::CheckLine(s, true, line, &plans);
    REQUIRE(match.verdict == TurnSolver::LineCheck::Verdict::Choose);
    REQUIRE(match.variants.size() == 4);
    std::vector<std::string> votes, takes;
    for (const auto& v : match.variants)
    {
        REQUIRE(v.subs.size() == 2);
        CHECK(v.subs[0].kind == "exprovote");
        CHECK(v.subs[1].kind == "exprotake");
        votes.push_back(v.subs[0].choice);
        takes.push_back(v.subs[1].choice);
    }
    std::sort(votes.begin(), votes.end());
    votes.erase(std::unique(votes.begin(), votes.end()), votes.end());
    std::sort(takes.begin(), takes.end());
    takes.erase(std::unique(takes.begin(), takes.end()), takes.end());
    CHECK(votes.size() == 2);
    CHECK(takes.size() == 2);
    CHECK(std::none_of(match.variants.begin(), match.variants.end(), [](const auto& v)
                       { return std::any_of(v.subs.begin(), v.subs.end(), [](const auto& sub)
                                 { return sub.kind == "x" || sub.kind == "enchant"; }); }));
}

TEST_CASE("Awaken payment preserves its untapped haste land when other lands suffice")
{
    EnsureCards();
    GameState s;
    static const ExtraTurnsProvider extra_turns;
    s.m_provider = &extra_turns;
    s.turn_number = 5;
    for (int i = 1; i <= 10; ++i) { Land(s, i); }
    s.players[0].hand.push_back(Copy("Part the Waterveil", 77));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    bool found = false;
    for (const auto& plan : plans)
    {
        if (plan.actions.size() != 1) { continue; }
        const auto& cast = plan.actions.front();
        if (cast.kind != Action::Kind::CastFromHand || !cast.awaken
            || cast.enchant_target != 1) { continue; }
        GameState played = s;
        TurnSolver::ApplyPlan(played, plan, true);
        REQUIRE(played.players[0].hand.empty());
        const Permanent& target = played.battlefield.front();
        CHECK_FALSE(target.tapped);
        CHECK(target.card.IsCreature());
        CHECK(target.card.HasKeyword(Keyword::Haste));
        CHECK(target.EffectivePower() == 6);
        // On a later turn, ordinary spells must also spare the now-animated land
        // while the other Islands can cover their cost.
        for (Permanent& permanent : played.battlefield) { permanent.tapped = false; }
        played.players[0].hand.push_back(Copy("Time Warp", 78));
        const auto followups = TurnSolver::EnumerateMainPlans(played, true);
        bool cast_followup = false;
        for (const auto& followup : followups)
        {
            if (followup.actions.size() != 1
                || followup.actions.front().kind != Action::Kind::CastFromHand) { continue; }
            GameState next = played;
            TurnSolver::ApplyPlan(next, followup, true);
            CHECK_FALSE(next.battlefield.front().tapped);
            cast_followup = true;
            break;
        }
        CHECK(cast_followup);
        found = true;
        break;
    }
    CHECK(found);
}

TEST_CASE("Horn drawn by the land cannot validate as a pre-land cast")
{
    if (!HumanPlayActive()) { return; }
    EnsureCards();
    GameState s;
    static const ExtraTurnsProvider extra_turns;
    s.m_provider = &extra_turns;
    s.turn_number = 5;
    for (int i = 1; i <= 5; ++i) { Land(s, i); }
    Permanent horn;
    horn.card = Copy("Horn of Greed", 20);
    horn.owner_index = horn.controller_index = 0;
    s.battlefield.push_back(horn);
    s.players[0].hand.push_back(Copy("Snow-Covered Island", 21));
    s.players[0].library.push_back(Copy("Horn of Greed", 22));

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    bool land_first_horn = false;
    TurnSolver::Plan positioned_horn;
    for (const auto& plan : plans)
    {
        if (plan.land_to_play != "Snow-Covered Island"
            || plan.actions.size() != 1) { continue; }
        const Action& a = plan.actions.front();
        if (a.kind == Action::Kind::CastFromHand && a.card_name == "Horn of Greed")
        {
            CHECK(plan.land_slot == 0); // no impossible cast-before-land menu entry
            if (plan.land_slot == 0) { land_first_horn = true; positioned_horn = plan; }
        }
    }
    REQUIRE(land_first_horn); // the fallback has a tempting land-first match
    positioned_horn.land_slot = 1; // audit a stale/externally supplied impossible proposal
    const auto audit = manaoracle::AuditLine(s, true, positioned_horn,
                                             nullptr, 400, /*deep=*/false);
    REQUIRE(audit.audited);
    CHECK_FALSE(audit.engine_full);
    CHECK(audit.dropped.find("Horn of Greed") != std::string::npos);
    TurnSolver::LineSpec after;
    after.has_land = true;
    after.land = "Snow-Covered Island";
    after.land_slot = TurnSolver::kLandSlotAfterCasts;
    after.casts = {"Horn of Greed"};
    const auto rejected = TurnSolver::CheckLine(s, true, after, &plans);
    CHECK(rejected.verdict == TurnSolver::LineCheck::Verdict::Illegal);
    CHECK(rejected.failed_action == "cast=Horn of Greed");
    TurnSolver::LineSpec after_first_cast = after;
    after_first_cast.land_slot = 0;
    after_first_cast.land_at_cast = 1;
    after_first_cast.land_at_raw = 1;
    const auto rejected_slot = TurnSolver::CheckLine(s, true, after_first_cast, &plans);
    CHECK(rejected_slot.verdict == TurnSolver::LineCheck::Verdict::Illegal);
    CHECK(rejected_slot.failed_action == "cast=Horn of Greed");
    TurnSolver::LineSpec land_first = after;
    land_first.land_slot = 0;
    land_first.land_at_raw = 0;
    const auto first = TurnSolver::CheckLine(s, true, land_first, &plans);
    CHECK(first.verdict == TurnSolver::LineCheck::Verdict::Accept);

    // The same requested order is legitimate when the Horn is already in hand.
    s.players[0].hand.push_back(Copy("Horn of Greed", 23));
    const auto payable_plans = TurnSolver::EnumerateMainPlans(s, true);
    const auto accepted = TurnSolver::CheckLine(s, true, after, &payable_plans);
    const bool legal = accepted.verdict == TurnSolver::LineCheck::Verdict::Accept
                    || accepted.verdict == TurnSolver::LineCheck::Verdict::Choose;
    CHECK(legal);
}

TEST_CASE("Extra Turns viewer accepts Lens into Ulamog and two turn spells")
{
    EnsureCards();
    GameState s;
    static const ExtraTurnsProvider extra_turns;
    s.m_provider = &extra_turns;
    s.turn_number = 5;
    for (int i = 0; i < 11; ++i) { Land(s, 100 + i); }
    Land(s, 400, 1); // Ulamog has legal opponent targets and need not exile its own Lens.
    Land(s, 401, 1);
    for (int i = 0; i < 2; ++i)
    {
        Permanent stone;
        stone.card = Copy("Mind Stone", 200 + i);
        stone.owner_index = stone.controller_index = 0;
        s.battlefield.push_back(stone);
    }
    s.players[0].hand.push_back(Copy("Extraplanar Lens", 300));
    s.players[0].hand.push_back(Copy("Ulamog, the Ceaseless Hunger", 301));
    s.players[0].hand.push_back(Copy("Temporal Manipulation", 302));
    s.players[0].hand.push_back(Copy("Temporal Manipulation", 303));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    int most_casts = 0;
    for (const auto& p : plans)
    {
        int casts = 0;
        for (const auto& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand) { ++casts; } }
        most_casts = std::max(most_casts, casts);
    }
    CHECK(most_casts == 4); // the autonomous search can see the complete Lens-funded line
    bool executes = false;
    int best_left = 99;
    std::string best_hand;
    std::string best_order;
    int best_tapped_lands = -1, best_tapped_stones = -1;
    std::string best_imprint;
    for (const auto& p : plans)
    {
        int casts = 0;
        bool imprint = false;
        for (const auto& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand) { ++casts; }
            if (a.card_name == "Extraplanar Lens" && a.chosen_x > 0) { imprint = true; }
        }
        if (casts != 4 || !imprint) { continue; }
        GameState played = s;
        TurnSolver::ApplyPlan(played, p, true);
        if (static_cast<int>(played.players[0].hand.size()) < best_left)
        {
            best_left = static_cast<int>(played.players[0].hand.size());
            best_hand.clear();
            for (const Card& c : played.players[0].hand) { best_hand += c.m_name.str() + ","; }
            best_order.clear();
            for (const auto& a : p.actions) { best_order += a.card_name.str() + ","; }
            best_tapped_lands = best_tapped_stones = 0;
            best_imprint.clear();
            for (const Permanent& q : played.battlefield)
            {
                if (q.card.m_name == "Snow-Covered Island" && q.tapped) { ++best_tapped_lands; }
                if (q.card.m_name == "Mind Stone" && q.tapped) { ++best_tapped_stones; }
                if (q.card.m_name == "Extraplanar Lens") { best_imprint = q.lens_imprinted_land.str(); }
            }
        }
        if (!played.players[0].hand.empty()) { continue; }
        for (const Permanent& perm : played.battlefield)
        {
            if (perm.card.m_name == "Extraplanar Lens"
                && perm.lens_imprinted_land.str() == "Snow-Covered Island") { executes = true; }
        }
        if (executes) { break; }
    }
    INFO("best_left=", best_left, " best_hand=", best_hand, " order=", best_order,
         " tapped_lands=", best_tapped_lands, " tapped_stones=", best_tapped_stones,
         " imprint=", best_imprint);
    CHECK(executes); // the plan's real payments leave no promised cast in hand
    if (!HumanPlayActive()) { return; }
    TurnSolver::LineSpec line;
    line.casts = {"Extraplanar Lens", "Ulamog, the Ceaseless Hunger",
                  "Temporal Manipulation", "Temporal Manipulation"};
    const auto match = TurnSolver::CheckLine(s, true, line, &plans);
    INFO("plans=", plans.size(), " most_casts=", most_casts, " reason=", match.reason);
    const bool playable = match.verdict == TurnSolver::LineCheck::Verdict::Accept
                       || match.verdict == TurnSolver::LineCheck::Verdict::Choose;
    CHECK(playable);
    TurnSolver::LineSpec lens_only;
    lens_only.casts = {"Extraplanar Lens"};
    const auto lens_pick = TurnSolver::CheckLine(s, true, lens_only, &plans);
    CHECK(lens_pick.verdict == TurnSolver::LineCheck::Verdict::Accept);
    for (const auto& v : lens_pick.variants)
    { for (const auto& sub : v.subs) { CHECK(sub.kind != "x"); } }
}

TEST_CASE("Mind Stone sacrifice draw is offered as a human-play dig")
{
    if (!HumanPlayActive()) { return; }
    EnsureCards();
    GameState s;
    s.turn_number = 3;
    Land(s, 1);
    Permanent stone;
    stone.card = Copy("Mind Stone", 2);
    stone.controller_index = 0;
    stone.owner_index = 0;
    s.battlefield.push_back(stone);
    s.players[0].library.push_back(Copy("Time Warp", 3));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(std::any_of(plans.begin(), plans.end(), [](const auto& p)
    {
        return p.actions.size() == 1
            && p.actions.front().kind == Action::Kind::DigDraw
            && p.actions.front().dig_sacrifice
            && p.actions.front().card_name == "Mind Stone";
    }));
}

TEST_CASE("Part the Waterveil awaken persists and keeps land abilities")
{
    EnsureCards();
    GameState s;
    Land(s, 4);
    StackEntry e = Spell("Part the Waterveil", 78);
    e.awaken = true;
    e.enchant_target = 4;
    CHECK(EffectHandler::Resolve(s, e, Def("Part the Waterveil")));
    CHECK(s.pending_extra_turns == 1);
    REQUIRE(s.battlefield.size() == 1);
    const Permanent& p = s.battlefield.front();
    CHECK(p.card.IsLand());
    CHECK(p.card.IsCreature());
    CHECK(CardHasSubtype(p.card, "Elemental"));
    CHECK(p.card.HasKeyword(Keyword::Haste));
    CHECK(p.EffectivePower() == 6);
    CHECK(p.EffectiveToughness() == 6);
    CHECK(p.CanAttack());
    CHECK(s.exile.size() == 1);
}

TEST_CASE("Part the Waterveil with illegal awaken target fails completely")
{
    EnsureCards();
    GameState s;
    StackEntry e = Spell("Part the Waterveil", 79);
    e.awaken = true;
    e.enchant_target = 4;
    CHECK(EffectHandler::Resolve(s, e, Def("Part the Waterveil")));
    CHECK(s.pending_extra_turns == 0);
    CHECK(s.exile.empty());
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard.front().m_number == 79);
}

TEST_CASE("Expropriate money respects ownership and keeps tapped state")
{
    EnsureCards();
    GameState s;
    Land(s, 1, 1);
    Land(s, 2, 1);
    s.battlefield[0].tapped = true;
    s.battlefield[0].controller_index = 0; // already stolen, still owned by voter
    ResolveExpropriate(s, 0, true, 2);
    CHECK(s.pending_extra_turns == 1); // own time; passive opponent money
    CHECK(s.battlefield[0].owner_index == 1);
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].owner_index == 1);
    CHECK(s.battlefield[1].controller_index == 0);
    CHECK(s.battlefield[1].gained_control_this_turn);
}
