// Unit tests for the LandfallElementals "lfe-fury" cluster (2026-10-18): Smokebraider's restricted
// any-combination mana, Incandescent Soulstoke's lord + put-from-hand (haste, delayed sacrifice) and
// Fury's double strike, divided-damage ETB and pitch evoke. They pin what a game log cannot show
// directly: which payments the restricted mana may make (executor and rollout twins), that a put
// creature ENTERS (watchers + own ETB) and dies at the end step, that the pitch choice is a searched
// axis that never spends one card twice, and that the evoked body enters before it is sacrificed.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/SubtypeRegistry.h"

#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLfe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefL(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int g_nl = 7000;

GameState BoardL()
{
    EnsureCardsLfe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    // An Elemental library, so Metallic Mimic's chosen type (DominantCreatureSubtypeId) is Elemental
    // exactly as in a LandfallElementals game, and a draw never decks us.
    // Smokebraider and Soulstoke are both Elemental SHAMANS, so Fury (Elemental Incarnation) is mixed
    // in: Elemental must be the strict plurality, as it is in the real list.
    for (int k = 0; k < 12; ++k)
    {
        Card c = DefL(k % 3 == 0 ? "Smokebraider" : (k % 3 == 1 ? "Incandescent Soulstoke" : "Fury")).card;
        c.m_number = g_nl++;
        s.players[0].library.push_back(c);
    }
    return s;
}

int PutL(GameState& s, const std::string& name, int controller = 0, bool sick = false, bool tapped = false)
{
    Permanent p;
    p.card              = DefL(name).card;
    p.card.m_number     = g_nl++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

// Put through the universal enter cascade (Mimic's type choice runs there).
int EnterL(GameState& s, const std::string& name)
{
    const int n = PutL(s, name, 0, /*sick=*/true);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    return n;
}

int HandL(GameState& s, const std::string& name)
{
    Card c = DefL(name).card;
    c.m_number = g_nl++;
    s.players[0].hand.push_back(c);
    return c.m_number;
}

int IdxL(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == num) { return i; } }
    return -1;
}
const Permanent* ByNumL(const GameState& s, int num)
{
    const int i = IdxL(s, num);
    return i < 0 ? nullptr : &s.battlefield[static_cast<std::size_t>(i)];
}
int CountOnBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}
template <class Zone>
int CountZone(const Zone& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}
int PlusCountersL(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

ManaCost CostL(int generic, int w = 0, int u = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}

// Both payment twins on copies of `board`: the executor (AIEngine::TapForCost) and the rollout
// (TapForCostDirect). The caller sets the payment context (spell / ability scope) around the call.
struct PayL { bool ok = false; GameState after; };
PayL ExecPay(const GameState& board, const ManaCost& c, bool for_creature)
{
    PayL r; r.after = board;
    AIEngine eng;
    ManaPool avail = AvailableManaPool(r.after);
    r.ok = MtgTestSeam::TapForCost(eng, r.after, c, avail, for_creature);
    return r;
}
PayL RollPay(const GameState& board, const ManaCost& c, bool for_creature)
{
    PayL r; r.after = board;
    r.ok = TapForCostDirect(r.after, c, for_creature);
    return r;
}
// Pay as the SPELL `spell` (its printed card is the paying-spell identity).
bool BothPaySpell(const GameState& board, const ManaCost& c, const std::string& spell)
{
    const CardDefinition& d = DefL(spell);
    SpellSubtypePayScope scope(&d.card);
    const PayL ex = ExecPay(board, c, d.card.IsCreature());
    const PayL ro = RollPay(board, c, d.card.IsCreature());
    CHECK(ex.ok == ro.ok);
    return ex.ok && ro.ok;
}

std::vector<TurnSolver::Plan> PlansL(const GameState& s)
{
    return TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
}

}   // namespace

// ================================ SMOKEBRAIDER ================================

TEST_CASE("Smokebraider: two mana in ANY combination pays an Elemental's {R}{G} alone; refuses a non-Elemental")
{
    GameState s = BoardL();
    PutL(s, "Smokebraider");
    // {R}{G} from one source: neither a one-of-each bundle (it would make WUBRG) nor a one-colour burst.
    CHECK(BothPaySpell(s, CostL(0, 0, 0, 0, 1, 1), "Fury"));
    CHECK(BothPaySpell(s, CostL(0, 0, 1, 0, 0, 1), "Incandescent Soulstoke"));   // {U}{G}, still an Elemental
    // Metallic Mimic is a SHAPESHIFTER in hand: its {2} may not be paid with this mana.
    CHECK_FALSE(BothPaySpell(s, CostL(2), "Metallic Mimic"));
    // Three mana is beyond one tap.
    CHECK_FALSE(BothPaySpell(s, CostL(1, 0, 0, 0, 1, 1), "Fury"));
}

TEST_CASE("Smokebraider: summoning sickness gates its {T} mana ability")
{
    GameState s = BoardL();
    PutL(s, "Smokebraider", 0, /*sick=*/true);
    CHECK_FALSE(BothPaySpell(s, CostL(0, 0, 0, 0, 1), "Fury"));
}

TEST_CASE("Smokebraider: pays a hybrid {B/R} Elemental pip as either half")
{
    GameState s = BoardL();
    PutL(s, "Smokebraider");
    PutL(s, "Mountain");
    // {1}{B/R}{B/R} (Fulminator Mage's shape): the first colour is baked into the flat count, the pair
    // rides the hybrid metadata -- exactly what the card parser builds.
    ManaCost c;
    c.generic = 1; c.black = 2; c.hybrid_count = 2;
    c.hybrid_pair[0] = static_cast<uint8_t>((static_cast<int>(Color::Black) << 4) | static_cast<int>(Color::Red));
    c.hybrid_pair[1] = c.hybrid_pair[0];
    CHECK(BothPaySpell(s, c, "Fury"));
}

TEST_CASE("Smokebraider: pays an ELEMENTAL's activated ability, never an unnamed one (Vial, a land)")
{
    GameState s = BoardL();
    PutL(s, "Smokebraider");
    const int soul = PutL(s, "Incandescent Soulstoke");
    const Card* src = AbilitySourceCard(s, 0, soul);
    REQUIRE(src != nullptr);
    {
        CreatureAbilityPayScope scope(src);
        CHECK(ExecPay(s, CostL(1, 0, 0, 0, 1), false).ok);
        CHECK(RollPay(s, CostL(1, 0, 0, 0, 1), false).ok);
    }
    // No source named (Aether Vial's tick, Myriad Landscape's {2}): refused.
    CHECK_FALSE(ExecPay(s, CostL(1, 0, 0, 0, 1), false).ok);
    CHECK_FALSE(RollPay(s, CostL(1, 0, 0, 0, 1), false).ok);
    CHECK_FALSE(RollPay(s, CostL(1), false).ok);
    // A NON-Elemental creature source (a Metallic Mimic that has not chosen Elemental): refused.
    GameState t = s;
    const int mimic = PutL(t, "Metallic Mimic");
    {
        CreatureAbilityPayScope scope(AbilitySourceCard(t, 0, mimic));
        CHECK_FALSE(RollPay(t, CostL(1), false).ok);
    }
    // ...but a Mimic that ENTERED naming Elemental is an Elemental on the battlefield.
    GameState u = s;
    const int mimic_e = EnterL(u, "Metallic Mimic");
    CHECK(CardHasSubtype(ByNumL(u, mimic_e)->card, "Elemental"));
    {
        CreatureAbilityPayScope scope(AbilitySourceCard(u, 0, mimic_e));
        CHECK(RollPay(u, CostL(1), false).ok);
    }
}

TEST_CASE("Smokebraider: an unspent unit floats WILD under its own restriction -- never into Metallic Mimic")
{
    GameState s = BoardL();
    PutL(s, "Smokebraider");
    GameState after;
    {
        SpellSubtypePayScope scope(&DefL("Fury").card);
        REQUIRE(TapForCostDirect(s, CostL(0, 0, 0, 0, 1), /*for_creature=*/true));   // {R}: one unit left
    }
    CHECK(s.floating_mana.Total() == 0);            // not laundered into general float
    CHECK(s.floating_creature_mana.Total() == 0);   // nor into the any-creature reserve
    CHECK(s.floating_bigspell_mana.wild == 1);      // the restricted reserve, any colour
    CHECK(s.floating_bigspell_pp == &DefL("Smokebraider").params);
    CHECK_FALSE(BothPaySpell(s, CostL(1), "Metallic Mimic"));             // Shapeshifter in hand
    CHECK(BothPaySpell(s, CostL(0, 0, 0, 0, 0, 1), "Incandescent Soulstoke"));   // {G}: wild pays it
    CHECK(RestrictedFloatUsableNow(s, /*for_creature=*/false) == false);   // an unnamed ability: no
}

// ============================ INCANDESCENT SOULSTOKE ============================

TEST_CASE("Soulstoke: other Elementals get +1/+1, not itself; a Mimic that chose Elemental is pumped")
{
    GameState s = BoardL();
    const int soul  = PutL(s, "Incandescent Soulstoke");
    const int smoke = PutL(s, "Smokebraider");
    const int mimic = EnterL(s, "Metallic Mimic");
    const Permanent& ps = *ByNumL(s, soul);
    const Permanent& pk = *ByNumL(s, smoke);
    const Permanent& pm = *ByNumL(s, mimic);
    CHECK(ComputeLordBonus(ps.card, s, 0, false, &ps).first == 0);
    CHECK(ComputeLordBonus(pk.card, s, 0, false, &pk).first == 1);
    CHECK(ComputeLordBonus(pk.card, s, 0, false, &pk).second == 1);
    CHECK(ComputeLordBonus(pm.card, s, 0, false, &pm).first == 1);
}

TEST_CASE("Soulstoke: one put variant per distinct Elemental CREATURE card; none while summoning-sick")
{
    GameState s = BoardL();
    PutL(s, "Incandescent Soulstoke");
    PutL(s, "Mountain"); PutL(s, "Mountain");
    HandL(s, "Fury"); HandL(s, "Fury"); HandL(s, "Smokebraider"); HandL(s, "Metallic Mimic");
    std::set<std::string> put;
    int fury_power = -1;
    for (const TurnSolver::Plan& p : PlansL(s))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::PutFromHandAbility) { continue; }
            put.insert(a.card_name.str());
            if (a.card_name.str() == "Fury") { fury_power = a.vial_attack_power; }
        }
    }
    CHECK(put == std::set<std::string>{ "Fury", "Smokebraider" });   // never the Shapeshifter
    // The hasted put's this-turn attack stamp: 3 + 1 (the lord) = 4, double strike = 8.
    CHECK(fury_power == 8);

    GameState t = BoardL();
    PutL(t, "Incandescent Soulstoke", 0, /*sick=*/true);
    PutL(t, "Mountain"); PutL(t, "Mountain");
    HandL(t, "Fury");
    for (const TurnSolver::Plan& p : PlansL(t))
    { for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::PutFromHandAbility); } }
}

TEST_CASE("Soulstoke: Smokebraider alone pays the put -- the search offers it and the rollout realises it")
{
    GameState s = BoardL();
    const int soul  = PutL(s, "Incandescent Soulstoke");
    const int smoke = PutL(s, "Smokebraider");
    HandL(s, "Fury");
    const TurnSolver::Plan* put_plan = nullptr;
    std::vector<TurnSolver::Plan> plans = PlansL(s);
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::PutFromHandAbility && a.card_name.str() == "Fury") { put_plan = &p; } }
    }
    REQUIRE(put_plan != nullptr);
    TurnSolver::ApplyPlan(s, *put_plan, /*is_pre_combat=*/true);
    CHECK(ByNumL(s, smoke)->tapped);
    CHECK(ByNumL(s, soul)->tapped);
    REQUIRE(CountOnBf(s, "Fury") == 1);
}

TEST_CASE("Soulstoke: the put creature ENTERS (watchers + own ETB), has haste, and dies at the end step")
{
    GameState s = BoardL();
    const int soul  = PutL(s, "Incandescent Soulstoke");
    const int mimic = EnterL(s, "Metallic Mimic");   // chose Elemental: "each other Elemental enters with a counter"
    s.battlefield[static_cast<std::size_t>(IdxL(s, mimic))].entered_this_turn = false;
    const int fury  = HandL(s, "Fury");
    REQUIRE(ApplyPutFromHand(s, 0, soul, "Fury"));
    const Permanent* pf = ByNumL(s, fury);
    REQUIRE(pf != nullptr);
    CHECK(pf->temp_haste);
    CHECK(pf->sac_at_end_step);
    CHECK(PlusCountersL(*pf) == 1);                  // Mimic's as-enters counter fired on the put
    CHECK(CanAttackFull(*pf, s.battlefield, 0));     // hasty the turn it arrives
    CHECK(ByNumL(s, soul)->tapped);
    CHECK(CountZone(s.players[0].hand, "Fury") == 0);
    // A non-Elemental card is not a legal put (the creature/type rider holds at apply too).
    HandL(s, "Metallic Mimic");
    GameState t = s;
    t.battlefield[static_cast<std::size_t>(IdxL(t, soul))].tapped = false;
    CHECK_FALSE(ApplyPutFromHand(t, 0, soul, "Metallic Mimic"));
    // The delayed trigger: sacrificed at the beginning of the end step; Soulstoke and Mimic stay.
    PerformEndStepDelayedSacrifices(s);
    CHECK(CountOnBf(s, "Fury") == 0);
    CHECK(CountZone(s.players[0].graveyard, "Fury") == 1);
    CHECK(ByNumL(s, soul) != nullptr);
    CHECK(ByNumL(s, mimic) != nullptr);
}

// ===================================== FURY =====================================

TEST_CASE("Fury: double strike -- a lone attacking Fury deals 6")
{
    GameState s = BoardL();
    const int fury = PutL(s, "Fury");
    const int life0 = s.players[1].life;
    ResolveCombatDamage(s, { IdxL(s, fury) }, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK(s.players[1].life == life0 - 6);
}

TEST_CASE("Fury: pitch evoke is offered once per distinct RED hand card (another Fury yes, colourless no)")
{
    GameState s = BoardL();
    HandL(s, "Fury");
    HandL(s, "Fury");
    HandL(s, "Smokebraider");
    HandL(s, "Smokebraider");     // two copies: still ONE variant (copies are interchangeable)
    HandL(s, "Metallic Mimic");   // colourless: not a red card
    std::set<std::string> pitched;
    for (const TurnSolver::Plan& p : PlansL(s))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || !a.evoke) { continue; }
            CHECK(a.cost.ManaValue() == 0);
            CHECK(!a.pitch_name.empty());
            CHECK(a.pitch_slot != a.hand_index);   // never pitches the copy being cast
            pitched.insert(a.pitch_name.str());
        }
    }
    CHECK(pitched == std::set<std::string>{ "Fury", "Smokebraider" });

    // No other red card: the evoke cannot be paid, so no variant exists (and with no mana, no cast).
    GameState t = BoardL();
    HandL(t, "Fury");
    HandL(t, "Metallic Mimic");
    for (const TurnSolver::Plan& p : PlansL(t))
    { for (const Action& a : p.actions) { CHECK_FALSE(a.evoke); } }
}

TEST_CASE("Fury: a plan never casts AND pitches the same copy")
{
    GameState s = BoardL();
    PutL(s, "Mountain"); PutL(s, "Mountain");
    HandL(s, "Fury");
    HandL(s, "Smokebraider");   // the ONLY red card besides Fury, and castable ({1}{R})
    bool saw_evoke = false, saw_cast_smoke = false;
    for (const TurnSolver::Plan& p : PlansL(s))
    {
        bool evoke_pitch_smoke = false, cast_smoke = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.evoke && a.pitch_name.str() == "Smokebraider") { evoke_pitch_smoke = true; }
            if (a.card_name.str() == "Smokebraider") { cast_smoke = true; }
        }
        saw_evoke      = saw_evoke || evoke_pitch_smoke;
        saw_cast_smoke = saw_cast_smoke || cast_smoke;
        CHECK_FALSE((evoke_pitch_smoke && cast_smoke));
    }
    CHECK(saw_evoke);
    CHECK(saw_cast_smoke);
}

TEST_CASE("Fury evoked (rollout ApplyPlan): the red card is exiled, Fury ENTERS, then is sacrificed")
{
    GameState s = BoardL();
    const int mimic = EnterL(s, "Metallic Mimic");
    (void)mimic;
    HandL(s, "Fury");
    HandL(s, "Smokebraider");
    const TurnSolver::Plan* evoke_plan = nullptr;
    std::vector<TurnSolver::Plan> plans = PlansL(s);
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions[0];
        if (a.kind == Action::Kind::CastFromHand && a.evoke && a.pitch_name.str() == "Smokebraider")
        { evoke_plan = &p; }
    }
    REQUIRE(evoke_plan != nullptr);
    TurnSolver::ApplyPlan(s, *evoke_plan, /*is_pre_combat=*/true);
    CHECK(CountZone(s.players[0].hand, "Fury") == 0);
    CHECK(CountZone(s.players[0].hand, "Smokebraider") == 0);
    CHECK(CountZone(s.exile, "Smokebraider") == 1);       // the pitch
    CHECK(CountOnBf(s, "Fury") == 0);                     // entered, then the evoke sacrifice
    CHECK(CountZone(s.players[0].graveyard, "Fury") == 1);
    CHECK(s.spells_cast_this_turn == 1);                  // it WAS cast
}

TEST_CASE("Fury evoked (executor CastSpellFromHand + resolution): same cost, same enter-then-die")
{
    GameState s = BoardL();
    HandL(s, "Fury");
    HandL(s, "Smokebraider");
    AIEngine eng;
    ManaPool avail = AvailableManaPool(s);
    Card* fury_card = nullptr;
    for (Card& c : s.players[0].hand) { if (c.m_name.str() == "Fury") { fury_card = &c; } }
    REQUIRE(fury_card != nullptr);
    MtgTestSeam::CastEvokeFromHand(eng, s, *fury_card, avail, "Smokebraider");
    CHECK(CountZone(s.players[0].hand, "Fury") == 0);
    CHECK(CountZone(s.exile, "Smokebraider") == 1);
    REQUIRE(!s.stack.empty());
    const StackEntry entry = s.stack.back();
    s.stack.pop_back();
    CHECK(entry.evoke);
    EffectHandler::Resolve(s, entry, DefL("Fury"));
    CHECK(CountOnBf(s, "Fury") == 0);
    CHECK(CountZone(s.players[0].graveyard, "Fury") == 1);

    // No pitch card left in hand: the evoke is not castable -- nothing moves, nothing is paid.
    GameState t = BoardL();
    HandL(t, "Fury");
    Card* f2 = &t.players[0].hand.back();
    ManaPool av2 = AvailableManaPool(t);
    MtgTestSeam::CastEvokeFromHand(eng, t, *f2, av2, "Smokebraider");
    CHECK(CountZone(t.players[0].hand, "Fury") == 1);
    CHECK(t.stack.empty());
}

TEST_CASE("Fury ETB: no targets by default; with an opponent-death payoff it kills opposing creatures")
{
    GameState s = BoardL();
    const int smoke = PutL(s, "Smokebraider");
    // Two opposing spawns, 1/1 and 3/3 (the goldfish's removal-target bodies).
    auto spawn = [&](int pw)
    {
        Card t;
        t.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
        t.RehashName();
        t.AddType(CardType::Creature);
        t.m_power = pw; t.m_toughness = pw; t.m_number = g_nl++;
        Permanent p; p.card = t; p.controller_index = 1; p.owner_index = 1;
        s.battlefield.push_back(p);
        return t.m_number;
    };
    const int o1 = spawn(1), o3 = spawn(3);
    GameState a = s;
    const int fury = EnterL(a, "Fury");
    FireOwnEtbTriggers(a, 0, IdxL(a, fury));
    CHECK(ByNumL(a, o1) != nullptr);          // the dominance fold: no damage anywhere
    CHECK(ByNumL(a, o3) != nullptr);
    CHECK(ByNumL(a, smoke)->damage == 0);
    CHECK(ByNumL(a, fury)->damage == 0);

    // With Massacre Wurm out each opposing death drains 2 -- the default kills both (1 + 3 = 4).
    GameState b = s;
    PutL(b, "Massacre Wurm");
    const int life_b = b.players[1].life;
    const int fury_b = EnterL(b, "Fury");
    FireOwnEtbTriggers(b, 0, IdxL(b, fury_b));
    CHECK(ByNumL(b, o1) == nullptr);
    CHECK(ByNumL(b, o3) == nullptr);
    CHECK(ByNumL(b, smoke)->damage == 0);     // never our own
    CHECK(b.players[1].life <= life_b - 4);
}

// ================== NO CARD-LOSING ACTION ON A NO-GAIN TIE (the pass is Plan::value -1) ==================

TEST_CASE("Fury: an evoke-for-nothing plan ranks strictly BELOW the pass (Plan::value < -1)")
{
    // The probe defect (seed 787 gi10): at -1 an evoke tied the pass, sorted ahead of it in
    // MoveOrderPlans' stable order and won every no-gain tie in the main-2 loop.
    GameState s = BoardL();
    HandL(s, "Fury");
    HandL(s, "Fury");
    HandL(s, "Smokebraider");
    int evoke_plans = 0;
    for (const bool pre : { true, false })
    {
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, pre))
        {
            bool only_evokes = !p.actions.empty();
            for (const Action& a : p.actions) { if (!(a.kind == Action::Kind::CastFromHand && a.evoke)) { only_evokes = false; } }
            if (!only_evokes) { continue; }
            ++evoke_plans;
            CHECK(p.value < -1);
        }
    }
    CHECK(evoke_plans > 0);
}

TEST_CASE("Soulstoke: AFTER combat the put has no attack stamp and ranks below the pass")
{
    GameState s = BoardL();
    PutL(s, "Incandescent Soulstoke");
    PutL(s, "Mountain"); PutL(s, "Mountain");
    HandL(s, "Fury");
    bool seen_pre = false, seen_post = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::PutFromHandAbility) { seen_pre = true; CHECK(a.vial_attack_power == 8); } }
    }
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
    {
        if (p.actions.size() != 1 || p.actions[0].kind != Action::Kind::PutFromHandAbility) { continue; }
        seen_post = true;
        CHECK(p.actions[0].vial_attack_power == 0);
        CHECK(p.value < -1);
    }
    CHECK(seen_pre);
    CHECK(seen_post);
}
