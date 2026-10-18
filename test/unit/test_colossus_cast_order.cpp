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

// ---- Equipment tutors (USER 2026-10-18): Colossus Hammer always and first; Lizard Blades only where the
// Hammer falls short; Loxodon Warhammer / Dragonfire Blade never (MTG_COLOSSUS_TUTOR_OTHERS: with no cheap
// attach only).
namespace
{
GameState TutorBoard(const std::vector<std::string>& bf, const std::vector<std::string>& hand,
                     const std::vector<std::string>& lib)
{
    EnsureCardsCo();
    static const ColossusHammerProvider prov;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &prov;
    int n = 1;
    for (const std::string& b : bf)
    {
        Permanent p;
        p.card             = DefCo(b).card;
        p.card.m_number    = n++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& h : hand) { Card c = DefCo(h).card; c.m_number = n++; s.players[0].hand.push_back(c); }
    for (const std::string& l : lib)  { Card c = DefCo(l).card; c.m_number = n++; s.players[0].library.push_back(c); }
    return s;
}

const std::vector<std::string> kTutorLib = { "Plains", "Loxodon Warhammer", "Colossus Hammer", "Dragonfire Blade",
                                             "Lizard Blades", "Mountain" };

std::vector<std::string> Picks(const GameState& s)
{
    return ResolveProvider(s).TutorCandidates(s, 0, DefCo("Steelshaper's Gift").params);
}
}   // namespace

TEST_CASE("Colossus tutors: a cheap attach and a creature -> Colossus Hammer alone")
{
    using V = std::vector<std::string>;
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, { "Kor Duelist" }, kTutorLib)) == V{ "Colossus Hammer" });
    CHECK(Picks(TutorBoard({ "Plains", "Kor Duelist" }, { "Magnetic Theft" }, kTutorLib)) == V{ "Colossus Hammer" });
    // A body and a free attach: the Hammer alone (USER: "There is no reason to include it if we have both a body and
    // a source to free-equip") -- Raubahn is already on the battlefield, so the Lizard cannot land first.
    CHECK(Picks(TutorBoard({ "Plains", "Raubahn, Bull of Ala Mhigo" }, {}, kTutorLib)) == V{ "Colossus Hammer" });
    CHECK(Picks(TutorBoard({ "Plains", "Raubahn, Bull of Ala Mhigo" }, { "Open the Armory" }, kTutorLib))
          == V{ "Colossus Hammer" });
    // ...and Open the Armory follows the same rule.
    const GameState s = TutorBoard({ "Plains", "Sigarda's Aid" }, { "Kor Duelist" }, kTutorLib);
    CHECK(ResolveProvider(s).TutorCandidates(s, 0, DefCo("Open the Armory").params) == V{ "Colossus Hammer" });
}

TEST_CASE("Colossus tutors: no creature, or no double striker and no cheap attach -> Lizard Blades joins, second")
{
    using V = std::vector<std::string>;
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, {}, kTutorLib)) == V{ "Colossus Hammer", "Lizard Blades" });
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens" }, {}, kTutorLib))
          == V{ "Colossus Hammer", "Lizard Blades", "Loxodon Warhammer", "Dragonfire Blade" });
    // A double striker but no cheap attach: no Lizard (it adds nothing the host lacks); the others only
    // because nothing attaches the Hammer cheaply (TUTOR_OTHERS, default on).
    CHECK(Picks(TutorBoard({ "Plains", "Swiftblade Vindicator" }, {}, kTutorLib))
          == V{ "Colossus Hammer", "Loxodon Warhammer", "Dragonfire Blade" });
}

TEST_CASE("Colossus tutors: Warhammer / Dragonfire Blade only with no cheap attach (default), never under =0")
{
    using V = std::vector<std::string>;
    {
        ArmCo never(heurarm::COLOSSUS_TUTOR_OTHERS, 0);
        CHECK(Picks(TutorBoard({ "Plains", "Swiftblade Vindicator" }, {}, kTutorLib)) == V{ "Colossus Hammer" });
    }
    CHECK(Picks(TutorBoard({ "Plains", "Swiftblade Vindicator" }, {}, kTutorLib))
          == V{ "Colossus Hammer", "Loxodon Warhammer", "Dragonfire Blade" });
    CHECK(Picks(TutorBoard({ "Plains", "Swiftblade Vindicator" }, { "Sigarda's Aid" }, kTutorLib)) == V{ "Colossus Hammer" });
}

TEST_CASE("Colossus tutors: no Hammer left -> every legal target; the rule off -> library order")
{
    using V = std::vector<std::string>;
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, { "Kor Duelist" },
                           { "Plains", "Loxodon Warhammer", "Dragonfire Blade", "Lizard Blades" }))
          == V{ "Loxodon Warhammer", "Dragonfire Blade", "Lizard Blades" });
    ArmCo off(heurarm::COLOSSUS_TUTOR_HAMMER, 0);
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, { "Kor Duelist" }, kTutorLib))
          == V{ "Loxodon Warhammer", "Colossus Hammer", "Dragonfire Blade", "Lizard Blades" });
}

TEST_CASE("Colossus tutors: Lizard Blades when it lands before every other creature (USER 2026-10-18)")
{
    using V = std::vector<std::string>;
    // s1001 gi118: Ardenn (3 mana) the only creature, Magnetic Theft and a second Gift in hand -> the Lizard joins.
    const GameState s = TutorBoard({ "Furycalm Snarl" },
                                   { "Ardenn, Intrepid Archaeologist", "Magnetic Theft", "Steelshaper's Gift" }, kTutorLib);
    CHECK(Picks(s) == V{ "Colossus Hammer", "Lizard Blades" });
    // ...with no second tutor too: landing first is the whole test.
    CHECK(Picks(TutorBoard({ "Furycalm Snarl" }, { "Ardenn, Intrepid Archaeologist", "Magnetic Theft" }, kTutorLib))
          == V{ "Colossus Hammer", "Lizard Blades" });
    // A 2-drop creature in hand lands no later than the Lizard ({1}{R}); Raubahn is a free attach that comes back
    // (his attack trigger) -> the Hammer alone.
    CHECK(Picks(TutorBoard({ "Furycalm Snarl" }, { "Raubahn, Bull of Ala Mhigo", "Magnetic Theft" }, kTutorLib))
          == V{ "Colossus Hammer" });
    // A 1-drop body (Speaker) beside a free attach -> the Hammer alone.
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, { "Speaker of the Heavens" }, kTutorLib)) == V{ "Colossus Hammer" });
    // Ardenn plus a 1-drop double striker in hand -> the Hammer alone.
    CHECK(Picks(TutorBoard({ "Furycalm Snarl" }, { "Ardenn, Intrepid Archaeologist", "Kor Duelist", "Magnetic Theft" },
                           kTutorLib)) == V{ "Colossus Hammer" });
    // A creature already on the battlefield (even a dear one) -> the Lizard cannot land first.
    CHECK(Picks(TutorBoard({ "Plains", "Ardenn, Intrepid Archaeologist" }, { "Magnetic Theft" }, kTutorLib))
          == V{ "Colossus Hammer" });
    // =0 (both amendments) -> the rule as first stated: a body and a free attach -> the Hammer alone.
    ArmCo stated(heurarm::COLOSSUS_TUTOR_LIZARD_FIRST, 0);
    ArmCo stated2(heurarm::COLOSSUS_TUTOR_LIZARD_EQUIP_COUNT, 0);
    CHECK(Picks(s) == V{ "Colossus Hammer" });
    // No creature at all: the USER's own clause, unchanged.
    CHECK(Picks(TutorBoard({ "Plains", "Sigarda's Aid" }, {}, kTutorLib)) == V{ "Colossus Hammer", "Lizard Blades" });
}

TEST_CASE("Colossus tutors: the Lizard clause needs ENOUGH free equip for every Hammer (USER 2026-10-18)")
{
    using V = std::vector<std::string>;
    // Speaker (no double strike) out, ONE Theft, no Hammer held: the Theft covers the fetched Hammer -> the Hammer alone.
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens" }, { "Magnetic Theft" }, kTutorLib)) == V{ "Colossus Hammer" });
    // ...a Hammer already in hand: two Hammers, one Theft -> not enough -> the Lizard joins.
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens" }, { "Magnetic Theft", "Colossus Hammer" }, kTutorLib))
          == V{ "Colossus Hammer", "Lizard Blades" });
    // ...an unattached Hammer on the battlefield counts the same.
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens", "Colossus Hammer" }, { "Magnetic Theft" }, kTutorLib))
          == V{ "Colossus Hammer", "Lizard Blades" });
    // Two Thefts for two Hammers -> enough -> the Hammer alone ("if we do, then the Hammer wins").
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens" }, { "Magnetic Theft", "Magnetic Theft", "Colossus Hammer" },
                           kTutorLib)) == V{ "Colossus Hammer" });
    // Sigarda's Aid attaches every Hammer -> the Hammer alone.
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens", "Sigarda's Aid" }, { "Colossus Hammer" }, kTutorLib))
          == V{ "Colossus Hammer" });
    // Forge Anew is one free equip a turn -> two Hammers is not enough -> the Lizard joins.
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens", "Forge Anew" }, { "Colossus Hammer" }, kTutorLib))
          == V{ "Colossus Hammer", "Lizard Blades" });
    // A double striker -> the Hammer alone, however short the free equip (the Lizard adds nothing it lacks).
    CHECK(Picks(TutorBoard({ "Plains", "Kor Duelist" }, { "Magnetic Theft", "Colossus Hammer" }, kTutorLib))
          == V{ "Colossus Hammer" });
    // =0 -> any free-attach card counts as enough.
    ArmCo off(heurarm::COLOSSUS_TUTOR_LIZARD_EQUIP_COUNT, 0);
    CHECK(Picks(TutorBoard({ "Plains", "Speaker of the Heavens" }, { "Magnetic Theft", "Colossus Hammer" }, kTutorLib))
          == V{ "Colossus Hammer" });
}
