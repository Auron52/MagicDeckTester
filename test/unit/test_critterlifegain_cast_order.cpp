// Unit cover for the USER-settled CritterLifegain cast order (MTG_CRITTER_ORDER, heurarm CRITTER_ORDER;
// docs/design/critterlifegain-cast-order-2026-10-14.md).
//
// USER 2026-10-14: "For CritterLifegain Ajani, Strength of the Pride should go after Ajani's pridemate
// because it produces them. The activation that produces them should go right after, whereas the +1 can go
// where you put it originally. Heliod is a trickier one ... It can go right where it is if it is a creature.
// Otherwise it probably should go after other creatures until it is one. ... If it isn't a creature after
// the next 2 have entered then it might actually be better to play it before. ... we could search or
// evaluate this case. Otherwise it seems fine." And, cross-deck: "Let's fully order the cards in each list
// inasmuch as possible. I don't like the randomness of choosing the order."
//
// What is pinned:
//   1. the order is TOTAL and is the USER's list; permuting the hand (the plan's action order) never changes
//      the realised order; lever off = the 2026-09-08 mined ranks (8 / 9 / Generic), nothing else moves;
//   2. Archangel of Thune before Ajani's Pridemate and before a plain body -- the draft's probe 1 at apply
//      level (14 counters, not 8);
//   3. Heliod's three cases: a creature at its slot stays; otherwise it walks past up to 2 later creature
//      entries; otherwise the canonical order puts it after the creatures and the searched alternative puts
//      it FIRST (CastOrderingCandidates offers exactly those two, canonical first, and nothing for any other
//      plan);
//   4. the Ajani split: the -2 cast (found by its ability's params) right after Pridemate, the +1 / plain
//      cast after the bodies;
//   5. the flip: 2+ more watchers than payoffs and no Thune -> the payoff ahead of the new watcher (the
//      draft's probe 3 at apply level), off with Thune out and off at watchers = payoffs + 1;
//   6. every cast pinned to main 1; the searched two-way opt-in stands down under the SearchOrder gate.
#include <doctest/doctest.h>

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

const char* kWarden = "Soul Warden";
const char* kAttend = "Soul's Attendant";
const char* kChamp  = "Auriok Champion";
const char* kDaxos  = "Daxos, Blessed by the Sun";
const char* kThune  = "Archangel of Thune";
const char* kVoice  = "Voice of the Blessed";
const char* kPride  = "Ajani's Pridemate";
const char* kAjani  = "Ajani, Strength of the Pride";
const char* kHeliod = "Heliod, Sun-Crowned";
const char* kOcelot = "Ocelot Pride";
const char* kSerra  = "Serra Ascendant";
const char* kRanger = "Ranger-Captain of Eos";
const char* kUA     = "Unexpectedly Absent";

const CritterLifegainProvider& ProvCo()
{
    static const CritterLifegainProvider p;
    return p;
}

// The lever for this thread, restored on scope exit.
struct CoArm
{
    explicit CoArm(bool on) { heurarm::t_arm[heurarm::CRITTER_ORDER] = on ? 1 : 0; }
    ~CoArm() { heurarm::t_arm[heurarm::CRITTER_ORDER] = -1; }
};

int g_num = 100;

void PutCo(GameState& s, const std::string& name)
{
    Permanent p;
    p.card             = DefCo(name).card;
    p.card.m_number    = g_num++;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
}

// Main 1 of turn 4: `plains` untapped Plains, the land drop made, `board` on the battlefield (not sick),
// `hand` in hand, a Plains library.
GameState BoardCo(int plains, const std::vector<std::string>& board, const std::vector<std::string>& hand)
{
    EnsureCardsCo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvCo();
    for (int k = 0; k < plains; ++k) { PutCo(s, "Plains"); }
    for (const std::string& n : board) { PutCo(s, n); }
    for (const std::string& n : hand)
    {
        Card c = DefCo(n).card;
        c.m_number = g_num++;
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefCo("Plains").card;
        c.m_number = g_num++;
        s.players[0].library.push_back(c);
    }
    return s;
}

int AbilityIndex(const std::string& effect)
{
    const CardDefinition& d = DefCo(kAjani);
    for (int i = 0; i < static_cast<int>(d.params.loyalty_abilities.size()); ++i)
    { if (d.params.loyalty_abilities[static_cast<std::size_t>(i)].effect == effect) { return i; } }
    return -1;
}

Action CastCo(const GameState& s, const std::string& name, int loyalty = -1)
{
    const CardDefinition& d = DefCo(name);
    Action a;
    a.kind            = Action::Kind::CastFromHand;
    a.card_name       = name;
    a.def             = &d;
    a.cost            = EffectiveSpellCost(d, s, 1);
    a.card_mv         = d.card.m_mana_cost.ManaValue();
    a.loyalty_ability = loyalty;
    return a;
}

std::vector<std::string> NamesCo(const std::vector<Action>& acts, const std::vector<int>& ord)
{
    std::vector<std::string> v;
    for (int i : ord)
    {
        std::string n = acts[static_cast<std::size_t>(i)].card_name.str();
        if (acts[static_cast<std::size_t>(i)].loyalty_ability >= 0)
        { n += "#" + std::to_string(acts[static_cast<std::size_t>(i)].loyalty_ability); }
        v.push_back(n);
    }
    return v;
}

// The order an apply site realises: the shared comparator, the deck's arrangement, the payable fallback.
std::vector<int> RealisedCo(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> ord(acts.size());
    for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    ApplyPayableCastOrder(s, acts, ord);
    return ord;
}

std::vector<std::string> RealisedNamesCo(const GameState& s, const std::vector<Action>& acts)
{
    return NamesCo(acts, RealisedCo(s, acts));
}

int PlusCo(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

int TotalCountersCo(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0) { n += PlusCo(p); } }
    return n;
}

const Permanent* FindCo(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

// Apply a plan of these casts (in this plan order) through the rollout apply -- the executor's lockstep twin
// through the same shared order functions.
GameState ApplyCo(GameState s, const std::vector<std::string>& plan_order)
{
    TurnSolver::Plan plan;
    for (const std::string& n : plan_order) { plan.actions.push_back(CastCo(s, n)); }
    plan.land_decided = true;
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    return s;
}

}   // namespace

TEST_CASE("CritterLifegain order: TOTAL and the USER's list; a permuted hand realises the same order")
{
    // Thirty Plains: the whole list pays in any order, so the payable fallback never has a say.
    const GameState s = BoardCo(30, {}, {});
    const int minus2 = AbilityIndex("pridemate_token");
    const int plus1  = AbilityIndex("lifegain_creatures_plus_walkers");
    REQUIRE(minus2 >= 0);
    REQUIRE(plus1 >= 0);
    CoArm arm(true);
    // Every distinct name in the shipped list (Ajani for its -2), in the USER's order. Board devotion 0 and
    // every cast before Heliod is white, so Heliod enters as a creature at its slot.
    const std::vector<std::string> user = { kWarden, kAttend, kChamp, kDaxos, kThune, kVoice, kPride,
                                            std::string(kAjani) + "#" + std::to_string(minus2), kHeliod,
                                            kOcelot, kSerra, kRanger, kUA };
    std::vector<Action> acts;
    for (const char* n : { kUA, kRanger, kSerra, kOcelot, kHeliod, kPride, kVoice, kThune, kDaxos, kChamp,
                           kAttend, kWarden })
    { acts.push_back(CastCo(s, n)); }
    acts.push_back(CastCo(s, kAjani, minus2));
    CHECK(RealisedNamesCo(s, acts) == user);
    // Permuting the plan's action order never changes the realised order (12 rotations x reversal).
    for (int rot = 0; rot < static_cast<int>(acts.size()); ++rot)
    {
        for (const bool rev : { false, true })
        {
            std::vector<Action> p = acts;
            std::rotate(p.begin(), p.begin() + rot, p.end());
            if (rev) { std::reverse(p.begin(), p.end()); }
            CAPTURE(rot); CAPTURE(rev);
            CHECK(RealisedNamesCo(s, p) == user);
        }
    }
    // The comparator alone is total over different names: no two of them compare equal.
    for (std::size_t i = 0; i < acts.size(); ++i)
    {
        for (std::size_t j = 0; j < acts.size(); ++j)
        {
            if (acts[i].card_name == acts[j].card_name) { continue; }
            CHECK(CastOrderLess(s, acts[i], acts[j]) != CastOrderLess(s, acts[j], acts[i]));
        }
    }
    // Ajani for +1 (or with no activation) goes after the bodies, before Unexpectedly Absent.
    for (const int la : { plus1, -1 })
    {
        std::vector<Action> b = { CastCo(s, kUA), CastCo(s, kAjani, la), CastCo(s, kRanger), CastCo(s, kPride),
                                  CastCo(s, kOcelot) };
        const std::string aj = la >= 0 ? std::string(kAjani) + "#" + std::to_string(la) : std::string(kAjani);
        CHECK(RealisedNamesCo(s, b) == std::vector<std::string>{ kPride, kOcelot, kRanger, aj, kUA });
    }
}

TEST_CASE("CritterLifegain order: lever off is the 2026-09-08 mined order, nothing else")
{
    const GameState s = BoardCo(12, { kWarden, kAttend }, {});
    CoArm arm(false);
    const GenericProvider generic;
    CHECK(ProvCo().CastOrderRank(s, DefCo(kWarden)) == 8);
    CHECK(ProvCo().CastOrderRank(s, DefCo(kDaxos)) == 8);
    CHECK(ProvCo().CastOrderRank(s, DefCo(kPride)) == 9);
    CHECK(ProvCo().CastOrderRank(s, DefCo(kVoice)) == 9);
    for (const char* n : { kThune, kHeliod, kOcelot, kSerra, kRanger, kAjani, kUA })
    {
        CAPTURE(n);
        CHECK(ProvCo().CastOrderRank(s, DefCo(n)) == generic.CastOrderRank(s, DefCo(n)));
        CHECK(ProvCo().CastOrderRankLatest(s, DefCo(n)) == generic.CastOrderRank(s, DefCo(n)));
        CHECK_FALSE(ProvCo().MainPhaseOverride(s, DefCo(n)).has_value());
    }
    CHECK(ProvCo().CastOrderTierName(8) == nullptr);
    CHECK_FALSE(ProvCo().CastOrderTiesByName());
    CHECK_FALSE(ProvCo().WantsCastOrderingSearch());
    // No arrangement: the order comes back exactly as given, ties in plan order.
    std::vector<Action> acts = { CastCo(s, kSerra), CastCo(s, kHeliod), CastCo(s, kOcelot), CastCo(s, kThune) };
    std::vector<int> ord = { 0, 1, 2, 3 };
    ProvCo().ArrangeCastOrder(s, acts, ord);
    CHECK(ord == std::vector<int>{ 0, 1, 2, 3 });
    std::vector<int> st = { 0, 1, 2, 3 };
    std::stable_sort(st.begin(), st.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    CHECK(st == std::vector<int>{ 0, 1, 2, 3 });   // rank 10 ties keep plan order (the draft's "nobody chose")
    // The Ajani -2 cast keeps its card's rank.
    CHECK(ProvCo().CastOrderActionRank(s, CastCo(s, kAjani, AbilityIndex("pridemate_token")), 20) == 20);
}

TEST_CASE("CritterLifegain order: Archangel of Thune before Pridemate and the bodies (draft probe 1, 14 counters not 8)")
{
    {
        CoArm arm(true);
        const GameState s = BoardCo(6, { kWarden, kAttend }, { kOcelot, kThune });
        CHECK(RealisedNamesCo(s, { CastCo(s, kOcelot), CastCo(s, kThune) })
              == std::vector<std::string>{ kThune, kOcelot });
        CHECK(RealisedNamesCo(s, { CastCo(s, kPride), CastCo(s, kThune) })
              == std::vector<std::string>{ kThune, kPride });
        // Thune enters: 2 events x 3 bodies = 6; Ocelot enters: 2 events x 4 bodies = 8 -> 14.
        const GameState t = ApplyCo(s, { kOcelot, kThune });
        CHECK(TotalCountersCo(t) == 14);
    }
    {
        CoArm arm(false);   // the mined order: a rank-10 tie, plan order decides -- Ocelot first, 8 counters
        const GameState s = BoardCo(6, { kWarden, kAttend }, { kOcelot, kThune });
        const GameState t = ApplyCo(s, { kOcelot, kThune });
        CHECK(TotalCountersCo(t) == 8);
    }
}

TEST_CASE("CritterLifegain order: Heliod -- a creature at its slot stays; else it walks past up to 2 creatures")
{
    CoArm arm(true);
    {
        // Devotion 4 on board (Warden 1, Pridemate 1, Champion 2): +1 own = 5 at the static slot.
        const GameState s = BoardCo(4, { kWarden, kPride, kChamp }, { kOcelot, kHeliod });
        CHECK(RealisedNamesCo(s, { CastCo(s, kOcelot), CastCo(s, kHeliod) })
              == std::vector<std::string>{ kHeliod, kOcelot });
    }
    {
        // Devotion 3 (Warden, Attendant, Pridemate): 4 at the slot, 5 after Ocelot -> walks past one.
        const GameState s = BoardCo(4, { kWarden, kAttend, kPride }, { kHeliod, kOcelot });
        CHECK(RealisedNamesCo(s, { CastCo(s, kHeliod), CastCo(s, kOcelot) })
              == std::vector<std::string>{ kOcelot, kHeliod });
        // ...and the walk is what the draft measured: Heliod enters as a creature and both watchers fire on
        // it -- Pridemate +6 (Ocelot's 2 events, Heliod's 2 events, Heliod's 2 counters), where Heliod first
        // (the mined order with this hand order) gives +4.
        const GameState t = ApplyCo(s, { kHeliod, kOcelot });
        const Permanent* h = FindCo(t, kHeliod);
        REQUIRE(h != nullptr);
        CHECK(h->card.IsCreature());
        CHECK(PlusCo(*FindCo(t, kPride)) == 6);
        CoArm off(false);
        const GameState u = ApplyCo(BoardCo(4, { kWarden, kAttend, kPride }, { kHeliod, kOcelot }), { kHeliod, kOcelot });
        CHECK(PlusCo(*FindCo(u, kPride)) == 4);
    }
    {
        // Devotion 2 (Warden, Attendant): 3 at the slot, 4 after Ocelot, 5 after Serra -> walks past two.
        const GameState s = BoardCo(5, { kWarden, kAttend }, { kHeliod, kOcelot, kSerra });
        CHECK(RealisedNamesCo(s, { CastCo(s, kSerra), CastCo(s, kHeliod), CastCo(s, kOcelot) })
              == std::vector<std::string>{ kOcelot, kSerra, kHeliod });
        // Not a two-way plan: the ordering search leaves it alone.
        std::vector<std::vector<int>> out;
        CHECK(ProvCo().CastOrderingCandidates(s, {}, { CastCo(s, kHeliod), CastCo(s, kOcelot), CastCo(s, kSerra) }, out));
        CHECK(out.empty());
    }
}

TEST_CASE("CritterLifegain order: Heliod's two-way case -- canonical after the creatures, searched FIRST")
{
    CoArm arm(true);
    {
        // Devotion 1 (Warden): 2 at the slot, 3 after Ocelot, 4 after Serra -- still not a creature after two.
        // Canonical: after the last creature (Ranger, whose {W}{W} makes it 6 -- a creature there); FIRST:
        // before everything.
        const GameState s = BoardCo(8, { kWarden }, { kHeliod, kOcelot, kSerra, kRanger });
        const std::vector<Action> acts = { CastCo(s, kRanger), CastCo(s, kHeliod), CastCo(s, kSerra), CastCo(s, kOcelot) };
        CHECK(RealisedNamesCo(s, acts) == std::vector<std::string>{ kOcelot, kSerra, kRanger, kHeliod });
        std::vector<std::vector<int>> out;
        CHECK(ProvCo().CastOrderingCandidates(s, acts, acts, out));
        REQUIRE(out.size() == 2);
        CHECK(NamesCo(acts, out[0]) == std::vector<std::string>{ kOcelot, kSerra, kRanger, kHeliod });
        CHECK(NamesCo(acts, out[1]) == std::vector<std::string>{ kHeliod, kOcelot, kSerra, kRanger });
    }
    {
        // An empty board: a watcher and a body. Canonical Warden, Ocelot, Heliod; FIRST Heliod, Warden, Ocelot
        // (a non-creature Heliod on the battlefield for Ocelot's gain event).
        const GameState s = BoardCo(5, {}, { kWarden, kHeliod, kOcelot });
        const std::vector<Action> acts = { CastCo(s, kOcelot), CastCo(s, kHeliod), CastCo(s, kWarden) };
        CHECK(RealisedNamesCo(s, acts) == std::vector<std::string>{ kWarden, kOcelot, kHeliod });
        std::vector<std::vector<int>> out;
        CHECK(ProvCo().CastOrderingCandidates(s, acts, acts, out));
        REQUIRE(out.size() == 2);
        CHECK(NamesCo(acts, out[0]) == std::vector<std::string>{ kWarden, kOcelot, kHeliod });
        CHECK(NamesCo(acts, out[1]) == std::vector<std::string>{ kHeliod, kWarden, kOcelot });
    }
    {
        // Heliod with nothing after it that could move it: one position, nothing to search.
        const GameState s = BoardCo(8, {}, { kHeliod, kAjani });
        const std::vector<Action> acts = { CastCo(s, kAjani, AbilityIndex("lifegain_creatures_plus_walkers")),
                                           CastCo(s, kHeliod) };
        std::vector<std::vector<int>> out;
        CHECK(ProvCo().CastOrderingCandidates(s, acts, acts, out));
        CHECK(out.empty());
    }
    // The SearchOrder gate (human play / MTG_UNPRUNED) keeps the full permutation fan: the opt-in stands down
    // only there, so autonomously it is on.
    CHECK(ProvCo().WantsCastOrderingSearch() == !DecisionUnpruned(UnprunedGate::SearchOrder));
}

TEST_CASE("CritterLifegain order: Ajani -2 right after Pridemate (found by the ability's params), +1 after the bodies")
{
    CoArm arm(true);
    const int minus2 = AbilityIndex("pridemate_token");
    const int plus1  = AbilityIndex("lifegain_creatures_plus_walkers");
    // Two watchers, one payoff out: no flip. Devotion 4 (Warden 1, Pridemate 1, Champion 2).
    const GameState s = BoardCo(14, { kWarden, kPride, kChamp }, {});
    // Devotion 4 + Pridemate 1 + Ajani 2 + own 1: Heliod is a creature at its slot, right after Ajani -2.
    std::vector<Action> acts = { CastCo(s, kOcelot), CastCo(s, kHeliod), CastCo(s, kAjani, minus2), CastCo(s, kPride) };
    const std::string aj2 = std::string(kAjani) + "#" + std::to_string(minus2);
    CHECK(RealisedNamesCo(s, acts) == std::vector<std::string>{ kPride, aj2, kHeliod, kOcelot });
    acts[2] = CastCo(s, kAjani, plus1);
    const std::string aj1 = std::string(kAjani) + "#" + std::to_string(plus1);
    CHECK(RealisedNamesCo(s, acts) == std::vector<std::string>{ kPride, kHeliod, kOcelot, aj1 });
    // The card-level rank is the late slot; only the -2 CAST moves.
    CHECK(ProvCo().CastOrderActionRank(s, CastCo(s, kAjani, minus2), ProvCo().CastOrderRank(s, DefCo(kAjani)))
          < ProvCo().CastOrderRank(s, DefCo(kHeliod)));
    CHECK(ProvCo().CastOrderActionRank(s, CastCo(s, kAjani, minus2), ProvCo().CastOrderRank(s, DefCo(kAjani)))
          > ProvCo().CastOrderRank(s, DefCo(kPride)));
    CHECK(ProvCo().CastOrderActionRank(s, CastCo(s, kAjani, plus1), ProvCo().CastOrderRank(s, DefCo(kAjani)))
          == ProvCo().CastOrderRank(s, DefCo(kAjani)));
    CHECK(ProvCo().CastOrderRank(s, DefCo(kAjani)) > ProvCo().CastOrderRank(s, DefCo(kRanger)));
}

TEST_CASE("CritterLifegain order: the flip -- 2+ more watchers than payoffs and no Thune puts the payoff first")
{
    CoArm arm(true);
    {
        // Draft probe 3: Warden + Attendant out, hand Pridemate + Warden. Pridemate first: 6/6, 4 life.
        const GameState s = BoardCo(3, { kWarden, kAttend }, { kPride, kWarden });
        CHECK(RealisedNamesCo(s, { CastCo(s, kWarden), CastCo(s, kPride) })
              == std::vector<std::string>{ kPride, kWarden });
        const GameState t = ApplyCo(s, { kWarden, kPride });
        const Permanent* pm = FindCo(t, kPride);
        REQUIRE(pm != nullptr);
        CHECK(PlusCo(*pm) == 4);
        CHECK(t.players[0].life == s.players[0].life + 4);
        // The whole payoff block moves, in its own order: Thune, Voice, Pridemate, Ajani -2, Heliod.
        const int minus2 = AbilityIndex("pridemate_token");
        const GameState s2 = BoardCo(14, { kWarden, kAttend }, {});
        CHECK(RealisedNamesCo(s2, { CastCo(s2, kChamp), CastCo(s2, kAjani, minus2), CastCo(s2, kPride),
                                     CastCo(s2, kThune), CastCo(s2, kVoice), CastCo(s2, kOcelot) })
              == std::vector<std::string>{ kThune, kVoice, kPride, std::string(kAjani) + "#" + std::to_string(minus2),
                                           kChamp, kOcelot });
    }
    {
        // Watchers = payoffs + 1 (Warden, Attendant, Pridemate out): no flip -- watcher first.
        const GameState s = BoardCo(3, { kWarden, kAttend, kPride }, { kPride, kWarden });
        CHECK(RealisedNamesCo(s, { CastCo(s, kPride), CastCo(s, kWarden) })
              == std::vector<std::string>{ kWarden, kPride });
    }
    {
        // Thune out: every event is worth a team -- no flip.
        const GameState s = BoardCo(3, { kWarden, kAttend, kChamp, kThune }, { kPride, kWarden });
        CHECK(RealisedNamesCo(s, { CastCo(s, kPride), CastCo(s, kWarden) })
              == std::vector<std::string>{ kWarden, kPride });
    }
    {
        // A non-creature Heliod on the board counts as a payoff: Warden + Attendant + Heliod -> no flip.
        const GameState s = BoardCo(3, { kWarden, kAttend, kHeliod }, { kPride, kWarden });
        CHECK(RealisedNamesCo(s, { CastCo(s, kPride), CastCo(s, kWarden) })
              == std::vector<std::string>{ kWarden, kPride });
    }
}

TEST_CASE("CritterLifegain order: every cast is a main-1 cast; every rank has a report label")
{
    CoArm arm(true);
    const GameState s = BoardCo(0, {}, {});
    for (const char* n : { kWarden, kAttend, kChamp, kDaxos, kThune, kVoice, kPride, kAjani, kHeliod, kOcelot,
                           kSerra, kRanger, kUA })
    {
        CAPTURE(n);
        const auto mp = ProvCo().MainPhaseOverride(s, DefCo(n));
        REQUIRE(mp.has_value());
        CHECK(*mp == DecisionProvider::MainPhase::Main1);
        CHECK(ProvCo().CastOrderTierName(ProvCo().CastOrderRank(s, DefCo(n))) != nullptr);
        CHECK(TurnSolver::ClassifyCastMainPhase(s, DefCo(n)) == 0);
    }
    CHECK(ProvCo().CastOrderTierName(ProvCo().CastOrderActionRank(s, CastCo(s, kAjani, AbilityIndex("pridemate_token")),
                                                                  ProvCo().CastOrderRank(s, DefCo(kAjani)))) != nullptr);
    // Heliod's range runs to the end of the creature block; every other card's range is a point.
    CHECK(ProvCo().CastOrderRankLatest(s, DefCo(kHeliod)) > ProvCo().CastOrderRank(s, DefCo(kRanger)));
    CHECK(ProvCo().CastOrderRankLatest(s, DefCo(kHeliod)) < ProvCo().CastOrderRank(s, DefCo(kAjani)));
    CHECK(ProvCo().CastOrderRankLatest(s, DefCo(kOcelot)) == ProvCo().CastOrderRank(s, DefCo(kOcelot)));
}
