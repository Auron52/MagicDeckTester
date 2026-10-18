// Colossus Hammer's cast order (USER-adopted 2026-10-18; docs/design/colossus-hammer-cast-order.md):
// "That seems reasonable, but we should make a full order, also, we should have equip abilities listed
// in the order." Pins the FULL order -- one position per card of the list, strictly increasing -- and
// the board activations' positions after every cast (ActivationOrderRankFor), plus the =0 fallback.

#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsCo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefCo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

struct ArmCo
{
    heurarm::Slot slot;
    std::int8_t   saved;
    ArmCo(heurarm::Slot s, int v) : slot(s), saved(heurarm::t_arm[s]) { heurarm::t_arm[s] = static_cast<std::int8_t>(v); }
    ~ArmCo() { heurarm::t_arm[slot] = saved; }
};

}   // namespace

TEST_CASE("Colossus cast order: one position per card, in the USER-adopted order")
{
    EnsureCardsCo();
    const ColossusHammerProvider p;
    const GameState s;
    const std::vector<std::string> order = {
        "Steelshaper's Gift", "Open the Armory", "Sigarda's Aid", "Kor Duelist", "Swiftblade Vindicator",
        "Speaker of the Heavens", "Raubahn, Bull of Ala Mhigo", "Ardenn, Intrepid Archaeologist",
        "Lizard Blades", "Forge Anew", "Colossus Hammer", "Loxodon Warhammer", "Dragonfire Blade",
        "Magnetic Theft", "Path to Exile" };
    int prev = -1;
    int prev_pos = 0;
    for (const std::string& n : order)
    {
        CAPTURE(n);
        const int r = p.CastOrderRank(s, DefCo(n));
        CHECK(r > prev);
        const int pos = r / 4096;
        CHECK(pos == prev_pos + 1);           // its OWN position: no two cards of the list share one
        prev = r;
        prev_pos = pos;
        CHECK(p.CastOrderTierName(r) != nullptr);
    }
    CHECK(p.LandDropCastOrderRank() == 0);    // the land drop before every cast
    CHECK(p.CastOrderTotalByName());
    CHECK(p.OrderOpaqueCastsByRank());
}

TEST_CASE("Colossus cast order: the activations follow every cast, in the listed order")
{
    EnsureCardsCo();
    const ColossusHammerProvider p;
    const GameState s;
    const int unattach = p.ActivationOrderRankFor(s, DefCo("Lizard Blades"), 0);   // ActivatePermAbility
    const int hammer   = p.ActivationOrderRankFor(s, DefCo("Colossus Hammer"), 1);
    const int lox      = p.ActivationOrderRankFor(s, DefCo("Loxodon Warhammer"), 1);
    const int blade    = p.ActivationOrderRankFor(s, DefCo("Dragonfire Blade"), 1);
    const int attach   = p.ActivationOrderRankFor(s, DefCo("Lizard Blades"), 1);   // reconfigure-attach
    const int angel    = p.ActivationOrderRankFor(s, DefCo("Speaker of the Heavens"), 0);
    CHECK(unattach > 0);                      // nonzero: OrderTrailingActivations sorts the pass
    CHECK(unattach < hammer);
    CHECK(hammer < lox);                      // the Hammer's equip FIRST -- the one Forge Anew's {0} pays
    CHECK(lox < blade);
    CHECK(blade < attach);
    CHECK(attach < angel);
    CHECK(TurnSolver::ActivationClass(Action::Kind::Equip) == 1);
    CHECK(TurnSolver::ActivationClass(Action::Kind::ActivatePermAbility) == 0);
    CHECK(TurnSolver::ActivationClass(Action::Kind::TapForTokenPay) == 0);

    // OrderTrailingActivations applies it: a pass emitted Angel, Blade, Hammer, unattach comes out in order.
    GameState g;
    g.m_provider = &p;
    auto act = [](Action::Kind k, const char* name)
    {
        Action a;
        a.kind      = k;
        a.card_name = name;
        a.def       = CardDatabase::Instance().Lookup(name);
        return a;
    };
    std::vector<Action> acts = { act(Action::Kind::TapForTokenPay, "Speaker of the Heavens"),
                                 act(Action::Kind::Equip, "Dragonfire Blade"),
                                 act(Action::Kind::Equip, "Colossus Hammer"),
                                 act(Action::Kind::ActivatePermAbility, "Lizard Blades") };
    TurnSolver::OrderTrailingActivations(g, acts);
    REQUIRE(acts.size() == 4);
    CHECK(acts[0].card_name == "Lizard Blades");
    CHECK(acts[1].card_name == "Colossus Hammer");
    CHECK(acts[2].card_name == "Dragonfire Blade");
    CHECK(acts[3].card_name == "Speaker of the Heavens");
}

TEST_CASE("Colossus cast order: =0 is the generic order and no activation order")
{
    EnsureCardsCo();
    const ColossusHammerProvider p;
    const GenericProvider generic;
    const GameState s;
    ArmCo off(heurarm::COLOSSUS_ORDER, 0);
    for (const char* n : { "Sigarda's Aid", "Colossus Hammer", "Kor Duelist", "Magnetic Theft" })
    {
        CAPTURE(n);
        CHECK(p.CastOrderRank(s, DefCo(n)) == generic.CastOrderRank(s, DefCo(n)));
        CHECK(p.ActivationOrderRankFor(s, DefCo(n), 1) == 0);
    }
    CHECK(p.LandDropCastOrderRank() == -1);
    CHECK_FALSE(p.CastOrderTotalByName());
}
