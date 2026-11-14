// Unit tests for the Red Elementals onboarding (2026-11-14): Spark Elemental / Ball Lightning (end-step
// self-sacrifice), Flamekin Bladewhirl (reveal an Elemental or pay {3}), Sunspine Lynx (ETB damage per
// nonbasic land + its two statics) and Hellspark Elemental's UNEARTH (CR 702.84a). Every number is a
// RULES number read off the cards' Oracle text and Scryfall rulings.
//
//  1. Unearth returns the card from the graveyard with haste; it is EXILED at the beginning of the end
//     step, and exiled INSTEAD when it is sacrificed (its own end-step trigger): never the graveyard.
//  2. The unearthed status is per OBJECT: a card exiled by another effect and returned is a new object
//     with no unearth (the Champion ruling).
//  3. The enumerator offers one Unearth per graveyard copy at {1}{R}, stamps the hasted (lord-pumped)
//     power pre-combat, and prices it -2 after combat.
//  4. Bladewhirl's reveal: another Elemental card in hand (a second Bladewhirl included), never the
//     Bladewhirl itself, never a Metallic Mimic in hand (only a Shapeshifter there).
//  5. Sunspine Lynx: the ETB hits each player per nonbasic land THAT player controls (us, for Fiery
//     Islet); while it is on the battlefield nobody gains life and damage can't be prevented.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

void EnsureCardsRe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DRe(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState BoardRe()
{
    EnsureCardsRe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    static const GenericProvider prov;
    s.m_provider          = &prov;
    return s;
}

int g_nre = 7000;
int PutRe(GameState& s, const std::string& name, int controller = 0, bool tapped = false)
{
    Permanent p;
    p.card              = DRe(name).card;
    p.card.m_number     = g_nre++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}
int GyRe(GameState& s, const std::string& name)
{
    Card c = DRe(name).card; c.m_number = g_nre++; s.players[0].graveyard.push_back(c);
    return c.m_number;
}
void HandRe(GameState& s, const std::string& name)
{
    Card c = DRe(name).card; c.m_number = g_nre++; s.players[0].hand.push_back(c);
}
template <class Zone>
int CountRe(const Zone& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}
int BfIdx(const GameState& s, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_name.str() == name) { return i; } }
    return -1;
}

}   // namespace

TEST_CASE("Hellspark Elemental: unearth returns it with haste; the end step EXILES it")
{
    GameState s = BoardRe();
    const int num = GyRe(s, "Hellspark Elemental");
    REQUIRE(DRe("Hellspark Elemental").params.unearth_cost.has_value());
    CHECK(DRe("Hellspark Elemental").params.unearth_cost->generic == 1);
    CHECK(DRe("Hellspark Elemental").params.unearth_cost->red == 1);
    REQUIRE(UnearthGraveyardIndex(s, 0, "Hellspark Elemental", num) == 0);
    REQUIRE(ApplyUnearth(s, 0, "Hellspark Elemental", num));
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 0);
    const int i = BfIdx(s, "Hellspark Elemental");
    REQUIRE(i >= 0);
    const Permanent& p = s.battlefield[static_cast<std::size_t>(i)];
    CHECK(static_cast<bool>(p.unearthed));
    CHECK(p.temp_haste);
    CHECK(p.entered_this_turn);
    CHECK(p.card.m_number == num);
    CHECK(CanAttackFull(p, s.battlefield, 0));
    // Nothing left to unearth: a stale action is a no-op.
    CHECK(UnearthGraveyardIndex(s, 0, "Hellspark Elemental", num) < 0);
    CHECK_FALSE(ApplyUnearth(s, 0, "Hellspark Elemental", num));
    // The beginning of the end step: the delayed exile.
    PerformEndStepUnearthExiles(s);
    CHECK(BfIdx(s, "Hellspark Elemental") < 0);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 0);
}

TEST_CASE("Hellspark Elemental: its own end-step sacrifice of an UNEARTHED copy is replaced by exile")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    GyRe(s, "Hellspark Elemental");
    REQUIRE(ApplyUnearth(s, 0, "Hellspark Elemental"));
    // The other order of the two simultaneous end-step triggers: the sacrifice resolves first.
    PerformEndStepSelfSacrifices(s);
    CHECK(BfIdx(s, "Hellspark Elemental") < 0);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 0);
    CHECK_FALSE(s.creature_died_this_turn);   // exiled, not died
    // ...and a HARD-CAST copy is sacrificed into the graveyard as usual (its next unearth).
    PutRe(s, "Hellspark Elemental");
    PerformEndStepSelfSacrifices(s);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 1);
}

TEST_CASE("Unearth: any sacrifice of the unearthed object exiles it; a new object is not unearthed")
{
    GameState s = BoardRe();
    GyRe(s, "Hellspark Elemental");
    REQUIRE(ApplyUnearth(s, 0, "Hellspark Elemental"));
    int i = BfIdx(s, "Hellspark Elemental");
    REQUIRE(i >= 0);
    SacrificePermanentAt(s, 0, i);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 0);
    // Champion ruling: an effect that EXILES it and later returns it returns a new object, which is no
    // longer under unearth (it would then go to the graveyard like any other creature).
    Card back = s.exile.back();
    s.exile.pop_back();
    PutCardOntoBattlefield(s, 0, back, "test return");
    i = BfIdx(s, "Hellspark Elemental");
    REQUIRE(i >= 0);
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(i)].unearthed));
    SacrificePermanentAt(s, 0, i);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 1);
}

TEST_CASE("Unearth: one searched action per graveyard copy, hasted power pre-combat, -2 after combat")
{
    GameState s = BoardRe();
    for (int k = 0; k < 4; ++k) { PutRe(s, "Mountain"); }
    GyRe(s, "Hellspark Elemental");
    GyRe(s, "Hellspark Elemental");
    GyRe(s, "Spark Elemental");                 // no unearth: never offered
    int n = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind != Action::Kind::Unearth) { continue; }
        ++n;
        CHECK(a.card_name.str() == "Hellspark Elemental");
        CHECK(a.hand_index == -1);
        CHECK(a.cost.generic == 1);
        CHECK(a.cost.red == 1);
        CHECK(a.vial_attack_power == 3);
        CHECK_FALSE(a.is_noncreature);
    }
    CHECK(n == 2);
    // Incandescent Soulstoke's lord: the unearthed Elemental attacks as a 4/2.
    PutRe(s, "Incandescent Soulstoke");
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    { if (a.kind == Action::Kind::Unearth) { CHECK(a.vial_attack_power == 4); } }
    // After combat: no attack left.
    int post = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/false))
    {
        if (a.kind != Action::Kind::Unearth) { continue; }
        ++post;
        CHECK(a.vial_attack_power == 0);
        CHECK(a.eval == -2);
    }
    CHECK(post == 2);
}

TEST_CASE("Flamekin Bladewhirl: reveal another Elemental card, never itself or a Mimic in hand")
{
    const CardDefinition& bw = DRe("Flamekin Bladewhirl");
    CHECK(bw.params.reveal_or_pay_subtype == "Elemental");
    REQUIRE(bw.params.reveal_or_pay_cost.has_value());
    CHECK(bw.params.reveal_or_pay_cost->generic == 3);
    {
        GameState s = BoardRe();
        HandRe(s, "Flamekin Bladewhirl");
        CHECK_FALSE(CanRevealForAdditionalCost(s, bw));      // only itself
        HandRe(s, "Metallic Mimic");
        CHECK_FALSE(CanRevealForAdditionalCost(s, bw));      // a Shapeshifter in hand
        HandRe(s, "Lightning Bolt");
        CHECK_FALSE(CanRevealForAdditionalCost(s, bw));
        HandRe(s, "Spark Elemental");
        CHECK(CanRevealForAdditionalCost(s, bw));
    }
    {
        GameState s = BoardRe();
        HandRe(s, "Flamekin Bladewhirl");
        HandRe(s, "Flamekin Bladewhirl");
        CHECK(CanRevealForAdditionalCost(s, bw));             // the second copy is a legal reveal
    }
}

TEST_CASE("Sunspine Lynx: ETB damage per nonbasic land each player controls; no lifegain, no prevention")
{
    GameState s = BoardRe();
    s.players[0].life = 20;
    s.players[1].life = 20;
    PutRe(s, "Fiery Islet");
    PutRe(s, "Fiery Islet");
    PutRe(s, "Mountain");
    CHECK_FALSE(PlayersCantGainLifeNow(s));
    CHECK_FALSE(DamageCantBePreventedNow(s));
    // The passive opponent's placeholder lands (GoldFishRunner): one nonbasic, one basic.
    for (int k = 0; k < 2; ++k)
    {
        Card land;
        land.m_name = k == 0 ? "Opponent Nonbasic Land" : "Opponent Basic Land";
        land.RehashName();
        land.AddType(CardType::Land);
        if (k == 1) { land.AddSupertype(Supertype::Basic); }
        Permanent perm; perm.card = land; perm.controller_index = 1; perm.owner_index = 1;
        s.battlefield.push_back(perm);
    }
    Card lynx = DRe("Sunspine Lynx").card; lynx.m_number = g_nre++;
    PutCardOntoBattlefield(s, 0, lynx, "test");
    CHECK(s.players[0].life == 18);   // two nonbasic lands of ours
    CHECK(s.players[1].life == 19);   // the opponent's one nonbasic placeholder
    CHECK(PlayersCantGainLifeNow(s));
    CHECK(DamageCantBePreventedNow(s));
    GainLife(s, 0, 3);
    CHECK(s.players[0].life == 18);
    // The statics end when the Lynx leaves.
    SacrificePermanentAt(s, 0, BfIdx(s, "Sunspine Lynx"));
    CHECK_FALSE(PlayersCantGainLifeNow(s));
    GainLife(s, 0, 3);
    CHECK(s.players[0].life == 21);
}

TEST_CASE("Spark Elemental / Ball Lightning: hasty, sacrificed at the beginning of the end step")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    for (const char* nm : { "Spark Elemental", "Ball Lightning" })
    {
        const CardDefinition& d = DRe(nm);
        CHECK(d.params.endstep_sacrifice_self);
        CHECK(d.card.HasKeyword(Keyword::Haste));
        CHECK(d.card.HasKeyword(Keyword::Trample));
        Permanent p;
        p.card = d.card; p.card.m_number = g_nre++;
        p.controller_index = 0; p.owner_index = 0; p.entered_this_turn = true;
        s.battlefield.push_back(p);
        CHECK(CanAttackFull(s.battlefield.back(), s.battlefield, 0));
    }
    CHECK(DRe("Spark Elemental").card.m_power.value_or(0) == 3);
    CHECK(DRe("Ball Lightning").card.m_power.value_or(0) == 6);
    PerformEndStepSelfSacrifices(s);
    CHECK(s.battlefield.empty());
    CHECK(CountRe(s.players[0].graveyard, "Spark Elemental") == 1);
    CHECK(CountRe(s.players[0].graveyard, "Ball Lightning") == 1);
}
