// A restricted Aura onto a creature cast the SAME turn (MTG_AURA_RESTRICTED_NEW_CREATURE,
// docs/design/auras-same-turn-restricted-aura-gap.md). With no creature in play and a hand of Slippery Bogle,
// Ethereal Armor and Daybreak Coronet, "cast the Bogle, cast the Armor on it, cast the Coronet on it" is
// rules-legal (the Armor makes the Bogle a creature with another Aura attached, CR 303.4a / 601.2c), but
// until 2026-10-10 the plan enumeration never offered the third cast: AppendSequencedAuraCandidates looked
// at battlefield creatures only and AppendCreatureTargetAuraCandidates skips restricted Auras. Each line
// below is derived by hand from the cards' costs and the rules, and the first two cases FAIL on the
// pre-fix tree (the line is not enumerated).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsArn()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardArn(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    c.RehashName();
    return c;
}

const DecisionProvider& AurasProv()
{
    static const AurasProvider prov;
    return prov;
}

struct BoardArn
{
    GameState s;
    int next = 1;
    BoardArn()
    {
        EnsureCardsArn();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &AurasProv();
        s.players[0].lands_played_this_turn = 1;   // the land drop is spent: no land-fold variants
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardArn("Plains", 900 + k)); }
    }
    int Put(const std::string& name)
    {
        Permanent p;
        p.card              = CardArn(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardArn(name, next)); return next++; }
};

// The plan's cast of `name` (nullptr if none).
const Action* CastOf(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

int PosOf(const TurnSolver::Plan& p, const std::string& name)
{
    for (std::size_t k = 0; k < p.actions.size(); ++k)
    {
        const Action& a = p.actions[k];
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return static_cast<int>(k); }
    }
    return -1;
}

const Permanent* OnBf(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

std::vector<TurnSolver::Plan> Plans(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::AURA_RESTRICTED_NEW_CREATURE] = arm;
    std::vector<TurnSolver::Plan> v = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    heurarm::t_arm[heurarm::AURA_RESTRICTED_NEW_CREATURE] = -1;
    return v;
}

// Every enumerated plan that casts restricted Aura `r` onto a creature cast this turn must also cast that
// creature and a PLAIN Aura onto it, in the order creature -> plain Aura -> `r`.
void CheckEveryRestrictedLineSound(const BoardArn& b, const std::vector<TurnSolver::Plan>& plans,
                                   const std::string& r, int* lines_out)
{
    int lines = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* ra = CastOf(p, r);
        if (ra == nullptr) { continue; }
        ++lines;
        const int tgt = ra->enchant_target;
        int creature_pos = -1, enabler_pos = -1;
        for (std::size_t k = 0; k < p.actions.size(); ++k)
        {
            const Action& a = p.actions[k];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            const CardDefinition* d = CardDatabase::Instance().Lookup(a.card_name);
            REQUIRE(d != nullptr);
            if (d->card.IsCreature() && a.hand_index >= 0
                && b.s.players[0].hand[static_cast<std::size_t>(a.hand_index)].m_number == tgt)
            { creature_pos = static_cast<int>(k); }
            if (d->params.is_aura && d->params.aura_enchant_requires.empty() && a.enchant_target == tgt
                && enabler_pos < 0)
            { enabler_pos = static_cast<int>(k); }
        }
        CHECK_MESSAGE(creature_pos >= 0, r, " onto #", tgt, " without casting that creature");
        CHECK_MESSAGE(enabler_pos >= 0, r, " onto #", tgt, " with no plain Aura on it");
        CHECK(creature_pos < enabler_pos);
        CHECK(enabler_pos < PosOf(p, r));
        // Applied, the restricted Aura sits on a creature that also carries the enabler.
        GameState after = b.s;
        TurnSolver::ApplyPlan(after, p, /*is_pre_combat=*/true);
        const Permanent* rp = OnBf(after, r);
        REQUIRE(rp != nullptr);
        int on_host = 0;
        for (const Permanent& q : after.battlefield) { if (q.aura_attached_to == rp->aura_attached_to) { ++on_host; } }
        CHECK(rp->aura_attached_to == tgt);
        CHECK(on_host >= 2);
    }
    if (lines_out != nullptr) { *lines_out = lines; }
}

}   // namespace

TEST_CASE("Restricted Aura on a new creature: Bogle -> Ethereal Armor -> Daybreak Coronet is enumerated and applies legally")
{
    // Four lands make {G} + {W} + {W}{W}: Forest -> Bogle, a Plains -> Armor, Plains + Brushland -> Coronet.
    BoardArn b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Brushland");
    b.Put("Forest");
    const int bogle = b.Hand("Slippery Bogle");
    b.Hand("Ethereal Armor");
    b.Hand("Daybreak Coronet");

    const std::vector<TurnSolver::Plan> plans = Plans(b.s, -1);   // the default: ON
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* armor = CastOf(p, "Ethereal Armor");
        const Action* cor   = CastOf(p, "Daybreak Coronet");
        if (CastOf(p, "Slippery Bogle") && armor && cor && armor->enchant_target == bogle && cor->enchant_target == bogle)
        { line = &p; break; }
    }
    REQUIRE_MESSAGE(line != nullptr, "Bogle + Armor -> Bogle + Coronet -> Bogle was not enumerated");
    CHECK(PosOf(*line, "Slippery Bogle") < PosOf(*line, "Ethereal Armor"));
    CHECK(PosOf(*line, "Ethereal Armor") < PosOf(*line, "Daybreak Coronet"));

    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    const Permanent* bp = OnBf(after, "Slippery Bogle");
    const Permanent* ap = OnBf(after, "Ethereal Armor");
    const Permanent* cp = OnBf(after, "Daybreak Coronet");
    REQUIRE(bp != nullptr);
    REQUIRE(ap != nullptr);
    REQUIRE(cp != nullptr);
    CHECK(ap->aura_attached_to == bogle);
    CHECK(cp->aura_attached_to == bogle);
    // The Auras' bonus on the Bogle: Ethereal Armor +1 per enchantment we control (2) + Coronet +3 = +5
    // (a 6-power Bogle).
    CHECK(AuraBonusFor(*bp, after).first == 5);

    int lines = 0;
    CheckEveryRestrictedLineSound(b, plans, "Daybreak Coronet", &lines);
    CHECK(lines >= 1);
}

TEST_CASE("Restricted Aura on a new creature: Scout -> Rancor -> Lion Umbra (enchant MODIFIED creature)")
{
    // {G} + {G} + {G}{G} off Forest, Forest, Brushland and Horizon Canopy.
    BoardArn b;
    b.Put("Forest");
    b.Put("Forest");
    b.Put("Brushland");
    b.Put("Horizon Canopy");
    const int scout = b.Hand("Gladecover Scout");
    b.Hand("Rancor");
    b.Hand("Lion Umbra");

    const std::vector<TurnSolver::Plan> plans = Plans(b.s, 1);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* r = CastOf(p, "Rancor");
        const Action* l = CastOf(p, "Lion Umbra");
        if (CastOf(p, "Gladecover Scout") && r && l && r->enchant_target == scout && l->enchant_target == scout)
        { line = &p; break; }
    }
    REQUIRE_MESSAGE(line != nullptr, "Scout + Rancor -> Scout + Lion Umbra -> Scout was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    const Permanent* lp = OnBf(after, "Lion Umbra");
    REQUIRE(lp != nullptr);
    CHECK(lp->aura_attached_to == scout);
    CheckEveryRestrictedLineSound(b, plans, "Lion Umbra", nullptr);
}

TEST_CASE("Restricted Aura on a new creature: MTG_AURA_RESTRICTED_NEW_CREATURE=0 never offers it")
{
    BoardArn b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Brushland");
    b.Put("Forest");
    b.Hand("Slippery Bogle");
    b.Hand("Ethereal Armor");
    b.Hand("Daybreak Coronet");
    const std::vector<TurnSolver::Plan> off = Plans(b.s, 0);
    for (const TurnSolver::Plan& p : off) { CHECK_MESSAGE(CastOf(p, "Daybreak Coronet") == nullptr, "Coronet offered with the arm off"); }
    // ...and with the arm on, the Coronet line is the extra plan.
    const std::vector<TurnSolver::Plan> on = Plans(b.s, 1);
    CHECK(on.size() > off.size());
}

TEST_CASE("Restricted Aura on a new creature: two hand creatures -- never Armor on one and Coronet on the other")
{
    // {G}{G}{W}{W}{W}: both 1-drops, the Armor and the Coronet.
    BoardArn b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Brushland");
    b.Put("Forest");
    b.Put("Forest");
    b.Hand("Slippery Bogle");
    b.Hand("Gladecover Scout");
    b.Hand("Ethereal Armor");
    b.Hand("Daybreak Coronet");
    const std::vector<TurnSolver::Plan> plans = Plans(b.s, 1);
    int lines = 0;
    CheckEveryRestrictedLineSound(b, plans, "Daybreak Coronet", &lines);
    CHECK(lines >= 1);
}

TEST_CASE("Restricted Aura on a new creature: bounded -- no plain Aura in hand, nothing is injected")
{
    // Coronet and Lion Umbra cannot enable each other on a creature cast this turn (neither is ever legal
    // on it first), so with no plain Aura there is no legal line and no candidate.
    BoardArn b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Forest");
    b.Put("Forest");
    b.Hand("Slippery Bogle");
    b.Hand("Daybreak Coronet");
    b.Hand("Lion Umbra");
    for (const TurnSolver::Plan& p : Plans(b.s, 1))
    {
        CHECK(CastOf(p, "Daybreak Coronet") == nullptr);
        CHECK(CastOf(p, "Lion Umbra") == nullptr);
    }
}
