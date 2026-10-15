// The USER's Angels cast order (approved 2026-10-14) and the narrow Giada second main
// (docs/design/angels-cast-order-2026-10-14.md; AngelsProvider::CastOrderRank / ArrangeCastOrder /
// RestrictedSecondMainLive / RestrictedSecondMainAdmits).
//
//   ORDER: land > Sol Ring > Lightning Greaves > Bishop of Wings > Righteous Valkyrie > Archangel of
//   Thune > Lyra, Archangel of Dawn > Giada > Youthful Valkyrie > Lyra Dawnbringer > Lightstall
//   Inquisitor > Resplendent Angel > a duplicate legend already on our battlefield > Serra the
//   Benevolent > Swords to Plowshares > Unexpectedly Absent.
//   CAVEATS: (a) Giada right after Greaves when the line needs her hasted {W} for an Angel ranked
//   ahead of her; (b) no converter on the board or in the cast set -> Thune and Lyra AoD behind Giada
//   and Youthful.
//   TOTAL: different names never compare equal, and permuting the hand never changes the realised
//   order (USER: "I don't like the randomness of choosing the order").
//   GIADA M2 (MTG_ANGELS_GIADA_M2): opens only when a vigilant, attack-capable Giada is untapped after
//   combat and an Angel in hand is payable with her {W} but not without it; its menu is those casts.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace
{

void EnsureCardsAO()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefAO(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const AngelsProvider& Angels()
{
    static const AngelsProvider p;
    return p;
}

// Pins the two levers for one test and restores "use the env default" on exit, so a test cannot
// leak its arm into the next one through the shared thread.
struct ArmAO
{
    ArmAO(int order, int m2)
    {
        heurarm::t_arm[heurarm::ANGELS_ORDER]    = static_cast<std::int8_t>(order);
        heurarm::t_arm[heurarm::ANGELS_GIADA_M2] = static_cast<std::int8_t>(m2);
    }
    ~ArmAO()
    {
        heurarm::t_arm[heurarm::ANGELS_ORDER]    = -1;
        heurarm::t_arm[heurarm::ANGELS_GIADA_M2] = -1;
    }
};

void PutAO(GameState& s, const std::string& name, int number, bool tapped = false, bool fresh = false)
{
    Permanent p;
    p.card              = DefAO(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = fresh;
    s.battlefield.push_back(p);
}

// Turn 5, pre-combat, our provider, `plains` untapped Plains (numbered 1..), the hand numbered 20+.
GameState BoardAO(int plains, const std::vector<std::string>& hand,
                  const std::vector<std::string>& board = {})
{
    EnsureCardsAO();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &Angels();
    int num = 1;
    for (int k = 0; k < plains; ++k) { PutAO(s, "Plains", num++); }
    for (const std::string& b : board) { PutAO(s, b, num++); }
    int h = 20;
    for (const std::string& n : hand)
    {
        Card c = DefAO(n).card;
        c.m_number = h++;
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefAO("Plains").card;
        c.m_number = 100 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int HandNumberOf(const GameState& s, const std::string& name, int nth = 0)
{
    for (const Card& c : s.players[0].hand)
    { if (c.m_name == name && nth-- == 0) { return c.m_number; } }
    FAIL("not in hand: ", name);
    return -1;
}

// A plan casting `casts` from hand IN THE ORDER GIVEN (the vector order the enumerator would emit).
TurnSolver::Plan CastPlan(const GameState& s, const std::vector<std::string>& casts)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    std::vector<int> used;
    for (const std::string& n : casts)
    {
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = n;
        a.def       = &DefAO(n);
        a.cost      = EffectiveSpellCost(DefAO(n), s);
        for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
        {
            if (s.players[0].hand[i].m_name != n) { continue; }
            if (std::find(used.begin(), used.end(), i) != used.end()) { continue; }
            a.hand_index = i;
            used.push_back(i);
            break;
        }
        plan.actions.push_back(a);
    }
    return plan;
}

void AddEquip(const GameState& s, TurnSolver::Plan& plan, const std::string& equipment, const std::string& host)
{
    Action e;
    e.kind         = Action::Kind::Equip;
    e.card_name    = equipment;
    e.def          = &DefAO(equipment);
    e.sac_source_id = HandNumberOf(s, equipment);
    e.sac_victim_id = HandNumberOf(s, host);
    plan.actions.push_back(e);
}

int RankAO(const GameState& s, const std::string& n) { return Angels().CastOrderRank(s, DefAO(n)); }

const Permanent* FindAO(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

}   // namespace

TEST_CASE("Angels order: the USER's order, every name on its own rank")
{
    ArmAO arm(1, 0);
    GameState s = BoardAO(3, {});
    const std::vector<std::string> order = {
        "Sol Ring", "Lightning Greaves", "Bishop of Wings", "Righteous Valkyrie", "Archangel of Thune",
        "Lyra, Archangel of Dawn", "Giada, Font of Hope", "Youthful Valkyrie", "Lyra Dawnbringer",
        "Lightstall Inquisitor", "Resplendent Angel", "Serra the Benevolent", "Swords to Plowshares",
        "Unexpectedly Absent"};
    for (std::size_t k = 0; k + 1 < order.size(); ++k)
    {
        CAPTURE(order[k]);
        CAPTURE(order[k + 1]);
        CHECK(RankAO(s, order[k]) < RankAO(s, order[k + 1]));
    }
    // The tier labels the --cast-order-report prints exist for every slot of the list.
    for (const std::string& n : order) { CHECK(Angels().CastOrderTierName(RankAO(s, n)) != nullptr); }
}

TEST_CASE("Angels order: a legend already on our battlefield is the last creature, ordered by its own slot")
{
    ArmAO arm(1, 0);
    GameState s = BoardAO(3, {}, {"Giada, Font of Hope", "Lyra, Archangel of Dawn", "Lyra Dawnbringer"});
    const int resplendent = RankAO(s, "Resplendent Angel");
    const int serra       = RankAO(s, "Serra the Benevolent");
    for (const char* n : {"Giada, Font of Hope", "Lyra, Archangel of Dawn", "Lyra Dawnbringer"})
    {
        CAPTURE(n);
        CHECK(RankAO(s, n) > resplendent);
        CHECK(RankAO(s, n) < serra);
    }
    CHECK(RankAO(s, "Lyra, Archangel of Dawn") < RankAO(s, "Giada, Font of Hope"));
    CHECK(RankAO(s, "Giada, Font of Hope") < RankAO(s, "Lyra Dawnbringer"));
    // A non-legend never moves (Thune, two copies coexist).
    CHECK(RankAO(s, "Archangel of Thune") < RankAO(s, "Giada, Font of Hope"));
}

TEST_CASE("Angels order: permuting the hand never changes the realised order (TOTAL)")
{
    ArmAO arm(1, 0);
    const std::vector<std::string> base = {
        "Lightstall Inquisitor", "Resplendent Angel", "Bishop of Wings", "Archangel of Thune",
        "Giada, Font of Hope", "Youthful Valkyrie", "Righteous Valkyrie", "Lyra, Archangel of Dawn",
        "Lyra Dawnbringer", "Serra the Benevolent", "Sol Ring", "Swords to Plowshares"};
    std::vector<std::string> first;
    std::mt19937 rng(20261014u);
    std::vector<std::string> hand = base;
    for (int trial = 0; trial < 40; ++trial)
    {
        if (trial == 1) { std::reverse(hand.begin(), hand.end()); }
        else if (trial > 1) { std::shuffle(hand.begin(), hand.end(), rng); }
        GameState s = BoardAO(30, hand);
        // The plan's vector order follows the HAND, which is what used to decide every tie.
        const std::vector<std::string> got = TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand));
        REQUIRE(got.size() == base.size());
        if (trial == 0) { first = got; continue; }
        CAPTURE(trial);
        CHECK(got == first);
    }
    // ...and that order is the USER's (Bishop is a converter, so no caveat applies).
    const std::vector<std::string> want = {
        "Sol Ring", "Bishop of Wings", "Righteous Valkyrie", "Archangel of Thune", "Lyra, Archangel of Dawn",
        "Giada, Font of Hope", "Youthful Valkyrie", "Lyra Dawnbringer", "Lightstall Inquisitor",
        "Resplendent Angel", "Serra the Benevolent", "Swords to Plowshares"};
    CHECK(first == want);
}

TEST_CASE("Angels order caveat (b): no converter on the board or in the set -> engines behind Giada and Youthful")
{
    ArmAO arm(1, 0);
    const std::vector<std::string> hand = {"Lightstall Inquisitor", "Archangel of Thune", "Lyra, Archangel of Dawn",
                                           "Youthful Valkyrie", "Giada, Font of Hope"};
    {
        GameState s = BoardAO(30, hand);
        const std::vector<std::string> want = {"Giada, Font of Hope", "Youthful Valkyrie", "Archangel of Thune",
                                               "Lyra, Archangel of Dawn", "Lightstall Inquisitor"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand)) == want);
    }
    const std::vector<std::string> engines_first = {"Archangel of Thune", "Lyra, Archangel of Dawn",
                                                    "Giada, Font of Hope", "Youthful Valkyrie", "Lightstall Inquisitor"};
    for (const char* conv : {"Bishop of Wings", "Seraph Sanctuary", "Righteous Valkyrie"})
    {
        CAPTURE(conv);   // a converter on the battlefield keeps the engines ahead
        GameState s = BoardAO(30, hand, {conv});
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand)) == engines_first);
    }
    {
        // ...and so does one cast in this same set (it is ranked ahead of the engines).
        std::vector<std::string> h2 = hand;
        h2.push_back("Bishop of Wings");
        GameState s = BoardAO(30, h2);
        std::vector<std::string> want = engines_first;
        want.insert(want.begin(), "Bishop of Wings");
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, h2)) == want);
    }
}

TEST_CASE("Angels order: a second copy of a legend in the same cast set goes last among the creatures")
{
    ArmAO arm(1, 0);
    const std::vector<std::string> hand = {"Giada, Font of Hope", "Giada, Font of Hope", "Lightstall Inquisitor",
                                           "Bishop of Wings", "Serra the Benevolent"};
    GameState s = BoardAO(30, hand);
    const std::vector<std::string> want = {"Bishop of Wings", "Giada, Font of Hope", "Lightstall Inquisitor",
                                           "Giada, Font of Hope", "Serra the Benevolent"};
    CHECK(TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand)) == want);
}

TEST_CASE("Angels order caveat (a): Giada right after Greaves when her hasted {W} pays an Angel ahead of her")
{
    ArmAO arm(1, 0);
    const std::vector<std::string> hand = {"Righteous Valkyrie", "Giada, Font of Hope", "Lightning Greaves"};
    // 6 Plains: Greaves {2} + Righteous {2}{W} + Giada {1}{W} = 7 -- payable only with Giada's hasted
    // {W}, which arrives the moment Giada and Greaves are both out. Ranked order (Greaves, Righteous,
    // Giada) strands Giada; the hoist (Greaves, Giada, Righteous) pays all three.
    {
        GameState s = BoardAO(6, hand);
        TurnSolver::Plan plan = CastPlan(s, hand);
        AddEquip(s, plan, "Lightning Greaves", "Giada, Font of Hope");
        const int giada = HandNumberOf(s, "Giada, Font of Hope");
        const int righteous = HandNumberOf(s, "Righteous Valkyrie");
        const std::vector<std::string> want = {"Lightning Greaves", "Giada, Font of Hope", "Righteous Valkyrie"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, plan) == want);
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        CHECK(FindAO(s, giada) != nullptr);
        CHECK(FindAO(s, righteous) != nullptr);
        CHECK(s.players[0].hand.empty());
    }
    // 7 Plains: the ranked order already pays everything, so nothing moves.
    {
        GameState s = BoardAO(7, hand);
        TurnSolver::Plan plan = CastPlan(s, hand);
        AddEquip(s, plan, "Lightning Greaves", "Giada, Font of Hope");
        const std::vector<std::string> want = {"Lightning Greaves", "Righteous Valkyrie", "Giada, Font of Hope"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, plan) == want);
    }
    // No equip onto Giada in the plan: no unlock to wait for, so no hoist.
    {
        GameState s = BoardAO(6, hand);
        const std::vector<std::string> want = {"Lightning Greaves", "Righteous Valkyrie", "Giada, Font of Hope"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand)) == want);
    }
    // The draft's probe 4-d: Giada + Greaves + Lightstall on 4 Plains -- the ranked order (Greaves,
    // Giada, Lightstall) is already the line, and it pays all three through the unlock.
    {
        const std::vector<std::string> h = {"Lightstall Inquisitor", "Lightning Greaves", "Giada, Font of Hope"};
        GameState s = BoardAO(4, h);
        TurnSolver::Plan plan = CastPlan(s, h);
        AddEquip(s, plan, "Lightning Greaves", "Giada, Font of Hope");
        const std::vector<std::string> want = {"Lightning Greaves", "Giada, Font of Hope", "Lightstall Inquisitor"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, plan) == want);
        TurnSolver::ApplyPlan(s, plan, true);
        CHECK(s.players[0].hand.empty());
    }
}

TEST_CASE("Payer: MTG_LINE_SPARE_TIGHT keeps Remote Farm's second {W} for the line at zero surplus")
{
    // The A/B's lost-line class (angels ship gi2060 and its siblings): Remote Farm ({T}, remove a
    // depletion counter: {W}{W}) + Seraph Sanctuary ({C}), casting Sol Ring, Righteous Valkyrie {2}{W},
    // Lightstall Inquisitor {W} in the USER's order. Sol Ring takes the {C}; Righteous's {W} taps the
    // Farm and its {2} must come from Sol Ring's {C}{C}, so the Farm's second {W} floats for Lightstall.
    // With the strict SHORT test the generic ate that {W} (owed 1, supply 1 -> "not short") and
    // Lightstall stayed in hand; the zero-surplus test pays the generic off the spare Sol Ring.
    for (int tight : {0, 1})
    {
        CAPTURE(tight);
        ArmAO arm(1, 0);
        heurarm::t_arm[heurarm::LINE_SPARE_TIGHT] = static_cast<std::int8_t>(tight);
        const std::vector<std::string> hand = {"Lightstall Inquisitor", "Righteous Valkyrie", "Sol Ring"};
        GameState s = BoardAO(0, hand, {"Seraph Sanctuary"});
        PutAO(s, "Remote Farm", 9);
        Counter dep; dep.type = Counter::Type::Depletion; dep.count = 2;
        s.battlefield.back().counters.Add(dep);
        TurnSolver::Plan plan = CastPlan(s, hand);
        const std::vector<std::string> want = {"Sol Ring", "Righteous Valkyrie", "Lightstall Inquisitor"};
        CHECK(TurnSolver::CanonicalNonSacCastOrder(s, plan) == want);
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        if (tight) { CHECK(s.players[0].hand.empty()); }
        else
        {
            REQUIRE(s.players[0].hand.size() == 1);   // the defect the lever fixes, pinned
            CHECK(s.players[0].hand[0].m_name == "Lightstall Inquisitor");
        }
        heurarm::t_arm[heurarm::LINE_SPARE_TIGHT] = -1;
    }
}

TEST_CASE("Angels order: the PROPOSED Giada-first amendment (MTG_ANGELS_GIADA_FIRST) and its default")
{
    ArmAO arm(1, 0);
    auto order_of = [](GameState& s, const std::vector<std::string>& hand, int first)
    {
        heurarm::t_arm[heurarm::ANGELS_GIADA_FIRST] = static_cast<std::int8_t>(first);
        std::vector<std::string> o = TurnSolver::CanonicalNonSacCastOrder(s, CastPlan(s, hand));
        heurarm::t_arm[heurarm::ANGELS_GIADA_FIRST] = -1;
        return o;
    };
    // (c) no engine: Righteous behind Giada only with the lever.
    {
        const std::vector<std::string> hand = {"Righteous Valkyrie", "Giada, Font of Hope"};
        GameState s = BoardAO(30, hand, {"Bishop of Wings"});
        CHECK(order_of(s, hand, -1) == std::vector<std::string>{"Righteous Valkyrie", "Giada, Font of Hope"});
        CHECK(order_of(s, hand, 1) == std::vector<std::string>{"Giada, Font of Hope", "Righteous Valkyrie"});
        // ...an engine on the battlefield keeps the USER's order.
        GameState s2 = BoardAO(30, hand, {"Bishop of Wings", "Archangel of Thune"});
        CHECK(order_of(s2, hand, 1) == std::vector<std::string>{"Righteous Valkyrie", "Giada, Font of Hope"});
    }
    // (d) one converter: Lyra AoD behind Giada with the lever; two converters keep her ahead.
    {
        const std::vector<std::string> hand = {"Lyra, Archangel of Dawn", "Giada, Font of Hope"};
        GameState s = BoardAO(30, hand, {"Righteous Valkyrie"});
        CHECK(order_of(s, hand, -1) == std::vector<std::string>{"Lyra, Archangel of Dawn", "Giada, Font of Hope"});
        CHECK(order_of(s, hand, 1) == std::vector<std::string>{"Giada, Font of Hope", "Lyra, Archangel of Dawn"});
        GameState s2 = BoardAO(30, hand, {"Righteous Valkyrie", "Bishop of Wings"});
        CHECK(order_of(s2, hand, 1) == std::vector<std::string>{"Lyra, Archangel of Dawn", "Giada, Font of Hope"});
    }
}

TEST_CASE("Angels order: the PROPOSED searched Giada pair (MTG_ANGELS_GIADA_SEARCH)")
{
    ArmAO arm(1, 0);
    auto cands = [](const GameState& s, const TurnSolver::Plan& plan, int on)
    {
        heurarm::t_arm[heurarm::ANGELS_GIADA_SEARCH] = static_cast<std::int8_t>(on);
        std::vector<Action> reorder;
        for (const Action& a : plan.actions) { if (a.kind == Action::Kind::CastFromHand) { reorder.push_back(a); } }
        std::vector<std::vector<int>> out;
        const bool r = Angels().CastOrderingCandidates(s, plan.actions, reorder, out);
        const bool wants = Angels().WantsCastOrderingSearch();
        heurarm::t_arm[heurarm::ANGELS_GIADA_SEARCH] = -1;
        CHECK(r);
        CHECK(wants == (on == 1));
        std::vector<std::vector<std::string>> names;
        for (const auto& o : out)
        {
            std::vector<std::string> n;
            for (int j : o) { n.push_back(reorder[static_cast<std::size_t>(j)].card_name.str()); }
            names.push_back(n);
        }
        return names;
    };
    {
        // The pair: the USER's order first, then Giada ahead of Righteous.
        const std::vector<std::string> hand = {"Giada, Font of Hope", "Lightstall Inquisitor", "Righteous Valkyrie"};
        GameState s = BoardAO(30, hand, {"Bishop of Wings"});
        const auto got = cands(s, CastPlan(s, hand), 1);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == std::vector<std::string>{"Righteous Valkyrie", "Giada, Font of Hope", "Lightstall Inquisitor"});
        CHECK(got[1] == std::vector<std::string>{"Giada, Font of Hope", "Righteous Valkyrie", "Lightstall Inquisitor"});
        CHECK(cands(s, CastPlan(s, hand), 0).empty());   // lever off: nothing to search
    }
    {
        // No pair (Lightstall ranks after Giada anyway): no candidates -> the plan is kept unsearched.
        const std::vector<std::string> hand = {"Giada, Font of Hope", "Lightstall Inquisitor"};
        GameState s = BoardAO(30, hand);
        CHECK(cands(s, CastPlan(s, hand), 1).empty());
    }
}

TEST_CASE("Angels order: MTG_ANGELS_ORDER=0 is exactly the generic tiers")
{
    ArmAO arm(0, 0);
    GameState s = BoardAO(3, {}, {"Giada, Font of Hope"});
    for (const char* n : {"Sol Ring", "Lightning Greaves", "Bishop of Wings", "Righteous Valkyrie",
                          "Archangel of Thune", "Lyra, Archangel of Dawn", "Giada, Font of Hope", "Youthful Valkyrie",
                          "Lyra Dawnbringer", "Lightstall Inquisitor", "Resplendent Angel", "Serra the Benevolent",
                          "Swords to Plowshares", "Unexpectedly Absent"})
    {
        CAPTURE(n);
        CHECK(Angels().CastOrderRank(s, DefAO(n)) == GenericProvider().CastOrderRank(s, DefAO(n)));
    }
    // ...and the set-level arrangement is a no-op (caveat (b) would otherwise move Thune here).
    const std::vector<std::string> hand = {"Archangel of Thune", "Giada, Font of Hope"};
    GameState s2 = BoardAO(30, hand);
    std::vector<int> order = {0, 1};
    const TurnSolver::Plan plan = CastPlan(s2, hand);
    Angels().ArrangeCastOrder(s2, plan.actions, order);
    CHECK(order == std::vector<int>{0, 1});
}

// ---- the narrow Giada second main ---------------------------------------------------------------

namespace
{
// Post-combat: `plains` Plains, `tapped_plains` of them tapped; Giada (number 50) untapped and not
// summoning sick unless said otherwise.
GameState PostCombatAO(int plains, int tapped_plains, const std::vector<std::string>& hand,
                       bool giada_fresh = false, bool giada_tapped = false)
{
    GameState s = BoardAO(0, hand);
    for (int k = 0; k < plains; ++k) { PutAO(s, "Plains", 1 + k, /*tapped=*/k < tapped_plains); }
    PutAO(s, "Giada, Font of Hope", 50, giada_tapped, giada_fresh);
    return s;
}
}   // namespace

TEST_CASE("Angels Giada m2: opens only when her {W} is what pays an Angel in hand")
{
    ArmAO arm(1, 1);
    // Every land tapped, Lightstall {W} in hand: only Giada can pay it -> live.
    CHECK(Angels().RestrictedSecondMainLive(PostCombatAO(2, 2, {"Lightstall Inquisitor"})));
    // One untapped Plains pays Lightstall alone: it was castable in main 1 -> not live.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(2, 1, {"Lightstall Inquisitor"})));
    // Righteous {2}{W} with two untapped Plains: needs her as the third mana -> live (HELP pay for).
    CHECK(Angels().RestrictedSecondMainLive(PostCombatAO(3, 1, {"Righteous Valkyrie"})));
    // ...but not with three.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(4, 1, {"Righteous Valkyrie"})));
    // Not affordable even with her -> nothing to open for.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(2, 2, {"Righteous Valkyrie"})));
    // Her mana is Angel-only: Bishop of Wings (Human Cleric) never opens it.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(3, 2, {"Bishop of Wings"})));
    // Summoning-sick (and not hasted) -> could not attack, cannot tap -> not live.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(2, 2, {"Lightstall Inquisitor"}, /*fresh=*/true)));
    // Tapped (main 1 spent her) -> not live.
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(2, 2, {"Lightstall Inquisitor"}, false, /*tapped=*/true)));
    // ProviderSecondMainLive is the engine's read and agrees.
    {
        GameState s = PostCombatAO(2, 2, {"Lightstall Inquisitor"});
        CHECK(ProviderSecondMainLive(s));
        s.uses_second_main = true;   // a deck that already plays a full main 2 is never narrowed
        CHECK_FALSE(ProviderSecondMainLive(s));
    }
}

TEST_CASE("Angels Giada m2: hasted by Lightning Greaves she opens it on the turn she was cast")
{
    ArmAO arm(1, 1);
    GameState s = PostCombatAO(2, 2, {"Lightstall Inquisitor"}, /*fresh=*/true);
    PutAO(s, "Lightning Greaves", 60);
    s.battlefield.back().equipped_to = 50;
    CHECK(Angels().RestrictedSecondMainLive(s));
}

TEST_CASE("Angels Giada m2: MTG_ANGELS_GIADA_M2=0 never opens it")
{
    ArmAO arm(1, 0);
    CHECK_FALSE(Angels().RestrictedSecondMainLive(PostCombatAO(2, 2, {"Lightstall Inquisitor"})));
}

TEST_CASE("Angels Giada m2: the menu is the Angel casts that need her, and at most one is ever payable")
{
    ArmAO arm(1, 1);
    // Two tapped Plains + one untapped: Lightstall {W} is payable without her (refused), Youthful {1}{W}
    // needs her (admitted), Righteous {2}{W} needs her (admitted), Bishop is not an Angel (refused).
    GameState s = PostCombatAO(3, 2, {"Lightstall Inquisitor", "Youthful Valkyrie", "Righteous Valkyrie",
                                      "Bishop of Wings", "Serra the Benevolent"});
    REQUIRE(Angels().RestrictedSecondMainLive(s));
    auto cast = [&](const std::string& n)
    {
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = n;
        a.def       = &DefAO(n);
        a.cost      = EffectiveSpellCost(DefAO(n), s);
        return a;
    };
    CHECK_FALSE(Angels().RestrictedSecondMainAdmits(s, cast("Lightstall Inquisitor")));
    CHECK(Angels().RestrictedSecondMainAdmits(s, cast("Youthful Valkyrie")));
    CHECK_FALSE(Angels().RestrictedSecondMainAdmits(s, cast("Righteous Valkyrie")));   // 3 > 1 Plains + Giada
    CHECK_FALSE(Angels().RestrictedSecondMainAdmits(s, cast("Bishop of Wings")));
    CHECK_FALSE(Angels().RestrictedSecondMainAdmits(s, cast("Serra the Benevolent")));
    // The post-combat enumeration offers exactly: pass, or ONE admitted cast -- never two.
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
    {
        int casts = 0;
        for (const Action& a : p.actions)
        {
            CHECK(a.kind == Action::Kind::CastFromHand);
            CHECK(a.card_name == "Youthful Valkyrie");
            ++casts;
        }
        CHECK(casts <= 1);
        CHECK(p.land_to_play.empty());
    }
}

TEST_CASE("Angels Giada m2: vigilance leaves her untapped through her own attack, and she then pays")
{
    // The card note's claim ("vigilance lets her attack and still tap for Angel mana the same turn"),
    // checked end to end on the rollout world: attack, then the post-combat plan casts Lightstall off
    // her {W} with every land tapped.
    ArmAO arm(1, 1);
    GameState s = PostCombatAO(2, 2, {"Lightstall Inquisitor"});
    RolloutSimulateCombat(s);
    REQUIRE(FindAO(s, 50) != nullptr);
    CHECK_FALSE(FindAO(s, 50)->tapped);           // attacked (2 damage), vigilance: still untapped
    CHECK(s.players[1].life == 18);
    REQUIRE(ProviderSecondMainLive(s));
    TurnSolver::Plan plan = CastPlan(s, {"Lightstall Inquisitor"});
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/false);
    CHECK(s.players[0].hand.empty());
    CHECK(FindAO(s, 50)->tapped);                  // her {W} paid it
}
