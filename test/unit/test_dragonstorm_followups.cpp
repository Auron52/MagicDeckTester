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
    }
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
