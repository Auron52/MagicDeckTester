// Brainstorm (NivMizzetCombo onboarding, 2026-10-17):
//   "Draw three cards, then put two cards from your hand on top of your library in any order."
//
// draw_spell draw 3 + put_back_from_hand 2, resolved by the shared ResolvePutBackFromHand inside the
// spell's resolution in both worlds. The put-back is SEARCHED (Plan::putback_choice, an ordinal into
// PutBackCandidates: every distinct ordered selection of hand classes, provider-ordered, identity-folded
// only). These pin:
//   1. the resolution: three drawn, two put back with element 0 on TOP, Brainstorm to the graveyard;
//   2. executor / rollout LOCKSTEP for every fanned variant (same hand, same library);
//   3. the enumeration: the variants reach EVERY distinct ordered pair of hand classes exactly once
//      (identical copies folded -- two Islands are one class), nothing else, nothing missing;
//   4. the fan width is an upper bound when the plan draws first (a Preordain ahead of it);
//   5. human play: the dig chooser is asked top-first, and -1 takes the default.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HandEntry.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "ai/HeuristicArm.h"
#include "ai/SearchBudget.h"
#include "mtg_test_seam.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr const char* kBs = "Brainstorm";

void EnsureCardsBs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefBs(const std::string& name)
{
    EnsureCardsBs();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardBs(const std::string& name, int number)
{
    Card c = DefBs(name).card;
    c.m_number = number;
    return c;
}

void PutBs(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefBs(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

// One untapped Island (exactly Brainstorm's {U}), the land drop already used, so nothing drawn can be
// played or cast afterwards -- the hand/library after the apply is the put-back's alone.
GameState BoardBs(const std::vector<std::string>& hand_rest, const std::vector<std::string>& library)
{
    EnsureCardsBs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 5151;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    PutBs(s, "Island", 1);
    s.players[0].hand.push_back(CardBs(kBs, 20));
    int h = 21;
    for (const std::string& n : hand_rest) { s.players[0].hand.push_back(CardBs(n, h++)); }
    int l = 100;
    for (const std::string& n : library) { s.players[0].library.push_back(CardBs(n, l++)); }
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(CardBs("Ponder", 200 + k)); }
    return s;
}

void ResolveStackBs(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorBs(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackBs);
    return exec;
}

bool IsSoleBs(const TurnSolver::Plan& p)
{
    return p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromHand
        && p.actions[0].card_name == kBs && p.land_to_play.empty();
}

std::vector<std::string> HandNames(const GameState& s)
{
    std::vector<std::string> v;
    for (const Card& c : s.players[0].hand) { v.push_back(c.m_name.str()); }
    std::sort(v.begin(), v.end());
    return v;
}
std::vector<int> HandNums(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].hand) { v.push_back(c.m_number); }
    std::sort(v.begin(), v.end());
    return v;
}
std::vector<int> LibNums(const GameState& s)
{
    std::vector<int> v;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) { v.push_back(s.players[0].library[i].m_number); }
    return v;
}
// The put-back's observable outcome, by CLASS (name): (top, second) + the hand multiset.
std::string OutcomeBs(const GameState& s)
{
    std::string o = s.players[0].library[0].m_name.str() + " / " + s.players[0].library[1].m_name.str() + " | ";
    for (const std::string& n : HandNames(s)) { o += n + ","; }
    return o;
}
bool InGyBs(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Brainstorm: the card data -- {U} instant, draw 3, put back 2")
{
    const CardDefinition& d = DefBs(kBs);
    CHECK(d.card.HasType(CardType::Instant));
    CHECK(d.card.m_mana_cost.ManaValue() == 1);
    CHECK(d.card.m_mana_cost.blue == 1);
    CHECK(d.tmpl == CardTemplate::DrawSpell);
    CHECK(d.params.draw == 3);
    CHECK(d.params.put_back_from_hand == 2);
}

TEST_CASE("Brainstorm: resolution draws three, then puts two back with element 0 on top (shared resolver)")
{
    GameState s = BoardBs({ "Lightning Bolt", "Goblin Guide" }, { "Birds of Paradise", "Sol Ring", "Llanowar Elves" });
    // Resolve through the EXECUTOR's stack path with a pinned candidate.
    const CardDefinition& d = DefBs(kBs);
    s.players[0].hand.erase(s.players[0].hand.begin());   // Brainstorm is on the stack now
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = CardBs(kBs, 20);
    e.controller_index = 0;
    GameState pinned = s;
    {
        // What candidate 3 is at resolution: compute on a copy that has drawn the three.
        GameState probe = s;
        DrawIntoHand(probe, 0, 3, HandEntryReason::Draw);
        const std::vector<std::vector<int>> cands = PutBackCandidates(probe, 0, 2);
        REQUIRE(cands.size() == 20u);   // 5 distinct cards -> 5 * 4 ordered pairs
        const int top_num = probe.players[0].hand[static_cast<std::size_t>(cands[3][0])].m_number;
        const int sec_num = probe.players[0].hand[static_cast<std::size_t>(cands[3][1])].m_number;
        ScriptedPutBack pin(3);
        EffectHandler::Resolve(pinned, e, d);
        CHECK(g_scripted_putback_choice == -1);              // consumed
        CHECK(pinned.players[0].library[0].m_number == top_num);
        CHECK(pinned.players[0].library[1].m_number == sec_num);
    }
    CHECK(pinned.players[0].hand.size() == 3u);              // 2 + 3 - 2
    CHECK(pinned.players[0].cards_drawn_this_turn == 3);
    CHECK(pinned.players[0].library.size() == s.players[0].library.size() - 1);   // net: -3 +2
    CHECK(InGyBs(pinned, kBs));
    // Unpinned = candidate 0, the provider's default.
    GameState def = s;
    EffectHandler::Resolve(def, e, d);
    GameState probe = s;
    DrawIntoHand(probe, 0, 3, HandEntryReason::Draw);
    const std::vector<std::vector<int>> cands = PutBackCandidates(probe, 0, 2);
    CHECK(def.players[0].library[0].m_number == probe.players[0].hand[static_cast<std::size_t>(cands[0][0])].m_number);
    CHECK(def.players[0].library[1].m_number == probe.players[0].hand[static_cast<std::size_t>(cands[0][1])].m_number);
}

TEST_CASE("Brainstorm: every distinct ordered pair of hand classes is reached exactly once, executor == rollout")
{
    // Hand after the draw: Island x2, Lightning Bolt x2, Birds, Sol Ring -> 4 classes, two of them
    // doubled: 4*3 ordered pairs of distinct classes + 2 same-class pairs = 14 distinct put-backs.
    GameState s = BoardBs({ "Island", "Island", "Lightning Bolt" }, { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    std::vector<const TurnSolver::Plan*> bs;
    for (const TurnSolver::Plan& p : plans) { if (IsSoleBs(p)) { bs.push_back(&p); } }
    REQUIRE_FALSE(bs.empty());
    std::set<int> choices;
    for (const TurnSolver::Plan* p : bs) { choices.insert(p->putback_choice); }
    CHECK(choices.count(-1) == 1);                            // the base plan (candidate 0)
    // The fan is an upper bound: at least the 13 non-default candidates, ranks 1..W-1 each once.
    CHECK(choices.size() == bs.size());
    CHECK(bs.size() >= 14u);
    std::map<std::string, int> outcomes;
    for (const TurnSolver::Plan* p : bs)
    {
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *p, true);
        GameState exec = RunExecutorBs(s, *p);
        INFO("putback_choice=" << p->putback_choice);
        CHECK(HandNums(roll) == HandNums(exec));
        CHECK(LibNums(roll) == LibNums(exec));
        CHECK(roll.players[0].hand.size() == 4u);             // 4 + 3 - 1 (Brainstorm) - 2
        CHECK(InGyBs(roll, kBs));
        ++outcomes[OutcomeBs(roll)];
    }
    CHECK(outcomes.size() == 14u);                            // every distinct put-back, no more
    // Clamped (too-wide) ranks repeat the LAST candidate; nothing else repeats.
    int repeated = 0;
    for (const auto& [o, n] : outcomes) { if (n > 1) { ++repeated; } }
    CHECK(repeated <= 1);
    // And the same multiset counter the fan uses agrees with the resolution's list.
    GameState probe = s;
    probe.players[0].hand.erase(probe.players[0].hand.begin());
    DrawIntoHand(probe, 0, 3, HandEntryReason::Draw);
    CHECK(PutBackCandidates(probe, 0, 2).size() == 14u);
    CHECK(PutBackCandidateCount(probe, probe.players[0].hand, 2) == 14);
}

TEST_CASE("Brainstorm: a cantrip earlier in the same plan still leaves every candidate reachable")
{
    // Preordain (scry 2, draw 1) ahead of Brainstorm draws/looks first; the fan must still cover the
    // resolution's candidates. Two Islands: one for each {U}.
    GameState s = BoardBs({ "Preordain", "Goblin Guide" },
                          { "Lightning Bolt", "Birds of Paradise", "Sol Ring", "Llanowar Elves", "Izzet Signet" });
    PutBs(s, "Island", 2);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int max_choice = -1;
    const TurnSolver::Plan* base = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty() || p.actions.size() != 2) { continue; }
        bool has_bs = false, has_pre = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            has_bs  |= a.card_name == kBs;
            has_pre |= a.card_name == "Preordain";
        }
        if (!has_bs || !has_pre || p.scry_choice >= 0 || p.ponder_choice >= 0) { continue; }
        if (p.putback_choice < 0 && base == nullptr) { base = &p; }
        max_choice = std::max(max_choice, p.putback_choice);
    }
    REQUIRE(base != nullptr);
    // What the resolution actually offers on this plan: apply the base and count at the put-back.
    // Re-derive by hand: Preordain draws one card, Brainstorm three -> hand at the put-back has
    // Goblin Guide + 4 drawn cards (whatever Preordain's scry left on top): at most 5 distinct -> 20.
    CHECK(max_choice + 1 >= 20);
}

TEST_CASE("Brainstorm: human play asks the dig chooser top-first; -1 takes the default")
{
    GameState s = BoardBs({ "Lightning Bolt", "Goblin Guide" }, { "Birds of Paradise", "Sol Ring", "Llanowar Elves" });
    s.players[0].hand.erase(s.players[0].hand.begin());
    DrawIntoHand(s, 0, 3, HandEntryReason::Draw);
    // Hand now: Bolt#21, Guide#22, Birds#100, Sol Ring#101, Llanowar#102.
    std::vector<std::string> asked;
    int call = 0;
    DigChooser chooser = [&](const GameState&, int, const std::string& source, const std::vector<Card>& examined,
                             const std::vector<int>& legal, int heuristic) -> int
    {
        asked.push_back(source);
        CHECK(legal.size() == examined.size());
        CHECK(heuristic >= 0);
        if (call++ == 0)
        {
            for (std::size_t i = 0; i < examined.size(); ++i) { if (examined[i].m_number == 101) { return static_cast<int>(i); } }
            return -1;
        }
        // Sol Ring is already chosen, so it is not offered again.
        for (const Card& c : examined) { CHECK(c.m_number != 101); }
        for (std::size_t i = 0; i < examined.size(); ++i) { if (examined[i].m_number == 22) { return static_cast<int>(i); } }
        return -1;
    };
    g_play_dig_chooser = &chooser;
    ResolvePutBackFromHand(s, 0, 2, kBs);
    g_play_dig_chooser = nullptr;
    REQUIRE(asked.size() == 2u);
    CHECK(asked[0] == "Brainstorm (put back: top)");
    CHECK(asked[1] == "Brainstorm (put back: second)");
    CHECK(s.players[0].library[0].m_number == 101);   // Sol Ring on top
    CHECK(s.players[0].library[1].m_number == 22);    // Goblin Guide beneath it
    CHECK(s.players[0].hand.size() == 3u);

    // -1 on both asks = the default candidate.
    GameState t = BoardBs({ "Lightning Bolt", "Goblin Guide" }, { "Birds of Paradise", "Sol Ring", "Llanowar Elves" });
    t.players[0].hand.erase(t.players[0].hand.begin());
    DrawIntoHand(t, 0, 3, HandEntryReason::Draw);
    GameState u = t;
    DigChooser decline = [](const GameState&, int, const std::string&, const std::vector<Card>&,
                            const std::vector<int>&, int) -> int { return -1; };
    g_play_dig_chooser = &decline;
    ResolvePutBackFromHand(t, 0, 2, kBs);
    g_play_dig_chooser = nullptr;
    ResolvePutBackFromHand(u, 0, 2, kBs);   // no chooser: the default
    CHECK(LibNums(t) == LibNums(u));
    CHECK(HandNums(t) == HandNums(u));
}

TEST_CASE("Brainstorm: fewer than two eligible hand cards puts back what there is")
{
    GameState s = BoardBs({}, {});
    s.players[0].hand.erase(s.players[0].hand.begin());
    s.players[0].library = Library{};
    s.players[0].library.push_back(CardBs("Sol Ring", 100));
    DrawIntoHand(s, 0, 3, HandEntryReason::Draw);    // only one card to draw
    REQUIRE(s.players[0].hand.size() == 1u);
    CHECK(PutBackCandidates(s, 0, 2).size() == 1u);
    ResolvePutBackFromHand(s, 0, 2, kBs);
    CHECK(s.players[0].hand.empty());
    REQUIRE(s.players[0].library.size() == 1u);
    CHECK(s.players[0].library[0].m_number == 100);
}

// THE FAST PATH IS THE REFERENCE, rank for rank. ResolvePutBackFromHand and the fan's width read the
// put-back list through PutBackPickAt / PutBackClassCounter, which never build it. This pins them to
// the materialised PutBackCandidates over random hands -- duplicates, staged/impulse cards (ineligible),
// a live m1_hand snapshot splitting a class, n = 1 and 2 -- and fails if a single rank, member or count
// differs (e.g. a closed-form order that merged or swapped two candidates).
TEST_CASE("Brainstorm: PutBackPickAt == PutBackCandidates rank for rank (random hands)")
{
    EnsureCardsBs();
    const std::vector<std::string> pool = {
        "Island", "Island", "Island", "Lightning Bolt", "Lightning Bolt", "Birds of Paradise", "Sol Ring",
        "Llanowar Elves", "Izzet Signet", "Ponder", "Ponder", "Preordain", "Goblin Guide", "Brainstorm" };
    std::uint64_t rng = 0x5eed1234abcdull;
    auto next = [&rng](int mod) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return static_cast<int>(rng % static_cast<std::uint64_t>(mod)); };
    int checked = 0;
    for (int trial = 0; trial < 400; ++trial)
    {
        GameState s = BoardBs({}, {});
        s.players[0].hand.clear();
        const int hn = next(12);   // 0..11 cards
        for (int i = 0; i < hn; ++i)
        {
            Card c = CardBs(pool[static_cast<std::size_t>(next(static_cast<int>(pool.size())))], 300 + i);
            const int odd = next(10);
            if (odd == 0) { c.m_is_staged = true; }
            else if (odd == 1) { c.m_impulse_no_land = true; }
            s.players[0].hand.push_back(c);
        }
        if (next(3) == 0)   // a live m1_hand snapshot naming some of the hand by number
        {
            s.m1_hand_turn = s.turn_number;
            s.m1_hand_n = 0;
            for (int i = 0; i < hn && s.m1_hand_n < 4; ++i) { if (next(2) == 0) { s.m1_hand[s.m1_hand_n++] = 300 + i; } }
        }
        for (int n = 1; n <= 2; ++n)
        {
            const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, n);
            const PutBackPick first = PutBackPickAt(s, 0, n, 0);
            INFO("trial=" << trial << " n=" << n << " hand=" << hn);
            REQUIRE(first.count == static_cast<int>(ref.size()));
            CHECK(PutBackCandidateCount(s, s.players[0].hand, n) == static_cast<int>(ref.size()));
            if (ref.empty()) { CHECK(first.take == 0); continue; }
            for (int k = 0; k < static_cast<int>(ref.size()) + 3; ++k)   // past the end: clamps like the resolver
            {
                const PutBackPick p = PutBackPickAt(s, 0, n, k);
                const std::vector<int>& want = ref[static_cast<std::size_t>(std::min(k, static_cast<int>(ref.size()) - 1))];
                REQUIRE(p.take == static_cast<int>(want.size()));
                for (int j = 0; j < p.take; ++j) { CHECK(p.idx[j] == want[static_cast<std::size_t>(j)]); }
                ++checked;
            }
            // ...and the resolver itself, pinned at a few ranks, moves exactly the reference's cards.
            {
                for (int k : { 0, static_cast<int>(ref.size()) / 2, static_cast<int>(ref.size()) - 1 })
                {
                    GameState t = s;
                    const std::vector<int>& want = ref[static_cast<std::size_t>(k)];
                    std::vector<int> nums;
                    for (int hi : want) { nums.push_back(t.players[0].hand[static_cast<std::size_t>(hi)].m_number); }
                    { ScriptedPutBack pin(k); ResolvePutBackFromHand(t, 0, n, kBs); }
                    CHECK(g_putback_resolved_w == static_cast<int>(ref.size()));
                    CHECK(g_putback_resolved_i == k);
                    for (std::size_t j = 0; j < nums.size(); ++j) { CHECK(t.players[0].library[j].m_number == nums[j]); }
                    CHECK(t.players[0].hand.size() == s.players[0].hand.size() - want.size());
                }
            }
        }
    }
    CHECK(checked > 2000);
}

// THE TWO BUDGET LEVERS ARE INERT AT AN UNLIMITED BUDGET. MTG_PUTBACK_PRICE (extra units per put-back
// variant applied) and MTG_LA_NESTED_BAIL (a nested solve stops at a tripped overrun ceiling) only
// re-price a FINITE budget: with no budget, or an Unlimited() one, neither is read, so the chosen plan
// and the units spent are identical with the levers on or off. At a finite budget the price IS charged.
TEST_CASE("Brainstorm: the put-back price and the nested overrun bail are inert at an unlimited budget")
{
    const GameState s = BoardBs({ "Island", "Lightning Bolt", "Goblin Guide" },
                                { "Birds of Paradise", "Sol Ring", "Llanowar Elves" });
    auto solve = [&](int price, int bail, SearchBudget* b, long long* charged) -> TurnSolver::Plan
    {
        const heurarm::Arm saved = heurarm::t_arm;
        heurarm::t_arm[heurarm::PUTBACK_PRICE]  = static_cast<std::int8_t>(price);
        heurarm::t_arm[heurarm::LA_NESTED_BAIL] = static_cast<std::int8_t>(bail);
        const long long c0 = TurnSolver::PutBackPriceCharged();
        TurnSolver::Plan p = TurnSolver::SolveWithLookahead(s, true, 2, 8, b, b != nullptr, false, nullptr);
        if (charged != nullptr) { *charged = TurnSolver::PutBackPriceCharged() - c0; }
        heurarm::t_arm = saved;
        return p;
    };
    long long ch = -1;
    const TurnSolver::Plan off = solve(0, 0, nullptr, nullptr);
    CHECK(solve(1, 1, nullptr, &ch) == off);
    CHECK(ch == 0);
    SearchBudget u_on(0), u_off(0);   // 0 = UNLIMITED
    const TurnSolver::Plan p_on = solve(1, 1, &u_on, &ch);
    CHECK(ch == 0);
    const TurnSolver::Plan p_off = solve(0, 0, &u_off, nullptr);
    CHECK(p_on == p_off);
    CHECK(u_on.Used() == u_off.Used());
    // A finite budget: the variants the root applies are billed the price.
    SearchBudget fin(SearchBudget::FromVirtualMs(1000));
    solve(1, 0, &fin, &ch);
    CHECK(ch > 0);
}
