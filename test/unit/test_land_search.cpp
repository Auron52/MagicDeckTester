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
//  * Fengraf's draw is a function of the eligible multiset (storage order does not matter).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "runner/GoldFishRunner.h"

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

TEST_CASE("Landfall fetch rule: two-land searches walk the sequence; Titan keeps the base order unpruned")
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
    GameState tl = LfState(4, 2, 1);
    Lib(tl, "Evolving Wilds", 60); Lib(tl, "Gruul Turf", 61);
    GameState tb = tl;
    tb.m_provider = nullptr;   // DefaultProvider
    const LandSearchSpec ti = LandSearchSpecOf(D("Primeval Titan").params);
    CHECK(LandSearchPicks(tl, 0, ti) == LandSearchPicks(tb, 0, ti));
    CHECK(LandSearchPicks(tl, 0, ti).size() > 1);
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
