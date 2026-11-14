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
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <string>
#include <vector>

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
    static const RedElementalsProvider prov;   // the deck's provider (Stage 4a): its champion / Thunderkin hooks
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

// ---- Nova Chaser: CHAMPION an Elemental (CR 702.72; CardParams::champion_subtype) ------------------
namespace
{
// Enter a Nova Chaser the way every entry site does (push, then the enter cascade), the searched pick
// installed as the cast / put sites install it. Returns its card number.
int EnterNova(GameState& s, int pick, bool sac_at_end = false)
{
    Permanent p;
    p.card              = DRe("Nova Chaser").card;
    p.card.m_number     = g_nre++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = true;
    p.sac_at_end_step   = sac_at_end;
    s.battlefield.push_back(p);
    const ChampionPinScope pin(p.card.m_number, pick);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    FireOwnEtbTriggers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    return p.card.m_number;
}
int BfNum(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == num) { return i; } }
    return -1;
}
}   // namespace

TEST_CASE("Nova Chaser: champion exiles the CHOSEN Elemental, linked; the champion stays")
{
    GameState s = BoardRe();
    REQUIRE(DRe("Nova Chaser").params.champion_subtype == "Elemental");
    CHECK(DRe("Nova Chaser").card.m_power.value_or(0) == 10);
    CHECK(DRe("Nova Chaser").card.m_toughness.value_or(0) == 2);
    CHECK(DRe("Nova Chaser").card.m_mana_cost.ManaValue() == 4);
    CHECK_FALSE(DRe("Nova Chaser").card.HasKeyword(Keyword::Haste));
    const int spark = PutRe(s, "Spark Elemental");
    const int ball  = PutRe(s, "Ball Lightning");
    PutRe(s, "Mountain");
    const int nova = EnterNova(s, ball);   // the searched pick: Ball Lightning, not the provider's front
    REQUIRE(BfNum(s, nova) >= 0);
    CHECK(BfNum(s, ball) < 0);
    CHECK(BfNum(s, spark) >= 0);
    REQUIRE(CountRe(s.exile, "Ball Lightning") == 1);
    CHECK(CardIsChampionExile(s.exile.back()));
    CHECK(ChampionExileSource(s.exile.back()) == nova);
    CHECK(s.battlefield[static_cast<std::size_t>(BfNum(s, nova))].linked_exile_number == ball);
    CHECK(CountRe(s.players[0].graveyard, "Ball Lightning") == 0);   // exiled, not died
    CHECK_FALSE(s.creature_died_this_turn);
}

TEST_CASE("Nova Chaser: no other Elemental (or a decline) -> the champion is sacrificed")
{
    GameState s = BoardRe();
    PutRe(s, "Mountain");
    const int nova = EnterNova(s, 0);
    CHECK(BfNum(s, nova) < 0);
    CHECK(CountRe(s.players[0].graveyard, "Nova Chaser") == 1);
    // A decline with a candidate on the board: the candidate stays, the champion goes.
    const int spark = PutRe(s, "Spark Elemental");
    const int nova2 = EnterNova(s, -1);
    CHECK(BfNum(s, nova2) < 0);
    CHECK(BfNum(s, spark) >= 0);
    CHECK(CountRe(s.players[0].graveyard, "Nova Chaser") == 2);
    // Another Nova Chaser is "another Elemental": champions it rather than itself.
    const int nova3 = EnterNova(s, 0);   // unpinned: the provider's front (the doomed Spark first)
    CHECK(BfNum(s, nova3) >= 0);
    CHECK(BfNum(s, spark) < 0);
    CHECK(CountRe(s.exile, "Spark Elemental") == 1);
}

TEST_CASE("Nova Chaser: a Metallic Mimic that chose Elemental is an Elemental you control")
{
    GameState s = BoardRe();
    const int mimic = PutRe(s, "Metallic Mimic");
    s.battlefield.back().card.m_subtypes.push_back("Elemental");   // its chosen type (layer 4)
    const int nova = EnterNova(s, mimic);
    CHECK(BfNum(s, nova) >= 0);
    CHECK(BfNum(s, mimic) < 0);
    REQUIRE(CountRe(s.exile, "Metallic Mimic") == 1);
}

TEST_CASE("Nova Chaser: when it leaves, the championed card returns as a NEW object")
{
    GameState s = BoardRe();
    const int spark = PutRe(s, "Spark Elemental");
    s.battlefield.back().NoteAttackDeclared();
    s.battlefield.back().tapped = true;
    const int nova = EnterNova(s, spark);
    REQUIRE(BfNum(s, spark) < 0);
    SacrificePermanentAt(s, 0, BfNum(s, nova));
    CHECK(CountRe(s.players[0].graveyard, "Nova Chaser") == 1);
    const int i = BfNum(s, spark);
    REQUIRE(i >= 0);
    const Permanent& p = s.battlefield[static_cast<std::size_t>(i)];
    CHECK(static_cast<bool>(p.entered_this_turn));      // summoning sick (Spark has haste of its own, printed)
    CHECK_FALSE(static_cast<bool>(p.tapped));
    CHECK(p.AttacksThisTurn() == 0);
    CHECK(CountRe(s.exile, "Spark Elemental") == 0);
}

TEST_CASE("Nova Chaser: an UNEARTHED Hellspark championed returns free of unearth")
{
    GameState s = BoardRe();
    GyRe(s, "Hellspark Elemental");
    REQUIRE(ApplyUnearth(s, 0, "Hellspark Elemental"));
    const int hs = s.battlefield.back().card.m_number;
    const int nova = EnterNova(s, hs);
    REQUIRE(BfNum(s, hs) < 0);
    REQUIRE(CountRe(s.exile, "Hellspark Elemental") == 1);
    CHECK(CardIsChampionExile(s.exile.back()));
    // The end step's unearth sweep finds nothing (the object is gone).
    PerformEndStepUnearthExiles(s);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 1);
    // The champion leaves: Hellspark returns as a new object, not unearthed -- its later sacrifice puts
    // it into the GRAVEYARD (unearthable again), not exile.
    SacrificePermanentAt(s, 0, BfNum(s, nova));
    const int i = BfNum(s, hs);
    REQUIRE(i >= 0);
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(i)].unearthed));
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(i)].temp_haste));
    SacrificePermanentAt(s, 0, i);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 0);
}

TEST_CASE("Nova Chaser: post-combat champion of a doomed attacker survives the end step")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    for (int k = 0; k < 4; ++k) { PutRe(s, "Mountain"); }
    const int spark = PutRe(s, "Spark Elemental");
    s.battlefield.back().NoteAttackDeclared();
    s.battlefield.back().tapped = true;
    const int put = PutRe(s, "Blistering Firecat");       // a Soulstoke-style put: sacrificed at end step
    s.battlefield.back().sac_at_end_step = true;
    HandRe(s, "Nova Chaser");
    // The post-combat enumeration offers one cast per candidate (+ a decline), the pick in enchant_target.
    std::vector<int> picks;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/false))
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Nova Chaser")
        { picks.push_back(a.enchant_target); }
    }
    // Under the provider's doomed-attacker prune the attacked Spark is the only admitted pick; unpruned
    // (the oracle, ChampionVariantTargets prune=false) the put Firecat and the decline are offered too.
    CHECK(picks == std::vector<int>{spark});
    const std::vector<int> all = ChampionVariantTargets(s, 0, "Elemental", nullptr, /*prune=*/false);
    CHECK(std::find(all.begin(), all.end(), spark) != all.end());
    CHECK(std::find(all.begin(), all.end(), put) != all.end());
    CHECK(std::find(picks.begin(), picks.end(), 0) == picks.end());   // never unpinned
    s.players[0].hand.clear();
    const int nova = EnterNova(s, spark);
    // The end step (both worlds' order): the delayed sacrifice, then the self-sacrifices.
    PerformEndStepUnearthExiles(s);
    PerformEndStepDelayedSacrifices(s);
    DestroyAttackedAtEndStep(s);
    PerformEndStepSelfSacrifices(s);
    CHECK(BfNum(s, nova) >= 0);                                   // the champion lives
    CHECK(CountRe(s.exile, "Spark Elemental") == 1);              // the attacker is preserved in exile
    CHECK(CountRe(s.players[0].graveyard, "Spark Elemental") == 0);
    CHECK(CountRe(s.players[0].graveyard, "Blistering Firecat") == 1);
    // A delayed "sacrifice at end step" pointing at the championed object finds nothing when it returns.
    SacrificePermanentAt(s, 0, BfNum(s, nova));
    const int i = BfNum(s, spark);
    REQUIRE(i >= 0);
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(i)].sac_at_end_step));
}

TEST_CASE("Nova Chaser: a card returning DURING the end step misses that end step's own trigger")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    const int spark = PutRe(s, "Spark Elemental");
    s.battlefield.back().NoteAttackDeclared();
    const int nova = EnterNova(s, spark, /*sac_at_end=*/true);   // a Soulstoke-put Nova
    REQUIRE(BfNum(s, spark) < 0);
    PerformEndStepUnearthExiles(s);
    PerformEndStepDelayedSacrifices(s);    // Nova sacrificed -> Spark returns, a new object
    DestroyAttackedAtEndStep(s);
    PerformEndStepSelfSacrifices(s);       // ...whose "at the beginning of the end step" never triggered
    CHECK(BfNum(s, nova) < 0);
    CHECK(BfNum(s, spark) >= 0);
    // The NEXT end step (the opponent's) does sacrifice it.
    PerformEndStepSelfSacrifices(s);
    CHECK(BfNum(s, spark) < 0);
    CHECK(CountRe(s.players[0].graveyard, "Spark Elemental") == 1);
}

TEST_CASE("Nova Chaser: identical doomed attackers fold to one variant; distinct states do not")
{
    GameState s = BoardRe();
    for (int k = 0; k < 2; ++k) { PutRe(s, "Spark Elemental"); s.battlefield.back().NoteAttackDeclared(); s.battlefield.back().tapped = true; }
    PutRe(s, "Spark Elemental");            // untapped, did not attack: a different choice
    CHECK(ChampionVariantTargets(s, 0, "Elemental", nullptr, /*prune=*/false).size() == 2);
    // The provider's doomed-attacker PRUNE admits the first doomed, spent candidate alone (no decline).
    bool others = true;
    CHECK(ChampionVariantTargets(s, 0, "Elemental", &others).size() == 1);
    CHECK_FALSE(others);
}

// ---- Agent B2: the SAME-LINE entrant pin and the RETURNING champion's pick (no unpinned pick in the window) ----
namespace
{
std::vector<Action> NovaCastVariants(const GameState& s, bool pre_combat)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Nova Chaser") { out.push_back(a); } }
    return out;
}
int HandNum(const GameState& s, const std::string& name, int nth = 0)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name && nth-- == 0) { return c.m_number; } }
    return 0;
}
// "Cast" the hand card with number `num` the way both worlds land a creature: the card keeps its number.
int LandFromHand(GameState& s, int num)
{
    auto& h = s.players[0].hand;
    for (std::size_t i = 0; i < h.size(); ++i)
    {
        if (h[i].m_number != num) { continue; }
        Permanent p;
        p.card = DRe(h[i].m_name.str()).card; p.card.m_number = num;
        p.controller_index = 0; p.owner_index = 0; p.entered_this_turn = true;
        h.erase(h.begin() + static_cast<std::ptrdiff_t>(i));
        s.battlefield.push_back(p);
        return num;
    }
    return 0;
}
}   // namespace

TEST_CASE("Nova Chaser: a SAME-LINE entrant is a searched variant (named by its card), never an unpinned pick")
{
    GameState s = BoardRe();
    for (int k = 0; k < 6; ++k) { PutRe(s, "Mountain"); }
    HandRe(s, "Nova Chaser");
    HandRe(s, "Spark Elemental");
    HandRe(s, "Spark Elemental");     // a second copy: still ONE variant per name
    HandRe(s, "Metallic Mimic");
    HandRe(s, "Lightning Bolt");
    // POST-combat: the deck provider offers the HARDCAST only there (the USER's main-2 rule, Stage 4a).
    const std::vector<Action> v = NovaCastVariants(s, /*pre_combat=*/false);
    // No Elemental on the board: the menu is the decline + one same-line variant per entrant name.
    std::vector<std::string> names;
    bool decline = false;
    for (const Action& a : v)
    {
        CHECK(a.enchant_target != 0);                       // never unpinned
        if (a.enchant_target == -1) { decline = true; CHECK(a.trick_hand_target.empty()); continue; }
        names.push_back(a.trick_hand_target);
        CHECK(a.enchant_target == HandNum(s, a.trick_hand_target));
    }
    CHECK(decline);
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"Metallic Mimic", "Spark Elemental"});
    // The champion follows the entrant it names, whatever order the plan was ranked in.
    Action spark_cast;
    spark_cast.kind = Action::Kind::CastFromHand;
    spark_cast.card_name = "Spark Elemental";
    Action nova_pinned;
    for (const Action& a : v) { if (a.trick_hand_target == "Spark Elemental") { nova_pinned = a; } }
    std::vector<Action> acts{nova_pinned, spark_cast};
    std::vector<int> order{0, 1};
    ApplyAttachSpellDependencyOrder(acts, order);
    CHECK(order == std::vector<int>{1, 0});
    CHECK(OrderingPlacesAttachSpellBeforeTarget(acts));
    // Nova championing ANOTHER copy of itself: it never satisfies its own dependency.
    Action nova_plain = nova_pinned;
    nova_plain.enchant_target = -1; nova_plain.trick_hand_target.clear();
    Action nova_on_nova = nova_pinned;
    nova_on_nova.trick_hand_target = "Nova Chaser";
    std::vector<Action> acts2{nova_on_nova, nova_plain};
    std::vector<int> order2{0, 1};
    ApplyAttachSpellDependencyOrder(acts2, order2);
    CHECK(order2 == std::vector<int>{1, 0});
}

TEST_CASE("Nova Chaser: a same-line pin resolves to the line's entrant (copy swap included); a gone pin declines")
{
    GameState s = BoardRe();
    const int old_spark = PutRe(s, "Spark Elemental");     // on the board before the line, attacked
    s.battlefield.back().NoteAttackDeclared();
    s.battlefield.back().tapped = true;
    HandRe(s, "Spark Elemental");
    HandRe(s, "Spark Elemental");
    const int a = HandNum(s, "Spark Elemental", 0);
    const int b = HandNum(s, "Spark Elemental", 1);
    // Exact: the pinned copy is the one the line cast.
    LandFromHand(s, a);
    const int nova = EnterNova(s, a);
    CHECK(BfNum(s, nova) >= 0);
    CHECK(BfNum(s, a) < 0);
    CHECK(BfNum(s, old_spark) >= 0);                        // not the provider's front (the doomed attacker)
    // Copy swap: the pin names copy `c` still in hand, copy `b` is the one that landed -> the newest Spark.
    HandRe(s, "Spark Elemental");
    const int c = HandNum(s, "Spark Elemental", 1);
    REQUIRE(c != b);
    LandFromHand(s, b);
    const int nova2 = EnterNova(s, c);
    CHECK(BfNum(s, nova2) >= 0);
    CHECK(BfNum(s, b) < 0);
    CHECK(BfNum(s, old_spark) >= 0);
    // A pin whose permanent left earlier in the line (now in exile) is the decline, not a pick.
    const int nova3 = EnterNova(s, a);
    CHECK(BfNum(s, nova3) < 0);
    CHECK(BfNum(s, old_spark) >= 0);
    // Two Nova Chasers swapped by a by-name cast: the pin names the champion ITSELF -> the other Nova.
    GameState t = BoardRe();
    const int n1 = EnterNova(t, -1);                        // (sacrificed: nothing to champion)
    (void)n1;
    const int spark = PutRe(t, "Spark Elemental");
    const int nova_a = EnterNova(t, spark);
    const int self_num = g_nre;                             // the number EnterNova assigns next
    const int nova_b = EnterNova(t, self_num);
    REQUIRE(nova_b == self_num);
    CHECK(BfNum(t, nova_b) >= 0);
    CHECK(BfNum(t, nova_a) < 0);                            // championed the other copy
    CHECK(CountRe(t.exile, "Nova Chaser") == 1);
}

TEST_CASE("Nova Chaser: a put champion's same-line pins, and the RETURNING champion's searched pick")
{
    GameState s = BoardRe();
    for (int k = 0; k < 6; ++k) { PutRe(s, "Mountain"); }
    const int stoke = PutRe(s, "Incandescent Soulstoke");
    const int nova1 = EnterNova(s, stoke);                  // Nova #1 champions a Soulstoke
    const int stoke2 = PutRe(s, "Incandescent Soulstoke");  // the put source
    (void)stoke2;
    HandRe(s, "Nova Chaser");
    HandRe(s, "Spark Elemental");
    GyRe(s, "Hellspark Elemental");
    // The put variants: Nova #1 (with a return rank per ret 1..bound+1), Soulstoke #2, the hand Spark,
    // the graveyard (unearthable) Hellspark, and the decline. Never an unpinned put.
    int on_nova = 0, decline = 0;
    bool spark_pin = false, hellspark_pin = false, stoke_pin = false;
    const int bound = ChampionReturnRankBound(s, 0, "Elemental");
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind != Action::Kind::PutFromHandAbility || a.card_name.str() != "Nova Chaser") { continue; }
        CHECK(a.enchant_target != 0);
        if (a.enchant_target == -1) { ++decline; continue; }
        const int pick = a.enchant_target & kChampionPickMask;
        const int ret  = a.enchant_target >> kChampionRetShift;
        if (pick == nova1) { ++on_nova; CHECK(ret >= 1); CHECK(ret <= bound + 1); continue; }
        CHECK(ret == 0);
        if (a.trick_hand_target == "Spark Elemental")     { spark_pin = true; }
        if (a.trick_hand_target == "Hellspark Elemental") { hellspark_pin = true; }
        if (pick == stoke2)                               { stoke_pin = true; }
    }
    CHECK(decline == 1);
    CHECK(on_nova == bound + 1);
    CHECK(spark_pin);
    CHECK(hellspark_pin);
    CHECK(stoke_pin);

    // Resolution: a Soulstoke-put Nova #2 champions Nova #1 carrying return rank r; at the end step Nova #2
    // is sacrificed, Nova #1 returns (a new object) and its own trigger takes candidate r -- not the front.
    greedywindow::Frame in_window(2);   // a searched frame: an unpinned pick here must be counted
    TurnSolver::ArmChampionCensusForTest();
    const long long before = TurnSolver::ChampionSearchedFrameUnpinnedForTest();
    for (int r = 0; r < 2; ++r)
    {
        GameState t = s;
        const int nova2 = EnterNova(t, ChampionPackPick(nova1, r + 1), /*sac_at_end=*/true);
        REQUIRE(BfNum(t, nova1) < 0);
        REQUIRE(CountRe(t.exile, "Nova Chaser") == 1);
        CHECK(ChampionExileSource(t.exile.back()) == nova2);
        CHECK(ChampionExileRet(t.exile.back()) == r + 1);
        // Nova #1's own Soulstoke came back when it left: the return-time candidates are both Soulstokes.
        // (Computed with Nova #2 gone, as it is when Nova #1 returns.)
        GameState u = t;
        u.battlefield.erase(u.battlefield.begin() + BfNum(u, nova2));
        const std::vector<int> legal = ChampionCandidateIndices(u, 0, nova1, "Elemental");
        REQUIRE(legal.size() == 2);
        const int expect = u.battlefield[static_cast<std::size_t>(legal[static_cast<std::size_t>(r)])].card.m_number;
        PerformEndStepUnearthExiles(t);
        PerformEndStepDelayedSacrifices(t);
        CHECK(BfNum(t, nova2) < 0);
        CHECK(BfNum(t, nova1) >= 0);                        // returned and championed
        CHECK(BfNum(t, expect) < 0);                        // ...exactly rank r
    }
    // A rank past the width declines: the returning Nova is sacrificed.
    {
        GameState t = s;
        EnterNova(t, ChampionPackPick(nova1, bound + 1), /*sac_at_end=*/true);
        PerformEndStepUnearthExiles(t);
        PerformEndStepDelayedSacrifices(t);
        CHECK(BfNum(t, nova1) < 0);
        CHECK(CountRe(t.players[0].graveyard, "Nova Chaser") == 2);
    }
    CHECK(TurnSolver::ChampionSearchedFrameUnpinnedForTest() == before);   // every pick above was pinned
    // An UNPINNED return (ret 0 -- no searched variant set it up; outside the search the census books it
    // as a real resolution, not a searched frame) takes the provider's front and the champion stays.
    {
        GameState t = s;
        EnterNova(t, nova1, /*sac_at_end=*/true);           // ret 0: no return pick carried
        PerformEndStepUnearthExiles(t);
        PerformEndStepDelayedSacrifices(t);
        CHECK(BfNum(t, nova1) >= 0);
    }
    g_champion_unpinned_hook = nullptr;
}

// ==== THUNDERKIN AWAKENER (attack_trigger_reanimate, 2026-11-14) =======================================
namespace
{
// Graveyard CARDS need card numbers below GameState::kFirstTokenNumber (a token is not a card).
int g_ntk = 500;
int GyTk(GameState& s, const std::string& name)
{
    Card c = DRe(name).card; c.m_number = g_ntk++; s.players[0].graveyard.push_back(c);
    return c.m_number;
}
int AwakenerAttacking(GameState& s)
{
    const int n = PutRe(s, "Thunderkin Awakener");
    s.battlefield.back().NoteAttackDeclared();
    s.battlefield.back().tapped = true;
    return n;
}
std::vector<int> FireAwakeners(GameState& s, int pin = -1)
{
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& q = s.battlefield[static_cast<std::size_t>(i)];
        if (q.controller_index == 0 && q.tapped && q.card.IsCreature()) { atk.push_back(i); }
    }
    s.scripted_attack_reanimate = pin;
    FireAttackReanimate(s, 0, atk, /*live=*/true);
    std::vector<int> nums;
    for (int i : atk) { nums.push_back(s.battlefield[static_cast<std::size_t>(i)].card.m_number); }
    return nums;
}
std::vector<std::string> OptNames(const GameState& s, int toughness)
{
    std::vector<std::string> out;
    for (const AttackReanimateOpt& o : AttackReanimateOptions(s, 0, DRe("Thunderkin Awakener").params, toughness, nullptr))
    {
        for (const Card& c : s.players[0].graveyard)
        { if (c.m_number == o.card) { out.push_back(c.m_name.str()); break; } }
    }
    return out;
}
}   // namespace

TEST_CASE("Thunderkin Awakener: targets an Elemental CREATURE CARD in the graveyard below its LIVE toughness")
{
    GameState s = BoardRe();
    const CardParams& p = DRe("Thunderkin Awakener").params;
    REQUIRE(p.attack_trigger_reanimate);
    CHECK(p.attack_trigger_reanimate_subtype == "Elemental");
    CHECK(p.attack_trigger_reanimate_toughness_lt_self);
    CHECK(p.attack_trigger_reanimate_sac_end_step);
    CHECK(DRe("Thunderkin Awakener").card.HasKeyword(Keyword::Haste));
    GyTk(s, "Spark Elemental");      // 3/1 Elemental: legal
    GyTk(s, "Nova Chaser");          // 10/2: not below toughness 2
    GyTk(s, "Metallic Mimic");       // 2/1 Shapeshifter: not an Elemental card
    GyTk(s, "Lightning Bolt");       // not a creature
    GyTk(s, "Mountain");
    PutRe(s, "Ball Lightning");      // on the battlefield, not in the graveyard
    CHECK(OptNames(s, 2) == std::vector<std::string>{"Spark Elemental"});
    // Toughness 3 (a Soulstoke lord / a Mimic counter) admits the toughness-2 Nova Chaser -- expanded into
    // its champion picks (the battlefield Ball Lightning) plus the decline.
    const std::vector<AttackReanimateOpt> o3 = AttackReanimateOptions(s, 0, p, 3, nullptr);
    int nova_entries = 0;
    for (const AttackReanimateOpt& o : o3) { if (o.champion != 0) { ++nova_entries; } }
    CHECK(o3.size() == 1u + static_cast<std::size_t>(nova_entries));
    CHECK(nova_entries >= 1);
    // Live toughness: the Awakener under an Incandescent Soulstoke is a 2/3 and can return Nova.
    GameState l = BoardRe();
    PutRe(l, "Incandescent Soulstoke");
    AwakenerAttacking(l);
    const int nova = GyTk(l, "Nova Chaser");
    FireAwakeners(l, (o3.size() > 0 ? 0 : -1));
    CHECK(CountRe(l.players[0].graveyard, "Nova Chaser") == 0);
    (void)nova;
}

TEST_CASE("Thunderkin Awakener: the card returns TAPPED and ATTACKING, doomed, not unearthed; no trigger of its own")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    PutRe(s, "Incandescent Soulstoke");                    // toughness 3: a Thunderkin card is a legal target
    const int awk = AwakenerAttacking(s);
    const int tk2 = GyTk(s, "Thunderkin Awakener");
    const int hs  = GyTk(s, "Hellspark Elemental");
    // Default order: biggest printed power first (Hellspark 3 over Thunderkin 1) -- rank 1 = Thunderkin.
    CHECK(OptNames(s, 3) == std::vector<std::string>{"Hellspark Elemental", "Thunderkin Awakener"});
    const std::vector<int> atk = FireAwakeners(s, 1);
    REQUIRE(BfNum(s, tk2) >= 0);
    const Permanent& r = s.battlefield[static_cast<std::size_t>(BfNum(s, tk2))];
    CHECK(r.tapped);
    CHECK(static_cast<bool>(r.sac_at_end_step));
    CHECK_FALSE(static_cast<bool>(r.unearthed));
    CHECK(std::find(atk.begin(), atk.end(), tk2) != atk.end());   // it joins this combat
    CHECK(std::find(atk.begin(), atk.end(), awk) != atk.end());
    // The returned Thunderkin was never declared: no second trigger, Hellspark stays in the graveyard.
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 1);
    // One trigger per DECLARED copy with no legal target does nothing.
    GameState e = BoardRe();
    AwakenerAttacking(e);
    GyTk(e, "Nova Chaser");
    const std::size_t bf = e.battlefield.size();
    FireAwakeners(e);
    CHECK(e.battlefield.size() == bf);
    (void)hs;
}

TEST_CASE("Thunderkin Awakener: the returned body deals combat damage; the projection sees it")
{
    GameState s = BoardRe();
    s.players[1].life = 20;
    for (int k = 0; k < 2; ++k) { PutRe(s, "Mountain"); }
    const int awk = PutRe(s, "Thunderkin Awakener");
    (void)awk;
    GyTk(s, "Spark Elemental");
    GyTk(s, "Blistering Firecat");
    const int proj = TurnSolver::ProjectedAttackDamageForTest(s);
    GameState t = s;
    RolloutSimulateCombat(t);
    // 1 (the Awakener) + 7 (the Firecat, the default front: biggest power).
    CHECK(s.players[1].life - t.players[1].life == 8);
    CHECK(proj == 8);
    CHECK(CountRe(t.players[0].graveyard, "Blistering Firecat") == 0);
    // The searched rank 1 returns the Spark instead: 1 + 3.
    GameState u = s;
    u.scripted_attack_reanimate = 1;
    CHECK(TurnSolver::ProjectedAttackDamageForTest(u) == 4);
    RolloutSimulateCombat(u);
    CHECK(s.players[1].life - u.players[1].life == 4);
}

TEST_CASE("Thunderkin Awakener: the delayed sacrifice keys on the OBJECT; a self-sacrificer dies once")
{
    GameState s = BoardRe();
    s.deck_has_endstep_self_sac = true;
    AwakenerAttacking(s);
    const int spark = GyTk(s, "Spark Elemental");
    const int hs    = GyTk(s, "Hellspark Elemental");
    FireAwakeners(s, 1);   // Hellspark / Spark tie on power 3: the non-unearth Spark is the front -> rank 1 = Hellspark
    REQUIRE(BfNum(s, hs) >= 0);
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(BfNum(s, hs))].unearthed));
    PerformEndStepUnearthExiles(s);
    PerformEndStepDelayedSacrifices(s);
    DestroyAttackedAtEndStep(s);
    PerformEndStepSelfSacrifices(s);
    // Not unearthed: the returned Hellspark dies into the graveyard (a later unearth / reanimation), once.
    CHECK(BfNum(s, hs) < 0);
    CHECK(CountRe(s.players[0].graveyard, "Hellspark Elemental") == 1);
    CHECK(CountRe(s.exile, "Hellspark Elemental") == 0);
    CHECK(CountRe(s.players[0].graveyard, "Spark Elemental") == 1);
    (void)spark;

    // Championed after its return, then back as a NEW object: the delayed sacrifice finds nothing.
    GameState c = BoardRe();
    AwakenerAttacking(c);
    const int bw = GyTk(c, "Flamekin Bladewhirl");
    FireAwakeners(c);
    REQUIRE(BfNum(c, bw) >= 0);
    CHECK(static_cast<bool>(c.battlefield[static_cast<std::size_t>(BfNum(c, bw))].sac_at_end_step));
    const int nova = EnterNova(c, bw);
    REQUIRE(BfNum(c, bw) < 0);
    SacrificePermanentAt(c, 0, BfNum(c, nova));   // the champion leaves -> Bladewhirl returns
    REQUIRE(BfNum(c, bw) >= 0);
    CHECK_FALSE(static_cast<bool>(c.battlefield[static_cast<std::size_t>(BfNum(c, bw))].sac_at_end_step));
    PerformEndStepDelayedSacrifices(c);
    CHECK(BfNum(c, bw) >= 0);
}

TEST_CASE("Thunderkin Awakener: the returned card enters through the enter cascade (Mimic's counter)")
{
    GameState s = BoardRe();
    PutRe(s, "Metallic Mimic");
    s.battlefield.back().card.m_subtypes.push_back("Elemental");   // its chosen type
    s.battlefield.back().chosen_subtype_id = SubtypeRegistry::Instance().Intern("Elemental");
    AwakenerAttacking(s);
    const int spark = GyTk(s, "Spark Elemental");
    FireAwakeners(s);
    REQUIRE(BfNum(s, spark) >= 0);
    const Permanent& r = s.battlefield[static_cast<std::size_t>(BfNum(s, spark))];
    CHECK(r.EffectivePower() == 4);       // 3/1 + the Mimic's +1/+1 counter
}

// ---- Stage 4a: routing, the root's neutral defaults, and the USER's Nova Chaser main-2 rule -------------------------

TEST_CASE("Red Elementals: routes to its own provider, above Morph")
{
    EnsureCardsRe();
    const Decklist d = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Red Elementals/Red Elementals.cod"));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "RedElementals");
    // The signature survives cutting any ONE of its three cards.
    for (const char* cut : {"Nova Chaser", "Thunderkin Awakener", "Hellspark Elemental"})
    {
        Decklist e = d;
        e.mainboard.erase(std::remove_if(e.mainboard.begin(), e.mainboard.end(),
                                         [&](const Card& c) { return c.m_name.str() == cut; }),
                          e.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(e).Name()) == "RedElementals", "cutting ", cut);
    }
}

TEST_CASE("Champion / Thunderkin: the ROOT provider is neutral; the deck provider orders and prunes")
{
    GameState s = BoardRe();
    PutRe(s, "Flamekin Bladewhirl");                                   // live, untapped, power 1
    const int spark = PutRe(s, "Spark Elemental", 0, /*tapped=*/true); // doomed + spent
    s.battlefield.back().NoteAttackDeclared();
    const GenericProvider generic;
    std::vector<int> idx{0, 1};
    generic.OrderChampionCandidates(s, 0, idx);
    CHECK(idx == std::vector<int>{0, 1});                              // battlefield order kept
    bool others = true;
    generic.PruneChampionCandidates(s, 0, idx, others);
    CHECK(idx.size() == 2);
    CHECK(others);
    const RedElementalsProvider deck;
    deck.OrderChampionCandidates(s, 0, idx);
    CHECK(idx == std::vector<int>{1, 0});                              // the doomed Spark first
    deck.PruneChampionCandidates(s, 0, idx, others);
    CHECK(idx == std::vector<int>{1});
    CHECK_FALSE(others);
    CHECK(s.battlefield[1].card.m_number == spark);
    // Thunderkin's target order: neutral at the root, printed power first in the deck provider.
    const int bl = GyRe(s, "Spark Elemental");
    const int ball = GyRe(s, "Ball Lightning");
    std::vector<int> gy{bl, ball};
    generic.AttackReanimateTargets(s, 0, gy);
    CHECK(gy == std::vector<int>{bl, ball});
    deck.AttackReanimateTargets(s, 0, gy);
    CHECK(gy == std::vector<int>{ball, bl});
}

TEST_CASE("Nova Chaser: HARDCAST only after combat (USER main-2 rule); the Soulstoke put stays in main 1")
{
    GameState s = BoardRe();
    for (int k = 0; k < 6; ++k) { PutRe(s, "Mountain"); }
    PutRe(s, "Flamekin Bladewhirl");
    PutRe(s, "Incandescent Soulstoke");
    HandRe(s, "Nova Chaser");
    // Pre-combat: no hardcast variant at all; the Soulstoke put of Nova (PutFromHandAbility) is offered -- it
    // grants haste, so that Nova attacks this turn.
    CHECK(NovaCastVariants(s, /*pre_combat=*/true).empty());
    bool put_nova = false;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind == Action::Kind::PutFromHandAbility && a.card_name.str() == "Nova Chaser") { put_nova = true; }
    }
    CHECK(put_nova);
    // Post-combat: every searched champion variant is back.
    CHECK_FALSE(NovaCastVariants(s, /*pre_combat=*/false).empty());
    // The rule is the deck provider's: the root offers the main-1 hardcast.
    const GenericProvider generic;
    s.m_provider = &generic;
    CHECK_FALSE(NovaCastVariants(s, /*pre_combat=*/true).empty());
}

// ---- INCANDESCENT SOULSTOKE AT THE OPPONENT'S END STEP (agent F, 2026-11-14) --------------------------------
// "{1}{R}, {T}: You may put an Elemental creature card from your hand onto the battlefield. That creature gains
// haste until end of turn. Sacrifice it at the beginning of the next end step." Activated in morph's opponent-end-
// step window (SpellEffects.h EotFaceUpMenu / EotPutOptions), searched on the BoundaryFan's face-up axis.
#include "ai/AIEngine.h"
#include "core/GameEngine.h"
namespace
{
GameState EotRe()
{
    GameState s = BoardRe();
    s.deck_has_eot_tap_put = true;
    for (int i = 0; i < 20; ++i) { Card c = DRe("Mountain").card; c.m_number = g_nre++; s.players[0].library.push_back(c); }
    return s;
}
int ByNameNum(const GameState& s, const std::string& name)
{
    const int i = BfIdx(s, name);
    return i < 0 ? -1 : s.battlefield[static_cast<std::size_t>(i)].card.m_number;
}
}   // namespace

TEST_CASE("Soulstoke eot: legal only untapped, not summoning sick, {1}{R} untapped, an Elemental creature in hand")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Ball Lightning");
    HandRe(s, "Lightning Bolt");
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    REQUIRE(menu.size() == 2u);                      // put Ball Lightning (the front), put nothing
    REQUIRE(menu[0].puts.size() == 1u);
    CHECK(menu[0].puts[0].name == "Ball Lightning");
    CHECK(menu[1].puts.empty());
    {   // cast this turn: not ours since our most recent turn began (CR 302.6)
        GameState t = s;
        t.battlefield[static_cast<std::size_t>(BfIdx(t, "Incandescent Soulstoke"))].entered_this_turn = true;
        CHECK_FALSE(AnyEotPutCandidate(t));
        CHECK(EotFaceUpMenu(t).empty());
    }
    {   // tapped (it attacked or activated this turn)
        GameState t = s;
        t.battlefield[static_cast<std::size_t>(BfIdx(t, "Incandescent Soulstoke"))].tapped = true;
        CHECK(EotFaceUpMenu(t).empty());
    }
    {   // no Elemental CREATURE card in hand (Lightning Bolt is not one)
        GameState t = s;
        t.players[0].hand.erase(t.players[0].hand.begin());
        CHECK(EotFaceUpMenu(t).empty());
    }
    {   // one untapped Mountain: {1}{R} not payable -> the lone "nothing" entry (no fan)
        GameState t = s;
        t.battlefield[static_cast<std::size_t>(BfIdx(t, "Mountain"))].tapped = true;
        const auto m = EotFaceUpMenu(t);
        REQUIRE(m.size() == 1u);
        CHECK(m[0].puts.empty());
    }
    {   // the deck stamp gates it all
        GameState t = s;
        t.deck_has_eot_tap_put = false;
        CHECK(EotFaceUpMenu(t).empty());
    }
}

TEST_CASE("Soulstoke eot: the put survives into our turn, attacks without haste, dies at OUR next end step")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Ball Lightning");
    HandRe(s, "Spark Elemental");
    s.scripted_eot_face_up = 0;   // the front: put Ball Lightning (power 6 before Spark's 1)
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    const int bi = BfIdx(s, "Ball Lightning");
    REQUIRE(bi >= 0);
    const Permanent& bl = s.battlefield[static_cast<std::size_t>(bi)];
    CHECK(bl.sac_at_end_step);
    CHECK_FALSE(bl.temp_haste);          // its haste ended at the opponent's cleanup
    CHECK_FALSE(bl.entered_this_turn);   // ours since this turn began: it attacks without haste
    CHECK(bl.CanTap());
    // Soulstoke untapped at our untap step: the main-phase put is available again (a second hasty creature).
    const int so = ByNameNum(s, "Incandescent Soulstoke");
    CHECK_FALSE(s.battlefield[static_cast<std::size_t>(BfIdx(s, "Incandescent Soulstoke"))].tapped);
    CHECK(PutFromHandLegalIndex(s, 0, so, "Spark Elemental") >= 0);
    bool main_put = false;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    { if (a.kind == Action::Kind::PutFromHandAbility && a.card_name.str() == "Spark Elemental") { main_put = true; } }
    CHECK(main_put);
    // Our end step: the delayed sacrifice (and Ball Lightning's own trigger) -- once, to the graveyard.
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(BfIdx(s, "Ball Lightning") < 0);
    CHECK(CountRe(s.players[0].graveyard, "Ball Lightning") == 1);
}

TEST_CASE("Soulstoke eot: one cast on turn N cannot activate at the end of N, but can at the end of N+1")
{
    GameState s = EotRe();
    const int so = PutRe(s, "Incandescent Soulstoke");
    s.battlefield.back().entered_this_turn = true;   // cast this turn
    for (int k = 0; k < 4; ++k) { PutRe(s, "Mountain"); }
    HandRe(s, "Ball Lightning");
    HandRe(s, "Spark Elemental");
    s.scripted_eot_face_up = 0;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));   // the end of turn N: nothing can be put
    CHECK(BfIdx(s, "Ball Lightning") < 0);
    CHECK(s.players[0].hand.size() == 3u);   // + the draw
    CHECK(AnyEotPutCandidate(s));             // turn N+1: ours since the turn began
    s.scripted_eot_face_up = 0;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));   // the end of turn N+1
    CHECK(BfIdx(s, "Ball Lightning") >= 0);
    CHECK(ByNameNum(s, "Incandescent Soulstoke") == so);
}

// Q5 (agent F2): "the next end step" is a BOOL (Permanent::sac_at_end_step), not a turn stamp. Proven for both put
// times with a creature that has NO self-sacrifice of its own (Flamekin Bladewhirl), so only the delayed trigger acts.
TEST_CASE("Soulstoke eot: the delayed sacrifice bool -- a main-phase put dies at OUR end step that turn")
{
    GameState s = EotRe();
    const int so = PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Flamekin Bladewhirl");
    REQUIRE(ApplyPutFromHand(s, 0, so, "Flamekin Bladewhirl"));   // the main-phase put (mana paid by the caller)
    const int bw = BfIdx(s, "Flamekin Bladewhirl");
    REQUIRE(bw >= 0);
    CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(bw)].sac_at_end_step));
    CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(BfIdx(s, "Incandescent Soulstoke"))].tapped));
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));   // OUR end step, then the opponent's turn
    CHECK(BfIdx(s, "Flamekin Bladewhirl") < 0);                    // sacrificed at the very next end step: ours
    CHECK(CountRe(s.players[0].graveyard, "Flamekin Bladewhirl") == 1);
}

TEST_CASE("Soulstoke eot: the delayed sacrifice bool -- an opponent-end-step put survives our turn, dies at OUR end step")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Flamekin Bladewhirl");
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    REQUIRE(menu.size() == 2u);
    REQUIRE(menu[0].puts.size() == 1u);
    CHECK(menu[0].puts[0].name == "Flamekin Bladewhirl");
    s.scripted_eot_face_up = 0;                                    // put it at the opponent's end step
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));   // our end step, opponent's turn + window, our untap
    const int bw = BfIdx(s, "Flamekin Bladewhirl");
    REQUIRE(bw >= 0);                                             // NOT sacrificed at the opponent's end step
    CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(bw)].sac_at_end_step));
    CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(bw)].entered_this_turn));   // attacks this turn, no haste needed
    CHECK(CountRe(s.players[0].graveyard, "Flamekin Bladewhirl") == 0);
    // Through our turn to OUR end step: pin the window to "put nothing" so the second crossing puts no other card.
    const std::vector<EotFaceUpOption> m2 = EotFaceUpMenu(s);
    int nothing = -1;
    for (std::size_t i = 0; i < m2.size(); ++i) { if (m2[i].puts.empty() && m2[i].ids.empty()) { nothing = static_cast<int>(i); } }
    if (nothing >= 0) { s.scripted_eot_face_up = nothing; }
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(BfIdx(s, "Flamekin Bladewhirl") < 0);                    // sacrificed at OUR next end step
    CHECK(CountRe(s.players[0].graveyard, "Flamekin Bladewhirl") == 1);
}

TEST_CASE("Soulstoke eot: the real turn loop and the rollout take the same steps (parity)")
{
    for (int pin : { 0, 1 })
    {
        GameState before = EotRe();
        PutRe(before, "Incandescent Soulstoke");
        PutRe(before, "Mountain");
        PutRe(before, "Mountain");
        PutRe(before, "Mountain", 0, /*tapped=*/true);
        HandRe(before, "Ball Lightning");
        HandRe(before, "Hellspark Elemental");
        before.scripted_eot_face_up = static_cast<std::int16_t>(pin);
        GameState rollout = before;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(rollout));
        GameState executor = before;
        AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine engine(ai);
        REQUIRE(engine.AdvanceToFirstMainForTest(executor));
        CHECK(executor.turn_number == rollout.turn_number);
        REQUIRE(executor.battlefield.size() == rollout.battlefield.size());
        for (std::size_t i = 0; i < rollout.battlefield.size(); ++i)
        {
            CHECK(executor.battlefield[i].card.m_number == rollout.battlefield[i].card.m_number);
            CHECK(static_cast<bool>(executor.battlefield[i].tapped) == static_cast<bool>(rollout.battlefield[i].tapped));
            CHECK(static_cast<bool>(executor.battlefield[i].sac_at_end_step) == static_cast<bool>(rollout.battlefield[i].sac_at_end_step));
            CHECK(static_cast<bool>(executor.battlefield[i].temp_haste) == static_cast<bool>(rollout.battlefield[i].temp_haste));
        }
        CHECK(executor.players[0].hand.size() == rollout.players[0].hand.size());
        CHECK((BfIdx(rollout, "Ball Lightning") >= 0) == (pin == 0));
    }
}

TEST_CASE("Soulstoke eot: the BoundaryFan crosses once per menu rank and records it; two sources put two")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Ball Lightning");
    const auto crossings = TurnSolver::FaceUpBoundaryForTest(s);
    REQUIRE(crossings.size() == 2u);
    CHECK(crossings[0].first == 0);
    CHECK(crossings[1].first == 1);
    // Two Soulstokes, four Mountains, two names: the multisets of up to two puts (the sources are interchangeable),
    // the front puts both names, "nothing" is last.
    GameState t = EotRe();
    PutRe(t, "Incandescent Soulstoke");
    PutRe(t, "Incandescent Soulstoke");
    for (int k = 0; k < 4; ++k) { PutRe(t, "Mountain"); }
    HandRe(t, "Ball Lightning");
    HandRe(t, "Spark Elemental");
    const auto m = EotFaceUpMenu(t);
    REQUIRE(m.size() == 4u);   // {BL, Spark}, {BL}, {Spark}, {}
    CHECK(m[0].puts.size() == 2u);
    CHECK(m[0].puts[0].src != m[0].puts[1].src);
    CHECK(m.back().puts.empty());
    GameState u = t;
    u.scripted_eot_face_up = 0;
    PerformOpponentEndStepFaceUp(u);
    CHECK(BfIdx(u, "Ball Lightning") >= 0);
    CHECK(BfIdx(u, "Spark Elemental") >= 0);
    // No live source: the axis is not live, the single crossing records nothing.
    GameState n = EotRe();
    PutRe(n, "Mountain");
    const auto none = TurnSolver::FaceUpBoundaryForTest(n);
    REQUIRE(none.size() == 1u);
    CHECK(none[0].first == -2);
}

TEST_CASE("Soulstoke eot: a put Nova Chaser's champion pick is a searched variant, never unpinned")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Nova Chaser");
    const auto m = EotFaceUpMenu(s);
    REQUIRE(m.size() >= 2u);
    bool decline = false;
    for (const EotFaceUpOption& o : m)
    {
        for (const EotPutStep& st : o.puts)
        {
            CHECK(st.name == "Nova Chaser");
            CHECK(st.champion != 0);   // a pinned exile pick or the decline -- no unpinned resolution
            decline = decline || st.champion == -1;
        }
    }
    CHECK(decline);
    CHECK(m.back().puts.empty());
}

TEST_CASE("Soulstoke eot: the searched HOLD -- an attacker the window could use is contested (kind 4)")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Ball Lightning");
    CHECK(DorkAtkContestedKind(s) == 4);
    {
        GameState t = s;
        t.players[0].hand.clear();
        CHECK(DorkAtkContestedKind(t) == 0);   // nothing to put: the attack is not contested
    }
    {
        GameState t = s;
        t.deck_has_eot_tap_put = false;
        CHECK(DorkAtkContestedKind(t) == 0);
    }
    {
        GameState t = s;
        t.battlefield[static_cast<std::size_t>(BfIdx(t, "Incandescent Soulstoke"))].entered_this_turn = true;
        CHECK(DorkAtkContestedKind(t) == 0);   // summoning sick: it can neither attack nor use the window
    }
}

TEST_CASE("Landfall Elementals: a Soulstoke-put Risen Reef's enter trigger fires in the window")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Risen Reef");
    const std::size_t lib_before = s.players[0].library.size();
    s.scripted_eot_face_up = 0;
    PerformOpponentEndStepFaceUp(s);
    REQUIRE(BfIdx(s, "Risen Reef") >= 0);
    // The look took the top card (a Mountain): onto the battlefield tapped or into the hand.
    CHECK(s.players[0].library.size() == lib_before - 1);
}

// B (agent F2, the no-greedy rule): a window-put creature's OWN enter choices are menu variants, exactly as the
// main-phase put's are (the tutor axis and the look-top fan) -- never the provider's default inside the window.
TEST_CASE("Landfall Elementals: a window-put Flamekin Harbinger's tutor target is a searched menu variant")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Flamekin Harbinger");
    for (const char* nm : { "Ball Lightning", "Spark Elemental", "Hellspark Elemental" })
    { Card c = DRe(nm).card; c.m_number = g_nre++; s.players[0].library.push_back(c); }
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    REQUIRE_FALSE(menu.empty());
    REQUIRE(menu[0].puts.size() == 1u);
    CHECK(menu[0].puts[0].name == "Flamekin Harbinger");
    CHECK(menu[0].puts[0].tutor == -1);          // the front: the provider's pick at resolution
    std::vector<int> tutors;
    for (const EotFaceUpOption& o : menu)
    { if (o.puts.size() == 1u && o.puts[0].name == "Flamekin Harbinger") { tutors.push_back(o.puts[0].tutor); } }
    CHECK(tutors.size() >= 3u);                  // front, >= 1 other rank, the optional decline
    CHECK(std::find(tutors.begin(), tutors.end(), 1) != tutors.end());
    CHECK(std::find(tutors.begin(), tutors.end(), kTutorDeclineChoice) != tutors.end());
    CHECK(menu.back().puts.empty());             // put nothing stays last
    // The pins steer the real resolution: rank -1 and rank 1 put DIFFERENT Elementals on top; declining keeps the top.
    auto top_after = [&](int tutor) -> std::string
    {
        GameState t = s;
        for (std::size_t i = 0; i < menu.size(); ++i)
        {
            if (menu[i].puts.size() == 1u && menu[i].puts[0].tutor == tutor) { t.scripted_eot_face_up = static_cast<int>(i); break; }
        }
        PerformOpponentEndStepFaceUp(t);
        REQUIRE(BfIdx(t, "Flamekin Harbinger") >= 0);
        return t.players[0].library.front().m_name.str();
    };
    const std::string a = top_after(-1), b = top_after(1), d = top_after(kTutorDeclineChoice);
    CHECK(a != b);
    CHECK(a != "Mountain");
    CHECK(b != "Mountain");
    CHECK(d == "Mountain");
}

TEST_CASE("Landfall Elementals: a window put under a Risen Reef fans the look-top land / order pins")
{
    GameState s = EotRe();
    s.deck_has_look_top_watcher = true;
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Risen Reef");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Flamekin Harbinger");
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    bool flip = false, order = false;
    for (const EotFaceUpOption& o : menu)
    {
        for (const EotPutStep& st : o.puts)
        {
            for (int v : st.look_top)
            {
                if (v == kLookTopLandFlip) { flip = true; }
                if (v >= kLookTopOrderBase) { order = true; }
            }
        }
    }
    CHECK(flip);
    CHECK(order);
    REQUIRE_FALSE(menu.empty());
    CHECK(menu[0].puts[0].look_top.empty());     // the front: every look at the provider's default
}

// A (agent F2, the completeness rule): the kind-4 hold's gate / contested tests the non-FSLineWin hosts' twins use.
TEST_CASE("Soulstoke eot: the hold twin's gate and contested test")
{
    GameState s = EotRe();
    PutRe(s, "Incandescent Soulstoke");
    PutRe(s, "Mountain");
    PutRe(s, "Mountain");
    HandRe(s, "Ball Lightning");
    CHECK(EotHoldTwinGate(s));
    CHECK(EotHoldContested(s));
    GameState t = s;
    t.players[0].hand.clear();
    CHECK_FALSE(EotHoldTwinGate(t));
    CHECK_FALSE(EotHoldContested(t));
    t = s;
    t.battlefield[static_cast<std::size_t>(BfIdx(t, "Incandescent Soulstoke"))].tapped = true;   // a main-phase put
    CHECK_FALSE(EotHoldContested(t));
    t = s;
    t.deck_has_eot_tap_put = false;
    CHECK_FALSE(EotHoldTwinGate(t));
}

// ---- STAGE 5f: THE DEAD-TRICK FOLD (RedElementalsProvider::TrickTargetsAttackersOnly / TrickTargetCandidates) ----
// Legion Leadership doubles power until end of turn and nothing in the list reads a non-attacker's power, so a
// Leadership aimed at a creature that cannot attack this turn, or cast after combat, is dominated by keeping it.
#include "ai/HeuristicArm.h"
namespace
{
// The Leadership CastFromHand variants: battlefield targets by card number, same-plan hand targets by name.
struct LeadershipTargets { std::vector<int> bf; std::vector<std::string> hand; int casts = 0; };
LeadershipTargets LeadershipVariants(const GameState& s, bool pre_combat)
{
    LeadershipTargets t;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Legion Leadership") { continue; }
        ++t.casts;
        if (!a.trick_hand_target.empty()) { t.hand.push_back(a.trick_hand_target); }
        else { t.bf.push_back(a.enchant_target); }
    }
    return t;
}
template <class V, class T> bool Has(const V& v, const T& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
}   // namespace

TEST_CASE("Legion Leadership: no target that cannot attack this turn; no cast after combat (sound fold)")
{
    GameState s = BoardRe();
    for (int k = 0; k < 6; ++k) { PutRe(s, "Mountain"); }
    const int ball = PutRe(s, "Ball Lightning");                 // attack-eligible
    const int blade = PutRe(s, "Flamekin Bladewhirl");           // summoning sick, no haste
    s.battlefield[static_cast<std::size_t>(BfIdx(s, "Flamekin Bladewhirl"))].entered_this_turn = true;
    const int stoke = PutRe(s, "Incandescent Soulstoke", 0, /*tapped=*/true);   // tapped (paid for a put)
    HandRe(s, "Legion Leadership");
    HandRe(s, "Spark Elemental");                                // a hasty same-plan body
    HandRe(s, "Metallic Mimic");                                 // no haste: it cannot attack this turn

    s.phase = Phase::PreCombatMain;
    const LeadershipTargets pre = LeadershipVariants(s, /*pre_combat=*/true);
    CHECK(Has(pre.bf, ball));
    CHECK_FALSE(Has(pre.bf, blade));
    CHECK_FALSE(Has(pre.bf, stoke));
    CHECK(Has(pre.hand, std::string("Spark Elemental")));
    CHECK_FALSE(Has(pre.hand, std::string("Metallic Mimic")));

    s.phase = Phase::PostCombatMain;
    CHECK(LeadershipVariants(s, /*pre_combat=*/false).casts == 0);

    // The lever off (heurarm REDELEM_TRICK_FOLD = 0): every target again, and the post-combat cast is back.
    heurarm::t_arm[static_cast<int>(heurarm::REDELEM_TRICK_FOLD)] = 0;
    s.phase = Phase::PreCombatMain;
    const LeadershipTargets open = LeadershipVariants(s, /*pre_combat=*/true);
    CHECK(Has(open.bf, blade));
    CHECK(Has(open.bf, stoke));
    CHECK(Has(open.hand, std::string("Metallic Mimic")));
    s.phase = Phase::PostCombatMain;
    CHECK(LeadershipVariants(s, /*pre_combat=*/false).casts > 0);
    heurarm::Clear();
}
