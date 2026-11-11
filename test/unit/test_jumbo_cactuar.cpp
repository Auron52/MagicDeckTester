#include <doctest/doctest.h>
#include <algorithm>
#include <set>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace
{
void LoadJumboCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CopyOf(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c;
    c.m_name = d->card.m_name;
    c.m_number = number;
    c.RehashName();
    return c;
}

int PutOnBoard(GameState& s, const char* name, int number, bool tapped = false)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
}

TEST_CASE("Tooth and Nail entwine puts Surrak and Cactuar simultaneously for immediate lethal")
{
    LoadJumboCards();
    GameState s;
    s.active_player_index = 0;
    s.deck_has_creature_enter_watcher = true;
    s.players[0].library.push_back(CopyOf("Jumbo Cactuar", 101));
    s.players[0].library.push_back(CopyOf("Surrak and Goreclaw", 102));
    s.players[0].library.push_back(CopyOf("Voyaging Satyr", 103));

    ResolveToothAndNail(s, 0, 2,
        EncodeToothChoices({"Jumbo Cactuar", "Surrak and Goreclaw"},
                           {"Jumbo Cactuar", "Surrak and Goreclaw"}));

    int cactuar = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_name.str() == "Jumbo Cactuar") { cactuar = i; } }
    REQUIRE(cactuar >= 0);
    const Permanent& body = s.battlefield[static_cast<std::size_t>(cactuar)];
    CHECK(body.temp_haste);
    CHECK(PlusCountersOn(body) == 1);
    CHECK(CanAttackFull(body, s.battlefield, 0));

    ApplyAttackSelfPumps(s, 0, {cactuar});
    CHECK(s.battlefield[static_cast<std::size_t>(cactuar)].EffectivePower() == 10001);
    const CombatDamageResult damage = ResolveCombatDamage(s, {cactuar}, 0, false);
    CHECK(damage.total_damage >= 10000);
}

TEST_CASE("Tooth and Nail viewer variants ask for entwine without an X choice")
{
    LoadJumboCards();
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.players[0].hand.push_back(CopyOf("Tooth and Nail", 130));
    s.players[0].library.push_back(CopyOf("Jumbo Cactuar", 131));
    s.players[0].library.push_back(CopyOf("Surrak and Goreclaw", 132));
    for (int i = 0; i < 9; ++i) { PutOnBoard(s, "Forest", 140 + i); }
    TurnSolver::LineSpec line;
    line.casts = {"Tooth and Nail"};
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const auto match = TurnSolver::CheckLine(s, true, line, &plans);
    REQUIRE(match.verdict == TurnSolver::LineCheck::Verdict::Choose);
    REQUIRE(match.variants.size() == 3);
    std::set<std::string> modes;
    for (const auto& variant : match.variants)
    {
        REQUIRE(variant.subs.size() == 1);
        CHECK(variant.subs[0].key == "Tooth and Nail entwine");
        CHECK(variant.subs[0].kind == "modal");
        modes.insert(variant.subs[0].choice);
    }
    const std::set<std::string> expected = {
        "No — search two to hand", "No — put two from hand",
        "Yes — pay {2}, search, then put"};
    CHECK(modes == expected);
}

TEST_CASE("Jumbo Cactuar provider finds the missing entwine piece already in the library")
{
    LoadJumboCards();
    GameState s;
    s.players[0].hand.push_back(CopyOf("Jumbo Cactuar", 201));
    s.players[0].library.push_back(CopyOf("Surrak and Goreclaw", 202));
    JumboCactuarProvider provider;
    CHECK(provider.ToothAndNailCandidateOk(s, 0, 2,
        {"Surrak and Goreclaw"}, {"Jumbo Cactuar", "Surrak and Goreclaw"}));
    CHECK_FALSE(provider.ToothAndNailCandidateOk(s, 0, 2,
        {"Voyaging Satyr"}, {"Jumbo Cactuar", "Voyaging Satyr"}));
    CHECK(provider.ToothAndNailCandidateOk(s, 0, 0,
        {"Voyaging Satyr"}, {}));
}

TEST_CASE("Jumbo Cactuar tutor uses Arbor Elf rather than Satyr on stacked Forests")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    PutOnBoard(s, "Forest", 221);
    PutOnBoard(s, "Utopia Sprawl", 222);
    s.battlefield.back().aura_attached_to = 221;
    PutOnBoard(s, "Utopia Sprawl", 223);
    s.battlefield.back().aura_attached_to = 221;
    const auto policy = provider.PutTargetPolicy(s, 0);
    const CardDefinition* elf = CardDatabase::Instance().Lookup("Arbor Elf");
    const CardDefinition* satyr = CardDatabase::Instance().Lookup("Voyaging Satyr");
    REQUIRE(elf != nullptr);
    REQUIRE(satyr != nullptr);
    CHECK(provider.PutTargetOk(policy, *elf));
    CHECK_FALSE(provider.PutTargetOk(policy, *satyr));
}

TEST_CASE("Jumbo Cactuar has a unique cast rank for every mainboard card")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    const char* names[] = {
        "Forest", "Wild Growth", "Utopia Sprawl", "Overgrowth",
        "Arbor Elf", "Voyaging Satyr", "Lightning Greaves",
        "Chariot of Victory", "Nylea, God of the Hunt", "Tooth and Nail",
        "Surrak and Goreclaw", "Green Sun's Zenith",
        "Finale of Devastation", "Jumbo Cactuar"
    };
    std::set<int> ranks;
    for (const char* name : names)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        CHECK(ranks.insert(provider.CastOrderRank(s, *d)).second);
    }
    CHECK(ranks.size() == 14);
    const auto rank = [&](const char* name)
    { return provider.CastOrderRank(s, *CardDatabase::Instance().Lookup(name)); };
    CHECK(rank("Wild Growth") < rank("Overgrowth"));
    CHECK(rank("Utopia Sprawl") < rank("Overgrowth"));
    CHECK(rank("Lightning Greaves") < rank("Arbor Elf"));
    CHECK(rank("Voyaging Satyr") < rank("Chariot of Victory"));
    CHECK(rank("Surrak and Goreclaw") < rank("Jumbo Cactuar"));
    // USER 2026-11-11: Surrak's default is after the untappers and Chariot, before Tooth and Nail.
    CHECK(rank("Chariot of Victory") < rank("Surrak and Goreclaw"));
    CHECK(rank("Surrak and Goreclaw") < rank("Tooth and Nail"));
}

namespace
{
Action JumboCast(GameState& s, const char* name, int number, int host = 0, const char* target = nullptr)
{
    s.players[0].hand.push_back(CopyOf(name, number));
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def = d;
    a.cost = d->card.m_mana_cost;
    a.enchant_target = host;
    a.is_noncreature = !d->card.IsCreature();
    if (target != nullptr) { a.tutor_target = target; }
    return a;
}

std::vector<std::string> ArrangedNames(GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> order(acts.size());
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) { order[static_cast<std::size_t>(i)] = i; }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, order);
    std::vector<std::string> names;
    for (int i : order)
    {
        const Action& a = acts[static_cast<std::size_t>(i)];
        names.push_back(a.tutor_target.str().empty() ? a.card_name.str()
                                                     : a.card_name.str() + "->" + a.tutor_target.str());
    }
    return names;
}

GameState JumboBoard(JumboCactuarProvider& provider, int forests)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 5;
    s.deck_has_land_aura = true;
    s.deck_has_creature_enter_watcher = true;
    s.m_provider = &provider;
    for (int i = 0; i < forests; ++i) { PutOnBoard(s, "Forest", 10 + i); }
    return s;
}
}

// USER 2026-11-11: a tutor ranked by its target "should always go after the cheaper cast-from-hand option".
TEST_CASE("Jumbo Cactuar ranks a targeted tutor after the hand copy of its target")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s = JumboBoard(provider, 0);
    const auto rank = [&](const char* name)
    { return provider.CastOrderRank(s, *CardDatabase::Instance().Lookup(name)); };
    for (const char* tutor : {"Green Sun's Zenith", "Finale of Devastation"})
    {
        for (const char* target : {"Arbor Elf", "Surrak and Goreclaw", "Jumbo Cactuar"})
        {
            Action a = JumboCast(s, tutor, 90, 0, target);
            const int r = provider.CastOrderActionRank(s, a, rank(tutor));
            CHECK(r > rank(target));
        }
    }
    Action elf_tutor = JumboCast(s, "Green Sun's Zenith", 91, 0, "Arbor Elf");
    CHECK(provider.CastOrderActionRank(s, elf_tutor, rank("Green Sun's Zenith")) < rank("Voyaging Satyr"));
    // Hand Elf, then the Zenith fetching the second Elf.
    const std::vector<Action> acts{JumboCast(s, "Green Sun's Zenith", 92, 0, "Arbor Elf"),
                                   JumboCast(s, "Arbor Elf", 2)};
    CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Arbor Elf", "Green Sun's Zenith->Arbor Elf"});
}

// USER 2026-11-11: Chariot after the FIRST untapper only when two or more untappers enter in the line.
TEST_CASE("Jumbo Cactuar casts Chariot after the first untapper only in a multi-untapper line")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    {
        GameState s = JumboBoard(provider, 8);
        const std::vector<Action> acts{JumboCast(s, "Chariot of Victory", 5), JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Arbor Elf", "Chariot of Victory"});
    }
    {
        GameState s = JumboBoard(provider, 8);
        const std::vector<Action> acts{JumboCast(s, "Chariot of Victory", 5), JumboCast(s, "Voyaging Satyr", 53),
                                       JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts)
              == std::vector<std::string>{"Arbor Elf", "Chariot of Victory", "Voyaging Satyr"});
    }
    {
        GameState s = JumboBoard(provider, 8);
        const std::vector<Action> acts{JumboCast(s, "Chariot of Victory", 5),
                                       JumboCast(s, "Green Sun's Zenith", 92, 0, "Arbor Elf"),
                                       JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Arbor Elf", "Chariot of Victory",
                                                                 "Green Sun's Zenith->Arbor Elf"});
    }
}

// USER 2026-11-11: Surrak "earlier only if mana allows" -- ahead of the untappers it hastes only when the
// exact sequential walk pays every cast in that order.
TEST_CASE("Jumbo Cactuar moves Surrak ahead of an untapper only when the earlier order pays")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    {
        GameState s = JumboBoard(provider, 8);
        const std::vector<Action> acts{JumboCast(s, "Surrak and Goreclaw", 43), JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Surrak and Goreclaw", "Arbor Elf"});
    }
    {
        // Six Forests pay Surrak but not Surrak + Elf: the earlier order is unproven, so the default stands.
        GameState s = JumboBoard(provider, 6);
        const std::vector<Action> acts{JumboCast(s, "Surrak and Goreclaw", 43), JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Arbor Elf", "Surrak and Goreclaw"});
    }
    {
        // Surrak goes ahead of the FIRST untapper; Chariot stays right after it.
        GameState s = JumboBoard(provider, 12);
        const std::vector<Action> acts{JumboCast(s, "Surrak and Goreclaw", 43), JumboCast(s, "Chariot of Victory", 5),
                                       JumboCast(s, "Voyaging Satyr", 53), JumboCast(s, "Arbor Elf", 2)};
        CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Surrak and Goreclaw", "Arbor Elf",
                                                                 "Chariot of Victory", "Voyaging Satyr"});
    }
}

// USER 2026-11-11: "I don't understand how Overgrowth ever goes before Wild Growth, I don't think it should."
TEST_CASE("Jumbo Cactuar never casts Overgrowth before a same-host Wild Growth")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s = JumboBoard(provider, 1);
    PutOnBoard(s, "Voyaging Satyr", 53);
    PutOnBoard(s, "Voyaging Satyr", 54);
    const std::vector<Action> acts{JumboCast(s, "Tooth and Nail", 60), JumboCast(s, "Overgrowth", 39, 10),
                                   JumboCast(s, "Wild Growth", 57, 10)};
    CHECK(ArrangedNames(s, acts) == std::vector<std::string>{"Wild Growth", "Overgrowth", "Tooth and Nail"});
}

TEST_CASE("Jumbo Cactuar moves Overgrowth before Sprawl when only that order funds the payoffs")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.deck_has_land_aura = true;
    s.m_provider = &provider;
    PutOnBoard(s, "Forest", 10);
    PutOnBoard(s, "Utopia Sprawl", 49);
    s.battlefield.back().aura_attached_to = 10;
    s.battlefield.back().chosen_color = static_cast<int8_t>(Color::Green);
    PutOnBoard(s, "Voyaging Satyr", 53);
    PutOnBoard(s, "Wild Growth", 57);
    s.battlefield.back().aura_attached_to = 10;
    PutOnBoard(s, "Voyaging Satyr", 54);

    auto cast = [&](const char* name, int number, int host = 0)
    {
        s.players[0].hand.push_back(CopyOf(name, number));
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        Action a;
        a.card_name = name;
        a.def = d;
        a.cost = d->card.m_mana_cost;
        a.enchant_target = host;
        a.is_noncreature = !d->card.IsCreature();
        return a;
    };
    const std::vector<Action> acts{
        cast("Tooth and Nail", 60), cast("Overgrowth", 39, 10),
        cast("Finale of Devastation", 61), cast("Overgrowth", 40, 10),
        cast("Utopia Sprawl", 50, 10)};
    std::vector<int> order{0, 1, 2, 3, 4};
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(s, acts[x], acts[y]); });
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, acts, order));
    ApplyDeckCastArrangement(s, acts, order);
    CHECK(order == std::vector<int>{1, 3, 4, 0, 2});
    CHECK(TurnSolver::OrderPaysSequentially(s, acts, order));
}

TEST_CASE("Jumbo Cactuar enumerates a newly hasted Arbor Elf funding Tooth and Nail")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.deck_has_land_aura = true;
    s.deck_has_creature_enter_watcher = true;
    s.m_provider = &provider;
    PutOnBoard(s, "Forest", 27);
    PutOnBoard(s, "Arbor Elf", 4);
    PutOnBoard(s, "Forest", 10);
    PutOnBoard(s, "Utopia Sprawl", 52);
    s.battlefield.back().aura_attached_to = 10;
    s.battlefield.back().chosen_color = static_cast<int8_t>(Color::Green);
    PutOnBoard(s, "Overgrowth", 40);
    s.battlefield.back().aura_attached_to = 10;
    s.players[0].hand.push_back(CopyOf("Tooth and Nail", 70));
    s.players[0].hand.push_back(CopyOf("Surrak and Goreclaw", 44));
    s.players[0].hand.push_back(CopyOf("Forest", 17));
    s.players[0].hand.push_back(CopyOf("Arbor Elf", 2));
    s.players[0].library.push_back(CopyOf("Jumbo Cactuar", 33));

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const bool found = std::any_of(plans.begin(), plans.end(), [](const TurnSolver::Plan& p)
    {
        if (p.land_to_play != "Forest") { return false; }
        bool tooth = false, surrak = false, elf = false;
        for (const Action& a : p.actions)
        {
            tooth |= a.card_name.str() == "Tooth and Nail";
            surrak |= a.card_name.str() == "Surrak and Goreclaw";
            elf |= a.card_name.str() == "Arbor Elf";
        }
        return tooth && surrak && elf;
    });
    CHECK(found);
}

TEST_CASE("Jumbo Zenith X payment retains green for a named Finale")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.deck_has_land_aura = true;
    s.deck_has_mana_untapper = true;
    s.m_provider = &provider;
    PutOnBoard(s, "Forest", 10);
    PutOnBoard(s, "Arbor Elf", 1);
    PutOnBoard(s, "Utopia Sprawl", 49);
    s.battlefield.back().aura_attached_to = 10;
    s.battlefield.back().chosen_color = static_cast<int8_t>(Color::White);
    PutOnBoard(s, "Voyaging Satyr", 53);
    PutOnBoard(s, "Forest", 11, true); // paid the preceding Sprawl
    PutOnBoard(s, "Utopia Sprawl", 50);
    s.battlefield.back().aura_attached_to = 10;
    s.battlefield.back().chosen_color = static_cast<int8_t>(Color::White);

    ManaCost zenith = CardDatabase::Instance().Lookup("Green Sun's Zenith")->card.m_mana_cost;
    zenith.generic = 6;
    const ManaCost finale = CardDatabase::Instance().Lookup("Finale of Devastation")->card.m_mana_cost;
    ManaCost line = zenith;
    AddManaCost(line, finale);
    LineUnpaidCostScope line_hold(line);
    LineOpensUnseenScope opener(false);
    CHECK(TapForCostDirect(s, zenith, false));
    CHECK(s.floating_mana.green >= 2);
    CHECK(TapForCostDirect(s, finale, false));
}

TEST_CASE("Finale graveyard-only whiff preserves library order and still pumps at X ten")
{
    LoadJumboCards();
    GameState s;
    s.players[0].library.push_back(CopyOf("Forest", 301));
    s.players[0].library.push_back(CopyOf("Voyaging Satyr", 302));
    Permanent body;
    body.card = CardDatabase::Instance().Lookup("Jumbo Cactuar")->card;
    body.card.m_number = 303;
    body.controller_index = 0;
    body.owner_index = 0;
    body.entered_this_turn = true;
    s.battlefield.push_back(body);
    const CardDefinition* finale = CardDatabase::Instance().Lookup("Finale of Devastation");
    REQUIRE(finale != nullptr);

    ResolveFinaleOfDevastation(s, 0, finale->params, 10, "Z");

    CHECK(s.players[0].library[0].m_number == 301);
    CHECK(s.players[0].library[1].m_number == 302);
    CHECK(s.battlefield[0].temp_power_bonus == 10);
    CHECK(s.battlefield[0].temp_tough_bonus == 10);
    CHECK(s.battlefield[0].temp_haste);
}

TEST_CASE("Voyaging Satyr untaps the selected land and no other land")
{
    LoadJumboCards();
    GameState s;
    const int satyr = PutOnBoard(s, "Voyaging Satyr", 401);
    const int first = PutOnBoard(s, "Forest", 402, true);
    const int second = PutOnBoard(s, "Forest", 403, true);
    TapTargetPumpPinScope chosen(403, 0);
    REQUIRE(PermAbilitySourceLive(s, 0, 401, PermAbilityMode::UntapLand));
    SetPermTapped(s, 0, 401, true);
    ApplyPermAbility(s, 0, 401, PermAbilityMode::UntapLand);
    CHECK(s.battlefield[satyr].tapped);
    CHECK(s.battlefield[first].tapped);
    CHECK_FALSE(s.battlefield[second].tapped);
}

TEST_CASE("Arbor Elf and Voyaging Satyr choose the highest-yield enchanted Forest")
{
    LoadJumboCards();
    GameState s;
    s.deck_has_land_aura = true;
    PutOnBoard(s, "Forest", 411);
    PutOnBoard(s, "Forest", 412);
    PutOnBoard(s, "Wild Growth", 413);
    s.battlefield.back().aura_attached_to = 411;
    PutOnBoard(s, "Overgrowth", 414);
    s.battlefield.back().aura_attached_to = 412;
    PutOnBoard(s, "Wild Growth", 415);
    s.battlefield.back().aura_attached_to = 412;
    const int elf = PutOnBoard(s, "Arbor Elf", 416);
    const int satyr = PutOnBoard(s, "Voyaging Satyr", 417);

    UntapLandHosts hosts;
    REQUIRE(UntapLandHostsOf(s, s.battlefield[elf], hosts) == 1);
    CHECK(hosts.host[0]->card.m_number == 412);
    CHECK(LandAuraBonus(s, s.battlefield[elf]) == 3);
    REQUIRE(UntapLandHostsOf(s, s.battlefield[satyr], hosts) == 1);
    CHECK(hosts.host[0]->card.m_number == 412);
    CHECK(LandAuraBonus(s, s.battlefield[satyr]) == 3);
}

TEST_CASE("Nylea's devotion gate and targeted pump follow the live creature set")
{
    LoadJumboCards();
    GameState s;
    const int nylea = PutOnBoard(s, "Nylea, God of the Hunt", 501);
    for (int i = 0; i < 4; ++i) { PutOnBoard(s, "Voyaging Satyr", 502 + i); }
    RefreshDevotionCreatures(s);
    REQUIRE(s.battlefield[nylea].card.IsCreature());
    {
        TapTargetPumpPinScope chosen(501, 0);
        REQUIRE(PermAbilitySourceLive(s, 0, 501, PermAbilityMode::TargetPump));
        ApplyPermAbility(s, 0, 501, PermAbilityMode::TargetPump);
    }
    CHECK(s.battlefield[nylea].temp_power_bonus == 2);
    CHECK(s.battlefield[nylea].temp_tough_bonus == 2);

    s.battlefield.pop_back();
    RefreshDevotionCreatures(s);
    CHECK_FALSE(s.battlefield[nylea].card.IsCreature());
    {
        TapTargetPumpPinScope chosen(502, 0);
        REQUIRE(PermAbilitySourceLive(s, 0, 501, PermAbilityMode::TargetPump));
        ApplyPermAbility(s, 0, 501, PermAbilityMode::TargetPump);
    }
    CHECK(s.battlefield[1].temp_power_bonus == 2);
}

TEST_CASE("Zenith's X-capped human tutor cannot borrow Chord's spare convoke")
{
    LoadJumboCards();
    GameState s;
    s.active_player_index = 0;
    s.players[0].library.push_back(CopyOf("Arbor Elf", 601));
    s.players[0].library.push_back(CopyOf("Voyaging Satyr", 602));
    const int elf = PutOnBoard(s, "Arbor Elf", 603);
    const CardDefinition* zenith = CardDatabase::Instance().Lookup("Green Sun's Zenith");
    REQUIRE(zenith != nullptr);

    std::vector<std::string> offered;
    TutorChooser chooser = [&](const GameState&, int, const std::string&,
                               const std::vector<std::string>& names, int,
                               const std::vector<int>&) -> int
    { offered = names; return 0; };
    TutorChooser* saved = g_play_tutor_chooser;
    g_play_tutor_chooser = &chooser;
    PerformTutorToBattlefield(s, 0, zenith->params, 1, {"Arbor Elf"},
                              "Green Sun's Zenith", -1, 1);
    g_play_tutor_chooser = saved;

    CHECK(offered == std::vector<std::string>{"Arbor Elf"});
    CHECK_FALSE(s.battlefield[elf].tapped);
}

namespace
{
bool HasPlanWith(const std::vector<TurnSolver::Plan>& plans, const std::vector<std::string>& casts, bool want_equip)
{
    return std::any_of(plans.begin(), plans.end(), [&](const TurnSolver::Plan& p)
    {
        std::multiset<std::string> got;
        bool equip = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand) { got.insert(a.card_name.str()); }
            if (a.kind == Action::Kind::Equip) { equip = true; }
        }
        for (const std::string& c : casts) { if (got.count(c) == 0) { return false; } }
        return equip == want_equip;
    });
}
}

// Mana-clean gate jumbo-sprawlgreen-20261110, ENUM-GAP s30027377 T3: Chariot of Victory's haste equip onto a
// freshly cast Voyaging Satyr is a MANA UNLOCK (ApplyManaUnlockEquips fires it before Tooth and Nail), and the
// hasted Satyr untapping the stacked Forest is what pays Tooth and Nail. The sequential walk refused every
// subset with a paid equip, so the gate never offered the line.
TEST_CASE("Jumbo Cactuar enumerates Chariot hasting a fresh Satyr that funds Tooth and Nail")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.deck_has_land_aura = true;
    s.m_provider = &provider;
    PutOnBoard(s, "Forest", 10);
    PutOnBoard(s, "Arbor Elf", 1);
    PutOnBoard(s, "Wild Growth", 57);
    s.battlefield.back().aura_attached_to = 10;
    PutOnBoard(s, "Arbor Elf", 2);
    PutOnBoard(s, "Arbor Elf", 3);
    s.players[0].hand.push_back(CopyOf("Overgrowth", 39));
    s.players[0].hand.push_back(CopyOf("Chariot of Victory", 5));
    s.players[0].hand.push_back(CopyOf("Tooth and Nail", 70));
    s.players[0].hand.push_back(CopyOf("Voyaging Satyr", 53));
    s.players[0].library.push_back(CopyOf("Jumbo Cactuar", 33));

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(HasPlanWith(plans, {"Overgrowth", "Chariot of Victory", "Tooth and Nail", "Voyaging Satyr"}, true));
}

// ENUM-GAP s30025119 T4: Overgrowth is paid off its own (declared) Forest, which leaves that Forest tapped; the
// Aura still enchants it, and a Surrak-hasted Arbor Elf untaps it for Tooth and Nail WITH the +2. The walk used
// to attach a land Aura only to an UNTAPPED host, so it priced the Forest without the Overgrowth.
TEST_CASE("Jumbo Cactuar walk keeps a land Aura on its tapped declared host")
{
    LoadJumboCards();
    JumboCactuarProvider provider;
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.deck_has_land_aura = true;
    s.deck_has_creature_enter_watcher = true;
    s.m_provider = &provider;
    PutOnBoard(s, "Forest", 10);
    PutOnBoard(s, "Utopia Sprawl", 49);
    s.battlefield.back().aura_attached_to = 10;
    s.battlefield.back().chosen_color = static_cast<int8_t>(Color::Green);
    PutOnBoard(s, "Forest", 11);
    PutOnBoard(s, "Overgrowth", 39);
    s.battlefield.back().aura_attached_to = 10;
    PutOnBoard(s, "Forest", 12);
    PutOnBoard(s, "Surrak and Goreclaw", 43);
    s.players[0].hand.push_back(CopyOf("Tooth and Nail", 70));
    s.players[0].hand.push_back(CopyOf("Overgrowth", 40));
    s.players[0].hand.push_back(CopyOf("Arbor Elf", 2));
    s.players[0].library.push_back(CopyOf("Jumbo Cactuar", 33));

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const bool found = std::any_of(plans.begin(), plans.end(), [](const TurnSolver::Plan& p)
    {
        bool tooth = false, elf = false, over10 = false;
        for (const Action& a : p.actions)
        {
            tooth  |= a.card_name.str() == "Tooth and Nail" && a.chosen_x != 2;
            elf    |= a.card_name.str() == "Arbor Elf";
            over10 |= a.card_name.str() == "Overgrowth" && a.enchant_target == 10;
        }
        return tooth && elf && over10;
    });
    CHECK(found);
}
