// Blood Sun step 2 (docs/design/blood-sun-pipeline.md 3.1 / 3.2 / 12): the Lotus lands -- Lotus Vale and Scorched
// Ruins (an entry REPLACEMENT: sacrifice two UNTAPPED lands, else the graveyard) and Lotus Field (an enter TRIGGER:
// sacrifice two lands, itself included, tap-first) -- their searched sacrifice menu (LotusSacrificePicks, the
// land-entry pin) with folds V1 / F-L1 / F-L2, and the big-mana payer tests (three-of-one-colour bursts, 16+ mana).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsBs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefBs(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardBs(const std::string& name, int number)
{
    Card c = DefBs(name).card;
    c.m_number = number;
    return c;
}

void PutBs(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card              = CardBs(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

GameState BaseBs(int turn)
{
    static const VialProvider vp;
    EnsureCardsBs();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardBs("Swamp", 900 + k)); }
    return s;
}

int CountLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0 && p.card.IsLand()) { ++n; } }
    return n;
}

bool OnBattlefield(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}

bool InGraveyard(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

ManaPool FloatBs(const GameState& s)
{
    ManaPool f = s.floating_mana;
    f.AddPool(s.floating_creature_mana);
    return f;
}

bool PlayFromHand(GameState& s, const std::string& name, int pin)
{
    for (std::size_t i = 0; i < s.players[0].hand.size(); ++i)
    {
        if (s.players[0].hand[i].m_name.str() != name) { continue; }
        LandPlayOptions o;
        o.rad_mode = pin;
        return PlayLandFromHand(s, i, DefBs(name), o);
    }
    return false;
}

}   // namespace

// ---- cards.json shape ---------------------------------------------------------------------------------------------

TEST_CASE("lotus lands: params as the design states them")
{
    EnsureCardsBs();
    const CardParams& vale = DefBs("Lotus Vale").params;
    CHECK(vale.etb_replace_sacrifice_untapped_lands == 2);
    CHECK(vale.produces_amount == 3);
    CHECK(vale.produces_one_color);
    CHECK_FALSE(vale.enters_tapped);
    const CardParams& ruins = DefBs("Scorched Ruins").params;
    CHECK(ruins.etb_replace_sacrifice_untapped_lands == 2);
    CHECK(ruins.produces_amount == 4);
    const CardParams& field = DefBs("Lotus Field").params;
    CHECK(field.etb_sacrifice_lands == 2);
    CHECK(field.enters_tapped);
    CHECK(field.produces_one_color);
    CHECK(LandHasEntryPinChoice(DefBs("Lotus Vale")));
    CHECK(LandHasEntryPinChoice(DefBs("Lotus Field")));
    CHECK_FALSE(LandHasEntryPinChoice(DefBs("Forest")));
}

// ---- the sacrifice menu --------------------------------------------------------------------------------------------

TEST_CASE("lotus vale menu: multisets of two UNTAPPED land classes; tapped lands are not victims")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Swamp", 2);
    PutBs(s, "Forest", 3);
    PutBs(s, "Island", 4, /*tapped=*/true);
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefBs("Lotus Vale"), -1);
    // Swamp x2 (one class) + Forest: {Swamp, Swamp}, {Swamp, Forest}. The Island is tapped.
    REQUIRE(picks.size() == 2);
    for (const std::vector<int>& p : picks)
    {
        CHECK(p.size() == 2);
        for (int i : p) { CHECK_FALSE(s.battlefield[static_cast<std::size_t>(i)].tapped); }
    }
    CHECK(LotusSacrificePickCount(s, 0, DefBs("Lotus Vale")) == 2);
    // The provider's pin candidates: one per pick.
    const std::vector<int> pins = ResolveProvider(s).LandEntryPinCandidates(s, 0, DefBs("Lotus Vale"));
    CHECK(pins == std::vector<int>{ 0, 1 });
}

TEST_CASE("lotus vale menu: exactly two untapped -> one forced pick; one untapped -> no pick")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Forest", 2);
    CHECK(LotusSacrificePicks(s, 0, DefBs("Lotus Vale"), -1).size() == 1);
    CHECK(ResolveProvider(s).LandEntryPinCandidates(s, 0, DefBs("Lotus Vale")).empty());   // no fan
    s.battlefield[1].tapped = true;
    CHECK(LotusSacrificePicks(s, 0, DefBs("Lotus Vale"), -1).empty());
}

TEST_CASE("lotus field menu: itself included, tapped twin first (F-L1), every land with no more than two")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1, /*tapped=*/false);
    PutBs(s, "Swamp", 2, /*tapped=*/true);
    PutBs(s, "Forest", 3);
    PutBs(s, "Lotus Field", 4, /*tapped=*/true);
    s.battlefield.back().entered_this_turn = true;
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefBs("Lotus Field"), 3);
    // Classes: Swamp (2), Forest, Lotus Field (self) -> {S,S} {S,F} {S,LF} {F,LF}.
    CHECK(picks.size() == 4);
    for (const std::vector<int>& p : picks)
    {
        // A pick holding ONE Swamp takes the TAPPED one (slot 1).
        int swamps = 0; bool tapped_swamp = false;
        for (int i : p)
        {
            if (s.battlefield[static_cast<std::size_t>(i)].card.m_name.str() == "Swamp")
            { ++swamps; if (s.battlefield[static_cast<std::size_t>(i)].tapped) { tapped_swamp = true; } }
        }
        if (swamps == 1) { CHECK(tapped_swamp); }
    }
    // Before it enters, the width bound counts the Field as one more class: never below the truth.
    GameState pre = s;
    pre.battlefield.pop_back();
    CHECK(LotusSacrificePickCount(pre, 0, DefBs("Lotus Field")) >= static_cast<int>(picks.size()));

    GameState two = BaseBs(3);
    PutBs(two, "Swamp", 1);
    PutBs(two, "Lotus Field", 4, /*tapped=*/true);
    const std::vector<std::vector<int>> all = LotusSacrificePicks(two, 0, DefBs("Lotus Field"), 1);
    REQUIRE(all.size() == 1);
    CHECK(all[0].size() == 2);   // the other land AND the Field
}

// ---- folds V1 / F-L2 -----------------------------------------------------------------------------------------------

TEST_CASE("lotus folds: V1 (Vale with < 2 untapped) and F-L2 (Field with < 2 other lands) are dominated drops")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Forest", 2, /*tapped=*/true);
    CHECK(LakeDropWithoutVictim(s, DefBs("Lotus Vale")));      // one untapped land
    CHECK(LakeDropWithoutVictim(s, DefBs("Scorched Ruins")));
    CHECK_FALSE(LakeDropWithoutVictim(s, DefBs("Lotus Field")));   // two other lands (tapped is fine)
    s.battlefield[1].tapped = false;
    CHECK_FALSE(LakeDropWithoutVictim(s, DefBs("Lotus Vale")));
    s.battlefield.pop_back();
    CHECK(LakeDropWithoutVictim(s, DefBs("Lotus Field")));     // one other land
    // F-L2 stands down for a landfall watcher (the Field DOES enter) -- V1 does not care (it never enters).
    s.deck_has_landfall = true;
    CHECK_FALSE(LakeDropWithoutVictim(s, DefBs("Lotus Field")));
    CHECK(LakeDropWithoutVictim(s, DefBs("Lotus Vale")));
    CHECK_FALSE(LakeDropWithoutVictim(s, DefBs("Forest")));
}

// ---- resolution through THE land drop ------------------------------------------------------------------------------

TEST_CASE("lotus vale drop: sacrifices the pinned pick, no tap-first float, enters untapped")
{
    for (int pin : { 0, 1 })
    {
        GameState s = BaseBs(3);
        PutBs(s, "Swamp", 1);
        PutBs(s, "Swamp", 2);
        PutBs(s, "Forest", 3);
        s.players[0].hand.push_back(CardBs("Lotus Vale", 50));
        const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefBs("Lotus Vale"), -1);
        REQUIRE(picks.size() == 2);
        std::vector<std::string> want;
        for (int i : picks[static_cast<std::size_t>(pin)])
        { want.push_back(s.battlefield[static_cast<std::size_t>(i)].card.m_name.str()); }
        REQUIRE(PlayFromHand(s, "Lotus Vale", pin));
        CHECK(s.players[0].lands_played_this_turn == 1);
        CHECK(OnBattlefield(s, 50));
        CHECK(CountLands(s) == 2);
        CHECK(FloatBs(s).Total() == 0);   // a replacement: no float
        for (const std::string& n : want) { CHECK(InGraveyard(s, n)); }
        for (const Permanent& p : s.battlefield) { if (p.card.m_number == 50) { CHECK_FALSE(p.tapped); } }
    }
}

TEST_CASE("lotus vale drop with one untapped land: graveyard, the drop is spent, nothing sacrificed")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Forest", 2, /*tapped=*/true);
    s.players[0].hand.push_back(CardBs("Lotus Vale", 50));
    REQUIRE(PlayFromHand(s, "Lotus Vale", -1));
    CHECK(s.players[0].lands_played_this_turn == 1);
    CHECK_FALSE(OnBattlefield(s, 50));
    CHECK(InGraveyard(s, "Lotus Vale"));
    CHECK(CountLands(s) == 2);
    CHECK(s.players[0].hand.empty());
}

TEST_CASE("lotus field drop: enters tapped, untapped victims float their mana first, then are sacrificed")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Forest", 2);
    PutBs(s, "Island", 3);
    s.players[0].hand.push_back(CardBs("Lotus Field", 50));
    // Find the pick {Swamp, Forest} on the post-entry board (the Field is appended last).
    GameState probe = s;
    PutBs(probe, "Lotus Field", 50, /*tapped=*/true);
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(probe, 0, DefBs("Lotus Field"), 3);
    int pin = -1;
    for (std::size_t k = 0; k < picks.size(); ++k)
    {
        std::vector<std::string> n;
        for (int i : picks[k]) { n.push_back(probe.battlefield[static_cast<std::size_t>(i)].card.m_name.str()); }
        std::sort(n.begin(), n.end());
        if (n == std::vector<std::string>{ "Forest", "Swamp" }) { pin = static_cast<int>(k); }
    }
    REQUIRE(pin >= 0);
    REQUIRE(PlayFromHand(s, "Lotus Field", pin));
    CHECK(OnBattlefield(s, 50));
    CHECK(OnBattlefield(s, 3));          // the Island survives
    CHECK(CountLands(s) == 2);
    CHECK(InGraveyard(s, "Swamp"));
    CHECK(InGraveyard(s, "Forest"));
    CHECK(FloatBs(s).Total() == 2);      // {B} + {G} floated in response
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 50) { CHECK(p.tapped); } }
}

TEST_CASE("lotus field drop with one other land: both are sacrificed, the Field included")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    s.players[0].hand.push_back(CardBs("Lotus Field", 50));
    REQUIRE(PlayFromHand(s, "Lotus Field", -1));
    CHECK(CountLands(s) == 0);
    CHECK(InGraveyard(s, "Swamp"));
    CHECK(InGraveyard(s, "Lotus Field"));
    CHECK(FloatBs(s).Total() == 1);      // the Swamp's {B}, tapped in response
}

TEST_CASE("lotus vale PUT (EnterLand): the replacement applies to a put too")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    EnterLand(s, DefBs("Scorched Ruins"), 60);
    CHECK_FALSE(OnBattlefield(s, 60));
    CHECK(InGraveyard(s, "Scorched Ruins"));
    PutBs(s, "Forest", 2);
    EnterLand(s, DefBs("Scorched Ruins"), 61);
    CHECK(OnBattlefield(s, 61));
    CHECK(CountLands(s) == 1);
}

// ---- the big-mana payer --------------------------------------------------------------------------------------------

TEST_CASE("lotus payer: four Scorched Ruins pay {15}")
{
    GameState s = BaseBs(6);
    for (int k = 0; k < 4; ++k) { PutBs(s, "Scorched Ruins", 10 + k); }
    ManaCost c; c.generic = 15;
    REQUIRE(TapForCostDirect(s, c, /*for_creature=*/true));
    for (const Permanent& p : s.battlefield) { CHECK(p.tapped); }
    CHECK(FloatBs(s).Total() == 1);
}

TEST_CASE("lotus payer: one Lotus tap is three of ONE colour -- it cannot pay {U}{G}")
{
    GameState s = BaseBs(4);
    PutBs(s, "Lotus Vale", 10);
    ManaCost ug; ug.blue = 1; ug.green = 1;
    CHECK_FALSE(TapForCostDirect(s, ug, /*for_creature=*/false));
    ManaCost uu; uu.blue = 2; uu.generic = 1;
    CHECK(TapForCostDirect(s, uu, /*for_creature=*/false));
}

TEST_CASE("lotus payer: two Lotus taps pay {U}{U}{G}")
{
    GameState s = BaseBs(4);
    PutBs(s, "Lotus Vale", 10);
    PutBs(s, "Lotus Field", 11);
    ManaCost c; c.blue = 2; c.green = 1;
    REQUIRE(TapForCostDirect(s, c, /*for_creature=*/false));
    for (const Permanent& p : s.battlefield) { CHECK(p.tapped); }
}

TEST_CASE("lotus payer: a 22-mana line through the whole-turn prepay (PrepayWildTrue)")
{
    GameState s = BaseBs(6);
    for (int k = 0; k < 4; ++k) { PutBs(s, "Scorched Ruins", 10 + k); }   // 16 {C}
    PutBs(s, "Lotus Vale", 20);                                           // 3 of one colour
    PutBs(s, "Lotus Field", 21);                                          // 3 of one colour
    s.players[0].lands_played_this_turn = 1;
    const char* names[] = { "Emrakul, the Aeons Torn", "Primeval Titan", "Elvish Mystic" };   // {15} {4}{G}{G} {G}
    int num = 100;
    for (const char* n : names) { s.players[0].hand.push_back(CardBs(n, num++)); }
    std::vector<Action> line;
    for (const char* n : names)
    {
        const CardDefinition& d = DefBs(n);
        Action a;
        a.kind           = Action::Kind::CastFromHand;
        a.card_name      = d.card.m_name;
        a.def            = &d;
        a.cost           = d.card.m_mana_cost;
        a.is_noncreature = !d.card.IsCreature();
        a.card_mv        = d.card.m_mana_cost.ManaValue();
        for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
        { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == n) { a.hand_index = i; break; } }
        line.push_back(a);
    }
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    for (const Permanent& p : s.battlefield) { CHECK(p.tapped); }
    REQUIRE(FloatBs(s).Total() == 22);
    for (const char* n : names) { CHECK(TapForCostDirect(s, DefBs(n).card.m_mana_cost, /*for_creature=*/true)); }
    CHECK(FloatBs(s).Total() == 0);
}

TEST_CASE("lotus field menu: the front pick spares the Field itself (LotusSacrificeRanking: lowest yield first)")
{
    GameState s = BaseBs(3);
    PutBs(s, "Swamp", 1);
    PutBs(s, "Forest", 2);
    PutBs(s, "Island", 3);
    PutBs(s, "Lotus Field", 4, /*tapped=*/true);
    s.battlefield.back().entered_this_turn = true;
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefBs("Lotus Field"), 3);
    REQUIRE_FALSE(picks.empty());
    for (int i : picks.front()) { CHECK(s.battlefield[static_cast<std::size_t>(i)].card.m_name.str() != "Lotus Field"); }
    // ...but a pick holding the Field is still on the menu (ORDER only).
    bool has_self = false;
    for (const std::vector<int>& p : picks) { for (int i : p) { if (i == 3) { has_self = true; } } }
    CHECK(has_self);
    CHECK(picks.size() == 6);   // 4 classes of one: C(4, 2)
}
