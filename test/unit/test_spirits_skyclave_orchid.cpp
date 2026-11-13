// Spirits onboarding, integrator C2 (2026-11-13): Skyclave Apparition + White Orchid Phantom.
//   * Skyclave: etb_exile_opp_nonland_nontoken_max_mv (ResolveEtbExileUpToOne, the shared enter cascade --
//     a cast AND an Aether Vial put, both worlds); the provider's identity fold takes the largest legal
//     body; the exile is PERMANENT (kSkyclaveExileBase, never returned by the Brutal Cathar orphan sweep);
//     ltb_exiled_owner_illusion_token makes the owner's X/X Illusion (X = 0 dies at once).
//   * White Orchid Phantom: etb_destroy_nonbasic_land_optional. Unpinned = the provider's fold (an
//     opponent's nonbasic, which then gets a TAPPED "Opponent Basic Land"; else none). OWN lands are
//     SEARCHED: one cast / Vial-put variant per distinct own nonbasic name, pinned on tutor_target, and
//     the basic it then finds is a land_search_pins ordinal. SpiritsProvider prunes the own variants
//     when no basic is left in the library.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
const char* const kSky = "Skyclave Apparition";
const char* const kWop = "White Orchid Phantom";

const CardDefinition& ScDef(const std::string& name)
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

Card ScZone(const std::string& name, int number)
{
    ScDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int ScPut(GameState& s, const std::string& name, int number, int controller = 0, bool tapped = false)
{
    Permanent p;
    p.card = ScDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A goldfish spawn exactly as GameEngine materialises one: a bare "P/T Creature" card, MV 0, nontoken.
int ScSpawn(GameState& s, int pw, int number)
{
    Card c;
    c.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power = pw;
    c.m_toughness = pw;
    c.m_number = number;
    Permanent p;
    p.card = c;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int ScOppLand(GameState& s, bool basic, int number = 0)
{
    Card land;
    land.m_name = basic ? "Opponent Basic Land" : "Opponent Nonbasic Land";
    land.RehashName();
    land.AddType(CardType::Land);
    if (basic) { land.AddSupertype(Supertype::Basic); }
    land.m_number = number;
    Permanent p;
    p.card = land;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const DecisionProvider& SpiritsProv()
{
    static const DecisionProvider& prov = [] () -> const DecisionProvider& {
        ScDef("Plains");
        return DetectDecisionProvider(DeckLoader::LoadFromFile(
            ResolveHeuristicDefaultsPath("decks/Spirits/Spirits.cod")));
    }();
    return prov;
}

GameState ScBoard(const std::vector<std::string>& lands, bool basics_in_library = true)
{
    GameState s;
    s.m_provider          = &SpiritsProv();
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(ScZone("Supreme Phantom", 900 + k)); }
    if (basics_in_library)
    { for (int k = 0; k < 4; ++k) { s.players[0].library.push_back(ScZone("Plains", 950 + k)); } }
    int n = 100;
    for (const std::string& l : lands) { ScPut(s, l, ++n); }
    return s;
}

int ScIndex(const GameState& s, const std::string& name, int controller = -1)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        if (s.battlefield[i].card.m_name.str() == name
            && (controller < 0 || s.battlefield[i].controller_index == controller)) { return i; }
    }
    return -1;
}

int ScCount(const GameState& s, const std::string& name, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == name && p.controller_index == controller) { ++n; } }
    return n;
}

bool ScInGraveyard(const GameState& s, int player, const std::string& name)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

void ScResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState ScRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ScResolveAll);
    return exec;
}

// The plan whose actions are exactly one `kind` of `name`, pinned (tutor_target) to `pin`.
const TurnSolver::Plan* ScFindPlan(const std::vector<TurnSolver::Plan>& plans, Action::Kind kind,
                                   const std::string& name, const std::string& pin,
                                   const std::vector<int>* ls_pins = nullptr)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind != kind || a.card_name.str() != name || a.tutor_target.str() != pin) { continue; }
        if (ls_pins != nullptr && p.land_search_pins != *ls_pins) { continue; }
        if (ls_pins == nullptr && !p.land_search_pins.empty()) { continue; }
        return &p;
    }
    return nullptr;
}

int ScPinnedVariants(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (a.card_name.str() == name && !a.tutor_target.str().empty()) { ++n; break; } }
    }
    return n;
}

void ScCheckWorlds(const GameState& s, const TurnSolver::Plan& plan,
                   const std::function<void(const GameState&)>& check)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = ScRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        check(*g);
    }
}
}   // namespace

// ---- the cards -----------------------------------------------------------------------------------
TEST_CASE("Skyclave Apparition / White Orchid Phantom: costs, types, params")
{
    const CardDefinition& sky = ScDef(kSky);
    CHECK(sky.card.m_mana_cost.ManaValue() == 3);
    CHECK(sky.card.IsCreature());
    CHECK(CardHasSubtype(sky.card, "Spirit"));
    CHECK(CardHasSubtype(sky.card, "Kor"));
    CHECK(sky.card.m_power == 2);
    CHECK(sky.card.m_toughness == 2);
    CHECK(sky.params.etb_exile_opp_nonland_nontoken_max_mv == 4);
    CHECK(sky.params.ltb_exiled_owner_illusion_token);
    const CardDefinition& wop = ScDef(kWop);
    CHECK(wop.card.m_mana_cost.ManaValue() == 2);
    CHECK(CardHasSubtype(wop.card, "Spirit"));
    CHECK(CardHasSubtype(wop.card, "Knight"));
    CHECK(wop.card.HasKeyword(Keyword::FirstStrike));
    CHECK_FALSE(wop.card.HasKeyword(Keyword::Flash));
    CHECK(wop.params.etb_destroy_nonbasic_land_optional);
    // Neither param moves the deck's routing (SpiritsProvider either way).
    CHECK(std::string(SpiritsProv().Name()) == "Spirits");
}

// ---- Skyclave ETB ----------------------------------------------------------------------------------
TEST_CASE("Skyclave CAST exiles the opponent's largest legal permanent in BOTH worlds; tokens / lands / MV>4 are illegal")
{
    GameState s = ScBoard({ "Plains", "Plains", "Island" });
    ScSpawn(s, 1, 700001);
    ScSpawn(s, 4, 700002);
    ScOppLand(s, false);
    const int big = ScPut(s, "Millicent, Restless Revenant", 800, /*controller=*/1);   // MV 7: illegal
    (void)big;
    const int tok = ScSpawn(s, 6, 700003);
    s.battlefield[tok].is_token = true;                                                // a 6/6 TOKEN: illegal
    s.players[0].hand.push_back(ScZone(kSky, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = ScFindPlan(plans, Action::Kind::CastFromHand, kSky, "");
    REQUIRE(cast != nullptr);
    ScCheckWorlds(s, *cast, [](const GameState& g) {
        const int si = ScIndex(g, kSky, 0);
        REQUIRE(si >= 0);
        CHECK(ScIndex(g, "4/4 Creature", 1) < 0);           // exiled
        CHECK(ScIndex(g, "1/1 Creature", 1) >= 0);
        CHECK(ScIndex(g, "6/6 Creature", 1) >= 0);          // the token stays
        CHECK(ScIndex(g, "Millicent, Restless Revenant", 1) >= 0);
        CHECK(ScIndex(g, "Opponent Nonbasic Land", 1) >= 0);
        CHECK(g.battlefield[si].linked_exile_number == 700002);
        bool in_exile = false;
        for (const Card& c : g.exile)
        {
            if (c.m_number != 700002) { continue; }
            in_exile = true;
            CHECK(CardIsSkyclaveExile(c));
            CHECK_FALSE(CardIsLinkedExile(c));               // never Brutal Cathar's returning link
            CHECK(SkyclaveExileSource(c) == 30);
            CHECK(SkyclaveExileOwner(c) == 1);
        }
        CHECK(in_exile);
    });
}

TEST_CASE("Skyclave AETHER VIAL PUT exiles too (the shared enter cascade), both worlds")
{
    GameState s = ScBoard({ "Plains" });
    ScSpawn(s, 3, 700001);
    const int vi = ScPut(s, "Aether Vial", 7);
    s.battlefield[vi].charge_counters = 3;
    s.players[0].hand.push_back(ScZone(kSky, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* put = ScFindPlan(plans, Action::Kind::ActivateVial, kSky, "");
    REQUIRE(put != nullptr);
    ScCheckWorlds(s, *put, [](const GameState& g) {
        CHECK(ScIndex(g, kSky, 0) >= 0);
        CHECK(ScIndex(g, "3/3 Creature", 1) < 0);
    });
}

TEST_CASE("Skyclave with no legal target exiles nothing; the provider fold picks the largest; a decline is -1")
{
    GameState s = ScBoard({});
    ScOppLand(s, false);
    const int src = ScPut(s, kSky, 30);
    ResolveEtbExileUpToOne(s, 0, 30, 4, kSky);
    CHECK(s.exile.empty());
    CHECK(s.battlefield[src].linked_exile_number == 0);
    ScSpawn(s, 2, 700001);
    ScSpawn(s, 5, 700002);
    ScSpawn(s, 3, 700003);
    const std::vector<int> legal = EtbExileUpToOneLegal(s, 0, 4);
    REQUIRE(legal.size() == 3);
    const int k = GenericProvider().EtbOptionalExileTargetPick(s, 0, legal);
    REQUIRE(k >= 0);
    CHECK(s.battlefield[legal[k]].card.m_number == 700002);
    CHECK(GenericProvider().EtbOptionalExileTargetPick(s, 0, {}) == -1);
}

// ---- Skyclave LTB ----------------------------------------------------------------------------------
TEST_CASE("Skyclave LTB: an MV-0 spawn gives a 0/0 Illusion that dies at once; the card stays exiled, never returned")
{
    GameState s = ScBoard({});
    ScPut(s, kSky, 30);
    ScSpawn(s, 4, 700002);
    ResolveEtbExileUpToOne(s, 0, 30, 4, kSky);
    REQUIRE(s.exile.size() == 1);
    // Skyclave dies.
    const int si = ScIndex(s, kSky, 0);
    const Card dead = s.battlefield[si].card;
    s.battlefield.erase(s.battlefield.begin() + si);
    s.players[0].graveyard.push_back(dead);
    OnCreatureDies(s, 0, dead, false, 0, nullptr, 0);
    CHECK(ScCount(s, "Illusion", 1) == 0);
    int illusions = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { ++illusions; } }
    CHECK(illusions == 0);
    REQUIRE(s.exile.size() == 1);
    CHECK_FALSE(CardIsSkyclaveExile(s.exile.front()));     // the link is spent
    ReturnOrphanedLinkedExiles(s);
    CHECK(s.exile.size() == 1);                             // never returned
    CHECK(ScIndex(s, "4/4 Creature", 1) < 0);
}

TEST_CASE("Skyclave LTB: an MV-3 permanent exiled -> its owner gets a 3/3 blue Illusion; nothing exiled -> no token")
{
    GameState s = ScBoard({});
    ScPut(s, kSky, 30);
    ScPut(s, "Drogskol Captain", 800, /*controller=*/1);    // MV 3, the opponent's
    ResolveEtbExileUpToOne(s, 0, 30, 4, kSky);
    REQUIRE(ScIndex(s, "Drogskol Captain", 1) < 0);
    const int si = ScIndex(s, kSky, 0);
    const Card dead = s.battlefield[si].card;
    s.battlefield.erase(s.battlefield.begin() + si);
    OnCreatureDies(s, 0, dead, false, 0, nullptr, 0);
    int found = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token || !CardHasSubtype(p.card, "Illusion")) { continue; }
        ++found;
        CHECK(p.controller_index == 1);
        CHECK(p.card.m_power == 3);
        CHECK(p.card.m_toughness == 3);
        CHECK(p.card.HasColor(Color::Blue));
    }
    CHECK(found == 1);

    GameState t = ScBoard({});
    ScPut(t, kSky, 31);
    ResolveEtbExileUpToOne(t, 0, 31, 4, kSky);               // nothing to exile
    const int ti = ScIndex(t, kSky, 0);
    const Card dead2 = t.battlefield[ti].card;
    t.battlefield.erase(t.battlefield.begin() + ti);
    OnCreatureDies(t, 0, dead2, false, 0, nullptr, 0);
    for (const Permanent& p : t.battlefield) { CHECK_FALSE(p.is_token); }
}

// ---- White Orchid Phantom ----------------------------------------------------------------------------
TEST_CASE("White Orchid base: destroys the OPPONENT's nonbasic, which gets a TAPPED basic; both worlds")
{
    GameState s = ScBoard({ "Plains", "Plains", "Godless Shrine" });
    ScOppLand(s, true);
    ScOppLand(s, false);
    s.players[0].hand.push_back(ScZone(kWop, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* base = ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "");
    REQUIRE(base != nullptr);
    ScCheckWorlds(s, *base, [](const GameState& g) {
        CHECK(ScIndex(g, kWop, 0) >= 0);
        CHECK(ScIndex(g, "Opponent Nonbasic Land", 1) < 0);
        CHECK(ScCount(g, "Opponent Basic Land", 1) == 2);
        int tapped_basics = 0;
        for (const Permanent& p : g.battlefield)
        { if (p.controller_index == 1 && p.card.m_name.str() == "Opponent Basic Land" && p.tapped) { ++tapped_basics; } }
        CHECK(tapped_basics == 1);
        CHECK(ScIndex(g, "Godless Shrine", 0) >= 0);   // ours untouched
    });
}

TEST_CASE("White Orchid base with no opponent nonbasic destroys nothing (ours is never the fold's pick)")
{
    GameState s = ScBoard({ "Plains", "Godless Shrine" });
    ScOppLand(s, true);
    s.players[0].hand.push_back(ScZone(kWop, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* base = ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "");
    REQUIRE(base != nullptr);
    ScCheckWorlds(s, *base, [](const GameState& g) {
        CHECK(ScIndex(g, "Godless Shrine", 0) >= 0);
        CHECK(ScCount(g, "Opponent Basic Land", 1) == 1);
    });
}

TEST_CASE("White Orchid OWN land is SEARCHED: a pinned cast destroys our Shrine and fetches a TAPPED Plains, both worlds")
{
    GameState s = ScBoard({ "Plains", "Plains", "Godless Shrine", "Unclaimed Territory" });
    s.players[0].hand.push_back(ScZone(kWop, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    // One variant per distinct own nonbasic name (Shrine, Territory) beside the base.
    CHECK(ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "") != nullptr);
    const TurnSolver::Plan* pin = ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "Godless Shrine");
    REQUIRE(pin != nullptr);
    bool territory = false;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions.front().tutor_target.str() == "Unclaimed Territory") { territory = true; }
    }
    CHECK(territory);
    const std::size_t lib0 = s.players[0].library.size();
    ScCheckWorlds(s, *pin, [lib0](const GameState& g) {
        CHECK(ScIndex(g, kWop, 0) >= 0);
        CHECK(ScIndex(g, "Godless Shrine", 0) < 0);
        CHECK(ScInGraveyard(g, 0, "Godless Shrine"));
        CHECK(ScCount(g, "Plains", 0) == 3);
        int fetched = 0;
        for (const Permanent& p : g.battlefield)
        {
            if (p.controller_index != 0 || p.card.m_name.str() != "Plains" || p.card.m_number < 950) { continue; }
            ++fetched;
            CHECK(p.tapped);                                  // "put it onto the battlefield tapped"
        }
        CHECK(fetched == 1);
        CHECK(g.players[0].library.size() == lib0 - 1);
    });
}

TEST_CASE("White Orchid pinned to a land name takes a TAPPED copy first")
{
    GameState s = ScBoard({});
    const int a = ScPut(s, "Godless Shrine", 201, 0, /*tapped=*/false);
    const int b = ScPut(s, "Godless Shrine", 202, 0, /*tapped=*/true);
    (void)a; (void)b;
    ResolveEtbDestroyNonbasicLandOptional(s, 0, "Godless Shrine", kWop);
    REQUIRE(ScCount(s, "Godless Shrine", 0) == 1);
    const Permanent& left = s.battlefield[ScIndex(s, "Godless Shrine", 0)];
    CHECK(left.card.m_number == 201);
    CHECK_FALSE(left.tapped);
}

TEST_CASE("White Orchid VIAL PUT carries the own-land pin, both worlds")
{
    GameState s = ScBoard({ "Plains", "Hallowed Fountain" });
    const int vi = ScPut(s, "Aether Vial", 7);
    s.battlefield[vi].charge_counters = 2;
    s.players[0].hand.push_back(ScZone(kWop, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    REQUIRE(ScFindPlan(plans, Action::Kind::ActivateVial, kWop, "") != nullptr);
    const TurnSolver::Plan* pin = ScFindPlan(plans, Action::Kind::ActivateVial, kWop, "Hallowed Fountain");
    REQUIRE(pin != nullptr);
    ScCheckWorlds(s, *pin, [](const GameState& g) {
        CHECK(ScIndex(g, kWop, 0) >= 0);
        CHECK(ScIndex(g, "Hallowed Fountain", 0) < 0);
        CHECK(ScCount(g, "Plains", 0) == 2);
    });
}

TEST_CASE("White Orchid's basic pick is itself searched (a land_search_pins variant: decline)")
{
    GameState s = ScBoard({ "Plains", "Plains", "Godless Shrine" });
    s.players[0].hand.push_back(ScZone(kWop, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<int> decline{ kLandSearchPinMask };
    const TurnSolver::Plan* dec = ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "Godless Shrine", &decline);
    REQUIRE(dec != nullptr);
    const std::size_t lib0 = s.players[0].library.size();
    ScCheckWorlds(s, *dec, [lib0](const GameState& g) {
        CHECK(ScIndex(g, "Godless Shrine", 0) < 0);
        CHECK(ScCount(g, "Plains", 0) == 2);                 // declined: no basic
        CHECK(g.players[0].library.size() == lib0);
    });
}

TEST_CASE("SpiritsProvider prunes the own-land variants only when no basic is left in the library")
{
    GameState s = ScBoard({ "Plains", "Plains", "Godless Shrine" }, /*basics_in_library=*/false);
    s.players[0].hand.push_back(ScZone(kWop, 30));
    CHECK_FALSE(SpiritsProv().OfferOwnNonbasicLandDestroy(s, 0));
    CHECK(GenericProvider().OfferOwnNonbasicLandDestroy(s, 0));   // the root offers always (lossless)
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(ScFindPlan(plans, Action::Kind::CastFromHand, kWop, "") != nullptr);
    CHECK(ScPinnedVariants(plans, kWop) == 0);
    s.players[0].library.push_back(ScZone("Island", 990));
    CHECK(SpiritsProv().OfferOwnNonbasicLandDestroy(s, 0));
    const std::vector<TurnSolver::Plan> plans2 = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(ScPinnedVariants(plans2, kWop) > 0);
}
