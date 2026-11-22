// SCRY: NO WIDTH CAP, A SOUND NAME FOLD, THE ORDER PRUNE HOOK, AND THE BURN SCRY HEURISTIC (USER 2026-11-22).
//
// "I don't like widths period because they provide either too many or too few options. My preference is to fully
// open it up and prune with a heuristic as needed." -- MTG_SCRY_WIDTH is gone; every disposition of a look is a
// candidate (TopDispositionCandidates), and the provider prunes them at resolution.
// "Ideally we would make it a relatively smart heuristic that keeps things based on impact and castability ... Also,
// we should certainly consider pruning orderings of the top." -- burnscry (DecisionProviders.cpp): Creature Burn's and
// Midrange Burn's Magma Jet scry by the hand's land goal (lands in HAND count), cheap burn kept, a crowded slot
// bottomed; two firm verdicts; and the order prune (DecisionProvider::ScryKeptOrder).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <vector>

namespace
{

const CardDefinition& DefBs(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
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

// Pins the three burn-scry levers for one scope (-1 = the env default) and restores them on exit.
struct BsArm
{
    heurarm::Arm saved;
    BsArm(int pick, int firm, int order) : saved(heurarm::t_arm)
    {
        heurarm::t_arm[heurarm::BURN_SCRY_PICK]  = static_cast<std::int8_t>(pick);
        heurarm::t_arm[heurarm::BURN_SCRY_FIRM]  = static_cast<std::int8_t>(firm);
        heurarm::t_arm[heurarm::BURN_SCRY_ORDER] = static_cast<std::int8_t>(order);
    }
    ~BsArm() { heurarm::t_arm = saved; }
};

const CreatureBurnProvider& Cb() { static const CreatureBurnProvider p; return p; }
const MidrangeBurnProvider& Mb() { static const MidrangeBurnProvider p; return p; }

// `mountains` Mountains in play (the land drop used), `hand` in hand, provider `prov`.
GameState Board(int mountains, const std::vector<std::string>& hand, const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < mountains; ++i)
    {
        Permanent p;
        p.card = CardBs("Mountain", 6100 + i);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    int n = 6200;
    for (const std::string& h : hand) { s.players[0].hand.push_back(CardBs(h, n++)); }
    s.m_provider = prov;
    return s;
}

std::vector<Card> Look(const std::vector<std::string>& names)
{
    std::vector<Card> out;
    int n = 6300;
    for (const std::string& nm : names) { out.push_back(CardBs(nm, n++)); }
    return out;
}

std::vector<std::vector<int>> Tops(const std::vector<TopDisposition>& c)
{
    std::vector<std::vector<int>> out;
    for (const TopDisposition& d : c) { out.push_back(d.top_order); }
    return out;
}

}   // namespace

TEST_CASE("scry fan: every disposition of a distinct-card look is a candidate (no width cap)")
{
    const GameState s = Board(2, {}, nullptr);   // the generic provider
    CHECK(TopDispositionCandidates(s, Look({"Lightning Bolt"}), LookKind::Scry).size() == 2);
    CHECK(TopDispositionCandidates(s, Look({"Lightning Bolt", "Browbeat"}), LookKind::Scry).size() == 5);
    CHECK(TopDispositionCandidates(s, Look({"Lightning Bolt", "Browbeat", "Magma Jet"}), LookKind::Scry).size() == 16);
    CHECK(TopDispositionCandidates(s, Look({"Lightning Bolt", "Browbeat"}), LookKind::Surveil).size() == 5);
}

TEST_CASE("scry fan: same-name looked cards fold (a sound identity: same library, same graveyard)")
{
    const GameState s = Board(2, {}, nullptr);
    // Two Mountains: {} / {one} / {both}. Candidate 0 is the generic pick (2 lands in play -> both bottomed).
    const std::vector<TopDisposition> mm = TopDispositionCandidates(s, Look({"Mountain", "Mountain"}), LookKind::Scry);
    CHECK(Tops(mm) == std::vector<std::vector<int>>{ {}, {0}, {0, 1} });
    // Mountain, Mountain, Bolt: the distinct (top sequence | away sequence) name patterns.
    const std::vector<Card> mmb = Look({"Mountain", "Mountain", "Lightning Bolt"});
    const std::vector<TopDisposition> c = TopDispositionCandidates(s, mmb, LookKind::Scry);
    std::set<std::vector<std::string>> patterns;
    for (const TopOption& o : EnumerateTopDispositions(LookKind::Scry, mmb))
    {
        std::vector<std::string> key;
        std::vector<char> top(3, 0);
        for (int i : o.disp.top_order) { key.push_back(mmb[static_cast<std::size_t>(i)].m_name.str()); top[static_cast<std::size_t>(i)] = 1; }
        key.push_back("|");
        for (int i = 0; i < 3; ++i) { if (!top[static_cast<std::size_t>(i)]) { key.push_back(mmb[static_cast<std::size_t>(i)].m_name.str()); } }
        patterns.insert(key);
    }
    CHECK(c.size() == patterns.size());
    CHECK(c.size() < 16);
    // A reorder is not folded (the Ponder axes index that list).
    CHECK(TopDispositionCandidates(s, Look({"Mountain", "Mountain"}), LookKind::Reorder).size() == 3);
}

TEST_CASE("burn scry: lands in HAND count toward the land goal (Creature Burn s6 gi5)")
{
    BsArm arm(1, 1, 1);
    // Two Mountains in play and one in hand, Ball Lightning + Bolt in hand: the goal is 4 lands (Ball + Bolt in one
    // turn), so ONE looked Mountain stays on top. The generic rule bottoms both (two lands in play).
    const GameState s = Board(2, {"Ball Lightning", "Lightning Bolt", "Mountain"}, &Cb());
    const std::vector<Card> look = Look({"Mountain", "Mountain"});
    CHECK(Cb().TopDispositionPick(s, look, LookKind::Scry, -1).top_order == std::vector<int>{ 0 });
    GameState g = s;
    g.m_provider = nullptr;
    CHECK(TopDispositionCandidates(g, look, LookKind::Scry).front().top_order.empty());
    // With the land goal already met (four lands), both go to the bottom.
    const GameState full = Board(3, {"Ball Lightning", "Lightning Bolt", "Mountain"}, &Cb());
    CHECK(Cb().TopDispositionPick(full, look, LookKind::Scry, -1).top_order.empty());
}

TEST_CASE("burn scry: a crowded slot is bottomed even when castable; cheap burn is kept (Creature Burn s8 gi7)")
{
    BsArm arm(1, 1, 1);
    // Three lands, Keldon Champion in hand: Browbeat (3) beside the Champion (4) is about two turns of spells on three
    // lands, so it goes; Goblin Fireslinger (1) fits anywhere and stays.
    const GameState s = Board(3, {"Keldon Champion"}, &Cb());
    CHECK(Cb().TopDispositionPick(s, Look({"Browbeat", "Goblin Fireslinger"}), LookKind::Scry, -1).top_order
          == std::vector<int>{ 1 });
    // Two 3-drops in hand on three lands: a third (Ball Lightning) is bottomed, the Bolt kept.
    const GameState t = Board(3, {"Browbeat", "Vulshok Sorcerer"}, &Cb());
    CHECK(Cb().TopDispositionPick(t, Look({"Ball Lightning", "Lightning Bolt"}), LookKind::Scry, -1).top_order
          == std::vector<int>{ 1 });
    // An empty hand on three lands keeps both.
    const GameState e = Board(3, {}, &Cb());
    CHECK(Cb().TopDispositionPick(e, Look({"Ball Lightning", "Lightning Bolt"}), LookKind::Scry, -1).top_order.size() == 2);
    // A card two land draws away (Keldon Champion on two lands, nothing in hand) is bottomed.
    const GameState two = Board(2, {}, &Cb());
    CHECK(Cb().TopDispositionPick(two, Look({"Keldon Champion"}), LookKind::Scry, -1).top_order.empty());
    // Price of Progress against an opponent with no nonbasic land is dead.
    CHECK(Cb().TopDispositionPick(e, Look({"Price of Progress"}), LookKind::Scry, -1).top_order.empty());
}

TEST_CASE("burn scry: the firm verdicts")
{
    BsArm arm(1, 1, 1);
    const GameState s = Board(3, {}, &Cb());
    CHECK(Cb().ScryVerdict(s, CardBs("Lightning Bolt", 1)) == 1);
    CHECK(Cb().ScryVerdict(s, CardBs("Chain Lightning", 1)) == 1);
    CHECK(Cb().ScryVerdict(s, CardBs("Rift Bolt", 1)) == -1);
    CHECK(Cb().ScryVerdict(s, CardBs("Goblin Fireslinger", 1)) == -1);
    CHECK(Cb().ScryVerdict(s, CardBs("Mountain", 1)) == -1);       // three lands, goal 3: not two over
    const GameState flood = Board(5, {}, &Cb());
    CHECK(Cb().ScryVerdict(flood, CardBs("Mountain", 1)) == 0);    // five lands, goal 3
    const GameState fig = Board(5, {"Figure of Destiny"}, &Cb());
    CHECK(Cb().ScryVerdict(fig, CardBs("Mountain", 1)) == -1);     // Figure sinks the mana
    BsArm off(1, 0, 1);
    CHECK(Cb().ScryVerdict(s, CardBs("Lightning Bolt", 1)) == -1);
}

TEST_CASE("burn scry: the order prune offers one order per kept set, the rule's")
{
    // Two lands in play, nothing in hand (goal 3): a kept Mountain is the next card needed, so it goes on top.
    const GameState s = Board(2, {}, &Cb());
    const std::vector<Card> look = Look({"Lightning Bolt", "Mountain"});
    {
        BsArm arm(1, 1, 1);
        std::vector<int> kept{ 0, 1 };
        CHECK(Cb().ScryKeptOrder(s, look, kept));
        CHECK(kept == std::vector<int>{ 1, 0 });
        // Candidates: the pick (Mountain then Bolt), and keep-the-Bolt-only. Bottoming the Bolt is firm-pruned, and
        // Bolt-then-Mountain is the order prune.
        CHECK(Tops(TopDispositionCandidates(s, look, LookKind::Scry)) == std::vector<std::vector<int>>{ {1, 0}, {0} });
    }
    {
        BsArm arm(1, 0, 1);   // no verdicts: every SET, one order each
        CHECK(Tops(TopDispositionCandidates(s, look, LookKind::Scry))
              == std::vector<std::vector<int>>{ {1, 0}, {}, {0}, {1} });
    }
    {
        BsArm arm(1, 0, 0);   // nothing pruned: the full fan, the pick in look order
        CHECK(Tops(TopDispositionCandidates(s, look, LookKind::Scry))
              == std::vector<std::vector<int>>{ {0, 1}, {}, {0}, {1}, {1, 0} });
    }
    {
        BsArm arm(0, 0, 0);   // the generic pick (keep the nonland; a land only below two in play)
        CHECK(Tops(TopDispositionCandidates(s, look, LookKind::Scry)).front() == std::vector<int>{ 0 });
    }
    // With the goal met, a kept land goes last.
    BsArm arm(1, 1, 1);
    const GameState met = Board(3, {}, &Cb());
    std::vector<int> kept{ 0, 1 };
    CHECK(Cb().ScryKeptOrder(met, Look({"Mountain", "Goblin Fireslinger"}), kept));
    CHECK(kept == std::vector<int>{ 1, 0 });
}

TEST_CASE("burn scry: Midrange Burn shares the rule")
{
    BsArm arm(1, 1, 1);
    const GameState c = Board(3, {"Chandra, Torch of Defiance"}, &Cb());
    GameState m = c;
    m.m_provider = &Mb();
    const std::vector<Card> look = Look({"Skewer the Critics", "Mountain"});
    CHECK(Mb().TopDispositionPick(m, look, LookKind::Scry, -1).top_order
          == Cb().TopDispositionPick(c, look, LookKind::Scry, -1).top_order);
    CHECK(Mb().ScryVerdict(m, CardBs("Lightning Bolt", 1)) == 1);
    // Chandra (4) on three lands: the goal is 4, the Mountain stays and goes first; Skewer (spectacle 1) stays.
    CHECK(Mb().TopDispositionPick(m, look, LookKind::Scry, -1).top_order == std::vector<int>{ 1, 0 });
}
