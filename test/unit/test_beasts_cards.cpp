// Beasts Stage 2 (2026-11-21). Batch A: Garruk's Packleader (power-3 enter draw), Krosan Warchief (Beast
// reducer, stacking with Urza's Incubator), Questing Beast's duplicate-legend offer, Thragtusk (LTB token),
// Cragplate Baloth (kicked counters), Kaheera (lord + vigilance). Batch B: Contested Cliffs (TapFight: the
// searched two-target fight, deathtouch, the SBA death pass). Batch C: Elder Gargaroth (the searched
// three-mode attack trigger, the provider's life fold). Batch D: Garruk's Harbinger. Batch E: Migloz, Maze Crusher
// (oil counters, the searched oil pump, the pruned vigilance grant and destroy). Batch F: Nykthos. Batch G: Bloated
// Contaminator (toxic, the combat-damage proliferate, poison in the search, 2HG's fifteen). Each test drives the SHARED helpers both worlds
// call (FireEtbWatchers / FireOwnEtbTriggers, EffectiveSpellCost, OfferDuplicateLegendCast, ApplyPermAbility,
// EnumerateMainPlans + ApplyPlan).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/Dominance.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/Companion.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
const CardDefinition& BeDef(const char* name)
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

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card BeZone(const char* name, int number)
{
    BeDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int BeAdd(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = BeDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState BeState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.phase = Phase::PreCombatMain;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(BeZone("Forest", 200 + k)); }
    return s;
}

// A creature ENTERS (both enter cascades, as every real entry route runs them).
void BeEnter(GameState& s, const char* name, int number, int controller = 0)
{
    const int idx = BeAdd(s, name, number, controller, /*sick=*/true);
    FireEtbWatchers(s, controller, idx);
    FireOwnEtbTriggers(s, controller, idx, std::string(), -1);
}

int BeGeneric(const char* spell, const GameState& s)
{
    return EffectiveSpellCost(BeDef(spell), s, 1).generic;
}

struct BeSearchedScope
{
    bool saved = g_searched_play;
    explicit BeSearchedScope(bool on) { TurnSolver::SetSearchedPlay(on); }
    ~BeSearchedScope() { TurnSolver::SetSearchedPlay(saved); }
};
}   // namespace

TEST_CASE("Beasts provider routing survives removal of any one signature card")
{
    BeDef("Elder Gargaroth");
    const Decklist full = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Beasts/Beasts.cod"));
    CHECK(std::string(DetectDecisionProvider(full).Name()) == "Beasts");
    for (const char* cut : {"Elder Gargaroth", "Garruk's Harbinger", "Bloated Contaminator",
                            "Migloz, Maze Crusher", "Woodland Bellower", "Cragplate Baloth"})
    {
        Decklist d = full;
        d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(),
            [&](const Card& c) { return c.m_name.str() == cut; }), d.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(d).Name()) == "Beasts", "cutting ", cut);
    }
}

TEST_CASE("Beasts provider second main opens for combat draw and proliferate resources")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    CHECK_FALSE(ProviderSecondMainLive(s));
    const CardParams& garg = BeDef("Elder Gargaroth").params;
    CHECK(ResolveAttackChooseOneMode(s, 0, garg, "Elder Gargaroth", 2));
    CHECK(ProviderSecondMainLive(s));
    ++s.turn_number;
    CHECK_FALSE(ProviderSecondMainLive(s));

    GameState token = BeState();
    token.m_provider = &provider;
    CHECK(ResolveAttackChooseOneMode(token, 0, garg, "Elder Gargaroth", 0));
    CHECK_FALSE(ProviderSecondMainLive(token));
    BeAdd(token, "Garruk's Packleader", 50);
    CHECK(ResolveAttackChooseOneMode(token, 0, garg, "Elder Gargaroth", 0));
    CHECK(ProviderSecondMainLive(token));

    GameState dig = BeState();
    dig.m_provider = &provider;
    dig.players[0].library = Library{};
    dig.players[0].library.push_back(BeZone("Questing Beast", 72));
    const int harbinger = BeAdd(dig, "Garruk's Harbinger", 71);
    FireCombatDamageDig(dig, 0, {harbinger}, {3});
    CHECK(ProviderSecondMainLive(dig));
    CHECK(dig.combat_hand[0] == 72);

    GameState prolif = BeState();
    prolif.m_provider = &provider;
    const int herald = BeAdd(prolif, "Herald of Ilharg", 81);
    AddPlusCounters(prolif.battlefield[static_cast<std::size_t>(herald)], 1);
    const int contam = BeAdd(prolif, "Bloated Contaminator", 82);
    FireCombatDamageProliferate(prolif, 0, {contam});
    CHECK(ProviderSecondMainLive(prolif));
}

TEST_CASE("Beasts provider folds same-name dig picks and values poison progress")
{
    BeastsProvider provider;
    GameState s = BeState();
    const std::vector<Card> seen = {BeZone("Questing Beast", 91), BeZone("Questing Beast", 92),
                                    BeZone("Craterhoof Behemoth", 93)};
    const std::vector<int> expected = {0, 2};
    CHECK(provider.EtbDigCandidates(s, 0, seen, {0, 1, 2}) == expected);
    CHECK(provider.NoWinLeafProgress(s) == 0);
    s.Opponent().poison_counters = 1;
    CHECK(provider.NoWinLeafProgress(s) > 0);
}

TEST_CASE("Beasts winless certificate declines on poison or any reachable action")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.deck_poison_hit_bound = 0;
    CHECK(provider.ProvenWinlessThisTurn(s, 0));
    s.Opponent().poison_counters = 1;
    CHECK_FALSE(provider.ProvenWinlessThisTurn(s, 0));
    s.Opponent().poison_counters = 0;
    s.deck_poison_hit_bound = 2;
    CHECK_FALSE(provider.ProvenWinlessThisTurn(s, 0));
    s.deck_poison_hit_bound = 0;
    s.players[0].hand.push_back(BeZone("Cragplate Baloth", 99));
    CHECK_FALSE(provider.ProvenWinlessThisTurn(s, 0));
}

TEST_CASE("Adaptive Omnitool consumes the shared combat dig rank before damage-step Harbinger")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    s.players[0].library = Library{};
    s.players[0].library.push_back(BeZone("Urza's Incubator", 101));
    s.players[0].library.push_back(BeZone("Adaptive Omnitool", 102));
    s.players[0].library.push_back(BeZone("Questing Beast", 103));
    const int attacker = BeAdd(s, "Questing Beast", 60);
    const int equip = BeAdd(s, "Adaptive Omnitool", 61);
    s.battlefield[static_cast<std::size_t>(equip)].equipped_to = 60;
    const int rank = 1;
    s.scripted_combat_dig = CombatDigPackRanks(&rank, 1);
    FireAttackEquipDigHand(s, 0, {attacker}, 1);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand.front().m_number == 102);
    CHECK(s.scripted_combat_dig == -1);

    GameState head = BeState();
    head.m_provider = &provider;
    head.deck_has_attack_dig_hand = true;
    head.players[0].lands_played_this_turn = 1;
    head.players[0].library = Library{};
    head.players[0].library.push_back(BeZone("Urza's Incubator", 101));
    head.players[0].library.push_back(BeZone("Adaptive Omnitool", 102));
    const int h = BeAdd(head, "Questing Beast", 60);
    const int e = BeAdd(head, "Adaptive Omnitool", 61);
    head.battlefield[static_cast<std::size_t>(e)].equipped_to = head.battlefield[static_cast<std::size_t>(h)].card.m_number;
    BeSearchedScope searched(true);
    // Exercise the combat pin fan on a real idle base plan. MainPlansBase can
    // omit idle plans from an empty-hand frame before this axis is reached.
    const auto plans = TurnSolver::SubdecisionAxesForTest(head, true, {TurnSolver::Plan{}});
    bool saw_second_rank = false;
    for (const auto& p : plans)
    { if (CombatDigRankAt(p.combat_dig_choice, 0) == 1) { saw_second_rank = true; break; } }
    CHECK(saw_second_rank);
}

// ---- Garruk's Packleader ------------------------------------------------------------------------

TEST_CASE("Garruk's Packleader: draws for another power-3+ creature of ours, not for a 2/2 Warchief")
{
    const CardDefinition& pl = BeDef("Garruk's Packleader");
    CHECK(pl.params.own_creature_enters_draw == 1);
    CHECK(pl.params.creature_enters_min_power == 3);
    CHECK_FALSE(pl.params.creature_enters_includes_self);
    {
        GameState s = BeState();
        BeAdd(s, "Garruk's Packleader", 60);
        BeEnter(s, "Questing Beast", 61);               // 4/4
        CHECK(s.players[0].hand.size() == 1);
        BeEnter(s, "Questing Beast", 62);               // no once-each-turn limit
        CHECK(s.players[0].hand.size() == 2);
        BeEnter(s, "Krosan Warchief", 63);              // 2/2: below the threshold
        CHECK(s.players[0].hand.size() == 2);
        BeEnter(s, "Questing Beast", 64, /*controller=*/1);   // not ours
        CHECK(s.players[0].hand.size() == 2);
    }
    {   // "Another": a Packleader entering does not draw off itself...
        GameState s = BeState();
        BeEnter(s, "Garruk's Packleader", 70);
        CHECK(s.players[0].hand.empty());
        // ...but a second Packleader entering draws off the first (and only the first).
        BeEnter(s, "Garruk's Packleader", 71);
        CHECK(s.players[0].hand.size() == 1);
    }
}

// ---- Krosan Warchief ----------------------------------------------------------------------------

TEST_CASE("Krosan Warchief: Beast spells cost {1} less, stacking per copy and with a Beast Incubator")
{
    // Craterhoof Behemoth {5}{G}{G}{G}: generic 5.
    REQUIRE(BeDef("Craterhoof Behemoth").card.m_mana_cost.generic == 5);
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    // Incubator chooses its type from the deck around it: a Beast library.
    for (const char* n : { "Questing Beast", "Garruk's Packleader", "Craterhoof Behemoth", "Herald of Ilharg", "Forest" })
    { s.players[0].library.push_back(BeDef(n).card); }
    CHECK(BeGeneric("Craterhoof Behemoth", s) == 5);
    CHECK(BeGeneric("Krosan Warchief", s) == 2);       // a lone Warchief in hand does not discount itself
    BeAdd(s, "Krosan Warchief", 80);
    CHECK(BeGeneric("Craterhoof Behemoth", s) == 4);
    CHECK(BeGeneric("Krosan Warchief", s) == 1);       // ...one on the battlefield discounts the next
    BeAdd(s, "Krosan Warchief", 81);
    CHECK(BeGeneric("Craterhoof Behemoth", s) == 3);
    {
        Permanent p;
        p.card = BeDef("Urza's Incubator").card;
        p.card.m_number = 82;
        p.controller_index = p.owner_index = 0;
        s.battlefield.push_back(p);
        FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
        CHECK(s.battlefield.back().chosen_subtype_id == SubtypeRegistry::Instance().Id("Beast"));
    }
    CHECK(BeGeneric("Craterhoof Behemoth", s) == 1);   // {1}{G}{G}{G}: 5 - 1 - 1 - 2
    CHECK(EffectiveSpellCost(BeDef("Craterhoof Behemoth"), s, 1).green == 3);   // pips untouched
    CHECK(BeGeneric("Questing Beast", s) == 0);         // {2}{G}{G} floors at {G}{G}
    // Not a Beast spell: no discount (Utopia Sprawl {G} has no generic; Wild Growth likewise -- use Incubator {3}).
    CHECK(BeGeneric("Urza's Incubator", s) == 3);
}

// ---- Questing Beast: the duplicate-legend offer --------------------------------------------------

TEST_CASE("Questing Beast: a second copy is offered when Herald's cast trigger or Packleader's draw pays")
{
    const CardDefinition& qb = BeDef("Questing Beast");
    REQUIRE(qb.card.HasSupertype(Supertype::Legendary));
    BeastsProvider provider;   // the Beasts route
    {   // Nothing pays: the duplicate is the tie the prune assumes, at every depth.
        GameState s = BeState();
        s.m_provider = &provider;
        BeAdd(s, "Questing Beast", 90);
        BeAdd(s, "Krosan Warchief", 91);
        BeSearchedScope on(true);
        CHECK_FALSE(provider.OfferDuplicateLegendCast(s, 0, qb));
    }
    {   // Herald of Ilharg: the CAST puts two counters on the resident Herald before the legend rule.
        GameState s = BeState();
        s.m_provider = &provider;
        BeAdd(s, "Questing Beast", 90);
        BeAdd(s, "Herald of Ilharg", 92);
        {
            BeSearchedScope on(true);
            CHECK(provider.OfferDuplicateLegendCast(s, 0, qb));
        }
        {   // Autonomous d0 keeps the prune (USER 2026-08-29 / 2026-11-18 ruling).
            BeSearchedScope off(false);
            CHECK_FALSE(provider.OfferDuplicateLegendCast(s, 0, qb));
        }
        // An OPPONENT's Herald does not trigger on our cast.
        s.battlefield.back().controller_index = 1;
        BeSearchedScope on(true);
        CHECK_FALSE(provider.OfferDuplicateLegendCast(s, 0, qb));
    }
    {   // Garruk's Packleader: the 4/4 duplicate ENTERS (draw) before the legend rule kills one.
        GameState s = BeState();
        s.m_provider = &provider;
        BeAdd(s, "Questing Beast", 90);
        BeAdd(s, "Garruk's Packleader", 93);
        BeSearchedScope on(true);
        CHECK(provider.OfferDuplicateLegendCast(s, 0, qb));
    }
    {   // ...and the draw really happens on a duplicate's entry (the legend rule is enforced after the cascade).
        GameState s = BeState();
        BeAdd(s, "Garruk's Packleader", 93);
        BeAdd(s, "Questing Beast", 90);
        BeEnter(s, "Questing Beast", 94);
        CHECK(s.players[0].hand.size() == 1);
    }
}

// ---- Thragtusk (ltb_self_creates_tokens) --------------------------------------------------------

namespace
{
int BeCountTokens(const GameState& s, const char* subtype, int controller = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller || !p.is_token) { continue; }
        for (const std::string& cs : p.card.m_subtypes) { if (cs == subtype) { ++n; break; } }
    }
    return n;
}
int BeIndexOf(const GameState& s, int number)
{
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { if (s.battlefield[i].card.m_number == number) { return static_cast<int>(i); } }
    return -1;
}
}   // namespace

TEST_CASE("Thragtusk: enters gaining 5; leaving (a death) leaves a 3/3 green Beast token")
{
    const CardDefinition& tt = BeDef("Thragtusk");
    CHECK(tt.params.ltb_self_creates_tokens == 1);
    CHECK(tt.params.ltb_token_subtypes == std::vector<std::string>{ "Beast" });
    GameState s = BeState();
    s.players[0].life = 20;
    BeEnter(s, "Thragtusk", 50);
    CHECK(s.players[0].life == 25);
    CHECK(BeCountTokens(s, "Beast") == 0);
    CHECK(SelfSacHasDeathPayoff(s, 0, 50));          // its own leave pays
    BeAdd(s, "Questing Beast", 51);
    CHECK_FALSE(SelfSacHasDeathPayoff(s, 0, 51));    // a plain body's does not
    SacrificePermanentAt(s, 0, BeIndexOf(s, 50));
    CHECK(BeIndexOf(s, 50) < 0);
    REQUIRE(BeCountTokens(s, "Beast") == 1);
    const Permanent& tok = s.battlefield.back();
    CHECK(tok.is_token);
    CHECK(tok.card.m_power.value_or(0) == 3);
    CHECK(tok.card.m_toughness.value_or(0) == 3);
    CHECK(tok.card.HasColor(Color::Green));
    CHECK(tok.card.IsCreature());
}

TEST_CASE("Thragtusk: the LTB token enters through the cascade (Packleader draws), and a body without it makes none")
{
    {
        GameState s = BeState();
        BeAdd(s, "Garruk's Packleader", 60);
        BeAdd(s, "Thragtusk", 61);
        SacrificePermanentAt(s, 0, BeIndexOf(s, 61));
        CHECK(BeCountTokens(s, "Beast") == 1);
        CHECK(s.players[0].hand.size() == 1);       // the 3/3 Beast is power 3
    }
    {   // byte-identity guard: a creature without the param leaves nothing behind
        GameState s = BeState();
        BeAdd(s, "Questing Beast", 62);
        SacrificePermanentAt(s, 0, BeIndexOf(s, 62));
        CHECK(BeCountTokens(s, "Beast") == 0);
    }
}

// ---- Cragplate Baloth (kicked_enters_with_plus_counters) -----------------------------------------

namespace
{
GameState BalothBoard()
{
    GameState s = BeState();
    for (int k = 0; k < 10; ++k) { BeAdd(s, "Forest", 300 + k); }
    Card c = BeDef("Cragplate Baloth").card;
    c.m_number = 40;
    s.players[0].hand.push_back(c);
    return s;
}
const Permanent* BeBaloth(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 40) { return &p; } }
    return nullptr;
}
}   // namespace

TEST_CASE("Cragplate Baloth: the kicked face carries the counters; the printed card is what enters")
{
    const CardDefinition& base = BeDef("Cragplate Baloth");
    const CardDefinition& kick = BeDef("Cragplate Baloth (Kicked)");
    CHECK(base.params.kicked_enters_with_plus_counters == 4);
    CHECK(kick.params.kicked_base_name == "Cragplate Baloth");
    CHECK(kick.card.m_mana_cost.generic == 7);
    CHECK(kick.card.m_mana_cost.green == 3);
    CHECK(PrintedSpellManaValue(base) == 7);
    CHECK(PrintedSpellManaValue(kick) == 7);   // paid 10, printed MV 7 (CR 202.3)
    CHECK(base.card.HasKeyword(Keyword::Haste));
}

TEST_CASE("Cragplate Baloth: executor site -- kicked enters with four +1/+1 counters, unkicked with none")
{
    for (int kicked = 0; kicked < 2; ++kicked)
    {
        GameState s = BeState();
        BeAdd(s, "Garruk's Packleader", 60);
        const CardDefinition& def = BeDef(kicked ? "Cragplate Baloth (Kicked)" : "Cragplate Baloth");
        StackEntry e;
        e.type = StackEntry::EntryType::Spell;
        e.source = def.card;
        e.source.m_number = 40;
        e.controller_index = 0;
        EffectHandler::Resolve(s, e, def);
        const Permanent* b = BeBaloth(s);
        REQUIRE(b != nullptr);
        CHECK(b->card.m_name.str() == "Cragplate Baloth");     // the printed card, never the face
        CHECK(PlusCountersOn(*b) == (kicked ? 4 : 0));
        CHECK(s.players[0].hand.size() == 1);                  // Packleader: a 6/6 or a 10/10 draws either way
    }
}

TEST_CASE("Cragplate Baloth: rollout site -- the kicked plan variant enters as a 10/10, the unkicked as a 6/6")
{
    BeastsProvider provider;
    GameState s = BalothBoard();
    s.m_provider = &provider;
    BeSearchedScope on(true);
    bool saw_kicked = false, saw_plain = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* cast = nullptr;
        for (const Action& a : plan.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == "Cragplate Baloth") { cast = &a; } }
        if (cast == nullptr) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, plan, true);
        const Permanent* b = BeBaloth(t);
        REQUIRE(b != nullptr);
        CHECK(b->card.m_name.str() == "Cragplate Baloth");
        if (cast->kicked) { saw_kicked = true; CHECK(PlusCountersOn(*b) == 4); }
        else              { saw_plain  = true; CHECK(PlusCountersOn(*b) == 0); }
    }
    CHECK(saw_kicked);
    CHECK(saw_plain);
}

// ---- Kaheera, the Orphanguard (lord + grants_vigilance) ------------------------------------------

TEST_CASE("Kaheera: pumps and grants vigilance to another Beast (tokens included), never to itself")
{
    GameState s = BeState();
    const int k = BeAdd(s, "Kaheera, the Orphanguard", 70);
    CreateToken(s, 0, 3, 3, { "Beast" }, "G");
    const int tok = static_cast<int>(s.battlefield.size()) - 1;
    CreateToken(s, 0, 6, 6, { "Wurm" }, "G");
    const int wurm = static_cast<int>(s.battlefield.size()) - 1;
    CreateToken(s, 1, 3, 3, { "Beast" }, "G");
    const int opp = static_cast<int>(s.battlefield.size()) - 1;
    auto bonus = [&](int i)
    {
        const Permanent& p = s.battlefield[static_cast<std::size_t>(i)];
        return ComputeLordBonus(p.card, s, p.controller_index, false, &p);
    };
    CHECK(bonus(tok) == std::pair<int, int>{ 1, 1 });
    CHECK(bonus(wurm) == std::pair<int, int>{ 0, 0 });
    CHECK(bonus(opp) == std::pair<int, int>{ 0, 0 });
    CHECK(bonus(k) == std::pair<int, int>{ 0, 0 });            // "each OTHER creature"
    CHECK(CreatureHasVigilance(s.battlefield[tok], s));
    CHECK_FALSE(CreatureHasVigilance(s.battlefield[wurm], s));
    CHECK_FALSE(CreatureHasVigilance(s.battlefield[opp], s));
    // Through the combat core's prefilter too (BoardSources::vigilance must admit the lord).
    const BoardSources bs = GatherBoardSources(s.battlefield, 0);
    CHECK(CreatureHasVigilance(s.battlefield[tok], s, &bs.vigilance));
    // Kaheera's own vigilance is the PRINTED keyword: strip it and the grant does not reach her.
    s.battlefield[static_cast<std::size_t>(k)].card.RemoveKeyword(Keyword::Vigilance);
    CHECK_FALSE(CreatureHasVigilance(s.battlefield[k], s));
    // No Kaheera: the Beast token has no vigilance.
    GameState t = BeState();
    CreateToken(t, 0, 3, 3, { "Beast" }, "G");
    CHECK_FALSE(CreatureHasVigilance(t.battlefield.back(), t));
}

TEST_CASE("Kaheera: a Krosan Warchief entering under her is a 3/3, so Garruk's Packleader draws")
{
    GameState s = BeState();
    BeAdd(s, "Garruk's Packleader", 60);
    BeEnter(s, "Krosan Warchief", 61);
    CHECK(s.players[0].hand.empty());
    BeAdd(s, "Kaheera, the Orphanguard", 70);
    BeEnter(s, "Krosan Warchief", 62);
    CHECK(s.players[0].hand.size() == 1);
}

// ---- Contested Cliffs (PermAbilityMode::TapFight; Beasts B) ---------------------------------------

namespace
{
// A goldfish spawn exactly as GameEngine materialises one (an "N/M Creature", opponent's, not a token).
int BeSpawn(GameState& s, int power, int toughness, int number)
{
    Card token;
    token.m_name = std::to_string(power) + "/" + std::to_string(toughness) + " Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power = power;
    token.m_toughness = toughness;
    token.m_number = number;
    Permanent perm;
    perm.card = token;
    perm.controller_index = perm.owner_index = 1;
    s.battlefield.push_back(perm);
    return static_cast<int>(s.battlefield.size()) - 1;
}
// Fight through the SHARED resolver both worlds call (ApplyPermAbility under the pin).
void BeCliffsFight(GameState& s, int cliffs, int own, int opp)
{
    const TapFightPinScope pin(own, opp);
    REQUIRE(PermAbilitySourceLive(s, 0, cliffs, PermAbilityMode::TapFight, nullptr));
    ApplyPermAbility(s, 0, cliffs, PermAbilityMode::TapFight);
}
// Every (own, opp) pair the enumerator offers as a TapFight action.
std::vector<std::pair<int, int>> BeFightPairs(const GameState& s, bool pre_combat = true)
{
    std::vector<std::pair<int, int>> out;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(s, pre_combat))
    {
        for (const Action& a : plan.actions)
        {
            if (a.kind != Action::Kind::ActivatePermAbility || a.ability_mode != Action::AbilityMode::TapFight) { continue; }
            const std::pair<int, int> pr{ a.sac_victim_id, a.enchant_target };
            if (std::find(out.begin(), out.end(), pr) == out.end()) { out.push_back(pr); }
        }
    }
    return out;
}
GameState BeCliffsBoard()
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    BeAdd(s, "Contested Cliffs", 70);
    BeAdd(s, "Mountain", 71);
    BeAdd(s, "Forest", 72);
    return s;
}
}   // namespace

TEST_CASE("Contested Cliffs: card data -- a {C} land with an {R}{G}, {T} Beast fight")
{
    const CardDefinition& cc = BeDef("Contested Cliffs");
    CHECK(cc.card.IsLand());
    REQUIRE(cc.params.tap_fight_cost.has_value());
    CHECK(cc.params.tap_fight_cost->ManaValue() == 2);
    CHECK(cc.params.tap_fight_requires_subtype == "Beast");
    CHECK(PermAbilityTaps(PermAbilityMode::TapFight));
}

TEST_CASE("Contested Cliffs: a plain fight -- simultaneous damage at power, lethal dies, the survivor keeps damage")
{
    GameState s = BeCliffsBoard();
    BeAdd(s, "Krosan Warchief", 80);   // 2/2 Beast
    BeSpawn(s, 6, 6, 9001);
    BeCliffsFight(s, 70, 80, 9001);
    CHECK(BeIndexOf(s, 80) < 0);                       // took 6
    const int sp = BeIndexOf(s, 9001);
    REQUIRE(sp >= 0);
    CHECK(s.battlefield[static_cast<std::size_t>(sp)].damage == 2);
    CHECK(s.players[0].graveyard.size() == 1);
    CHECK(s.players[1].graveyard.empty());
}

TEST_CASE("Contested Cliffs: deathtouch -- Questing Beast's 4 kills a 6/6, and both die simultaneously")
{
    CHECK(BeDef("Questing Beast").card.HasKeyword(Keyword::Deathtouch));
    GameState s = BeCliffsBoard();
    BeAdd(s, "Questing Beast", 81);    // 4/4 deathtouch
    BeSpawn(s, 6, 6, 9002);
    BeCliffsFight(s, 70, 81, 9002);
    CHECK(BeIndexOf(s, 9002) < 0);     // 4 < 6 toughness, but deathtouch
    CHECK(BeIndexOf(s, 81) < 0);       // took 6
    CHECK(s.players[1].graveyard.size() == 1);
    {   // ...and a 0-power hit is not a deathtouch kill (CR 702.2b needs damage dealt)
        GameState t = BeCliffsBoard();
        BeAdd(t, "Questing Beast", 82);
        BeSpawn(t, 0, 3, 9003);
        BeCliffsFight(t, 70, 82, 9003);
        CHECK(BeIndexOf(t, 9003) < 0);             // QB's 4 >= 3: dies to plain lethal damage
        CHECK(BeIndexOf(t, 82) >= 0);              // the 0-power spawn dealt nothing
        CHECK(t.battlefield[static_cast<std::size_t>(BeIndexOf(t, 82))].damage == 0);
    }
}

TEST_CASE("Contested Cliffs: Thragtusk dies in the fight -> its 3/3 Beast, which Garruk's Packleader draws off")
{
    GameState s = BeCliffsBoard();
    BeAdd(s, "Garruk's Packleader", 83);
    BeAdd(s, "Thragtusk", 84);          // 5/3
    BeSpawn(s, 4, 4, 9004);
    BeCliffsFight(s, 70, 84, 9004);
    CHECK(BeIndexOf(s, 84) < 0);        // took 4 >= 3
    CHECK(BeIndexOf(s, 9004) < 0);      // took 5 >= 4
    CHECK(BeCountTokens(s, "Beast") == 1);
    CHECK(s.players[0].hand.size() == 1);
}

TEST_CASE("Contested Cliffs: no activation without BOTH targets, a tapped source, or the mana")
{
    {   // no opponent creature
        GameState s = BeCliffsBoard();
        BeAdd(s, "Krosan Warchief", 85);
        CHECK_FALSE(PermAbilitySourceLive(s, 0, 70, PermAbilityMode::TapFight, nullptr));
        CHECK(BeFightPairs(s).empty());
    }
    {   // no Beast of ours (the opponent's creature is not ours; a land is no creature)
        GameState s = BeCliffsBoard();
        BeSpawn(s, 2, 2, 9005);
        CHECK_FALSE(PermAbilitySourceLive(s, 0, 70, PermAbilityMode::TapFight, nullptr));
        CHECK(BeFightPairs(s).empty());
    }
    {   // a stale pin (the pinned spawn is gone) is a clean no-op
        GameState s = BeCliffsBoard();
        BeAdd(s, "Krosan Warchief", 86);
        BeSpawn(s, 2, 2, 9006);
        const TapFightPinScope pin(86, 9999);
        CHECK_FALSE(PermAbilitySourceLive(s, 0, 70, PermAbilityMode::TapFight, nullptr));
    }
    {   // only the Cliffs' own {C}: {R}{G} must come from other sources
        GameState s = BeState();
        s.players[0].lands_played_this_turn = 1;
        BeAdd(s, "Contested Cliffs", 70);
        BeAdd(s, "Krosan Warchief", 87);
        BeSpawn(s, 2, 2, 9007);
        CHECK(BeFightPairs(s).empty());
    }
}

TEST_CASE("Contested Cliffs: both targets are searched -- one variant per (own class x opponent class)")
{
    BeastsProvider provider;   // the Beasts route; its TapFightTargets is the base (all)
    GameState s = BeCliffsBoard();
    s.m_provider = &provider;
    BeSearchedScope on(true);
    BeAdd(s, "Krosan Warchief", 90);
    BeAdd(s, "Krosan Warchief", 91);    // identical to 90: one class
    BeAdd(s, "Thragtusk", 92);
    BeSpawn(s, 1, 1, 9010);
    BeSpawn(s, 1, 1, 9011);             // identical to 9010: one class
    BeSpawn(s, 4, 4, 9012);
    const auto pairs = BeFightPairs(s);
    CHECK(pairs.size() == 4);
    auto has = [&](int o, int t) { return std::find(pairs.begin(), pairs.end(), std::make_pair(o, t)) != pairs.end(); };
    CHECK(has(90, 9010));
    CHECK(has(90, 9012));
    CHECK(has(92, 9010));
    CHECK(has(92, 9012));
    CHECK(BeFightPairs(s, /*pre_combat=*/false).size() == 4);   // the same menu in a main 2
    // Applied through the plan route (the rollout's trailing pass): the pinned pair fights, Cliffs + mana tapped.
    bool applied = false;
    for (const TurnSolver::Plan& plan : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (plan.actions.size() != 1 || plan.actions[0].ability_mode != Action::AbilityMode::TapFight
            || plan.actions[0].sac_victim_id != 92 || plan.actions[0].enchant_target != 9012) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, plan, true);
        CHECK(BeIndexOf(t, 92) < 0);           // Thragtusk took 4
        CHECK(BeIndexOf(t, 9012) < 0);         // the 4/4 took 5
        CHECK(BeIndexOf(t, 9010) >= 0);        // the others untouched
        CHECK(BeCountTokens(t, "Beast") == 1); // Thragtusk's LTB
        CHECK(t.battlefield[static_cast<std::size_t>(BeIndexOf(t, 70))].tapped);
        CHECK(t.battlefield[static_cast<std::size_t>(BeIndexOf(t, 71))].tapped);
        CHECK(t.battlefield[static_cast<std::size_t>(BeIndexOf(t, 72))].tapped);
        applied = true;
        break;
    }
    CHECK(applied);
    CHECK(g_tap_fight_unpinned.load() == 0);
}

// ---- Elder Gargaroth (batch C) -----------------------------------------------------------------
// "Whenever this creature attacks or blocks, choose one -- * Create a 3/3 green Beast creature token. * You gain
// 3 life. * Draw a card." The shared ApplyAttackChooseOneTriggers (both worlds), the searched pin packed 2 bits per
// ordinal, the Tectonic-shaped mode axis and the provider's life-mode fold.
namespace
{
constexpr const char* kGarg = "Elder Gargaroth";
GameState BeGargBoard()
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.deck_has_attack_choose_one = true;
    return s;
}
int BePinOf(std::initializer_list<int> modes)
{
    int m[kAttackChooseMaxOrdinals];
    int n = 0;
    for (int x : modes) { m[n++] = x; }
    return AttackChoosePackModes(m, n);
}
// Fire the shared trigger over the Gargaroths on the board (declared attackers = every Gargaroth).
void BeFireGarg(GameState& s, int pin)
{
    s.scripted_attack_choose_mode = static_cast<std::int16_t>(pin);
    std::vector<int> atk;
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { if (s.battlefield[i].card.m_name == kGarg) { atk.push_back(static_cast<int>(i)); } }
    ApplyAttackChooseOneTriggers(s, 0, atk);
}
std::set<int> BeGargCodes(const GameState& s)
{
    TurnSolver::Plan base;
    base.land_decided = true;
    std::set<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
    { if (p.attack_choose_mode_choice >= 0) { out.insert(p.attack_choose_mode_choice); } }
    return out;
}
bool BeCodeUsesMode(int packed, int mode)
{
    for (int k = 0; k < kAttackChooseMaxOrdinals; ++k) { if (AttackChooseModePinAt(packed, k) == mode) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("Elder Gargaroth: card data -- 6/6 vigilance reach trample, its OWN modal attack trigger (not Tectonic's)")
{
    const CardDefinition& d = BeDef(kGarg);
    CHECK(d.card.m_power.value_or(0) == 6);
    CHECK(d.card.m_toughness.value_or(0) == 6);
    CHECK(d.card.HasKeyword(Keyword::Vigilance));
    CHECK(d.card.HasKeyword(Keyword::Trample));
    CHECK(d.params.attack_choose_one);
    CHECK_FALSE(d.params.attack_trigger_modal);   // the Giants signature stays off
    CHECK(d.params.attack_choose_token_power == 3);
    CHECK(d.params.attack_choose_token_toughness == 3);
    CHECK(d.params.attack_choose_lifegain == 3);
    CHECK(d.params.attack_choose_draw == 1);
    std::vector<int> legal;
    AttackChooseOneLegalModes(d.params, legal);
    CHECK(legal == std::vector<int>{ 0, 1, 2 });
}

TEST_CASE("Elder Gargaroth: each mode resolves through the shared trigger (token / 3 life / a real draw)")
{
    {   // token: a 3/3 green Beast, NOT attacking
        GameState s = BeGargBoard();
        BeAdd(s, kGarg, 50);
        BeFireGarg(s, BePinOf({ 0 }));
        CHECK(BeCountTokens(s, "Beast") == 1);
        for (const Permanent& p : s.battlefield)
        {
            if (!p.is_token) { continue; }
            CHECK(p.card.m_power.value_or(0) == 3);
            CHECK(p.card.m_toughness.value_or(0) == 3);
        }
        CHECK(s.players[0].life == 20);
        CHECK(s.players[0].hand.empty());
        CHECK(s.scripted_attack_choose_mode == -1);
    }
    {   // life
        GameState s = BeGargBoard();
        BeAdd(s, kGarg, 50);
        BeFireGarg(s, BePinOf({ 1 }));
        CHECK(s.players[0].life == 23);
        CHECK(BeCountTokens(s, "Beast") == 0);
        CHECK(s.players[0].hand.empty());
    }
    {   // draw
        GameState s = BeGargBoard();
        BeAdd(s, kGarg, 50);
        const std::size_t lib = s.players[0].library.size();
        BeFireGarg(s, BePinOf({ 2 }));
        CHECK(s.players[0].hand.size() == 1);
        CHECK(s.players[0].library.size() == lib - 1);
        CHECK(s.players[0].cards_drawn_this_turn == 1);
        CHECK(BeCountTokens(s, "Beast") == 0);
        CHECK(s.players[0].life == 20);
    }
    {   // draw off an empty library loses (a real draw)
        GameState s = BeGargBoard();
        while (!s.players[0].library.empty()) { s.players[0].library.DrawTop(); }
        BeAdd(s, kGarg, 50);
        BeFireGarg(s, BePinOf({ 2 }));
        CHECK(s.player_lost_on_draw);
    }
    {   // unpinned: the provider's front -- the token
        GameState s = BeGargBoard();
        BeAdd(s, kGarg, 50);
        BeFireGarg(s, -1);
        CHECK(BeCountTokens(s, "Beast") == 1);
    }
}

TEST_CASE("Elder Gargaroth: rollout combat -- the pinned mode resolves at declare-attackers, the 6/6 connects")
{
    for (int mode = 0; mode < 3; ++mode)
    {
        GameState s = BeGargBoard();
        s.phase = Phase::Combat;
        BeAdd(s, kGarg, 50);
        s.scripted_attack_choose_mode = static_cast<std::int16_t>(BePinOf({ mode }));
        const std::size_t lib = s.players[0].library.size();
        RolloutSimulateCombat(s);
        CAPTURE(mode);
        CHECK(s.players[1].life == 14);                                   // the 6/6 (the token does not attack)
        CHECK(BeCountTokens(s, "Beast") == (mode == 0 ? 1 : 0));
        CHECK(s.players[0].life == (mode == 1 ? 23 : 20));
        CHECK(s.players[0].library.size() == lib - (mode == 2 ? 1u : 0u));
        CHECK(s.scripted_attack_choose_mode == -1);                       // spent
        CHECK_FALSE(s.battlefield[static_cast<std::size_t>(BeIndexOf(s, 50))].tapped);   // vigilance
    }
}

TEST_CASE("Elder Gargaroth: executor combat -- the same modes through GameEngine::CombatPhase")
{
    for (int mode = 0; mode < 3; ++mode)
    {
        GameState ex = BeGargBoard();
        BeAdd(ex, kGarg, 50);
        ex.scripted_attack_choose_mode = static_cast<std::int16_t>(BePinOf({ mode }));
        ex.phase = Phase::Combat;
        const std::size_t lib = ex.players[0].library.size();
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::Combat);
        CAPTURE(mode);
        CHECK(ex.players[1].life == 14);
        CHECK(BeCountTokens(ex, "Beast") == (mode == 0 ? 1 : 0));
        CHECK(ex.players[0].life == (mode == 1 ? 23 : 20));
        CHECK(ex.players[0].library.size() == lib - (mode == 2 ? 1u : 0u));
        CHECK(ex.scripted_attack_choose_mode == -1);
    }
}

TEST_CASE("Elder Gargaroth: the token mode's 3/3 Beast enters through the cascade -- Garruk's Packleader draws")
{
    GameState s = BeGargBoard();
    BeAdd(s, kGarg, 50);
    BeAdd(s, "Garruk's Packleader", 60);
    BeFireGarg(s, BePinOf({ 0 }));
    CHECK(BeCountTokens(s, "Beast") == 1);
    CHECK(s.players[0].hand.size() == 1);   // the power-3 token's Packleader draw
    // ...and the life mode makes no body, so no draw.
    GameState t = BeGargBoard();
    BeAdd(t, kGarg, 50);
    BeAdd(t, "Garruk's Packleader", 60);
    BeFireGarg(t, BePinOf({ 1 }));
    CHECK(t.players[0].hand.empty());
}

TEST_CASE("Elder Gargaroth: the pin is read per ordinal, spent after the loop, and never leaks to a later trigger")
{
    GameState s = BeGargBoard();
    BeAdd(s, kGarg, 50);
    BeAdd(s, kGarg, 51);
    // ordinal 0 draws, ordinal 1 makes the token.
    BeFireGarg(s, BePinOf({ 2, 0 }));
    CHECK(s.players[0].hand.size() == 1);
    CHECK(BeCountTokens(s, "Beast") == 1);
    CHECK(s.scripted_attack_choose_mode == -1);
    // Only ordinal 1 pinned (life): ordinal 0 takes the default (token).
    GameState t = BeGargBoard();
    BeAdd(t, kGarg, 50);
    BeAdd(t, kGarg, 51);
    BeFireGarg(t, BePinOf({ -1, 1 }));
    CHECK(BeCountTokens(t, "Beast") == 1);
    CHECK(t.players[0].life == 23);
    CHECK(t.players[0].hand.empty());
    // A second combat's triggers with nothing re-pinned take the default -- the spent pin does not leak.
    std::vector<int> atk = { BeIndexOf(t, 50), BeIndexOf(t, 51) };
    ApplyAttackChooseOneTriggers(t, 0, atk);
    CHECK(BeCountTokens(t, "Beast") == 3);
    CHECK(t.players[0].life == 23);
    // An unfired pin (no Gargaroth attacked) is spent too.
    GameState u = BeGargBoard();
    BeAdd(u, kGarg, 50);
    u.scripted_attack_choose_mode = static_cast<std::int16_t>(BePinOf({ 2 }));
    ApplyAttackChooseOneTriggers(u, 0, {});
    CHECK(u.scripted_attack_choose_mode == -1);
    CHECK(u.players[0].hand.empty());
    // Packing round trip.
    const int packed = BePinOf({ 0, 2, -1, 1 });
    CHECK(AttackChooseModePinAt(packed, 0) == 0);
    CHECK(AttackChooseModePinAt(packed, 1) == 2);
    CHECK(AttackChooseModePinAt(packed, 2) == -1);
    CHECK(AttackChooseModePinAt(packed, 3) == 1);
    CHECK(BePinOf({ -1, -1 }) == -1);
}

TEST_CASE("Elder Gargaroth: the provider fold drops ONLY the life mode, and only when our life is unobservable")
{
    GameState s = BeGargBoard();
    auto run = [&](const GameState& g, bool unpruned) {
        std::vector<int> m = { 0, 1, 2 };
        RestrictAttackChooseOneModesFor(g, m, unpruned);
        return m;
    };
    s.deck_life_inert = true;
    s.own_death_live = false;
    CHECK(run(s, false) == std::vector<int>{ 0, 2 });          // token front, draw; life folded
    CHECK(run(s, true)  == std::vector<int>{ 0, 2, 1 });       // MTG_UNPRUNE=attackchooselife
    GameState t = s; t.deck_life_inert = false;
    CHECK(run(t, false) == std::vector<int>{ 0, 2, 1 });       // a life reader in the deck keeps it
    GameState u = s; u.own_death_live = true;
    CHECK(run(u, false) == std::vector<int>{ 0, 2, 1 });       // our life can lose the game: keep it
    {   // no token mode on the menu: nothing dominates life, keep it
        std::vector<int> m = { 1, 2 };
        RestrictAttackChooseOneModesFor(s, m, false);
        CHECK(m == std::vector<int>{ 2, 1 });
    }
    // Through the virtual (the deck's current route inherits the root).
    BeastsProvider provider;
    s.m_provider = &provider;
    std::vector<int> modes;
    AttackChooseOneSearchModes(s, 0, BeDef(kGarg), modes);
    CHECK(modes == std::vector<int>{ 0, 2 });
}

TEST_CASE("Elder Gargaroth: the mode axis -- two attackers make 4 joint variants after the fold, 9 without it")
{
    BeastsProvider provider;
    GameState s = BeGargBoard();
    s.m_provider = &provider;
    s.deck_life_inert = true;
    BeAdd(s, kGarg, 50);
    BeAdd(s, kGarg, 51);
    const std::set<int> codes = BeGargCodes(s);
    CHECK(codes.size() == 4);
    for (int c : codes)
    {
        CHECK_FALSE(BeCodeUsesMode(c, 1));                      // no life mode in the search
        CHECK(AttackChooseModePinAt(c, 0) >= 0);
        CHECK(AttackChooseModePinAt(c, 1) >= 0);
        CHECK(AttackChooseModePinAt(c, 2) == -1);
    }
    CHECK(codes.count(BePinOf({ 0, 0 })) == 1);
    CHECK(codes.count(BePinOf({ 2, 2 })) == 1);
    CHECK(codes.count(BePinOf({ 0, 2 })) == 1);
    CHECK(codes.count(BePinOf({ 2, 0 })) == 1);
    GameState t = s;
    t.deck_life_inert = false;                                   // a deck that reads life: all three modes
    CHECK(BeGargCodes(t).size() == 9);
    // A summoning-sick copy cannot attack: no ordinal, no variants.
    GameState u = BeGargBoard();
    u.m_provider = &provider;
    u.deck_life_inert = true;
    BeAdd(u, kGarg, 50, 0, /*sick=*/true);
    CHECK(BeGargCodes(u).empty());
}

// ---- Garruk's Harbinger (batch D) --------------------------------------------------------------
// "Whenever this creature deals combat damage to a player or planeswalker, look at that many cards from the top of
// your library. You may reveal a creature card or Garruk planeswalker card from among them and put it into your
// hand. Put the rest on the bottom of your library in a random order." The shared FireCombatDamageDig (both combat
// worlds), the searched rank pin packed 4 bits per ordinal, and the main-phase dig axis.
namespace
{
constexpr const char* kHarb = "Garruk's Harbinger";
// A Harbinger board whose library is exactly `names` (top first), numbered 300 + position.
GameState BeHarbBoard(std::initializer_list<const char*> names)
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.deck_has_combat_damage_dig = true;
    s.players[0].library = Library();
    int n = 0;
    for (const char* nm : names) { s.players[0].library.push_back(BeZone(nm, 300 + n++)); }
    return s;
}
std::vector<int> BeLibNums(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].library) { v.push_back(c.m_number); }
    return v;
}
std::vector<int> BeHandNums(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].hand) { v.push_back(c.m_number); }
    return v;
}
int BeDigPin(std::initializer_list<int> ranks)
{
    int r[kCombatDigMaxOrdinals];
    int n = 0;
    for (int x : ranks) { r[n++] = x; }
    return CombatDigPackRanks(r, n);
}
// Fire the shared trigger directly: every Harbinger on the board connected for `dmg`.
void BeFireHarb(GameState& s, int pin, int dmg)
{
    s.scripted_combat_dig = static_cast<std::int16_t>(pin);
    std::vector<int> idx, pw;
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { if (s.battlefield[i].card.m_name == kHarb) { idx.push_back(static_cast<int>(i)); pw.push_back(dmg); } }
    FireCombatDamageDig(s, 0, idx, pw);
}
std::set<int> BeHarbCodes(const GameState& s)
{
    TurnSolver::Plan base;
    base.land_decided = true;
    std::set<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
    { if (p.combat_dig_choice >= 0) { out.insert(p.combat_dig_choice); } }
    return out;
}
}   // namespace

TEST_CASE("Garruk's Harbinger: card data, and the dig looks at exactly the damage dealt (Kaheera's anthem included)")
{
    const CardDefinition& d = BeDef(kHarb);
    CHECK(d.card.m_power.value_or(0) == 4);
    CHECK(d.card.m_toughness.value_or(0) == 3);
    CHECK(d.params.combat_damage_dig_per_damage == 1);
    CHECK(d.params.combat_damage_dig_types == std::vector<std::string>{ "Creature", "Garruk" });
    // The only creature is 5th from the top: a 4-power hit misses it, a 5-power hit (Kaheera's +1/+1) finds it.
    for (int with_kaheera = 0; with_kaheera < 2; ++with_kaheera)
    {
        GameState s = BeHarbBoard({ "Forest", "Forest", "Forest", "Forest", "Thragtusk", "Forest", "Forest" });
        s.phase = Phase::Combat;
        BeAdd(s, kHarb, 50);
        if (with_kaheera != 0) { BeAdd(s, "Kaheera, the Orphanguard", 70); }
        RolloutSimulateCombat(s);
        CAPTURE(with_kaheera);
        if (with_kaheera == 0)
        {
            CHECK(s.players[1].life == 16);
            CHECK(s.players[0].hand.empty());
            CHECK(s.players[0].library.size() == 7);
            // The four looked-at Forests went under the two unseen cards: Thragtusk is now on top.
            CHECK(s.players[0].library.front().m_number == 304);
        }
        else
        {
            CHECK(s.players[1].life == 20 - 5 - 3);                    // Harbinger 5 (anthem) + Kaheera 3
            CHECK(BeHandNums(s) == std::vector<int>{ 304 });          // Thragtusk dug
            CHECK(s.players[0].library.size() == 6);
            CHECK(s.players[0].library.front().m_number == 305);      // the unseen rest stays on top
        }
    }
}

TEST_CASE("Garruk's Harbinger: the filter takes a creature or a Garruk planeswalker, never another nonland")
{
    GameState s = BeHarbBoard({ "Utopia Sprawl", "Forest", "Garruk, Primal Hunter", "Forest", "Thragtusk" });
    BeAdd(s, kHarb, 50);
    BeFireHarb(s, -1, 4);
    CHECK(BeHandNums(s) == std::vector<int>{ 302 });                  // Garruk; Sprawl is not legal
    CHECK(s.players[0].library.size() == 4);
    CHECK(s.players[0].library.front().m_number == 304);              // Thragtusk was not looked at
    // A window holding only an Aura and lands takes nothing; all of it goes to the bottom.
    GameState t = BeHarbBoard({ "Utopia Sprawl", "Forest", "Wild Growth", "Forest", "Thragtusk" });
    BeAdd(t, kHarb, 50);
    BeFireHarb(t, -1, 4);
    CHECK(t.players[0].hand.empty());
    CHECK(t.players[0].library.size() == 5);
    CHECK(t.players[0].library.front().m_number == 304);
}

TEST_CASE("Garruk's Harbinger: the rest go to the bottom in the shared keyed random order")
{
    const auto make = [] {
        GameState s = BeHarbBoard({ "Forest", "Mountain", "Thragtusk", "Forest", "Mountain", "Forest", "Questing Beast" });
        s.game_seed = 12345;
        BeAdd(s, kHarb, 50);
        return s;
    };
    GameState s = make();
    const int sc = s.search_count;
    BeFireHarb(s, -1, 6);
    CHECK(BeHandNums(s) == std::vector<int>{ 302 });                  // the provider's front (first match)
    CHECK(s.search_count == sc + 1);                                   // the keyed order consumed one ordinal
    const std::vector<int> lib = BeLibNums(s);
    REQUIRE(lib.size() == 6);
    CHECK(lib[0] == 306);                                              // the unseen card stays on top
    std::vector<int> bottom(lib.begin() + 1, lib.end());
    std::vector<int> sorted_bottom = bottom;
    std::sort(sorted_bottom.begin(), sorted_bottom.end());
    CHECK(sorted_bottom == std::vector<int>{ 300, 301, 303, 304, 305 });
    // ...and it IS BottomInRandomOrder's order, the one every world shares (not look order).
    GameState ref = make();
    std::vector<Card> rest;
    for (int i = 0; i < 6; ++i)
    {
        Card c = ref.players[0].library.front();
        ref.players[0].library.DrawTop();
        if (i != 2) { rest.push_back(c); }
    }
    BottomInRandomOrder(ref, 0, std::move(rest));
    CHECK(BeLibNums(ref) == lib);
    // Determinism: the same state, the same order.
    GameState again = make();
    BeFireHarb(again, -1, 6);
    CHECK(BeLibNums(again) == lib);
}

TEST_CASE("Garruk's Harbinger: executor and rollout combat resolve the same pinned dig identically")
{
    for (int rank = 0; rank < 3; ++rank)
    {
        const auto make = [&] {
            GameState s = BeHarbBoard({ "Thragtusk", "Forest", "Questing Beast", "Elder Gargaroth", "Forest", "Forest" });
            s.game_seed = 777;
            BeAdd(s, kHarb, 50);
            s.scripted_combat_dig = static_cast<std::int16_t>(BeDigPin({ rank }));
            s.phase = Phase::Combat;
            return s;
        };
        GameState ro = make();
        RolloutSimulateCombat(ro);
        GameState ex = make();
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::Combat);
        CAPTURE(rank);
        const int want = rank == 0 ? 300 : rank == 1 ? 302 : 303;     // the legal matches in look order
        CHECK(BeHandNums(ro) == std::vector<int>{ want });
        CHECK(BeHandNums(ex) == std::vector<int>{ want });
        CHECK(BeLibNums(ro) == BeLibNums(ex));
        CHECK(ro.players[1].life == 16);
        CHECK(ex.players[1].life == 16);
        CHECK(ro.scripted_combat_dig == -1);
        CHECK(ex.scripted_combat_dig == -1);
    }
}

TEST_CASE("Garruk's Harbinger: the dig axis branches every reachable rank -- no greedy top pick")
{
    // Three matches in the library, a 4-power Harbinger: width = min(15, 3 matches, 4 + 2) = 3. The base plan is the
    // front (rank 0); the axis adds ranks 1 and 2 -- every legal pick is a plan.
    GameState s = BeHarbBoard({ "Forest", "Thragtusk", "Forest", "Questing Beast", "Elder Gargaroth", "Forest" });
    BeAdd(s, kHarb, 50);
    CHECK(BeHarbCodes(s) == std::set<int>{ BeDigPin({ 1 }), BeDigPin({ 2 }) });
    // Each rank takes a DIFFERENT card, so each variant really is a distinct line (a 6-damage hit -- the width's
    // +2 slack -- sees all three; a 4-damage one sees two, and rank 2 clamps onto rank 1's pick).
    std::set<int> taken;
    for (int r = 0; r < 3; ++r)
    {
        GameState t = s;
        BeFireHarb(t, BeDigPin({ r }), 6);
        REQUIRE(t.players[0].hand.size() == 1);
        taken.insert(t.players[0].hand[0].m_number);
    }
    CHECK(taken == std::set<int>{ 301, 303, 304 });
    GameState clamp = s;
    BeFireHarb(clamp, BeDigPin({ 2 }), 4);
    CHECK(BeHandNums(clamp) == std::vector<int>{ 303 });
    // Two Harbingers: the joint product (3 x 3) minus the base's all-front code.
    GameState two = s;
    BeAdd(two, kHarb, 51);
    const std::set<int> codes = BeHarbCodes(two);
    CHECK(codes.size() == 8);
    CHECK(codes.count(BeDigPin({ 2, 1 })) == 1);
    CHECK(codes.count(BeDigPin({ 0, 0 })) == 0);
    // A summoning-sick Harbinger cannot attack: no ordinal, no variants. One match: nothing to choose.
    GameState sick = BeHarbBoard({ "Thragtusk", "Questing Beast", "Forest" });
    BeAdd(sick, kHarb, 50, 0, /*sick=*/true);
    CHECK(BeHarbCodes(sick).empty());
    GameState one = BeHarbBoard({ "Thragtusk", "Forest", "Forest", "Forest" });
    BeAdd(one, kHarb, 50);
    CHECK(BeHarbCodes(one).empty());
    // A deck without the card never builds the axis.
    GameState off = s;
    off.deck_has_combat_damage_dig = false;
    CHECK(BeHarbCodes(off).empty());
}

TEST_CASE("Garruk's Harbinger: the pin is read per ordinal, spent after combat, and never leaks")
{
    // Two Harbingers: ordinal 0 takes rank 2, ordinal 1 rank 0 of what is left on top of its own window.
    GameState s = BeHarbBoard({ "Thragtusk", "Questing Beast", "Elder Gargaroth", "Forest",
                                "Thragtusk", "Questing Beast", "Forest", "Forest" });
    BeAdd(s, kHarb, 50);
    BeAdd(s, kHarb, 51);
    BeFireHarb(s, BeDigPin({ 2, 0 }), 4);
    CHECK(BeHandNums(s) == std::vector<int>{ 302, 304 });
    CHECK(s.scripted_combat_dig == -1);
    // An inherited main-phase ETB-dig pin is not eaten by the combat dig.
    {
        GameState t = BeHarbBoard({ "Thragtusk", "Questing Beast", "Forest", "Forest" });
        BeAdd(t, kHarb, 50);
        ScriptedEtbDig outer(1);
        BeFireHarb(t, -1, 4);
        CHECK(BeHandNums(t) == std::vector<int>{ 300 });
        CHECK(g_scripted_etbdig_choice == 1);
    }
    // A pin whose Harbinger never connects (summoning sick) is spent at the end of combat damage anyway.
    GameState u = BeHarbBoard({ "Thragtusk", "Questing Beast", "Forest", "Forest" });
    u.phase = Phase::Combat;
    BeAdd(u, kHarb, 50, 0, /*sick=*/true);
    u.scripted_combat_dig = static_cast<std::int16_t>(BeDigPin({ 1 }));
    RolloutSimulateCombat(u);
    CHECK(u.scripted_combat_dig == -1);
    CHECK(u.players[0].hand.empty());
    // Packing round trip and the shift.
    const int packed = BeDigPin({ 3, -1, 0 });
    CHECK(CombatDigRankAt(packed, 0) == 3);
    CHECK(CombatDigRankAt(packed, 1) == -1);
    CHECK(CombatDigRankAt(packed, 2) == 0);
    CHECK(CombatDigRankAt(CombatDigPinShift(packed), 0) == -1);
    CHECK(CombatDigRankAt(CombatDigPinShift(packed), 1) == 0);
    CHECK(CombatDigPinShift(BeDigPin({ 3 })) == -1);
    CHECK(BeDigPin({ -1, -1 }) == -1);
}

TEST_CASE("Garruk's Harbinger: no dig once the opponent is dead (a lethal combat ends at the SBA check)")
{
    GameState s = BeHarbBoard({ "Thragtusk", "Forest", "Forest", "Forest" });
    s.phase = Phase::Combat;
    s.players[1].life = 4;
    BeAdd(s, kHarb, 50);
    const std::vector<int> lib = BeLibNums(s);
    RolloutSimulateCombat(s);
    CHECK(s.players[1].life <= 0);
    CHECK(s.players[0].hand.empty());
    CHECK(BeLibNums(s) == lib);
    // ...and the direct trigger agrees.
    GameState t = BeHarbBoard({ "Thragtusk", "Forest", "Forest", "Forest" });
    t.players[1].life = 0;
    BeAdd(t, kHarb, 50);
    BeFireHarb(t, -1, 4);
    CHECK(t.players[0].hand.empty());
    CHECK(BeLibNums(t) == lib);
}

// ---- Migloz, Maze Crusher (batch E) ------------------------------------------------------------
// "Migloz enters with five oil counters on it. {1}, Remove an oil counter: vigilance and menace until end of turn.
// {2}, Remove two oil counters: +2/+2 until end of turn. {3}, Remove three oil counters: destroy target artifact or
// enchantment." USER 2026-11-21: "For Migloz we should include the power pump mode for sure. The other two can be
// heuristically pruned."
namespace
{
constexpr const char* kMig = "Migloz, Maze Crusher";
// `lands` untapped Forests (10..), the land drop spent, Migloz #60 with `oil` oil counters.
GameState BeMigBoard(int lands, int oil, bool sick = true)
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    s.players[0].life = 20;
    s.players[1].life = 20;
    for (int i = 0; i < lands; ++i) { BeAdd(s, "Forest", 10 + i); }
    const int mi = BeAdd(s, kMig, 60, 0, sick);
    s.battlefield[static_cast<std::size_t>(mi)].oil_counters = oil;
    return s;
}
const Permanent& BeMigOf(const GameState& s, int num = 60)
{
    const int i = BeIndexOf(s, num);
    REQUIRE(i >= 0);
    return s.battlefield[static_cast<std::size_t>(i)];
}
std::set<int> BeMigPumpKs(const GameState& s, bool pre_combat)
{
    std::set<int> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    {
        if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 7 && a.sac_source_id == 60)
        {
            CHECK(a.cost.generic == 2 * a.chosen_x);   // pre-scaled K x {2}
            out.insert(a.chosen_x);
        }
    }
    return out;
}
int BeMigModeCount(const GameState& s, bool pre_combat, int mode)
{
    int n = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    { if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == mode) { ++n; } }
    return n;
}
int BeTappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}
void BeResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
// A provider that says YES to the vigilance grant -- the shape a deck provider (or MTG_UNPRUNE=oilgrant) takes.
struct BeOfferGrantProvider : BoarsProvider
{
    bool OffersOilVigilanceMenace(const GameState&, const CardDefinition&) const override { return true; }
};
}   // namespace

TEST_CASE("Migloz: card data, and it enters with five oil counters (no +1/+1 counters)")
{
    const CardDefinition& d = BeDef(kMig);
    CHECK(d.card.IsCreature());
    CHECK(d.card.m_power.value_or(0) == 4);
    CHECK(d.card.m_toughness.value_or(0) == 4);
    CHECK(CardHasSubtype(d.card, "Beast"));
    CHECK(d.params.enters_with_oil_counters == 5);
    REQUIRE(d.params.firebreathing_cost.has_value());
    CHECK(d.params.firebreathing_cost->ManaValue() == 2);
    CHECK(d.params.firebreathing_power == 2);
    CHECK(d.params.firebreathing_tough == 2);
    CHECK(d.params.firebreathing_remove_oil_counters == 2);
    REQUIRE(d.params.vigilance_menace_cost.has_value());
    CHECK(d.params.vigilance_menace_cost->ManaValue() == 1);
    CHECK(d.params.vigilance_menace_remove_oil == 1);
    REQUIRE(d.params.remove_counter_destroy_art_ench_cost.has_value());
    CHECK(d.params.remove_counter_destroy_art_ench_cost->ManaValue() == 3);
    CHECK(d.params.remove_counter_destroy_art_ench_oil == 3);
    CHECK_FALSE(d.params.firebreathing_discard);
    CHECK_FALSE(d.params.firebreathing_grants_lifelink);

    GameState s = BeState();
    BeEnter(s, kMig, 60);
    CHECK(BeMigOf(s).oil_counters == 5);
    CHECK(PlusCountersOn(BeMigOf(s)) == 0);
    CHECK(BeMigOf(s).EffectivePower() == 4);
}

TEST_CASE("Migloz: the pump removes two oil per activation and stops when the oil runs out")
{
    GameState s = BeMigBoard(0, 5);
    CHECK(ApplyActivatePump(s, 0, 60, 7, 2) == 2);
    CHECK(BeMigOf(s).oil_counters == 1);
    CHECK(BeMigOf(s).temp_power_bonus == 4);
    CHECK(BeMigOf(s).temp_tough_bonus == 4);
    CHECK(ApplyActivatePump(s, 0, 60, 7, 1) == 0);   // one oil left: unpayable
    CHECK(BeMigOf(s).oil_counters == 1);
    CHECK(BeMigOf(s).temp_power_bonus == 4);

    GameState t = BeMigBoard(0, 3);
    CHECK(ApplyActivatePump(t, 0, 60, 7, 3) == 1);   // K=3 asked, the oil covers one
    CHECK(BeMigOf(t).oil_counters == 1);
    CHECK(BeMigOf(t).temp_power_bonus == 2);

    // Both pay sites pay only for the activations the oil still covers.
    Action a;
    a.kind = Action::Kind::ActivatePump;
    a.def = &BeDef(kMig);
    a.sac_source_id = 60;
    a.gy_exile_mode = 7;
    a.chosen_x = 3;
    CHECK(PumpPayableActivations(BeMigBoard(0, 5), 0, a) == 2);
    CHECK(PumpPayableActivations(BeMigBoard(0, 1), 0, a) == 0);
    a.chosen_x = 1;
    CHECK(PumpPayableActivations(BeMigBoard(0, 4), 0, a) == 1);
}

TEST_CASE("Migloz: the pump is SEARCHED -- one variant per K (oil- and mana-capped), legal while sick, main 1 and main 2")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    {
        GameState s = BeMigBoard(4, 5);   // 4 mana, 5 oil -> K = 1, 2
        s.m_provider = &provider;
        CHECK(BeMigPumpKs(s, true) == std::set<int>{ 1, 2 });
        CHECK(BeMigPumpKs(s, false) == std::set<int>{ 1, 2 });   // the same menu in a main 2
    }
    {
        GameState s = BeMigBoard(2, 5);   // 2 mana -> K = 1
        s.m_provider = &provider;
        CHECK(BeMigPumpKs(s, true) == std::set<int>{ 1 });
    }
    {
        GameState s = BeMigBoard(6, 3);   // 3 oil -> K = 1 however much mana
        s.m_provider = &provider;
        CHECK(BeMigPumpKs(s, true) == std::set<int>{ 1 });
    }
    {
        GameState s = BeMigBoard(6, 1);   // 1 oil -> no pump
        s.m_provider = &provider;
        CHECK(BeMigPumpKs(s, true).empty());
    }
    // NO GREEDY PICK: the plan menu holds every K side by side. (The no-pump line is not in this list: with the
    // land drop spent the enumeration offers no empty plan, and the host appends the pass itself -- Solve's
    // attack-only seed -- so it is searched against both K's.)
    GameState s = BeMigBoard(4, 5);
    s.m_provider = &provider;
    std::set<int> ks;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int k = 0;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 7) { k += a.chosen_x; } }
        if (k != 0) { ks.insert(k); }
    }
    CHECK(ks.count(1) == 1);
    CHECK(ks.count(2) == 1);
    CHECK(ks.count(3) == 0);   // never more than the oil pays
}

TEST_CASE("Migloz: executor and rollout apply the searched pump identically (oil, P/T, mana)")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    GameState s = BeMigBoard(4, 5);
    s.m_provider = &provider;
    const TurnSolver::Plan* k2 = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::ActivatePump
            && p.actions[0].gy_exile_mode == 7 && p.actions[0].chosen_x == 2) { k2 = &p; break; }
    }
    REQUIRE(k2 != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *k2, true);
    CHECK(BeMigOf(roll).oil_counters == 1);
    CHECK(BeMigOf(roll).EffectivePower() == 8);
    CHECK(BeMigOf(roll).EffectiveToughness() == 8);
    CHECK(BeTappedLands(roll) == 4);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *k2, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeMigOf(ex).oil_counters == BeMigOf(roll).oil_counters);
    CHECK(BeMigOf(ex).temp_power_bonus == BeMigOf(roll).temp_power_bonus);
    CHECK(BeMigOf(ex).temp_tough_bonus == BeMigOf(roll).temp_tough_bonus);
    CHECK(BeTappedLands(ex) == BeTappedLands(roll));

    // OIL ACCOUNTING: the same K=2 plan on a Migloz that has only two oil left pays {2} for ONE activation in both
    // worlds -- never {4} for a second activation the oil cannot pay.
    GameState s2 = s;
    s2.battlefield[static_cast<std::size_t>(BeIndexOf(s2, 60))].oil_counters = 2;
    GameState roll2 = s2;
    TurnSolver::ApplyPlan(roll2, *k2, true);
    CHECK(BeMigOf(roll2).oil_counters == 0);
    CHECK(BeMigOf(roll2).temp_power_bonus == 2);
    CHECK(BeTappedLands(roll2) == 2);
    AIEngine eng2(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng2, *k2, true);
    GameState ex2 = s2;
    eng2.TakeTurn(ex2, true, BeResolveAll);
    CHECK(BeMigOf(ex2).oil_counters == 0);
    CHECK(BeMigOf(ex2).temp_power_bonus == 2);
    CHECK(BeTappedLands(ex2) == 2);
}

TEST_CASE("Migloz: a plan casting Migloz opens breakpoint site 9 (pump it the same main)")
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    BeAdd(s, "Stomping Ground", 10);
    for (int i = 0; i < 4; ++i) { BeAdd(s, "Forest", 11 + i); }
    s.players[0].hand.push_back(BeZone(kMig, 1));
    TurnSolver::Plan p;
    Action a;
    a.kind       = Action::Kind::CastFromHand;
    a.card_name  = BeDef(kMig).card.m_name;
    a.def        = &BeDef(kMig);
    a.hand_index = 0;
    p.actions.push_back(a);
    CHECK((TurnSolver::PlanOpensBreakpointForTest(s, p) & (1 << 9)) != 0);
    const std::vector<std::uint64_t> pre = TurnSolver::SnapshotActivatableAbilities(s);
    GameState t = s;
    t.players[0].hand.clear();
    const int mi = BeAdd(t, kMig, 1, 0, /*sick=*/true);
    t.battlefield[static_cast<std::size_t>(mi)].oil_counters = 5;
    CHECK(TurnSolver::PostEntryActivationPending(t, pre));
}

TEST_CASE("Migloz: vigilance + menace is implemented, live only when it changes something, and pruned from the search")
{
    {
        GameState s = BeMigBoard(2, 5, /*sick=*/false);
        CHECK(OilVigilanceActivationLive(s, 0, 60));
        CHECK(ApplyActivatePump(s, 0, 60, 8, 1) == 1);
        CHECK(BeMigOf(s).temp_vigilance);
        CHECK(BeMigOf(s).oil_counters == 4);
        CHECK_FALSE(OilVigilanceActivationLive(s, 0, 60));    // already vigilant: a second grant is a no-op
        CHECK(ApplyActivatePump(s, 0, 60, 8, 1) == 0);
        CHECK(BeMigOf(s).oil_counters == 4);
    }
    CHECK_FALSE(OilVigilanceActivationLive(BeMigBoard(2, 5, /*sick=*/true), 0, 60));   // cannot attack: nothing gained
    CHECK_FALSE(OilVigilanceActivationLive(BeMigBoard(2, 0, false), 0, 60));           // no oil
    {
        GameState s = BeMigBoard(2, 5, false);
        BeAdd(s, "Kaheera, the Orphanguard", 61);                                     // Kaheera: Beasts have vigilance
        CHECK_FALSE(OilVigilanceActivationLive(s, 0, 60));
    }
    // The ENGINE offers it; the root provider keeps it out of the autonomous search (USER: heuristically pruned).
    BeSearchedScope on(true);
    {
        BeastsProvider provider;
        GameState s = BeMigBoard(2, 5, false);
        s.m_provider = &provider;
        CHECK_FALSE(provider.OffersOilVigilanceMenace(s, BeDef(kMig)));
        CHECK(BeMigModeCount(s, true, 8) == 0);
        CHECK(BeMigModeCount(s, true, 7) == 1);   // the pump is still searched
    }
    {
        BeOfferGrantProvider provider;   // a provider that admits it (what MTG_UNPRUNE=oilgrant does at the root)
        GameState s = BeMigBoard(2, 5, false);
        s.m_provider = &provider;
        CHECK(BeMigModeCount(s, true, 8) == 1);
        CHECK(BeMigModeCount(s, false, 8) == 0);   // after combat the grant changes nothing
        GameState t = s;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (p.actions.size() != 1 || p.actions[0].gy_exile_mode != 8) { continue; }
            TurnSolver::ApplyPlan(t, p, true);
            break;
        }
        CHECK(BeMigOf(t).temp_vigilance);
        CHECK(BeMigOf(t).oil_counters == 4);
        CHECK(BeTappedLands(t) == 1);
    }
    CHECK(std::string(GateName(UnprunedGate::OilGrant)) == "oilgrant");
}

TEST_CASE("Migloz: the destroy is paid in OIL, targets any artifact/enchantment, and the search never hits our own")
{
    GameState s = BeMigBoard(3, 5);
    BeAdd(s, "Urza's Incubator", 70);                  // ours
    BeAdd(s, "Lightning Greaves", 71, /*controller=*/1);  // theirs
    const std::vector<int> legal = CounterDestroyLegalTargets(s, 0);
    CHECK(legal.size() == 2);
    const Permanent& mig = BeMigOf(s);
    // The root prune (USER: heuristically pruned): our own artifact/enchantment is never offered; theirs is.
    const std::vector<int> pruned = RestrictCounterDestroyTargetsFor(s, 0, mig, legal, /*unpruned=*/false);
    REQUIRE(pruned.size() == 1);
    CHECK(s.battlefield[static_cast<std::size_t>(pruned[0])].card.m_number == 71);
    CHECK(RestrictCounterDestroyTargetsFor(s, 0, mig, legal, /*unpruned=*/true) == legal);   // MTG_UNPRUNE=counterdestroy
    {   // Steelbane Hydra (a +1/+1-counter source) keeps every legal target at the root
        GameState h = s;
        BeAdd(h, "Steelbane Hydra", 80);
        CHECK(RestrictCounterDestroyTargetsFor(h, 0, BeMigOf(h, 80), legal, false) == legal);
    }
    // Search enumeration: one destroy action, at their Greaves.
    BeastsProvider provider;
    s.m_provider = &provider;
    BeSearchedScope on(true);
    int destroys = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind != Action::Kind::ActivatePermAbility || a.ability_mode != Action::AbilityMode::CounterDestroyArtEnch)
        { continue; }
        ++destroys;
        CHECK(a.sac_victim_id == 71);
    }
    CHECK(destroys == 1);
    {   // two oil cannot pay it
        GameState t = s;
        t.battlefield[static_cast<std::size_t>(BeIndexOf(t, 60))].oil_counters = 2;
        int n = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(t, true))
        { if (a.ability_mode == Action::AbilityMode::CounterDestroyArtEnch && a.kind == Action::Kind::ActivatePermAbility) { ++n; } }
        CHECK(n == 0);
        const TapTargetPumpPinScope pin(71, 0);
        CHECK_FALSE(PermAbilitySourceLive(t, 0, 60, PermAbilityMode::CounterDestroyArtEnch, nullptr));
    }
    // Resolution: three oil off, the target destroyed, Migloz's P/T untouched (no +1/+1 counter is involved).
    ResolveCounterDestroyArtEnch(s, 0, 60, 71);
    CHECK(BeMigOf(s).oil_counters == 2);
    CHECK(BeMigOf(s).EffectivePower() == 4);
    CHECK(BeIndexOf(s, 71) < 0);
    CHECK(PermAbilityLabelFor(PermAbilityMode::CounterDestroyArtEnch, &BeDef(kMig)).find("oil") != std::string::npos);
}

TEST_CASE("Migloz: oil is state -- the legend copies differ, and more oil dominates less")
{
    GameState s = BeMigBoard(0, 5, false);
    BeAdd(s, kMig, 61);
    s.battlefield[static_cast<std::size_t>(BeIndexOf(s, 61))].oil_counters = 5;
    const std::vector<int> dup = { BeIndexOf(s, 60), BeIndexOf(s, 61) };
    CHECK(LegendCopiesInterchangeable(s, dup));
    s.battlefield[static_cast<std::size_t>(BeIndexOf(s, 61))].oil_counters = 1;
    CHECK_FALSE(LegendCopiesInterchangeable(s, dup));   // keeping the 5-oil copy is a real choice

    auto snap = [](int oil)
    {
        GameState g = BeMigBoard(0, oil, false);
        g.step = Step::Untap;
        return dominance::Build(g, DefaultProvider());
    };
    const dominance::DomSnap more = snap(5);
    const dominance::DomSnap less = snap(2);
    REQUIRE(more.comparable);
    REQUIRE(less.comparable);
    CHECK(dominance::Covers(more, less));
    CHECK_FALSE(dominance::Covers(less, more));
}

// ---- Nykthos, Shrine to Nyx (Beasts F) ------------------------------------------------------------------------------
// "{2}, {T}: Choose a color. Add an amount of mana of that color equal to your devotion to that color." The Three Tree
// City scaled-land shape with N = DevotionTo(chosen colour), PER COLOUR: the payer branches every colour with N >= 1,
// the mana cache keys all five counts, the human picks a per-colour fed face, and the recolour refuses a re-read whose
// yield differs.
namespace
{
constexpr const char* kNyk = "Nykthos, Shrine to Nyx";
ManaCost BeCost(int generic, int w, int u, int b, int r, int g)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}
// Nykthos #5, `forests` untapped Forests (10..), then `perms` (40..), the land drop spent.
GameState BeNykBoard(int forests, std::initializer_list<const char*> perms)
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    BeAdd(s, kNyk, 5);
    for (int i = 0; i < forests; ++i) { BeAdd(s, "Forest", 10 + i); }
    int n = 40;
    for (const char* p : perms) { BeAdd(s, p, n++); }
    return s;
}
const Permanent& BeNyk(const GameState& s)
{
    const int i = BeIndexOf(s, 5);
    REQUIRE(i >= 0);
    return s.battlefield[static_cast<std::size_t>(i)];
}
bool BeTapped(const GameState& s, int num)
{
    const int i = BeIndexOf(s, num);
    return i >= 0 && s.battlefield[static_cast<std::size_t>(i)].tapped;
}
void BeHand(GameState& s, const char* name, int number)
{
    Card c = BeDef(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}
}   // namespace

TEST_CASE("Nykthos: card data, and the fed yield is the devotion to the colour picked (G vs R)")
{
    const CardDefinition& d = BeDef(kNyk);
    CHECK(d.card.IsLand());
    CHECK(d.card.HasSupertype(Supertype::Legendary));
    CHECK(d.params.mana_per_devotion);
    CHECK(d.params.mana_per_creature_feeder_generic == 2);
    CHECK(IsScaledManaLand(d));
    CHECK(IsDevotionManaLand(d));
    CHECK_FALSE(IsDevotionManaLand(BeDef("Forest")));
    CHECK(d.params.mana_per_creature_subtype.empty());
    CHECK(d.params.mana_per_other_controlled_subtype.empty());

    GameState s = BeNykBoard(2, { "Questing Beast", "Herald of Ilharg" });   // G 2 + 1, R 1
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 3);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Red) == 1);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::White) == 0);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Colorless) == 0);
    CHECK(DevotionTo(s, 0, "G") == 3);
    CHECK(DevotionTo(s, 0, "R") == 1);
    // The colour-blind bound is the best colour (sound over-count), by source and by the null-source convention.
    CHECK(ScaledManaCreatureCount(s, &BeNyk(s)) == 3);
    CHECK(ScaledManaCreatureCount(s) == 3);
    // An opponent's permanent adds nothing; a Garruk planeswalker's {G}{G}{G} does (devotion counts every permanent).
    BeAdd(s, "Questing Beast", 50, /*controller=*/1);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 3);
    BeAdd(s, "Garruk, Primal Hunter", 51);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 6);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Red) == 1);
}

TEST_CASE("Nykthos: the payer branches every colour with devotion -- {G}{G}{G} and {R} pay, red is never pinned away")
{
    ManaCacheClearPerGame();
    const GameState base = BeNykBoard(2, { "Questing Beast", "Herald of Ilharg" });   // G 3, R 1
    {   // fed for three GREEN off the two Forests' feed
        GameState s = base;
        CHECK(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 0, 3), false, ManaPool{}));
        CHECK(BeNyk(s).tapped);
    }
    {   // fed for one RED: the only red source on the board
        GameState s = base;
        CHECK(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 1, 0), false, ManaPool{}));
        CHECK(BeNyk(s).tapped);
        CHECK(BeTapped(s, 10));
        CHECK(BeTapped(s, 11));
    }
    { GameState s = base; CHECK_FALSE(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 2, 0), false, ManaPool{})); }  // R = 1
    { GameState s = base; CHECK_FALSE(TapForCostBacktrack(s, BeCost(0, 1, 0, 0, 0, 0), false, ManaPool{})); }  // no W
    { GameState s = base; CHECK_FALSE(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 0, 4), false, ManaPool{})); }  // G = 3
    // The cheap plain {C} still pays a generic bill (the fed mode is reached only when the plain tap cannot pay).
    {
        GameState s = base;
        CHECK(TapForCostBacktrack(s, BeCost(3, 0, 0, 0, 0, 0), false, ManaPool{}));
    }
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: the mana cache keys every colour's devotion -- the same best colour, a different red, splits the key")
{
    // A: Questing Beast + Herald (G 3, R 1). B: Questing Beast + Thragtusk (G 3, R 0). Same source list (Nykthos + two
    // Forests), same MAX devotion, so a max-only key collides; {R} pays on A only. Both orders: a stale positive and a
    // stale negative.
    const GameState a0 = BeNykBoard(2, { "Questing Beast", "Herald of Ilharg" });
    const GameState b0 = BeNykBoard(2, { "Questing Beast", "Thragtusk" });
    REQUIRE(ScaledManaCreatureCount(a0) == ScaledManaCreatureCount(b0));
    const ManaCost red = BeCost(0, 0, 0, 0, 1, 0);
    ManaCacheClearPerGame();
    {
        GameState a = a0; CHECK(TapForCostBacktrack(a, red, false, ManaPool{}));
        GameState b = b0; CHECK_FALSE(TapForCostBacktrack(b, red, false, ManaPool{}));
    }
    ManaCacheClearPerGame();
    {
        GameState b = b0; CHECK_FALSE(TapForCostBacktrack(b, red, false, ManaPool{}));
        GameState a = a0; CHECK(TapForCostBacktrack(a, red, false, ManaPool{}));
    }
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: a Wild Growth on it adds its {G} to the fed tap (+1), and counts toward green devotion")
{
    ManaCacheClearPerGame();
    auto board = [](bool on_nykthos)
    {
        GameState s = BeNykBoard(0, { "Questing Beast" });   // G 2
        s.deck_has_land_aura = true;   // StampDeckTraits' land-Aura stamp (LandAuraBonusOfLand reads nothing without it)
        BeAdd(s, "Mountain", 12);
        s.battlefield[static_cast<std::size_t>(BeIndexOf(s, 12))].tapped = true;
        const int wg = BeAdd(s, "Wild Growth", 45);
        s.battlefield[static_cast<std::size_t>(wg)].aura_attached_to = on_nykthos ? 5 : 12;
        return s;
    };
    ManaPool feed;
    feed.colorless = 2;   // the {2} already floating
    {
        GameState s = board(true);
        CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 3);   // QB 2 + Wild Growth 1
        CHECK(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 0, 4), false, feed));   // 3 + the Aura's 1
    }
    {
        GameState s = board(true);
        CHECK_FALSE(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 0, 5), false, feed));
    }
    {
        GameState s = board(false);   // the Aura on the (tapped) Mountain: devotion 3, no bonus on Nykthos
        CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 3);
        CHECK_FALSE(TapForCostBacktrack(s, BeCost(0, 0, 0, 0, 0, 4), false, feed));
    }
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: hybrid pips count for both colours -- Kaheera's {G/W}{G/W} is 2 green and 2 white")
{
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(2, { "Kaheera, the Orphanguard" });
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Green) == 2);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::White) == 2);
    CHECK(DevotionTo(s, 0, "W") == 2);
    CHECK(ScaledManaYieldFor(s, &BeNyk(s), Color::Red) == 0);
    {
        GameState p = s;
        CHECK(TapForCostBacktrack(p, BeCost(0, 2, 0, 0, 0, 0), false, ManaPool{}));   // {W}{W}: fed white
    }
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, BeNyk(s));
    CHECK(o.fed_faces == std::vector<std::string>{ "W", "G" });   // a tie: WUBRG order
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: the human's fed face is per colour -- the R face adds the red count, a W face is refused")
{
    const GameState base = BeNykBoard(2, { "Questing Beast", "Herald of Ilharg" });   // G 3, R 1
    const HumanPreTapOffer o = HumanPreTapOfferFor(base, BeNyk(base));
    CHECK(o.free_faces == "C");
    CHECK(o.fed_faces == std::vector<std::string>{ "G", "R" });   // N descending; no W/U/B (no devotion)
    CHECK(o.feed == "{2}");
    CHECK(o.note.find("3 {G}") != std::string::npos);
    CHECK(o.note.find("1 {R}") != std::string::npos);

    auto tap = [](GameState& s, int color)
    {
        TurnSolver::PreTap t;
        t.name = kNyk; t.num = 5; t.color = color;
        return ApplyHumanPreTap(s, t);
    };
    {
        GameState s = base;
        s.floating_mana.green = 2;   // the two Forests, tapped first by the human
        CHECK(tap(s, static_cast<int>(Color::Red)) == "");
        CHECK(s.floating_mana.red == 1);
        CHECK(s.floating_mana.green == 0);
        CHECK(BeNyk(s).tapped);
    }
    {
        GameState s = base;
        s.floating_mana.green = 2;
        CHECK(tap(s, static_cast<int>(Color::Green)) == "");
        CHECK(s.floating_mana.green == 3);
    }
    {
        GameState s = base;
        s.floating_mana.green = 2;
        CHECK_FALSE(tap(s, static_cast<int>(Color::White)).empty());   // no white devotion: not a face
        CHECK_FALSE(BeNyk(s).tapped);
        CHECK(s.floating_mana.green == 2);
    }
}

TEST_CASE("Nykthos: the fed-tap recolour is refused when the other colour's devotion differs (and taken when equal)")
{
    // The payment: two Forests fed Nykthos for GREEN (devotion 3) and {2} took two of it -- one {G} floats. The line's
    // colour is RED (Monastery Swiftspear in hand). Re-reading the surplus as red is the same tap only if Nykthos makes
    // as much red as green.
    auto run = [](std::initializer_list<const char*> perms)
    {
        GameState pre = BeNykBoard(2, perms);
        BeHand(pre, "Monastery Swiftspear", 70);
        GameState post = pre;
        for (int n : { 5, 10, 11 }) { post.battlefield[static_cast<std::size_t>(BeIndexOf(post, n))].tapped = true; }
        post.floating_mana.green = 1;
        ScaledFedRecolourAfterForTest(post, BeCost(2, 0, 0, 0, 0, 0), pre);
        return post.floating_mana;
    };
    const ManaPool unequal = run({ "Questing Beast", "Herald of Ilharg" });   // G 3, R 1: refused
    CHECK(unequal.green == 1);
    CHECK(unequal.red == 0);
    const ManaPool equal = run({ "Herald of Ilharg", "Herald of Ilharg", "Herald of Ilharg" });   // G 3, R 3
    CHECK(equal.green == 0);
    CHECK(equal.red == 1);
}

TEST_CASE("Nykthos: executor and rollout pay a red spell off the fed RED tap identically")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(2, { "Questing Beast", "Herald of Ilharg" });   // the only red: Nykthos fed, R 1
    BeHand(s, "Monastery Swiftspear", 70);
    s.m_provider = &provider;
    const TurnSolver::Plan* cast = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromHand && p.actions[0].def != nullptr
            && p.actions[0].def->card.m_name.str() == "Monastery Swiftspear") { cast = &p; break; }
    }
    REQUIRE(cast != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *cast, true);
    CHECK(BeIndexOf(roll, 70) >= 0);
    CHECK(BeNyk(roll).tapped);

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *cast, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeIndexOf(ex, 70) >= 0);
    for (int n : { 5, 10, 11 }) { CHECK(BeTapped(ex, n) == BeTapped(roll, n)); }
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: the payability walk grows devotion -- Harbinger's {G}{G} funds the Gargaroth after it")
{
    // Nykthos + six Forests + Questing Beast (G 2). Harbinger {1}{G}{G} off three Forests, then devotion is 4: two
    // Forests feed Nykthos for four green + the last Forest = Gargaroth's 5. No other order pays, and a probe that never
    // put Harbinger on its scratch board would read Nykthos at 2 and reject the pair.
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(6, { "Questing Beast" });
    BeHand(s, "Garruk's Harbinger", 70);
    BeHand(s, "Elder Gargaroth", 71);
    s.m_provider = &provider;
    bool both = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int n = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
        if (n == 2) { both = true; break; }
    }
    CHECK(both);
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: the payer holds it for a later devotion-grown fed tap, and spends its {C} when holding would strand it")
{
    // MTG_DEVOTION_LAND_HOLD (ManaSourceRankBase): a payment PREFERENCE. Hold: Harbinger's {1} off a Forest so the fed
    // tap is left for Gargaroth at devotion 4. No hold: three Forests, a {1}{G} then a {G}{G} -- holding Nykthos would
    // leave one Forest to feed its {2}, so its plain {C} must pay the {1}.
    BeastsProvider provider;
    ManaCacheClearPerGame();
    {
        GameState s = BeNykBoard(6, { "Questing Beast" });   // G 2
        s.m_provider = &provider;
        {
            LineUnpaidCostScope luc(BeCost(4, 0, 0, 0, 0, 4));   // Harbinger {1}{G}{G} + Gargaroth {3}{G}{G}
            CHECK(TapForCostDirect(s, BeCost(1, 0, 0, 0, 0, 2), true));
        }
        CHECK_FALSE(BeNyk(s).tapped);
        BeAdd(s, "Garruk's Harbinger", 41);   // resolved: G 4
        {
            LineUnpaidCostScope luc(BeCost(3, 0, 0, 0, 0, 2));
            CHECK(TapForCostDirect(s, BeCost(3, 0, 0, 0, 0, 2), true));
        }
        CHECK(BeNyk(s).tapped);
    }
    ManaCacheClearPerGame();
    {
        GameState s = BeNykBoard(3, {});   // devotion 0
        s.m_provider = &provider;
        {
            LineUnpaidCostScope luc(BeCost(1, 0, 0, 0, 0, 3));   // a {1}{G} creature + Nourish {G}{G}
            CHECK(TapForCostDirect(s, BeCost(1, 0, 0, 0, 0, 1), true));
        }
        CHECK(BeNyk(s).tapped);
        {
            LineUnpaidCostScope luc(BeCost(0, 0, 0, 0, 0, 2));
            CHECK(TapForCostDirect(s, BeCost(0, 0, 0, 0, 0, 2), false));
        }
    }
    ManaCacheClearPerGame();
}

namespace
{
// The plan that casts exactly the hand cards named `names` (any order), or nullptr.
const TurnSolver::Plan* BeFindCastPlan(const std::vector<TurnSolver::Plan>& plans,
                                       std::initializer_list<std::string> names)
{
    for (const TurnSolver::Plan& p : plans)
    {
        std::vector<std::string> got;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            got.push_back(a.card_name.str());
        }
        std::vector<std::string> want(names);
        std::sort(got.begin(), got.end());
        std::sort(want.begin(), want.end());
        if (got == want) { return &p; }
    }
    return nullptr;
}

}   // namespace

TEST_CASE("Nykthos: the pip-heavy permanent first can be the ONLY payable order -- enumerated, and both apply worlds take it")
{
    // Nykthos + eight Forests + Questing Beast (G 2). Hand: Thragtusk {4}{G} (one pip) and Elder Gargaroth {3}{G}{G} (two),
    // the same mana value, Thragtusk first in the hand. Thragtusk first: five Forests, then devotion 3 -- two Forests feed
    // Nykthos for three green + one Forest = 4 < 5 (or the plain {C}: 3 Forests + 1 = 4). Gargaroth first: five
    // Forests, then devotion 4 -- two of the last three Forests feed Nykthos for four, + the third = Thragtusk's 5. Board
    // total 9 (Forests + the {C}) < 10, so the flat pool rejects the pair, the filter rescue's single (rank) walk takes
    // Thragtusk first and fails, and before the payable-order domain covered devotion growth the pair was REFUSED --
    // a payable line no budget could find as one plan.
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(8, { "Questing Beast" });
    BeHand(s, "Thragtusk", 70);
    BeHand(s, "Elder Gargaroth", 71);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = BeFindCastPlan(plans, { "Thragtusk", "Elder Gargaroth" });
    REQUIRE(both != nullptr);
    // The walk itself: only Gargaroth-first pays.
    std::vector<int> tg, gt;
    for (int i = 0; i < static_cast<int>(both->actions.size()); ++i)
    {
        const Action& a = both->actions[static_cast<std::size_t>(i)];
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Thragtusk") { tg.insert(tg.begin(), i); gt.push_back(i); }
        else                     { tg.push_back(i); gt.insert(gt.begin(), i); }
    }
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, both->actions, tg));
    CHECK(TurnSolver::OrderPaysSequentially(s, both->actions, gt));
    // Rollout and executor both realise the paying order and cast both.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    CHECK(BeIndexOf(roll, 70) >= 0);
    CHECK(BeIndexOf(roll, 71) >= 0);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *both, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeIndexOf(ex, 70) >= 0);
    CHECK(BeIndexOf(ex, 71) >= 0);
    ManaCacheClearPerGame();
}

TEST_CASE("Nykthos: a red devotion made INSIDE the line opens the red pip -- Vexing Shusher {R/G}{R/G} off Forests, then Swiftspear off the fed {R}")
{
    // Nykthos + four Forests, no devotion, no red source. Hand: Vexing Shusher {R/G}{R/G} and Monastery Swiftspear {R}.
    // Shusher paid with two Forests gives red devotion 2; the last two Forests feed Nykthos for {R}{R}. The colour gate
    // used to read the BOARD's devotion only (red 0) and refused every subset holding the {R} pip as "no untapped source
    // produces red". It now credits the colours a permanent in hand would add.
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(4, {});
    BeHand(s, "Vexing Shusher", 70);
    BeHand(s, "Monastery Swiftspear", 71);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = BeFindCastPlan(plans, { "Vexing Shusher", "Monastery Swiftspear" });
    REQUIRE(both != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    CHECK(BeIndexOf(roll, 70) >= 0);
    CHECK(BeIndexOf(roll, 71) >= 0);
    CHECK(BeNyk(roll).tapped);
    // Without Shusher the red pip stays unreachable (the gate still refuses what nothing can make).
    GameState lone = BeNykBoard(4, {});
    BeHand(lone, "Monastery Swiftspear", 71);
    lone.m_provider = &provider;
    CHECK(BeFindCastPlan(TurnSolver::EnumerateMainPlans(lone, true), { "Monastery Swiftspear" }) == nullptr);
    ManaCacheClearPerGame();
}


// ---- Bloated Contaminator (Beasts G) ----------------------------------------------------------------------------
// "Trample / Toxic 1 / Whenever this creature deals combat damage to a player, proliferate." Toxic is part of the
// damage event (ApplyToxicPoison, both damage-step shapes of the shared combat core); the proliferate is
// FireCombatDamageProliferate, the subset ranked through the proliferate half of the combat-time pin and collapsed
// by the root provider (ProliferateCollapse) when every kind is proven beneficial.
namespace
{
constexpr const char* kCont = "Bloated Contaminator";
GameState BeContBoard()
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.deck_has_combat_proliferate = true;
    s.phase = Phase::Combat;
    return s;
}
Permanent& BeNum(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    REQUIRE_MESSAGE(false, "no permanent #", number);
    return s.battlefield.front();
}
int BeProlifPin(std::initializer_list<int> ranks)
{
    int r[kProlifMaxOrdinals];
    int n = 0;
    for (int x : ranks) { r[n++] = x; }
    return ProlifPackRanks(r, n);
}
std::set<int> BeProlifCodes(const GameState& s)
{
    TurnSolver::Plan base;
    base.land_decided = true;
    std::set<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
    { if (p.combat_dig_choice >= 0) { out.insert(p.combat_dig_choice); } }
    return out;
}
// A board with one counter of every kind the deck makes: Herald (2 +1/+1, sick), Garruk (3 loyalty), Migloz (5 oil,
// sick), and a Contaminator able to attack.
GameState BeContKinds()
{
    GameState s = BeContBoard();
    BeAdd(s, kCont, 50);
    BeAdd(s, "Herald of Ilharg", 51, 0, /*sick=*/true);
    BeNum(s, 51).counters.Add(Counter::Type::PlusOnePlusOne, 2);
    BeAdd(s, "Garruk, Primal Hunter", 52);
    BeNum(s, 52).loyalty = 3;
    BeNum(s, 52).counters.Add(Counter::Type::Loyalty, 3);
    BeAdd(s, "Migloz, Maze Crusher", 53, 0, /*sick=*/true);
    BeNum(s, 53).oil_counters = 5;
    return s;
}
int BeLoyaltyMirror(const Permanent& p)
{
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::Loyalty) { return c.count; } }
    return -1;
}
// A provider whose collapse refuses (what MTG_UNPRUNE=proliferate does at the root): the full subset menu.
struct BeFullProlifProvider : BoarsProvider
{
    bool ProliferateCollapse(const GameState&, int, const std::vector<ProliferateCand>&) const override { return false; }
};
struct BeHeadsScope
{
    int saved = gamesetup::t_setup.opponent_heads;
    explicit BeHeadsScope(int h) { gamesetup::t_setup.opponent_heads = h; }
    ~BeHeadsScope() { gamesetup::t_setup.opponent_heads = saved; }
};
}   // namespace

TEST_CASE("Contaminator: card data -- a 4/4 trample Phyrexian Beast with toxic 1 and the combat-damage proliferate")
{
    const CardDefinition& d = BeDef(kCont);
    CHECK(d.card.m_power.value_or(0) == 4);
    CHECK(d.card.m_toughness.value_or(0) == 4);
    CHECK(d.card.HasKeyword(Keyword::Trample));
    std::vector<std::string> subs;
    for (const std::string& st : d.card.m_subtypes) { subs.push_back(st); }
    CHECK(subs == std::vector<std::string>{ "Phyrexian", "Beast" });
    CHECK(d.params.toxic == 1);
    CHECK(d.params.combat_damage_self_proliferate);
    CHECK(PoisonPerHitOf(&d) == 2);
}

TEST_CASE("Contaminator: toxic poisons on BOTH damage-step shapes, as part of the damage (before the proliferate)")
{
    for (int two_step = 0; two_step < 2; ++two_step)
    {
        for (int prolif = 0; prolif < 2; ++prolif)
        {
            GameState s = BeContBoard();
            s.deck_has_combat_proliferate = prolif != 0;   // off: only the damage result is left to see
            BeAdd(s, kCont, 50);
            if (two_step != 0) { BeAdd(s, "Knight of Meadowgrain", 60); }   // a first striker: the two-step shape
            RolloutSimulateCombat(s);
            CAPTURE(two_step); CAPTURE(prolif);
            CHECK(s.players[1].life == 20 - 4 - (two_step != 0 ? 2 : 0));
            // toxic 1 is on as the damage lands; the proliferate trigger then adds the second counter.
            CHECK(s.players[1].poison_counters == (prolif != 0 ? 2 : 1));
        }
    }
    // A creature WITHOUT toxic gives no poison (and a prevented hit gives none: no damage, no toxic).
    GameState q = BeContBoard();
    BeAdd(q, "Questing Beast", 61);
    RolloutSimulateCombat(q);
    CHECK(q.players[1].poison_counters == 0);
    GameState pv = BeContBoard();
    BeAdd(pv, kCont, 50);
    pv.combat_damage_prevented_this_turn = true;
    RolloutSimulateCombat(pv);
    CHECK(pv.players[1].poison_counters == 0);
    CHECK(pv.players[1].life == 20);
}

TEST_CASE("Contaminator: executor and rollout combat give the same toxic + proliferate (every kind)")
{
    GameState ro = BeContKinds();
    RolloutSimulateCombat(ro);
    GameState ex = BeContKinds();
    BeNum(ex, 52).loyalty_activated_this_turn = true;   // the executor plays on to main 2: keep Garruk's loyalty still
    BeNum(ro, 52).loyalty_activated_this_turn = true;
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::Combat);
    for (GameState* g : { &ro, &ex })
    {
        CHECK(g->players[1].life == 16);
        CHECK(g->players[1].poison_counters == 2);               // toxic 1, then proliferate +1
        CHECK(PlusCountersOn(BeNum(*g, 51)) == 3);               // Herald 2 -> 3
        CHECK(BeNum(*g, 52).loyalty == 4);                       // Garruk 3 -> 4 ...
        CHECK(BeLoyaltyMirror(BeNum(*g, 52)) == 4);              // ... and its counter mirror in step
        CHECK(BeNum(*g, 53).oil_counters == 6);                  // Migloz 5 -> 6
        CHECK(PlusCountersOn(BeNum(*g, 50)) == 0);               // the Contaminator itself had none
        CHECK(g->scripted_combat_dig == -1);
    }
}

TEST_CASE("Contaminator: the poison clock -- 1 then 2 on the first hit, 3 then 4 on the second; none after a lethal toxic")
{
    // CR 120.3g / 702.164c: the toxic counter is a RESULT of the damage; the proliferate is a trigger that resolves
    // after it (CR 603.2 / 603.3), so on a 0-poison opponent the first hit is toxic -> 1, proliferate -> 2.
    GameState s = BeContBoard();
    BeAdd(s, kCont, 50);
    {
        GameState t = s;
        t.deck_has_combat_proliferate = false;
        RolloutSimulateCombat(t);
        CHECK(t.players[1].poison_counters == 1);
    }
    RolloutSimulateCombat(s);
    CHECK(s.players[1].poison_counters == 2);
    BeNum(s, 50).tapped = false;
    s.phase = Phase::Combat;
    {
        GameState t = s;
        t.deck_has_combat_proliferate = false;
        RolloutSimulateCombat(t);
        CHECK(t.players[1].poison_counters == 3);
    }
    RolloutSimulateCombat(s);
    CHECK(s.players[1].poison_counters == 4);
    // At 8: toxic 9, proliferate 10 -> the opponent has lost (poison) at 12 life.
    GameState w = BeContBoard();
    w.players[1].poison_counters = 8;
    BeAdd(w, kCont, 50);
    RolloutSimulateCombat(w);
    CHECK(w.players[1].poison_counters == 10);
    CHECK(w.players[1].life == 16);
    CHECK(OpponentHasLost(w));
    // At 9: toxic makes 10, the SBA ends the game before the trigger resolves -- no proliferate (Herald stays at 2).
    GameState l = BeContKinds();
    l.players[1].poison_counters = 9;
    RolloutSimulateCombat(l);
    CHECK(l.players[1].poison_counters == 10);
    CHECK(PlusCountersOn(BeNum(l, 51)) == 2);
    CHECK(OpponentHasLost(l));
}

TEST_CASE("Contaminator: the search finds a 10-poison win where life damage alone cannot win")
{
    // Opponent at 20 life (4 damage cannot kill) and 8 poison: the Contaminator's hit wins by poison THIS turn. Unlimited
    // budget and max_turns = this turn arm every no-win proof the engine has (proven_no_win included).
    for (int poison = 7; poison <= 8; ++poison)
    {
        GameState s = BeContBoard();
        s.phase = Phase::PreCombatMain;
        s.players[1].poison_counters = poison;
        BeAdd(s, kCont, 50);
        BeAdd(s, "Forest", 40);
        AIEngine eng(MulliganProfile::DefaultProfile(), 2, 0);
        eng.SetMaxTurns(s.turn_number);
        GameEngine ge(eng);
        const int won = ge.PlayOutFrom(s, s.turn_number, GameEngine::ResumeAt::Main1);
        CAPTURE(poison);
        CHECK(won == (poison == 8 ? s.turn_number : -1));
        CHECK(s.players[1].life == 16);
        CHECK(s.players[1].poison_counters == poison + 2);
    }
    // The life-only lethal-exempt drops and the X-waste early proof read this over-approximation (both twins).
    GameState r = BeContBoard();
    r.deck_poison_hit_bound = 2;               // the stamped Beasts bound: one Contaminator, toxic 1 + proliferate
    r.players[1].poison_counters = 5;
    CHECK_FALSE(PoisonWinReachableThisTurn(r));   // 5 + 2 x 2 (the double-strike allowance) = 9 < 10
    r.players[1].poison_counters = 6;
    CHECK(PoisonWinReachableThisTurn(r));         // 6 + 4 = 10
    r.deck_poison_hit_bound = 0;
    r.players[1].poison_counters = 9;
    CHECK_FALSE(PoisonWinReachableThisTurn(r));   // a deck that cannot poison: never
    // Poison is part of the search's state key (two boards differing only in poison never share a memo entry).
    GameState a = BeContBoard();
    BeAdd(a, kCont, 50);
    GameState b = a;
    b.players[1].poison_counters = 3;
    GameState c = a;
    CHECK(TurnSolver::MidTurnStateKeyOf(a) != TurnSolver::MidTurnStateKeyOf(b));
    CHECK(TurnSolver::MidTurnStateKeyOf(a) == TurnSolver::MidTurnStateKeyOf(c));
}

TEST_CASE("Contaminator: Two-Headed Giant -- the team loses at FIFTEEN poison, and toxic is not doubled by the second head")
{
    BeHeadsScope two(2);
    REQUIRE(gamesetup::OpponentHeads() == 2);
    CHECK(Player::PoisonLossThreshold() == 15);
    Player p;
    p.life = 30;
    p.poison_counters = 14;
    CHECK_FALSE(p.HasLost());
    p.poison_counters = 15;
    CHECK(p.HasLost());
    // 13: toxic 1 (one counter for the team, not one per head) -> 14, proliferate -> 15 (one counter for the team).
    GameState s = BeContBoard();
    s.players[1].life = 30;
    s.players[1].poison_counters = 13;
    BeAdd(s, kCont, 50);
    RolloutSimulateCombat(s);
    CHECK(s.players[1].poison_counters == 15);
    CHECK(s.players[1].life == 26);
    CHECK(OpponentHasLost(s));
    // 10 poison is NOT a loss in 2HG (CR 704.5c is ignored there; 704.6b).
    GameState t = BeContBoard();
    t.players[1].life = 30;
    t.players[1].poison_counters = 8;
    BeAdd(t, kCont, 50);
    RolloutSimulateCombat(t);
    CHECK(t.players[1].poison_counters == 10);
    CHECK_FALSE(OpponentHasLost(t));
    t.deck_poison_hit_bound = 2;
    t.players[1].poison_counters = 10;
    CHECK_FALSE(PoisonWinReachableThisTurn(t));   // 10 + 4 < 15
    t.players[1].poison_counters = 11;
    CHECK(PoisonWinReachableThisTurn(t));         // 11 + 4 = 15
}

TEST_CASE("Contaminator: the root collapses the menu to the dominant set; MTG_UNPRUNE=proliferate exposes every subset")
{
    GameState s = BeContKinds();
    s.players[1].poison_counters = 1;
    const std::vector<ProliferateCand> cands = ProliferateCandidates(s);
    REQUIRE(cands.size() == 4);   // Herald, Garruk, Migloz, the opponent (the Contaminator has no counter)
    CHECK(ProliferateCollapseFor(s, 0, cands, /*unpruned=*/false));
    CHECK_FALSE(ProliferateCollapseFor(s, 0, cands, /*unpruned=*/true));
    CHECK(std::string(GateName(UnprunedGate::Proliferate)) == "proliferate");
    CHECK(ProliferateDefaultSubset(s, 0, cands) == std::vector<int>{ 0, 1, 2, 3 });
    {
        BeastsProvider provider;
        s.m_provider = &provider;
        const auto menu = ProliferateMenu(s, 0, cands);
        REQUIRE(menu.size() == 1);
        CHECK(menu[0] == std::vector<int>{ 0, 1, 2, 3 });
        // Collapsed: nothing to branch, the axis adds no variant.
        GameState m = s;
        m.phase = Phase::PreCombatMain;
        CHECK(BeProlifCodes(m).empty());
    }
    {
        BeFullProlifProvider provider;   // the unpruned root
        s.m_provider = &provider;
        const auto menu = ProliferateMenu(s, 0, cands);
        CHECK(menu.size() == 16);                                   // every subset of four distinct candidates
        CHECK(menu[0] == std::vector<int>{ 0, 1, 2, 3 });           // the default first
        CHECK(menu.back().empty());                                 // ... "none" (fewest counters) last
        std::set<std::vector<int>> uniq(menu.begin(), menu.end());
        CHECK(uniq.size() == 16);
        // The axis branches the ranks (one Contaminator, 4 candidates + the plan's 0 casts + 1 -> width 31 capped).
        GameState m = s;
        m.phase = Phase::PreCombatMain;
        const std::set<int> codes = BeProlifCodes(m);
        CHECK(codes.size() == 30);
        CHECK(codes.count(BeProlifPin({ 15 })) == 1);
        // A pinned rank resolves that exact subset: rank 15 = "none" -> only the toxic counter lands.
        GameState t = s;
        t.scripted_combat_dig = BeProlifPin({ 15 });
        RolloutSimulateCombat(t);
        CHECK(t.players[1].poison_counters == 2);                   // 1 + toxic, no proliferate
        CHECK(PlusCountersOn(BeNum(t, 51)) == 2);
        CHECK(t.scripted_combat_dig == -1);
    }
    // An UNPROVEN kind refuses the collapse at the root (a -1/-1 counter on our creature): searched, never auto-added.
    GameState u = BeContKinds();
    BeNum(u, 51).counters.clear();
    BeNum(u, 51).counters.Add(Counter::Type::MinusOneMinusOne, 1);
    const std::vector<ProliferateCand> uc = ProliferateCandidates(u);
    CHECK_FALSE(ProliferateCollapseFor(u, 0, uc, false));
    CHECK(ProliferateDefaultSubset(u, 0, uc) == std::vector<int>{ 1, 2 });   // Garruk, Migloz -- never the -1/-1 Herald
    // The two halves of the one pin never disturb each other.
    const int both = CombatPinMerge(BeDigPin({ 2 }), BeProlifPin({ 5, 1 }));
    CHECK(CombatDigRankAt(both, 0) == 2);
    CHECK(ProlifRankAt(both, 0) == 5);
    CHECK(ProlifRankAt(both, 1) == 1);
    CHECK(ProlifRankAt(CombatDigPinShift(both), 0) == 5);
    CHECK(CombatDigRankAt(ProlifPinShift(both), 0) == 2);
    CHECK(ProlifRankAt(ProlifPinShift(both), 0) == 1);
    CHECK(ProlifPinShift(ProlifPinShift(BeProlifPin({ 5, 1 }))) == -1);
}

TEST_CASE("Contaminator: the human proliferate chooser sees every candidate, an empty reply stands")
{
    GameState s = BeContKinds();
    s.players[1].poison_counters = 1;
    std::vector<std::string> names;
    std::vector<int> heur_seen;
    int max_seen = -1;
    DragonChooser chooser = [&](const GameState&, int, const std::string& source, const std::vector<Card>& cands,
                                int max_puts, const std::vector<int>& heur) -> std::vector<int>
    {
        CHECK(source.find("(proliferate)") != std::string::npos);
        for (const Card& c : cands) { names.push_back(c.m_name.str()); }
        heur_seen = heur;
        max_seen = max_puts;
        return {};
    };
    DragonChooser* saved = g_play_dragon_chooser;
    g_play_dragon_chooser = &chooser;
    RolloutSimulateCombat(s);
    g_play_dragon_chooser = saved;
    // The opponent's poison is 2 here (1 + toxic) when the trigger asks; every candidate is offered, the player as a
    // tile named "Opponent".
    CHECK(names == std::vector<std::string>{ "Herald of Ilharg", "Garruk, Primal Hunter", "Migloz, Maze Crusher", "Opponent" });
    CHECK(heur_seen == std::vector<int>{ 0, 1, 2, 3 });
    CHECK(max_seen == 4);
    CHECK(s.players[1].poison_counters == 2);                       // empty reply: proliferate nothing
    CHECK(PlusCountersOn(BeNum(s, 51)) == 2);
    CHECK(BeNum(s, 52).loyalty == 3);
}

// ---- Woodland Bellower (Beasts H) -----------------------------------------------------------------------------------
// "When this creature enters, you may search your library for a nonlegendary green creature card with mana value 3 or
// less, put it onto the battlefield, then shuffle." The Natural Order put route (PerformTutorToBattlefield) fired from
// the creature's OWN ETB; the target is the searched tutor axis, the decline puts nothing and does not shuffle.
namespace
{
constexpr const char* kBell = "Woodland Bellower";
// The test library: the three legal targets and the two legendary / off-filter Beasts in front of BeState's Forests.
GameState BeBellBoard(int forests)
{
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < forests; ++i) { BeAdd(s, "Forest", 10 + i); }
    auto& lib = s.players[0].library;
    const char* front[] = { "Kaheera, the Orphanguard", "Krosan Warchief", "Migloz, Maze Crusher",
                            "Garruk's Harbinger", "Craterhoof Behemoth", "Bloated Contaminator", "Herald of Ilharg" };
    for (int i = static_cast<int>(sizeof(front) / sizeof(front[0])) - 1; i >= 0; --i)
    { lib.insert(lib.begin(), BeZone(front[i], 300 + i)); }
    return s;
}
std::vector<std::string> BeBellCands(const GameState& s)
{
    std::vector<std::string> out;
    for (const std::string& c : RankedTutorCandidates(ResolveProvider(s), s, 0, BeDef(kBell).params))
    { if (std::find(out.begin(), out.end(), c) == out.end()) { out.push_back(c); } }
    return out;
}
bool BeOnBf(const GameState& s, const char* name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("Woodland Bellower: card data, and the filter is green + creature + MV <= 3 + NONLEGENDARY")
{
    const CardDefinition& d = BeDef(kBell);
    CHECK(d.card.m_mana_cost.ManaValue() == 6);
    CHECK(d.card.m_power.value_or(0) == 6);
    CHECK(d.card.m_toughness.value_or(0) == 5);
    CHECK(d.params.tutor_to_battlefield_single);
    CHECK(d.params.tutor_optional);
    CHECK(d.params.tutor_nonlegendary);
    CHECK(d.params.tutor_max_mv == 3);
    CHECK(d.params.tutor_shuffle_after);
    auto ok = [&](const char* nm) { return TutorPutTargetMatches(BeDef(nm).card, d.params); };
    CHECK(ok("Krosan Warchief"));
    CHECK(ok("Garruk's Harbinger"));
    CHECK(ok("Bloated Contaminator"));
    CHECK_FALSE(ok("Kaheera, the Orphanguard"));   // legendary (MV 3, green)
    CHECK_FALSE(ok("Migloz, Maze Crusher"));       // legendary
    CHECK_FALSE(ok("Questing Beast"));             // legendary, MV 4
    CHECK_FALSE(ok("Craterhoof Behemoth"));        // MV 8
    CHECK_FALSE(ok("Herald of Ilharg"));           // MV 4
    CHECK_FALSE(ok("Forest"));                     // not a creature
    // The conjunct is TutorNumericFilterOk's, so every tutor filter site (the providers' TutorCandidates) sees it.
    CHECK_FALSE(TutorNumericFilterOk(BeDef("Kaheera, the Orphanguard").card, d.params));
    CHECK(TutorNumericFilterOk(BeDef("Krosan Warchief").card, d.params));
    // The candidate list over the test library: exactly the three legal names.
    BeastsProvider provider;
    GameState s = BeBellBoard(0);
    s.m_provider = &provider;
    std::vector<std::string> c = BeBellCands(s);
    std::sort(c.begin(), c.end());
    CHECK(c == std::vector<std::string>{ "Bloated Contaminator", "Garruk's Harbinger", "Krosan Warchief" });
}

TEST_CASE("Woodland Bellower: the tutor pin picks EACH candidate, the named target is put, and the put shuffles")
{
    BeastsProvider provider;
    GameState base = BeBellBoard(0);
    base.m_provider = &provider;
    const std::vector<std::string> cands = BeBellCands(base);
    REQUIRE(cands.size() == 3);
    for (int k = 0; k < 3; ++k)
    {
        GameState g = base;
        const int sc = g.search_count;
        {
            ScriptedTutor pin(k);
            BeEnter(g, kBell, 60);
        }
        CHECK(BeOnBf(g, cands[static_cast<std::size_t>(k)].c_str()));
        CHECK(g.battlefield.size() == 2);                       // Bellower + exactly one put
        CHECK(g.players[0].library.size() == base.players[0].library.size() - 1);
        CHECK(g.search_count == sc + 1);                         // "then shuffle"
    }
    // The human menu's named target rides chosen_tutor.
    GameState h = base;
    const int idx = BeAdd(h, kBell, 61, 0, true);
    FireEtbWatchers(h, 0, idx);
    FireOwnEtbTriggers(h, 0, idx, std::string("Bloated Contaminator"), -1);
    CHECK(BeOnBf(h, "Bloated Contaminator"));
}

TEST_CASE("Woodland Bellower: 'you may' -- both decline shapes put nothing and do NOT shuffle")
{
    BeastsProvider provider;
    GameState base = BeBellBoard(0);
    base.m_provider = &provider;
    // (a) the search's decline variant (Plan::tutor_choice = kTutorDeclineChoice).
    GameState g = base;
    {
        ScriptedTutor pin(kTutorDeclineChoice);
        BeEnter(g, kBell, 60);
        CHECK(g_scripted_tutor_choice == -1);   // consumed like any pin
    }
    CHECK(g.battlefield.size() == 1);
    CHECK(g.search_count == base.search_count);
    CHECK(BeLibNums(g) == BeLibNums(base));
    // (b) the human menu's named decline.
    GameState h = base;
    const int idx = BeAdd(h, kBell, 61, 0, true);
    FireOwnEtbTriggers(h, 0, idx, std::string(kTutorDeclineTarget), -1);
    CHECK(h.battlefield.size() == 1);
    CHECK(h.search_count == base.search_count);
    CHECK(BeLibNums(h) == BeLibNums(base));
    // ...while a Natural Order fail-to-find (not optional) still shuffles (byte-identical for the old route).
    GameState n = base;
    PerformTutorToBattlefield(n, 0, BeDef("Natural Order").params, 1, { std::string(kTutorDeclineTarget) }, "Natural Order");
    CHECK(n.search_count == base.search_count + 1);
}

TEST_CASE("Woodland Bellower: only legendary / off-filter creatures left -> nothing is put")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    auto& lib = s.players[0].library;
    lib.insert(lib.begin(), BeZone("Craterhoof Behemoth", 303));
    lib.insert(lib.begin(), BeZone("Questing Beast", 302));
    lib.insert(lib.begin(), BeZone("Migloz, Maze Crusher", 301));
    lib.insert(lib.begin(), BeZone("Kaheera, the Orphanguard", 300));
    BeEnter(s, kBell, 60);
    CHECK(s.battlefield.size() == 1);
    CHECK(lib.size() == 14);
}

TEST_CASE("Woodland Bellower: the put creature's OWN ETB fires (Nest Invader's Spawn), and a put is not a cast")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    BeAdd(s, "Herald of Ilharg", 40);   // "Whenever you cast a creature spell" -- a put is no cast
    auto& lib = s.players[0].library;
    lib.insert(lib.begin(), BeZone("Nest Invader", 300));
    const int spells = s.spells_cast_this_turn;
    BeEnter(s, kBell, 60);
    CHECK(BeIndexOf(s, 300) >= 0);
    int tokens = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token) { ++tokens; } }
    CHECK(tokens == 1);                               // the Eldrazi Spawn
    CHECK(s.spells_cast_this_turn == spells);
    CHECK(PlusCountersOn(s.battlefield[static_cast<std::size_t>(BeIndexOf(s, 40))]) == 0);   // no cast trigger
}

TEST_CASE("Woodland Bellower: Packleader draws for Bellower BEFORE the search, and for the put Harbinger AFTER the shuffle")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    BeAdd(s, "Garruk's Packleader", 40);
    auto& lib = s.players[0].library;
    lib.insert(lib.begin() + 1, BeZone("Garruk's Harbinger", 300));   // under the top Forest (200)
    // The reference: the pre-search draw takes Forest 200, the put removes Harbinger, then the shuffle.
    GameState ref = s;
    ref.players[0].library.erase(ref.players[0].library.begin());
    ref.players[0].library.erase(ref.players[0].library.begin());
    ShuffleAfterSearch(ref, 0);
    const int post_top = ref.players[0].library.front().m_number;
    BeEnter(s, kBell, 60);   // base pin: the provider front = Harbinger (the only legal name)
    REQUIRE(BeIndexOf(s, 300) >= 0);
    const auto& hand = s.players[0].hand;
    REQUIRE(hand.size() == 2);
    CHECK(hand[0].m_number == 200);        // Bellower's own entry: the ordering collapse (draw first)
    CHECK(hand[1].m_number == post_top);   // Harbinger's entry: drawn off the SHUFFLED library
}

TEST_CASE("Woodland Bellower: human re-chooses when Packleader draws the named target first")
{
    BeastsProvider provider;
    GameState s = BeState();
    s.m_provider = &provider;
    BeAdd(s, "Garruk's Packleader", 40);
    auto& lib = s.players[0].library;
    lib.insert(lib.begin(), BeZone("Bloated Contaminator", 301));
    lib.insert(lib.begin(), BeZone("Krosan Warchief", 300));
    const int idx = BeAdd(s, kBell, 60, 0, true);
    FireEtbWatchers(s, 0, idx);             // Packleader draws Warchief
    REQUIRE(s.players[0].hand.front().m_number == 300);
    int calls = 0;
    TutorChooser ask = [&](const GameState&, int, const std::string&,
                           const std::vector<std::string>& choices, int heuristic,
                           const std::vector<int>&) -> int
    {
        ++calls;
        CHECK(heuristic == -1);
        CHECK(choices == std::vector<std::string>{"Bloated Contaminator"});
        return 0;
    };
    TutorChooser* saved = g_play_tutor_chooser;
    g_play_tutor_chooser = &ask;
    FireOwnEtbTriggers(s, 0, idx, "Krosan Warchief", -1);
    g_play_tutor_chooser = saved;
    CHECK(calls == 1);
    CHECK(BeOnBf(s, "Bloated Contaminator"));
    CHECK_FALSE(BeOnBf(s, "Krosan Warchief"));
}

TEST_CASE("Woodland Bellower: every put target is SEARCHED -- one plan per legal name + the decline, no greedy pick")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeBellBoard(6);
    BeHand(s, kBell, 70);
    s.m_provider = &provider;
    std::set<int> choices;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool casts = false;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kBell) { casts = true; } }
        if (casts) { choices.insert(p.tutor_choice); }
    }
    CHECK(choices.count(-1) == 1);                   // base = the provider front
    CHECK(choices.count(1) == 1);
    CHECK(choices.count(2) == 1);
    CHECK(choices.count(kTutorDeclineChoice) == 1);
    CHECK(choices.count(3) == 0);                    // only three legal names
    ManaCacheClearPerGame();
}

TEST_CASE("Woodland Bellower: executor and rollout put the same pinned target")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeBellBoard(6);
    BeHand(s, kBell, 70);
    s.m_provider = &provider;
    const std::vector<std::string> cands = BeBellCands(s);
    REQUIRE(cands.size() == 3);
    const TurnSolver::Plan* pick = nullptr;
    std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.tutor_choice != 2) { continue; }
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kBell) { pick = &p; } }
        if (pick) { break; }
    }
    REQUIRE(pick != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *pick, true);
    CHECK(BeIndexOf(roll, 70) >= 0);
    CHECK(BeOnBf(roll, cands[2].c_str()));
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *pick, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeIndexOf(ex, 70) >= 0);
    CHECK(BeOnBf(ex, cands[2].c_str()));
    ManaCacheClearPerGame();
}

TEST_CASE("Woodland Bellower: a Nykthos line paid by the PUT's devotion is enumerated and both worlds cast it")
{
    // Nykthos + eight Forests, no devotion on board. Bellower {4}{G}{G} off six Forests (devotion 2), the put Harbinger
    // {1}{G}{G} makes it 4: two Forests feed Nykthos for four green = Questing Beast. Nine sources, ten mana: payable
    // ONLY through the put (Bellower's own pips alone leave Nykthos at 2), so a bound / walk that credited the cast
    // and not the put would refuse the pair before any payment could price it.
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeNykBoard(8, {});
    auto& lib = s.players[0].library;
    lib.insert(lib.begin(), BeZone("Garruk's Harbinger", 300));   // the provider front (base plan)
    BeHand(s, kBell, 70);
    BeHand(s, "Questing Beast", 71);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.tutor_choice != -1) { continue; }
        int n = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
        if (n == 2) { both = &p; break; }
    }
    REQUIRE(both != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    CHECK(BeIndexOf(roll, 300) >= 0);
    CHECK(BeIndexOf(roll, 71) >= 0);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, *both, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeIndexOf(ex, 300) >= 0);
    CHECK(BeIndexOf(ex, 71) >= 0);
    ManaCacheClearPerGame();
}

TEST_CASE("Woodland Bellower: a put Krosan Warchief's {1} off a later Beast is credited -- the exact-mana pair is enumerated")
{
    // Ten Forests. Bellower (6) + Thragtusk {4}{G} (5) = 11 printed; the put Warchief makes Thragtusk 4 -> 10.
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 10; ++i) { BeAdd(s, "Forest", 10 + i); }
    auto& lib = s.players[0].library;
    lib.insert(lib.begin(), BeZone("Krosan Warchief", 300));
    BeHand(s, kBell, 70);
    BeHand(s, "Thragtusk", 71);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = BeFindCastPlan(plans, { kBell, "Thragtusk" });
    REQUIRE(both != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    CHECK(BeIndexOf(roll, 300) >= 0);
    CHECK(BeIndexOf(roll, 71) >= 0);
    ManaCacheClearPerGame();
}

TEST_CASE("Kaheera companion: starting-deck restriction selects one outside-game copy")
{
    Decklist eligible;
    eligible.mainboard.push_back(BeZone("Krosan Warchief", 1));
    eligible.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 61));
    eligible.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 62));
    CHECK(KaheeraCompanionEligible(eligible.mainboard));
    GameState s = BeState();
    GoldFishRunner::DealWishPool(s, eligible);
    REQUIRE(s.players[0].sideboard.size() == 1);
    CHECK(s.players[0].sideboard[0].m_number == 300000);

    Decklist ineligible = eligible;
    ineligible.mainboard.push_back(BeZone("Monastery Swiftspear", 2));
    CHECK_FALSE(KaheeraCompanionEligible(ineligible.mainboard));
    GameState no = BeState();
    GoldFishRunner::DealWishPool(no, ineligible);
    CHECK(no.players[0].sideboard.empty());

    // A wish deck still gets its sideboard, but that does not grant companion access.
    ineligible.mainboard.push_back(BeZone("Living Wish", 3));
    GameState wish = BeState();
    GoldFishRunner::DealWishPool(wish, ineligible);
    REQUIRE(wish.players[0].sideboard.size() == 2);
    CHECK_FALSE(CompanionInSideboard(wish, 0, wish.players[0].sideboard[0].m_number));
}

TEST_CASE("Kaheera companion: searched hand and same-turn cast choices match executor and rollout")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    Decklist deck;
    deck.mainboard.push_back(BeZone("Krosan Warchief", 1));
    deck.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 61));
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 6; ++i) { BeAdd(s, "Forest", 10 + i); }
    GoldFishRunner::DealWishPool(s, deck);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (int cast = 0; cast < 2; ++cast)
    {
        const TurnSolver::Plan* pick = nullptr;
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CompanionToHand
                && p.actions[0].chosen_x == cast) { pick = &p; break; }
        }
        REQUIRE_MESSAGE(pick != nullptr, "missing companion choice ", cast);
        TurnSolver::LineSpec spec;
        if (cast) { spec.companion_casts.push_back("Kaheera, the Orphanguard"); }
        else { spec.companion_hands.push_back("Kaheera, the Orphanguard"); }
        const auto check = TurnSolver::CheckLineExact(s, true, spec, &plans);
        CHECK(check.verdict == TurnSolver::LineCheck::Verdict::Accept);

        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *pick, true);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, *pick, true);
        GameState ex = s;
        eng.TakeTurn(ex, true, BeResolveAll);
        CHECK(roll.players[0].sideboard.empty());
        CHECK(ex.players[0].sideboard.empty());
        CHECK(BeTappedLands(roll) == (cast ? 6 : 3));
        CHECK(BeTappedLands(ex) == BeTappedLands(roll));
        CHECK(BeOnBf(roll, "Kaheera, the Orphanguard") == static_cast<bool>(cast));
        CHECK(BeOnBf(ex, "Kaheera, the Orphanguard") == static_cast<bool>(cast));
        CHECK(roll.players[0].hand.size() == static_cast<std::size_t>(cast ? 0 : 1));
        CHECK(ex.players[0].hand.size() == roll.players[0].hand.size());
    }
    ManaCacheClearPerGame();
}

TEST_CASE("Kaheera companion: hand entry opens a searched site-9 continuation, replayed by executor")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    Decklist deck;
    deck.mainboard.push_back(BeZone("Krosan Warchief", 1));
    deck.mainboard.push_back(BeZone("Garruk's Packleader", 2));
    deck.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 61));
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 9; ++i) { BeAdd(s, "Forest", 10 + i); }
    BeAdd(s, "Garruk's Packleader", 30);
    s.players[0].hand.push_back(BeZone("Krosan Warchief", 40));
    GoldFishRunner::DealWishPool(s, deck);
    s.m_provider = &provider;
    std::vector<TurnSolver::Plan> roots = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* hand_only = nullptr;
    for (const TurnSolver::Plan& p : roots)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CompanionToHand
            && p.actions[0].chosen_x == 0) { hand_only = &p; break; }
    }
    REQUIRE(hand_only != nullptr);
    CHECK((TurnSolver::PlanOpensBreakpointForTest(s, *hand_only) & (1 << 9)) != 0);
    const std::vector<std::uint64_t> pre = TurnSolver::SnapshotActivatableAbilities(s);
    GameState reached = s;
    TurnSolver::ApplyPlan(reached, *hand_only, true);
    REQUIRE(reached.players[0].hand.size() == 2);
    CHECK(TurnSolver::PostEntryActivationPending(reached, pre));
    const std::vector<TurnSolver::Plan> continuations = TurnSolver::EnumerateBreakpointPlans(reached, true);
    const TurnSolver::Plan* both = BeFindCastPlan(continuations,
        { "Kaheera, the Orphanguard", "Krosan Warchief" });
    REQUIRE(both != nullptr);
    TurnSolver::Plan selected = *hand_only;
    selected.bp_at = 0;
    selected.bp_choice = static_cast<int>(both - continuations.data());
    GameState roll = s;
    TurnSolver::ApplyPlanWithRecordsForTest(roll, selected, true);
    CHECK(BeOnBf(roll, "Kaheera, the Orphanguard"));
    CHECK(BeOnBf(roll, "Krosan Warchief"));
    {
        const Permanent& warchief = BeNum(roll, 40);
        CHECK(warchief.EffectivePower()
              + ComputeLordBonus(warchief.card, roll, 0, warchief.AnimatedAllTypes(), &warchief).first == 3);
    }
    CHECK(roll.players[0].hand.size() == 2); // Packleader draws for Kaheera and the 3/3 Warchief.
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, selected, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeOnBf(ex, "Kaheera, the Orphanguard"));
    CHECK(BeOnBf(ex, "Krosan Warchief"));
    {
        const Permanent& warchief = BeNum(ex, 40);
        CHECK(warchief.EffectivePower()
              + ComputeLordBonus(warchief.card, ex, 0, warchief.AnimatedAllTypes(), &warchief).first == 3);
    }
    CHECK(ex.players[0].hand.size() == roll.players[0].hand.size());
    CHECK(BeTappedLands(ex) == BeTappedLands(roll));
    ManaCacheClearPerGame();
}

TEST_CASE("Kaheera companion: Warchief before fee and another Beast after Kaheera")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    Decklist deck;
    deck.mainboard.push_back(BeZone("Krosan Warchief", 1));
    deck.mainboard.push_back(BeZone("Garruk's Packleader", 2));
    deck.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 61));
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 10; ++i) { BeAdd(s, "Forest", 10 + i); }
    BeAdd(s, "Garruk's Packleader", 30);
    s.players[0].hand.push_back(BeZone("Krosan Warchief", 40));
    s.players[0].hand.push_back(BeZone("Krosan Warchief", 41));
    GoldFishRunner::DealWishPool(s, deck);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> roots = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* first = nullptr;
    for (const TurnSolver::Plan& p : roots)
    {
        if (p.actions.size() != 2) { continue; }
        bool cast_first = false;
        bool fee = false;
        for (const Action& a : p.actions)
        {
            cast_first |= a.kind == Action::Kind::CastFromHand && a.hand_index == 0
                && a.card_name == "Krosan Warchief";
            fee |= a.kind == Action::Kind::CompanionToHand && a.chosen_x == 0;
        }
        if (cast_first && fee) { first = &p; break; }
    }
    REQUIRE(first != nullptr);
    GameState reached = s;
    TurnSolver::ApplyPlan(reached, *first, true);
    CHECK(BeOnBf(reached, "Krosan Warchief"));
    CHECK_FALSE(BeOnBf(reached, "Kaheera, the Orphanguard"));
    REQUIRE(reached.players[0].hand.size() == 2);
    const std::vector<TurnSolver::Plan> continuations = TurnSolver::EnumerateBreakpointPlans(reached, true);
    const TurnSolver::Plan* last = BeFindCastPlan(continuations,
        { "Kaheera, the Orphanguard", "Krosan Warchief" });
    REQUIRE(last != nullptr);
    TurnSolver::Plan selected = *first;
    selected.bp_at = 0;
    selected.bp_choice = static_cast<int>(last - continuations.data());
    GameState roll = s;
    TurnSolver::ApplyPlanWithRecordsForTest(roll, selected, true);
    CHECK(BeOnBf(roll, "Kaheera, the Orphanguard"));
    CHECK(BeIndexOf(roll, 40) >= 0);
    CHECK(BeIndexOf(roll, 41) >= 0);
    CHECK(roll.players[0].hand.size() == 2); // Kaheera, then the lord-pumped second Warchief.
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, selected, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, BeResolveAll);
    CHECK(BeIndexOf(ex, 40) >= 0);
    CHECK(BeIndexOf(ex, 41) >= 0);
    CHECK(BeOnBf(ex, "Kaheera, the Orphanguard"));
    CHECK(ex.players[0].hand.size() == roll.players[0].hand.size());
    CHECK(BeTappedLands(ex) == BeTappedLands(roll));
    ManaCacheClearPerGame();
}

TEST_CASE("Kaheera and Warchief: mana-tight pair keeps the reducer-first order")
{
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 5; ++i) { BeAdd(s, "Forest", 10 + i); }
    BeAdd(s, "Garruk's Packleader", 30);
    s.players[0].hand.push_back(BeZone("Kaheera, the Orphanguard", 40));
    s.players[0].hand.push_back(BeZone("Krosan Warchief", 41));
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* pair = BeFindCastPlan(plans,
        { "Kaheera, the Orphanguard", "Krosan Warchief" });
    REQUIRE(pair != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *pair, true);
    CHECK(BeOnBf(roll, "Kaheera, the Orphanguard"));
    CHECK(BeOnBf(roll, "Krosan Warchief"));
    CHECK(roll.players[0].hand.size() == 1); // reducer first pays 5 and Kaheera draws once
    ManaCacheClearPerGame();
}

TEST_CASE("Kaheera companion: human menu offers fee then an exact ordinary cast")
{
    // MTG_HUMAN_PLAY is process static. The normal unit run skips this arm;
    // run this case again with MTG_HUMAN_PLAY=1 to test the viewer menu.
    if (!HumanPlayActive()) { return; }
    BeastsProvider provider;
    BeSearchedScope on(true);
    ManaCacheClearPerGame();
    Decklist deck;
    deck.mainboard.push_back(BeZone("Krosan Warchief", 1));
    deck.sideboard.push_back(BeZone("Kaheera, the Orphanguard", 61));
    GameState s = BeState();
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 6; ++i) { BeAdd(s, "Forest", 10 + i); }
    GoldFishRunner::DealWishPool(s, deck);
    s.m_provider = &provider;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* fee = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            const bool misleading_compound = a.kind == Action::Kind::CompanionToHand && a.chosen_x == 1;
            CHECK_FALSE(misleading_compound);
            if (p.actions.size() == 1 && a.kind == Action::Kind::CompanionToHand
                && a.chosen_x == 0) { fee = &p; }
        }
    }
    REQUIRE(fee != nullptr);
    TurnSolver::LineSpec hand;
    hand.companion_hands.push_back("Kaheera, the Orphanguard");
    CHECK(TurnSolver::CheckLineExact(s, true, hand, &plans).verdict
          == TurnSolver::LineCheck::Verdict::Accept);
    TurnSolver::LineSpec compound;
    compound.companion_casts.push_back("Kaheera, the Orphanguard");
    CHECK(TurnSolver::CheckLineExact(s, true, compound, &plans).verdict
          != TurnSolver::LineCheck::Verdict::Accept);
    GameState after = s;
    TurnSolver::ApplyPlan(after, *fee, true);
    REQUIRE(after.players[0].hand.size() == 1);
    CHECK(after.players[0].hand[0].m_name == "Kaheera, the Orphanguard");
    const std::vector<TurnSolver::Plan> next = TurnSolver::EnumerateMainPlans(after, true);
    bool ordinary_cast = false;
    for (const TurnSolver::Plan& p : next)
    for (const Action& a : p.actions)
    { ordinary_cast |= a.kind == Action::Kind::CastFromHand
          && a.card_name == "Kaheera, the Orphanguard"; }
    CHECK(ordinary_cast);

    // With only {3}, the fee is still legal, but the next prompt cannot claim
    // a Kaheera cast. This is the case the old compound button misrepresented.
    GameState three = s;
    three.battlefield.resize(3);
    const std::vector<TurnSolver::Plan> short_menu = TurnSolver::EnumerateMainPlans(three, true);
    const TurnSolver::Plan* short_fee = nullptr;
    for (const TurnSolver::Plan& p : short_menu)
    for (const Action& a : p.actions)
    {
        const bool misleading_compound = a.kind == Action::Kind::CompanionToHand && a.chosen_x == 1;
        CHECK_FALSE(misleading_compound);
        if (p.actions.size() == 1 && a.kind == Action::Kind::CompanionToHand
            && a.chosen_x == 0) { short_fee = &p; }
    }
    REQUIRE(short_fee != nullptr);
    GameState three_after = three;
    TurnSolver::ApplyPlan(three_after, *short_fee, true);
    CHECK(BeTappedLands(three_after) == 3);
    REQUIRE(three_after.players[0].hand.size() == 1);
    const std::vector<TurnSolver::Plan> short_next = TurnSolver::EnumerateMainPlans(three_after, true);
    bool unaffordable_cast = false;
    for (const TurnSolver::Plan& p : short_next)
    for (const Action& a : p.actions)
    { unaffordable_cast |= a.kind == Action::Kind::CastFromHand
          && a.card_name == "Kaheera, the Orphanguard"; }
    CHECK_FALSE(unaffordable_cast);
    ManaCacheClearPerGame();
}
