// Unit cover for LifegainHeliodCombo's USER-reviewed rules (2026-10-28,
// docs/design/lifegainheliodcombo-order-and-discard-proposal.md): the cast order (MTG_LHC_ORDER), Martyr of
// Sands' pinned reveal (MTG_LHC_MARTYR_PIN / _SERRA / _LATE), Walking Ballista's X (MTG_LHC_BALLISTA_X) and its
// activations waiting for lethal (MTG_LHC_BALLISTA_WAIT), and the cleanup discard (MTG_LHC_BUCKET_DISCARD).
//
// USER: "Abiding Grace low is correct in Goldfish"; "A second Ballista is better than a second Heliod, but not
// particularly strong ... I would rank it lower for sure"; "Heliod on its own is pretty solid and I would keep it
// sometimes, though it may be discardable if there are better payoffs"; "Martyr's activation timing should be as
// early as possible ... not searched"; "It really depends on whether you are enabling Serra Ascendant or just
// gaining life for the triggers"; "Ballista's X should be maximum or less to ensure the combo can go off";
// "The activations may as well wait until they would make lethal or we are comboing."
//
// Every expected value below was derived BY HAND from those rulings and the proposal's ladder text, not read
// off the code's output. The apply-level Martyr placement (right after its own cast, through the site-9
// continuation) is pinned by test/scenarios/lhc_martyr_after_own_cast.json.
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

void EnsureCardsLhc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLhc(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const char* kWarden   = "Soul Warden";
const char* kPride    = "Ajani's Pridemate";
const char* kVoice    = "Voice of the Blessed";
const char* kHeliod   = "Heliod, Sun-Crowned";
const char* kSerra    = "Serra Ascendant";
const char* kRanger   = "Ranger-Captain of Eos";
const char* kAjani    = "Ajani, Strength of the Pride";
const char* kMartyr   = "Martyr of Sands";
const char* kGrace    = "Abiding Grace";
const char* kBallista = "Walking Ballista";

const LifegainHeliodComboProvider& ProvLhc()
{
    static const LifegainHeliodComboProvider p;
    return p;
}

// One lever for this thread, restored on scope exit (-1 = the env default).
struct LhcArm
{
    heurarm::Slot slot;
    LhcArm(heurarm::Slot s, int v) : slot(s) { heurarm::t_arm[s] = static_cast<std::int8_t>(v); }
    ~LhcArm() { heurarm::t_arm[slot] = -1; }
};

int g_lhc_num = 100;

Permanent& PutLhc(GameState& s, const std::string& name, int controller = 0)
{
    Permanent p;
    p.card             = DefLhc(name).card;
    p.card.m_number    = g_lhc_num++;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return s.battlefield.back();
}

// Main 1 of turn 4: `plains` untapped Plains, the land drop made, `board` on the battlefield (not sick), `hand`
// in hand, a Plains library.
GameState BoardLhc(int plains, const std::vector<std::string>& board, const std::vector<std::string>& hand,
                   int life = 20)
{
    EnsureCardsLhc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].life = life;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvLhc();
    for (int k = 0; k < plains; ++k) { PutLhc(s, "Plains"); }
    for (const std::string& n : board) { PutLhc(s, n); }
    for (const std::string& n : hand)
    {
        Card c = DefLhc(n).card;
        c.m_number = g_lhc_num++;
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefLhc("Plains").card;
        c.m_number = g_lhc_num++;
        s.players[0].library.push_back(c);
    }
    return s;
}

Action CastLhc(const GameState& s, const std::string& name)
{
    const CardDefinition& d = DefLhc(name);
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &d;
    a.cost      = EffectiveSpellCost(d, s, 1);
    a.card_mv   = d.card.m_mana_cost.ManaValue();
    return a;
}

// A Walking Ballista cast at X: {X}{X} folded into the generic, as the enumeration carries it.
Action BallistaLhc(const GameState& s, int x)
{
    Action a = CastLhc(s, kBallista);
    a.chosen_x     = x;
    a.cost.generic = 2 * x;
    a.cost.has_x   = true;
    return a;
}

// The order an apply site realises: the shared comparator, the deck's arrangement, the payable fallback.
std::vector<std::string> RealisedLhc(const GameState& s, const std::vector<std::string>& plan_order)
{
    std::vector<Action> acts;
    for (const std::string& n : plan_order) { acts.push_back(CastLhc(s, n)); }
    std::vector<int> ord(acts.size());
    for (std::size_t i = 0; i < ord.size(); ++i) { ord[i] = static_cast<int>(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    ApplyPayableCastOrder(s, acts, ord);
    std::vector<std::string> v;
    for (int i : ord) { v.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return v;
}

// The discard ranking, value alone (MTG_DISCARD_DIST_MODEL=0: an empty-library test board makes the distance
// model's factor a second axis these hand derivations do not need).
std::vector<int> ShedLhc(const GameState& s, int arm = 1)
{
    LhcArm a(heurarm::LHC_BUCKET_DISCARD, arm);
    LhcArm d(heurarm::DISCARD_DIST_MODEL, 0);
    return ProvLhc().CleanupDiscardCandidates(s, nullptr);
}

GameState HandLhc(const std::vector<std::string>& hand, const std::vector<std::string>& board, int life = 20)
{
    EnsureCardsLhc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = life;
    for (const std::string& n : hand)
    {
        Card c;
        c.m_name = n;
        c.RehashName();
        c.m_number = g_lhc_num++;
        REQUIRE(CardDatabase::Instance().Lookup(n) != nullptr);
        s.players[0].hand.push_back(c);
    }
    for (const std::string& n : board) { PutLhc(s, n); }
    return s;
}

}   // namespace

TEST_CASE("LHC order: Abiding Grace first; Ballista right after Heliod, never before it; Ranger-Captain first body")
{
    const GameState s = BoardLhc(30, {}, {});
    {
        LhcArm on(heurarm::LHC_ORDER, 1);
        // Grace's trigger is at the end step: in main 1 it is devotion only, so it leads.
        CHECK(RealisedLhc(s, { kWarden, kPride, kGrace }) == std::vector<std::string>{ kGrace, kWarden, kPride });
        // Heliod is not a creature here (Warden is its only devotion), so CritterLifegain puts it after the
        // creatures; the Ballista then moves right after it.
        CHECK(RealisedLhc(s, { kBallista, kHeliod, kWarden })
              == std::vector<std::string>{ kWarden, kHeliod, kBallista });
        // The bodies: Ranger-Captain (its tutor) ahead of Walking Ballista.
        CHECK(RealisedLhc(s, { kBallista, kRanger }) == std::vector<std::string>{ kRanger, kBallista });
    }
    {
        LhcArm off(heurarm::LHC_ORDER, 0);   // the inherited CritterLifegain order: Heliod after the creatures
        const std::vector<std::string> o = RealisedLhc(s, { kBallista, kHeliod, kWarden });
        CHECK(o.back() == kHeliod);
    }
}

TEST_CASE("LHC order: Martyr of Sands first only when it enables Serra Ascendant")
{
    LhcArm on(heurarm::LHC_ORDER, 1);
    // The hoist mechanics under the USER's first ruling; the adopted default (MTG_LHC_MARTYR_CAST_LATE) keeps the CAST
    // at its rank and moves only the activation -- see the "cast late" case below.
    LhcArm hoist(heurarm::LHC_MARTYR_CAST_LATE, 0);
    // Serra out, life 20 < 30: Martyr leads (the biggest reveal).
    const GameState serra = BoardLhc(30, { kSerra }, {}, 20);
    CHECK(RealisedLhc(serra, { kWarden, kPride, kMartyr }).front() == kMartyr);
    // Serra cast in the same list counts too.
    const GameState plain = BoardLhc(30, {}, {}, 20);
    CHECK(RealisedLhc(plain, { kWarden, kSerra, kMartyr }).front() == kMartyr);
    // No Serra: late -- after the watcher and the payoff, so its entry and its gain feed them.
    {
        const std::vector<std::string> o = RealisedLhc(plain, { kMartyr, kPride, kWarden });
        CHECK(o.front() == kWarden);
        CHECK(o.back() == kMartyr);
    }
    // Serra already online (life 30): late again.
    const GameState high = BoardLhc(30, { kSerra }, {}, 30);
    CHECK(RealisedLhc(high, { kMartyr, kPride, kWarden }).back() == kMartyr);
    {
        LhcArm always(heurarm::LHC_MARTYR_SERRA, 0);   // the first ruling: always early
        CHECK(RealisedLhc(plain, { kWarden, kPride, kMartyr }).front() == kMartyr);
    }
    {
        LhcArm late(heurarm::LHC_MARTYR_LATE, 1);      // A/B arm: always late
        CHECK(RealisedLhc(serra, { kWarden, kPride, kMartyr }).back() == kMartyr);
    }
}

TEST_CASE("LHC Martyr pin: before every cast when it enables Serra, else after the last gain watcher")
{
    const GameState serra = BoardLhc(4, { kSerra, kMartyr }, {}, 20);
    const GameState plain = BoardLhc(4, { kMartyr }, {}, 20);
    {
        LhcArm on(heurarm::LHC_ORDER, 1);
        CHECK(ProvLhc().RevealOutletPinned());
        CHECK(ProvLhc().RevealOutletPin(serra, {}) == 0);
        CHECK(ProvLhc().RevealOutletPin(plain, {}) == 1);
        CHECK(ProvLhc().RevealOutletPin(plain, { CastLhc(plain, kSerra) }) == 0);   // a Serra this plan casts
        {
            LhcArm off(heurarm::LHC_MARTYR_PIN, 0);
            CHECK_FALSE(ProvLhc().RevealOutletPinned());   // the searched position fan
        }
    }
    {
        LhcArm off(heurarm::LHC_ORDER, 0);   // the late position counts on the deck's order
        CHECK_FALSE(ProvLhc().RevealOutletPinned());
    }
}

TEST_CASE("LHC Martyr cast late (A/B arm): the cast stays behind the watchers, the activation keeps the Serra pin")
{
    LhcArm on(heurarm::LHC_ORDER, 1);
    LhcArm cast_late(heurarm::LHC_MARTYR_CAST_LATE, 1);
    // Serra out, life 20 < 30 (std s975001 gi1638): the watcher still sees Martyr's entry...
    const GameState serra = BoardLhc(30, { kSerra }, {}, 20);
    {
        const std::vector<std::string> o = RealisedLhc(serra, { kMartyr, kPride, kWarden });
        CHECK(o.front() == kWarden);
        CHECK(o.back() == kMartyr);
    }
    // ...while a Martyr already out is still activated before every cast for Serra.
    const GameState serra_out = BoardLhc(4, { kSerra, kMartyr }, {}, 20);
    CHECK(ProvLhc().RevealOutletPin(serra_out, {}) == 0);
}

TEST_CASE("LHC Martyr after a pinger: a Walking Ballista cast is a reveal position boundary (USER 2026-10-29)")
{
    const GameState s = BoardLhc(6, { kHeliod }, {}, 20);
    const Action ballista = CastLhc(s, kBallista);
    const Action pride = CastLhc(s, kPride);
    CHECK(TurnSolver::RevealSlotCounts(pride));             // a "whenever you gain life" watcher always counts
    // 2HG s971001 gi231: Ballista X=1, THEN Martyr -- Heliod's counter bumps the Ballista and the loop goes live.
    CHECK(TurnSolver::RevealSlotCounts(ballista));
    {
        LhcArm off(heurarm::LHC_MARTYR_AFTER_PINGER, 0);
        CHECK_FALSE(TurnSolver::RevealSlotCounts(ballista));
    }
}

TEST_CASE("LHC ping last: Walking Ballista's ping and the Heliod loop run after every other activation")
{
    const GameState s = BoardLhc(6, { kHeliod, kBallista, kMartyr }, {}, 20);
    const CardDefinition& ball = DefLhc(kBallista);
    const CardDefinition& martyr = DefLhc(kMartyr);
    Action ping;  ping.kind = Action::Kind::ActivatePermAbility;  ping.ability_mode = Action::AbilityMode::CounterPing;
    Action loop = ping;                                          loop.ability_mode = Action::AbilityMode::PingLoop;
    Action sac;   sac.kind = Action::Kind::SacCreatureOutlet;
    CHECK(ProvLhc().ActivationOrderRankForAction(s, ball, ping) > ProvLhc().ActivationOrderRankForAction(s, martyr, sac));
    CHECK(ProvLhc().ActivationOrderRankForAction(s, ball, loop) > ProvLhc().ActivationOrderRankForAction(s, martyr, sac));
    {
        LhcArm off(heurarm::LHC_PING_LAST, 0);
        CHECK(ProvLhc().ActivationOrderRankForAction(s, ball, ping) == ProvLhc().ActivationOrderRankForAction(s, martyr, sac));
    }
}

TEST_CASE("LHC Heliod's counter goes on the Ballista whenever one is out")
{
    GameState s = BoardLhc(0, { kHeliod, kVoice, kBallista }, {}, 20);   // no mana: the loop-live bump cannot apply
    int ball = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_name.str() == kBallista) { ball = i; } }
    REQUIRE(ball >= 0);
    // A 0/0 entrant is dead to the toughness SBA before the trigger targets (lhc_ballista_x0_warden_dies): never it.
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) != ball);
    s.battlefield[static_cast<std::size_t>(ball)].counters.Add(Counter::Type::PlusOnePlusOne, 1);
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == ball);
    {
        LhcArm off(heurarm::LHC_HELIOD_ON_BALLISTA, 0);
        CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) != ball);
    }
}

TEST_CASE("LHC Heliod's counter: no summoning sickness first, then lifelink, then power (USER 2026-10-29)")
{
    // No Ballista: Voice of the Blessed (2/2), Serra Ascendant (1/1 lifelink at 20 life), Soul Warden (1/1).
    GameState s = BoardLhc(0, { kHeliod, kVoice, kSerra, kWarden }, {}, 20);
    auto at = [&](const char* n)
    {
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[static_cast<std::size_t>(i)].card.m_name.str() == n) { return i; } }
        return -1;
    };
    const int heliod = at(kHeliod), voice = at(kVoice), serra = at(kSerra), warden = at(kWarden);
    REQUIRE((heliod >= 0 && voice >= 0 && serra >= 0 && warden >= 0));
    // Devotion 5 (Heliod 1, Voice 2, Serra 1, Warden 1): Heliod is a 5/5 creature too.
    // All unsick: the lifelinker beats the bigger Heliod and Voice.
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == serra);
    // A TAPPED Serra (it attacked) is not summoning-sick: still Serra.
    s.battlefield[static_cast<std::size_t>(serra)].tapped = true;
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == serra);
    s.battlefield[static_cast<std::size_t>(serra)].tapped = false;
    // A summoning-sick Serra loses to the unsick bodies; among those, power decides (Heliod 5 > Voice 2 > Warden 1).
    s.battlefield[static_cast<std::size_t>(serra)].entered_this_turn = true;
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == heliod);
    s.battlefield[static_cast<std::size_t>(heliod)].entered_this_turn = true;
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == voice);
    // Everything sick: lifelink decides again.
    s.battlefield[static_cast<std::size_t>(voice)].entered_this_turn = true;
    s.battlefield[static_cast<std::size_t>(warden)].entered_this_turn = true;
    CHECK(ProvLhc().LifegainCounterTarget(s, s.active_player_index) == serra);
    // Off our turn the same rule applies.
    s.active_player_index = 1;
    CHECK(ProvLhc().LifegainCounterTarget(s, 0) == serra);
    {
        LhcArm off(heurarm::LHC_HELIOD_UNSICK_LIFELINK, 0);
        CHECK(ProvLhc().LifegainCounterTarget(s, 0) == -1);   // the generic pick
    }
}

TEST_CASE("LHC Ballista X: the maximum the plan leaves, or exactly the Heliod loop's {1}{W} reserve")
{
    LhcArm on(heurarm::LHC_BALLISTA_X, 1);
    auto wasted = [](const GameState& s, const std::vector<Action>& cands, int pool)
    {
        std::vector<int> sel;
        for (int i = 0; i < static_cast<int>(cands.size()); ++i) { sel.push_back(i); }
        return ProvLhc().XCastBelowMax(s, cands, sel, 0, pool);
    };
    // No Heliod: 6 mana -> X = 3 takes it all; X = 2 leaves a whole X step.
    const GameState bare = BoardLhc(6, {}, {});
    CHECK_FALSE(wasted(bare, { BallistaLhc(bare, 3) }, 6));
    CHECK(wasted(bare, { BallistaLhc(bare, 2) }, 6));
    CHECK(wasted(bare, { BallistaLhc(bare, 0) }, 6));
    CHECK_FALSE(wasted(bare, { BallistaLhc(bare, 2) }, 5));   // 1 left: no X step fits
    // The plan's other casts are its "need more mana in the turn": X = 1 next to Pridemate (2) spends 4 of 4.
    CHECK_FALSE(wasted(bare, { BallistaLhc(bare, 1), CastLhc(bare, kPride) }, 4));
    // Heliod out: X = 2 + the {1}{W} grant is the 6-mana loop -- the 2 left are its reserve.
    const GameState heliod = BoardLhc(6, { kHeliod }, {});
    CHECK_FALSE(wasted(heliod, { BallistaLhc(heliod, 2) }, 6));
    CHECK(wasted(heliod, { BallistaLhc(heliod, 1) }, 6));      // X = 1 leaves 4: the grant AND a whole X step
    // X = 1 goes live with any later gain event (a Warden's entry, Ajani's +1 -- confirm A/B seed 967001 gi1347):
    // 2 + the 2 of the grant is the 4-mana loop, with or without an enter watcher.
    const GameState warden = BoardLhc(4, { kHeliod, kWarden }, {});
    CHECK_FALSE(wasted(warden, { BallistaLhc(warden, 1) }, 4));
    CHECK(wasted(warden, { BallistaLhc(warden, 0) }, 4));
    const GameState ajani = BoardLhc(5, { kHeliod, kAjani }, {});
    CHECK_FALSE(wasted(ajani, { BallistaLhc(ajani, 1) }, 5));    // 3 left: the grant + 1, less than an X step
    // A Heliod the plan casts counts like one on the battlefield: X = 2 (4) + Heliod (3) + grant (2) = 9.
    CHECK_FALSE(wasted(bare, { BallistaLhc(bare, 2), CastLhc(bare, kHeliod) }, 9));
    {
        LhcArm off(heurarm::LHC_BALLISTA_X, 0);
        CHECK_FALSE(wasted(bare, { BallistaLhc(bare, 0) }, 6));
    }
}

TEST_CASE("LHC Ballista waits: pings and the {4} counter only when they can make lethal")
{
    LhcArm on(heurarm::LHC_BALLISTA_WAIT, 1);
    GameState s = BoardLhc(8, {}, {});
    Permanent& b = PutLhc(s, kBallista);
    b.counters.Add(Counter::Type::PlusOnePlusOne, 2);
    const Permanent ballista = b;
    // Face pings: the smallest lethal count, else none.
    s.players[1].life = 20;
    CHECK(ProvLhc().CounterPingCounts(s, ballista, 0, 2).empty());
    s.players[1].life = 2;
    CHECK(ProvLhc().CounterPingCounts(s, ballista, 0, 2) == std::vector<int>{ 2 });
    s.players[1].life = 1;
    CHECK(ProvLhc().CounterPingCounts(s, ballista, 0, 2) == std::vector<int>{ 1 });
    // A creature target never (the ruling: activations wait for lethal or the combo).
    CHECK(ProvLhc().CounterPingCounts(s, ballista, 1, 2).empty());
    // A LIFELINKED ping whose gain a payoff reads is the combo (Heliod's grant): the generic range.
    {
        GameState l = s;
        PutLhc(l, kPride);
        l.players[1].life = 20;
        Permanent lb = ballista;
        lb.temp_lifelink = true;
        CHECK(ProvLhc().CounterPingCounts(l, lb, 0, 2) == std::vector<int>{ 1, 2 });
    }
    // The {4} counter: the smallest K with the attack (twice its counters: swing + ping after combat) lethal -- under
    // MTG_LHC_BALLISTA_SINK_WAIT=1 (the USER's first ruling; the adopted default lets the counter go free).
    LhcArm sink_wait(heurarm::LHC_BALLISTA_SINK_WAIT, 1);
    s.players[1].life = 8;   // not sick, 2 counters: 2 * (2 + K) >= 8 -> K = 2
    CHECK(ProvLhc().ManaSinkActivationCounts(s, ballista, PermAbilityMode::PayCounterSelf, 2) == std::vector<int>{ 2 });
    s.players[1].life = 20;  // 2 * (2 + 2) = 8 < 20
    CHECK(ProvLhc().ManaSinkActivationCounts(s, ballista, PermAbilityMode::PayCounterSelf, 2).empty());
    {
        LhcArm off(heurarm::LHC_BALLISTA_WAIT, 0);
        CHECK(ProvLhc().ManaSinkActivationCounts(s, ballista, PermAbilityMode::PayCounterSelf, 2).size() >= 1);
    }
    {
        LhcArm sink(heurarm::LHC_BALLISTA_SINK_WAIT, 0);   // the {4} counter free, pings still wait
        CHECK(ProvLhc().ManaSinkActivationCounts(s, ballista, PermAbilityMode::PayCounterSelf, 2).size() >= 1);
        CHECK(ProvLhc().CounterPingCounts(s, ballista, 0, 2).empty());
    }
}

TEST_CASE("LHC discard D1: the combo pair is protected; the 4th land is kept with the combo in reach")
{
    // Board 2 Plains + Soul Warden; hand 0 Heliod, 1 Ballista, 2 Pridemate, 3 Voice, 4 Serra, 5-7 Plains.
    // Ladder: land (board 2) and watcher (board Warden) full -> COMBO: Heliod (a Ballista is ours; it is also
    // payoff #1) and Ballista (a Heliod is ours) -> payoff #2 = Pridemate (ties Voice, first in hand) -> land
    // #3 = Plains 5. Fill: Voice (0) then Serra (3, life 20). The combo is in reach, so Plains 6 is the 4th
    // land. Shed: the surplus Plains 7, then the ladder backwards: 6, Serra, Voice, 5, Pridemate, Ballista,
    // Heliod.
    const GameState s = HandLhc({ kHeliod, kBallista, kPride, kVoice, kSerra, "Plains", "Plains", "Plains" },
                                { "Plains", "Plains", kWarden });
    CHECK(ShedLhc(s) == std::vector<int>{ 7, 6, 4, 3, 5, 2, 1, 0 });
}

TEST_CASE("LHC discard D2: Heliod without a partner is a fill card -- discardable for better payoffs")
{
    // As D1 with Martyr for the Ballista: no Ballista or Ranger-Captain is ours, so Heliod is NOT protected.
    // Ladder: payoff #1 Pridemate, payoff #2 Voice, land #3 Plains 5. Fill: Heliod (1), Martyr (2: a Serra is
    // ours and life is under 30), Serra (3). No 4+ drop and no combo in reach -> no 4th land. Shed: Plains 7,
    // Plains 6, then backwards: Serra, Martyr, Heliod, Plains 5, Voice, Pridemate.
    const GameState s = HandLhc({ kHeliod, kMartyr, kPride, kVoice, kSerra, "Plains", "Plains", "Plains" },
                                { "Plains", "Plains", kWarden });
    CHECK(ShedLhc(s) == std::vector<int>{ 7, 6, 4, 1, 0, 5, 3, 2 });
}

TEST_CASE("LHC discard D3: a second Ballista sheds first, below Abiding Grace and Ajani")
{
    // Board 3 Plains + Soul Warden + Heliod. Hand 0 Ballista, 1 Ballista, 2 Grace, 3 Ajani, 4 Plains,
    // 5 Pridemate, 6 Voice, 7 Martyr. COMBO: the first Ballista (Heliod is ours). Payoffs: Pridemate, Voice.
    // Fill: first copies before repeats, 4+ drops last among them: Martyr (4; no Serra), Grace (7), Ajani
    // (4 mana), then the second Ballista. Ajani is kept -> Plains 4 is the 4th land, and the room left (3)
    // takes Martyr, Grace, Ajani. Shed: the second Ballista, then backwards: Plains, Ajani, Grace, Martyr,
    // Voice, Pridemate, Ballista.
    const GameState s = HandLhc({ kBallista, kBallista, kGrace, kAjani, "Plains", kPride, kVoice, kMartyr },
                                { "Plains", "Plains", "Plains", kWarden, kHeliod });
    CHECK(ShedLhc(s) == std::vector<int>{ 1, 4, 3, 2, 7, 6, 5, 0 });
}

TEST_CASE("LHC discard: MTG_LHC_BUCKET_DISCARD=0 restores the inherited CritterLifegain rule exactly")
{
    const GameState s = HandLhc({ kBallista, kBallista, kGrace, kAjani, "Plains", kPride, kVoice, kMartyr },
                                { "Plains", "Plains", "Plains", kWarden, kHeliod });
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = 1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = 0;
    const std::vector<int> critter = CritterLifegainProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = -1;
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = 1;
    CHECK(ShedLhc(s, 0) == critter);
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = -1;
}
