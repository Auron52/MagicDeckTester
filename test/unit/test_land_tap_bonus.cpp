// Hydras (2026-11-10): the GLOBAL land-tap bonus -- Vernal Bloom ("Whenever a Forest is tapped for mana,
// its controller adds an additional {G}"), Gauntlet of Power (a basic land tapped for the CHOSEN colour adds
// one more of it; creatures of that colour get +1/+1) and Nissa, Who Shakes the World's static ("Whenever
// YOU tap a Forest for mana, add an additional {G}"). A triggered MANA ability (CR 605.1b / 605.4a) folded
// into the land-Aura readers, so these tests pin it at every payer the Badger grant tests pin (the pool, the
// yield, the greedy payer executor and rollout, the backtracker, the payment fail-fast), plus the defects the
// research named: the empty-fold PaymentManaCovers trap, the payability-cache key, the land-put credit, the
// EffectiveProducesFor exclusion, the same-plan credit and the Gauntlet colour axis.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>

namespace
{

int g_ltb_num = 100;

void EnsureCardsLtb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLtb(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutLtb(GameState& s, const std::string& name, int controller = 0, int chosen_color = -1)
{
    Permanent p;
    p.card              = DefLtb(name).card;
    p.card.m_number     = g_ltb_num++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.chosen_color      = static_cast<int8_t>(chosen_color);
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandLtb(GameState& s, const std::string& name)
{
    Card c = DefLtb(name).card; c.m_number = g_ltb_num++; s.players[0].hand.push_back(c);
}

void LibLtb(GameState& s, const std::string& name)
{
    Card c = DefLtb(name).card; c.m_number = g_ltb_num++; s.players[0].library.push_back(c);
}

GameState FreshLtb(int forests)
{
    EnsureCardsLtb();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop: keeps the plan space small
    for (int k = 0; k < forests; ++k) { PutLtb(s, "Forest"); }
    for (int k = 0; k < 8; ++k) { LibLtb(s, "Forest"); }
    return s;
}

ManaCost Generic(int n) { ManaCost c; c.generic = n; return c; }
constexpr int kG = static_cast<int>(Color::Green);
constexpr int kR = static_cast<int>(Color::Red);

const Action* CastOfLtb(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

}   // namespace

TEST_CASE("Land-tap bonus: card data and the shared colour-choice predicates")
{
    EnsureCardsLtb();
    const CardParams& bloom = DefLtb("Vernal Bloom").params;
    CHECK(bloom.land_tap_bonus == 1);
    CHECK(bloom.land_tap_bonus_subtype == "Forest");
    CHECK(bloom.land_tap_bonus_produces == std::vector<Color>{ Color::Green });
    CHECK_FALSE(bloom.land_tap_bonus_you_only);
    const CardParams& g = DefLtb("Gauntlet of Power").params;
    CHECK(g.etb_choose_color);
    CHECK(g.land_tap_bonus_basic);
    CHECK(g.land_tap_bonus_same_color);
    CHECK(g.land_tap_bonus_produces.size() == 5);
    CHECK(g.chosen_color_pump_power == 1);
    CHECK(g.chosen_color_pump_tough == 1);
    CHECK(EtbColorChoiceIsNonSource(g));
    CHECK(&EtbColorMenu(g) == &g.land_tap_bonus_produces);
    CHECK_FALSE(EtbColorChoiceIsNonSource(DefLtb("Coldsteel Heart").params));   // a real source
    const CardParams& nissa = DefLtb("Nissa, Who Shakes the World").params;
    CHECK(nissa.loyalty_start == 5);
    CHECK(nissa.land_tap_bonus_you_only);
    const CardParams& visits = DefLtb("Three Visits").params;
    const CardParams& lore   = DefLtb("Nature's Lore").params;
    CHECK(visits.land_search_on_resolve == lore.land_search_on_resolve);
    CHECK(visits.land_search_count == lore.land_search_count);
    CHECK(visits.land_search_subtype == lore.land_search_subtype);
    CHECK(visits.land_search_tapped == lore.land_search_tapped);
    CHECK(LandSearchPutsUntapped(DefLtb("Three Visits")));
}

TEST_CASE("Vernal Bloom: every payer sees the doubled Forest (pool, yield, greedy x2, backtracker, fail-fast)")
{
    GameState s = FreshLtb(3);
    PutLtb(s, "Vernal Bloom");
    const Permanent& f = s.battlefield[0];
    CHECK(LandAuraBonus(s, f) == 1);
    CHECK(PermanentManaYield(s, f, DefLtb("Forest")) == 2);
    ManaPool b; LandAuraAddToPool(b, s, f);
    CHECK(b.green == 1); CHECK(b.Total() == 1);
    CHECK(LandAuraColorMask(s, f) == (1 << kG));
    CHECK(AvailableManaPool(s).Total() == 6);
    CHECK(AvailableManaPool(s).green == 6);
    // The empty-fold trap: no Aura anywhere, yet {6} is payable. PaymentManaCovers must not refuse it.
    CHECK(PaymentManaCovers(s, false, 6));
    CHECK_FALSE(PaymentManaCovers(s, false, 7));
    // Greedy payer, executor and rollout shapes.
    { GameState t = s; ManaPool pool = AvailableManaPool(t); CHECK(TapForCostShared(t, Generic(6), false, &pool, false)); }
    { GameState t = s; CHECK(TapForCostShared(t, Generic(6), false, nullptr, true)); }
    { GameState t = s; CHECK_FALSE(TapForCostShared(t, Generic(7), false, nullptr, true)); }
    // {5} off three Forests taps all three (2+2+1 -- the third's bonus floats or is spent).
    { GameState t = s; CHECK(TapForCostBacktrack(t, Generic(5), false, ManaPool{})); }
    // Two Blooms stack additively.
    PutLtb(s, "Vernal Bloom");
    CHECK(PermanentManaYield(s, s.battlefield[0], DefLtb("Forest")) == 3);
    CHECK(AvailableManaPool(s).Total() == 9);
}

TEST_CASE("Land-tap bonus: no bonus source -> nothing changes; a stamped-off game skips the walk")
{
    GameState s = FreshLtb(3);
    CHECK(AvailableManaPool(s).Total() == 3);
    PutLtb(s, "Vernal Bloom");
    s.deck_has_land_tap_bonus = false;   // the StampDeckTraits gate: a clear bit = no source in the deck
    CHECK(LandAuraBonus(s, s.battlefield[0]) == 0);
}

TEST_CASE("Gauntlet of Power: only the LOCKED colour, only basics tapped for it; not a mana source")
{
    for (int col : { -1, kR, kG })
    {
        CAPTURE(col);
        GameState s = FreshLtb(3);
        const int gi = PutLtb(s, "Gauntlet of Power", 0, col);
        const int want = col == kG ? 6 : 3;   // an unlocked or red Gauntlet adds nothing to Forests
        CHECK(AvailableManaPool(s).Total() == want);
        // Never a phantom {G} source of its own (the Coldsteel Heart shape is for real sources only).
        CHECK(EffectiveProducesFor(s, 0, DefLtb("Gauntlet of Power"), &s.battlefield[gi]).empty());
    }
    // A red Gauntlet doubles a basic Mountain, not a Forest.
    GameState m = FreshLtb(1);
    PutLtb(m, "Mountain");
    PutLtb(m, "Gauntlet of Power", 0, kR);
    CHECK(LandAuraBonus(m, m.battlefield[0]) == 0);
    CHECK(LandAuraBonus(m, m.battlefield[1]) == 1);
    ManaPool b; LandAuraAddToPool(b, m, m.battlefield[1]);
    CHECK(b.red == 1);
}

TEST_CASE("Gauntlet of Power: +1/+1 to its controller's creatures of the chosen colour only")
{
    GameState s = FreshLtb(1);
    const int elf = PutLtb(s, "Llanowar Elves");
    const Card& c = s.battlefield[elf].card;
    CHECK(ComputeLordBonus(c, s, 0) == std::make_pair(0, 0));
    PutLtb(s, "Gauntlet of Power", 0, kG);
    CHECK(ComputeLordBonus(c, s, 0) == std::make_pair(1, 1));
    PutLtb(s, "Gauntlet of Power", 0, kR);
    CHECK(ComputeLordBonus(c, s, 0) == std::make_pair(1, 1));   // the red one pumps nothing green
    // A land is colourless: a (hypothetically animated) Forest is never pumped.
    CHECK(ComputeLordBonus(s.battlefield[0].card, s, 0) == std::make_pair(0, 0));
    // Unlocked: nothing.
    GameState u = FreshLtb(1);
    const int e2 = PutLtb(u, "Llanowar Elves");
    PutLtb(u, "Gauntlet of Power");
    CHECK(ComputeLordBonus(u.battlefield[e2].card, u, 0) == std::make_pair(0, 0));
}

TEST_CASE("Nissa's static: only Forests HER CONTROLLER taps; Bloom's is symmetric")
{
    GameState s = FreshLtb(2);
    const int theirs = PutLtb(s, "Forest", /*controller=*/1);
    PutLtb(s, "Nissa, Who Shakes the World");
    CHECK(LandAuraBonus(s, s.battlefield[0]) == 1);
    CHECK(LandAuraBonus(s, s.battlefield[theirs]) == 0);
    PutLtb(s, "Vernal Bloom");
    CHECK(LandAuraBonus(s, s.battlefield[0]) == 2);
    CHECK(LandAuraBonus(s, s.battlefield[theirs]) == 1);   // "its controller adds" -- any controller
    // Everything at once: 2 Bloom + 2 green Gauntlet + Nissa = 1 + 2 + 2 + 1 = 6 per Forest tap.
    GameState a = FreshLtb(1);
    PutLtb(a, "Vernal Bloom"); PutLtb(a, "Vernal Bloom");
    PutLtb(a, "Gauntlet of Power", 0, kG); PutLtb(a, "Gauntlet of Power", 0, kG);
    PutLtb(a, "Nissa, Who Shakes the World");
    CHECK(PermanentManaYield(a, a.battlefield[0], DefLtb("Forest")) == 6);
    CHECK(AvailableManaPool(a).green == 6);
}

TEST_CASE("Payability cache: a pre-Bloom 'unpayable' verdict is not replayed post-Bloom")
{
    GameState s = FreshLtb(2);
    { GameState t = s; CHECK_FALSE(TapForCostBacktrack(t, Generic(4), false, ManaPool{})); }
    PutLtb(s, "Vernal Bloom");
    { GameState t = s; CHECK(TapForCostBacktrack(t, Generic(4), false, ManaPool{})); }
    // ...and a red Gauntlet keys apart from a green one.
    GameState r = FreshLtb(2);
    PutLtb(r, "Gauntlet of Power", 0, kR);
    { GameState t = r; CHECK_FALSE(TapForCostBacktrack(t, Generic(4), false, ManaPool{})); }
    r.battlefield.back().chosen_color = static_cast<int8_t>(kG);
    { GameState t = r; CHECK(TapForCostBacktrack(t, Generic(4), false, ManaPool{})); }
}

TEST_CASE("Land-put credit: a fetched Forest under a live Bloom is credited its whole doubled tap")
{
    GameState s = FreshLtb(2);
    const LandSearchSpec spec = LandSearchSpecOf(DefLtb("Three Visits").params);
    std::vector<ManaPool> before = LandSearchCreditGroups(s, 0, spec);
    REQUIRE(before.size() == 1);
    CHECK(before[0].green == 1);
    PutLtb(s, "Vernal Bloom");
    std::vector<ManaPool> after = LandSearchCreditGroups(s, 0, spec);
    REQUIRE(after.size() == 1);
    CHECK(after[0].green == 2);
    CHECK(after[0].Total() == 2);
}

TEST_CASE("Same-plan credit: only the matching lands still untapped after the source's own cost")
{
    GameState s = FreshLtb(6);
    // Bloom {3}{G} off six Forests: four pay it, two remain -> {G}{G}.
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Vernal Bloom")).green == 2);
    // Nissa {3}{G}{G}: five pay, one remains.
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Nissa, Who Shakes the World")).green == 1);
    // Gauntlet {5}: a green pin credits the one Forest left; red, unpinned -> nothing.
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Gauntlet of Power"), kG).green == 1);
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Gauntlet of Power"), kR).Total() == 0);
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Gauntlet of Power"), -1).Total() == 0);
    // A live Bloom already doubles the Forests: a second Bloom costs two of them, four remain.
    PutLtb(s, "Vernal Bloom");
    CHECK(SamePlanLandTapBonusCredit(s, 0, DefLtb("Vernal Bloom")).green == 4);
    // Too few Forests to leave one untapped: no credit.
    GameState t = FreshLtb(4);
    CHECK(SamePlanLandTapBonusCredit(t, 0, DefLtb("Vernal Bloom")).Total() == 0);
}

TEST_CASE("Same-plan credit: 'Bloom, then a 4-drop off the two doubled Forests' is enumerated and payable")
{
    GameState s = FreshLtb(6);
    HandLtb(s, "Vernal Bloom");
    HandLtb(s, "Timeless Witness");
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (CastOfLtb(p, "Vernal Bloom") == nullptr || CastOfLtb(p, "Timeless Witness") == nullptr) { continue; }
        found = true;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        int witness = 0;
        for (const Permanent& q : t.battlefield) { if (q.card.m_name.str() == "Timeless Witness") { ++witness; } }
        CHECK(witness == 1);
    }
    CHECK(found);
}

TEST_CASE("Gauntlet of Power: the colour is a SEARCHED ETB-choice axis; each variant carries its own credit")
{
    GameState s = FreshLtb(6);
    s.deck_has_land_aura = true;
    HandLtb(s, "Gauntlet of Power");
    std::set<int> colours;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = CastOfLtb(p, "Gauntlet of Power");
        if (a == nullptr) { continue; }
        colours.insert(a->chosen_x);
        CHECK(a->rock_mana.green == (a->chosen_x == kG + 1 ? 1 : 0));
        // The rollout locks the pinned colour.
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        const Permanent* gp = nullptr;
        for (const Permanent& q : t.battlefield) { if (q.card.m_name.str() == "Gauntlet of Power") { gp = &q; } }
        REQUIRE(gp != nullptr);
        CHECK(gp->chosen_color == a->chosen_x - 1);
    }
    CHECK(colours == std::set<int>{ 1, 2, 3, 4, 5 });   // Generic provider: the whole menu
    // The executor's stack entry locks the same colour.
    const CardDefinition& def = DefLtb("Gauntlet of Power");
    for (int col : { kR, kG })
    {
        GameState ex = s;
        StackEntry e;
        e.type = StackEntry::EntryType::Spell;
        e.source = def.card; e.source.m_number = 4343;
        e.controller_index = 0;
        e.chosen_x = col + 1;
        EffectHandler::Resolve(ex, e, def);
        const Permanent* gp = nullptr;
        for (const Permanent& q : ex.battlefield) { if (q.card.m_number == 4343) { gp = &q; } }
        REQUIRE(gp != nullptr);
        CHECK(gp->chosen_color == col);
    }
    // Unpinned (a rollout leaf / a put): the provider must still LOCK a colour -- choosing is mandatory.
    // With only Forests around, the generic rule takes green.
    GameState u = FreshLtb(3);
    u.players[0].hand.clear();
    const int gi = PutLtb(u, "Gauntlet of Power");
    FireOwnEtbTriggers(u, 0, gi, "", -1);
    CHECK(u.battlefield[gi].chosen_color == kG);
}

TEST_CASE("HydrasProvider: Gauntlet pruned to green only while the list provably makes no other colour")
{
    GameState s = FreshLtb(2);
    HandLtb(s, "Llanowar Elves");
    const HydrasProvider hp;
    CHECK(hp.EtbChoiceCandidates(s, 0, DefLtb("Gauntlet of Power")) == std::vector<int>{ kG + 1 });
    // A basic Mountain anywhere in the list breaks the proof: the full menu comes back.
    GameState m = s;
    LibLtb(m, "Mountain");
    CHECK(hp.EtbChoiceCandidates(m, 0, DefLtb("Gauntlet of Power")).size() == 5);
    // ...as does a non-green creature card.
    GameState r = s;
    HandLtb(r, "Goblin Guide");
    CHECK(hp.EtbChoiceCandidates(r, 0, DefLtb("Gauntlet of Power")).size() == 5);
    // Other ETB-choice cards are the base fan, unchanged.
    CHECK(hp.EtbChoiceCandidates(s, 0, DefLtb("Trufflesnout")) == std::vector<int>{ 1, 2 });
}
