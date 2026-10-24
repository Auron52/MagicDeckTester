// Unit tests for the LAND-SEARCH cluster (core/LandSearch.h, 2026-10-15): Rampant Growth, Search for
// Tomorrow (incl. Suspend 2--{G}), Cultivate, Evolving Wilds / Terramorphic Expanse, Myriad Landscape,
// Khalni Heart Expedition's activation, Primeval Titan (ETB + attack), and Haunted Fengraf's random
// graveyard return (+ the GyR_CreatureNames dominance projection it and Haven share).
//
// What is pinned:
//  * the pick generator: basic-only via the Basic SUPERTYPE, identity folds (copies; Evolving Wilds ==
//    Terramorphic Expanse by behaviour_identity), maximal "up to" picks, Myriad's shared land type,
//    Cultivate's ordered (battlefield, hand) splits, Titan's any-land pairs;
//  * the resolution: forced-tapped entry (no shock life), ONE shuffle (also on a whiff), hand entry,
//    the pin indexes the list and clamps, landfall fires per land (Khalni counts them);
//  * the activations: Khalni's counter cost + sacrifice, the stale-plan no-op;
//  * suspend: the {G} is paid, the off-suspend sorcery resolves and goes to the graveyard;
//  * Titan's attack trigger repairs attacker indices after a fetched Gruul Turf's bounce;
//  * Fengraf's draw is a function of the eligible multiset (storage order does not matter);
//  * Harrow pays its sacrifice-a-land cost BEFORE its search, in both worlds; Explosive Vegetation's two
//    basics are tapped; Nature's Lore / Skyshroud Claim find "Forest cards" (the subtype, basic or not),
//    and the Landfall provider does not mistake them for Primeval Titan's any-land search.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "runner/GoldFishRunner.h"
#include "ai/AIEngine.h"
#include "core/EffectHandler.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void LoadCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int Put(GameState& s, const std::string& name, int number, bool tapped, bool sick = false)
{
    Permanent p;
    p.card              = D(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void Lib(GameState& s, const std::string& name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

void Grave(GameState& s, const std::string& name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    s.players[0].graveyard.push_back(c);
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 12345;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int CountOnBf(const GameState& s, const std::string& name, bool tapped)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == name && p.tapped == tapped) { ++n; } }
    return n;
}

int CountIn(const std::vector<Card>& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

int LibCount(const GameState& s, const std::string& name)
{
    int n = 0;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i)
    { if (s.players[0].library[i].m_name.str() == name) { ++n; } }
    return n;
}

using Picks = std::vector<std::vector<std::string>>;

bool HasPick(const Picks& p, std::vector<std::string> want)
{
    return std::find(p.begin(), p.end(), want) != p.end();
}

}   // namespace

TEST_CASE("Land search: basic-only picks read the Basic SUPERTYPE and fold copies")
{
    LoadCards();
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52);
    Lib(s, "Gruul Turf", 53); Lib(s, "Radiant Fountain", 54); Lib(s, "Evolving Wilds", 55);
    const Picks rg = LandSearchPicks(s, 0, LandSearchSpecOf(D("Rampant Growth").params));
    CHECK(rg.size() == 2);   // Forest, Mountain -- the Turf / Fountain / Wilds are NOT basic
    CHECK(HasPick(rg, { "Forest" }));
    CHECK(HasPick(rg, { "Mountain" }));
    // Mountain is basic only because of its supertype (it used to lack it).
    CHECK(IsBasicLandCard(D("Mountain")));
    CHECK_FALSE(IsBasicLandCard(D("Gruul Turf")));
    CHECK_FALSE(IsBasicLandCard(D("Radiant Fountain")));
}

TEST_CASE("Land search: Cultivate emits every ordered (battlefield, hand) split; a lone find goes to the battlefield")
{
    LoadCards();
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52); Lib(s, "Mountain", 53);
    const LandSearchSpec sp = LandSearchSpecOf(D("Cultivate").params);
    const Picks p = LandSearchPicks(s, 0, sp);
    CHECK(p.size() == 4);
    CHECK(HasPick(p, { "Forest", "Forest" }));
    CHECK(HasPick(p, { "Forest", "Mountain" }));   // Forest onto the battlefield, Mountain to hand
    CHECK(HasPick(p, { "Mountain", "Forest" }));
    CHECK(HasPick(p, { "Mountain", "Mountain" }));
    // One Mountain only: (M, M) is gone, the others stay.
    GameState s2 = Fresh();
    Lib(s2, "Forest", 50); Lib(s2, "Forest", 51); Lib(s2, "Mountain", 52);
    CHECK(LandSearchPicks(s2, 0, sp).size() == 3);
    CHECK(LandSearchBattlefieldCount(sp, 1) == 1);   // ruling 2010-08-15
    CHECK(LandSearchBattlefieldCount(sp, 2) == 1);
}

TEST_CASE("Land search: Myriad Landscape's picks share a land type and are maximal")
{
    LoadCards();
    const LandSearchSpec sp = LandSearchSpecOf(D("Myriad Landscape").params);
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52); Lib(s, "Mountain", 53);
    Picks p = LandSearchPicks(s, 0, sp);
    CHECK(p.size() == 2);
    CHECK(HasPick(p, { "Forest", "Forest" }));
    CHECK(HasPick(p, { "Mountain", "Mountain" }));
    // A lone Mountain: {Mountain} is maximal (no other Mountain; a Forest shares no type with it).
    GameState s2 = Fresh();
    Lib(s2, "Forest", 50); Lib(s2, "Forest", 51); Lib(s2, "Mountain", 52);
    p = LandSearchPicks(s2, 0, sp);
    CHECK(p.size() == 2);
    CHECK(HasPick(p, { "Forest", "Forest" }));
    CHECK(HasPick(p, { "Mountain" }));
}

TEST_CASE("Land search: Khalni / Titan pick lists, and Evolving Wilds == Terramorphic Expanse")
{
    LoadCards();
    CHECK(D("Evolving Wilds").behaviour_identity == D("Terramorphic Expanse").behaviour_identity);
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52); Lib(s, "Mountain", 53);
    Lib(s, "Gruul Turf", 54); Lib(s, "Evolving Wilds", 55); Lib(s, "Terramorphic Expanse", 56);
    const Picks k = LandSearchPicks(s, 0, LandSearchSpecOf(D("Khalni Heart Expedition").params));
    CHECK(k.size() == 3);   // {F,F} {F,M} {M,M}
    const Picks t = LandSearchPicks(s, 0, LandSearchSpecOf(D("Primeval Titan").params));
    // Classes F(2) M(2) T(1) W(2: Wilds + Terramorphic folded): FF FM FT FW MM MT MW TW WW = 9.
    CHECK(t.size() == 9);
    CHECK(HasPick(t, { "Evolving Wilds", "Evolving Wilds" }));   // the folded pair (one of each)
    CHECK_FALSE(HasPick(t, { "Gruul Turf", "Gruul Turf" }));     // one copy only
}

TEST_CASE("Land search: Rampant Growth puts a TAPPED basic, shuffles once, never uses the land drop")
{
    LoadCards();
    GameState s = Fresh();
    Put(s, "Forest", 10, false);
    Lib(s, "Mountain", 50); Lib(s, "Forest", 51); Lib(s, "Forest", 52);
    const int sc0 = s.search_count;
    // Pin 0 = the provider's front; find the Mountain explicitly by index instead.
    const Picks p = LandSearchPicks(s, 0, LandSearchSpecOf(D("Rampant Growth").params));
    int mi = -1;
    for (int i = 0; i < static_cast<int>(p.size()); ++i) { if (p[static_cast<std::size_t>(i)] == std::vector<std::string>{ "Mountain" }) { mi = i; } }
    REQUIRE(mi >= 0);
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Rampant Growth").params), "Rampant Growth", mi);
    CHECK(CountOnBf(s, "Mountain", /*tapped=*/true) == 1);
    CHECK(LibCount(s, "Mountain") == 0);
    CHECK(s.search_count == sc0 + 1);
    CHECK(s.players[0].lands_played_this_turn == 0);
    // An out-of-range pin clamps to the last pick (duplicate-not-whiff).
    GameState s2 = Fresh();
    Lib(s2, "Forest", 51);
    PerformLandSearch(s2, 0, LandSearchSpecOf(D("Rampant Growth").params), "Rampant Growth", 7);
    CHECK(CountOnBf(s2, "Forest", true) == 1);
}

TEST_CASE("Land search: a fail-to-find still shuffles exactly once")
{
    LoadCards();
    GameState s = Fresh();
    Lib(s, "Gruul Turf", 50);   // not basic
    const int sc0 = s.search_count;
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Rampant Growth").params), "Rampant Growth", -1);
    CHECK(s.search_count == sc0 + 1);
    CHECK(s.battlefield.empty());
    CHECK(LibCount(s, "Gruul Turf") == 1);
}

TEST_CASE("Land search: Search for Tomorrow's basic enters UNTAPPED; Cultivate's second basic goes to hand")
{
    LoadCards();
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51);
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Search for Tomorrow").params), "Search for Tomorrow", -1);
    CHECK(CountOnBf(s, "Forest", /*tapped=*/false) == 1);

    GameState c = Fresh();
    Lib(c, "Forest", 50); Lib(c, "Mountain", 51);
    const LandSearchSpec sp = LandSearchSpecOf(D("Cultivate").params);
    const Picks p = LandSearchPicks(c, 0, sp);
    int fm = -1;
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
    { if (p[static_cast<std::size_t>(i)] == std::vector<std::string>{ "Forest", "Mountain" }) { fm = i; } }
    REQUIRE(fm >= 0);
    PerformLandSearch(c, 0, sp, "Cultivate", fm);
    CHECK(CountOnBf(c, "Forest", /*tapped=*/true) == 1);
    CHECK(CountIn(c.players[0].hand, "Mountain") == 1);
    CHECK(c.players[0].library.size() == 0);
}

TEST_CASE("Land search: a forced-tapped put pays no shock life")
{
    LoadCards();
    // Primeval Titan may fetch ANY land; a shock land put "tapped" must not ask for 2 life.
    const CardDefinition* shock = CardDatabase::Instance().Lookup("Stomping Ground");
    if (shock == nullptr) { return; }   // not in the pool: nothing to pin
    GameState s = Fresh();
    Lib(s, "Stomping Ground", 50);
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Primeval Titan").params), "Primeval Titan", -1);
    CHECK(CountOnBf(s, "Stomping Ground", /*tapped=*/true) == 1);
    CHECK(s.players[0].life == 20);
}

TEST_CASE("Land search: each put land is a landfall event (Khalni counts them); the crack pays counters + sacrifice")
{
    LoadCards();
    GameState s = Fresh();
    const int k = Put(s, "Khalni Heart Expedition", 10, false);
    (void)k;
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Forest", 52); Lib(s, "Forest", 53);
    // Myriad's two Forests -> two landfall triggers.
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Myriad Landscape").params), "Myriad Landscape", -1);
    int qc = -1;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 10) { qc = p.quest_counters; } }
    CHECK(qc == 2);
    CHECK(CountOnBf(s, "Forest", true) == 2);
    // A stale crack (2 < 3 counters) is refused before anything is paid.
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::QuestSacLandSearch));
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 10) { p.quest_counters = 3; } }
    CHECK(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::QuestSacLandSearch));
    ApplyPermAbility(s, 0, 10, PermAbilityMode::QuestSacLandSearch);
    CHECK(CountIn(s.players[0].graveyard, "Khalni Heart Expedition") == 1);
    CHECK(CountOnBf(s, "Khalni Heart Expedition", false) == 0);
    CHECK(CountOnBf(s, "Forest", true) == 4);
}

TEST_CASE("Land search: Evolving Wilds' activation sacrifices it and fetches a tapped basic")
{
    LoadCards();
    GameState s = Fresh();
    Put(s, "Evolving Wilds", 10, /*tapped=*/true);   // the caller paid the {T}
    Lib(s, "Forest", 50);
    ApplyPermAbility(s, 0, 10, PermAbilityMode::SacLandSearch);
    CHECK(CountIn(s.players[0].graveyard, "Evolving Wilds") == 1);
    CHECK(CountOnBf(s, "Forest", true) == 1);
    // Wilds is a land with NO mana ability: no produced colour, not a basic_land template.
    CHECK(D("Evolving Wilds").params.produces.empty());
    CHECK(D("Evolving Wilds").tmpl != CardTemplate::BasicLand);
}

TEST_CASE("Land search: Search for Tomorrow -- the {G} suspend cost is paid; off suspend it resolves and goes to the graveyard")
{
    LoadCards();
    GameState s = Fresh();
    Put(s, "Forest", 10, false);
    Card sft = D("Search for Tomorrow").card; sft.m_number = 30;
    s.players[0].hand.push_back(sft);
    ApplySuspend(s, 0, "Search for Tomorrow");
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards[0].arrive_turn == s.turn_number + 2);
    CHECK(CountOnBf(s, "Forest", /*tapped=*/true) == 1);   // the {G} was paid
    // No green source: stranded -> stays in hand.
    GameState t = Fresh();
    t.players[0].hand.push_back(sft);
    ApplySuspend(t, 0, "Search for Tomorrow");
    CHECK(t.players[0].suspended_cards.empty());
    CHECK(CountIn(t.players[0].hand, "Search for Tomorrow") == 1);
    // The arrival: cast (storm +1), a basic onto the battlefield UNTAPPED, the card to the graveyard.
    s.turn_number += 2;
    Lib(s, "Forest", 50);
    const int casts0 = s.spells_cast_this_turn;
    ProcessSuspendArrivals(s, 0);
    CHECK(s.spells_cast_this_turn == casts0 + 1);
    CHECK(CountOnBf(s, "Forest", /*tapped=*/false) == 1);
    CHECK(CountIn(s.players[0].graveyard, "Search for Tomorrow") == 1);
    CHECK(CountOnBf(s, "Search for Tomorrow", false) == 0);
    CHECK(s.scripted_upkeep_land_search == -1);
}

TEST_CASE("Land search: the packed state pin is consumed one ordinal at a time and expires")
{
    int packed = (3) | (5 << kLandSearchPinBits);
    CHECK(ConsumePackedLandSearchPin(packed) == 3);
    CHECK(ConsumePackedLandSearchPin(packed) == 5);
    CHECK(packed == -1);
    CHECK(ConsumePackedLandSearchPin(packed) == -1);
    int zero_tail = 2;
    CHECK(ConsumePackedLandSearchPin(zero_tail) == 2);
    CHECK(zero_tail == -1);
}

TEST_CASE("Land search: Primeval Titan's ETB puts two lands tapped; its attack trigger repairs attacker indices after a Karoo bounce")
{
    LoadCards();
    {
        GameState s = Fresh();
        Lib(s, "Forest", 50); Lib(s, "Mountain", 51);
        const int t = Put(s, "Primeval Titan", 10, false, /*sick=*/true);
        FireOwnEtbTriggers(s, 0, t);
        CHECK(CountOnBf(s, "Forest", true) + CountOnBf(s, "Mountain", true) == 2);
    }
    {
        GameState s = Fresh();
        Put(s, "Forest", 9, /*tapped=*/true);              // index 0: a bounce candidate BEFORE the Titan
        const int t = Put(s, "Primeval Titan", 10, false);  // index 1
        Lib(s, "Gruul Turf", 50); Lib(s, "Mountain", 51);
        // Pick the {Gruul Turf, Mountain} pair explicitly.
        const Picks p = LandSearchPicks(s, 0, LandSearchSpecOf(D("Primeval Titan").params));
        int want = -1;
        for (int i = 0; i < static_cast<int>(p.size()); ++i)
        {
            const auto& v = p[static_cast<std::size_t>(i)];
            if (std::find(v.begin(), v.end(), "Gruul Turf") != v.end()
                && std::find(v.begin(), v.end(), "Mountain") != v.end()) { want = i; }
        }
        REQUIRE(want >= 0);
        s.scripted_attack_land_search = want;
        std::vector<int> atk{ t };
        FireAttackLandSearch(s, 0, atk);
        REQUIRE(atk.size() == 1);
        REQUIRE(atk[0] >= 0);
        CHECK(s.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == 10);   // still the Titan
        CHECK(CountOnBf(s, "Gruul Turf", true) == 1);
        CHECK(s.players[0].hand.size() == 1);   // the Turf's mandatory bounce returned a land
        CHECK(s.scripted_attack_land_search == -1);
    }
}

TEST_CASE("Haunted Fengraf: the random return is a function of the eligible MULTISET, not graveyard order")
{
    LoadCards();
    auto run = [](bool reversed) -> std::string
    {
        GameState s = Fresh();
        Put(s, "Haunted Fengraf", 10, false);
        if (!reversed)
        { Grave(s, "Avenger of Zendikar", 40); Grave(s, "Primeval Titan", 41); Grave(s, "Forest", 42); }
        else
        { Grave(s, "Forest", 42); Grave(s, "Primeval Titan", 41); Grave(s, "Avenger of Zendikar", 40); }
        ApplyGraveyardReturnAbility(s, 0, 10, kGyReturnRandom);
        if (s.players[0].hand.size() != 1) { return std::string("<none>"); }
        CHECK(CountIn(s.players[0].graveyard, "Haunted Fengraf") == 1);   // sacrificed as a cost
        return s.players[0].hand[0].m_name.str();
    };
    const std::string a = run(false);
    const std::string b = run(true);
    CHECK(a == b);
    CHECK((a == "Avenger of Zendikar" || a == "Primeval Titan"));   // never the Forest
    // With no creature card the activation does nothing at all (it would not have been offered).
    GameState s = Fresh();
    Put(s, "Haunted Fengraf", 10, false);
    Grave(s, "Forest", 42);
    ApplyGraveyardReturnAbility(s, 0, 10, kGyReturnRandom);
    CHECK(CountOnBf(s, "Haunted Fengraf", false) == 1);
}

TEST_CASE("Graveyard readers: Haven and Haunted Fengraf both set GyR_CreatureNames")
{
    LoadCards();
    Decklist fen;
    fen.mainboard.push_back(D("Haunted Fengraf").card);
    fen.mainboard.push_back(D("Forest").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(fen) & GyR_CreatureNames) != 0);
    Decklist haven;
    haven.mainboard.push_back(D("Haven of the Spirit Dragon").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(haven) & GyR_CreatureNames) != 0);
    Decklist plain;
    plain.mainboard.push_back(D("Forest").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(plain) & GyR_CreatureNames) == 0);
}

TEST_CASE("Land search: the IN-COMBAT Khalni crack is pinned by a bitmask, consumes the attack stream, and repairs attackers")
{
    LoadCards();
    GameState s = Fresh();
    Put(s, "Forest", 9, true);
    const int t = Put(s, "Primeval Titan", 10, false);
    Put(s, "Khalni Heart Expedition", 11, false);
    Put(s, "Khalni Heart Expedition", 12, false);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 11) { p.quest_counters = 3; } }
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 12) { p.quest_counters = 1; } }
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52); Lib(s, "Mountain", 53);
    std::vector<int> atk{ t };
    // No pin: nothing happens (autonomous; no chooser installed).
    FireCombatLandSearchCracks(s, 0, atk);
    CHECK(CountIn(s.players[0].graveyard, "Khalni Heart Expedition") == 0);
    // Bit 0 = the first Khalni in battlefield order (#11, eligible); bit 1 = #12 (1 counter: not
    // eligible, and #11's two landfalls make it 3 -- ascending order, so it IS eligible when reached).
    s.scripted_combat_land_search_crack = 3;
    FireCombatLandSearchCracks(s, 0, atk);
    CHECK(CountIn(s.players[0].graveyard, "Khalni Heart Expedition") == 2);
    CHECK(s.scripted_combat_land_search_crack == -1);
    REQUIRE(atk.size() == 1);
    CHECK(s.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == 10);
    CHECK(s.players[0].library.size() == 0);   // four basics fetched across the two cracks
}

TEST_CASE("Land search: Plan::land_search_act_slot -- the shared body cracks, the trailing list drops it")
{
    LoadCards();
    GameState s = Fresh();
    Put(s, "Forest", 9, false);
    Put(s, "Khalni Heart Expedition", 11, false);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 11) { p.quest_counters = 3; } }
    Lib(s, "Forest", 50); Lib(s, "Forest", 51);
    Action crack;
    crack.kind          = Action::Kind::ActivatePermAbility;
    crack.card_name     = "Khalni Heart Expedition";
    crack.def           = &D("Khalni Heart Expedition");
    crack.sac_source_id = 11;
    crack.ability_mode  = Action::AbilityMode::QuestSacLandSearch;
    Action cast;
    cast.kind      = Action::Kind::CastFromHand;
    cast.card_name = "Avenger of Zendikar";
    const std::vector<Action> acts{ cast, crack };
    CHECK(TurnSolver::IsLandSearchActivation(crack));
    CHECK_FALSE(TurnSolver::IsLandSearchActivation(cast));
    const std::vector<Action> trail = TurnSolver::WithoutLandSearchActs(acts);
    REQUIRE(trail.size() == 1);
    CHECK(trail[0].kind == Action::Kind::CastFromHand);
    TurnSolver::ApplyLandSearchActs(s, acts);
    CHECK(CountIn(s.players[0].graveyard, "Khalni Heart Expedition") == 1);
    CHECK(CountOnBf(s, "Forest", /*tapped=*/true) == 2);
}

// Plan::land_search_act_slot in ApplyPlan, in Plan::land_slot's encoding: 0 cracks before every cast,
// k after the k-th landfall-watcher cast, -1 in the trailing pass, and a k never reached falls back to
// the trailing pass (never lost). Two watchers (Khalni Heart Expedition, Rampaging Baloths) + an
// Evolving Wilds crack: the crack's landfall reaches exactly the watchers dispatched before it.
TEST_CASE("Land search: Plan::land_search_act_slot positions the crack among the watcher casts")
{
    LoadCards();
    GameState s = Fresh();
    for (int i = 0; i < 8; ++i) { Put(s, "Forest", 20 + i, false); }
    Put(s, "Evolving Wilds", 30, false);
    Card baloths = D("Rampaging Baloths").card;          baloths.m_number = 1;
    Card khalni  = D("Khalni Heart Expedition").card;    khalni.m_number  = 2;
    s.players[0].hand.push_back(baloths);
    s.players[0].hand.push_back(khalni);
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Forest", 52);
    const TurnSolver::Plan* base = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_search_act_slot != -1 || !p.land_search_pins.empty() || p.bp_choice >= 0) { continue; }
        bool b = false, k = false, crack = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Rampaging Baloths" && a.kind == Action::Kind::CastFromHand) { b = true; }
            if (a.card_name.str() == "Khalni Heart Expedition" && a.kind == Action::Kind::CastFromHand) { k = true; }
            if (TurnSolver::IsLandSearchActivation(a)) { crack = true; }
        }
        if (b && k && crack) { base = &p; break; }
    }
    REQUIRE(base != nullptr);
    auto triggers = [&](int8_t slot)
    {
        TurnSolver::Plan v = *base;
        v.land_search_act_slot = slot;
        GameState t = s;
        TurnSolver::ApplyPlan(t, v, true);
        int beasts = 0, counters = 0;
        for (const Permanent& p : t.battlefield)
        {
            if (CardHasSubtype(p.card, "Beast") && p.card.m_name.str() != "Rampaging Baloths") { ++beasts; }   // tokens
            if (p.card.m_name.str() == "Khalni Heart Expedition") { counters += p.quest_counters; }
        }
        CHECK(CountIn(t.players[0].graveyard, "Evolving Wilds") == 1);   // cracked exactly once
        return beasts + counters;
    };
    CHECK(triggers(-1) == 2);   // trailing: both watchers see the crack's Forest
    CHECK(triggers(0)  == 0);   // before every cast: neither does
    CHECK(triggers(1)  == 1);   // after the first-dispatched watcher only
    CHECK(triggers(2)  == 2);   // after the second == after both
    CHECK(triggers(5)  == 2);   // never reached -> the trailing pass, never lost
}

// The position fan: a plan that cracks a land search AND casts an ETB that counts lands gets 0 and
// 1..W-1 over its W landfall-watcher casts (every land-counter in the pool is itself a watcher, so W
// is the trailing pass, never a twin); a plan without a land-counter gets none.
TEST_CASE("Land search: Plan::land_search_act_slot is fanned at 0 and between watchers, only with a land-counting ETB")
{
    LoadCards();
    GameState s = Fresh();
    for (int i = 0; i < 13; ++i) { Put(s, "Forest", 20 + i, false); }
    Put(s, "Evolving Wilds", 40, false);
    Card baloths = D("Rampaging Baloths").card;   baloths.m_number = 1;
    Card avenger = D("Avenger of Zendikar").card; avenger.m_number = 2;
    s.players[0].hand.push_back(baloths);
    s.players[0].hand.push_back(avenger);
    Lib(s, "Forest", 50); Lib(s, "Forest", 51);
    bool both_0 = false, both_1 = false, both_2 = false, baloths_only_twin = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool b = false, a = false, crack = false;
        for (const Action& x : p.actions)
        {
            if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == "Rampaging Baloths")   { b = true; }
            if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == "Avenger of Zendikar") { a = true; }
            if (TurnSolver::IsLandSearchActivation(x)) { crack = true; }
        }
        if (!crack) { CHECK(p.land_search_act_slot == -1); continue; }
        if (b && a && p.land_search_act_slot == 0) { both_0 = true; }
        if (b && a && p.land_search_act_slot == 1) { both_1 = true; }
        if (b && a && p.land_search_act_slot == 2) { both_2 = true; }
        if (b && !a && p.land_search_act_slot != -1) { baloths_only_twin = true; }
    }
    CHECK(both_0);
    CHECK(both_1);
    CHECK_FALSE(both_2);             // == the trailing pass: an identity fold, not a twin
    CHECK_FALSE(baloths_only_twin);  // no land-counting ETB -> cast-then-crack is the only order
}

TEST_CASE("Land search: the human's in-combat Khalni prompt defaults to KEEP and says what it does")
{
    // Landfall claude-play sweep gi10 (2026-10-16): the prompt's heuristic_default was 0 ("crack now"),
    // labelled the AI's pick, while the engine HOLDS an unpinned Expedition -- and its source read as a
    // generic dig. The default is now -1 (keep), and the source carries the "(combat crack)" tag the
    // emitter and the viewer key their crack wording on.
    LoadCards();
    GameState s = Fresh();
    Put(s, "Forest", 9, true);
    const int t = Put(s, "Primeval Titan", 10, false);
    Put(s, "Khalni Heart Expedition", 11, false);
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 11) { p.quest_counters = 3; } }
    Lib(s, "Forest", 50); Lib(s, "Forest", 51);
    std::vector<int> atk{ t };
    int asked = 0, seen_default = 99;
    std::string seen_source;
    DigChooser fn = [&](const GameState&, int, const std::string& source, const std::vector<Card>& examined,
                        const std::vector<int>&, int def) -> int
    {
        ++asked; seen_default = def; seen_source = source;
        CHECK(examined.size() == 1);
        return def;   // the default: keep
    };
    DigChooser* saved = g_play_dig_chooser;
    g_play_dig_chooser = &fn;
    FireCombatLandSearchCracks(s, 0, atk);
    g_play_dig_chooser = saved;
    CHECK(asked == 1);
    CHECK(seen_default == -1);
    CHECK(seen_source.find("(combat crack)") != std::string::npos);
    CHECK(CountIn(s.players[0].graveyard, "Khalni Heart Expedition") == 0);   // kept
}

// ---- LandfallProvider: the splash-basic fetch rule (USER 2026-10-16) ------------------------------
// "Myriad landscape should search for Mountains unless there is only one left in the deck or we have
// fewer than 2 green. The others are trickier because you want to search for mountains unless our only
// other search source is myriad landscape." ORDER only: entry 0 is the default; every pick stays.
namespace
{
const LandfallProvider& LfProv()
{
    static const LandfallProvider p;
    return p;
}

GameState LfState(int lib_forests, int lib_mountains, int bf_forests)
{
    GameState s = Fresh();
    s.m_provider = &LfProv();
    int n = 50;
    for (int i = 0; i < lib_forests; ++i)   { Lib(s, "Forest", n++); }
    for (int i = 0; i < lib_mountains; ++i) { Lib(s, "Mountain", n++); }
    for (int i = 0; i < bf_forests; ++i)    { Put(s, "Forest", 10 + i, false); }
    return s;
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}
}   // namespace

TEST_CASE("Landfall fetch rule: Myriad takes the Mountain pair only with >= 2 Mountains left and >= 2 green")
{
    LoadCards();
    const LandSearchSpec sp = LandSearchSpecOf(D("Myriad Landscape").params);
    GameState s = LfState(4, 2, 2);
    CHECK(LandSearchPicks(s, 0, sp)[0] == std::vector<std::string>{ "Mountain", "Mountain" });
    GameState g1 = LfState(4, 2, 1);                       // fewer than 2 green
    CHECK(LandSearchPicks(g1, 0, sp)[0] == std::vector<std::string>{ "Forest", "Forest" });
    GameState m1 = LfState(4, 1, 2);                       // only one Mountain left
    Put(m1, "Mountain", 30, false);
    CHECK(LandSearchPicks(m1, 0, sp)[0] == std::vector<std::string>{ "Forest", "Forest" });
    GameState t = LfState(4, 2, 1);                        // Gruul Turf makes green too
    Put(t, "Gruul Turf", 31, false);
    CHECK(LandSearchPicks(t, 0, sp)[0] == std::vector<std::string>{ "Mountain", "Mountain" });
}

TEST_CASE("Landfall fetch rule: basic searches follow green, green, red, green, red")
{
    LoadCards();
    const LandSearchSpec rg = LandSearchSpecOf(D("Rampant Growth").params);
    auto front = [&](GameState& st) { return LandSearchPicks(st, 0, rg)[0]; };
    const std::vector<std::string> F{ "Forest" }, M{ "Mountain" };
    GameState a = LfState(4, 2, 0); CHECK(front(a) == F);   // g0
    GameState b = LfState(4, 2, 1); CHECK(front(b) == F);   // g1
    GameState c = LfState(4, 2, 2); CHECK(front(c) == M);   // g2 r0 -> the first red
    GameState d = LfState(4, 1, 2); Put(d, "Mountain", 30, false);
    CHECK(front(d) == F);                                    // g2 r1 -> the third green
    GameState e = LfState(4, 1, 3); Put(e, "Mountain", 30, false);
    CHECK(front(e) == M);                                    // g3 r1 -> the final red
    GameState f = LfState(4, 0, 3); Put(f, "Mountain", 30, false); Put(f, "Mountain", 31, false);
    CHECK(LandSearchPicks(f, 0, rg).size() == 1);            // no Mountain left: Forest only
    // A Mountain in HAND counts as a red source; a Gruul Turf in hand does not (it enters tapped),
    // though one on the battlefield does.
    GameState h = LfState(4, 1, 2); Hand(h, "Mountain", 44);
    CHECK(front(h) == F);                                    // g2, r1 (in hand) -> green
    GameState ht = LfState(4, 2, 2); Hand(ht, "Gruul Turf", 45);
    CHECK(front(ht) == M);                                   // the Turf in hand is not counted
    GameState bt = LfState(4, 2, 1); Put(bt, "Gruul Turf", 46, false);
    CHECK(front(bt) == F);                                   // on board: g2 r1 -> the third green
}

TEST_CASE("Landfall fetch rule: the Myriad situation leaves the reds for an on-board Myriad that is the only search left")
{
    LoadCards();
    const LandSearchSpec rg = LandSearchSpecOf(D("Rampant Growth").params);
    auto front = [&](GameState& st) { return LandSearchPicks(st, 0, rg)[0]; };
    const std::vector<std::string> F{ "Forest" }, M{ "Mountain" };
    GameState onb = LfState(4, 2, 2); Put(onb, "Myriad Landscape", 41, false);
    CHECK(front(onb) == F);
    GameState inh = LfState(4, 2, 2); Hand(inh, "Myriad Landscape", 40);
    CHECK(front(inh) == M);                                  // in hand: not planned around
    GameState other = LfState(4, 2, 2); Put(other, "Myriad Landscape", 41, false); Hand(other, "Evolving Wilds", 42);
    CHECK(front(other) == M);
    GameState sfx = LfState(4, 2, 2); Put(sfx, "Myriad Landscape", 41, false);
    SuspendedCard sc; sc.card = D("Search for Tomorrow").card; sc.card.m_number = 43; sc.arrive_turn = 6;
    sfx.players[0].suspended_cards.push_back(sc);
    CHECK(front(sfx) == M);
    GameState one = LfState(4, 1, 2); Put(one, "Myriad Landscape", 41, false); Put(one, "Mountain", 30, false);
    CHECK(front(one) == F);                                  // g2 r1: the sequence's green anyway
    GameState one2 = LfState(4, 1, 3); Put(one2, "Myriad Landscape", 41, false); Put(one2, "Mountain", 30, false);
    CHECK(front(one2) == M);                                 // a lone Mountain is never Myriad's: take it
}

TEST_CASE("Landfall fetch rule: two-land searches walk the sequence")
{
    LoadCards();
    const LandSearchSpec cu = LandSearchSpecOf(D("Cultivate").params);
    const LandSearchSpec kh = LandSearchSpecOf(D("Khalni Heart Expedition").params);
    GameState s = LfState(4, 2, 2);
    CHECK(LandSearchPicks(s, 0, cu)[0] == std::vector<std::string>{ "Mountain", "Forest" });   // red into play, green to hand
    CHECK(LandSearchPicks(s, 0, kh)[0] == std::vector<std::string>{ "Forest", "Mountain" });   // both into play (sorted)
    GameState g1 = LfState(4, 2, 1);
    CHECK(LandSearchPicks(g1, 0, kh)[0] == std::vector<std::string>{ "Forest", "Mountain" });  // green then red
    GameState g0 = LfState(4, 2, 0);
    CHECK(LandSearchPicks(g0, 0, cu)[0] == std::vector<std::string>{ "Forest", "Forest" });
}

// ---- LandfallProvider: Primeval Titan's pick (USER 2026-10-16) -----------------------------------
// "you usually want one of three": Evolving Wilds / Terramorphic, Gruul Turf (automatic with no land
// drop, or when short of green / red), Myriad Landscape; with no basic left a fixed order.
namespace
{
// A library holding every nonbasic the list plays: 2 Wilds-class (Evolving Wilds + Terramorphic,
// one identity class), 2 Myriad, the Turf, Radiant Fountain, Haunted Fengraf.
void TitanLib(GameState& s, bool turf = true, int myriads = 2, int wilds = 2)
{
    int n = 70;
    if (wilds >= 1) { Lib(s, "Evolving Wilds", n++); }
    if (wilds >= 2) { Lib(s, "Terramorphic Expanse", n++); }
    for (int i = 0; i < myriads; ++i) { Lib(s, "Myriad Landscape", n++); }
    if (turf) { Lib(s, "Gruul Turf", n++); }
    Lib(s, "Radiant Fountain", n++);
    Lib(s, "Haunted Fengraf", n++);
}
using Pick = std::vector<std::string>;
bool Holds(const std::vector<Pick>& picks, const Pick& p)
{ return std::find(picks.begin(), picks.end(), p) != picks.end(); }
}   // namespace

TEST_CASE("Landfall Titan rule: Gruul Turf with no land in hand or when short of a colour, beside Wilds or Myriad")
{
    LoadCards();
    const LandSearchSpec ti = LandSearchSpecOf(D("Primeval Titan").params);
    // g3, r2 on board (three Forests, two Mountains), nothing in hand: no land drop -> Turf.
    GameState nh = LfState(4, 0, 3); Put(nh, "Mountain", 30, true); Put(nh, "Mountain", 31, true);
    TitanLib(nh);
    const std::vector<Pick> a = LandSearchPicks(nh, 0, ti);
    CHECK(a.size() == 2);
    CHECK(Holds(a, Pick{ "Evolving Wilds", "Gruul Turf" }));
    CHECK(Holds(a, Pick{ "Gruul Turf", "Myriad Landscape" }));
    // The same board with a land in hand: no Turf; the Wilds / Myriad pairs, searched.
    GameState lh = nh; Hand(lh, "Forest", 44);
    const std::vector<Pick> b = LandSearchPicks(lh, 0, ti);
    CHECK(b.size() == 3);
    CHECK(Holds(b, Pick{ "Evolving Wilds", "Evolving Wilds" }));
    CHECK(Holds(b, Pick{ "Evolving Wilds", "Myriad Landscape" }));
    CHECK(Holds(b, Pick{ "Myriad Landscape", "Myriad Landscape" }));
    // A land in hand but short of green (g2) or of red (r1): Turf again.
    GameState g2 = LfState(4, 0, 2); Put(g2, "Mountain", 30, true); Put(g2, "Mountain", 31, true);
    TitanLib(g2); Hand(g2, "Forest", 44);
    CHECK(LandSearchPicks(g2, 0, ti).size() == 2);
    CHECK(LandSearchPicks(g2, 0, ti)[0].size() == 2);
    for (const Pick& p : LandSearchPicks(g2, 0, ti)) { CHECK(std::count(p.begin(), p.end(), "Gruul Turf") == 1); }
    GameState r1 = LfState(4, 1, 3); Put(r1, "Mountain", 30, true);
    TitanLib(r1); Hand(r1, "Forest", 44);
    for (const Pick& p : LandSearchPicks(r1, 0, ti)) { CHECK(std::count(p.begin(), p.end(), "Gruul Turf") == 1); }
    // A Mountain in hand is red (r2): with g3 the Turf is not wanted.
    GameState rh = LfState(4, 0, 3); Put(rh, "Mountain", 30, true);
    TitanLib(rh); Hand(rh, "Mountain", 44);
    CHECK(LandSearchPicks(rh, 0, ti).size() == 3);
    // Turf wanted but gone: the Wilds / Myriad pairs.
    GameState nt = LfState(4, 0, 3); Put(nt, "Mountain", 30, true); Put(nt, "Mountain", 31, true);
    TitanLib(nt, false);
    CHECK(LandSearchPicks(nt, 0, ti).size() == 3);
    // One Wilds-class card and no Myriad left, Turf not wanted: that card plus an open, searched slot.
    GameState one = LfState(1, 0, 3); Put(one, "Mountain", 30, true); Put(one, "Mountain", 31, true);
    Hand(one, "Forest", 44);
    Lib(one, "Evolving Wilds", 70); Lib(one, "Gruul Turf", 71); Lib(one, "Radiant Fountain", 72);
    const std::vector<Pick> c = LandSearchPicks(one, 0, ti);
    CHECK(c.size() == 2);   // Wilds beside the Turf or the Fountain -- never the Forest
    for (const Pick& p : c) { CHECK(std::count(p.begin(), p.end(), "Evolving Wilds") == 1); }
    // The base (DefaultProvider) list is not narrowed.
    GameState base = lh; base.m_provider = nullptr;
    CHECK(LandSearchPicks(base, 0, ti).size() > 3);
}

TEST_CASE("Landfall Titan rule: out of basics -- Turf, Myriad, Radiant Fountain, Haunted Fengraf, Wilds; one pick")
{
    LoadCards();
    const LandSearchSpec ti = LandSearchSpecOf(D("Primeval Titan").params);
    GameState s = LfState(0, 0, 3); Hand(s, "Forest", 44);
    TitanLib(s);
    CHECK(LandSearchPicks(s, 0, ti) == std::vector<Pick>{ Pick{ "Gruul Turf", "Myriad Landscape" } });
    GameState nt = LfState(0, 0, 3); TitanLib(nt, false);
    CHECK(LandSearchPicks(nt, 0, ti) == std::vector<Pick>{ Pick{ "Myriad Landscape", "Myriad Landscape" } });
    GameState m1 = LfState(0, 0, 3); TitanLib(m1, false, 1);
    CHECK(LandSearchPicks(m1, 0, ti) == std::vector<Pick>{ Pick{ "Myriad Landscape", "Radiant Fountain" } });
    GameState w = LfState(0, 0, 3); TitanLib(w, false, 0);
    CHECK(LandSearchPicks(w, 0, ti) == std::vector<Pick>{ Pick{ "Haunted Fengraf", "Radiant Fountain" } });
    // The land-in-hand bit is part of the memo key.
    std::uint64_t k0 = 0, k1 = 0;
    GameState h0 = LfState(4, 2, 3); GameState h1 = h0; Hand(h1, "Forest", 44);
    CHECK(LfProv().LandSearchPickOrderKey(h0, 0, k0));
    CHECK(LfProv().LandSearchPickOrderKey(h1, 0, k1));
    CHECK(k0 != k1);
}

TEST_CASE("Landfall Titan rule: never a basic -- one nonbasic alone, or nothing")
{
    // USER 2026-10-16: "Primeval Titan must never search for a basic in my opinion."
    LoadCards();
    const LandSearchSpec ti = LandSearchSpecOf(D("Primeval Titan").params);
    GameState s = LfState(4, 0, 3); Put(s, "Mountain", 30, true); Put(s, "Mountain", 31, true);
    Hand(s, "Forest", 44);
    TitanLib(s);
    for (const Pick& p : LandSearchPicks(s, 0, ti))
    { CHECK(std::count(p.begin(), p.end(), "Forest") == 0); CHECK(std::count(p.begin(), p.end(), "Mountain") == 0); }
    GameState one = LfState(4, 2, 3); Hand(one, "Forest", 44); Lib(one, "Evolving Wilds", 70);
    CHECK(LandSearchPicks(one, 0, ti) == std::vector<Pick>{ Pick{ "Evolving Wilds" } });
    GameState none = LfState(4, 2, 3); Hand(none, "Forest", 44);
    CHECK(LandSearchPicks(none, 0, ti).empty());
    // The base (DefaultProvider) list still holds the basics.
    GameState b = none; b.m_provider = nullptr;
    CHECK_FALSE(LandSearchPicks(b, 0, ti).empty());
}

TEST_CASE("Land search: the human prompt's default matches lands by identity class and follows the human's answers")
{
    // USER 2026-10-16, Landfall s5 gi4: Titan's rule pick was Wilds + Wilds with only one Evolving Wilds
    // and one Terramorphic Expanse left; the second prompt defaulted to a Forest (entry 0) because the
    // pick names the class "Evolving Wilds", no longer a candidate name. It must be the Terramorphic.
    LoadCards();
    const LandSearchSpec ti = LandSearchSpecOf(D("Primeval Titan").params);
    GameState s = LfState(4, 1, 3);
    Put(s, "Mountain", 30, true); Put(s, "Myriad Landscape", 31, true); Put(s, "Radiant Fountain", 32, true);
    Hand(s, "Evolving Wilds", 40); Hand(s, "Forest", 41); Hand(s, "Gruul Turf", 42);
    Lib(s, "Evolving Wilds", 70); Lib(s, "Terramorphic Expanse", 71); Lib(s, "Myriad Landscape", 72);
    Lib(s, "Haunted Fengraf", 73);
    std::vector<std::string> defaults;
    int answer_first = -2;   // -2 = take the default
    TutorChooser tc = [&](const GameState&, int, const std::string&, const std::vector<std::string>& cands, int def,
                          const std::vector<int>&)
    {
        REQUIRE(def >= 0); REQUIRE(def < static_cast<int>(cands.size()));
        defaults.push_back(cands[static_cast<std::size_t>(def)]);
        if (defaults.size() == 1 && answer_first >= -1)
        {
            for (std::size_t i = 0; i < cands.size(); ++i) { if (cands[i] == "Myriad Landscape") { return static_cast<int>(i); } }
        }
        return def;
    };
    TutorChooser* saved = g_play_tutor_chooser;
    g_play_tutor_chooser = &tc;
    GameState t = s;
    PerformLandSearch(t, 0, ti, "Primeval Titan", -1);
    REQUIRE(defaults.size() == 2);
    CHECK(defaults[0] == "Evolving Wilds");
    CHECK(defaults[1] == "Terramorphic Expanse");
    // The human answers Myriad first: the default follows (a pick holding Myriad -> its Wilds).
    defaults.clear(); answer_first = 0;
    GameState u = s;
    PerformLandSearch(u, 0, ti, "Primeval Titan", -1);
    g_play_tutor_chooser = saved;
    REQUIRE(defaults.size() == 2);
    CHECK(defaults[1] == "Evolving Wilds");
}

TEST_CASE("Landfall Karoo rule: bounce colourless first, then the colour with the most to spare")
{
    LoadCards();
    // Requirements from the deck's costs: Genesis Wave GGG, Omnath RR.
    auto board = [](int forests, int mountains, bool fountain, std::vector<int>& legal, int& turf)
    {
        GameState s = LfState(0, 0, 0);
        Lib(s, "Genesis Wave", 80); Lib(s, "Omnath, Locus of Rage", 81);
        int n = 10;
        for (int i = 0; i < forests; ++i)   { Put(s, "Forest", n++, true); }
        for (int i = 0; i < mountains; ++i) { Put(s, "Mountain", n++, true); }
        if (fountain) { Put(s, "Radiant Fountain", n++, true); }
        turf = Put(s, "Gruul Turf", n++, false);
        legal.clear();
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (i != turf) { legal.push_back(i); } }
        return s;
    };
    auto picked = [](const GameState& s, const std::vector<int>& legal)
    {
        const int i = LfProv().BounceUserRulePick(s, 0, legal, legal);
        REQUIRE(i >= 0);
        return s.battlefield[static_cast<std::size_t>(i)].card.m_name.str();
    };
    std::vector<int> legal; int turf = -1;
    GameState a = board(3, 1, true, legal, turf);  CHECK(picked(a, legal) == "Radiant Fountain");
    GameState b = board(3, 1, false, legal, turf); CHECK(picked(b, legal) == "Forest");     // G 4/3 spare 1, R 2/2 spare 0
    GameState c = board(2, 2, false, legal, turf); CHECK(picked(c, legal) == "Mountain");   // G 3/3 spare 0, R 3/2 spare 1
    GameState d = board(2, 1, false, legal, turf); CHECK(picked(d, legal) == "Forest");     // both spare 0: more copies
    GameState e = board(1, 1, false, legal, turf); CHECK(picked(e, legal) == "Mountain");   // G 2/3 short, R 2/2 spare 0
}

TEST_CASE("Landfall fetch rule: basic searches are PRUNED to the rule's one pick; the key separates every input")
{
    LoadCards();
    const LandfallProvider& lp = LfProv();
    const LandSearchSpec rg = LandSearchSpecOf(D("Rampant Growth").params);
    const LandSearchSpec kh = LandSearchSpecOf(D("Khalni Heart Expedition").params);
    GameState s = LfState(4, 2, 2);
    CHECK(LandSearchPicks(s, 0, rg).size() == 1);
    CHECK(LandSearchPicks(s, 0, kh).size() == 1);
    // The base (DefaultProvider) list is unpruned: both basics.
    GameState b = s; b.m_provider = nullptr;
    CHECK(LandSearchPicks(b, 0, rg).size() == 2);
    std::uint64_t k0 = 0, k1 = 0, k2 = 0, k3 = 0;
    GameState myr = LfState(4, 2, 2); Put(myr, "Myriad Landscape", 41, false);
    GameState g1  = LfState(4, 2, 1);
    GameState mh  = LfState(4, 2, 2); Hand(mh, "Mountain", 44);
    CHECK(lp.LandSearchPickOrderKey(s, 0, k0));
    CHECK(lp.LandSearchPickOrderKey(myr, 0, k1));
    CHECK(lp.LandSearchPickOrderKey(g1, 0, k2));
    CHECK(lp.LandSearchPickOrderKey(mh, 0, k3));
    CHECK(k0 != k1); CHECK(k0 != k2); CHECK(k0 != k3);
}

TEST_CASE("RefindByNumberInOrder: the wide (map) path returns exactly what the occurrence scan does")
{
    // The reference is the scan itself (the helper's short-list path, copied): for nums[i], the
    // (number of earlier entries carrying nums[i])-th battlefield slot with that number, dropped if
    // there are not that many. Duplicated numbers, missing numbers and 0 are all exercised.
    auto scan = [](const GameState& s, const std::vector<int>& nums)
    {
        std::vector<int> out;
        for (std::size_t i = 0; i < nums.size(); ++i)
        {
            int seen_before = 0;
            for (std::size_t k = 0; k < i; ++k) { if (nums[k] == nums[i]) { ++seen_before; } }
            for (int j = 0; j < static_cast<int>(s.battlefield.size()); ++j)
            {
                if (s.battlefield[static_cast<std::size_t>(j)].card.m_number != nums[i]) { continue; }
                if (seen_before-- == 0) { out.push_back(j); break; }
            }
        }
        return out;
    };
    unsigned x = 12345u;
    auto rnd = [&x](unsigned m) { x = x * 1103515245u + 12345u; return static_cast<int>((x >> 8) % m); };
    for (int trial = 0; trial < 200; ++trial)
    {
        GameState s;
        const int b = 1 + rnd(300);
        for (int j = 0; j < b; ++j)
        {
            Permanent p;
            p.card.m_number = rnd(4) == 0 ? rnd(8) : rnd(400);   // some heavy duplicates
            s.battlefield.push_back(p);
        }
        std::vector<int> nums;
        const int n = 1 + rnd(250);
        for (int i = 0; i < n; ++i)
        {
            const int r = rnd(10);
            nums.push_back(r == 0 ? 0 : r == 1 ? 1000 + rnd(50)   // absent
                         : s.battlefield[static_cast<std::size_t>(rnd(static_cast<unsigned>(b)))].card.m_number);
        }
        CHECK(RefindByNumberInOrder(s, nums) == scan(s, nums));
    }
}

namespace
{
void ResolveAllLs(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

Action CastLs(const GameState& s, const std::string& name)
{
    const CardDefinition& d = D(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d.card.m_name;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.is_noncreature = !d.card.IsCreature();
    a.card_mv        = d.card.m_mana_cost.ManaValue();
    a.sacrifice_land = d.params.sacrifice_land;   // as the enumerator stamps it
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name == d.card.m_name) { a.hand_index = i; break; } }
    return a;
}
}   // namespace

TEST_CASE("Harrow: the sacrifice is paid at CAST, before the search -- a fetched basic is never the victim (both worlds)")
{
    LoadCards();
    // Forests 10, 11, 12 pay {2}{G}; the Khalni counts landfalls. Sac pin 3 = the FOURTH candidate of the
    // tapped-first ranking. Paid before the search (CR 601.2h) the board holds three lands, so the pin clamps
    // to one of them; paid after it (the defect), the two fetched basics are candidates four and five and the
    // pin would sacrifice one of them.
    GameState s = LfState(/*lib_forests=*/2, /*lib_mountains=*/1, /*bf_forests=*/3);
    s.phase = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    Put(s, "Khalni Heart Expedition", 20, false);
    Hand(s, "Harrow", 30);
    const int sc0 = s.search_count;
    TurnSolver::Plan line;
    line.land_decided = true;
    line.actions      = { CastLs(s, "Harrow") };
    line.sac_pins     = { 3 };
    auto check = [&](const GameState& st, const char* world)
    {
        std::string bf, gy;
        for (const Permanent& q : st.battlefield)
        { bf += q.card.m_name.str() + "#" + std::to_string(q.card.m_number) + (q.tapped ? "(T) " : " "); }
        for (const Card& c : st.players[0].graveyard) { gy += c.m_name.str() + "#" + std::to_string(c.m_number) + " "; }
        INFO(world << " bf: " << bf << "| gy: " << gy);
        CHECK(CountIn(st.players[0].graveyard, "Harrow") == 1);
        int victims = 0, fetched_victims = 0;
        for (const Card& c : st.players[0].graveyard)
        {
            if (!c.IsLand()) { continue; }
            ++victims;
            if (c.m_number >= 50) { ++fetched_victims; }
        }
        CHECK(victims == 1);
        CHECK(fetched_victims == 0);
        int old_tapped = 0, new_untapped = 0, khalni = -1;
        for (const Permanent& q : st.battlefield)
        {
            if (q.card.m_number == 20) { khalni = q.quest_counters; }
            if (!q.card.IsLand()) { continue; }
            if (q.card.m_number < 50 && q.tapped) { ++old_tapped; }
            if (q.card.m_number >= 50 && !q.tapped) { ++new_untapped; }
        }
        CHECK(old_tapped == 2);      // the three payers, less the victim
        CHECK(new_untapped == 2);    // both basics UNTAPPED
        CHECK(khalni == 2);          // two landfalls
        CHECK(st.search_count == sc0 + 1);
    };
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    check(roll, "rollout");
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAllLs);
    check(exec, "executor");
}

TEST_CASE("Explosive Vegetation: two basics, TAPPED, one shuffle")
{
    LoadCards();
    GameState s = Fresh();
    Lib(s, "Forest", 50); Lib(s, "Forest", 51); Lib(s, "Mountain", 52);
    const int sc0 = s.search_count;
    PerformLandSearch(s, 0, LandSearchSpecOf(D("Explosive Vegetation").params), "Explosive Vegetation", -1);
    CHECK(CountOnBf(s, "Forest", true) + CountOnBf(s, "Mountain", true) == 2);
    CHECK(CountOnBf(s, "Forest", false) + CountOnBf(s, "Mountain", false) == 0);
    CHECK(s.search_count == sc0 + 1);
}

TEST_CASE("Nature's Lore / Skyshroud Claim: a 'Forest card' is the Forest subtype, basic or not -- never a Mountain")
{
    LoadCards();
    const bool shock = CardDatabase::Instance().Lookup("Stomping Ground") != nullptr;
    GameState s = Fresh();
    Lib(s, "Mountain", 50); Lib(s, "Forest", 51); Lib(s, "Forest", 52);
    if (shock) { Lib(s, "Stomping Ground", 53); }
    const LandSearchSpec nl = LandSearchSpecOf(D("Nature's Lore").params);
    const LandSearchSpec sc = LandSearchSpecOf(D("Skyshroud Claim").params);
    CHECK(nl.subtype != SubtypeRegistry::kNone);
    CHECK_FALSE(nl.basic_only);
    for (const auto& pk : LandSearchPicks(s, 0, nl))
    { for (const std::string& nm : pk) { CHECK(nm != "Mountain"); } }
    CHECK(HasPick(LandSearchPicks(s, 0, nl), { "Forest" }));
    if (shock) { CHECK(HasPick(LandSearchPicks(s, 0, nl), { "Stomping Ground" })); }
    for (const auto& pk : LandSearchPicks(s, 0, sc))
    {
        CHECK(pk.size() == 2);
        for (const std::string& nm : pk) { CHECK(nm != "Mountain"); }
    }
    // The memo keys on the filter: a basic search over the SAME library still offers the Mountain.
    CHECK(HasPick(LandSearchPicks(s, 0, LandSearchSpecOf(D("Rampant Growth").params)), { "Mountain" }));
    // Resolution: Skyshroud's two Forests enter UNTAPPED (a basic's own entry); the Mountain stays.
    GameState r = Fresh();
    Lib(r, "Mountain", 50); Lib(r, "Forest", 51); Lib(r, "Forest", 52);
    PerformLandSearch(r, 0, sc, "Skyshroud Claim", -1);
    CHECK(CountOnBf(r, "Forest", /*tapped=*/false) == 2);
    CHECK(LibCount(r, "Mountain") == 1);
}

TEST_CASE("Landfall provider: a 'Forest card' search is not Primeval Titan's any-land search -- it may take a basic")
{
    LoadCards();
    // The Titan rule admits no basic for an any-land search; Nature's Lore finds only Forests, which here are
    // all basics, so taking the Titan branch would whiff it.
    GameState s = LfState(/*lib_forests=*/3, /*lib_mountains=*/2, /*bf_forests=*/2);
    Lib(s, "Gruul Turf", 70);
    const LandSearchSpec nl = LandSearchSpecOf(D("Nature's Lore").params);
    const Picks p = LandSearchPicks(s, 0, nl);
    REQUIRE_FALSE(p.empty());
    CHECK(p[0] == std::vector<std::string>{ "Forest" });
    PerformLandSearch(s, 0, nl, "Nature's Lore", -1);
    CHECK(CountOnBf(s, "Forest", /*tapped=*/false) == 3);
    // ...while Titan's search keeps the rule: no basic.
    for (const auto& pk : LandSearchPicks(s, 0, LandSearchSpecOf(D("Primeval Titan").params)))
    { for (const std::string& nm : pk) { CHECK(nm != "Forest"); CHECK(nm != "Mountain"); } }
}
