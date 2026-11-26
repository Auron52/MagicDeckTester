// Enchanted Evening: the USER's Grove / tutor prune (rulings 15 / 16, 2026-11-25/26; ledger "USER rulings" 15-16,
// EnchantedEveningProvider in DecisionProviders.cpp). One lever, MTG_EE_USER_PRUNE (heurarm EE_USER_PRUNE), DEFAULT ON.
//   (A) no plan leaves a second Sterling Grove on the battlefield: a Grove cast is kept only beside a Grove sacrifice.
//   (B) the Grove sacrifice tutor's and Idyllic Tutor's targets: B1 only EE / Opalescence / Wave / one mana Aura /
//       Presence; B2 no duplicates; B3 Wild Growth vs Utopia Sprawl; B4 the mana thresholds; B5 Presence; B6 the order;
//       B7 nothing kept -> not cast / not activated, the idle pass stays.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kEE     = "Enchanted Evening";
const char* const kOpal   = "Opalescence";
const char* const kWave   = "Parallax Wave";
const char* const kPres   = "Enchantress's Presence";
const char* const kGrove  = "Sterling Grove";
const char* const kWG     = "Wild Growth";
const char* const kSprawl = "Utopia Sprawl";
const char* const kExpl   = "Exploration";
const char* const kORing  = "Oblivion Ring";
const char* const kTutor  = "Idyllic Tutor";

const CardDefinition& GpDef(const std::string& name)
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

Card GpCard(const std::string& name, int number)
{
    Card c = GpDef(name).card;
    c.m_number = number;
    return c;
}

int GpPut(GameState& s, const std::string& name, int number, int attached_to = 0)
{
    Permanent p;
    p.card = GpCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.aura_attached_to = attached_to;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const Decklist& GpDeck()
{
    static const Decklist d = [] {
        Decklist k;
        int n = 1;
        for (const char* nm : { kEE, kOpal, kWave, kPres, kGrove, kWG, kSprawl, kExpl, kORing, kTutor, "Forest", "Plains" })
        { k.mainboard.push_back(GpCard(nm, n++)); }
        return k;
    }();
    return d;
}

// Our main phase 1, the land drop spent, every enchantment of the list in the library (plus filler).
GameState GpBoard()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_float_wild_exact = true;
    s.m_provider = &DetectDecisionProvider(GpDeck());
    GoldFishRunner::StampDeckTraits(s, GpDeck());
    int n = 500;
    for (const char* nm : { kEE, kOpal, kWave, kPres, kGrove, kWG, kSprawl, kExpl, kORing, "Plains", "Forest" })
    { s.players[0].library.push_back(GpCard(nm, n++)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(GpCard("Plains", 100900 + k)); }
    return s;
}

void GpDropFromLibrary(GameState& s, const std::string& name)
{
    Library kept;
    for (const Card& c : s.players[0].library) { if (c.m_name != name) { kept.push_back(c); } }
    s.players[0].library = std::move(kept);
}

const CardParams& GroveTutor() { return *GpDef(kGrove).activated_tutor; }
const CardParams& IdyllicTutor() { return GpDef(kTutor).params; }

std::vector<std::string> Cands(const GameState& s, const CardParams& pp)
{ return ResolveProvider(s).TutorCandidates(s, 0, pp); }

void CheckBoth(const GameState& s, const std::vector<std::string>& want)
{
    for (const CardParams* pp : { &GroveTutor(), &IdyllicTutor() })
    {
        INFO("tutor: " << std::string(pp == &GroveTutor() ? "Sterling Grove" : "Idyllic Tutor"));
        CHECK(Cands(s, *pp) == want);
    }
}

void AddLands(GameState& s, int forests, int plains, int base = 10)
{
    for (int k = 0; k < forests; ++k) { GpPut(s, "Forest", base + k); }
    for (int k = 0; k < plains; ++k) { GpPut(s, "Plains", base + 20 + k); }
}

bool PlanCastsGrove(const TurnSolver::Plan& p, int* n_cast = nullptr, int* n_sac = nullptr)
{
    int c = 0, sc = 0;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name == kGrove) { ++c; }
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacTutorTop) { ++sc; }
    }
    if (n_cast != nullptr) { *n_cast = c; }
    if (n_sac != nullptr) { *n_sac = sc; }
    return c > 0;
}

struct ArmScope
{
    heurarm::Arm saved = heurarm::t_arm;
    explicit ArmScope(int v) { heurarm::t_arm[heurarm::EE_USER_PRUNE] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm = saved; }
};
}   // namespace

TEST_CASE("EE prune: routed to the deck's provider; the lever is a heurarm slot, default ON")
{
    GameState s = GpBoard();
    CHECK(std::string(ResolveProvider(s).Name()) == "EnchantedEvening");
    CHECK(heurarm::SlotOf("MTG_EE_USER_PRUNE") == heurarm::EE_USER_PRUNE);
    CHECK(eeprune::On());
    ArmScope off(0);
    CHECK_FALSE(eeprune::On());
}

TEST_CASE("EE prune B1 + B6: only the five kinds, missing lock pieces first (EE, Opalescence, Wave), then one mana Aura, then Presence")
{
    GameState s = GpBoard();
    AddLands(s, 0, 2);   // 2 total, 2 white: mana short -> the mana option and Presence are kept
    CheckBoth(s, { kEE, kOpal, kWave, kWG, kPres });
    // Never Sterling Grove, Exploration, Oblivion Ring.
    for (const std::string& n : Cands(s, IdyllicTutor()))
    { CHECK(n != kGrove); CHECK(n != kExpl); CHECK(n != kORing); }
    // Lever off: the base list (every legal enchantment, library order).
    ArmScope off(0);
    const std::vector<std::string> base = Cands(s, IdyllicTutor());
    CHECK(base.size() == 9);
    CHECK(std::find(base.begin(), base.end(), kGrove) != base.end());
    CHECK_FALSE(ResolveProvider(s).TutorNeedsCandidate(s, 0, IdyllicTutor()));
}

TEST_CASE("EE prune B2: a piece already in hand or on our battlefield is never offered (Presence included)")
{
    GameState s = GpBoard();
    AddLands(s, 0, 2);
    s.players[0].hand.push_back(GpCard(kEE, 200));
    GpPut(s, kOpal, 201);
    s.players[0].hand.push_back(GpCard(kPres, 202));
    CheckBoth(s, { kWave, kWG });
    // The opponent's copy does not count.
    GameState t = GpBoard();
    AddLands(t, 0, 2);
    Permanent theirs;
    theirs.card = GpCard(kEE, 100300);
    theirs.controller_index = theirs.owner_index = 1;
    t.battlefield.push_back(theirs);
    CheckBoth(t, { kEE, kOpal, kWave, kWG, kPres });
}

TEST_CASE("EE prune B3: Wild Growth by default; Utopia Sprawl when short of white with a Forest, or with 2+ Forests; the one left")
{
    {   // 2 Plains + 1 Forest: white 2, one Forest -> Wild Growth
        GameState s = GpBoard();
        AddLands(s, 1, 2);
        CHECK(eeprune::CensusOf(s, 0).forests == 1);
        CheckBoth(s, { kEE, kOpal, kWave, kWG, kPres });
    }
    {   // 1 Forest + 1 Plains: short of white with a Forest host -> Utopia Sprawl
        GameState s = GpBoard();
        AddLands(s, 1, 1);
        CheckBoth(s, { kEE, kOpal, kWave, kSprawl, kPres });
    }
    {   // 2 Forests + 1 Plains: short of white AND 2 Forests -> Utopia Sprawl
        GameState s = GpBoard();
        AddLands(s, 2, 1);
        CheckBoth(s, { kEE, kOpal, kWave, kSprawl, kPres });
    }
    {   // 1 Plains: short of white but no Forest host (and no fetch) -> Wild Growth
        GameState s = GpBoard();
        GpPut(s, "Plains", 30);
        CHECK(eeprune::CensusOf(s, 0).white == 1);
        CheckBoth(s, { kEE, kOpal, kWave, kWG, kPres });   // short of white but no Forest host -> Wild Growth
    }
    {   // Wild Growth wanted but only Utopia Sprawl left -> Sprawl
        GameState s = GpBoard();
        AddLands(s, 0, 2);
        GpDropFromLibrary(s, kWG);
        CheckBoth(s, { kEE, kOpal, kWave, kSprawl, kPres });
    }
}

TEST_CASE("EE prune B4: the mana thresholds (< 4 or < 2 white kept; 4 only when no lock piece is fetchable; >= 5 pruned)")
{
    {   // 5 lands, 2 white: fine -> no mana option, and (a piece fetchable) no Presence
        GameState s = GpBoard();
        AddLands(s, 3, 2);
        const eeprune::Census c = eeprune::CensusOf(s, 0);
        CHECK(c.total == 5);
        CHECK(c.white == 2);
        CHECK_FALSE(eeprune::ManaNeeded(c));
        CheckBoth(s, { kEE, kOpal, kWave });
    }
    {   // 5 lands but 1 white: mana needed
        GameState s = GpBoard();
        AddLands(s, 4, 1);
        CheckBoth(s, { kEE, kOpal, kWave, kSprawl, kPres });
    }
    {   // exactly 4 (3 lands + Wild Growth), 2 white, a piece fetchable: the mana option is pruned
        GameState s = GpBoard();
        AddLands(s, 1, 2);
        GpPut(s, kWG, 40, /*attached_to=*/10);
        const eeprune::Census c = eeprune::CensusOf(s, 0);
        CHECK(c.total == 4);
        CHECK(c.white == 2);
        CheckBoth(s, { kEE, kOpal, kWave });
    }
    {   // exactly 4, 2 white, every lock piece held: mana is the only thing missing -> kept (and Presence after it);
        // 1 Forest -> Wild Growth, 2 Forests -> Utopia Sprawl
        GameState s = GpBoard();
        AddLands(s, 1, 3);
        s.players[0].hand.push_back(GpCard(kEE, 200));
        s.players[0].hand.push_back(GpCard(kOpal, 201));
        s.players[0].hand.push_back(GpCard(kWave, 202));
        CheckBoth(s, { kWG, kPres });
        GameState t = GpBoard();
        AddLands(t, 2, 2);
        t.players[0].hand = s.players[0].hand;
        CheckBoth(t, { kSprawl, kPres });
    }
    {   // a Utopia Sprawl locked to white counts as a white unit: 3 lands (1 white) + white Sprawl = 4 total, 2 white
        GameState s = GpBoard();
        AddLands(s, 2, 1);
        const int i = GpPut(s, kSprawl, 41, /*attached_to=*/10);
        s.battlefield[static_cast<std::size_t>(i)].chosen_color = static_cast<int>(Color::White);
        const eeprune::Census c = eeprune::CensusOf(s, 0);
        CHECK(c.total == 4);
        CHECK(c.white == 2);
        CheckBoth(s, { kEE, kOpal, kWave });
    }
    {   // an uncracked fetch counts one unit that can be white (Windswept Heath finds a Plains)
        GameState s = GpBoard();
        AddLands(s, 2, 1);
        GpPut(s, "Windswept Heath", 50);
        const eeprune::Census c = eeprune::CensusOf(s, 0);
        CHECK(c.total == 4);
        CHECK(c.white == 2);
        CHECK(c.fetch_forest);
    }
}

TEST_CASE("EE prune B5: Presence pruned while a lock piece is fetchable and mana is fine; offered otherwise; never a second")
{
    {   // every lock piece held, mana fine (5, 2 white): Presence alone
        GameState s = GpBoard();
        AddLands(s, 3, 2);
        s.players[0].hand.push_back(GpCard(kEE, 200));
        GpPut(s, kOpal, 201);
        GpPut(s, kWave, 202);
        CheckBoth(s, { kPres });
    }
    {   // a lock piece missing but none left in the library (not fetchable): Presence offered
        GameState s = GpBoard();
        AddLands(s, 3, 2);
        s.players[0].hand.push_back(GpCard(kEE, 200));
        s.players[0].hand.push_back(GpCard(kOpal, 201));
        GpDropFromLibrary(s, kWave);
        CHECK_FALSE(eeprune::LockPieceFetchable(eeprune::CensusOf(s, 0)));
        CheckBoth(s, { kPres });
    }
    {   // Presence already ours: never offered, even with nothing else to fetch
        GameState s = GpBoard();
        AddLands(s, 3, 2);
        s.players[0].hand.push_back(GpCard(kEE, 200));
        GpPut(s, kOpal, 201);
        GpPut(s, kWave, 202);
        GpPut(s, kPres, 203);
        CheckBoth(s, {});
    }
}

TEST_CASE("EE prune B7: nothing kept -> Idyllic Tutor is not cast, the Grove is not activated, the idle pass stays")
{
    GameState s = GpBoard();
    AddLands(s, 3, 3);   // 6 mana, 3 white: fine
    s.players[0].hand.push_back(GpCard(kEE, 200));
    GpPut(s, kOpal, 201);
    GpPut(s, kWave, 202);
    GpPut(s, kPres, 203);
    GpPut(s, kGrove, 204);
    s.players[0].hand.push_back(GpCard(kTutor, 205));
    REQUIRE(Cands(s, IdyllicTutor()).empty());
    CHECK(ResolveProvider(s).TutorNeedsCandidate(s, 0, IdyllicTutor()));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            CHECK_FALSE((a.kind == Action::Kind::CastFromHand && a.card_name == kTutor));
            CHECK_FALSE((a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacTutorTop));
        }
    }
    const std::vector<TurnSolver::Plan> pass = TurnSolver::IdlePassWithAxes(s);
    REQUIRE_FALSE(pass.empty());
    CHECK(pass.front().actions.empty());
    // Lever off: the Tutor is cast and the Grove activated (the unrestricted search).
    ArmScope off(0);
    bool tutor = false, grove = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            tutor = tutor || (a.kind == Action::Kind::CastFromHand && a.card_name == kTutor);
            grove = grove || (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacTutorTop);
        }
    }
    CHECK(tutor);
    CHECK(grove);
}

TEST_CASE("EE prune A: a second Sterling Grove is cast only beside a sacrifice of the first (lead or trail); never two at once")
{
    // One Grove ours, one in hand, mana for the cast AND the {1} activation; Enchanted Evening in the library (a target).
    GameState s = GpBoard();
    AddLands(s, 2, 2);
    GpPut(s, kGrove, 204);
    s.players[0].hand.push_back(GpCard(kGrove, 206));
    int with_sac = 0, alone = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int c = 0, sc = 0;
        if (!PlanCastsGrove(p, &c, &sc)) { continue; }
        if (sc > 0) { ++with_sac; } else { ++alone; }
    }
    CHECK(alone == 0);
    CHECK(with_sac > 0);
    {   // lever off: the lone second-Grove cast is a plan
        ArmScope off(0);
        int lone = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        { int c = 0, sc = 0; if (PlanCastsGrove(p, &c, &sc) && sc == 0) { ++lone; } }
        CHECK(lone > 0);
    }
    // No Grove ours, two in hand: one cast is fine, both in one plan never (nothing to sacrifice).
    GameState t = GpBoard();
    AddLands(t, 2, 2);
    t.players[0].hand.push_back(GpCard(kGrove, 206));
    t.players[0].hand.push_back(GpCard(kGrove, 207));
    int one = 0, two = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, true))
    { int c = 0, sc = 0; PlanCastsGrove(p, &c, &sc); if (c == 1) { ++one; } if (c >= 2) { ++two; } }
    CHECK(one > 0);
    CHECK(two == 0);
    // The Grove ours but its {1} unpayable after the cast (exactly {G}{W}): the second Grove is not offered at all.
    GameState u = GpBoard();
    AddLands(u, 1, 1);
    GpPut(u, kGrove, 204);
    u.players[0].hand.push_back(GpCard(kGrove, 206));
    int any = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, true)) { if (PlanCastsGrove(p)) { ++any; } }
    CHECK(any == 0);
    // ...and the idle pass is still offered.
    CHECK_FALSE(TurnSolver::IdlePassWithAxes(u).empty());
}

TEST_CASE("EE prune amendment arm MTG_EE_GROVE_PRELOCK: before EE is ours, with an Opalescence, the Grove is a beater")
{
    struct PreScope
    {
        heurarm::Arm saved = heurarm::t_arm;
        PreScope() { heurarm::t_arm[heurarm::EE_GROVE_PRELOCK] = 1; }
        ~PreScope() { heurarm::t_arm = saved; }
    };
    GameState s = GpBoard();
    AddLands(s, 2, 2);
    GpPut(s, kGrove, 204);
    s.players[0].hand.push_back(GpCard(kGrove, 206));
    s.players[0].hand.push_back(GpCard(kOpal, 207));
    CHECK_FALSE(eeprune::GrovePrelockException(s, 0));   // default OFF
    {
        PreScope on;
        CHECK(eeprune::GrovePrelockException(s, 0));
        int lone = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        { int c = 0, sc = 0; if (PlanCastsGrove(p, &c, &sc) && sc == 0) { ++lone; } }
        CHECK(lone > 0);                                   // the second Grove alone is a plan again
        const std::vector<std::string> cands = Cands(s, IdyllicTutor());
        REQUIRE_FALSE(cands.empty());
        CHECK(cands.back() == kGrove);                     // a tutor target, ordered last
        CHECK(cands.front() == kEE);
        // Enchanted Evening ours: the lock is assembling -- the exception is off, the prune binds again.
        GameState t = s;
        GpPut(t, kEE, 208);
        CHECK_FALSE(eeprune::GrovePrelockException(t, 0));
        const std::vector<std::string> c2 = Cands(t, IdyllicTutor());
        CHECK(std::find(c2.begin(), c2.end(), kGrove) == c2.end());
        // No Opalescence anywhere: off.
        GameState u = GpBoard();
        AddLands(u, 2, 2);
        CHECK_FALSE(eeprune::GrovePrelockException(u, 0));
    }
}

TEST_CASE("EE prune amendment MTG_EE_GROVE_PRELOCK, second Opalescence: before EE is ours, with an Opalescence out, a "
          "second copy is a tutor target after the missing lock pieces (seed 10000 gi=50)")
{
    struct PreScope
    {
        heurarm::Arm saved = heurarm::t_arm;
        PreScope() { heurarm::t_arm[heurarm::EE_GROVE_PRELOCK] = 1; }
        ~PreScope() { heurarm::t_arm = saved; }
    };
    GameState s = GpBoard();   // the library holds one more Opalescence
    AddLands(s, 2, 2);
    GpPut(s, kOpal, 207);      // one Opalescence ours
    // Default OFF: B2 -- never a duplicate.
    CHECK_FALSE(eeprune::SecondOpalExtension(s, 0));
    CheckBoth(s, { kEE, kWave });
    {
        PreScope on;
        CHECK(eeprune::SecondOpalExtension(s, 0));
        // Listed after the missing lock pieces (EE, Wave); Sterling Grove (the amendment's beater) stays last.
        for (const CardParams* pp : { &GroveTutor(), &IdyllicTutor() })
        {
            const std::vector<std::string> c = Cands(s, *pp);
            REQUIRE(c.size() >= 3);
            CHECK(c[0] == kEE);
            CHECK(c[1] == kWave);
            CHECK(c[2] == kOpal);
            CHECK(c.back() == kGrove);
        }
        // A second Opalescence already in hand: no third.
        GameState h = s;
        h.players[0].hand.push_back(GpCard(kOpal, 209));
        CHECK_FALSE(eeprune::SecondOpalExtension(h, 0));
        const std::vector<std::string> ch = Cands(h, IdyllicTutor());
        CHECK(std::count(ch.begin(), ch.end(), std::string(kOpal)) == 0);
        // Enchanted Evening ours: the lock is assembling -- no second copy.
        GameState t = s;
        GpPut(t, kEE, 208);
        CHECK_FALSE(eeprune::SecondOpalExtension(t, 0));
        const std::vector<std::string> ct = Cands(t, IdyllicTutor());
        CHECK(std::count(ct.begin(), ct.end(), std::string(kOpal)) == 0);
        // Opalescence only in HAND (not ours yet): the extension needs one on the battlefield.
        GameState u = GpBoard();
        AddLands(u, 2, 2);
        u.players[0].hand.push_back(GpCard(kOpal, 207));
        CHECK_FALSE(eeprune::SecondOpalExtension(u, 0));
        // No copy left in the library: nothing to fetch.
        GameState w = s;
        GpDropFromLibrary(w, kOpal);
        const std::vector<std::string> cw = Cands(w, IdyllicTutor());
        CHECK(std::count(cw.begin(), cw.end(), std::string(kOpal)) == 0);
    }
}
