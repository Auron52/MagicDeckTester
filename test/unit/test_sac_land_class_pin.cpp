// The searched sacrifice-a-land pin (Plan::sac_pins, MTG_SAC_AXIS) indexes the DISTINCT land classes
// of the provider's ranking, each represented by its tapped copy where it has one (SacLandClassRanking,
// core/SpellEffects.h). Burn mana-clean pass1 (100k games): the pin indexed the RAW tapped-first ranking,
// so on a board of Mountains "pin 1" after a one-land payment named the first UNTAPPED Mountain -- Shard
// Volley ate the land the turn's next spell needed while a tapped Mountain survived (seed 30000241 gi 241
// T3: land, Shard Volley, Shard Volley, Light Up the Stage dropped Light Up the Stage; 2270 APPLY-DROP +
// ~465 ORACLE-GAP flags). Every check below has a control that the old raw indexing fails.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsSLC()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefS(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutS(GameState& s, const std::string& name, int number, bool tapped)
{
    Permanent p;
    p.card             = DefS(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
}

bool OnBattlefield(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}

// Sacrifice one land under the pin list `pins` and return the m_number that left.
int Sacrificed(GameState s, const std::vector<int>& pins)
{
    std::vector<int> before;
    for (const Permanent& p : s.battlefield) { before.push_back(p.card.m_number); }
    {
        ScriptedSacLand pin(pins);
        PerformSacrificeLandCost(s, "Shard Volley");
    }
    for (int n : before) { if (!OnBattlefield(s, n)) { return n; } }
    return -1;
}

const BurnProvider& ProvS()
{
    static const BurnProvider p;
    return p;
}

struct PinArm
{
    heurarm::Slot s;
    PinArm(heurarm::Slot slot, bool on) : s(slot) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~PinArm() { heurarm::t_arm[s] = -1; }
};
}   // namespace

TEST_CASE("sac-land pin: a pin past the front never takes an untapped twin of a tapped land")
{
    EnsureCardsSLC();
    GameState s;
    s.active_player_index = 0;
    s.m_provider          = &DefaultProvider();
    PutS(s, "Mountain", 10, /*tapped=*/false);
    PutS(s, "Mountain", 11, /*tapped=*/true);    // the land the payment tapped
    PutS(s, "Mountain", 12, /*tapped=*/false);
    CHECK(Sacrificed(s, {}) == 11);              // the provider's front: tapped first
    CHECK(Sacrificed(s, { 0 }) == 11);
    // One class only: every pin is the tapped Mountain (the raw ranking's rank 1 was untapped #10).
    CHECK(Sacrificed(s, { 1 }) == 11);
    CHECK(Sacrificed(s, { 3 }) == 11);
}

TEST_CASE("sac-land pin: the pin walks the distinct classes, so a different land is one step away")
{
    EnsureCardsSLC();
    GameState s;
    s.active_player_index = 0;
    s.m_provider          = &DefaultProvider();
    PutS(s, "Mountain", 10, /*tapped=*/true);
    PutS(s, "Mountain", 11, /*tapped=*/false);
    PutS(s, "Mountain", 12, /*tapped=*/false);
    PutS(s, "Forest",   13, /*tapped=*/false);
    CHECK(Sacrificed(s, {}) == 10);
    // Raw rank 1 and 2 are the untapped Mountains and the Forest sat at rank 3, past the axis's width
    // (distinct names + 1 = 3) -- unreachable. Over the classes it is pin 1.
    CHECK(Sacrificed(s, { 1 }) == 13);
    CHECK(Sacrificed(s, { 2 }) == 13);           // clamped to the last class
    // Two pins, two sacrifices: the second ordinal's pin is read at its own state.
    {
        GameState t = s;
        const std::vector<int> pins = { -1, 1 };   // the scope holds a POINTER to the list
        ScriptedSacLand pin(pins);
        PerformSacrificeLandCost(t, "Shard Volley");
        CHECK_FALSE(OnBattlefield(t, 10));
        PerformSacrificeLandCost(t, "Shard Volley");
        CHECK_FALSE(OnBattlefield(t, 13));       // the untapped Mountains are one class: the Forest is next
        CHECK(OnBattlefield(t, 11));
        CHECK(OnBattlefield(t, 12));
    }
}

TEST_CASE("sac-land pin: twins keep apart what the cost can tell apart")
{
    EnsureCardsSLC();
    GameState s;
    s.active_player_index = 0;
    s.m_provider          = &DefaultProvider();
    PutS(s, "Mountain", 10, /*tapped=*/true);
    PutS(s, "Mountain", 11, /*tapped=*/false);
    s.battlefield[1].is_token = true;            // a token Mountain is not the card's twin
    CHECK(Sacrificed(s, { 1 }) == 11);
    GameState c = s;
    c.battlefield[1].is_token = false;
    c.battlefield[1].counters.Add(Counter::Type::PlusOnePlusOne, 1);   // nor is one with a counter
    CHECK(Sacrificed(c, { 1 }) == 11);
}

TEST_CASE("sac-land pin, burn seed 30000241 T3: land, Shard Volley x2, Light Up the Stage casts all three under every pin")
{
    // Board: Mountain x2, Goblin Guide, Monastery Swiftspear; hand: Shard Volley x2, Mountain, Skullcrack,
    // Light Up the Stage. Three Mountains pay three {R}: Volley, Volley, then Light Up for spectacle {R}.
    // Under the raw indexing the plan's pin-1 variant tapped the second Mountain for the second Volley and
    // sacrificed the THIRD (untapped), so Light Up the Stage had no mana.
    EnsureCardsSLC();
    PinArm o(heurarm::BURN_ORDER, true);
    PinArm c(heurarm::SPECTACLE_PLAN_CREDIT, true);
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &ProvS();
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.uses_second_main    = true;
    s.deck_has_dredge     = false;   // the burn deck's own stamp (no dredge card): no land position after a draw cast
    int num = 1;
    for (const char* n : { "Shard Volley", "Shard Volley", "Mountain", "Skullcrack", "Light Up the Stage" })
    {
        Card k = DefS(n).card;
        k.m_number = num++;
        s.players[0].hand.push_back(k);
    }
    PutS(s, "Mountain", 43, false);
    PutS(s, "Goblin Guide", 6, false);
    PutS(s, "Mountain", 40, false);
    PutS(s, "Monastery Swiftspear", 20, false);
    for (int k = 0; k < 20; ++k) { Card m = DefS("Mountain").card; m.m_number = 100 + k; s.players[0].library.push_back(m); }

    auto casts = [](const TurnSolver::Plan& p, const char* name)
    {
        int n = 0;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { ++n; } }
        return n;
    };
    int variants = 0, pinned = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (casts(p, "Shard Volley") != 2 || casts(p, "Light Up the Stage") != 1 || p.actions.size() != 3
            || p.land_to_play != "Mountain") { continue; }
        ++variants;
        bool any_pin = false;
        for (int x : p.sac_pins) { if (x > 0) { any_pin = true; } }
        if (any_pin) { ++pinned; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        bool luts_in_hand = false;
        for (const Card& k : t.players[0].hand) { if (k.m_name.str() == "Light Up the Stage") { luts_in_hand = true; } }
        CHECK_FALSE_MESSAGE(luts_in_hand, "a plan variant dropped Light Up the Stage (pins: ",
                            p.sac_pins.size(), ")");
    }
    CHECK(variants > 0);
    CHECK(pinned > 0);    // the pinned variants are on the menu, so the check above covers them
}
