// Unit tests for the LANDFALL trigger infrastructure (2026-10-15) and the Landfall deck's payoff
// cards -- see OnLandEntered's contract in core/SpellEffects.h.
//
// Every failure guarded here is SILENT: a landfall trigger that never fires, fires twice, or fires
// before the permanent it should have seen is just a game that plays a little worse, and no
// committed deck holds a landfall card, so the regression suite cannot notice any of it.
//
//  1. ONE CHOKEPOINT, EVERY ROUTE. A PLAYED land (PlayLandFromHand), a land PUT by an effect
//     (EnterLand / PerformFetch), the fetchland's own collapsed entry, and a land reaching the
//     universal enter cascade (FireEtbWatchers) each trigger landfall exactly once.
//  2. SIMULTANEITY (CR 603.6a). Genesis Wave putting Rampaging Baloths beside five lands makes FIVE
//     Beasts; Avenger of Zendikar put beside lands counts them AND puts its counters on the Plants
//     its own ETB made (the dominant CR 603.3b trigger order); a land a neighbouring Karoo bounced
//     still triggers.
//  3. TARGETING (CR 603.3d). Phylath's landfall target exists when the trigger is put on the stack:
//     from a simultaneous Wave, a Plant made by that Wave is NOT a legal target, and with no Plant at
//     all the trigger does nothing. The identity fold makes identical Plants one class; the search
//     policy (rank) chooses among classes.
//  4. THE PAYOFFS: Baloths' 4/4 Beast, Avenger's counter on EVERY Plant (Phylath's too), Omnath's
//     red AND green 5/5 Elemental, Omnath's 3 damage when a legend-ruled Phylath dies, Khalni's quest
//     counter, Courser's life.
//  5. THE GATE: with GameState::deck_has_landfall clear, a land entry walks nothing -- the one bool
//     is the whole cost for a deck without a landfall card.
//  6. LAND-DROP TIMING (Plan::land_slot -1, after the casts): "cast Baloths, then play the land" is enumerated
//     and, applied, makes the Beast; the land-first plan stays enumerated beside it; a plan whose
//     casts put no landfall watcher gets no land-after variant.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"   // MtgTestSeam: hand the real executor a committed plan

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoadedLf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLf(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshLf()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A permanent ALREADY on the battlefield (entered on an earlier turn: not summoning sick).
int PutLf(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefLf(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card HandLf(const std::string& name, int number)
{
    Card c = DefLf(name).card;
    c.m_number = number;
    return c;
}

int CountNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

int CountSubtype(const GameState& s, const std::string& sub)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && p.card.IsCreature() && CardHasSubtype(p.card, sub)) { ++n; } }
    return n;
}

const Permanent* ByNumberLf(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

constexpr const char* kBaloths = "Rampaging Baloths";
constexpr const char* kAvenger = "Avenger of Zendikar";
constexpr const char* kOmnath  = "Omnath, Locus of Rage";
constexpr const char* kPhylath = "Phylath, World Sculptor";
constexpr const char* kCourser = "Courser of Kruphix";
constexpr const char* kKhalni  = "Khalni Heart Expedition";

}   // namespace

TEST_CASE("Landfall cards: printed data and params as Scryfall says")
{
    EnsureCardsLoadedLf();
    const CardDefinition& b = DefLf(kBaloths);
    CHECK(b.card.m_mana_cost.generic == 4);
    CHECK(b.card.m_mana_cost.green == 2);
    CHECK(b.card.m_power == 6);
    CHECK(b.card.HasKeyword(Keyword::Trample));
    CHECK(b.landfall_watcher);
    CHECK(b.params.landfall_creates_tokens == 1);
    CHECK(b.params.landfall_token_power == 4);

    const CardDefinition& a = DefLf(kAvenger);
    CHECK(a.card.m_mana_cost.generic == 5);
    CHECK(CardHasSubtype(a.card, "Elemental"));
    CHECK(a.landfall_watcher);
    CHECK(a.params.etb_tokens_per_land == 1);
    CHECK_FALSE(a.params.etb_tokens_per_land_basic_only);

    const CardDefinition& o = DefLf(kOmnath);
    CHECK(o.card.HasSupertype(Supertype::Legendary));
    CHECK(o.card.m_mana_cost.red == 2);
    CHECK(o.card.m_mana_cost.green == 2);
    CHECK(o.params.dies_watch_subtype == "Elemental");
    CHECK(o.params.dies_trigger_damage == 3);

    const CardDefinition& p = DefLf(kPhylath);
    CHECK(p.card.HasSupertype(Supertype::Legendary));
    CHECK(CardHasSubtype(p.card, "Elemental"));   // a legend-ruled Phylath feeds Omnath
    CHECK(p.params.etb_tokens_per_land_basic_only);
    CHECK(p.params.landfall_target_counters == 4);

    CHECK(DefLf(kCourser).card.IsCreature());
    CHECK(DefLf(kCourser).card.HasType(CardType::Enchantment));
    CHECK(DefLf(kCourser).params.landfall_lifegain == 1);
    CHECK(DefLf(kKhalni).params.landfall_quest_counters == 1);
    CHECK(DefLf(kKhalni).card.HasKeyword(Keyword::Landfall));   // pasted verbatim from Scryfall

    // Searing Blaze's landfall CONDITION is not a trigger, and a basic land is not a watcher.
    CHECK_FALSE(DefLf("Searing Blaze").landfall_watcher);
    CHECK_FALSE(DefLf("Forest").landfall_watcher);
}

TEST_CASE("Basic lands: the shared predicate keys on the Basic supertype, never the template")
{
    EnsureCardsLoadedLf();
    CHECK(IsBasicLandCard(DefLf("Forest")));
    CHECK(IsBasicLandCard(DefLf("Mountain")));   // was missing "Basic" in cards.json
    CHECK(IsBasicLandCard(DefLf("Island")));
    CHECK(IsBasicLandCard(DefLf("Snow-Covered Forest")));
    CHECK_FALSE(IsBasicLandCard(DefLf("Gruul Turf")));   // template basic_land, but NOT basic
    // The Card form resolves a name-only zone placeholder through its definition.
    Card placeholder;
    placeholder.m_name = CardDatabase::Instance().CanonicalName("Mountain");
    placeholder.RehashName();
    CHECK(IsBasicLandCard(placeholder));
}

TEST_CASE("Landfall route 1: a PLAYED land triggers Rampaging Baloths once")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kBaloths, 1);
    s.players[0].hand.push_back(HandLf("Forest", 2));
    REQUIRE(PlayLandFromHand(s, 0, DefLf("Forest"), LandPlayOptions{}));
    CHECK(CountNamed(s, "4/4 Beast Token") == 1);
    const Permanent& beast = s.battlefield.back();
    CHECK(beast.is_token);
    CHECK(beast.entered_this_turn);                       // summoning sick: attacks next turn
    CHECK(beast.card.HasColor(Color::Green));
    CHECK(s.players[0].lands_played_this_turn == 1);
}

TEST_CASE("Landfall route 2: a land PUT by an effect triggers too (EnterLand)")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kBaloths, 1);
    EnterLand(s, DefLf("Forest"), 7);
    CHECK(CountNamed(s, "4/4 Beast Token") == 1);
    CHECK(s.players[0].lands_played_this_turn == 0);   // a put land is not PLAYED
    // ...and the deferral opt-out fires nothing (its caller fires the batch itself).
    EnterLand(s, DefLf("Forest"), 8, /*resolve_full_etb=*/false, /*fire_landfall=*/false);
    CHECK(CountNamed(s, "4/4 Beast Token") == 1);
}

TEST_CASE("Landfall: a fetchland's drop is TWO landfall triggers -- the fetchland and the land it fetches")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kBaloths, 1);
    s.players[0].hand.push_back(HandLf("Wooded Foothills", 2));
    s.players[0].library.push_back(HandLf("Forest", 3));
    s.players[0].library.push_back(HandLf("Mountain", 4));
    REQUIRE(PlayLandFromHand(s, 0, DefLf("Wooded Foothills"), LandPlayOptions{}));
    CHECK(CountNamed(s, "4/4 Beast Token") == 2);
}

TEST_CASE("Landfall route 3: a land entrant reaching the universal enter cascade triggers once")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kBaloths, 1);
    const int idx = PutLf(s, "Forest", 9, /*sick=*/true);
    FireEtbWatchers(s, 0, idx);
    CHECK(CountNamed(s, "4/4 Beast Token") == 1);
    FireEtbWatchers(s, 0, idx, /*fire_landfall=*/false);   // the double-registration opt-out
    CHECK(CountNamed(s, "4/4 Beast Token") == 1);
}

TEST_CASE("Landfall gate: with deck_has_landfall clear a land entry walks nothing")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kBaloths, 1);
    s.deck_has_landfall = false;   // an inconsistent board ON PURPOSE: the gate alone decides
    EnterLand(s, DefLf("Forest"), 7);
    CHECK(CountNamed(s, "4/4 Beast Token") == 0);
}

TEST_CASE("Avenger of Zendikar: Plants per land, then a counter on EVERY Plant -- Phylath's included")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 4; ++i) { PutLf(s, "Forest", 10 + i); }
    PutLf(s, "Gruul Turf", 20);                              // a nonbasic land counts for Avenger
    const int av = PutLf(s, kAvenger, 1, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, av);
    CHECK(CountSubtype(s, "Plant") == 5);                    // one per land, nonbasic included
    const int ph = PutLf(s, kPhylath, 2, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, ph);
    CHECK(CountSubtype(s, "Plant") == 9);                    // + one per BASIC land (4 Forests)
    // A land: Avenger puts +1/+1 on all nine Plants; Phylath puts four on ONE.
    EnterLand(s, DefLf("Forest"), 30);
    int total_plus = 0, with_counter = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (!CardHasSubtype(p.card, "Plant")) { continue; }
        const int c = PlusCountersOn(p);
        total_plus += c;
        if (c > 0) { ++with_counter; }
    }
    CHECK(with_counter == 9);
    CHECK(total_plus == 9 + 4);
}

TEST_CASE("Omnath: a red AND green 5/5 Elemental per land; a legend-ruled Phylath is 3 to the face")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kOmnath, 1);
    EnterLand(s, DefLf("Forest"), 7);
    REQUIRE(CountNamed(s, "5/5 Elemental Token") == 1);
    const Permanent& el = s.battlefield.back();
    CHECK(el.card.HasColor(Color::Red));
    CHECK(el.card.HasColor(Color::Green));
    CHECK(el.entered_this_turn);
    // Two Phylaths: the legend rule bins one -- a DEATH (CR 704.5j / 700.4) of an Elemental.
    PutLf(s, kPhylath, 2);
    PutLf(s, kPhylath, 3, /*sick=*/true);
    EnforceLegendRule(s, 0);
    CHECK(CountNamed(s, kPhylath) == 1);
    CHECK(s.players[1].life == 17);
}

TEST_CASE("Khalni Heart Expedition gains a quest counter, Courser of Kruphix gains a life, per land")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    const int kh = PutLf(s, kKhalni, 1);
    PutLf(s, kCourser, 2);
    const int kh_num = s.battlefield[static_cast<std::size_t>(kh)].card.m_number;
    EnterLand(s, DefLf("Forest"), 7);
    s.players[0].hand.push_back(HandLf("Mountain", 8));
    REQUIRE(PlayLandFromHand(s, 0, DefLf("Mountain"), LandPlayOptions{}));
    CHECK(ByNumberLf(s, kh_num)->quest_counters == 2);
    CHECK(s.players[0].life == 22);
    CHECK(s.players[0].life_gained_this_turn == 2);   // two life-gain EVENTS through GainLife
}

TEST_CASE("Phylath target: identical Plants fold to one class; the policy rank picks among classes")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kPhylath, 1);
    // Two classes of Plant: two OLD 0/1s that can attack now, and one SICK 0/1 made this turn.
    auto plant = [&](int num, bool sick)
    {
        CreateTokenOnce(s, 0, 0, 1, { "Plant" }, "G", {});
        s.battlefield.back().entered_this_turn = sick;
        s.battlefield.back().card.m_number = num;
    };
    plant(9001, false);
    plant(9002, false);
    plant(9003, true);
    // Default (rank 0): the provider's front -- a Plant that can attack now, the oldest of its class.
    EnterLand(s, DefLf("Forest"), 7);
    CHECK(PlusCountersOn(*ByNumberLf(s, 9001)) == 4);
    CHECK(PlusCountersOn(*ByNumberLf(s, 9002)) == 0);
    CHECK(PlusCountersOn(*ByNumberLf(s, 9003)) == 0);
    // The 4/5 is now its own class. Rank 1 under the policy = the next class: the 0/1 that can attack.
    {
        ScriptedLandfallTarget pin(1, s.turn_number);
        EnterLand(s, DefLf("Forest"), 8);
    }
    CHECK(PlusCountersOn(*ByNumberLf(s, 9002)) == 4);
    // Rank 2 = the sick class. A rank past the end clamps to the last class (never a whiff).
    {
        ScriptedLandfallTarget pin(7, s.turn_number);
        EnterLand(s, DefLf("Forest"), 10);
    }
    CHECK(PlusCountersOn(*ByNumberLf(s, 9003)) == 4);
    // The policy is TURN-SCOPED: installed for another turn, it steers nothing here.
    {
        ScriptedLandfallTarget pin(2, s.turn_number + 1);
        CHECK(LandfallTargetPinFor(s) == -1);
    }
}

TEST_CASE("Phylath target: no Plant means the trigger is removed and nothing happens")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kPhylath, 1);
    EnterLand(s, DefLf("Forest"), 7);
    for (const Permanent& p : s.battlefield) { CHECK(PlusCountersOn(p) == 0); }
}

TEST_CASE("Genesis Wave: Baloths put beside five lands makes FIVE Beasts (CR 603.6a)")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    s.players[0].library.push_back(HandLf(kBaloths, 40));
    for (int i = 0; i < 5; ++i) { s.players[0].library.push_back(HandLf("Forest", 41 + i)); }
    PerformGenesisWave(s, 0, 6, "Genesis Wave");
    CHECK(CountNamed(s, kBaloths) == 1);
    CHECK(CountNamed(s, "Forest") == 5);
    CHECK(CountNamed(s, "4/4 Beast Token") == 5);
}

TEST_CASE("Genesis Wave: Avenger counts the Wave's lands AND its counters reach its own Plants")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, "Forest", 10);                        // one land before the Wave
    // Reveal order puts the LANDS FIRST -- the case reveal-order resolution got wrong (the landfall
    // counters resolved before Avenger's Plants existed).
    for (int i = 0; i < 3; ++i) { s.players[0].library.push_back(HandLf("Forest", 41 + i)); }
    s.players[0].library.push_back(HandLf(kAvenger, 40));
    PerformGenesisWave(s, 0, 7, "Genesis Wave");
    REQUIRE(CountSubtype(s, "Plant") == 4);         // 1 old + 3 Wave lands, counted at resolution
    for (const Permanent& p : s.battlefield)
    {
        if (!CardHasSubtype(p.card, "Plant")) { continue; }
        CHECK(PlusCountersOn(p) == 3);              // one per Wave land, on every Plant
    }
}

TEST_CASE("Genesis Wave: Phylath's landfall can only target a Plant that existed when the lands entered")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 2; ++i) { PutLf(s, "Forest", 10 + i); }
    // The Wave puts Phylath AND two lands: Phylath's ETB makes Plants, but its landfall triggers were
    // put on the stack BEFORE that ETB resolved (CR 603.3d) -- no Plant existed, so they do nothing.
    s.players[0].library.push_back(HandLf("Forest", 41));
    s.players[0].library.push_back(HandLf(kPhylath, 40));
    s.players[0].library.push_back(HandLf("Forest", 42));
    PerformGenesisWave(s, 0, 6, "Genesis Wave");
    REQUIRE(CountSubtype(s, "Plant") == 4);        // 4 basics at resolution
    for (const Permanent& p : s.battlefield) { CHECK(PlusCountersOn(p) == 0); }
}

TEST_CASE("Genesis Wave: a duplicate legend IS put (the alternative is the same graveyard)")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, kOmnath, 1);
    PutLf(s, kPhylath, 2);
    s.players[0].library.push_back(HandLf(kPhylath, 40));
    PerformGenesisWave(s, 0, 6, "Genesis Wave");
    CHECK(CountNamed(s, kPhylath) == 1);           // the legend rule binned one...
    CHECK(s.players[1].life == 17);                // ...and that death was Omnath's 3 to the face
}

TEST_CASE("Searing Blaze's landfall condition counts a PUT land, not only a played one")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    CHECK_FALSE(LandEnteredUnderControlThisTurn(s, 0));
    PutLf(s, "Mountain", 10);                      // an OLD land: entered on an earlier turn
    CHECK_FALSE(LandEnteredUnderControlThisTurn(s, 0));
    EnterLand(s, DefLf("Mountain"), 11);
    CHECK(LandEnteredUnderControlThisTurn(s, 0));
    CHECK(CreatureBurnDamage(DefLf("Searing Blaze"), s) == 3);
}

TEST_CASE("Land-drop timing: 'cast Baloths, THEN play the land' is enumerated and makes the Beast")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 6; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kBaloths, 1));
    s.players[0].hand.push_back(HandLf("Forest", 2));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* after = nullptr;
    bool land_first = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool casts_baloths = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == kBaloths) { casts_baloths = true; } }
        if (!casts_baloths || p.land_to_play != "Forest") { continue; }
        if (p.land_slot == TurnSolver::kLandSlotAfterCasts) { if (after == nullptr) { after = &p; } }
        else if (p.land_slot == 0)                          { land_first = true; }
    }
    CHECK(land_first);                              // both orders stay enumerable (lossless)
    REQUIRE(after != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *after, true);
    CHECK(CountNamed(t, kBaloths) == 1);
    CHECK(CountNamed(t, "4/4 Beast Token") == 1);   // the land entered AFTER Baloths
    CHECK(t.players[0].lands_played_this_turn == 1);
}

TEST_CASE("Land-drop timing: no landfall watcher cast in the plan -> no land-after variant")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 6; ++i) { PutLf(s, "Forest", 10 + i); }
    PutLf(s, kBaloths, 3);                          // a watcher ALREADY on the battlefield
    s.players[0].hand.push_back(HandLf("Terastodon", 1));   // a cast that is not a watcher
    s.players[0].hand.push_back(HandLf("Forest", 2));
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { CHECK(p.land_slot == 0); }
}

TEST_CASE("Land-drop timing: a deck with no landfall card never emits a land-after variant")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    s.deck_has_landfall = false;
    for (int i = 0; i < 6; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kBaloths, 1));
    s.players[0].hand.push_back(HandLf("Forest", 2));
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { CHECK(p.land_slot == 0); }
}

// ---- LAND-DROP POSITION (Plan::land_slot k >= 1, 2026-10-15) ------------------------------------
// The land in the MIDDLE of the cast sequence: right after the k-th landfall-watcher cast, so the
// casts after it may spend its mana. Before this axis "cast Avenger, play the land, cast Khalni with
// its mana" and "cast Khalni, play the land, cast the next spell with its mana" were inexpressible.

TEST_CASE("Land position: 'cast Avenger, play the land, cast Khalni with its mana' is enumerated and applied")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 8; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kAvenger, 1));
    s.players[0].hand.push_back(HandLf(kKhalni, 2));
    s.players[0].hand.push_back(HandLf("Forest", 3));
    const TurnSolver::Plan* mid = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool avenger = false, khalni = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == kAvenger) { avenger = true; }
            if (a.card_name.str() == kKhalni)  { khalni  = true; }
        }
        if (avenger && khalni && p.land_to_play == "Forest" && p.land_slot == 1) { mid = &p; break; }
    }
    REQUIRE(mid != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *mid, true);
    CHECK(CountNamed(t, kAvenger) == 1);
    CHECK(CountNamed(t, kKhalni) == 1);                  // paid with the land played after Avenger
    CHECK(t.players[0].lands_played_this_turn == 1);
    // Avenger's ETB counted 8 lands (the Forest came after it), and its landfall put a counter on
    // every one of those 8 Plants.
    int plants = 0, countered = 0;
    for (const Permanent& p : t.battlefield)
    {
        if (!CardHasSubtype(p.card, "Plant")) { continue; }
        ++plants;
        if (PlusCountersOn(p) >= 1) { ++countered; }
    }
    CHECK(plants == 8);
    CHECK(countered == 8);
}

TEST_CASE("Land position: tied-rank watchers go first -- 'Khalni, play the Mountain, Lava Spike off it'")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 3; ++i) { PutLf(s, "Forest", 10 + i); }
    // Lava Spike is listed FIRST in hand: Khalni and Lava Spike share a cast-order rank, so without the
    // position plan's watchers-first tie-break the canonical order would cast Lava Spike before the
    // Mountain exists.
    s.players[0].hand.push_back(HandLf("Lava Spike", 1));
    s.players[0].hand.push_back(HandLf(kKhalni, 2));
    s.players[0].hand.push_back(HandLf("Mountain", 3));
    const TurnSolver::Plan* mid = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool spike = false, khalni = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Lava Spike") { spike  = true; }
            if (a.card_name.str() == kKhalni)      { khalni = true; }
        }
        if (spike && khalni && p.land_to_play == "Mountain" && p.land_slot == 1) { mid = &p; break; }
    }
    REQUIRE(mid != nullptr);
    GameState t = s;
    const int opp_before = t.players[1].life;
    TurnSolver::ApplyPlan(t, *mid, true);
    CHECK(t.players[1].life == opp_before - 3);          // Lava Spike resolved off the Mountain
    const Permanent* kh = nullptr;
    for (const Permanent& p : t.battlefield) { if (p.card.m_name.str() == kKhalni) { kh = &p; } }
    REQUIRE(kh != nullptr);
    CHECK(kh->quest_counters == 1);                      // the Mountain entered after Khalni
}

TEST_CASE("Land position: one variant per watcher cast, none for a plan casting no watcher")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 9; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kKhalni, 1));
    s.players[0].hand.push_back(HandLf(kCourser, 2));
    s.players[0].hand.push_back(HandLf("Lava Spike", 3));
    s.players[0].hand.push_back(HandLf("Forest", 4));
    int max_slot = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int watchers = 0;
        for (const Action& a : p.actions) { if (TurnSolver::PutsLandfallWatcher(a)) { ++watchers; } }
        CHECK(p.land_slot <= watchers);                  // a position names a watcher this plan casts
        if (p.land_slot > max_slot) { max_slot = p.land_slot; }
    }
    CHECK(max_slot == 2);                                // Khalni + Courser in one plan -> positions 1, 2
}

TEST_CASE("Land position: a land Aura cast onto the positioned land is held until the land is played, then cast")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 7; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kBaloths, 1));
    s.players[0].hand.push_back(HandLf("Mountain", 2));
    s.players[0].hand.push_back(HandLf("Wild Growth", 3));
    // "Baloths (six Forests), THEN the Mountain (a Beast), THEN Wild Growth on that Mountain (the
    // seventh Forest)". The autonomous host ranking aims the Aura at a Forest, but the human (the
    // viewer offers every host) can aim it at the land being played: re-aim the enumerated
    // position-1 plan's Aura at the Mountain (enchant_target = its number) and apply it. The Aura
    // has no host until the land is played, so the apply HOLDS it and must cast it right after the
    // positioned land (play_deferred_land), not drop it.
    TurnSolver::Plan mid;
    bool found = false;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool baloths = false, growth = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == kBaloths)      { baloths = true; }
            if (a.card_name.str() == "Wild Growth") { growth  = true; }
        }
        if (baloths && growth && p.land_to_play == "Mountain" && p.land_slot == 1) { mid = p; found = true; break; }
    }
    REQUIRE(found);
    for (Action& a : mid.actions) { if (a.card_name.str() == "Wild Growth") { a.enchant_target = 2; } }
    GameState t = s;
    TurnSolver::ApplyPlan(t, mid, true);
    CHECK(CountNamed(t, "4/4 Beast Token") == 1);
    const Permanent* wg = nullptr;
    for (const Permanent& p : t.battlefield) { if (p.card.m_name.str() == "Wild Growth") { wg = &p; } }
    REQUIRE(wg != nullptr);                              // released after the land, not dropped
    CHECK(wg->aura_attached_to == 2);
}

// ---- UNPAYABLE PREFIX (Landfall claude-play sweep flag 1, 2026-10-16) ----------------------------
// A position clone inherits casts enumerated WITH the land's mana; a position whose casts before the
// land cannot be paid without it is not emitted. Applied, such a line dropped the cast and played the
// land anyway -- in BOTH worlds, so nothing ever scored a phantom quest counter (checked below on the
// rollout's own apply) -- and ran the rest of the plan: a different line than the one offered.

TEST_CASE("Land position: 'Khalni, then the Forest' with one Forest in play is not offered")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, "Forest", 10);                              // ONE land: Khalni ({1}{G}) needs the drop
    s.players[0].hand.push_back(HandLf(kKhalni, 1));
    s.players[0].hand.push_back(HandLf("Forest", 2));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* land_first = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool khalni = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == kKhalni) { khalni = true; } }
        if (!khalni) { continue; }
        CHECK(p.land_slot == 0);                         // never "after Khalni": unpayable without it
        if (p.land_to_play == "Forest") { land_first = &p; }
    }
    REQUIRE(land_first != nullptr);                      // the payable line stays

    // The rollout's own apply of the degenerate clone (what the search used to score): Khalni is
    // DROPPED -- not cast for free -- and the Forest is played anyway. No phantom quest counter.
    TurnSolver::Plan degenerate = *land_first;
    degenerate.land_slot = 1;
    GameState t = s;
    TurnSolver::ApplyPlan(t, degenerate, true);
    CHECK(CountNamed(t, kKhalni) == 0);
    CHECK(t.players[0].lands_played_this_turn == 1);
    CHECK(CountNamed(t, "Forest") == 2);
    int khalni_in_hand = 0;
    for (const Card& c : t.players[0].hand) { if (c.m_name.str() == kKhalni) { ++khalni_in_hand; } }
    CHECK(khalni_in_hand == 1);
}

TEST_CASE("Land position: gi10's 'land after Phylath; Phylath, Khalni crack' is not offered -- its realised line is")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 4; ++i) { PutLf(s, "Forest", 10 + i); }
    PutLf(s, "Mountain", 14);                            // five lands: Phylath ({4}{R}{G}) needs the sixth
    const int kh = PutLf(s, kKhalni, 20);
    s.battlefield[static_cast<std::size_t>(kh)].quest_counters = 2;   // the land's landfall fills it
    for (int i = 0; i < 6; ++i) { s.players[0].library.push_back(HandLf("Forest", 40 + i)); }   // crack targets
    s.players[0].hand.push_back(HandLf(kPhylath, 1));
    s.players[0].hand.push_back(HandLf("Forest", 2));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool phylath_first_offered = false, crack_line = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool phylath = false, crack = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == kPhylath && a.kind == Action::Kind::CastFromHand) { phylath = true; }
            if (TurnSolver::IsLandSearchActivation(a)) { crack = true; }
        }
        if (phylath && p.land_slot >= 1) { phylath_first_offered = true; }
        if (crack && !phylath && p.land_to_play == "Forest" && p.land_slot == 0) { crack_line = true; }
    }
    CHECK_FALSE(phylath_first_offered);
    CHECK(crack_line);   // "Forest, then crack" -- what the degenerate pick actually did -- is its own plan
}

TEST_CASE("Land position: the EXECUTOR realises an unpayable-prefix position exactly as the rollout does")
{
    // The claude-play sweep asked whether the search could have been scoring a phantom quest counter:
    // did the rollout (ApplyPlanDirect) and the executor (AIEngine::TakeTurn) treat the degenerate
    // "Khalni, then the Forest" (one Forest in play) the same way? Both drop the unpayable Khalni and
    // play the Forest anyway (the slot hook counts the ATTEMPT in each world) -- no counter anywhere.
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    PutLf(s, "Forest", 10);
    s.players[0].hand.push_back(HandLf(kKhalni, 1));
    s.players[0].hand.push_back(HandLf("Forest", 2));
    for (int i = 0; i < 5; ++i) { s.players[0].library.push_back(HandLf("Forest", 40 + i)); }
    TurnSolver::Plan degenerate;
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.land_to_play != "Forest" || p.land_slot != 0 || p.actions.size() != 1) { continue; }
        if (p.actions[0].card_name.str() != kKhalni) { continue; }
        degenerate = p; found = true; break;
    }
    REQUIRE(found);
    degenerate.land_slot = 1;

    GameState rollout = s;
    TurnSolver::ApplyPlan(rollout, degenerate, true);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, degenerate, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, [](GameState&) {});

    for (const GameState* g : { &rollout, &exec })
    {
        CHECK(CountNamed(*g, kKhalni) == 0);
        CHECK(CountNamed(*g, "Forest") == 2);
        CHECK(g->players[0].lands_played_this_turn == 1);
        int khalni_in_hand = 0;
        for (const Card& c : g->players[0].hand) { if (c.m_name.str() == kKhalni) { ++khalni_in_hand; } }
        CHECK(khalni_in_hand == 1);
        CHECK(g->stack.empty());
    }
}

// ---- THE k == -1 TWIN (Landfall claude-play sweep flag 2, 2026-10-16) ------------------------------
// "land right after the LAST watcher" and "land after the casts" over one cast list are one line when
// nothing after that watcher can see the land's position. The human menu hides the positioned twin
// only on an identical post-apply state; a position that matters is never a twin.

TEST_CASE("Land position twin: 'after Baloths' == 'after the casts' for a lone Baloths; mid-sequence is not")
{
    EnsureCardsLoadedLf();
    GameState s = FreshLf();
    for (int i = 0; i < 9; ++i) { PutLf(s, "Forest", 10 + i); }
    s.players[0].hand.push_back(HandLf(kBaloths, 1));
    s.players[0].hand.push_back(HandLf(kKhalni, 2));
    s.players[0].hand.push_back(HandLf("Forest", 3));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int lone_twin = 0, mid_twin = 0, last_twin = 0, lone = 0, mid = 0, last = 0;
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        const TurnSolver::Plan& p = plans[i];
        if (p.land_slot <= 0 || p.land_to_play != "Forest") { continue; }
        int watchers = 0;
        for (const Action& a : p.actions) { if (TurnSolver::PutsLandfallWatcher(a)) { ++watchers; } }
        const int twin = TurnSolver::PositionedLandTwinIndex(s, plans, i, true);
        if (twin >= 0)
        {
            CHECK(plans[static_cast<std::size_t>(twin)].land_slot == TurnSolver::kLandSlotAfterCasts);
            CHECK(plans[static_cast<std::size_t>(twin)].actions.size() == p.actions.size());
        }
        if (watchers == 1)                       { ++lone; if (twin >= 0) { ++lone_twin; } }
        else if (p.land_slot < watchers)         { ++mid;  if (twin >= 0) { ++mid_twin; } }
        else                                     { ++last; if (twin >= 0) { ++last_twin; } }
    }
    REQUIRE(lone >= 1);
    CHECK(lone_twin == lone);   // one watcher, nothing after it: the twin of the after-all plan
    REQUIRE(mid >= 1);
    CHECK(mid_twin == 0);       // the second watcher never sees the land: a different line
    CHECK(last_twin == last);   // after the last of two watchers, nothing after: the twin again
}
