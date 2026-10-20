// Unit cover for the defects found by the USER-order audit (docs/design/dragonstorm-user-cast-order.md,
// "Found, not fixed" -> fixed in the 2026-10-10 follow-up round). One TEST_CASE group per lever.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
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

void EnsureCardsFu()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFu(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutFu(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefFu(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardFu(const std::string& name, int number)
{
    Card c     = DefFu(name).card;
    c.m_number = number;
    return c;
}

const DragonstormProvider& DsProvFu()
{
    static const DragonstormProvider p;
    return p;
}

// One lever forced for this thread, restored on scope exit.
struct ArmFu
{
    heurarm::Slot slot;
    ArmFu(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmFu() { heurarm::t_arm[slot] = -1; }
};

GameState BoardFu(const std::vector<std::string>& lands, const std::vector<std::string>& hand, int turn = 5)
{
    EnsureCardsFu();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &DsProvFu();
    int num = 300;
    for (const std::string& l : lands) { PutFu(s, l, num++); }
    num = 10;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardFu(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardFu("Mountain", 900 + k)); }
    return s;
}

bool CastsBoth(const TurnSolver::Plan& p, const std::string& a, const std::string& b)
{
    bool ha = false, hb = false;
    for (const Action& x : p.actions)
    {
        if (x.kind != Action::Kind::CastFromHand) { continue; }
        if (x.card_name.str() == a) { ha = true; }
        if (x.card_name.str() == b) { hb = true; }
    }
    return ha && hb;
}

}   // namespace

TEST_CASE("Ritual float colour: Irencrag's seven red cannot pay Karrthus's {B}{G} (MTG_RITUAL_FLOAT_COLOR_EXACT)")
{
    // Four Mountains + one City of Brass: Irencrag Feat (4) leaves one land and floats seven RED. Karrthus
    // {4}{B}{R}{G} needs a black AND a green pip; only the City makes either -> no order pays it. The wild
    // credit admitted the pair.
    const GameState s = BoardFu({ "Mountain", "Mountain", "Mountain", "Mountain", "City of Brass" },
                                { "Irencrag Feat", "Karrthus, Tyrant of Jund" });
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::RITUAL_FLOAT_COLOR_EXACT, on);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        bool any = false;
        for (const TurnSolver::Plan& p : plans) { if (CastsBoth(p, "Irencrag Feat", "Karrthus, Tyrant of Jund")) { any = true; } }
        CHECK(any == !on);
    }
    // With a SECOND City of Brass the same pair IS payable, lever on (the Mountains pay Irencrag, the float
    // pays Karrthus's red and generic, the two Cities its {B}{G}).
    const GameState s2 = BoardFu({ "Mountain", "Mountain", "Mountain", "Mountain", "City of Brass", "City of Brass" },
                                 { "Irencrag Feat", "Karrthus, Tyrant of Jund" });
    ArmFu arm(heurarm::RITUAL_FLOAT_COLOR_EXACT, true);
    bool any2 = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s2, true))
    { if (CastsBoth(p, "Irencrag Feat", "Karrthus, Tyrant of Jund")) { any2 = true; } }
    CHECK(any2);
}

namespace
{
Action CastFu(const GameState& s, const std::string& name, int splice = 0)
{
    const CardDefinition& d = DefFu(name);
    Action a;
    a.kind         = Action::Kind::CastFromHand;
    a.card_name    = name;
    a.def          = &d;
    a.splice_count = splice;
    a.cost         = EffectiveSpellCost(d, s, splice + 1);
    a.card_mv      = d.card.m_mana_cost.ManaValue();
    if (IsManaRitual(d)) { a.ritual_float = RitualFloatAmount(s, d, 0, splice + 1); }
    a.max_casts_after = d.params.max_casts_after;
    return a;
}

std::vector<std::string> CastNamesFu(const std::vector<Action>& acts)
{
    std::vector<std::string> v;
    for (const Action& a : acts) { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    return v;
}
}   // namespace

TEST_CASE("Breakpoint leftovers: the interrupted list's casts join the continuation in the USER order (MTG_DRAGONSTORM_BP_LEFTOVERS)")
{
    EnsureCardsFu();
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    ArmFu lo(heurarm::DRAGONSTORM_BP_LEFTOVERS, true);
    CHECK(DsProvFu().MergesBreakpointLeftovers(DefFu("Apex of Power")));
    CHECK_FALSE(DsProvFu().MergesBreakpointLeftovers(DefFu("Seething Song")));
    {
        // After Apex: 14 mana. The continuation casts Rite of Flame then Dragonstorm (9); the pre-Apex plan
        // still holds Scourge of Valkas (5). Together: Rite, Scourge, Dragonstorm -- the Dragon BEFORE the
        // storm (it pings for every storm Dragon and adds one to the count), and everything pays.
        const GameState s = BoardFu(std::vector<std::string>(14, "Mountain"),
                                    { "Rite of Flame", "Dragonstorm", "Scourge of Valkas" }, 6);
        std::vector<Action> cont = { CastFu(s, "Rite of Flame"), CastFu(s, "Dragonstorm") };
        bool searched = false;
        CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Scourge of Valkas") }));
        CHECK(searched);
        CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Rite of Flame", "Scourge of Valkas", "Dragonstorm" });
    }
    {
        // Only 9 mana: the Scourge in front would leave Dragonstorm unpaid -> the leftover is CARRIED behind
        // the continuation's own casts (a later Apex in it would take it up again; with none it is cast
        // after, as before -- the nearest order that pays).
        const GameState s = BoardFu(std::vector<std::string>(9, "Mountain"),
                                    { "Dragonstorm", "Scourge of Valkas" }, 6);
        std::vector<Action> cont = { CastFu(s, "Dragonstorm") };
        bool searched = false;
        CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Scourge of Valkas") }));
        CHECK(searched);
        CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Dragonstorm", "Scourge of Valkas" });
    }
    {
        // A leftover the continuation already casts is not added twice; none -> false (nothing to stop).
        const GameState s = BoardFu(std::vector<std::string>(14, "Mountain"), { "Dragonstorm", "Scourge of Valkas" }, 6);
        std::vector<Action> cont = { CastFu(s, "Scourge of Valkas"), CastFu(s, "Dragonstorm") };
        bool searched = false;
        CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Scourge of Valkas") }));
        CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Scourge of Valkas", "Dragonstorm" });
        CHECK_FALSE(MergeBreakpointLeftovers(s, cont, searched, {}));
    }
    {
        // Irencrag left over from the pre-Apex plan, Utvara Hellkite the continuation: Irencrag goes in
        // front of the lone Dragon closer instead of after it (s1191413 T3: Irencrag cast LAST, wasted).
        const GameState s = BoardFu(std::vector<std::string>(13, "Mountain"),
                                    { "Utvara Hellkite", "Irencrag Feat" }, 3);
        std::vector<Action> cont = { CastFu(s, "Utvara Hellkite") };
        bool searched = false;
        CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
        CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Irencrag Feat", "Utvara Hellkite" });
    }
    ArmFu off(heurarm::DRAGONSTORM_BP_LEFTOVERS, false);
    CHECK_FALSE(DsProvFu().MergesBreakpointLeftovers(DefFu("Apex of Power")));
}

TEST_CASE("Exact walk: a creature-only source's mana is {C} for a noncreature cast (MTG_EXACT_WALK_CCO)")
{
    // Unclaimed Territory makes coloured mana for creature spells only -- {C} otherwise. Rite of Flame's
    // {R} off a lone Territory is not payable; Scourge of Valkas's {R}{R}{R} off five Territories is.
    const GameState s1 = BoardFu({ "Unclaimed Territory" }, { "Rite of Flame" });
    const std::vector<Action> rite = { CastFu(s1, "Rite of Flame") };
    const GameState s5 = BoardFu(std::vector<std::string>(5, "Unclaimed Territory"), { "Scourge of Valkas" });
    const std::vector<Action> scourge = { CastFu(s5, "Scourge of Valkas") };
    {
        ArmFu arm(heurarm::EXACT_WALK_CCO, true);
        CHECK(ExactOrderFirstUnpayable(s1, rite, { 0 }) == 0);
        CHECK(ExactOrderFirstUnpayable(s5, scourge, { 0 }) == -1);
    }
    {
        ArmFu arm(heurarm::EXACT_WALK_CCO, false);
        CHECK(ExactOrderFirstUnpayable(s1, rite, { 0 }) == -1);   // the historical wild booking
    }
}

TEST_CASE("Rituals-for-payoff guard: an Apex line keeps its rituals, a restrictor still has to earn its place (MTG_RITUAL_GUARD_APEX)")
{
    // Eight Mountains, hand Rite of Flame, Seething Song, Apex of Power (+ Irencrag Feat): Rite + Seething +
    // Apex costs 14 against 8 + 7 floated -- one to spare, so the guard called Rite "surplus" and rejected
    // the chain, and the accel-prefix collapse never offers the Rite-less {Seething, Apex}: no Apex line was
    // left at all (s7600011 T6). Apex's value is its exile, paid by the mana after it, so the rituals stay.
    // Irencrag is a cast restrictor and still has to be needed: {.., Irencrag, Apex} stays refused.
    const GameState s = BoardFu(std::vector<std::string>(8, "Mountain"),
                                { "Rite of Flame", "Seething Song", "Apex of Power", "Irencrag Feat" }, 6);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::RITUAL_GUARD_APEX, on);
        const TurnSolver::Plan plan = TurnSolver::Solve(s, /*is_pre_combat=*/true,
            TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
        bool apex = false, iren = false;
        for (const Action& a : plan.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Apex of Power") { apex = true; }
            if (a.card_name.str() == "Irencrag Feat") { iren = true; }
        }
        CHECK(apex == on);
        CHECK_FALSE(iren);
    }
}

namespace
{
const GenericProvider& GenProvFu()
{
    static const GenericProvider p;
    return p;
}

std::vector<std::string> OrderNamesFu(const std::vector<Action>& acts, const std::vector<int>& order)
{
    std::vector<std::string> v;
    for (int i : order) { v.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return v;
}
}   // namespace

TEST_CASE("Payable order, every deck: the exact walk keeps a cast cap and a later cast's colour (MTG_PAYABLE_ORDER_EXACT)")
{
    // (a) hinata d0 gi1427's shape, on a provider that does NOT opt into ExactOrderWalk. Four Mountains,
    // given "Lathliss, Irencrag Feat, Pyretic Ritual" (Lathliss unpayable first). The plain walk ignores
    // Irencrag's "only one more spell" and takes the nearest order it can pay, "Irencrag, Pyretic,
    // Lathliss" -- whose Lathliss is the SECOND spell after Irencrag. The exact walk takes "Pyretic,
    // Irencrag, Lathliss": 4 -> 5 -> 8 red, Lathliss the one permitted cast.
    GameState s = BoardFu(std::vector<std::string>(4, "Mountain"),
                          { "Lathliss, Dragon Queen", "Irencrag Feat", "Pyretic Ritual" });
    s.m_provider = &GenProvFu();
    const std::vector<Action> acts = { CastFu(s, "Lathliss, Dragon Queen"), CastFu(s, "Irencrag Feat"),
                                       CastFu(s, "Pyretic Ritual") };
    // (b) gi1680's shape: Island + City of Brass, "Sol Ring, Gamble". It pays as given (the Island pays
    // Sol Ring's {1}, the City Gamble's {R}); the plain walk spent the City's wild unit on the {1}, read
    // the order as unpayable and swapped Gamble in front of the Sol Ring.
    GameState s2 = BoardFu({ "Island", "City of Brass" }, { "Sol Ring", "Gamble" });
    s2.m_provider = &GenProvFu();
    Action ring = CastFu(s2, "Sol Ring");
    ring.rock_mana.colorless = 2;   // the same-turn rock credit the enumeration stamps
    const std::vector<Action> acts2 = { ring, CastFu(s2, "Gamble") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYABLE_ORDER_EXACT, on);
        std::vector<int> order = { 0, 1, 2 };
        ApplyPayableCastOrder(s, acts, order);
        if (on)
        { CHECK(OrderNamesFu(acts, order) == std::vector<std::string>{ "Pyretic Ritual", "Irencrag Feat", "Lathliss, Dragon Queen" }); }
        else
        { CHECK(OrderNamesFu(acts, order) == std::vector<std::string>{ "Irencrag Feat", "Pyretic Ritual", "Lathliss, Dragon Queen" }); }
        std::vector<int> order2 = { 0, 1 };
        ApplyPayableCastOrder(s2, acts2, order2);
        if (on) { CHECK(OrderNamesFu(acts2, order2) == std::vector<std::string>{ "Sol Ring", "Gamble" }); }
        else    { CHECK(OrderNamesFu(acts2, order2) == std::vector<std::string>{ "Gamble", "Sol Ring" }); }
    }
}

TEST_CASE("Payable order: a live Hinata discount keeps the order as given (MTG_PAYABLE_ORDER_DYNAMIC_DECLINE)")
{
    // Hinata, Dawn-Crowned in play, Island + Mountain, two opponent permanents: Magma Opus ({6}{U}{R}, up to
    // six targets among permanents and players) is stamped at {U}{R}. Given "Magma Opus, Rite of Flame" the
    // Rite's {R} is unpaid; "Rite, Opus" would pay. That walk prices the Opus at a board the line changes,
    // so -- as LadderProjectable already does -- the order is kept. Lever off: walked and permuted.
    GameState s = BoardFu({ "Island", "Mountain" }, { "Magma Opus", "Rite of Flame" });
    s.m_provider = &GenProvFu();
    PutFu(s, "Hinata, Dawn-Crowned", 400);
    for (int k = 0; k < 2; ++k)
    {
        Permanent p;
        p.card             = DefFu("Mountain").card;
        p.card.m_number    = 500 + k;
        p.controller_index = 1;
        p.owner_index      = 1;
        s.battlefield.push_back(p);
    }
    const std::vector<Action> acts = { CastFu(s, "Magma Opus"), CastFu(s, "Rite of Flame") };
    REQUIRE(HinataGenericDiscount(DefFu("Magma Opus"), s, 0) == 6);
    REQUIRE(acts[0].cost.ManaValue() == 2);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYABLE_ORDER_DYNAMIC_DECLINE, on);
        std::vector<int> order = { 0, 1 };
        ApplyPayableCastOrder(s, acts, order);
        if (on) { CHECK(OrderNamesFu(acts, order) == std::vector<std::string>{ "Magma Opus", "Rite of Flame" }); }
        else    { CHECK(OrderNamesFu(acts, order) == std::vector<std::string>{ "Rite of Flame", "Magma Opus" }); }
    }
}

TEST_CASE("Irencrag needs a closer after it: subset and post-Apex leftovers (MTG_DRAGONSTORM_IRENCRAG_CLOSER)")
{
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    ArmFu lo(heurarm::DRAGONSTORM_BP_LEFTOVERS, true);
    const GameState s = BoardFu(std::vector<std::string>(8, "Mountain"),
                                { "Seething Song", "Apex of Power", "Irencrag Feat", "Scourge of Valkas" }, 6);
    const std::vector<Action> cands = { CastFu(s, "Seething Song"), CastFu(s, "Apex of Power"),
                                        CastFu(s, "Irencrag Feat"), CastFu(s, "Scourge of Valkas") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, on);
        // SUBSET. {Seething, Irencrag}: nothing can spend Irencrag's seven red -> refused. With an Apex the
        // exile may bring the closer (s8500074 T6 found two Scourges there) -> admitted; with a Dragon, admitted.
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 2 }) == !on);
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1, 2 }));
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1, 2, 3 }));
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 1 }));   // no restrictor: untouched
        // POST-APEX MERGE. An empty continuation + the leftover Irencrag: not cast (s7000035 T5) ...
        {
            std::vector<Action> cont;
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == (on ? std::vector<std::string>{}
                                           : std::vector<std::string>{ "Irencrag Feat" }));
        }
        // ... but a continuation Dragon is its closer, and a continuation Apex may bring one: kept.
        {
            std::vector<Action> cont = { CastFu(s, "Scourge of Valkas") };
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Irencrag Feat", "Scourge of Valkas" });
        }
        {
            std::vector<Action> cont = { CastFu(s, "Apex of Power") };
            bool searched = false;
            CHECK(MergeBreakpointLeftovers(s, cont, searched, { CastFu(s, "Irencrag Feat") }));
            CHECK(CastNamesFu(cont) == std::vector<std::string>{ "Apex of Power", "Irencrag Feat" });
        }
    }
}

namespace
{
// Scourge of Valkas (4/4, "{R}: +1/+0") attacking with two Mountains up and one BANKED land: a Sandstone
// Needle with two depletion counters or a Mercadian Bazaar with three storage counters.
GameState PumpBoardFu(const std::string& banked, int opp_life)
{
    GameState s = BoardFu({ "Mountain", "Mountain", banked }, {}, 6);
    Permanent& b = s.battlefield.back();
    if (banked == "Sandstone Needle") { PutDepletionCounters(s, b, 2); }
    else                              { b.storage_counters = 3; }
    PutFu(s, "Scourge of Valkas", 400);
    s.players[1].life = opp_life;
    return s;
}

const Permanent& PermFu(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return p; } }
    FAIL("not on the battlefield: " << name);
    return s.battlefield.front();
}

int DepletionFu(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::Depletion) { n += c.count; } }
    return n;
}
}   // namespace

TEST_CASE("Combat pump keeps a depletion / storage land's mana unless the pump is lethal (MTG_FB_HOLD_BANKED)")
{
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::FB_HOLD_BANKED, on);
        {
            // Not lethal (20 life): the Mountains pump twice (4 + 2); the Needle's two counters stay unless off.
            GameState s = PumpBoardFu("Sandstone Needle", 20);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == (on ? 14 : 12));
            CHECK(DepletionFu(PermFu(s, "Sandstone Needle")) == (on ? 2 : 1));
        }
        {
            // Lethal only WITH the Needle (8 life = 4 + 2 Mountains + 2 Needle): it is spent on both arms.
            GameState s = PumpBoardFu("Sandstone Needle", 8);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life <= 0);
        }
        {
            // The Bazaar's three banked counters, the same rule.
            GameState s = PumpBoardFu("Mercadian Bazaar", 20);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == (on ? 14 : 11));
            CHECK(PermFu(s, "Mercadian Bazaar").storage_counters == (on ? 3 : 0));
        }
    }
}

TEST_CASE("Payoff prune: a ritual that funds Ruby Medallion is not burnt for nothing (MTG_PAYOFF_PRUNE_REDUCER_SINK)")
{
    // dragonstorm ab5 d3 gi 289's T1: one Mountain, Dragonstorm in hand. Rite of Flame -> Ruby Medallion (and
    // the storm on T3) was deleted by the payoff prune -- a ritual with no Dragon / Dragonstorm / Apex.
    const GameState s = BoardFu({ "Mountain" },
                                { "Rite of Flame", "Ruby Medallion", "Dragonstorm", "Pyretic Ritual", "Seething Song" }, 1);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYOFF_PRUNE_REDUCER_SINK, on);
        bool rite_medallion = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            int casts = 0;
            for (const Action& x : p.actions) { if (x.kind == Action::Kind::CastFromHand) { ++casts; } }
            if (casts == 2 && CastsBoth(p, "Rite of Flame", "Ruby Medallion")) { rite_medallion = true; }
        }
        CHECK(rite_medallion == on);
    }
}

TEST_CASE("Combat pump spends the banked mana when only that buys a next-turn kill (MTG_FB_HOLD_BANKED_NEXT_TURN)")
{
    // Full pump now 8 (Scourge 4 + two Mountains + the Needle's {R}{R}), held pump 6. Next turn the
    // projection draws (the library is Mountains) and makes the land drop: a third Mountain, so 9 with the Needle.
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::FB_HOLD_BANKED_NEXT_TURN, on);
        {
            // 16 life: spent now -> 8, and next turn's 9 kills; held -> 10, and next turn's 9 does not
            // (dragonstorm ab5 d3 gi 60's race). The Needle is spent only with the lever.
            GameState s = PumpBoardFu("Sandstone Needle", 16);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == (on ? 8 : 10));
            CHECK(DepletionFu(PermFu(s, "Sandstone Needle")) == (on ? 1 : 2));
        }
        {
            // 14 life: next turn kills from either world (6 or 8 left, 9 damage) -> the bank is kept on both arms.
            GameState s = PumpBoardFu("Sandstone Needle", 14);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == 8);
            CHECK(DepletionFu(PermFu(s, "Sandstone Needle")) == 2);
        }
        {
            // 20 life: no next-turn kill in either world -> held on both arms (the MTG_FB_HOLD_BANKED case).
            GameState s = PumpBoardFu("Sandstone Needle", 20);
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == 14);
        }
        {
            // The Needle's LAST counter, 16 life: spending it sacrifices the Needle, so next turn swings for
            // 7 into 8 -- no kill; held, next turn's 9 does not kill 10 either -> held on both arms. (Without
            // the depletion sweep in the projection the sacked Needle paid again next turn and read lethal.)
            GameState s = PumpBoardFu("Sandstone Needle", 16);
            for (Permanent& p : s.battlefield)
            { for (Counter& c : p.counters) { if (c.type == Counter::Type::Depletion) { c.count = 1; } } }
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == 10);
            CHECK(DepletionFu(PermFu(s, "Sandstone Needle")) == 1);
        }
    }
}

TEST_CASE("Searched banked hold: the pin takes the other side of the verdict (MTG_FB_HOLD_SEARCHED)")
{
    // dragonstorm overnight d3 s7007 gi466 T5: the verdict held both Needles (a non-lethal pump, no
    // next-turn kill in the projection), and the T6 kill (Scourge #2's ETB pings) needed them spent.
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::FB_HOLD_SEARCHED, on);
        {
            // 20 life, the verdict holds (MTG_FB_HOLD_BANKED's case): the pinned other side spends the
            // Needle (4 + 2 Mountains + {R}{R}) -- with the lever only. The pin is consumed either way.
            GameState s = PumpBoardFu("Sandstone Needle", 20);
            s.scripted_fb_hold = 1;
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == (on ? 12 : 14));
            CHECK(DepletionFu(PermFu(s, "Sandstone Needle")) == (on ? 1 : 2));
            CHECK(s.scripted_fb_hold == -1);
        }
        {
            // Rank 0 is the verdict itself.
            GameState s = PumpBoardFu("Sandstone Needle", 20);
            s.scripted_fb_hold = 0;
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == 14);
        }
        {
            // 16 life, the verdict SPENDS (only spending kills next turn, MTG_FB_HOLD_BANKED_NEXT_TURN): the
            // other side holds.
            GameState s = PumpBoardFu("Sandstone Needle", 16);
            s.scripted_fb_hold = 1;
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life == (on ? 10 : 8));
        }
        {
            // Uncontested -- lethal with the Needle (8 life): holding is dominated, the pin is ignored.
            GameState s = PumpBoardFu("Sandstone Needle", 8);
            s.scripted_fb_hold = 1;
            RolloutSimulateCombat(s);
            CHECK(s.players[1].life <= 0);
        }
        {
            // The axis: a pre-combat list on the pump board carries the other-side variant, with the lever only.
            GameState s = PumpBoardFu("Sandstone Needle", 20);
            s.players[0].hand.push_back(CardFu("Lotus Bloom", 50));   // a non-empty list: suspend / pass
            int variants = 0;
            for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
            { if (p.fb_hold_choice == 1) { ++variants; } }
            CHECK((variants > 0) == on);
            // ...and never on a post-combat list (its pin could not be read).
            int m2 = 0;
            for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
            { if (p.fb_hold_choice >= 0) { ++m2; } }
            CHECK(m2 == 0);
        }
    }
}

TEST_CASE("Payoff prune: the reducer sink admits a ritual only when the Medallion needs it (MTG_PAYOFF_REDUCER_SINK_NEEDED)")
{
    // dragonstorm overnight d3 s7007 gi466 T2: Territory + Mountain pay Ruby Medallion by themselves, so
    // {Rite, Medallion, Desperate, Seething} burnt three rituals for nothing and the search tied it with the
    // Medallion-only line. Two Mountains here; the one-Mountain "Rite -> Medallion" stays (the test above).
    const GameState s = BoardFu({ "Mountain", "Mountain" },
                                { "Rite of Flame", "Ruby Medallion", "Desperate Ritual", "Seething Song" }, 2);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYOFF_REDUCER_SINK_NEEDED, on);
        bool rite_medallion = false, medallion_alone = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            int casts = 0;
            for (const Action& x : p.actions) { if (x.kind == Action::Kind::CastFromHand) { ++casts; } }
            if (CastsBoth(p, "Rite of Flame", "Ruby Medallion")) { rite_medallion = true; }
            if (casts == 1 && CastsBoth(p, "Ruby Medallion", "Ruby Medallion")) { medallion_alone = true; }
        }
        CHECK(rite_medallion == !on);
        CHECK(medallion_alone);
    }
    // The same game's land-less twin: the lone Unclaimed Territory ({C} for a noncreature spell) cannot pay
    // Rite of Flame's {R}, nor Medallion's {2}, so every Rite + Medallion subset is a PHANTOM the flat sum
    // admitted (it sorted first among the tied T2 plans and the executor cast nothing).
    const GameState t = BoardFu({ "Unclaimed Territory" },
                                { "Rite of Flame", "Ruby Medallion", "Desperate Ritual", "Seething Song" }, 2);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYOFF_REDUCER_SINK_NEEDED, on);
        bool rite_medallion = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, /*is_pre_combat=*/true))
        { if (CastsBoth(p, "Rite of Flame", "Ruby Medallion")) { rite_medallion = true; } }
        CHECK(rite_medallion == !on);
    }
    // Minimal funding, the same game's T1 on one Mountain: Rite of Flame funds the Medallion and stays;
    // {Rite, Desperate, Medallion} floats Desperate's {R} for nothing beside it and goes.
    const GameState u = BoardFu({ "Mountain" },
                                { "Rite of Flame", "Ruby Medallion", "Desperate Ritual", "Seething Song" }, 1);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::PAYOFF_REDUCER_SINK_NEEDED, on);
        bool rite_medallion = false, with_desperate = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, /*is_pre_combat=*/true))
        {
            if (!CastsBoth(p, "Rite of Flame", "Ruby Medallion")) { continue; }
            int casts = 0;
            for (const Action& x : p.actions) { if (x.kind == Action::Kind::CastFromHand) { ++casts; } }
            if (casts == 2) { rite_medallion = true; }
            if (CastsBoth(p, "Desperate Ritual", "Ruby Medallion")) { with_desperate = true; }
        }
        CHECK(rite_medallion);
        CHECK(with_desperate == !on);
    }
    // ...but not beside a BANKED land: dragonstorm smoke d3 s1001 gi67 T3, Mountain + Mercadian Bazaar. Rite
    // of Flame pays the Medallion off the Mountain so the Bazaar banks a counter for the T5 Dragonstorm;
    // the ritual-free sibling may tap the Bazaar, so it does not dominate and {Rite, Medallion} stays.
    GameState v = BoardFu({ "Mountain", "Mercadian Bazaar" },
                          { "Rite of Flame", "Ruby Medallion", "Desperate Ritual", "Dragonstorm" }, 3);
    v.battlefield.back().storage_counters = 1;
    {
        ArmFu arm(heurarm::PAYOFF_REDUCER_SINK_NEEDED, true);
        bool rite_medallion = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(v, /*is_pre_combat=*/true))
        {
            int casts = 0;
            for (const Action& x : p.actions) { if (x.kind == Action::Kind::CastFromHand) { ++casts; } }
            if (casts == 2 && CastsBoth(p, "Rite of Flame", "Ruby Medallion")) { rite_medallion = true; }
        }
        CHECK(rite_medallion);
    }
}

namespace
{
// Bind the applying line's unpaid total for one test payment (the plan-apply paths bind it in play).
struct LineCostFu
{
    ManaCost prev;
    explicit LineCostFu(const ManaCost& c) : prev(g_line_unpaid_cost) { g_line_unpaid_cost = c; }
    ~LineCostFu() { g_line_unpaid_cost = prev; }
};
}   // namespace

TEST_CASE("Line float: a spare source pays the generic before floating colour the line still owes (MTG_LINE_FLOAT_TAP_FIRST)")
{
    // s7100081 T2's shape: an Unclaimed Territory up, {R}{R} floating off Rite of Flame, and Ruby Medallion's {2}
    // to pay with Pyretic Ritual ({1}{R}) still to come in the line. The Territory makes only {C} for the
    // Medallion -- spare -- so it pays one generic and one {R} stays floating for Pyretic.
    const GameState s0 = BoardFu({ "Unclaimed Territory" }, {}, 5);
    ManaCost med;
    med.generic = 2;
    ManaCost owes_red = med;     // + Pyretic {1}{R}
    owes_red.generic += 1;
    owes_red.red = 1;
    ManaCost owes_black = med;   // + Kolaghan's {B} (and generic): the Territory may be its only {B}
    owes_black.generic += 5;
    owes_black.black = 1;
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::LINE_FLOAT_TAP_FIRST, on);
        {
            GameState s = s0;
            s.floating_mana.red = 2;
            LineCostFu line(owes_red);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(s.floating_mana.red == (on ? 1 : 0));
            CHECK(PermFu(s, "Unclaimed Territory").tapped == on);
        }
        {
            // The guard: the line owes {B} that neither the float nor another source makes -> the float pays.
            GameState s = s0;
            s.floating_mana.red = 2;
            LineCostFu line(owes_black);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(s.floating_mana.red == 0);
            CHECK_FALSE(PermFu(s, "Unclaimed Territory").tapped);
        }
        {
            // No live line: the plain drain on both arms.
            GameState s = s0;
            s.floating_mana.red = 2;
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(s.floating_mana.red == 0);
        }
        {
            // The named case under the unseen guard: a line that names every cast it makes (no breakpoint
            // opener) is untouched by MTG_LINE_FLOAT_UNSEEN_GUARD -- the Territory still pays the generic.
            ArmFu guard(heurarm::LINE_FLOAT_UNSEEN_GUARD, true);
            GameState s = s0;
            s.floating_mana.red = 2;
            LineCostFu line(owes_red);
            LineOpensUnseenScope lou(false);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(s.floating_mana.red == (on ? 1 : 0));
            CHECK(PermFu(s, "Unclaimed Territory").tapped == on);
        }
    }
}

TEST_CASE("Line float: a line that will reveal cards it cannot name keeps the creature-only-colour land (MTG_LINE_FLOAT_UNSEEN_GUARD)")
{
    // dragonstorm s4050 g46 / s5254 g249 T4's shape: Unclaimed Territory + Mountain up, Lotus Bloom's {R}{R}{R}
    // floating, the first Seething Song's {1}{R} (Ruby Medallion out) to pay, and the line "Seething Song,
    // Seething Song, Apex of Power" owing only red after it. Apex then exiles Dragonlord Kolaghan, whose {B}
    // only the Territory makes -- but no pip the line NAMES is black, so without the guard the Territory
    // reads spare and pays the generic, and the T4 kill is gone (the payment is not searched).
    const GameState s0 = BoardFu({ "Unclaimed Territory", "Mountain" }, {}, 4);
    ManaCost seething;
    seething.generic = 1;
    seething.red     = 1;
    ManaCost line;               // Seething Song x2 + Apex of Power, each one less under the Medallion
    line.generic = 8;
    line.red     = 5;
    ArmFu rung(heurarm::LINE_FLOAT_TAP_FIRST, true);
    for (const bool guard : { true, false })
    {
        CAPTURE(guard);
        ArmFu arm(heurarm::LINE_FLOAT_UNSEEN_GUARD, guard);
        {
            // A line with no breakpoint opener: the rung pays the generic with the Territory on both arms.
            GameState s = s0;
            s.floating_mana.red = 3;
            LineCostFu lc(line);
            REQUIRE(TapForCostDirect(s, seething, /*for_creature=*/false));
            CHECK(PermFu(s, "Unclaimed Territory").tapped);
            CHECK(s.floating_mana.red == 2);
        }
        {
            // The line casts Apex of Power: with the guard the Territory stays up and the float pays (the rituals
            // refill the red, so nothing the line names is lost); without it, the rung as before.
            GameState s = s0;
            s.floating_mana.red = 3;
            LineCostFu lc(line);
            LineOpensUnseenScope lou(true);
            REQUIRE(TapForCostDirect(s, seething, /*for_creature=*/false));
            CHECK(PermFu(s, "Unclaimed Territory").tapped == !guard);
            CHECK(s.floating_mana.red == (guard ? 1 : 2));
            CHECK_FALSE(PermFu(s, "Mountain").tapped);
        }
    }
}

TEST_CASE("Line float unseen guard yields when the named line is short without the rung (MTG_LINE_FLOAT_UNSEEN_GUARD)")
{
    // dragonstorm s1112019 gi19 T3: the rung's own named shape (Mountain tapped for Rite of Flame, {R}{R}
    // floating, Ruby Medallion's {2}, Unclaimed Territory the only untapped land) with Apex of Power in the
    // line. Held, the float pays both pips and Desperate Ritual's {R} has nothing left to pay it; the rung
    // must fire whatever the line might reveal later.
    ManaCost med;
    med.generic = 2;
    ManaCost owes;               // + Desperate Ritual {R} (under the Medallion) and the rest of the line
    owes.generic = 8;
    owes.red     = 5;
    ArmFu rung(heurarm::LINE_FLOAT_TAP_FIRST, true);
    for (const bool guard : { true, false })
    {
        CAPTURE(guard);
        ArmFu arm(heurarm::LINE_FLOAT_UNSEEN_GUARD, guard);
        {
            // No red provider left after the plain payment: the rung fires on both arms.
            GameState s = BoardFu({ "Unclaimed Territory" }, {}, 3);
            s.floating_mana.red = 2;
            LineCostFu lc(owes);
            LineOpensUnseenScope lou(true);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(PermFu(s, "Unclaimed Territory").tapped);
            CHECK(s.floating_mana.red == 1);
        }
        {
            // An untapped Mountain still makes the owed {R}: not short, so the guard holds the Territory.
            GameState s = BoardFu({ "Unclaimed Territory", "Mountain" }, {}, 3);
            s.floating_mana.red = 2;
            LineCostFu lc(owes);
            LineOpensUnseenScope lou(true);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(PermFu(s, "Unclaimed Territory").tapped == !guard);
            CHECK(s.floating_mana.red == (guard ? 0 : 1));
            CHECK_FALSE(PermFu(s, "Mountain").tapped);
        }
    }
}

TEST_CASE("Line float unseen guard: the line's breakpoint opener is the one ParamKeyedDrawClass names (MTG_LINE_FLOAT_UNSEEN_GUARD)")
{
    const GameState s = BoardFu({ "Mountain" }, { "Seething Song", "Apex of Power", "Rite of Flame" }, 4);
    const std::vector<Action> with_apex = { CastFu(s, "Seething Song"), CastFu(s, "Apex of Power") };
    const std::vector<Action> rituals   = { CastFu(s, "Rite of Flame"), CastFu(s, "Seething Song") };
    for (const bool rung_on : { true, false })
    {
        for (const bool guard : { true, false })
        {
            CAPTURE(rung_on);
            CAPTURE(guard);
            ArmFu rung(heurarm::LINE_FLOAT_TAP_FIRST, rung_on);
            ArmFu arm(heurarm::LINE_FLOAT_UNSEEN_GUARD, guard);
            // Only the live guard scans (=0, or the rung off, binds false: byte-identical to before).
            CHECK(LineOpensUnseenCasts(s, with_apex) == (rung_on && guard));
            CHECK_FALSE(LineOpensUnseenCasts(s, rituals));
        }
    }
    CHECK(CastOpensUnseen(s, CastFu(s, "Apex of Power")));
    CHECK_FALSE(CastOpensUnseen(s, CastFu(s, "Seething Song")));
}

namespace
{
// dragonstorm s3104 gi101 T2: Sandstone Needle (two depletion counters; {T}: {R}{R}) + Unclaimed Territory
// ({C} for a noncreature spell), hand Ruby Medallion, Rite of Flame, Seething Song x2, Dragonstorm.
GameState NeedleTerritoryFu()
{
    GameState s = BoardFu({ "Sandstone Needle", "Unclaimed Territory" },
                          { "Seething Song", "Seething Song", "Rite of Flame", "Dragonstorm", "Ruby Medallion" }, 2);
    for (Permanent& p : s.battlefield)
    { if (p.card.m_name.str() == "Sandstone Needle") { PutDepletionCounters(s, p, 2); } }
    return s;
}

// The USER order's line on that board, as the plan stamps it (Medallion not yet on the battlefield).
std::vector<Action> NeedleLineFu(const GameState& s)
{
    return { CastFu(s, "Ruby Medallion"), CastFu(s, "Rite of Flame"), CastFu(s, "Seething Song"),
             CastFu(s, "Seething Song"), CastFu(s, "Dragonstorm") };
}
}   // namespace

TEST_CASE("Line-spare generic: a colour-short line pays a generic pip with the spare {C} first (MTG_LINE_SPARE_GENERIC_FIRST)")
{
    const GameState s0 = NeedleTerritoryFu();
    const std::vector<Action> line = NeedleLineFu(s0);
    ManaCost total;
    for (const Action& a : line) { AddManaCost(total, a.cost); }
    ManaCost med;
    med.generic = 2;
    ManaCost rite;
    rite.red = 1;
    // The exact walk (lever-independent) reads the USER order as payable: the {C} and one {R} on the
    // Medallion, the other {R} floating for Rite. The payer must realise the same payment.
    CHECK(ExactOrderFirstUnpayable(s0, line, { 0, 1, 2, 3, 4 }) == -1);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::LINE_SPARE_GENERIC_FIRST, on);
        {
            GameState s = s0;
            LineCostFu lc(total);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK(PermFu(s, "Sandstone Needle").tapped);
            CHECK(PermFu(s, "Unclaimed Territory").tapped == on);
            CHECK(s.floating_mana.red == (on ? 1 : 0));
            // ...so Rite of Flame's {R} is payable next (off: only the Territory's {C} is left).
            CHECK(TapForCostDirect(s, rite, /*for_creature=*/false) == on);
        }
        {
            // No live line: the historical tap on both arms (the Needle pays both pips, the Territory stays up).
            GameState s = s0;
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            CHECK_FALSE(PermFu(s, "Unclaimed Territory").tapped);
            CHECK(s.floating_mana.red == 0);
        }
    }
    {
        // NOT SHORT: with two Mountains as well the board alone makes every {R} the rest of a Medallion ->
        // Rite line owes, so the rule stands down and both arms tap exactly alike.
        GameState sa = NeedleTerritoryFu();
        PutFu(sa, "Mountain", 350);
        PutFu(sa, "Mountain", 351);
        ManaCost owes = med;
        owes.red = 1;
        std::vector<bool> tapped_on, tapped_off;
        for (const bool on : { true, false })
        {
            ArmFu arm(heurarm::LINE_SPARE_GENERIC_FIRST, on);
            GameState s = sa;
            LineCostFu lc(owes);
            REQUIRE(TapForCostDirect(s, med, /*for_creature=*/false));
            for (const Permanent& p : s.battlefield) { (on ? tapped_on : tapped_off).push_back(p.tapped); }
        }
        CHECK(tapped_on == tapped_off);
    }
}

TEST_CASE("Line-spare generic, apply level: the USER order's T2 line on Needle + Territory casts Dragonstorm (MTG_LINE_SPARE_GENERIC_FIRST)")
{
    // The rollout apply (ApplyPlanDirect) pays through the same shared payer as the executor: the whole
    // ordered line "Medallion, Rite, Seething, Seething, Dragonstorm" resolves (s3104 gi101's T2 kill).
    // Lever off: the Medallion's {2} takes both of the Needle's {R}, Rite is unpayable and the line dies.
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::LINE_SPARE_GENERIC_FIRST, on);
        ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
        GameState s = NeedleTerritoryFu();
        TurnSolver::Plan plan;
        plan.actions = NeedleLineFu(s);
        plan.land_decided = true;
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        int in_hand_storm = 0, in_hand_rite = 0;
        for (const Card& c : s.players[0].hand)
        {
            if (c.m_name.str() == "Dragonstorm")   { ++in_hand_storm; }
            if (c.m_name.str() == "Rite of Flame") { ++in_hand_rite; }
        }
        CHECK(in_hand_storm == (on ? 0 : 1));
        CHECK(in_hand_rite == (on ? 0 : 1));
    }
}

TEST_CASE("Dragon order: the searched orderings try every order of the hard-cast Dragons (MTG_DRAGONSTORM_DRAGON_ORDER)")
{
    // s3050 gi47 T5 after Apex: Desperate x2, Utvara Hellkite, Lathliss, Irencrag Feat in exile (plan) order. The
    // canonical order keeps Utvara before Lathliss and puts Irencrag before the LAST Dragon: "Utvara, Irencrag,
    // Lathliss" -- Lathliss enters last, Utvara's entry makes no 5/5. "Lathliss, Irencrag, Utvara" is the
    // other order the USER's rules allow (they never rank one Dragon against another).
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    const GameState s = BoardFu(std::vector<std::string>(10, "Mountain"),
                                { "Desperate Ritual", "Desperate Ritual", "Utvara Hellkite", "Lathliss, Dragon Queen",
                                  "Irencrag Feat" }, 5);
    const std::vector<Action> acts = { CastFu(s, "Desperate Ritual"), CastFu(s, "Desperate Ritual"),
                                       CastFu(s, "Utvara Hellkite"), CastFu(s, "Lathliss, Dragon Queen"),
                                       CastFu(s, "Irencrag Feat") };
    const std::vector<std::string> canon = { "Desperate Ritual", "Desperate Ritual", "Utvara Hellkite", "Irencrag Feat",
                                             "Lathliss, Dragon Queen" };
    const std::vector<std::string> lath_first = { "Desperate Ritual", "Desperate Ritual", "Lathliss, Dragon Queen",
                                                  "Irencrag Feat", "Utvara Hellkite" };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::DRAGONSTORM_DRAGON_ORDER, on);
        std::vector<std::vector<int>> out;
        REQUIRE(DsProvFu().CastOrderingCandidates(s, acts, acts, out));
        REQUIRE_FALSE(out.empty());
        CHECK(OrderNamesFu(acts, out[0]) == canon);   // the canonical order stays first (wins every tie)
        bool has = false;
        for (const std::vector<int>& o : out) { if (OrderNamesFu(acts, o) == lath_first) { has = true; } }
        CHECK(has == on);
        CHECK(out.size() == (on ? 2u : 1u));
        // Every candidate pays (the exact walk): 10 + 2 = 12 -> a Dragon (6 or 8) -> Irencrag (+7) -> the other.
        for (const std::vector<int>& o : out) { CHECK(ExactOrderFirstUnpayable(s, acts, o) == -1); }
    }
    {
        // Without Irencrag, two Dragons: both orders offered; one Dragon: nothing added.
        ArmFu arm(heurarm::DRAGONSTORM_DRAGON_ORDER, true);
        const GameState s2 = BoardFu(std::vector<std::string>(14, "Mountain"),
                                     { "Utvara Hellkite", "Lathliss, Dragon Queen" }, 5);
        const std::vector<Action> two = { CastFu(s2, "Utvara Hellkite"), CastFu(s2, "Lathliss, Dragon Queen") };
        std::vector<std::vector<int>> out;
        REQUIRE(DsProvFu().CastOrderingCandidates(s2, two, two, out));
        CHECK(out.size() == 2u);
        const std::vector<Action> one = { CastFu(s2, "Utvara Hellkite") };
        std::vector<std::vector<int>> out1;
        REQUIRE(DsProvFu().CastOrderingCandidates(s2, one, one, out1));
        CHECK(out1.size() == 1u);
    }
}

TEST_CASE("Dragon order, apply level: Lathliss before the other Dragon makes the 5/5 (MTG_DRAGONSTORM_DRAGON_ORDER)")
{
    // The searched order is realised as given (Plan::searched_order): "Lathliss, Irencrag, Utvara" on 12 red --
    // Utvara enters after Lathliss and her trigger makes a 5/5 Dragon token; the canonical order makes none.
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    for (const bool lath_first : { true, false })
    {
        CAPTURE(lath_first);
        GameState s = BoardFu(std::vector<std::string>(12, "Mountain"),
                              { "Utvara Hellkite", "Lathliss, Dragon Queen", "Irencrag Feat" }, 5);
        TurnSolver::Plan plan;
        if (lath_first)
        { plan.actions = { CastFu(s, "Lathliss, Dragon Queen"), CastFu(s, "Irencrag Feat"), CastFu(s, "Utvara Hellkite") }; }
        else
        { plan.actions = { CastFu(s, "Utvara Hellkite"), CastFu(s, "Irencrag Feat"), CastFu(s, "Lathliss, Dragon Queen") }; }
        plan.searched_order = true;
        plan.land_decided = true;
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        int creatures = 0, tokens = 0;
        for (const Permanent& p : s.battlefield)
        {
            if (!p.card.IsCreature()) { continue; }
            ++creatures;
            if (p.card.m_name.str() != "Utvara Hellkite" && p.card.m_name.str() != "Lathliss, Dragon Queen") { ++tokens; }
        }
        CHECK(creatures == (lath_first ? 3 : 2));
        CHECK(tokens == (lath_first ? 1 : 0));
    }
}

namespace
{
bool PlanHasSpliceFu(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.splice_count > 0) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("d0 runner: a spliced subset no legal order pays in full is refused (MTG_DRAGONSTORM_D0_SPLICE_REFUSE)")
{
    // Regression d0 s2857 gi855 T2, on two Mountains: {Seething Song, Desperate+splice, Desperate+splice,
    // Desperate, Dragonstorm} with three Desperates in hand. The order-free totals admit it -- the only order
    // that funds Seething casts a plain Desperate FIRST and then splices a copy that has left the hand. The
    // legal arrangement (the two spliced casts before the plain one) fails at its first cast, Seething, and
    // the payable fallback finds nothing legal either. The old test refuses only a failure AT a spliced cast,
    // so the d0 runner committed the set and cast one plain Desperate for nothing.
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    const GameState s = BoardFu({ "Mountain", "Mountain" },
        { "Seething Song", "Desperate Ritual", "Desperate Ritual", "Desperate Ritual", "Dragonstorm" }, 2);
    const std::vector<Action> cands = { CastFu(s, "Seething Song"), CastFu(s, "Desperate Ritual", 1),
                                        CastFu(s, "Desperate Ritual", 1), CastFu(s, "Desperate Ritual"),
                                        CastFu(s, "Dragonstorm") };
    const std::vector<int> all = { 0, 1, 2, 3, 4 };
    // A spliced set that DOES pay in a legal order (four Mountains, two copies: spliced first 4 -> 6 red,
    // then the plain copy 2 -> +3) and a non-spliced set that does not pay at all (Seething on two lands).
    const GameState s4 = BoardFu({ "Mountain", "Mountain", "Mountain", "Mountain" },
                                 { "Desperate Ritual", "Desperate Ritual", "Dragonstorm" }, 4);
    const std::vector<Action> legal = { CastFu(s4, "Desperate Ritual", 1), CastFu(s4, "Desperate Ritual") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::DRAGONSTORM_D0_SPLICE_REFUSE, on);
        // Outside the depth-0 runner (the search's rollouts and searched frames): the old test, either arm.
        REQUIRE_FALSE(TurnSolver::InD0Runner());
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, all));
        TurnSolver::GreedyPermit d0(TurnSolver::GreedySite::D0Runner, 0);
        REQUIRE(TurnSolver::InD0Runner());
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, all) == !on);
        CHECK(DsProvFu().OrderedSubsetRealisable(s4, legal, { 0, 1 }));       // a legal order pays: kept
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, { 0, 4 }));        // no splice: untouched
    }
    {
        // A USER-order provider hook: with the USER order off the whole test is off.
        ArmFu off(heurarm::DRAGONSTORM_USER_ORDER, false);
        ArmFu arm(heurarm::DRAGONSTORM_D0_SPLICE_REFUSE, true);
        TurnSolver::GreedyPermit d0(TurnSolver::GreedySite::D0Runner, 0);
        CHECK(DsProvFu().OrderedSubsetRealisable(s, cands, all));
    }
}

TEST_CASE("d0 runner: the gi855 hand on two lands does not commit the unpayable splice plan (MTG_DRAGONSTORM_D0_SPLICE_REFUSE)")
{
    // The same board through the d0 runner's own pick (TurnSolver::Solve under a D0Runner permit). With the
    // lever off it commits a spliced plan whose legal order cannot pay (the executor then casts the plain
    // Desperate alone); with it on, any spliced plan it still picks is paid in full by its realised order.
    ArmFu user(heurarm::DRAGONSTORM_USER_ORDER, true);
    GameState s = BoardFu({ "Mountain", "Mountain" },
        { "Seething Song", "Desperate Ritual", "Desperate Ritual", "Desperate Ritual", "Dragonstorm" }, 2);
    for (int k = 0; k < 4; ++k) { s.players[0].library[static_cast<std::size_t>(k)] = CardFu("Scourge of Valkas", 950 + k); }
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmFu arm(heurarm::DRAGONSTORM_D0_SPLICE_REFUSE, on);
        const TurnSolver::Plan plan = TurnSolver::Solve(s, /*is_pre_combat=*/true,
            TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
        std::vector<int> ord;
        for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
        { if (plan.actions[i].kind == Action::Kind::CastFromHand) { ord.push_back(i); } }
        std::stable_sort(ord.begin(), ord.end(),
                         [&](int x, int y) { return CastOrderLess(s, plan.actions[x], plan.actions[y]); });
        ApplyDeckCastArrangement(s, plan.actions, ord);
        ApplyPayableCastOrder(s, plan.actions, ord);
        const bool pays = ExactOrderFirstUnpayable(s, plan.actions, ord) < 0;
        if (on) { CHECK((!PlanHasSpliceFu(plan) || pays)); }
        else    { CHECK(PlanHasSpliceFu(plan)); CHECK_FALSE(pays); }   // the old commit: the waste
    }
}
