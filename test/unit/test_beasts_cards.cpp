// Beasts Stage 2 (2026-11-21). Batch A: Garruk's Packleader (power-3 enter draw), Krosan Warchief (Beast
// reducer, stacking with Urza's Incubator), Questing Beast's duplicate-legend offer, Thragtusk (LTB token),
// Cragplate Baloth (kicked counters), Kaheera (lord + vigilance). Batch B: Contested Cliffs (TapFight: the
// searched two-target fight, deathtouch, the SBA death pass). Each test drives the SHARED helpers both worlds
// call (FireEtbWatchers / FireOwnEtbTriggers, EffectiveSpellCost, OfferDuplicateLegendCast, ApplyPermAbility,
// EnumerateMainPlans + ApplyPlan).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
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
