// NivMizzetCombo: Brainstorm's put-back by the USER's bucket rule (MTG_NIV_BRAINSTORM_RULE) and its
// one-option prune (MTG_NIV_BRAINSTORM_SINGLE). USER 2026-10-22: "Brainstorm put-back should use the same
// approach we talked about for ponder. You put the cards you need the least first using the same rules,
// followed by the ones you can't use yet so you can draw them later."
//
// The ranking is most-expendable-first; candidate 0 puts ranking[0] back DEEPEST and ranking[1] on TOP.
// The default is read per CARD (DecisionProvider::PutBackRankingPerCard), so one copy of a duplicated
// class goes back without its twin. Every case below pins the arms through heurarm (never the env).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsNb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefNb(const std::string& name)
{
    EnsureCardsNb();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const NivMizzetComboProvider& NivProvNb()
{
    static const NivMizzetComboProvider p;
    return p;
}

Card CardNb(const std::string& name, int number)
{
    Card c = DefNb(name).card;
    c.m_number = number;
    return c;
}

void PutNb(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = DefNb(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

// Island + Spirebluff Canal untapped (2 mana: {U}, {U}/{R}); `drop_spent` closes this turn's land drop.
GameState BoardNb(const std::vector<std::string>& hand, bool drop_spent,
                  const std::vector<std::string>& library = {})
{
    EnsureCardsNb();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 6601;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = drop_spent ? 1 : 0;
    s.m_provider          = &NivProvNb();
    PutNb(s, "Island", 1);
    PutNb(s, "Spirebluff Canal", 2);
    int h = 21;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardNb(n, h++)); }
    int l = 100;
    for (const std::string& n : library) { s.players[0].library.push_back(CardNb(n, l++)); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(CardNb("Island", l++)); }
    return s;
}

struct ArmNb
{
    explicit ArmNb(std::initializer_list<heurarm::Slot> on)
    { for (heurarm::Slot sl : on) { heurarm::t_arm[static_cast<int>(sl)] = 1; } }
    ~ArmNb() { heurarm::Clear(); }
};

std::string NameAt(const GameState& s, int hi) { return s.players[0].hand[static_cast<std::size_t>(hi)].m_name.str(); }

// Candidate 0 as (top, second) card names, through the resolver's fast path.
std::pair<std::string, std::string> Default2(const GameState& s)
{
    const PutBackPick p = PutBackPickAt(s, 0, 2, 0);
    REQUIRE(p.take == 2);
    return { NameAt(s, p.idx[0]), NameAt(s, p.idx[1]) };
}

}   // namespace

TEST_CASE("Niv Brainstorm rule: inert unless MTG_NIV_CANTRIP_RULES and MTG_NIV_BRAINSTORM_RULE are both on")
{
    const GameState s = BoardNb({ "Niv-Mizzet, the Firemind", "Tandem Lookout", "Tandem Lookout", "Island" }, true);
    const std::vector<int> base = NivProvNb().DecisionProvider::PutBackRanking(s, 0);
    for (int arm = 0; arm < 3; ++arm)
    {
        heurarm::Clear();
        if (arm == 1) { heurarm::t_arm[static_cast<int>(heurarm::NIV_CANTRIP_RULES)] = 1; }
        if (arm == 2) { heurarm::t_arm[static_cast<int>(heurarm::NIV_BRAINSTORM_RULE)] = 1;
                        heurarm::t_arm[static_cast<int>(heurarm::NIV_BRAINSTORM_SINGLE)] = 1; }
        INFO("arm " << arm);
        CHECK(NivProvNb().PutBackRanking(s, 0) == base);
        CHECK_FALSE(NivProvNb().PutBackRankingPerCard(s));
        CHECK_FALSE(NivProvNb().PutBackSingleCandidate(s));
    }
    heurarm::Clear();
}

// A duplicate Lookout is the least needed card and goes back DEEPEST; the Island cannot be dropped this
// turn (the drop is spent) and goes on TOP, drawn next turn. Read per class, the Lookout class ranks at its
// duplicate and candidate 0 would put back BOTH Lookouts; per card it puts back exactly one.
TEST_CASE("Niv Brainstorm rule: the duplicate goes back deepest, a can't-use-yet card on top, one copy only")
{
    ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_BRAINSTORM_RULE });
    const GameState s = BoardNb({ "Niv-Mizzet, the Firemind", "Tandem Lookout", "Tandem Lookout", "Island" }, true);
    const std::vector<int> r = NivProvNb().PutBackRanking(s, 0);
    REQUIRE(r.size() == 4);
    CHECK(NameAt(s, r[0]) == "Tandem Lookout");               // the duplicate (230)
    CHECK(NameAt(s, r[1]) == "Island");                       // wanted (820), usable next turn
    CHECK(NameAt(s, r[2]) == "Tandem Lookout");               // the enabler (880), a mana short
    CHECK(NameAt(s, r[3]) == "Niv-Mizzet, the Firemind");     // the Niv (900)
    CHECK(Default2(s) == std::make_pair(std::string("Island"), std::string("Tandem Lookout")));
    // The resolver itself: Island on top, one Lookout under it, the Niv and a Lookout stay.
    GameState t = s;
    ResolvePutBackFromHand(t, 0, 2, "Brainstorm");
    REQUIRE(t.players[0].library.size() >= 2);
    CHECK(t.players[0].library[0].m_name.str() == "Island");
    CHECK(t.players[0].library[1].m_name.str() == "Tandem Lookout");
    std::vector<std::string> left;
    for (const Card& c : t.players[0].hand) { left.push_back(c.m_name.str()); }
    std::sort(left.begin(), left.end());
    CHECK(left == std::vector<std::string>{ "Niv-Mizzet, the Firemind", "Tandem Lookout" });
}

// MTG_NIV_CANTRIP_AMEND's order-tier change carries over: a cantrip castable with this turn's mana is a
// set-up card usable now and stays in hand; without the amendment the rules file it in tier 3 and it goes
// back (deepest) ahead of the next-turn pieces.
TEST_CASE("Niv Brainstorm rule: a castable cantrip stays under the amendment, goes back without it")
{
    const GameState s = BoardNb({ "Niv-Mizzet, the Firemind", "Preordain", "Tandem Lookout", "Island" }, true);
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_BRAINSTORM_RULE });
        const std::vector<int> r = NivProvNb().PutBackRanking(s, 0);
        REQUIRE(r.size() == 4);
        CHECK(NameAt(s, r[0]) == "Preordain");
        CHECK(NameAt(s, r[1]) == "Island");
        CHECK(Default2(s) == std::make_pair(std::string("Island"), std::string("Preordain")));
    }
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_CANTRIP_AMEND, heurarm::NIV_BRAINSTORM_RULE });
        const std::vector<int> r = NivProvNb().PutBackRanking(s, 0);
        REQUIRE(r.size() == 4);
        CHECK(NameAt(s, r[3]) == "Preordain");                // usable now: kept
        CHECK(Default2(s) == std::make_pair(std::string("Tandem Lookout"), std::string("Island")));
    }
}

// The fast path (PutBackPickAt) and the reference enumeration (PutBackCandidates) agree rank for rank with
// the per-card default live, over random Niv hands -- and rank 0 IS the per-card default.
TEST_CASE("Niv Brainstorm rule: PutBackPickAt == PutBackCandidates rank for rank, rank 0 = the per-card default")
{
    ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_BRAINSTORM_RULE });
    const std::vector<std::string> pool = {
        "Niv-Mizzet, the Firemind", "Niv-Mizzet, Parun", "Tandem Lookout", "Curiosity", "Generator Servant",
        "Gitaxian Probe", "Preordain", "Ponder", "Brainstorm", "Remand", "Island", "Island", "Spirebluff Canal",
        "Sulfur Falls", "Saprazzan Skerry", "Sol Ring", "Izzet Signet" };
    std::uint64_t rng = 0x6e1b5a17c0ffeeull;
    auto next = [&rng](int mod) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return static_cast<int>(rng % static_cast<std::uint64_t>(mod)); };
    int checked = 0, per_card_moved = 0;
    for (int trial = 0; trial < 300; ++trial)
    {
        GameState s = BoardNb({}, next(2) == 0);
        const int hn = next(10);
        for (int i = 0; i < hn; ++i)
        {
            Card c = CardNb(pool[static_cast<std::size_t>(next(static_cast<int>(pool.size())))], 300 + i);
            if (next(12) == 0) { c.m_is_staged = true; }
            s.players[0].hand.push_back(c);
        }
        for (int n = 1; n <= 2; ++n)
        {
            const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, n);
            const PutBackPick first = PutBackPickAt(s, 0, n, 0);
            INFO("trial=" << trial << " n=" << n << " hand=" << hn);
            REQUIRE(first.count == static_cast<int>(ref.size()));
            if (ref.empty()) { continue; }
            for (int k = 0; k < static_cast<int>(ref.size()) + 2; ++k)
            {
                const PutBackPick p = PutBackPickAt(s, 0, n, k);
                const std::vector<int>& want = ref[static_cast<std::size_t>(std::min(k, static_cast<int>(ref.size()) - 1))];
                REQUIRE(p.take == static_cast<int>(want.size()));
                for (int j = 0; j < p.take; ++j)
                {
                    CHECK(PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(p.idx[j])])
                          == PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(want[static_cast<std::size_t>(j)])]));
                }
                ++checked;
            }
            if (ref.size() > 1)
            {
                // Rank 0 = the two most expendable eligible CARDS of the ranking, the less expendable on top.
                const std::vector<int> r = NivProvNb().PutBackRanking(s, 0);
                std::vector<int> elig;
                for (int hi : r) { if (PutBackEligible(s.players[0].hand[static_cast<std::size_t>(hi)])) { elig.push_back(hi); } }
                REQUIRE(static_cast<int>(elig.size()) >= first.take);
                const int top = first.take == 2 ? elig[1] : elig[0];
                CHECK(PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(first.idx[0])])
                      == PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(top)]));
                if (first.take == 2)
                {
                    CHECK(PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(first.idx[1])])
                          == PutBackClassKey(s, s.players[0].hand[static_cast<std::size_t>(elig[0])]));
                }
                heurarm::t_arm[static_cast<int>(heurarm::NIV_BRAINSTORM_RULE)] = 0;
                const PutBackPick plain = PutBackPickAt(s, 0, n, 0);
                heurarm::t_arm[static_cast<int>(heurarm::NIV_BRAINSTORM_RULE)] = 1;
                if (plain.idx[0] != first.idx[0] || plain.idx[1] != first.idx[1]) { ++per_card_moved; }
            }
        }
    }
    CHECK(checked > 1000);
    CHECK(per_card_moved > 0);   // the rule really moves the default on some hands
}

// MTG_NIV_BRAINSTORM_SINGLE prunes the searched put-back fan: no put-back variant is enumerated, so every
// Brainstorm resolves at the rule's pick. With the rule alone the fan is still there.
TEST_CASE("Niv Brainstorm rule: BRAINSTORM_SINGLE prunes the put-back fan to the rule's pick")
{
    auto board = []
    {
        return BoardNb({ "Brainstorm", "Niv-Mizzet, the Firemind", "Tandem Lookout", "Island" }, false,
                       { "Preordain", "Sol Ring", "Curiosity", "Island", "Gitaxian Probe" });
    }();
    auto variants = [&]()
    {
        int v = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(board, true)) { if (p.putback_choice >= 1) { ++v; } }
        return v;
    };
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_BRAINSTORM_RULE });
        CHECK(variants() > 0);
    }
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_BRAINSTORM_RULE, heurarm::NIV_BRAINSTORM_SINGLE });
        CHECK(variants() == 0);
    }
    {
        ArmNb arms({ heurarm::NIV_BRAINSTORM_SINGLE });   // inert without the rules
        CHECK(variants() > 0);
    }
}

// MTG_NIV_PONDER_KEEP_SHUFFLE (USER 2026-10-22: "My ponder idea was allowing for search between shuffle and
// keep, but not the order."): Ponder's searched fan is exactly KEEP in the rules' order and SHUFFLE -- no
// order variants -- and the keep variant resolves in TopDispositionPick's order. Without the flag (rules on)
// the full order axis is still there.
TEST_CASE("Niv Ponder keep-vs-shuffle: the fan is keep-in-the-rule's-order plus shuffle, nothing else")
{
    GameState s = BoardNb({ "Ponder", "Niv-Mizzet, the Firemind" }, true, { "Sol Ring", "Island", "Tandem Lookout" });
    s.battlefield.erase(s.battlefield.begin() + 1);   // Island alone: {U} for Ponder, nothing after it
    auto ponder_plans = [&](std::vector<int>& keeps, int& order_variants)
    {
        keeps.clear();
        order_variants = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (p.ponder_choice >= 1) { ++order_variants; }
            if (!p.land_to_play.empty() || p.actions.size() != 1 || p.actions[0].card_name.str() != "Ponder") { continue; }
            if (p.ponder_choice < 1) { keeps.push_back(p.actions[0].ponder_keep); }
        }
        std::sort(keeps.begin(), keeps.end());
    };
    std::vector<int> keeps;
    int order_variants = 0;
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES });
        ponder_plans(keeps, order_variants);
        CHECK(order_variants > 0);                       // the full order axis
    }
    {
        ArmNb arms({ heurarm::NIV_CANTRIP_RULES, heurarm::NIV_PONDER_KEEP_SHUFFLE });
        ponder_plans(keeps, order_variants);
        CHECK(order_variants == 0);
        CHECK(keeps == std::vector<int>{ -1, 0, 1 });    // the base plan + shuffle + keep
        // The keep variant resolves in the rules' order: the first card is drawn, the other two stay on top.
        const std::vector<Card> looked(s.players[0].library.begin(), s.players[0].library.begin() + 3);
        // ...read on the board Ponder RESOLVES on: the Island tapped for it, Ponder off the hand.
        GameState at = s;
        at.battlefield[0].tapped = true;
        at.players[0].hand.erase(at.players[0].hand.begin());
        const TopDisposition rule = NivProvNb().TopDispositionPick(at, looked, LookKind::Reorder, 1);
        REQUIRE_FALSE(rule.shuffle);
        REQUIRE(rule.top_order.size() == 3);
        const TurnSolver::Plan* keep = nullptr;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.land_to_play.empty() && p.actions.size() == 1 && p.actions[0].card_name.str() == "Ponder"
                && p.actions[0].ponder_keep == 1) { keep = &p; break; }
        }
        REQUIRE(keep != nullptr);
        GameState t = s;
        TurnSolver::ApplyPlan(t, *keep, true);
        bool drew = false;
        for (const Card& c : t.players[0].hand) { if (c.m_number == looked[static_cast<std::size_t>(rule.top_order[0])].m_number) { drew = true; } }
        CHECK(drew);
        REQUIRE(t.players[0].library.size() >= 2);
        CHECK(t.players[0].library[0].m_number == looked[static_cast<std::size_t>(rule.top_order[1])].m_number);
        CHECK(t.players[0].library[1].m_number == looked[static_cast<std::size_t>(rule.top_order[2])].m_number);
    }
    {
        ArmNb arms({ heurarm::NIV_PONDER_KEEP_SHUFFLE });   // inert without the rules
        ponder_plans(keeps, order_variants);
        CHECK(order_variants > 0);
    }
}

// ---- Tandem Lookout's pairing by the USER's rule (MTG_NIV_LOOKOUT_RULE2) ----------------------------------
// "Niv in hand means wait for it, whereas extra draw sources without niv are what give us permission to try
// pairing them with something else." A Lookout (#10) and a Generator Servant (#11) on the battlefield; the
// Servant is the only legal partner unless a Niv is up.
namespace
{
GameState LookoutBoard(const std::vector<std::string>& hand, const std::vector<std::string>& library,
                       bool second_lookout_on_bf = false, bool niv_on_bf = false)
{
    GameState s = BoardNb(hand, true, library);
    if (library.empty())   // "no Niv in the library": BoardNb's filler is Islands only
    { REQUIRE(true); }
    PutNb(s, "Tandem Lookout", 10);
    PutNb(s, "Generator Servant", 11);
    if (second_lookout_on_bf) { PutNb(s, "Tandem Lookout", 12); }
    if (niv_on_bf) { PutNb(s, "Niv-Mizzet, the Firemind", 13); }
    s.deck_has_soulbond  = true;
    s.deck_has_draw_ping = true;
    return s;
}
int BfIndex(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { return i; } }
    return -1;
}
int PairFor(const GameState& s, const std::vector<int>& cand_nums)
{
    std::vector<int> cands;
    for (int n : cand_nums) { cands.push_back(BfIndex(s, n)); }
    return NivProvNb().SoulbondPartner(s, 0, s.battlefield[static_cast<std::size_t>(BfIndex(s, 10))], cands);
}
}   // namespace

TEST_CASE("Niv Lookout rule 2: wait for a Niv in hand, pair with permission, every case")
{
    ArmNb arms({ heurarm::NIV_LOOKOUT_RULE2 });
    {   // a Niv in hand: wait, the twin pruned
        const GameState s = LookoutBoard({ "Niv-Mizzet, the Firemind" }, {});
        CHECK(PairFor(s, { 11 }) == 0);
        CHECK(NivProvNb().SoulbondTwinPruned(s));
    }
    {   // no Niv in hand, a spare Curiosity in hand, a Niv in the library: pair now, searched
        const GameState s = LookoutBoard({ "Curiosity" }, { "Niv-Mizzet, Parun" });
        CHECK(PairFor(s, { 11 }) == 11);
        CHECK_FALSE(NivProvNb().SoulbondTwinPruned(s));
    }
    {   // ...the spare is a second Lookout on the battlefield
        const GameState s = LookoutBoard({}, { "Niv-Mizzet, the Firemind" }, /*second_lookout_on_bf=*/true);
        CHECK(PairFor(s, { 11 }) == 11);
        CHECK_FALSE(NivProvNb().SoulbondTwinPruned(s));
    }
    {   // no Niv in hand, no spare source, a Niv in the library: wait, pruned
        const GameState s = LookoutBoard({ "Island" }, { "Niv-Mizzet, the Firemind" });
        CHECK(PairFor(s, { 11 }) == 0);
        CHECK(NivProvNb().SoulbondTwinPruned(s));
    }
    {   // no Niv in hand or library: pair as before, searched
        const GameState s = LookoutBoard({ "Island" }, {});
        CHECK(PairFor(s, { 11 }) == 11);
        CHECK_FALSE(NivProvNb().SoulbondTwinPruned(s));
    }
    {   // a Niv among the legal partners is taken, whatever is in hand
        const GameState s = LookoutBoard({ "Niv-Mizzet, Parun" }, {}, false, /*niv_on_bf=*/true);
        CHECK(PairFor(s, { 11, 13 }) == 13);
    }
}

TEST_CASE("Niv Lookout rule 2: off -> the base pairing; the twin is enumerated unless the rule prunes it")
{
    const GameState s = LookoutBoard({ "Generator Servant", "Niv-Mizzet, the Firemind" }, {});
    // The twin generator's shape (b): a free Lookout on the battlefield and a plan casting a creature.
    auto twins = [&]()
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { if (p.soulbond_decline) { ++n; } }
        return n;
    };
    heurarm::Clear();
    CHECK(PairFor(s, { 11 }) == NivProvNb().DecisionProvider::SoulbondPartner(
              s, 0, s.battlefield[static_cast<std::size_t>(BfIndex(s, 10))], { BfIndex(s, 11) }));
    CHECK_FALSE(NivProvNb().SoulbondTwinPruned(s));
    const int base_twins = twins();
    CHECK(base_twins > 0);
    {
        ArmNb arms({ heurarm::NIV_LOOKOUT_RULE2 });   // a Niv in hand: the twin is pruned
        CHECK(twins() == 0);
    }
}
