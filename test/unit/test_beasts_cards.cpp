// Beasts Stage 2 (2026-11-21). Batch A: Garruk's Packleader (power-3 enter draw), Krosan Warchief (Beast
// reducer, stacking with Urza's Incubator), Questing Beast's duplicate-legend offer, Thragtusk (LTB token),
// Cragplate Baloth (kicked counters), Kaheera (lord + vigilance). Batch B: Contested Cliffs (TapFight: the
// searched two-target fight, deathtouch, the SBA death pass). Batch C: Elder Gargaroth (the searched
// three-mode attack trigger, the provider's life fold). Batch D: Garruk's Harbinger. Batch E: Migloz, Maze Crusher
// (oil counters, the searched oil pump, the pruned vigilance grant and destroy). Each test drives the SHARED helpers both worlds
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
    BoarsProvider provider;   // what the list routes to before its own provider lands
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
    BoarsProvider provider;
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
    BoarsProvider provider;   // the deck's current route; its TapFightTargets is the base (all)
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
    BoarsProvider provider;
    s.m_provider = &provider;
    std::vector<int> modes;
    AttackChooseOneSearchModes(s, 0, BeDef(kGarg), modes);
    CHECK(modes == std::vector<int>{ 0, 2 });
}

TEST_CASE("Elder Gargaroth: the mode axis -- two attackers make 4 joint variants after the fold, 9 without it")
{
    BoarsProvider provider;
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
    BoarsProvider provider;
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
    BoarsProvider provider;
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
        BoarsProvider provider;
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
    BoarsProvider provider;
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
