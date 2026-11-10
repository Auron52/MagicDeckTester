// Shadowborn Apostle onboarding, integrator INT-1 (2026-11-03; docs/design/analysis-Shadowborn
// Apostle.md): the generic "sacrifice a creature" additional cost (Diabolic Intent / Culling the
// Weak), the morbid tracker, Lake of the Dead's victim choice, Profane Tutor's off-suspend tutor pin,
// and the deck's routing to ShadowbornProvider.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
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
void EnsureCardsSb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card FullCardSb(const std::string& name, int number)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Card c     = def->card;
    c.m_number = number;
    return c;
}

// Hand cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PlaceholderSb(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

Permanent PermSb(const std::string& name, int number, bool fresh = false, bool tapped = false)
{
    Permanent p;
    p.card              = FullCardSb(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    p.tapped            = tapped;
    return p;
}

// INT-9's fixed answers OFF (the MTG_UNPRUNED audit arm's shape): these tests exercise the searched victim fan and
// the readiness-route mapping; INT-9's one-victim order is pinned in test_shadowborn_int9.cpp.
struct SearchedProv1 : ShadowbornProvider
{
    bool SacCostVictimFixed(const GameState&) const override { return false; }
    bool RippleFixed(const GameState&) const override { return false; }
    bool NoLandPlanDominated(const GameState&) const override { return false; }
};
const ShadowbornProvider& Prov() { static const SearchedProv1 p; return p; }

GameState BaseState()
{
    EnsureCardsSb();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &Prov();
    return s;
}

int CountCasts(const std::vector<Action>& acts, const std::string& name)
{
    int n = 0;
    for (const Action& a : acts)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Shadowborn routing: the deck lands on ShadowbornProvider, width 32, not AntiLifegain")
{
    EnsureCardsSb();
    const Decklist deck = DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Shadowborn Apostle/Shadowborn Apostle.cod"));
    const DecisionProvider& p = DetectDecisionProvider(deck);
    CHECK(std::string(p.Name()) == "ShadowbornApostle");
    CHECK(p.TutorSearchWidth() == 32);
}

TEST_CASE("Morbid: a creature death sets the flag, a land sacrifice does not, the rollout turn clears it")
{
    GameState s = BaseState();
    s.battlefield.push_back(PermSb("Swamp", 10));
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 11));
    CHECK_FALSE(s.creature_died_this_turn);
    SacrificeLandAt(s, 0, 0);                    // the Swamp: a land leaving is no creature death
    CHECK_FALSE(s.creature_died_this_turn);
    REQUIRE(s.battlefield.size() == 1);
    SacrificePermanentAt(s, 0, 0);               // the Vampire
    CHECK(s.creature_died_this_turn);
    for (int k = 0; k < 40; ++k) { s.players[0].library.push_back(FullCardSb("Swamp", 100 + k)); }
    TurnSolver::SimulateEndAndStartNextTurnForTest(s);
    CHECK_FALSE(s.creature_died_this_turn);
}

TEST_CASE("Readiness victim fold: a fresh copy is offered instead of an attack-ready one of the same identity")
{
    GameState s = BaseState();
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 20));                 // attack-ready
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 21, /*fresh=*/true)); // summoning sick
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 22, /*fresh=*/true)); // identical to 21
    const std::vector<int> reps = SacReadinessVictimReps(s, 0);
    REQUIRE(reps.size() == 1);
    CHECK(s.battlefield[reps[0]].card.m_number == 21);
    // Two attack-ready copies only: one class, and it is offered (nothing dominates it).
    GameState t = BaseState();
    t.battlefield.push_back(PermSb("Vampire of the Dire Moon", 30));
    t.battlefield.push_back(PermSb("Vampire of the Dire Moon", 31));
    CHECK(SacReadinessVictimReps(t, 0).size() == 1);
    // A tapped body and a sick untapped body are different classes (neither dominates).
    GameState u = BaseState();
    u.battlefield.push_back(PermSb("Vampire of the Dire Moon", 40, false, /*tapped=*/true));
    u.battlefield.push_back(PermSb("Vampire of the Dire Moon", 41, /*fresh=*/true));
    CHECK(SacReadinessVictimReps(u, 0).size() == 2);
}

TEST_CASE("Culling the Weak: uncastable with no creature; battlefield and same-plan hand victims otherwise")
{
    GameState s = BaseState();
    s.battlefield.push_back(PermSb("Swamp", 50));
    s.players[0].hand.push_back(PlaceholderSb("Culling the Weak", 51));
    CHECK(CountCasts(TurnSolver::CollectActionsForTest(s, true), "Culling the Weak") == 0);

    // A creature in HAND: one variant, pinned to it, carrying the co-cast requirement.
    s.players[0].hand.push_back(PlaceholderSb("Vampire of the Dire Moon", 52));
    {
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        int n = 0;
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Culling the Weak") { continue; }
            ++n;
            CHECK(a.soulfire_own_targets == 52);
            CHECK(a.trick_hand_target == "Vampire of the Dire Moon");
        }
        CHECK(n == 1);
    }
    // ...plus an attack-ready one on the battlefield: two variants (the board body, the hand body).
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 53));
    {
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        std::vector<int> victims;
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Culling the Weak")
            { victims.push_back(a.soulfire_own_targets); }
        }
        std::sort(victims.begin(), victims.end());
        CHECK(victims == std::vector<int>{ 52, 53 });
    }
}

TEST_CASE("Readiness route payment: a hand-pinned victim maps onto the copy the plan actually cast")
{
    GameState s = BaseState();
    s.players[0].hand.push_back(PlaceholderSb("Vampire of the Dire Moon", 60));   // the pinned copy (still in hand)
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 61));               // attack-ready
    s.battlefield.push_back(PermSb("Vampire of the Dire Moon", 62, /*fresh=*/true)); // the copy the plan cast
    CHECK(ResolveSacReadinessVictimId(s, 60) == 62);
    PerformSacrificeCreatureCost(s, "Culling the Weak", std::string{}, 60, /*readiness_route=*/true);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_number == 61);   // the attack-ready body survives
    CHECK(s.creature_died_this_turn);
}

TEST_CASE("Lake of the Dead: a basic Swamp is sacrificed before Blood Crypt; Crypt when it is the only Swamp")
{
    GameState s = BaseState();
    s.battlefield.push_back(PermSb("Blood Crypt", 70));
    s.battlefield.push_back(PermSb("Swamp", 71));
    s.battlefield.push_back(PermSb("Swamp", 72, false, /*tapped=*/true));
    const int v = ChooseSubtypeLandSacrifice(s, 0, "Swamp", 0, "Lake of the Dead");
    REQUIRE(v >= 0);
    CHECK(s.battlefield[v].card.m_name.str() == "Swamp");
    GameState t = BaseState();
    t.battlefield.push_back(PermSb("Blood Crypt", 73));
    const int w = ChooseSubtypeLandSacrifice(t, 0, "Swamp", 0, "Lake of the Dead");
    REQUIRE(w >= 0);
    CHECK(t.battlefield[w].card.m_name.str() == "Blood Crypt");
    GameState u = BaseState();
    u.battlefield.push_back(PermSb("Lake of the Dead", 74));   // Lake is not a Swamp
    CHECK(ChooseSubtypeLandSacrifice(u, 0, "Swamp", 0, "Lake of the Dead") < 0);
    // ...so a second Lake with only a Lake in play is a victimless drop the autonomous choice skips.
    const CardDefinition* lake = CardDatabase::Instance().Lookup("Lake of the Dead");
    REQUIRE(lake != nullptr);
    CHECK(LakeDropWithoutVictim(u, *lake));
    CHECK_FALSE(LakeDropWithoutVictim(t, *lake));   // Blood Crypt is a Swamp
    CHECK_FALSE(LakeDropWithoutVictim(u, *CardDatabase::Instance().Lookup("Swamp")));   // not a Lake
}

TEST_CASE("Lake of the Dead's crack taps the Lake, sacrifices another Swamp tapped-first, floats BBBB")
{
    GameState s = BaseState();
    s.battlefield.push_back(PermSb("Lake of the Dead", 80));
    s.battlefield.push_back(PermSb("Swamp", 81));   // untapped: floats its {B} before it goes
    ApplySacForMana(s, 0, 80, "B", 4, 0);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_name.str() == "Lake of the Dead");
    CHECK(s.battlefield[0].tapped);
    CHECK(s.floating_mana.black == 5);
    // No Swamp left: the cost cannot be paid -- no activation, nothing floats, the Lake stays untapped.
    GameState t = BaseState();
    t.battlefield.push_back(PermSb("Lake of the Dead", 82));
    ApplySacForMana(t, 0, 82, "B", 4, 0);
    CHECK_FALSE(t.battlefield[0].tapped);
    CHECK(t.floating_mana.black == 0);
}

TEST_CASE("Profane Tutor off suspend: the upkeep pin is a rank into the resolution-time candidates")
{
    for (int pin : { -1, 1 })
    {
        GameState s = BaseState();
        s.players[0].library.push_back(FullCardSb("Dark Ritual", 90));
        s.players[0].library.push_back(FullCardSb("Demonic Tutor", 91));
        s.players[0].library.push_back(FullCardSb("Culling the Weak", 92));
        // INT-4 restricted the deck's tutor list (USER 2026-11-03): a Stone + an Apostle give it >= 2 names.
        s.players[0].library.push_back(FullCardSb("Thrumming Stone", 94));
        s.players[0].library.push_back(FullCardSb("Shadowborn Apostle", 95));
        // One packed ordinal: rank `pin` for the first suspended-tutor search (-1 = unpinned = front). Since
        // MTG_PROFANE_RESOLVE_FAN (2026-11-04) it is the crossing's pin, not the main-phase upkeep stream.
        s.scripted_suspend_tutor = static_cast<std::int16_t>(pin < 0 ? -1 : SuspendTutorPinPack(1, &pin));   // rank + cover 1
        const std::vector<std::string> cands =
            RankedTutorCandidates(Prov(), s, 0, CardDatabase::Instance().Lookup("Profane Tutor")->params);
        REQUIRE(cands.size() >= 2);
        const std::string want = cands[pin < 0 ? 0 : static_cast<std::size_t>(pin)];
        CastOffSuspend(s, 0, FullCardSb("Profane Tutor", 93));
        REQUIRE(s.players[0].hand.size() == 1);
        CHECK(s.players[0].hand[0].m_name.str() == want);
        CHECK(s.spells_cast_this_turn == 1);
        bool in_gy = false;
        for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == "Profane Tutor") { in_gy = true; } }
        CHECK(in_gy);
        CHECK(s.scripted_suspend_tutor == -1);   // the ordinal was consumed
    }
}
