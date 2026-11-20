// Blood Sun step 4 (docs/design/blood-sun-pipeline.md §6, §7, §11 R1, §12 step 4): Fastbond -- unlimited land plays
// (kUnlimitedDrops folded into the static extra drops), its land-PLAY damage after the first, the drop recompute when
// a drop-maker leaves, the greedy's no-suicide stop, and own death armed from the card's own param (USER D1: "Yes,
// running out of life is a loss, so the deck should not fastbond so much that it loses.").
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
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsFb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFb(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardFb(const std::string& name, int number)
{
    Card c = DefFb(name).card;
    c.m_number = number;
    return c;
}

void PutFb(GameState& s, const std::string& name, int number, bool tapped = false, int controller = 0)
{
    Permanent p;
    p.card             = CardFb(name, number);
    p.controller_index = controller;
    p.owner_index      = controller;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
}

// Our main phase, nothing played yet; the drops seeded as the untap step seeds them. Own death live (the Fastbond
// deck's stamp), the static-drop stamp on (every Fastbond deck carries it), no Blood Sun.
GameState BaseFb(int life = 20)
{
    static const VialProvider vp;
    EnsureCardsFb();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = false;
    s.own_death_live      = true;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardFb("Swamp", 900 + k)); }
    return s;
}

void SeedDrops(GameState& s)
{
    s.players[0].lands_played_this_turn     = 0;
    s.players[0].bonus_land_drops_this_turn = StaticExtraLandDrops(s, 0);
}

bool PlayFb(GameState& s, const std::string& name, int number)
{
    s.players[0].hand.push_back(CardFb(name, number));
    LandPlayOptions o;
    return PlayLandFromHand(s, s.players[0].hand.size() - 1, DefFb(name), o);
}

bool InGraveyardFb(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("fastbond: params as the design states them; the unlimited drops fold into the static extra drops")
{
    EnsureCardsFb();
    const CardDefinition& fb = DefFb("Fastbond");
    CHECK(fb.params.unlimited_land_plays);
    CHECK(fb.params.land_play_damage_after_first == 1);
    CHECK(fb.params.static_extra_land_drops == kUnlimitedDrops);
    CHECK(DefFb("Exploration").params.static_extra_land_drops == 1);   // unchanged
    CHECK(DefFb("Exploration").params.land_play_damage_after_first == 0);
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    SeedDrops(s);
    CHECK(s.players[0].LandDropsAvailable() == 1 + kUnlimitedDrops);
    CHECK(TurnSolver::DropRearmUnbounded(s));
    GameState e = BaseFb();
    for (int k = 0; k < 4; ++k) { PutFb(e, "Exploration", 710 + k); }
    SeedDrops(e);
    CHECK_FALSE(TurnSolver::DropRearmUnbounded(e));   // no Exploration count reaches it: the caps stay
}

TEST_CASE("fastbond: four lands played -> three damage (the first play is free)")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    SeedDrops(s);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(s.players[0].life == 20);
    CHECK(LandPlayDamageNext(s, 0) == 1);
    CHECK(PlayFb(s, "Forest", 802));
    CHECK(PlayFb(s, "Swamp", 803));
    CHECK(PlayFb(s, "Forest", 804));
    CHECK(s.players[0].lands_played_this_turn == 4);
    CHECK(s.players[0].life == 17);
}

TEST_CASE("fastbond: two Fastbonds -> two damage per play after the first")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Fastbond", 701);
    SeedDrops(s);
    CHECK(s.players[0].LandDropsAvailable() == 1 + 2 * kUnlimitedDrops);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(s.players[0].life == 20);
    CHECK(PlayFb(s, "Forest", 802));
    CHECK(s.players[0].life == 18);
    CHECK(PlayFb(s, "Forest", 803));
    CHECK(s.players[0].life == 16);
}

TEST_CASE("fastbond + Exploration: still one damage per non-first play (ruling 1: every play, whatever permission)")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Exploration", 701);
    SeedDrops(s);
    CHECK(s.players[0].LandDropsAvailable() == 2 + kUnlimitedDrops);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(PlayFb(s, "Forest", 802));   // Exploration's drop (or Fastbond's): either way it is a land PLAY
    CHECK(PlayFb(s, "Forest", 803));
    CHECK(s.players[0].life == 18);
}

TEST_CASE("fastbond: a land PUT onto the battlefield never pings (ruling 2) -- Burgeoning, EnterLand")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Burgeoning", 701);
    SeedDrops(s);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(PlayFb(s, "Forest", 802));
    CHECK(s.players[0].life == 19);
    // Burgeoning's put (the opponent's land play) -- a put, not a play: no damage, and no land play counted.
    s.players[0].hand.push_back(CardFb("Swamp", 810));
    ResolveBurgeoningTriggers(s);
    bool swamp_out = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 810) { swamp_out = true; } }
    CHECK(swamp_out);
    CHECK(s.players[0].life == 19);
    CHECK(s.players[0].lands_played_this_turn == 2);
    // A direct effect put (the land-tutor / EnterLand route) likewise.
    EnterLand(s, DefFb("Forest"), 811);
    CHECK(s.players[0].life == 19);
}

TEST_CASE("fastbond R1: a Lotus Vale PLAYED into the graveyard was still played -- it pings (CR 305.1)")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Swamp", 750, /*tapped=*/true);   // no two UNTAPPED lands: the Vale's replacement fails
    SeedDrops(s);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(s.players[0].life == 20);
    CHECK(PlayFb(s, "Lotus Vale", 802));
    CHECK(InGraveyardFb(s, "Lotus Vale"));
    CHECK(s.players[0].lands_played_this_turn == 2);
    CHECK(s.players[0].life == 19);
}

TEST_CASE("fastbond leaving: its drops end with it (dies / exile hook, DestroyAllEnchantments); nothing retroactive")
{
    GameState s = BaseFb();
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Exploration", 701);
    SeedDrops(s);
    CHECK(PlayFb(s, "Forest", 801));
    CHECK(PlayFb(s, "Forest", 802));
    CHECK(PlayFb(s, "Forest", 803));
    REQUIRE(s.players[0].lands_played_this_turn == 3);
    // Fastbond leaves (the shared leave hook, as a sacrifice / destroy / exile reaches it).
    GameState t = s;
    for (auto it = t.battlefield.begin(); it != t.battlefield.end(); ++it)
    { if (it->card.m_number == 700) { const Card gone = it->card; t.battlefield.erase(it);
                                      FireLeavesBattlefieldTriggers(t, 0, gone); break; } }
    CHECK(t.players[0].LandDropsAvailable() == 2);        // Exploration's drop remains
    CHECK(t.players[0].lands_played_this_turn == 3);      // nothing retroactive
    CHECK(t.players[0].lands_played_this_turn >= t.players[0].LandDropsAvailable());   // no more drops this turn
    // DestroyAllEnchantments takes both.
    GameState u = s;
    DestroyAllEnchantments(u);
    CHECK(u.players[0].LandDropsAvailable() == 1);
    // On the OPPONENT's turn nothing is taken (its drops are reseeded at our untap anyway).
    GameState v = s;
    v.active_player_index = 1;
    DestroyAllEnchantments(v);
    CHECK(v.players[0].bonus_land_drops_this_turn == s.players[0].bonus_land_drops_this_turn);
}

TEST_CASE("fastbond greedy drop: takes every open drop, but never the land that would kill us (own death live)")
{
    GameState s = BaseFb(/*life=*/3);
    PutFb(s, "Fastbond", 700);
    SeedDrops(s);
    for (int k = 0; k < 5; ++k) { s.players[0].hand.push_back(CardFb("Forest", 810 + k)); }
    CHECK_FALSE(LandPlayWouldKill(s, 0));
    AIEngine eng(MulliganProfile::DefaultProfile(), /*depth=*/1, /*budget_ms=*/0);
    CHECK(MtgTestSeam::TryPlayLand(eng, s));
    CHECK(s.players[0].lands_played_this_turn == 3);   // 3 plays: 2 damage, life 1; a 4th would kill
    CHECK(s.players[0].life == 1);
    CHECK(LandPlayWouldKill(s, 0));
    CHECK(s.players[0].hand.size() == 2);
    // Own death NOT live (no Fastbond deck would be unstamped so, but the predicate is exact): no stop.
    GameState t = BaseFb(/*life=*/3);
    t.own_death_live = false;
    PutFb(t, "Fastbond", 700);
    SeedDrops(t);
    for (int k = 0; k < 12; ++k) { t.players[0].hand.push_back(CardFb("Forest", 810 + k)); }
    CHECK(MtgTestSeam::TryPlayLand(eng, t));
    CHECK(t.players[0].lands_played_this_turn == 12);  // past the old `guard < 8` (nine lands) cap
}

TEST_CASE("fastbond own death: armed from the card's own param; City of Brass alone still does not arm it")
{
    EnsureCardsFb();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Blood Sun/Blood Sun.cod"));
    {
        GameState s;
        GoldFishRunner::StampDeckTraits(s, deck);
        CHECK(s.own_death_live);
        CHECK(s.deck_has_static_land_drops);
        CHECK_FALSE(s.dmg_events_armed);
    }
    // Without Fastbond (no Grim Tutor / Worldfire params yet -- step 5): unarmed, City of Brass included.
    Decklist no_fb = deck;
    for (auto it = no_fb.mainboard.begin(); it != no_fb.mainboard.end(); )
    { if (it->m_name.str() == "Fastbond") { it = no_fb.mainboard.erase(it); } else { ++it; } }
    for (auto it = no_fb.sideboard.begin(); it != no_fb.sideboard.end(); )
    { if (it->m_name.str() == "Fastbond") { it = no_fb.sideboard.erase(it); } else { ++it; } }
    bool has_city = false;
    for (const Card& c : no_fb.mainboard) { if (c.m_name.str() == "City of Brass") { has_city = true; } }
    CHECK(has_city);
    {
        GameState s;
        GoldFishRunner::StampDeckTraits(s, no_fb);
        CHECK_FALSE(s.own_death_live);
    }
}

TEST_CASE("fastbond search: life 3, five lands in hand -- the search holds lands and survives (d3, unlimited)")
{
    // Fastbond + one Mountain out, five Mountains and four Lightning Bolts in hand, opponent at 12: the only kill
    // this turn is exactly three more lands (two pings: 3 -> 1) and four Bolts. A fourth land would kill us first.
    GameState s = BaseFb(/*life=*/3);
    s.players[1].life = 12;
    PutFb(s, "Fastbond", 700);
    PutFb(s, "Mountain", 701);
    for (int k = 0; k < 5; ++k) { s.players[0].hand.push_back(CardFb("Mountain", 810 + k)); }
    for (int k = 0; k < 4; ++k) { s.players[0].hand.push_back(CardFb("Lightning Bolt", 820 + k)); }
    SeedDrops(s);
    AIEngine eng(MulliganProfile::DefaultProfile(), /*depth=*/3, /*budget_ms=*/0);
    GameEngine ge(eng);
    const int wt = ge.PlayOutFrom(s, /*max_turns=*/6, GameEngine::ResumeAt::Main1);
    CHECK(wt == 4);
    CHECK(s.players[0].life == 1);
    CHECK(OpponentHasLost(s));
}
