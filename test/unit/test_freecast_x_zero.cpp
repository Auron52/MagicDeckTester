// CR 107.3b in the post-combat free-cast bank (Maelstrom Archangel): a spell with {X} in its mana cost
// that is cast without paying its mana cost has X = 0 (MTG_FREECAST_X_ZERO,
// docs/design/precollect-def-dead-reads.md).
//
// CollectActions' free-cast bank copied every CastFromHand candidate into a free twin and was meant to
// set X = 0 on it -- `if (f.def && f.def->card.m_mana_cost.has_x) f.chosen_x = 0` -- but a candidate's
// Action::def is filled only by the closing resolve pass BELOW the bank, so the check never fired: every
// X > 0 variant got a free twin that paid nothing and still resolved at its X. The fix resolves the def
// the way the closing pass does and gives an {X} spell free variants only from its X = 0 candidates.
// The `dead` arm (=0) reproduces the committed tree (77e094a2) exactly; the `fix` arm is the default.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsFx()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardFx(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    c.RehashName();
    return c;
}

// A post-combat main with one banked free cast (the Archangel connected), the land drop spent, and
// `mountains` untapped Mountains. The default (generic) provider.
struct BoardFx
{
    GameState s;
    int next = 1;
    explicit BoardFx(int mountains)
    {
        EnsureCardsFx();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.players[0].lands_played_this_turn = 1;
        s.free_casts_available = 1;
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardFx("Mountain", 900 + k)); }
        for (int k = 0; k < mountains; ++k)
        {
            Permanent p;
            p.card              = CardFx("Mountain", next++);
            p.controller_index  = 0;
            p.owner_index       = 0;
            p.entered_this_turn = false;
            s.battlefield.push_back(p);
        }
    }
    void Hand(const std::string& name) { s.players[0].hand.push_back(CardFx(name, next++)); }
};

// arm: 1 = the fix, 0 = the dead read, -1 = the binary's default.
std::vector<TurnSolver::Plan> PlansFx(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::FREECAST_X_ZERO] = static_cast<std::int8_t>(arm);
    std::vector<TurnSolver::Plan> v = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    heurarm::t_arm[heurarm::FREECAST_X_ZERO] = -1;
    return v;
}

// The chosen_x of every CastFromHand of `name` across the plan menu, split by free / paid.
std::multiset<int> Xs(const std::vector<TurnSolver::Plan>& plans, const std::string& name, bool free)
{
    std::multiset<int> out;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != name) { continue; }
            if (a.free_cast != free) { continue; }
            out.insert(a.chosen_x);
        }
    }
    return out;
}

int MaxOf(const std::multiset<int>& m) { return m.empty() ? -1 : *m.rbegin(); }

}   // namespace

TEST_CASE("free-cast bank: an {X} spell cast free has X = 0 (CR 107.3b)")
{
    // Six Mountains: Rolling Earthquake ({X}{R}) is offered paid at every X in 0..5 (GenericProvider's
    // whole-range rule for the symmetric sweeper), Crackle with Power ({X}{X}{X}{R}{R}) at its max X = 1.
    BoardFx b(6);
    b.Hand("Rolling Earthquake");
    b.Hand("Crackle with Power");

    const std::vector<TurnSolver::Plan> fix  = PlansFx(b.s, 1);
    const std::vector<TurnSolver::Plan> dead = PlansFx(b.s, 0);
    const std::vector<TurnSolver::Plan> dflt = PlansFx(b.s, -1);

    // The paid enumeration offers the same X values in both arms and really does carry X > 0. (Compared
    // as SETS of X: how often a value recurs across the menu depends on which free twins it is paired
    // with, which is exactly what differs.)
    auto distinct = [](const std::multiset<int>& m) { return std::set<int>(m.begin(), m.end()); };
    REQUIRE(MaxOf(Xs(fix, "Rolling Earthquake", false)) > 0);
    REQUIRE(MaxOf(Xs(fix, "Crackle with Power", false)) == 1);
    CHECK(distinct(Xs(fix, "Rolling Earthquake", false)) == distinct(Xs(dead, "Rolling Earthquake", false)));

    // FIX: the free Earthquake is offered, only at X = 0 (its enumerated X = 0 candidate) ...
    const std::multiset<int> fq = Xs(fix, "Rolling Earthquake", true);
    CHECK_FALSE(fq.empty());
    CHECK(MaxOf(fq) == 0);
    // ... and an X burn, whose provider never offers X = 0 (a 0-damage burn is not cast), has no free
    // twin at all, rather than a free X = 1 Crackle that deals 5.
    CHECK(Xs(fix, "Crackle with Power", true).empty());

    // DEAD (the committed tree): the X = 0 rule never fired, so the bank cast X > 0 spells for free.
    CHECK(MaxOf(Xs(dead, "Rolling Earthquake", true)) > 0);
    CHECK(MaxOf(Xs(dead, "Crackle with Power", true)) == 1);

    // The binary's default is the fix.
    CHECK(Xs(dflt, "Rolling Earthquake", true) == fq);
    CHECK(Xs(dflt, "Crackle with Power", true).empty());
}

TEST_CASE("free-cast bank: a non-{X} card's chosen_x axis is untouched (Unite the Coalition's mode split)")
{
    // Unite carries its searched split S (S x 2 damage + (5 - S) draws) on chosen_x. It has no {X}, so
    // every S keeps its free twin in both arms -- this is what keeps FiveColour, the only deck that
    // banks a charge, byte-identical. Seven Mountains do not pay {2}{W}{U}{B}{R}{G}; the live charge's
    // emission-prune exemption is what puts it on the menu at all.
    BoardFx b(7);
    b.Hand("Unite the Coalition");

    const std::multiset<int> fix  = Xs(PlansFx(b.s, 1), "Unite the Coalition", true);
    const std::multiset<int> dead = Xs(PlansFx(b.s, 0), "Unite the Coalition", true);
    REQUIRE_FALSE(fix.empty());
    CHECK(MaxOf(fix) > 0);
    CHECK(fix == dead);
}

TEST_CASE("free-cast bank: every cast candidate leaves CollectActions with its def resolved")
{
    // The closing resolve pass now goes through CollectedActionDef, the helper the bank uses: the
    // refactor must leave every enumerated action's def exactly as the inline expression set it.
    BoardFx b(6);
    b.Hand("Rolling Earthquake");
    b.Hand("Unite the Coalition");
    for (const TurnSolver::Plan& p : PlansFx(b.s, -1))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            REQUIRE(a.def != nullptr);
            CHECK(a.def == CardDatabase::Instance().Lookup(a.card_name));
        }
    }
}
