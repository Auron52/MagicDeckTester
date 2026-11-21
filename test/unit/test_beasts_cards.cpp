// Beasts Stage 2, integrator batch A (2026-11-21): Garruk's Packleader (power-3 enter draw), Krosan Warchief
// (Beast reducer, stacking with Urza's Incubator), Questing Beast's duplicate-legend offer (Herald of Ilharg's
// cast trigger, Packleader's enter draw). Each test drives the SHARED helpers both worlds call
// (FireEtbWatchers / FireOwnEtbTriggers, EffectiveSpellCost, OfferDuplicateLegendCast).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
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
