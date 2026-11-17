// Maze's End deck, integration phase I2 (2026-11-17; docs/design/analysis-Maze's End.md): the land-entry
// pipeline. Controller-aware enters-tapped predicate, Gond Gate (untap replacement incl. a forced-tapped put,
// reflected colours), Amulet of Vigor (single + multi-copy float, every entry route), Thran Portal, Gateway
// Plaza's pay-or-sacrifice, the CLB chosen-colour Gates, The Black Gate, and the searched land-entry pin
// (land options per colour / pay choice, Maze's End fetch variants).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadI2Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    return c;
}

int Put(GameState& s, const char* name, int number, bool tapped = false, int controller = 0)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return number;
}

GameState Base()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

const Permanent* Find(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

int FindIdx(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { return i; } }
    return -1;
}

bool InGraveyard(const GameState& s, int num)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == num) { return true; } }
    return false;
}

// Drop the hand card `num` (must be in hand) with land-entry pin `pin`.
bool Drop(GameState& s, int num, int pin = -1)
{
    for (std::size_t i = 0; i < s.players[0].hand.size(); ++i)
    {
        if (s.players[0].hand[i].m_number != num) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(s.players[0].hand[i]);
        REQUIRE(d != nullptr);
        LandPlayOptions o;
        o.rad_mode = pin;
        return PlayLandFromHand(s, i, *d, o);
    }
    FAIL("card not in hand");
    return false;
}

bool Has(const std::vector<Color>& v, Color c) { return std::find(v.begin(), v.end(), c) != v.end(); }

// The colours the Maze's End list spends (Guild Summit U, Sylvan Scrying G (Exploration arrives in I3), Gates Ablaze R) in the library, so
// MazesEndProvider's spendable-colour restriction has its premise.
void StockSpendLibrary(GameState& s, int base)
{
    s.players[0].library.push_back(C("Guild Summit", base));
    s.players[0].library.push_back(C("Sylvan Scrying", base + 1));
    s.players[0].library.push_back(C("Gates Ablaze", base + 2));
}
}   // namespace

TEST_CASE("Maze's End I2: the enters-tapped predicate is judged for the entering land's controller")
{
    LoadI2Cards();
    GameState s = Base();
    // Player 1 controls three lands, player 0 one: Thran Portal (fastland, <= 2 other lands).
    Put(s, "Forest", 1, false, 0);
    Put(s, "Forest", 2, false, 1); Put(s, "Forest", 3, false, 1); Put(s, "Forest", 4, false, 1);
    const CardDefinition& thran = D("Thran Portal");
    CHECK_FALSE(LandWouldEnterTapped(s, thran));                 // default = the active player (0)
    CHECK_FALSE(LandWouldEnterTapped(s, thran, true, 0));
    CHECK(LandWouldEnterTapped(s, thran, true, 1));              // player 1 controls three others
    s.active_player_index = 1;
    CHECK(LandWouldEnterTapped(s, thran));                       // the default follows the active player
    CHECK_FALSE(LandWouldEnterTapped(s, thran, true, 0));        // ...an explicit controller does not
    s.active_player_index = 0;
    // Gond Gate is "Gates YOU control": an opponent's Gond does not untap our Gate.
    Put(s, "Gond Gate", 5, false, 1);
    const CardDefinition& az = D("Azorius Guildgate");
    CHECK(LandWouldEnterTapped(s, az, true, 0));
    CHECK_FALSE(LandWouldEnterTapped(s, az, true, 1));
    // The Black Gate's life is the controller's: player 1 at 2 life cannot pay 3.
    s.players[1].life = 2;
    s.battlefield.erase(s.battlefield.begin() + FindIdx(s, 5));
    CHECK(LandWouldEnterTapped(s, D("The Black Gate"), true, 1));
    CHECK_FALSE(LandWouldEnterTapped(s, D("The Black Gate"), true, 0));
}

TEST_CASE("Maze's End I2: Gond Gate untaps Gates on every route, never a non-Gate, and reflects Gate colours")
{
    LoadI2Cards();
    GameState s = Base();
    Put(s, "Gond Gate", 10);
    const CardDefinition& az = D("Azorius Guildgate");
    CHECK_FALSE(LandWouldEnterTapped(s, az));
    CHECK_FALSE(LandEntryHasChoice(s, D("The Black Gate")));     // Gond untaps it: no life choice
    CHECK(LandWouldEnterTapped(s, D("Maze's End")));            // not a Gate
    // Land drop.
    s.players[0].hand.push_back(C("Azorius Guildgate", 11));
    s.players[0].hand.push_back(C("The Black Gate", 12));
    REQUIRE(Drop(s, 11));
    CHECK_FALSE(Find(s, 11)->tapped);
    GameState t = s;
    t.players[0].lands_played_this_turn = 0;
    REQUIRE(Drop(t, 12));
    CHECK_FALSE(Find(t, 12)->tapped);
    CHECK(t.players[0].life == 20);                             // no shock life paid under a Gond
    // A forced-tapped put (Archdruid's Charm's "onto the battlefield tapped"): Gond still untaps it.
    GameState u = s;
    EnterLand(u, D("Simic Guildgate"), 13, false, true, LandEntryTap::ForcedTapped);
    CHECK_FALSE(Find(u, 13)->tapped);
    EnterLand(u, D("Maze's End"), 14, false, true, LandEntryTap::ForcedTapped);
    CHECK(Find(u, 14)->tapped);                                 // a non-Gate stays tapped
    // Without the Gond the forced put is tapped.
    GameState v = Base();
    EnterLand(v, D("Simic Guildgate"), 15, false, true, LandEntryTap::ForcedTapped);
    CHECK(Find(v, 15)->tapped);
    // Reflected colours: {C} + every colour a controlled Gate could produce, per permanent.
    GameState w = Base();
    Put(w, "Gond Gate", 20);
    Put(w, "Azorius Guildgate", 21, true);                      // tapped Gates count (CR 106.7)
    Put(w, "Sea Gate", 22);
    w.battlefield[static_cast<std::size_t>(FindIdx(w, 22))].chosen_color = static_cast<int8_t>(Color::Red);
    Put(w, "Forest", 23);                                       // not a Gate
    const Permanent& gond = *Find(w, 20);
    const std::vector<Color> cols = EffectiveProducesFor(w, 0, D("Gond Gate"), &gond);
    CHECK(Has(cols, Color::Colorless));
    CHECK(Has(cols, Color::White));
    CHECK(Has(cols, Color::Blue));
    CHECK(Has(cols, Color::Red));
    CHECK_FALSE(Has(cols, Color::Black));
    CHECK_FALSE(Has(cols, Color::Green));                       // the Forest is not a Gate
    const std::vector<Color> in_hand = EffectiveProduces(w, 0, D("Gond Gate"), /*in_hand=*/true);
    CHECK(in_hand.size() == 1);
    CHECK(Has(in_hand, Color::Colorless));
    // Gateway Plaza (any colour) makes Gond rainbow.
    Put(w, "Gateway Plaza", 24);
    const std::vector<Color> all = EffectiveProducesFor(w, 0, D("Gond Gate"), &gond);
    CHECK(all.size() == 6);
}

TEST_CASE("Maze's End I2: Amulet of Vigor untaps a tapped entrant; N Amulets float N-1 mana")
{
    LoadI2Cards();
    for (int amulets = 0; amulets <= 3; ++amulets)
    {
        GameState s = Base();
        for (int a = 0; a < amulets; ++a) { Put(s, "Amulet of Vigor", 30 + a); }
        s.players[0].hand.push_back(C("Izzet Guildgate", 40));
        REQUIRE(Drop(s, 40));
        CAPTURE(amulets);
        CHECK(Find(s, 40)->tapped == (amulets == 0));
        CHECK(s.floating_mana.Total() == std::max(0, amulets - 1));
        CHECK(LandUsableOnEntry(Base(), D("Izzet Guildgate")) == false);
    }
    // The projection knows a tapped entrant becomes usable.
    GameState p = Base();
    Put(p, "Amulet of Vigor", 31);
    CHECK(LandWouldEnterTapped(p, D("Izzet Guildgate")));
    CHECK(LandUsableOnEntry(p, D("Izzet Guildgate")));
    // EnterLand routes: a Natural tapped put (Maze's End's fetch) and a ForcedTapped put (Charm), 2 Amulets.
    GameState e = Base();
    Put(e, "Amulet of Vigor", 32); Put(e, "Amulet of Vigor", 33);
    EnterLand(e, D("Golgari Guildgate"), 41, true, true, LandEntryTap::Natural);
    CHECK_FALSE(Find(e, 41)->tapped);
    CHECK(e.floating_mana.Total() == 1);
    EnterLand(e, D("Forest"), 42, false, true, LandEntryTap::ForcedTapped);
    CHECK_FALSE(Find(e, 42)->tapped);
    CHECK(e.floating_mana.Total() == 2);
    // Outside a main phase of its controller (an I3 put route) the untap happens, the float does not.
    GameState f = Base();
    Put(f, "Amulet of Vigor", 34); Put(f, "Amulet of Vigor", 35);
    {
        const LandEntryNoFloatScope nofloat;
        EnterLand(f, D("Golgari Guildgate"), 43, false, true, LandEntryTap::Natural);
    }
    CHECK_FALSE(Find(f, 43)->tapped);
    CHECK(f.floating_mana.Total() == 0);
    // A NONLAND entering tapped (FireEtbWatchers): untapped, no float. An Amulet sees itself (CR 603.6a).
    GameState n = Base();
    Put(n, "Amulet of Vigor", 36, /*tapped=*/true);
    FireEtbWatchers(n, 0, FindIdx(n, 36));
    CHECK_FALSE(Find(n, 36)->tapped);
    CHECK(n.floating_mana.Total() == 0);
    // Gond Gate's replacement: the Gate never enters tapped, so no trigger and no float.
    GameState g = Base();
    Put(g, "Gond Gate", 37); Put(g, "Amulet of Vigor", 38); Put(g, "Amulet of Vigor", 39);
    g.players[0].hand.push_back(C("Boros Guildgate", 44));
    REQUIRE(Drop(g, 44));
    CHECK_FALSE(Find(g, 44)->tapped);
    CHECK(g.floating_mana.Total() == 0);
}

TEST_CASE("Maze's End I2: Gateway Plaza pays {1} or is sacrificed; Amulet lets it pay with itself")
{
    LoadI2Cards();
    // No mana at all: forced sacrifice whatever the pin.
    GameState s = Base();
    s.players[0].hand.push_back(C("Gateway Plaza", 50));
    REQUIRE(Drop(s, 50, /*pin=*/1));
    CHECK(Find(s, 50) == nullptr);
    CHECK(InGraveyard(s, 50));
    CHECK(s.players[0].lands_played_this_turn == 1);
    // An untapped Forest: pay (pin 1) keeps it, the Forest taps; sacrifice (pin 0); unpinned = pay.
    for (int pin : { 1, 0, -1 })
    {
        GameState t = Base();
        Put(t, "Forest", 51);
        t.players[0].hand.push_back(C("Gateway Plaza", 52));
        REQUIRE(Drop(t, 52, pin));
        CAPTURE(pin);
        const bool kept = pin != 0;
        CHECK((Find(t, 52) != nullptr) == kept);
        CHECK(InGraveyard(t, 52) == !kept);
        CHECK(Find(t, 51)->tapped == kept);
        if (kept) { CHECK(Find(t, 52)->tapped); }               // it entered tapped
    }
    // One Amulet, no other land: the Amulet untaps it first, it pays {1} with itself and stays (tapped).
    GameState a = Base();
    Put(a, "Amulet of Vigor", 53);
    a.players[0].hand.push_back(C("Gateway Plaza", 54));
    REQUIRE(Drop(a, 54, 1));
    REQUIRE(Find(a, 54) != nullptr);
    CHECK(Find(a, 54)->tapped);
    // Two Amulets: it floats one, pays with the float, and ends UNTAPPED.
    GameState b = Base();
    Put(b, "Amulet of Vigor", 55); Put(b, "Amulet of Vigor", 56);
    b.players[0].hand.push_back(C("Gateway Plaza", 57));
    REQUIRE(Drop(b, 57, 1));
    REQUIRE(Find(b, 57) != nullptr);
    CHECK_FALSE(Find(b, 57)->tapped);
    CHECK(b.floating_mana.Total() == 0);
    // The effect-put route reads the land-entry pin (Maze's End's fetch: chosen_x - 1).
    GameState e = Base();
    Put(e, "Forest", 58);
    {
        const LandEntryPinScope pin(0);
        EnterLand(e, D("Gateway Plaza"), 59, true, true, LandEntryTap::Natural);
    }
    CHECK(Find(e, 59) == nullptr);
    CHECK_FALSE(Find(e, 58)->tapped);
    CHECK(g_land_entry_pin == -1);
}

TEST_CASE("Maze's End I2: a CLB Gate locks a colour other than its printed one and makes both")
{
    LoadI2Cards();
    const CardDefinition& sea = D("Sea Gate");
    REQUIRE(sea.params.etb_choose_color_fixed == static_cast<int>(Color::Blue));
    CHECK(EtbColorMenu(sea.params).size() == 4);
    CHECK_FALSE(Has(EtbColorMenu(sea.params), Color::Blue));
    // A pinned drop.
    GameState s = Base();
    s.players[0].hand.push_back(C("Sea Gate", 60));
    REQUIRE(Drop(s, 60, static_cast<int>(Color::Red)));
    const Permanent& p = *Find(s, 60);
    CHECK(p.tapped);
    CHECK(p.chosen_color == static_cast<int>(Color::Red));
    const std::vector<Color>& prod = EffectiveProducesFor(s, 0, sea, &p);
    REQUIRE(prod.size() == 2);
    CHECK(prod[0] == Color::Blue);                               // the printed colour first
    CHECK(prod[1] == Color::Red);
    // A pin on the printed colour is off the menu: the provider's colour (never blue) is locked.
    GameState t = Base();
    t.players[0].hand.push_back(C("Sea Gate", 61));
    REQUIRE(Drop(t, 61, static_cast<int>(Color::Blue)));
    CHECK(Find(t, 61)->chosen_color >= 0);
    CHECK(Find(t, 61)->chosen_color != static_cast<int>(Color::Blue));
    // The effect-put route locks the pinned colour too, and consumes the pin.
    GameState u = Base();
    {
        const LandEntryPinScope pin(static_cast<int>(Color::Green));
        EnterLand(u, D("Cliffgate"), 62, true, true, LandEntryTap::Natural);
    }
    CHECK(Find(u, 62)->chosen_color == static_cast<int>(Color::Green));
    CHECK(g_land_entry_pin == -1);
    // The menu the SEARCH is offered: base = the whole menu; the deck provider keeps the colours the list
    // spends (U/G/R) -- Sea Gate {R,G}, Manor Gate {U,R}, Black Dragon Gate {U,R,G}.
    GameState v = Base();
    StockSpendLibrary(v, 70);
    const MazesEndProvider prov;
    CHECK(GenericProvider().LandEntryPinCandidates(v, 0, sea).size() == 4);
    const std::vector<int> sea_m = prov.LandEntryPinCandidates(v, 0, sea);
    CHECK(sea_m == std::vector<int>{ static_cast<int>(Color::Red), static_cast<int>(Color::Green) });
    CHECK(prov.LandEntryPinCandidates(v, 0, D("Manor Gate"))
          == std::vector<int>{ static_cast<int>(Color::Blue), static_cast<int>(Color::Red) });
    CHECK(prov.LandEntryPinCandidates(v, 0, D("Black Dragon Gate")).size() == 3);
    CHECK(prov.LandEntryPinCandidates(v, 0, D("Gateway Plaza")) == std::vector<int>{ 1, 0 });
    CHECK(prov.LandEntryPinCandidates(v, 0, D("Azorius Guildgate")).empty());
}

TEST_CASE("Maze's End I2: Thran Portal -- fastland, chosen basic land type, the life cost")
{
    LoadI2Cards();
    const CardDefinition& thran = D("Thran Portal");
    GameState s = Base();
    Put(s, "Forest", 80); Put(s, "Forest", 81);
    CHECK_FALSE(LandWouldEnterTapped(s, thran));                 // two other lands
    Put(s, "Forest", 82);
    CHECK(LandWouldEnterTapped(s, thran));                       // three
    s.battlefield.pop_back();
    s.players[0].hand.push_back(C("Thran Portal", 83));
    REQUIRE(Drop(s, 83, static_cast<int>(Color::Green)));
    const Permanent& p = *Find(s, 83);
    CHECK_FALSE(p.tapped);
    CHECK(p.chosen_color == static_cast<int>(Color::Green));
    CHECK(LandHasSubtypeNow(s, p, "Forest"));
    CHECK(LandHasSubtypeNow(s, p, "Gate"));
    CHECK_FALSE(LandHasSubtypeNow(s, p, "Island"));
    const std::vector<Color>& prod = EffectiveProducesFor(s, 0, thran, &p);
    REQUIRE(prod.size() == 1);
    CHECK(prod[0] == Color::Green);
    CHECK(thran.params.tap_self_damage == 1);
    CHECK(thran.params.tap_is_life_payment);
    CHECK_FALSE(thran.card.HasSupertype(Supertype::Basic));
    // Deck provider: Island / Mountain / Forest only.
    GameState v = Base();
    StockSpendLibrary(v, 90);
    CHECK(MazesEndProvider().LandEntryPinCandidates(v, 0, thran).size() == 3);
}

TEST_CASE("Maze's End I2: The Black Gate pays 3 life to enter untapped; declines under an Amulet")
{
    LoadI2Cards();
    const CardDefinition& bg = D("The Black Gate");
    CHECK(bg.card.HasSupertype(Supertype::Legendary));
    static const MazesEndProvider kProv;
    GameState s = Base();
    s.m_provider = &kProv;
    s.players[0].hand.push_back(C("The Black Gate", 100));
    REQUIRE(Drop(s, 100));
    CHECK_FALSE(Find(s, 100)->tapped);
    CHECK(s.players[0].life == 17);
    GameState t = Base();
    t.m_provider = &kProv;
    Put(t, "Amulet of Vigor", 101);
    t.players[0].hand.push_back(C("The Black Gate", 102));
    REQUIRE(Drop(t, 102));
    CHECK_FALSE(Find(t, 102)->tapped);                           // entered tapped, Amulet untapped it
    CHECK(t.players[0].life == 20);                              // no life paid
}

TEST_CASE("Maze's End I2: the search is offered every admitted land-entry choice (drop and fetch)")
{
    LoadI2Cards();
    static const MazesEndProvider kProv;
    // A Cliffgate drop under an Amulet: the colour pins G and U are separate land options, and the G option
    // can cast Sylvan Scrying with the Gate's own mana (only reachable if the casts are enumerated on THAT
    // option's board).
    GameState s = Base();
    s.m_provider = &kProv;
    s.players[0].lands_played_this_turn = 0;
    StockSpendLibrary(s, 110);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 120 + k)); }
    Put(s, "Amulet of Vigor", 111);
    Put(s, "Island", 112);
    s.players[0].hand.push_back(C("Cliffgate", 113));
    s.players[0].hand.push_back(C("Sylvan Scrying", 114));
    s.players[0].hand.push_back(C("Forest", 115));
    s.players[0].hand.push_back(C("Forest", 116));
    bool saw_g = false, saw_u = false, scry_with_g = false, unpinned_cliff = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.land_to_play != "Cliffgate") { continue; }
        if (p.rad_mode == static_cast<int>(Color::Green)) { saw_g = true; }
        if (p.rad_mode == static_cast<int>(Color::Blue))  { saw_u = true; }
        if (p.rad_mode < 0) { unpinned_cliff = true; }
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name == "Sylvan Scrying"
                && p.rad_mode == static_cast<int>(Color::Green)) { scry_with_g = true; }
        }
    }
    CHECK(saw_g);
    CHECK(saw_u);
    CHECK_FALSE(unpinned_cliff);
    CHECK(scry_with_g);
    // Gateway Plaza: pay and sacrifice are both offered.
    GameState t = Base();
    t.m_provider = &kProv;
    t.players[0].lands_played_this_turn = 0;
    StockSpendLibrary(t, 130);
    Put(t, "Forest", 131);
    t.players[0].hand.push_back(C("Gateway Plaza", 132));
    bool pay = false, sac = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, true))
    {
        if (p.land_to_play != "Gateway Plaza") { continue; }
        if (p.rad_mode == 1) { pay = true; }
        if (p.rad_mode == 0) { sac = true; }
    }
    CHECK(pay);
    CHECK(sac);
    // Maze's End fetching a CLB Gate: one activation variant per admitted colour (chosen_x = colour + 1).
    GameState u = Base();
    u.m_provider = &kProv;
    StockSpendLibrary(u, 140);
    u.players[0].library.push_back(C("Sea Gate", 145));
    Put(u, "Maze's End", 146);
    Put(u, "Forest", 147); Put(u, "Forest", 148); Put(u, "Forest", 149);
    std::vector<int> xs;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTutorPut
                && a.tutor_target == "Sea Gate"
                && std::find(xs.begin(), xs.end(), a.chosen_x) == xs.end())
            { xs.push_back(a.chosen_x); }
        }
    }
    std::sort(xs.begin(), xs.end());
    CHECK(xs == std::vector<int>{ static_cast<int>(Color::Red) + 1, static_cast<int>(Color::Green) + 1 });
    // ...and the resolver locks the pinned colour on the fetched Gate, once.
    GameState w = u;
    {
        const std::string target = "Sea Gate";
        const SacNamedPinScope tpin(0, &target);
        const LandEntryPinScope lpin(static_cast<int>(Color::Green));
        REQUIRE(PermAbilitySourceLive(w, 0, 146, PermAbilityMode::TapTutorPut, nullptr));
        ApplyPermAbility(w, 0, 146, PermAbilityMode::TapTutorPut);
    }
    REQUIRE(Find(w, 145) != nullptr);
    CHECK(Find(w, 145)->chosen_color == static_cast<int>(Color::Green));
}

TEST_CASE("Maze's End I2: Archdruid's Charm searches the found land's as-enters choice (decision 15)")
{
    LoadI2Cards();
    static const MazesEndProvider kProv;
    const CardDefinition& charm = D("Archdruid's Charm");
    // The encoding: every pre-existing value decodes to itself; a pinned value round-trips.
    CHECK(TutorChoiceIndex(3) == 3);
    CHECK(TutorChoiceEntryPin(3) == -1);
    CHECK(TutorChoiceIndex(kTutorDeclineChoice) == kTutorDeclineChoice);
    CHECK(TutorChoiceEntryPin(kTutorDeclineChoice) == -1);
    CHECK(TutorChoiceIndex(EncodeTutorEntryPin(2, 0)) == 2);
    CHECK(TutorChoiceEntryPin(EncodeTutorEntryPin(2, 0)) == 0);
    CHECK(TutorChoiceEntryPin(EncodeTutorEntryPin(0, static_cast<int>(Color::Green))) == static_cast<int>(Color::Green));
    // The enumeration: a Charm whose only land find is a Sea Gate fans one variant per admitted colour (R, G),
    // index 0 included (the base plan takes the provider's default colour, not a searched one).
    GameState s = Base();
    s.m_provider = &kProv;
    StockSpendLibrary(s, 150);
    s.players[0].library.push_back(C("Sea Gate", 155));
    Put(s, "Forest", 156); Put(s, "Forest", 157); Put(s, "Forest", 158);
    s.players[0].hand.push_back(C("Archdruid's Charm", 159));
    std::vector<int> pins;
    bool base = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool casts = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == "Archdruid's Charm") { casts = true; } }
        if (!casts) { continue; }
        if (p.tutor_choice == -1) { base = true; }
        else if (TutorChoiceHasEntryPin(p.tutor_choice))
        {
            CHECK(TutorChoiceIndex(p.tutor_choice) == 0);
            if (std::find(pins.begin(), pins.end(), TutorChoiceEntryPin(p.tutor_choice)) == pins.end())
            { pins.push_back(TutorChoiceEntryPin(p.tutor_choice)); }
        }
    }
    std::sort(pins.begin(), pins.end());
    CHECK(base);
    CHECK(pins == std::vector<int>{ static_cast<int>(Color::Red), static_cast<int>(Color::Green) });
    // The resolver: the pinned colour is locked on the found Gate, which enters tapped (the effect's); the pin
    // is consumed and nothing leaks.
    GameState w = s;
    {
        const ScriptedTutor st(EncodeTutorEntryPin(0, static_cast<int>(Color::Green)));
        PerformLandTutorToBattlefield(w, 0, charm.params);
        CHECK(g_scripted_tutor_choice == -1);
    }
    REQUIRE(Find(w, 155) != nullptr);
    CHECK(Find(w, 155)->chosen_color == static_cast<int>(Color::Green));
    CHECK(Find(w, 155)->tapped);
    CHECK(g_land_entry_pin == -1);
    GameState w2 = s;
    {
        const ScriptedTutor st(EncodeTutorEntryPin(0, static_cast<int>(Color::Red)));
        PerformLandTutorToBattlefield(w2, 0, charm.params);
    }
    REQUIRE(Find(w2, 155) != nullptr);
    CHECK(Find(w2, 155)->chosen_color == static_cast<int>(Color::Red));
    // Gateway Plaza found by the Charm: pay (1) and sacrifice (0) are both variants, and pin 0 sacrifices it.
    GameState t = Base();
    t.m_provider = &kProv;
    StockSpendLibrary(t, 160);
    t.players[0].library.push_back(C("Gateway Plaza", 165));
    Put(t, "Forest", 166); Put(t, "Forest", 167); Put(t, "Forest", 168); Put(t, "Forest", 169);
    t.players[0].hand.push_back(C("Archdruid's Charm", 170));
    std::vector<int> plaza;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, true))
    {
        if (!TutorChoiceHasEntryPin(p.tutor_choice)) { continue; }
        if (std::find(plaza.begin(), plaza.end(), TutorChoiceEntryPin(p.tutor_choice)) == plaza.end())
        { plaza.push_back(TutorChoiceEntryPin(p.tutor_choice)); }
    }
    std::sort(plaza.begin(), plaza.end());
    CHECK(plaza == std::vector<int>{ 0, 1 });
    GameState t2 = t;
    {
        const ScriptedTutor st(EncodeTutorEntryPin(0, 0));
        PerformLandTutorToBattlefield(t2, 0, charm.params);
    }
    CHECK(Find(t2, 165) == nullptr);
    CHECK(InGraveyard(t2, 165));
    GameState t3 = t;
    {
        const ScriptedTutor st(EncodeTutorEntryPin(0, 1));
        PerformLandTutorToBattlefield(t3, 0, charm.params);
    }
    CHECK(Find(t3, 165) != nullptr);
}
