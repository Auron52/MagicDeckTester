// Unit tests for the LandfallElementals "lfe-etb" cluster (2026-10-18):
//
//  1. RISEN REEF (CardParams::enters_look_top_land_or_hand; core/SpellEffects.h LOOK-TOP WATCHERS):
//     "Whenever this creature or another Elemental you control enters, look at the top card of your
//     library. If it's a land card, you may put it onto the battlefield tapped. If you don't put the
//     card onto the battlefield, put it into your hand." Recorded in the enter cascade, resolved at the
//     outermost FireOwnEtbTriggers exit; the land-vs-hand choice is a searched pin (Plan::look_top_pins).
//  2. FLAMEKIN HARBINGER (tutor_to_top Elemental) and its CR 603.3b ORDER against Reef's triggers.
//  3. FOUNDATION BREAKER (etb_destroy_artifact_enchantment_optional + evoke): the evoked body still
//     enters (Reef triggers) and the optional destroy declines our own artifacts.
//  4. FULMINATOR MAGE (self-only sac outlet + destroy target nonbasic land; hybrid {B/R}{B/R}).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "core/EffectHandler.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

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

const CardDefinition& DefLfe(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardLfe(const std::string& name, int number)
{
    Card c = DefLfe(name).card;
    c.m_number = number;
    return c;
}

constexpr const char* kReef      = "Risen Reef";
constexpr const char* kHarbinger = "Flamekin Harbinger";
constexpr const char* kBreaker   = "Foundation Breaker";
constexpr const char* kFulm      = "Fulminator Mage";
constexpr const char* kMimic     = "Metallic Mimic";
constexpr const char* kVial      = "Aether Vial";

GameState FreshLfe()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    // The land drop is SPENT unless a test says otherwise: the provider's default for a land on top is
    // then "battlefield" (an open drop with no land in hand defaults to "hand" -- tested separately).
    s.players[0].lands_played_this_turn = 1;
    return s;
}

void PutLfe(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = CardLfe(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
}

void LibLfe(GameState& s, const std::vector<std::string>& names, int first_number)
{
    int n = first_number;
    for (const std::string& name : names) { s.players[0].library.push_back(CardLfe(name, n++)); }
}

// Make `name` ENTER the way a resolved creature spell / a Vial put does: push it, run the universal
// enter cascade (watchers RECORD), then the entrant's own ETBs (the outermost exit DRAINS).
void EnterLfe(GameState& s, const std::string& name, int number, const std::string& tutor = "")
{
    PutLfe(s, name, number);
    const int slot = static_cast<int>(s.battlefield.size()) - 1;
    FireEtbWatchers(s, 0, slot);
    FireOwnEtbTriggers(s, 0, slot, tutor);
}

int CountNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

const Permanent* FindNamed(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

}   // namespace

TEST_CASE("lfe-etb cards: printed data and params")
{
    EnsureCardsLfe();
    const CardDefinition& r = DefLfe(kReef);
    CHECK(r.card.m_mana_cost.generic == 1);
    CHECK(r.card.m_mana_cost.green == 1);
    CHECK(r.card.m_mana_cost.blue == 1);
    CHECK(r.card.m_power == 1);
    CHECK(r.card.m_toughness == 1);
    CHECK(CardHasSubtype(r.card, "Elemental"));
    CHECK(r.params.enters_look_top_land_or_hand);
    CHECK(r.params.creature_enters_includes_self);
    CHECK(r.enter_watcher);   // the FireCreatureEnterWatchers byte must see it

    const CardDefinition& h = DefLfe(kHarbinger);
    CHECK(h.card.m_mana_cost.red == 1);
    CHECK(h.card.m_mana_cost.ManaValue() == 1);
    CHECK(CardHasSubtype(h.card, "Elemental"));
    CHECK(CardHasSubtype(h.card, "Shaman"));
    CHECK(h.params.tutor_to_top);
    CHECK(h.params.tutor_optional);

    const CardDefinition& b = DefLfe(kBreaker);
    CHECK(b.card.m_mana_cost.ManaValue() == 4);
    REQUIRE(b.params.evoke_cost.has_value());
    CHECK(b.params.evoke_cost->ManaValue() == 2);
    CHECK(b.params.etb_destroy_artifact_enchantment_optional);

    const CardDefinition& f = DefLfe(kFulm);
    CHECK(f.card.m_mana_cost.ManaValue() == 3);
    CHECK(f.card.m_mana_cost.hybrid_count == 2);   // {B/R}{B/R}: real either-colour pips
    CHECK(f.card.HasColor(Color::Black));
    CHECK(f.card.HasColor(Color::Red));
    CHECK(f.params.sac_creature_outlet);
    CHECK(f.params.sac_outlet_self_only);
    CHECK(f.params.sac_outlet_destroy_nonbasic_land);
    CHECK(CardHasSubtype(f.card, "Elemental"));
}

TEST_CASE("Risen Reef: its OWN entry looks at the top -- a land goes onto the battlefield tapped, landfall fires")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, "Rampaging Baloths", 2);   // landfall: a 4/4 Beast per land entering
    LibLfe(s, { "Forest", "Island" }, 10);
    EnterLfe(s, kReef, 1);
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library[0].m_name.str() == "Island");
    const Permanent* f = FindNamed(s, "Forest");
    REQUIRE(f != nullptr);
    CHECK(f->tapped);                                   // "onto the battlefield TAPPED"
    CHECK(s.players[0].lands_played_this_turn == 1);    // a put, not a land drop
    int beasts = 0;
    for (const Permanent& p : s.battlefield) { if (p.is_token && CardHasSubtype(p.card, "Beast")) { ++beasts; } }
    CHECK(beasts == 1);                                 // landfall fired through EnterLand
    CHECK(g_pending_look_top.empty());
}

TEST_CASE("Risen Reef: a NONLAND top goes into the hand (a put, not a draw)")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    LibLfe(s, { kHarbinger, "Forest" }, 10);
    const int drawn_before = s.players[0].cards_drawn_this_turn;
    EnterLfe(s, kReef, 1);
    CHECK(InHand(s, kHarbinger));
    CHECK(s.players[0].cards_drawn_this_turn == drawn_before);
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library[0].m_name.str() == "Forest");
}

TEST_CASE("Risen Reef: another ELEMENTAL entering triggers it; a non-Elemental does not")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    LibLfe(s, { "Mountain", kHarbinger, "Forest" }, 10);
    EnterLfe(s, "Monastery Swiftspear", 2);   // a Human Monk: no trigger
    CHECK(s.players[0].library.size() == 3);
    EnterLfe(s, kFulm, 3);                    // an Elemental Shaman: Reef looks
    CHECK(s.players[0].library.size() == 2);
    CHECK(CountNamed(s, "Mountain") == 1);
}

TEST_CASE("Risen Reef: two Reefs both trigger; a Reef entering beside another triggers both")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, kReef, 2);
    LibLfe(s, { "Forest", "Island", "Mountain", "Forest", "Forest" }, 10);
    EnterLfe(s, kFulm, 3);                    // two watchers -> two looks
    CHECK(s.players[0].library.size() == 3);
    EnterLfe(s, kReef, 4);                    // its own trigger + the two others -> three looks
    CHECK(s.players[0].library.size() == 0);
    CHECK(CountNamed(s, "Forest") == 3);
}

TEST_CASE("Risen Reef: a Metallic Mimic that chose Elemental IS an Elemental as it enters")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, kFulm, 2);
    PutLfe(s, kHarbinger, 3);
    LibLfe(s, { "Forest", "Island" }, 10);
    EnterLfe(s, kMimic, 4);
    const Permanent* m = FindNamed(s, kMimic);
    REQUIRE(m != nullptr);
    CHECK(CardHasSubtype(m->card, "Elemental"));
    CHECK(s.players[0].library.size() == 1);  // the Reef looked
}

TEST_CASE("Risen Reef: the searched land pin -- 1 puts the land into the hand, 0 onto the battlefield")
{
    EnsureCardsLfe();
    {
        GameState s = FreshLfe();
        LibLfe(s, { "Forest", "Island" }, 10);
        const std::vector<int> pins{ 1 };
        ScriptedLookTop scope(pins);
        EnterLfe(s, kReef, 1);
        CHECK(InHand(s, "Forest"));
        CHECK(CountNamed(s, "Forest") == 0);
    }
    {
        // An OPEN land drop with no land in hand and an untapped land on top: the provider's DEFAULT
        // is "hand" (play it untapped this turn) -- and the pin 0 overrides it onto the battlefield.
        GameState s = FreshLfe();
        s.players[0].lands_played_this_turn = 0;
        LibLfe(s, { "Forest", "Island" }, 10);
        GameState d = s;
        EnterLfe(d, kReef, 1);
        CHECK(InHand(d, "Forest"));
        const std::vector<int> pins{ 0 };
        ScriptedLookTop scope(pins);
        EnterLfe(s, kReef, 1);
        CHECK(CountNamed(s, "Forest") == 1);
        CHECK_FALSE(InHand(s, "Forest"));
    }
}

TEST_CASE("Risen Reef: a landfall Elemental TOKEN made mid-drain triggers the Reef again (re-entrant drain)")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, "Omnath, Locus of Rage", 2);   // landfall: a 5/5 Elemental token
    LibLfe(s, { "Forest", "Mountain", kHarbinger, "Island" }, 10);
    // The Fulminator's entry: the Reef puts Forest (landfall -> an Elemental token -> the Reef looks
    // again) -> Mountain (another token, another look) -> Harbinger to hand; Island stays on top.
    EnterLfe(s, kFulm, 3);
    CHECK(CountNamed(s, "Forest") == 1);
    CHECK(CountNamed(s, "Mountain") == 1);
    CHECK(InHand(s, kHarbinger));
    REQUIRE(s.players[0].library.size() == 1);
    CHECK(s.players[0].library[0].m_name.str() == "Island");
    CHECK(g_pending_look_top.empty());
    CHECK_FALSE(g_look_top_draining);
}

TEST_CASE("Flamekin Harbinger alone: tutors an Elemental card to the TOP")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    EnterLfe(s, kHarbinger, 1, kFulm);
    REQUIRE(!s.players[0].library.empty());
    CHECK(s.players[0].library[0].m_name.str() == kFulm);
    CHECK(s.players[0].library.size() == 4);
}

TEST_CASE("Flamekin Harbinger + Risen Reef: the DEFAULT order resolves the search first -- the found card to hand")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    EnterLfe(s, kHarbinger, 2, kFulm);
    CHECK(InHand(s, kFulm));                     // tutor to top, then the Reef took it
    CHECK(s.players[0].library.size() == 3);
    CHECK(CountNamed(s, "Forest") == 0);         // the old top was not looked at
}

TEST_CASE("Flamekin Harbinger + Risen Reef: ORDER pin k=1 -- the Reef takes today's top, the found card waits on top")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    const std::vector<int> pins{ kLookTopOrderBase + 1 + 1 };
    ScriptedLookTop scope(pins);
    EnterLfe(s, kHarbinger, 2, kFulm);
    CHECK(CountNamed(s, "Forest") == 1);         // the Reef looked first: Forest onto the battlefield
    CHECK_FALSE(InHand(s, kFulm));
    REQUIRE(!s.players[0].library.empty());
    CHECK(s.players[0].library[0].m_name.str() == kFulm);   // then the search put it on top
}

TEST_CASE("Flamekin Harbinger + TWO Reefs: the default order takes today's top AND the found card")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, kReef, 2);
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    EnterLfe(s, kHarbinger, 3, kFulm);
    CHECK(CountNamed(s, "Forest") == 1);         // first Reef: today's top
    CHECK(InHand(s, kFulm));                     // search, then the second Reef takes the found card
}

TEST_CASE("Flamekin Harbinger's tutor with NO Reef out is resolved inline (nothing deferred)")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    LibLfe(s, { "Forest", kFulm }, 10);
    PutLfe(s, kHarbinger, 1);
    const int slot = static_cast<int>(s.battlefield.size()) - 1;
    FireEtbWatchers(s, 0, slot);
    CHECK(g_pending_look_top.empty());
    FireOwnEtbTriggers(s, 0, slot, kFulm);
    CHECK(s.players[0].library[0].m_name.str() == kFulm);
}

TEST_CASE("Foundation Breaker: the optional destroy DECLINES our own artifacts")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kVial, 1);
    PutLfe(s, kMimic, 2);
    LibLfe(s, { "Forest" }, 10);
    EnterLfe(s, kBreaker, 3);
    CHECK(CountNamed(s, kVial) == 1);
    CHECK(CountNamed(s, kMimic) == 1);
    CHECK(CountNamed(s, kBreaker) == 1);
}

TEST_CASE("Foundation Breaker: an OPPONENT's artifact is destroyed")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    Permanent v;
    v.card = CardLfe(kVial, 50);
    v.controller_index = 1;
    v.owner_index = 1;
    s.battlefield.push_back(v);
    PutLfe(s, kVial, 1);
    EnterLfe(s, kBreaker, 3);
    int opp_vials = 0, my_vials = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_name.str() != kVial) { continue; }
        (p.controller_index == 1 ? opp_vials : my_vials) += 1;
    }
    CHECK(opp_vials == 0);
    CHECK(my_vials == 1);
}

TEST_CASE("Foundation Breaker: EVOKED it still enters -- the Reef looks -- and is then sacrificed")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, "Forest", 20);
    PutLfe(s, "Forest", 21);
    s.players[0].hand.push_back(CardLfe(kBreaker, 5));
    LibLfe(s, { kHarbinger, "Island", "Mountain" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* evoke = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1 || !p.look_top_pins.empty()) { continue; }
        if (p.actions[0].card_name.str() == kBreaker && p.actions[0].evoke) { evoke = &p; break; }
    }
    REQUIRE(evoke != nullptr);
    TurnSolver::ApplyPlan(s, *evoke, true);
    CHECK(CountNamed(s, kBreaker) == 0);         // sacrificed on entry
    bool in_gy = false;
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == kBreaker) { in_gy = true; } }
    CHECK(in_gy);
    CHECK(InHand(s, kHarbinger));                // ...but the Reef saw it enter
}

TEST_CASE("Search axis: an Elemental cast with a Reef out fans look-top land variants")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    for (int i = 0; i < 3; ++i) { PutLfe(s, "Mountain", 20 + i); }
    s.players[0].hand.push_back(CardLfe(kFulm, 5));
    LibLfe(s, { "Forest", "Island" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool base = false, to_bf = false, to_hand = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool fulm = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == kFulm) { fulm = true; } }
        if (!fulm) { continue; }
        if (p.look_top_pins.empty()) { base = true; continue; }
        if (p.look_top_pins[0] == 0) { to_bf = true; }
        if (p.look_top_pins[0] == 1) { to_hand = true; }
    }
    CHECK(base);
    CHECK(to_bf);
    CHECK(to_hand);
    // ...and the hand variant really does put the Forest into the hand.
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.look_top_pins.size() != 1 || p.look_top_pins[0] != 1) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        CHECK(InHand(t, "Forest"));
        CHECK(CountNamed(t, "Forest") == 0);
        break;
    }
}

TEST_CASE("Search axis: Harbinger cast with a Reef out fans the ORDER (and both orders apply)")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, "Mountain", 20);
    s.players[0].hand.push_back(CardLfe(kHarbinger, 5));
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool k0 = false, k1 = false;
    for (const TurnSolver::Plan& p : plans)
    {
        for (int e : p.look_top_pins)
        {
            if (e == kLookTopOrderBase + 1 + 0) { k0 = true; }
            if (e == kLookTopOrderBase + 1 + 1) { k1 = true; }
        }
    }
    CHECK(k0);
    CHECK(k1);
}

TEST_CASE("Fulminator Mage: hybrid {B/R}{B/R} is castable off Mountains alone (the second colour)")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    for (int i = 0; i < 3; ++i) { PutLfe(s, "Mountain", 20 + i); }
    s.players[0].hand.push_back(CardLfe(kFulm, 5));
    LibLfe(s, { "Mountain", "Mountain" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool cast = false;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (a.card_name.str() == kFulm) { cast = true; } } }
    CHECK(cast);
}

TEST_CASE("Fulminator Mage: the sacrifice ability is not offered to the search without a death payoff")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, kFulm, 1);
    PutLfe(s, "Secluded Courtyard", 20);   // a legal (our own) nonbasic land target exists
    for (int i = 0; i < 2; ++i) { PutLfe(s, "Mountain", 21 + i); }
    LibLfe(s, { "Mountain" }, 10);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::SacCreatureOutlet); } }
}

TEST_CASE("Fulminator Mage: 'destroy target nonbasic land' -- basics are never legal")
{
    EnsureCardsLfe();
    GameState s = FreshLfe();
    PutLfe(s, "Mountain", 20);
    CHECK_FALSE(AnyNonbasicLand(s));
    ResolveDestroyTargetNonbasicLand(s, 0, kFulm);
    CHECK(CountNamed(s, "Mountain") == 1);
    PutLfe(s, "Secluded Courtyard", 21, /*tapped=*/true);
    CHECK(AnyNonbasicLand(s));
    ResolveDestroyTargetNonbasicLand(s, 0, kFulm);
    CHECK(CountNamed(s, "Secluded Courtyard") == 0);
    CHECK(CountNamed(s, "Mountain") == 1);
}

TEST_CASE("Risen Reef: the SEARCH finds 'land to hand, play it, cast more' -- and the executor realises it")
{
    // Vial(1) puts Flamekin Harbinger for free -> the Reef looks at a Mountain. The provider's default
    // is BATTLEFIELD (a Gruul Turf is already in hand), which leaves two Bolts + the Reef's attack = 7.
    // The kill (opponent at 9) needs the land pin's HAND variant AND the site-10 continuation that plays
    // the Mountain as the land drop and casts the third Bolt.
    EnsureCardsLfe();
    GameState s = FreshLfe();
    s.players[0].lands_played_this_turn = 0;
    s.players[1].life = 9;
    PutLfe(s, "Mountain", 20);
    PutLfe(s, "Mountain", 21);
    PutLfe(s, kReef, 22);
    PutLfe(s, kVial, 23);
    s.battlefield.back().charge_counters = 1;
    s.players[0].hand.push_back(CardLfe(kHarbinger, 1));
    s.players[0].hand.push_back(CardLfe("Lightning Bolt", 2));
    s.players[0].hand.push_back(CardLfe("Lightning Bolt", 3));
    s.players[0].hand.push_back(CardLfe("Lightning Bolt", 4));
    s.players[0].hand.push_back(CardLfe("Gruul Turf", 5));
    LibLfe(s, { "Mountain", "Swords to Plowshares", "Swords to Plowshares", "Swords to Plowshares" }, 10);
    {
        const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, true, 2);
        bool hand_pin = false;
        for (int e : pick.look_top_pins) { if (e == 1) { hand_pin = true; } }
        CHECK(hand_pin);
        GameState t = s;
        TurnSolver::ApplyPlan(t, pick, true);
        CHECK(t.players[1].life <= 0);
    }
    {
        GameState t = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 2, 20);
        eng.TakeTurn(t, true, [](GameState& st) {
            while (!st.stack.empty())
            {
                const StackEntry e = st.stack.back();
                st.stack.pop_back();
                const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
                if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
            }
        });
        CHECK(t.players[1].life <= 0);
        CHECK(t.players[0].lands_played_this_turn == 1);
        CHECK(CountNamed(t, "Mountain") == 3);
    }
}

namespace
{
struct DigChooserScope
{
    DigChooser fn; DigChooser* saved;
    explicit DigChooserScope(DigChooser f) : fn(std::move(f)), saved(g_play_dig_chooser) { g_play_dig_chooser = &fn; }
    ~DigChooserScope() { g_play_dig_chooser = saved; }
};
struct LoyaltyChooserScope
{
    LoyaltyTargetChooser fn; LoyaltyTargetChooser* saved;
    explicit LoyaltyChooserScope(LoyaltyTargetChooser f) : fn(std::move(f)), saved(g_play_loyalty_chooser) { g_play_loyalty_chooser = &fn; }
    ~LoyaltyChooserScope() { g_play_loyalty_chooser = saved; }
};
}   // namespace

TEST_CASE("Human play: the '(look-top land)' dig asks only for a LAND top, and -1 puts it into the hand")
{
    EnsureCardsLfe();
    std::vector<std::string> asked;
    DigChooserScope scope([&](const GameState&, int, const std::string& src, const std::vector<Card>& ex,
                              const std::vector<int>& legal, int dflt) -> int
    {
        asked.push_back(src);
        CHECK(ex.size() == 1);
        CHECK(legal == std::vector<int>{ 0 });
        CHECK(dflt == 0);   // drop spent -> the default is the battlefield
        return -1;
    });
    GameState s = FreshLfe();
    LibLfe(s, { "Forest", kFulm }, 10);
    EnterLfe(s, kReef, 1);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0].find("(look-top land)") != std::string::npos);
    CHECK(InHand(s, "Forest"));
    // A nonland top: no question at all.
    EnterLfe(s, kFulm, 2);
    CHECK(asked.size() == 1);
    CHECK(InHand(s, kFulm));
}

TEST_CASE("Human play: the '(trigger order)' dig is asked UP FRONT and places the search")
{
    EnsureCardsLfe();
    std::vector<int> answers{ 1, 0 };   // a Reef look first, then the search (k = 1 of 2)
    int orders = 0;
    DigChooserScope scope([&](const GameState&, int, const std::string& src, const std::vector<Card>& ex,
                              const std::vector<int>&, int) -> int
    {
        if (src.find("(trigger order)") == std::string::npos) { return 0; }   // land looks: battlefield
        CHECK(ex.size() == 2);
        CHECK(ex[0].m_name.str() == kHarbinger);
        CHECK(ex[1].m_name.str() == kReef);
        return answers[static_cast<std::size_t>(orders++)];
    });
    GameState s = FreshLfe();
    PutLfe(s, kReef, 1);
    PutLfe(s, kReef, 2);
    LibLfe(s, { "Forest", "Island", kFulm, "Mountain" }, 10);
    EnterLfe(s, kHarbinger, 3, kFulm);
    CHECK(orders == 2);
    CHECK(CountNamed(s, "Forest") == 1);   // the first Reef took today's top (a land -> battlefield)
    CHECK(InHand(s, kFulm));               // the search, then the second Reef took the found card
}

TEST_CASE("Human play: '(may destroy)' offers our own artifacts and honours the pick")
{
    EnsureCardsLfe();
    int asked = 0;
    DigChooserScope scope([&](const GameState&, int, const std::string& src, const std::vector<Card>& ex,
                              const std::vector<int>& legal, int dflt) -> int
    {
        if (src.find("(may destroy)") == std::string::npos) { return -1; }
        ++asked;
        CHECK(ex.size() == 2);          // Aether Vial + Metallic Mimic (an artifact creature)
        CHECK(legal.size() == 2);
        CHECK(dflt == -1);              // the default declines our own
        for (std::size_t i = 0; i < ex.size(); ++i) { if (ex[i].m_name.str() == kVial) { return static_cast<int>(i); } }
        return -1;
    });
    GameState s = FreshLfe();
    PutLfe(s, kVial, 1);
    PutLfe(s, kMimic, 2);
    EnterLfe(s, kBreaker, 3);
    CHECK(asked == 1);
    CHECK(CountNamed(s, kVial) == 0);
    CHECK(CountNamed(s, kMimic) == 1);
}

TEST_CASE("Human play: Fulminator's 'destroy target nonbasic land' is the board-click target over every nonbasic")
{
    EnsureCardsLfe();
    int asked = 0;
    LoyaltyChooserScope scope([&](const GameState& st, int, const std::string& src, const std::string& prompt,
                                  const std::vector<int>& legal, int) -> int
    {
        ++asked;
        CHECK(src == kFulm);
        CHECK(prompt.find("nonbasic land") != std::string::npos);
        CHECK(legal.size() == 2);       // Secluded Courtyard + Gruul Turf; the Mountain is basic
        for (std::size_t i = 0; i < legal.size(); ++i)
        { if (st.battlefield[static_cast<std::size_t>(legal[i])].card.m_name.str() == "Gruul Turf") { return static_cast<int>(i); } }
        return -1;
    });
    GameState s = FreshLfe();
    PutLfe(s, "Mountain", 20);
    PutLfe(s, "Secluded Courtyard", 21);
    PutLfe(s, "Gruul Turf", 22);
    ResolveDestroyTargetNonbasicLand(s, 0, kFulm);
    CHECK(asked == 1);
    CHECK(CountNamed(s, "Gruul Turf") == 0);
    CHECK(CountNamed(s, "Secluded Courtyard") == 1);
}
