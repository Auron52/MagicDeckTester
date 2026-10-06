// Unified single-attempt mana payment -- see ManaPayment.h for why this exists and what the
// two parameters (available / honor_legacy_cco) encode. The body is the merged text of the
// former AIEngine::TapForCostOnce and TurnSolver TapForCostDirectOnce twins (303 of ~380 lines
// were already identical); comments were merged from both.
#include "ManaPayment.h"
#include "DecisionProviders.h"   // IdealOrderSuppressScope (the range's cost-efficient end)
#include "EngineFlags.h"
#include "PayLedger.h"            // MTG_PAY_ROLLBACK_AUDIT: the rollback's count-only instrument
#include "PayRollback.h"          // MTG_PAY_ROLLBACK: the rollback itself (ledger on the state)
#include "../cards/CardDatabase.h"
#include "../core/SpellEffects.h"
#include "../core/GameLogger.h"   // g_real_resolution (TEMP MTG_TAPDBG diagnostic)

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>   // std::strchr -- the pre-tap colour alphabet lookup
#include <deque>     // PaySnapScratch's stable-address pool
#include <functional>
#include <vector>

// ---- NEEDS-BASED TAP ORDER (MTG_NEEDS_TAP_ORDER; NeedsTapOrderOn in EngineFlags.h) -------------
//
// USER 2026-10-05: *"It should be needs based ... improve our heuristics to reflect need (first hand
// and then deck for cases where we are drawing cards, since you want to retain the most potentially
// useful colour for the deck we are playing)"*, and on colourless: *"Regarding using colourless for
// generic costs that entirely depends on what our deck needs. EDF is a good example of a case that
// doesn't work nicely with this rule ... The handling should be generalized, but choose different
// types of mana based on the deck and hand."*
//
// The scarcity ladder (ManaSourceRank: {C}-only 5, mono 10, dual 20, tri 30, rainbow 50) is a PRIOR
// -- "a flexible source is more likely to be wanted later" -- that knows nothing about what this
// hand, this board or this library actually wants. This computes the posterior once per payment:
//
//   D[c]  DEMAND still ahead of this payment THIS TURN, per colour (W,U,B,R,G,C):
//           the hand's cast costs and our battlefield's repeatable activation pips (Displacer's
//           {2}{C}, Sheets' {1}{S}) that are CASTABLE with what the board has left after this
//           payment -- every land untaps at the next untap step, so a colour held for a spell this
//           turn cannot reach is a colour held for nothing (the uncapped hand was measured: it
//           held Blood Crypt for an eight-mana spell on five sources, fivecolour d0 gi414) --
//           the spell being paid excluded ("hand first");
//         + on a turn whose plan attacks, the repeatable combat pumps as a SINK (firebreathing
//           {R}, a team pump {1}{R}): pips x (remainder / MV), since ApplyFirebreathing spends the
//           whole leftover pool;
//         + when a REVEAL is live this turn (a dig/draw in the plan, or an untapped tap-draw /
//           sac-draw permanent of ours), ONE reveal's expected pips: the library's castable nonland
//           cards' pips over the library size ("then deck"), the same castability cap. This reads
//           the library as a MULTISET only -- the decklist minus the zones you can see, which a
//           human knows -- never its order.
//         NOT priced: a depletion land's counter (the ladder's +1 nudge and dep tiebreak keep it,
//           inside equal harm) -- charging it half a shortfall pushed a dual ahead of the two-mana
//           depletion land and stranded the dual's other colour (th d0 gi50/218/687).
//   S[c]  SUPPLY: untapped own sources that produce c (lands, dorks that can tap now, rocks),
//           decremented as this payment's taps land so later pips see what is really left.
//
// A candidate's HARM is the unmet demand its tap would create, summed over the colours it produces:
// per colour, shortfall(S - 1) - shortfall(S) with shortfall(x) = max(0, D - x). Fixed point x64 so
// the library's fractional pips compare against integer supply. The payment's comparator then
// orders the plain-land tiers by (harm, ladder rank) instead of (ladder rank): a {C}-only land with
// a live {C} sink (D[C] >= S[C]) is held while a Forest nobody needs pays the generic pip, which is
// the autonomous-play generalisation of the human-only MTG_C_SOURCE_HOLD / MTG_HOLD_C_FOR_SINK /
// MTG_HUMAN_TAP_DEMAND rules; a rainbow land nothing in hand or deck needs this turn pays before a
// mono land whose colour is exactly covered. The reserve tiers (60+), the creature band (64+) and
// the pins (negative ranks) keep their absolute order -- harm only breaks ties INSIDE them -- so
// "no creature ever ranks ahead of any land" (six refuted variants) still holds.
//
// Lockstep: computed inside the shared payment, so executor and rollout agree. Cost: one hand +
// battlefield scan per payment (not per pip), plus one library scan only when a reveal is live.
static constexpr int kNdScale = 64;
static void ComputeNeedsDemandSupply(const GameState& state, int active, const ManaCost& cost,
                                     int* D, int* S)
{
    for (int i = 0; i < 6; ++i) { D[i] = 0; S[i] = 0; }
    const PlanTraits* pt = CurrentPlanTraits();
    bool reveal = (pt != nullptr && pt->mid_turn_casts);
    int  total  = 0;   // untapped mana: the castability cap for the hand, the board and the library
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { continue; }
        if ((d->params.tap_draw_cost.has_value() || d->params.sac_draw_cost.has_value())
            && (!d->card.IsCreature() || CanTapNow(p, state.battlefield))) { reveal = true; }
        const bool dork = d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)
                       && GraveyardFuelLive(state, active, *d);
        if (!dork && !p.card.IsLand() && !d->params.mana_rock) { continue; }
        total += ManaProducedPerTap(*d);
        // A CONVERSION source (Cascade Bluffs, Capital City) is not supply of its colours: it
        // only re-colours a feeder's mana, so counting it would credit the board with colours it
        // cannot make on its own, and the harm of spending it would hold it while the real duals
        // pay -- exactly the Fluctuator s10 T3 defect the rank-6 tier was built against ("taps
        // both black lands"). Its FREE {C} mode is real supply; its outputs are not.
        if (IsManaConversionSource(d->params) || d->params.any_color_filter)
        {
            // A RAMP filter (Ferrous Lake "{1}, {T}: Add {U}{R}") has no free mode at all -- it
            // produces nothing without a feeder -- so it is not {C} supply either (the th d0
            // instrument read S[C] = 2 with one Reliquary Tower on the board).
            if (!d->params.filter_no_free_colorless && !d->params.ramp_filter)
            { S[static_cast<int>(Color::Colorless)] += kNdScale; }
            continue;
        }
        int seen = 0;
        for (Color c : EffectiveProducesFor(state, active, *d, &p))
        {
            const int ci = static_cast<int>(c);
            if (ci > 5 || (seen & (1 << ci))) { continue; }
            seen |= (1 << ci);
            S[ci] += kNdScale;
        }
    }
    // DEMAND IS WHAT COULD STILL BE CAST THIS TURN. Every land untaps at the next untap step,
    // so holding a colour for a spell this turn cannot reach buys nothing and costs the ladder's
    // flexibility order. Round 2 measured the uncapped hand (ComputeRefloatDemand: every card's
    // pips): fivecolour d0 gi414 held Blood Crypt for an eight-mana {4}{U}{B}{B}{R} on a
    // five-source board and spent Jetmir's Garden (T5 -> T6); selesnya d0 gi623 held Selesnya
    // Sanctuary's only W for a {4}{W}{W} it was two short of; fluctuator d0 gi618 the same with
    // Glittering Massif. The cap is the mana left after this cost -- the library half below
    // already used it. The spell being paid is excluded by cost identity when it is in hand; an
    // activation (no hand match) subtracts its own pips instead.
    const int castable = std::max(0, total - cost.ManaValue());
    int need[6] = { 0, 0, 0, 0, 0, 0 };
    bool skipped_self = false;
    const auto same_cost = [&](const ManaCost& m) {
        return m.generic == cost.generic && m.white == cost.white && m.blue == cost.blue
            && m.black == cost.black && m.red == cost.red && m.green == cost.green
            && m.colorless == cost.colorless;
    };
    for (const Card& hc : state.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
        if (hd == nullptr || hd->card.IsLand()) { continue; }
        const ManaCost& m = hd->card.m_mana_cost;
        if (!skipped_self && same_cost(m)) { skipped_self = true; continue; }
        if (m.ManaValue() > castable) { continue; }
        AddCostToRefloatDemand(need, m);
    }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { continue; }
        const CardParams& q = d->params;
        const std::optional<ManaCost>* costs[] = {
            &q.drain_cost, &q.exile_opponent_top_cost, &q.blink_cost, &q.tap_damage_cost,
            &q.tap_investigate_cost, &q.tap_draw_cost, &q.sac_draw_cost,
        };
        for (const std::optional<ManaCost>* c : costs)
        {
            if (!c->has_value()) { continue; }
            const ManaCost m = EffectiveActivationCost(state, active, p.card, c->value());
            if (!skipped_self && same_cost(m)) { skipped_self = true; continue; }
            if (m.ManaValue() > castable) { continue; }
            AddCostToRefloatDemand(need, m);
        }
    }
    // REPEATABLE COMBAT PUMPS ARE A SINK, NOT ONE CAST. Firebreathing (Scourge of Valkas "{R}:
    // +1/+0") and a team pump (Lathliss "{1}{R}") spend every leftover mana of their colour in
    // combat -- ApplyFirebreathing reads the untapped pool after the main phase -- so on a turn
    // whose plan attacks, their demand is the whole remainder: pips x (remainder / MV). The
    // uncapped hand protected R here only by accident (dragons d0 gi340: four R pips of
    // uncastable dragons kept both Mountains for Scourge, 10 damage and the win on T6; capped to
    // the hand alone it spent a Mountain and dealt 9).
    if (pt != nullptr && pt->attack_matters && castable > 0)
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d == nullptr) { continue; }
            if (d->params.firebreathing_cost.has_value()
                && (p.card.IsCreature() || p.is_animated)
                && CanAttackFull(p, state.battlefield, active))
            {
                const ManaCost& m = d->params.firebreathing_cost.value();
                const int times = castable / std::max(1, m.ManaValue());
                for (int k = 0; k < times; ++k) { AddCostToRefloatDemand(need, m); }
            }
            if (d->params.team_pump_cost.has_value())
            {
                const ManaCost& m = d->params.team_pump_cost.value();
                const int times = castable / std::max(1, m.ManaValue());
                for (int k = 0; k < times; ++k) { AddCostToRefloatDemand(need, m); }
            }
        }
    }
    if (!skipped_self)
    {
        const int own[6] = { cost.white, cost.blue, cost.black, cost.red, cost.green, cost.colorless };
        for (int i = 0; i < 6; ++i) { need[i] = std::max(0, need[i] - own[i]); }
    }
    for (int i = 0; i < 6; ++i) { D[i] = need[i] * kNdScale; }
    if (!reveal) { return; }
    const Library& lib = state.players[active].library;
    const int n = static_cast<int>(lib.size());
    if (n <= 0) { return; }
    const int cap = std::max(1, total - cost.ManaValue());
    int pips[6] = { 0, 0, 0, 0, 0, 0 };
    for (const Card& c : lib)
    {
        // The DEFINITION's cost and type: a library Card is a placeholder (empty masks, empty cost).
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d == nullptr || d->card.IsLand()) { continue; }
        const ManaCost& m = d->card.m_mana_cost;
        if (m.ManaValue() > cap) { continue; }
        pips[0] += m.white; pips[1] += m.blue; pips[2] += m.black;
        pips[3] += m.red;   pips[4] += m.green; pips[5] += m.colorless;
    }
    for (int i = 0; i < 6; ++i) { D[i] += pips[i] * kNdScale / n; }
}

// MTG_FLOAT_TRACE (see the two print sites below). Namespace scope, not a function-local static:
// this is read on every payment, and a magic static would add a guard check to each.
static const bool g_float_trace = EnvOn("MTG_FLOAT_TRACE");
// MTG_CREATURE_ONLY_FIRST (DEFAULT ON; =0 restores the old rank) -- see the payer's rank site.
static bool CreatureOnlyFirstEnabled()
{
    static const bool on = EnvOn("MTG_CREATURE_ONLY_FIRST", true);
    return on;
}

// ---- COMPACT PAYMENT SNAPSHOT (2026-09-08) ---------------------------------------------------
//
// Payment rollback used to deep-copy the whole battlefield (`std::vector<Permanent> bf_pre =
// state.battlefield`) at THREE sites -- every TapForCostSharedOnce, the reserved-retry wrapper and
// the hybrid wrapper -- and EDF profiling put that copy machinery at ~23% of a whole game
// (vector<Permanent> copy-ctor 14.3% + operator= 6.9% + the allocator traffic behind them): each
// snapshot is N Permanent deep copies, each allocating for its counters vector, taken 2-3x per
// payment attempt, millions of times per game.
//
// A payment attempt mutates EXACTLY three Permanent fields before any restore point:
//   * tapped                (tap_source and the backtracker's tap sites)
//   * the Depletion counter (DecrementDepletionOnTap -- decrements the first entry's count;
//                            entries are never added or removed mid-payment)
//   * storage_counters      (a storage-land burst zeroes the battery)
// Everything else a payment touches lives on the PLAYERS or the pools (life, graveyard, energy,
// floating_mana, `available`) and was always restored separately. The battlefield's SIZE is
// invariant across every restore path: CommitPaySacSacrifices -- the only eraser -- runs strictly
// on success returns, which never restore. So rollback needs 9 bytes per permanent, not a deep
// Permanent copy. (AnimateLands' is_animated write is a separate entry point, outside every
// payment snapshot scope.)
//
// MTG_PAY_SNAP_VERIFY=1 (diagnostic, default off): every site ALSO takes the old full copy and
// compares field-by-field after the compact restore, aborting on the first divergence -- the
// tool that proves the mutation census above stays complete if the payment path grows a new
// side effect. The standing proof for the shipped engine is byte-identity across the full
// suite (this change is pure perf).
struct PermPaySnap
{
    bool tapped;
    int  depletion;   // first Depletion entry's count; -1 = no Depletion entry
    int  storage;
    bool eaten;       // §2b: marked as sac-outlet fodder by THIS payment attempt (Permanent::pay_sac_eaten)
    std::uint8_t dmg_mark;   // Prevent Damage: Permanent::mana_tap_mark (0 on every unarmed board)
};

static const bool g_pay_snap_verify = EnvOn("MTG_PAY_SNAP_VERIFY");

// ---- WARM SCRATCH FOR THE SNAPSHOT BUFFERS (perf, 2026-09-11) -------------------------------
//
// The compact snapshot above removed the deep Permanent copies but left the BUFFERS themselves
// freshly allocated per payment: every TapForCostSharedOnce builds a `vector<PermPaySnap>` and a
// `vector<Card>` graveyard copy, and each of the two TapForCostSharedImpl wrappers builds another
// pair. Those are four-to-six malloc/free round trips on a path that runs millions of times a
// game -- TapForCostSharedOnce alone is 43% of EDF instructions (perf, seed 9 gi=8) and the
// snapshot/restore machinery under it was already measured at 18.1% of all instructions.
//
// Same fix, same contract and the same soundness argument as TurnSolver.cpp's PayScratch: hand
// out a per-thread buffer and copy-ASSIGN into it, so steady state reuses the previous call's
// capacity and only the element copy remains. Nothing observable changes -- the bytes written are
// the bytes the fresh vector would have held; only a vector's CAPACITY differs, and no key,
// digest or log folds a capacity or a heap address.
//
// A POOL RATHER THAN ONE BUFFER PER TYPE, and that is load-bearing here for two reasons: the
// payment path NESTS (the hybrid wrapper re-enters TapForCostShared once per colour assignment,
// and TapForCostSharedOnce holds its own snapshot live across the backtracker), and a single
// function can hold two of these alive at once. Each scratch object takes the next free depth on
// construction and releases it on destruction, so an inner frame can never write the buffer an
// outer frame is still holding. std::deque, not std::vector, because growing the pool must not
// relocate a buffer an outer frame already references.
template <class T>
class PaySnapScratch
{
public:
    PaySnapScratch()
    {
        static thread_local std::deque<std::vector<T>> pool;
        static thread_local std::size_t                depth = 0;
        while (pool.size() <= depth) { pool.emplace_back(); }
        m_buf   = &pool[depth];
        m_depth = &depth;
        ++depth;
    }
    ~PaySnapScratch() { --(*m_depth); }
    PaySnapScratch(const PaySnapScratch&)            = delete;
    PaySnapScratch& operator=(const PaySnapScratch&) = delete;

    std::vector<T>& Buf() const { return *m_buf; }

private:
    std::vector<T>* m_buf   = nullptr;
    std::size_t*    m_depth = nullptr;
};

static void SnapPayFields(const std::vector<Permanent>& bf, std::vector<PermPaySnap>& out)
{
    out.resize(bf.size());
    for (std::size_t i = 0; i < bf.size(); ++i)
    {
        int dep = -1;
        for (const Counter& c : bf[i].counters)
        { if (c.type == Counter::Type::Depletion) { dep = c.count; break; } }
        out[i] = PermPaySnap{ bf[i].tapped, dep, bf[i].storage_counters, bf[i].pay_sac_eaten,
                              bf[i].mana_tap_mark };
    }
}

static void RestorePayFields(std::vector<Permanent>& bf, const std::vector<PermPaySnap>& snap)
{
    // The size invariant (see the header comment) makes the index alignment exact. min() is pure
    // belt: a violation would mean a new eraser ran on a failure path, which MTG_PAY_SNAP_VERIFY
    // exists to catch.
    const std::size_t n = std::min(bf.size(), snap.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        Permanent& p       = bf[i];
        p.tapped           = snap[i].tapped;
        p.storage_counters = snap[i].storage;
        p.pay_sac_eaten    = snap[i].eaten;   // §2b: a failed attempt eats nothing
        p.mana_tap_mark    = snap[i].dmg_mark; // a failed attempt records no damage event
        if (snap[i].depletion >= 0)
        {
            for (Counter& c : p.counters)
            { if (c.type == Counter::Type::Depletion) { c.count = snap[i].depletion; break; } }
        }
    }
}

// Verify half: full-copy comparison over EVERY Permanent field (kept in sync with Permanent.h by
// hand; a miss here only weakens the diagnostic, never the engine).
static void VerifyPaySnapRestore(const std::vector<Permanent>& now,
                                 const std::vector<Permanent>& want, const char* site)
{
    auto fail = [&](std::size_t i, const char* field)
    {
        std::fprintf(stderr, "[pay-snap-verify] DIVERGENCE at %s: permanent %zu field %s\n",
                     site, i, field);
        std::abort();
    };
    if (now.size() != want.size())
    { std::fprintf(stderr, "[pay-snap-verify] SIZE DIVERGENCE at %s: %zu vs %zu\n",
                   site, now.size(), want.size()); std::abort(); }
    for (std::size_t i = 0; i < now.size(); ++i)
    {
        const Permanent& a = now[i];
        const Permanent& b = want[i];
        if (a.card.m_number != b.card.m_number)   { fail(i, "card"); }
        if (a.controller_index != b.controller_index) { fail(i, "controller_index"); }
        if (a.owner_index != b.owner_index)       { fail(i, "owner_index"); }
        if (a.tapped != b.tapped)                 { fail(i, "tapped"); }
        if (a.damage != b.damage)                 { fail(i, "damage"); }
        if (a.pending_death_trigger != b.pending_death_trigger) { fail(i, "pending_death_trigger"); }
        if (a.counters.size() != b.counters.size()) { fail(i, "counters.size"); }
        for (std::size_t k = 0; k < a.counters.size(); ++k)
        { if (a.counters[k].type != b.counters[k].type
              || a.counters[k].count != b.counters[k].count) { fail(i, "counters"); } }
        if (a.entered_this_turn != b.entered_this_turn) { fail(i, "entered_this_turn"); }
        if (a.gained_control_this_turn != b.gained_control_this_turn) { fail(i, "gained_control_this_turn"); }
        if (a.aura_attached_to != b.aura_attached_to) { fail(i, "aura_attached_to"); }
        if (a.marked_for_destruction != b.marked_for_destruction) { fail(i, "marked_for_destruction"); }
        if (a.temp_power_bonus != b.temp_power_bonus) { fail(i, "temp_power_bonus"); }
        if (a.temp_tough_bonus != b.temp_tough_bonus) { fail(i, "temp_tough_bonus"); }
        if (a.charge_counters != b.charge_counters)   { fail(i, "charge_counters"); }
        if (a.hone_counters != b.hone_counters)       { fail(i, "hone_counters"); }
        if (a.verse_counters != b.verse_counters)     { fail(i, "verse_counters"); }
        if (a.storage_counters != b.storage_counters) { fail(i, "storage_counters"); }
        if (a.storage_hold_this_turn != b.storage_hold_this_turn) { fail(i, "storage_hold_this_turn"); }
        if (a.pay_sac_eaten != b.pay_sac_eaten)       { fail(i, "pay_sac_eaten"); }
        if (a.mana_tap_mark != b.mana_tap_mark)       { fail(i, "mana_tap_mark"); }
        if (a.garth_chosen_mask != b.garth_chosen_mask) { fail(i, "garth_chosen_mask"); }
        if (a.loyalty != b.loyalty)                   { fail(i, "loyalty"); }
        if (a.loyalty_activated_this_turn != b.loyalty_activated_this_turn) { fail(i, "loyalty_activated_this_turn"); }
        if (a.equipped_to != b.equipped_to)           { fail(i, "equipped_to"); }
        if (a.colored_cast_lifegain_used_this_turn != b.colored_cast_lifegain_used_this_turn) { fail(i, "colored_cast_lifegain"); }
        // Nykthos Paragon's once-each-turn flag. Like the two until-EOT grants noted below, no
        // payment path can set it, so this is a completeness fix to a CHECKER and not a behaviour
        // change: it can only turn a silent divergence into a loud one.
        if (a.lifegain_counters_used_this_turn != b.lifegain_counters_used_this_turn) { fail(i, "lifegain_counters_used"); }
        if (a.ice_counters != b.ice_counters)         { fail(i, "ice_counters"); }
        if (a.age_counters != b.age_counters)         { fail(i, "age_counters"); }
        if (a.temp_haste != b.temp_haste)             { fail(i, "temp_haste"); }
        // The other two until-EOT keyword grants. temp_lifelink was missing here since it was added
        // (2026-09-08) -- noticed while adding temp_double_strike beside it. Neither can be set by a
        // payment path, so this is a completeness fix to a CHECKER, not a behaviour change: it can
        // only turn a silent divergence into a loud one.
        if (a.temp_lifelink != b.temp_lifelink)       { fail(i, "temp_lifelink"); }
        if (a.temp_double_strike != b.temp_double_strike) { fail(i, "temp_double_strike"); }
        if (a.paired_with != b.paired_with)           { fail(i, "paired_with"); }
        if (a.exile_at_end != b.exile_at_end)         { fail(i, "exile_at_end"); }
        if (a.chosen_subtype_id != b.chosen_subtype_id) { fail(i, "chosen_subtype_id"); }
        if (a.is_animated != b.is_animated)           { fail(i, "is_animated"); }
        // Typed vs all-types animation (Gideon's +1 vs Mutavault): same is_animated bit, but a
        // divergence here silently changes which subtype lords reach the body.
        if (a.animated_printed_types != b.animated_printed_types)
        { fail(i, "animated_printed_types"); }
        if (a.is_token != b.is_token)                 { fail(i, "is_token"); }
        if (a.echo_resolved != b.echo_resolved)       { fail(i, "echo_resolved"); }
        // EXERT (CR 701.38): the tap-token activation path this file owns is exactly what SETS this
        // flag, so a divergence here is the most likely of any field in the list -- one world exerting
        // and the other not is a whole attack of difference next turn.
        if (a.skip_next_untap != b.skip_next_untap)   { fail(i, "skip_next_untap"); }
        if (a.etb_tap_pending != b.etb_tap_pending)   { fail(i, "etb_tap_pending"); }
    }
}

// Tap one non-filter source, producing `amt` of colour `col`, applying depletion
// decrement and pain. Mirrors the accounting in BuildAvailableMana (AddSourceToPool).
//
// EXTRACTED from TapForCostSharedOnce's `tap_source` lambda -- a pure code move (every branch,
// comment and side effect verbatim; the four captures became parameters) so the HUMAN PRE-TAP
// (ApplyHumanPreTap) can tap a source through THE SAME mechanic the payment uses. A second
// implementation of "tap this land" is how a painland stops taking its damage, an Aether Hub stops
// spending its {E}, a storage land stops bursting all its counters and an enchanted land stops
// paying its Aura's bonus: four rules bugs for the price of one copy-paste.
//
// `available` is the executor's turn-scoped accounting pool, decremented as the source taps;
// nullptr for callers that keep no such pool (the rollout, and the pre-tap).
void TapSourceIntoFloat(GameState& state, int active, Permanent& p, const CardDefinition& def,
                        Color col, ManaPool& floating, ManaPool* available, bool for_creature,
                        const std::vector<int>* aura_colors)
{
    CcoAuditTap(def, col, for_creature);   // legality audit (MTG_CCO_AUDIT); inert when off
    // TEMP DIAGNOSTIC (MTG_TAPDBG, default off): every real tap with source, colour and energy.
    { static const bool s_tapdbg = EnvOn("MTG_TAPDBG");
      if (s_tapdbg && g_real_resolution)
      { std::fprintf(stderr, "[tapdbg] tap %s col=%d energy=%d\n",
                     def.card.m_name.str().c_str(), (int)col,
                     state.players[active].energy_counters); } }
    p.tapped = true;
    // Rollback audit (count-only, real play): this tap and the alternatives it had.
    if (payledger::On() && g_real_resolution)
    { payledger::Record(state, active, p, def, col, payledger::t_cur_pip_any); }
    // Rollback ledger (MTG_PAY_ROLLBACK, both worlds): the committed tap, on the state.
    if (PayRollbackOn())
    { state.pay_ledger.Push(state.turn_number, active, p.card.m_number, static_cast<int>(col), payledger::t_cur_pip_any); }
    // CRACK FLAG: a pay-sac source (Treasure / Eldrazi Spawn) is SACRIFICED, not left tapped, and
    // the erase is deferred to CommitPaySacSacrifices -- which uses this flag to skip its whole
    // battlefield walk on the overwhelmingly common board that cracked nothing. Set here because
    // this is the tap helper a pay-sac source goes through: it produces no mana of its own, so the
    // filter / ramp-filter / untap-burst branches in TapForCostSharedOnce all skip it (each
    // requires `is_filter`, `any_color_filter`, or a non-empty `produces`). MTG_PAYSAC_VERIFY=1
    // tests that claim rather than resting on it.
    if (IsPaySacSource(def)) { g_paysac_cracked = true; }
    DecrementDepletionOnTap(p);
    // Aether Hub: "{T}, Pay {E}: Add one mana of any color." Energy is part of the ACTIVATION
    // cost, so it is spent here beside the depletion counter. Only the COLOURED mode costs it --
    // the separate free "{T}: Add {C}" ability does not -- which is the same Colorless guard the
    // painland damage below uses, and is what keeps a spent-out Hub a live {C} source for
    // Eldrazi Displacer's {2}{C} pip.
    if (def.params.energy_per_colored_tap > 0 && col != Color::Colorless)
    { state.players[active].energy_counters -= def.params.energy_per_colored_tap; }
    // Deathrite Shaman ability 1: the mana tap exiles a graveyard land (usable() guaranteed
    // one exists). A failed payment restores the graveyard from gy_pre below.
    if (def.params.gy_land_exile_mana) { ExileGraveyardLandForMana(state, active); }
    // Painland ({T}: Add {C}. / {T}: Add {W} or {U}. This land deals 1 damage to you.) -- the
    // two are SEPARATE abilities and only the coloured one hurts, so a Colorless tap of a land
    // that actually has a {C} mode is painless. Exactly the shape of the Grove drip guard
    // above. The `produces contains Colorless` half is load-bearing for byte-identity: a
    // painland with no {C} mode (how every painland was modelled before this deck) can still
    // be handed Color::Colorless for a GENERIC pip, and must keep taking its damage there.
    if (state.dmg_events_armed)
    {
        // PREVENT DAMAGE (armed only): the tap is a damage EVENT + a land tap. The pain comes off our
        // life now (unless Purity prevents it) and the triggers are recorded on the permanent for
        // the payment's flush (core/DamageEvents.h). Same painless-{C} rule as below, except that
        // a tap_self_damage_any_mode land (Ancient Tomb) hurts in every mode.
        int pain = 0;
        if (def.params.tap_self_damage > 0)
        {
            bool has_c_mode = false;
            for (Color pc : EffectiveProducesFor(state, active, def, &p))
            { if (pc == Color::Colorless) { has_c_mode = true; break; } }
            pain = dmgev::PainForTap(def, col, has_c_mode);
        }
        dmgev::ArmedManaTap(state, active, p, pain);
    }
    else if (def.params.tap_self_damage > 0)
    {
        // ONE EffectiveProduces call: it returns a reference into a thread_local buffer that
        // the next call overwrites (see RitualTapAheadIntoFloat's note).
        bool has_c_mode = false;
        for (Color pc : EffectiveProducesFor(state, active, def, &p))
        { if (pc == Color::Colorless) { has_c_mode = true; break; } }
        if (!(col == Color::Colorless && has_c_mode))
        { state.players[active].life -= def.params.tap_self_damage; }
    }
    // Grove of the Burnwillows: the COLOURED tap ({R}/{G}) makes the opponent gain 1 (-> 1 damage
    // with Tainted Remedy out). A `col == Colorless` tap is the painless "{T}: Add {C}" mode --
    // no drip (see DripLandAnyPipColor: a generic pip absent a Remedy routes here as Colorless).
    if (def.params.tap_opponent_lifegain > 0 && col != Color::Colorless)
    {
        // Grove: "each opponent gains 1" -- once per head (2HG = x2, shared pool).
        OpponentGainsLife(state, active,
                          def.params.tap_opponent_lifegain * gamesetup::OpponentHeads(),
                          def.card.m_name.str());
    }
    // Karoo bounce land ({U}{R} from one tap): produce one mana of EACH colour it makes, so a
    // lone Izzet Boilerworks can pay a two-colour cost (Expressive Iteration {U}{R}) the planner
    // promised. Crediting `amt` of the single matched colour would lose the second colour --
    // the spell was enumerated but unpayable, a silent no-op. AddSourceToPool credits such a
    // land as `amt` wild, so the executor decrements `available.wild`. Single-colour sources
    // keep `amt` of the matched colour (byte-identical).
    //
    // `amt` = mana produced into floating; `consumed` = mana removed from this-turn's `available`
    // pool. For a STORAGE-COUNTER land (Dwarven Hold / Mercadian Bazaar) a single tap now BURSTS
    // ALL live counters (amt == consumed == had): the land is committed for the turn, and the
    // planner already credits it its full PermanentManaYield (= counters) and marks the whole count
    // consumed on tap. The old per-spell PARTIAL burst (amt = min(had, cost - produced_total)) set
    // consumed = had but floated LESS, so the executor delivered fewer red than the planner
    // promised -- silently dropping a legal cast on a tight multi-spell plan when an earlier spell
    // under-burst and stranded a counter (burst amount shifted with irrelevant cast order). See
    // docs/design/dragonstorm-plan-execution-fidelity-bug.md. Bank-the-rest is via the RESERVE (an
    // unneeded storage land is held untapped), not a partial burst. ManaSourceRank taps storage
    // LAST. Non-storage sources keep amt == consumed == the static per-tap yield -> byte-identical
    // for every non-storage deck.
    int amt, consumed;
    if (def.params.storage_land)
    {
        amt = consumed = p.storage_counters;   // burst ALL counters on tap
        p.storage_counters = 0;
    }
    else if (def.params.domain_mana)
    {
        // Faeburrow / Bloom Tender: one mana of EACH colour among controlled permanents.
        amt = consumed = static_cast<int>(EffectiveProducesFor(state, active, def, &p).size());
    }
    else if (IsScaledManaDork(def))
    {
        // Priest of Titania / Elvish Archdruid: one tap bursts the LIVE Elf count of {G}.
        amt = consumed = ScaledDorkCount(state, active, def);
    }
    else { amt = consumed = ManaProducedPerTap(def); }
    // Per-PERMANENT: a locked etb_choose_color rock produces its ONE colour here, so it takes
    // the single-colour path below instead of the multi-colour `wild` accounting -- which is
    // what makes the executor's tap match the pool credit AddSourceToPool made for it.
    const std::vector<Color>& prod = EffectiveProducesFor(state, active, def, &p);
    // A multi-mode source that could have made {C} stops being able to once it is tapped, so
    // retire its share of ManaPool::wild_c alongside its `wild` -- otherwise the projection keeps
    // promising a {C} the board can no longer produce. Only ever nonzero for a source whose
    // produces list includes Colorless, so every other deck's accounting is untouched.
    const bool made_c = std::find(prod.begin(), prod.end(), Color::Colorless) != prod.end();
    auto retire_wild_c = [&](int n)
    { if (available && made_c) { available->wild_c = std::max(0, available->wild_c - n); } };
    // Accomplished Alchemist is amt>1 across a multi-colour `produces` but is NOT a one-of-each
    // bundle: "Add X mana of any ONE color". So it must take the single-colour branch below, and
    // there it must debit `wild` rather than the colour -- AddSourceToPool credited a multi-colour
    // source as `amt` wild, and debiting the COLOUR for a 7-unit burst would drive that colour to
    // -7 while leaving the wild untouched. Same fix required in the backtracker (SpellEffects.cpp);
    // fixing only one leaves the documented greedy/backtracker split.
    const bool one_colour_burst = IsSingleColorBurstSource(def);
    if (amt > 1 && prod.size() > 1 && !one_colour_burst)
    {
        for (Color c : prod) { floating.Add(c, 1); }
        if (available) { available->wild -= consumed; }
        retire_wild_c(consumed);
    }
    else
    {
        floating.Add(col, amt);
        if (available)
        {
            if (one_colour_burst && prod.size() > 1)
            { available->wild = std::max(0, available->wild - consumed); }
            else { available->Add(col, -consumed); }
        }
        if (prod.size() > 1) { retire_wild_c(consumed); }
    }
    // "Whenever enchanted land is tapped for mana, its controller adds an additional <X>" --
    // the land Aura's mana arrives on THIS tap, in the AURA's colour (Wild Growth {G},
    // Overgrowth {G}{G}, Fertile Ground any). AvailableManaPool credited it the same way via
    // AddSourceToPool, so retire the same units from `available`. No-op with no aura attached.
    // Per-tap half of MTG_FLOAT_TRACE. NOTE it covers only the GREEDY path: TapForCostBacktrack
    // has its own tapping, so a cast paid by the backtracker prints a `cost=` line with no `tap`
    // lines above it. That absence is itself the useful signal (it says which solver paid).
    if (g_float_trace && !AllPlayHooksNull())
    { std::fprintf(stderr, "[float]   tap %s as col=%d amt=%d -> float{w%d u%d b%d r%d g%d c%d *%d}\n",
                   def.card.m_name.str().c_str(), (int)col, amt,
                   floating.white, floating.blue, floating.black, floating.red,
                   floating.green, floating.colorless, floating.wild); }
    if (LandAuraBonus(state, p) > 0)
    {
        ManaPool bonus;
        // `aura_colors` is the human pre-tap's positional pick for the "one mana of any color"
        // Auras (Fertile Ground / Trace of Abundance); nullptr for every payment caller, which is
        // what keeps the allocator's credit -- and therefore GT -- byte-identical.
        LandAuraAddToPool(bonus, state, p, aura_colors);
        floating.AddPool(bonus);
        if (available)
        {
            available->white -= bonus.white; available->blue      -= bonus.blue;
            available->black -= bonus.black; available->red       -= bonus.red;
            available->green -= bonus.green; available->colorless -= bonus.colorless;
            available->wild  -= bonus.wild;
        }
    }
}

static bool TapForCostSharedOnceImpl(GameState& state, const ManaCost& cost_in, bool for_creature,
                                     std::uint64_t reserved_mask, ManaPool* available,
                                     bool honor_legacy_cco);

// PREVENT DAMAGE: the pain-aware payment policy (dmgev::PainAwarePay) wraps each payment ATTEMPT --
// the reservation ladder's held attempts and the unrestricted one alike -- so the damage floor and
// the cap are computed over the sources THIS attempt may tap. Wrapping the whole ladder instead
// computed the floor over every source, so a held attempt failed its cap and the unrestricted
// fallback paid the minimum with the very source the hold was keeping for a later cast of the same
// line (Prevent Damage s31016 gi15 T5: Green Sun's Zenith paid painlessly with City / Coliseum /
// Citadel, stranding the {R} the line's Rolling Earthquake needed). Only the unrestricted attempt
// may fall back to a lethal assignment (allow_lethal). A nested payment (the hybrid wrapper) sees
// the policy live and only enforces it; unarmed boards pass straight through.
bool TapForCostSharedOnce(GameState& state, const ManaCost& cost_in, bool for_creature,
                          std::uint64_t reserved_mask, ManaPool* available,
                          bool honor_legacy_cco)
{
    return dmgev::PainAwarePay(state, available, /*allow_lethal=*/reserved_mask == 0, [&]() -> bool
    { return TapForCostSharedOnceImpl(state, cost_in, for_creature, reserved_mask | dmgev::t_pay_hold,
                                      available, honor_legacy_cco); },
    [&](bool barbs, int barb_total) -> dmgev::PayFloor
    { return PaymentDamageFloor(state, cost_in, state.floating_mana, barbs, barb_total, reserved_mask); },
    [&](bool probe) -> int
    {
        // The line being applied: the plan's other mana casts, approximated exactly as
        // ScarceColorHoldMask does (the plan's coloured pips and mana value, minus this cost's --
        // an OVER-estimate of what is still owed once earlier casts have paid, so the check can
        // only err toward the historical payment).
        const PlanTraits* pt = CurrentPlanTraits();
        if (pt == nullptr || pt->mana_casts < 2) { return -1; }
        if (probe) { return 1; }
        const int cur[5] = { cost_in.white, cost_in.blue, cost_in.black, cost_in.red, cost_in.green };
        int need[5], need_tot = std::max(0, pt->cast_mv_total - cost_in.ManaValue());
        for (int c = 0; c < 5; ++c) { need[c] = std::max(0, pt->cast_pips[c] - cur[c]); }
        const int a = state.active_player_index;
        const ManaPool& fl = state.floating_mana;
        int have_tot = fl.Total();
        int have[5] = { fl.white + fl.wild, fl.blue + fl.wild, fl.black + fl.wild,
                        fl.red + fl.wild, fl.green + fl.wild };
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != a || p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d == nullptr) { continue; }
            const int y = SourceMaxNetLive(state, p, *d);
            if (y <= 0) { continue; }
            have_tot += y;
            int seen = 0;
            for (Color c : EffectiveProducesFor(state, a, *d, &p))
            {
                const int ci = static_cast<int>(c);
                if (ci >= 5 || (seen & (1 << ci))) { continue; }
                seen |= (1 << ci);
                have[ci] += y;
            }
        }
        if (have_tot < need_tot) { return 0; }
        for (int c = 0; c < 5; ++c) { if (have[c] < need[c]) { return 0; } }
        return 1;
    });
}

static bool TapForCostSharedOnceImpl(GameState& state, const ManaCost& cost_in, bool for_creature,
                                     std::uint64_t reserved_mask, ManaPool* available,
                                     bool honor_legacy_cco)
{
    if (tapstats::Enabled()) { tapstats::g_pay_once.fetch_add(1, std::memory_order_relaxed); }
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed (held locally)

    // Payment-legal produces for the scarcity path. ProducesForPayment: a colored_creature_only
    // land (Unclaimed Territory / Cavern of Souls / Sliver Hive / Secluded Courtyard) makes only
    // {C} for a NON-creature spell, so it must not be selected for a coloured pip there -- it
    // still pays a generic pip as {C}. The rollout-only MTG_LEGACY_CCO_PAY hatch re-enables the
    // old EffectiveProduces read so the fix's effect stays measurable in one binary (it makes the
    // rollout score lines the game cannot realise; see docs/design/post-breakpoint-search.md).
    static const bool s_legacy_cco = EnvOn("MTG_LEGACY_CCO_PAY");
    // `pm` (optional): the PERMANENT this query is about, so an etb_choose_color source resolves
    // its locked colour instead of the definition's menu. Callers that have one must pass it.
    auto pay_produces = [&](const CardDefinition& d, const Permanent* pm = nullptr)
                            -> const std::vector<Color>&
    {
        if (honor_legacy_cco && s_legacy_cco) { return EffectiveProducesFor(state, active, d, pm); }
        return ProducesForPayment(state, active, d, for_creature, pm);
    };

    // STRICT SNOW PAYMENT scope (see g_snow_pay_strict): only when the cost carries {S} pips AND
    // some mana source the payer controls is NOT snow. On an all-snow board (the Snow deck,
    // always) strict stays off and every snow branch below is a no-op -- byte-identity by
    // construction. The scan covers TAPPED sources too: turn-scoped floating is leftover tap
    // output, and it counts as snow only if everything that could have produced it is snow.
    // (Ritual float on a snow board is unattributable and assumed snow -- no current deck mixes
    // rituals with {S} costs; disclosed in the Snow ledger.)
    bool snow_strict = false;
    if (cost_in.snow_pips > 0)
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d) { continue; }
            const bool sourcey = d->card.IsLand() || d->tmpl == CardTemplate::ManaDork
                              || d->params.mana_rock || d->params.is_filter
                              || d->params.any_color_filter || d->params.ramp_filter;
            if (sourcey && !d->card.HasSupertype(Supertype::Snow)) { snow_strict = true; break; }
        }
    }
    struct SnowStrictScope
    {
        bool prev;
        explicit SnowStrictScope(bool on) : prev(g_snow_pay_strict) { g_snow_pay_strict = on; }
        ~SnowStrictScope() { g_snow_pay_strict = prev; }
    } _snow_strict_scope(snow_strict);

    // HOLD THE COLOURLESS BANK FOR THE BOARD'S {C} SINK (human play only; MTG_HOLD_C_FOR_SINK=0
    // restores). USER, EDF seed 8 (2026-09-09): *"the mana spending is so poor... I want to retain
    // my colourless in this deck."*
    //
    // MEASURED, not assumed. MTG_FLOAT_TRACE over the user's own hand-driven blink line shows the
    // loop banking colourless CORRECTLY -- the float climbs {g44,c22} in lockstep, +2 green +1
    // colourless per Drake blink, and the refloat counters read committed C=54023 vs G=18525, so
    // the tap-ahead is not the defect. The bank then falls off a cliff in multi-mana steps
    // (c22 -> c18 -> c13 -> c11 ... -> c0) while FORTY-SIX GREEN sits untouched, and the line dies
    // one {C} short of the next Eldrazi Displacer activation.
    //
    // The cliff is the GENERIC-ONLY costs this deck spends its bank on: the Clue token's {2}, the
    // Conservatory's {4} investigate, Mariposa's {5} draw. `SpendFloatingTowardCost` drains generic
    // pips wild -> COLOURLESS -> W/U/B/R/G, so each of those ate 2-5 colourless off the top with a
    // mountain of green available to pay them instead.
    //
    // Session 5b's MTG_HOLD_C_FOR_PIPS already fixed the neighbouring case -- a cost with a {C} pip
    // OF ITS OWN holds colourless back from its own generic portion -- but its trigger is the COST,
    // and a Clue sacrifice has no {C} pip at all. The demand signal is not this cost, it is the
    // BOARD: while we control a permanent whose activation carries a {C} pip (Eldrazi Displacer's
    // {2}{C} blink, Essence Depleter's {1}{C} drain, Dimensional Infiltrator's {1}{C}), colourless
    // is the scarcest thing in the pool and no generic pip should touch it while a colour can pay.
    // Locally free in the same sense as 5b: paying generic from a colour instead is never worse
    // when the colour is there, and when it is NOT there the drain still falls through to
    // colourless (hold_c only REORDERS, it never refuses -- see SpendFloatingTowardCost).
    //
    // HUMAN PLAY ONLY, per the human-line-vs-AI-average rule and the FilterCFirst / HUMAN_TAP_DEMAND
    // precedents directly below: the search pays a whole plan as one batch and its ordering is
    // measured, while a human builds a phase as a sequence of separate activations this payment
    // cannot see past. HumanPlayActive() is false in rollouts (HumanPlaySuppress) and in every
    // autonomous run, so GT/scenarios/smoke are byte-identical by construction.
    // THE KNOWN COST, stated up front: this hold changes `references/EldraziDisplacerFlicker/
    // claude_s1_gi0` from its recorded turn-3 win to a turn-4 one (viewer_protocol_check reports it
    // as the sweep's ONE play-drift; 304 of 305 references are unaffected, and smoke / the six
    // autonomous reference digests are byte-identical because none of this runs outside human
    // play). It is a deliberate trade, reported rather than hidden -- the same call session 5c made
    // when the cast-site painland fix cost claude_s2_gi1 -- because the bug it fixes is the user's
    // stated worst: *"The worst is the poor colourless usage that makes Displacer almost useless."*
    // MTG_HOLD_C_FOR_SINK=0 reverts in one binary if the user judges the trade the other way.
    //
    // TWO NARROWINGS WERE TRIED AND BOTH FAILED TO SEPARATE THE CASES -- recorded so the next
    // attempt does not repeat them:
    //   (a) the per-colour surplus BUDGET below. It is kept (it is right on its own terms), but it
    //       does not rescue s1_gi0: that is a go-off turn, and the demand model cannot see the
    //       cards the turn is about to WISH FOR or DRAW, so a colour that scores as surplus now is
    //       spent and a later fetched cast strands.
    //   (b) the user's own "once we have a STOCK of other mana" as a literal float threshold
    //       (>= 8, then >= 20 non-colourless floating). Both still drift s1_gi0 and neither
    //       loosened the seed-8 fix, because s1_gi0's turn 3 is ALSO a go-off with a fat float --
    //       pool size is simply not the axis that distinguishes the two.
    // What would settle it is a measured sweep (heuristic-optimization), not another hand-picked
    // constant; the honest state today is one decisive user-reported fix against one reference.
    static const bool s_hold_c_sink = EnvOn("MTG_HOLD_C_FOR_SINK", true);
    const bool board_c_sink = s_hold_c_sink && HumanPlayActive()
                           && state.floating_mana.colorless > 0
                           && BoardHasColorlessPipSink(state, active);
    // ...AND THE HOLD IS BUDGETED, which is what stops it trading one stranding for another. Only a
    // colour we hold MORE of than the rest of the turn still wants may pay a generic pip ahead of
    // the bank; a colour at or under its demand is scarcer than the colourless and keeps its place
    // behind it. Demand = the hand's coloured pips plus the board's repeatable activations, i.e.
    // exactly ComputeRefloatDemand, the same model the ETB tap-ahead commits its colours by --
    // one demand notion for the whole human-play mana policy rather than two that can disagree.
    // Unbudgeted, the hold cost references/EldraziDisplacerFlicker/claude_s1_gi0 its recorded T3.
    int c_budget[5] = { 0, 0, 0, 0, 0 };
    if (board_c_sink)
    {
        int demand[6] = { 0, 0, 0, 0, 0, 0 };
        ComputeHumanPlayDemand(state, active, demand);
        const ManaPool& f = state.floating_mana;
        const int have[5] = { f.white, f.blue, f.black, f.red, f.green };
        for (int i = 0; i < 5; ++i) { c_budget[i] = std::max(0, have[i] - demand[i]); }
    }
    HoldColorlessScope _hcs_sink(board_c_sink ? true : g_hold_colorless_for_pips);
    GenericSpendBudgetScope _gsb_sink(board_c_sink ? c_budget : nullptr);
    // ...AND THE SAME HOLD INSIDE THIS PAYMENT'S OWN POOL (see g_hold_colorless_in_payment).
    // `board_c_sink` above requires colourless to be FLOATING already, because that is what the
    // scope it guards can reorder. The in-payment hold has no such precondition: the {C} it
    // protects is produced by a tap this payment is about to make (Mariposa's `{C}{G}` on the
    // user's seed-8 T3 frame), so the gate is the SINK plus the board's ability to make {C} at all.
    // MTG_HOLD_C_IN_PAYMENT=0 isolates just this half.
    static const bool s_hold_c_pay = EnvOn("MTG_HOLD_C_IN_PAYMENT", true);
    bool board_makes_c = false;
    if (s_hold_c_pay && s_hold_c_sink && HumanPlayActive()
        && BoardHasColorlessPipSink(state, active))
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d == nullptr) { continue; }
            for (Color c : EffectiveProduces(state, active, *d))
            { if (c == Color::Colorless) { board_makes_c = true; break; } }
            if (board_makes_c) { break; }
        }
    }
    HoldColorlessInPaymentScope _hcp_sink(board_makes_c);

    // Spend any turn-scoped RESERVE mana (a ritual's floating output) before tapping. No-op when
    // empty -> byte-identical for non-ritual decks. Restored if the whole payment fails below.
    const ManaPool reserve_pre = state.floating_mana;
    const ManaPool cre_reserve_pre = state.floating_creature_mana;
    const ManaPool big_reserve_pre = state.floating_bigspell_mana;
    ManaCost cost = cost_in;
    // A CREATURE spell spends the creature-only reserve FIRST (it can pay nothing else, so using it
    // first is never worse); a noncreature payment never touches it. Empty for every deck without a
    // multi-yield creature_mana_only source -> byte-identical.
    if (for_creature && state.floating_creature_mana.Total() > 0)
    { SpendFloatingTowardCost(state.floating_creature_mana, cost); }
    // Same for the BIG-SPELL-ONLY reserve (Troyan's floated {G}{U}): a qualifying spell (MV 5+ or
    // {X}) spends it before the general float -- it can pay nothing else. Empty for every deck
    // without such a source -> byte-identical.
    if (BigFloatUsableNow(state))
    {
        const int before = static_cast<int>(state.floating_bigspell_mana.Total());
        SpendFloatingTowardCost(state.floating_bigspell_mana, cost);
        if (BigFloatStats::Enabled())
        { BigFloatStats::g_spent.fetch_add(static_cast<std::uint64_t>(before - state.floating_bigspell_mana.Total()),
                                           std::memory_order_relaxed); }
    }
    SpendFloatingTowardCost(state.floating_mana, cost);
    // Publish this payment's coloured need (net of floating) for the sole-colour-provider rank
    // tier -- see PayNeedScope in SpellEffects.h. RAII: dead again the instant this payment ends.
    PayNeedScope _pns(cost.white, cost.blue, cost.black, cost.red, cost.green, cost.ManaValue());
    // ...and this payment's OWN coloured pips as the line hold counts them (cost_in, before any
    // float is spent: the hold carries the cast's full cost until it lands). The generic-pip
    // readers of the hold take them back out -- see g_pay_own_line_pips in SpellEffects.h.
    PayOwnPipsScope _pops(cost_in);
    const int pay_full_mv = cost.ManaValue();   // restored before the backtracker (see pay())

    // SNOW-pip restriction (see ManaCost::snow_pips): while the greedy loop is settling an {S}
    // pip this flag narrows every candidate to SNOW producers -- the one choke point (usable)
    // covers the scarcity path, the legacy 4-step path and the filter branches alike. The
    // producing PERMANENT's snow-ness is what CR 106.4b cares about (a fed snow filter's output
    // is snow mana regardless of the feeder). false for every non-{S} pip -> byte-identical.
    bool paying_snow = false;

    // Needs-based tap order (MTG_NEEDS_TAP_ORDER): per-payment demand/supply, computed lazily at the
    // first pip that reaches the scarcity loop and decremented as taps land. See
    // ComputeNeedsDemandSupply at the top of this file.
    bool nd_ready = false;
    int  nd_left[7] = { 0, 0, 0, 0, 0, 0, 0 };   // W,U,B,R,G,C pips + generic still owed by THIS payment (needs lever)
    int  nd_D[6] = { 0, 0, 0, 0, 0, 0 };
    int  nd_S[6] = { 0, 0, 0, 0, 0, 0 };

    // §2b SAC-FOR-MANA FODDER (MTG_SAC_OUTLET_PAY -- see SacOutletPayEnabled in SpellEffects.h).
    // Resolved ONCE per payment, never per source per pip: the latter is the O(board^2) walk this
    // whole strand exists to delete on a 40-Saproling board. An invalid descriptor makes every
    // branch below a single null test, which is what keeps the lever's OFF arm and every deck
    // without an outlet byte-identical.
    const SacPayOutlet sac_outlet = LiveSacPayOutlet(state, active);
    // Brightcap Badger's "each Fungus and Saproling you control has '{T}: Add {G}'". Resolved ONCE
    // per payment, exactly like the outlet above, and free for every deck that cannot contain a
    // grant source (GameState::deck_has_mana_grant).
    const ManaGrant    mana_grant = LiveManaGrant(state, active);
    // Fodder is only ever considered for a permanent `usable()` REJECTS, so no permanent can be
    // offered twice in one pip selection (a Goblin mana dork taps at its own rank; it is not eaten).
    // `def` is NULLABLE: a TOKEN has no CardDefinition, and on this deck the fodder IS tokens (see
    // IsSacPayFodder). Requiring one here is what made the §2b payment source unable to eat the very
    // bodies MTG_SAC_OUTLET_PAY suppresses the searched actions for.
    auto fodder_ok = [&](const Permanent& p, const CardDefinition* def) -> bool
    {
        if (!sac_outlet.valid()) { return false; }
        // The creature THIS cast is about to sacrifice as a cost is already spent (see
        // g_pay_sac_victim below); eating it too would spend one body twice.
        if (g_pay_sac_victim != 0 && p.card.m_number == g_pay_sac_victim) { return false; }
        // Snow-pip restriction: with no definition we cannot prove the body is snow, so refuse --
        // the pessimistic direction (a payable cast fails, never an unpayable one resolving).
        if (paying_snow && !(def != nullptr && def->card.HasSupertype(Supertype::Snow))
            && !p.card.HasSupertype(Supertype::Snow)) { return false; }
        if (def != nullptr && def->params.creature_mana_only && !for_creature) { return false; }
        if (reserved_mask)
        {
            const std::size_t idx = static_cast<std::size_t>(&p - state.battlefield.data());
            if (idx < 64 && (reserved_mask & (1ull << idx))) { return false; }
        }
        return IsSacPayFodder(p, def, sac_outlet);
    };


    // CREATURE-ONLY MANA NEVER FEEDS A FILTER (Somberwald Sage + Skycloud Expanse). A filter's {1}
    // is the activation cost of a mana ability, not a creature spell, so restricted mana cannot pay
    // it (CR 106.6 + the card's restriction) -- even when the filter's output then pays a creature
    // spell. `feeding` marks the recursive feed call (usable() refuses a creature_mana_only source
    // there), and `restricted_in_float` marks that this payment's local float may already hold
    // restricted units, which the fed steps (3)-(5) below would otherwise consume as the feed. Both
    // false for any payment that never touches such a source -> byte-identical.
    bool feeding = false;
    bool restricted_in_float = false;
    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        if (paying_snow && !def.card.HasSupertype(Supertype::Snow)) { return false; }
        if (feeding && (def.params.creature_mana_only || BigSpellOnlySource(def.params))) { return false; }
        if (reserved_mask)   // reservation audit: a held source is not tappable this attempt
        {
            const std::size_t idx = static_cast<std::size_t>(&p - state.battlefield.data());
            if (idx < 64 && (reserved_mask & (1ull << idx))) { return false; }
        }
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                   || def.params.mana_rock
                   || PaySacSpendableNow(state, p, def);   // §2a: a Treasure pays like a land, then dies (fresh-hold aware)
        if (!is_src) { return false; }
        if (!RestrictedManaUsable(def.params, for_creature, 1)) { return false; }
        if (!StorageSourceLive(p, def)) { return false; }   // uncharged storage land makes no mana
        if (!GraveyardFuelLive(state, active, def)) { return false; }   // Deathrite: no gy land = no mana
        if (!ManaSubtypeGateLive(state, active, def)) { return false; } // Arbor Elf: no Forest = no mana
        return true;
    };

    // Tap one non-filter source: THE shared mechanic (TapSourceIntoFloat above), which the
    // human pre-tap calls too so the two can never drift.
    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    { if (def.params.creature_mana_only || BigSpellOnlySource(def.params)) { restricted_in_float = true; }
      TapSourceIntoFloat(state, active, p, def, col, floating, available, for_creature); };

    // Ensure floating can satisfy one pip: `any` = generic, else specific colour
    // `needed`. Taps at most one producing source (a filter may also tap one feeder).
    // allow_ramp: may a ramp filter (Ferrous Lake) be used? false when called to FEED a
    // ramp filter's {1}, so ramp filters never feed each other (avoids recursion; the
    // unmodelled ramp->ramp chain is inert unless 2+ ramp filters are the ONLY sources).
    //
    // SELF-REFERENCE WITHOUT std::function (perf, 2026-09-11). This closure is recursive -- two
    // sites below feed a ramp filter's {1} through it -- and the only reason it was a
    // std::function was to give the lambda a name it could call itself by. That type erasure was
    // the single hottest symbol on EldraziDisplacerFlicker (perf task-clock, seed 21 gi=20: 8.66%
    // self, 22.4% inclusive), and it cost twice over: the closure captures a dozen enclosing
    // locals by reference, far past libstdc++'s 16-byte small-object buffer, so CONSTRUCTING it
    // heap-allocated and freed on EVERY TapForCostSharedOnce call (43% of the game's instructions
    // are under that function), and every call THROUGH it was an indirect call the optimiser could
    // neither inline nor specialise.
    // The Y-combinator form keeps the closure a plain lambda -- no allocation, direct calls -- by
    // passing it its own self-reference. `produce` below is the unchanged three-argument name every
    // call site already used; only the two RECURSIVE sites inside the body say `self(self, ...)`.
    // Pure mechanics: same body, same order, same result -> byte-identical.
    // See the line-float hold at the top of produce_impl. Set only for the duration of the one
    // nested attempt it makes, so the attempt cannot re-enter the hold.
    bool line_float_bypass = false;
    auto produce_impl = [&](auto& self, Color needed, bool any, bool allow_ramp) -> bool
    {
        // Snow pips skip the floating shortcut: mid-payment floating carries no snow provenance
        // (conservatively non-snow), so an {S} pip must tap a fresh snow source. Fungibility keeps
        // this sound when floating is non-empty -- the snow-produced unit enters the float and any
        // unit is consumed; a valid pip<->unit reassignment always exists.
        if (!paying_snow && !line_float_bypass)
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed))
          {
              // LINE-FLOAT HOLD (human play only; MTG_PAY_LINE_FLOAT_HOLD=0 restores). A generic pip
              // took whatever this payment had floating -- typically the SECOND mana of a two-mana
              // tap (a Karoo's {W}{W}) -- even when every floating unit is a colour the rest of the
              // HUMAN'S DECLARED LINE still owes and an untapped source of unneeded mana (Sol Ring's
              // {C}{C}) sat idle. USER 2026-10-06, WhiteKnights seed 5 T2: Plains + Sol Ring +
              // Accorder Paladin + Dauntless Bodyguard, with Remote Farm in play. The Paladin's {W}
              // tapped the Farm, its {1} ate the Farm's second {W}, Sol Ring stayed up, and the
              // Bodyguard's {W} was unpayable -- the line was dropped. Try the tap first; the
              // floating colour then survives as this payment's leftover (ConsumeFloatingAny takes
              // the new {C} first) and the next cast spends it. If no tap succeeds the float pays,
              // exactly as before. g_line_unpaid_cost is zero outside a plan apply, and the whole
              // hold is gated on HumanPlayActive(), so autonomous play is byte-identical.
              static const bool s_lfh = EnvOn("MTG_PAY_LINE_FLOAT_HOLD", true);
              if (any && s_lfh && HumanPlayActive() && floating.colorless == 0 && floating.wild == 0)
              {
                  // Colours the rest of the line still owes AFTER this cast's own pips (the shared
                  // definition; clamping at zero changes neither comparison below).
                  int later[5];
                  LineOwedAfterOwnPips(later);
                  const int fl[5] = { floating.white, floating.blue, floating.black,
                                      floating.red, floating.green };
                  bool all_needed = floating.Total() > 0;
                  for (int c = 0; c < 5 && all_needed; ++c)
                  { if (fl[c] > 0 && fl[c] > later[c]) { all_needed = false; } }
                  // An untapped source of mana nobody later in the line needs.
                  bool spare = false;
                  if (all_needed)
                  {
                      for (const Permanent& q : state.battlefield)
                      {
                          if (q.controller_index != active || q.tapped) { continue; }
                          const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
                          if (qd == nullptr) { continue; }
                          if (!q.card.IsLand() && qd->tmpl != CardTemplate::ManaDork
                              && !qd->params.mana_rock) { continue; }
                          if (!CanTapNow(q, state.battlefield)) { continue; }
                          for (Color c : EffectiveProducesFor(state, active, *qd, &q))
                          {
                              const int ci = static_cast<int>(c);
                              if (c == Color::Colorless || (ci >= 0 && ci < 5 && later[ci] <= 0))
                              { spare = true; break; }
                          }
                          if (spare) { break; }
                      }
                  }
                  if (spare)
                  {
                      line_float_bypass = true;
                      const bool tapped = self(self, needed, any, allow_ramp);
                      line_float_bypass = false;
                      if (tapped) { return true; }
                  }
              }
              return true;
          } }

        // Scarcity-first source selection (default ON; MTG_TAP_LEGACY opts OUT to the battlefield-order
        // 4-step path below, a byte-identical A/B baseline): pick the LEAST-flexible qualifying source
        // for this pip (via ManaSourceRank, lower = earlier) so rainbow sources stay up; filters rank
        // between duals and tri and are candidates only when feedable now. Ramp filters (rare) are left
        // to the legacy path / backtracker, the complete fallback either way.
        if (TapScarcityEnabled())
        {
            const int bn = static_cast<int>(state.battlefield.size());
            int best_i = -1, best_rank = 1 << 30, best_kind = 0;  // 1 direct, 2 filter-colour, 3 filter-{C}
            int best_dep = -1;   // depletion tiebreak: more counters tap FIRST (DepletionTapOrderEnabled)
            int best_hold = 1 << 30;   // provider hold value: LOWER taps first (ManaSourceHoldValue)
            // HUMAN-PLAY DEMAND TIEBREAK (MTG_HUMAN_TAP_DEMAND, default ON; =0 restores the
            // battlefield-order tie). USER 2026-09-05, Melira s1 T4: a pod activation's generic
            // {1} tapped the FOREST while Boulderloft (W) and Darkbore (B) sat untapped -- all
            // three mono lands tie on the flexibility rank, the tie fell to battlefield order,
            // and the hand's Chord ({X}{G}{G}{G}) was one green short in the human's NEXT line.
            // Among EQUAL-rank sources paying a GENERIC pip, spend the one whose colours the
            // HAND demands least: surplus(c) = untapped supply of c minus hand pips of c; a
            // source's spend-safety is its worst colour's surplus. Rank still decides first
            // (the flexibility ladder is untouched), coloured pips are untouched (they must tap
            // their colour), and it is HUMAN PLAY ONLY (the FilterCFirst precedent): a human
            // builds a phase as several lines, so this payment cannot see the next line's cost
            // the way the engine's whole-plan batch payment can. Rollouts (HumanPlaySuppress)
            // and every autonomous game keep the historical tie -- GT byte-identical.
            static const bool s_tap_demand = EnvOn("MTG_HUMAN_TAP_DEMAND", true);
            const bool hp_demand_live = s_tap_demand && any && HumanPlayActive();
            int hp_supply[5] = {0, 0, 0, 0, 0}, hp_demand[5] = {0, 0, 0, 0, 0};
            int best_sur = -(1 << 30);
            if (hp_demand_live)
            {
                for (const Permanent& q : state.battlefield)
                {
                    if (q.controller_index != active || q.tapped) { continue; }
                    const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
                    if (!qd) { continue; }
                    if (!q.card.IsLand() && qd->tmpl != CardTemplate::ManaDork
                        && !qd->params.mana_rock) { continue; }
                    int seen = 0;
                    for (Color c : EffectiveProducesFor(state, active, *qd, &q))
                    {
                        const int ci = static_cast<int>(c);
                        if (ci < 5 && !(seen & (1 << ci))) { seen |= 1 << ci; ++hp_supply[ci]; }
                    }
                }
                for (const Card& hc : state.players[active].hand)
                {
                    // Cost via the DEFINITION: a zone Card carries the name, not the cost (the
                    // hand's own m_mana_cost is empty for decklist-loaded copies).
                    const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                    if (hd == nullptr) { continue; }
                    const ManaCost& m = hd->card.m_mana_cost;   // hybrid/phyrexian pips baked flat
                    hp_demand[static_cast<int>(Color::White)] += m.white;
                    hp_demand[static_cast<int>(Color::Blue)]  += m.blue;
                    hp_demand[static_cast<int>(Color::Black)] += m.black;
                    hp_demand[static_cast<int>(Color::Red)]   += m.red;
                    hp_demand[static_cast<int>(Color::Green)] += m.green;
                }
            }
            int ff_i = -1, ff_rank = 1 << 30;   // best kind-2 filter candidate (FeedFilterFirstOn reroute)
            // Needs-based order (MTG_NEEDS_TAP_ORDER): demand/supply once per payment.
            const bool nd_live = NeedsTapOrderOn();
            if (nd_live && !nd_ready) { nd_ready = true; ComputeNeedsDemandSupply(state, active, cost, nd_D, nd_S); }
            // TEMP DIAGNOSTIC (MTG_TAPDBG, default off): the needs model's demand/supply for this
            // pip and every candidate's key, real payments only (the [tapdbg]/[paydbg] contract).
            static const bool s_nddbg = EnvOn("MTG_TAPDBG");
            const bool nddbg = s_nddbg && nd_live && g_real_resolution;
            if (nddbg && reserved_mask)
            {
                std::fprintf(stderr, "[nddbg] reserved:");
                for (std::size_t ri = 0; ri < state.battlefield.size() && ri < 64; ++ri)
                {
                    if (!(reserved_mask & (1ull << ri))) { continue; }
                    const CardDefinition* rd = state.battlefield[ri].def_absent ? nullptr
                        : CardDatabase::Instance().LookupCached(state.battlefield[ri].card);
                    std::fprintf(stderr, " %s#%d", rd ? rd->card.m_name.str().c_str() : "?",
                                 static_cast<int>(state.battlefield[ri].card.m_number));
                }
                std::fprintf(stderr, "\n");
            }
            if (nddbg)
            { std::fprintf(stderr, "[nddbg] pip=%s D=[%d %d %d %d %d %d] S=[%d %d %d %d %d %d]\n",
                           any ? "any" : std::to_string(static_cast<int>(needed)).c_str(),
                           nd_D[0], nd_D[1], nd_D[2], nd_D[3], nd_D[4], nd_D[5],
                           nd_S[0], nd_S[1], nd_S[2], nd_S[3], nd_S[4], nd_S[5]); }
            // The comparator's lexicographic key (see the block at the comparator): plain-land
            // tiers compare (0, harm, rank, hold); every other tier (reserves, the creature band,
            // the negative pins) compares (rank, hold, harm, 0). Then depletion, then demand surplus.
            int best_key[4] = { 1 << 30, 1 << 30, 1 << 30, 1 << 30 };
            // FILTER DEFERRAL (needs lever only). A filter's modes -- the fed conversion (kind 2)
            // and the free {C} (kind 3) -- are NOT needs-ordered against the direct sources: the
            // needs model prices access to colours and a filter neither adds supply nor spends it
            // in a way that model can see (its fed mode consumes a feeder; its {C} mode forfeits a
            // conversion). Both directions were measured wrong: charged 0, the fed mode jumped a
            // harm-carrying dual (th d0 gi442: Bluffs+Vents paid {1}{U}, the {1}{R}{R} payload lost
            // its conversion, 5 -> unwon); charged its outputs, Capital City was held while the
            // duals paid (Fluctuator s10 T3, the user's 09-05 defect). So the best filter is kept
            // aside and compared ONCE, after the loop, against the best direct source AT THAT
            // SOURCE'S HARM -- i.e. by the ladder rank, dep and sur alone, exactly the historical
            // filter-vs-direct order -- while the direct sources are needs-ordered among themselves.
            int best_f = -1, best_f_kind = 0, best_f_rank = 1 << 30, best_f_dep = -1, best_f_sur = -(1 << 30);
            // HOISTED out of the per-permanent loop below: SacPayFodderRank is called once per
            // candidate body, and resolving the doomed-token creators inside it made the whole pass
            // O(board^2) -- measured at +48% CPU on Fungus candidate B before this hoist. Empty
            // whenever MTG_SAC_VICTIM_DOOMED is off, so the default path does not even scan.
            const std::vector<int> pay_doomed = DoomedTokenCreators(state);
            for (int i = 0; i < bn; ++i)
            {
                Permanent& p = state.battlefield[i];
                if (p.controller_index != active) { continue; }
                // A TAPPED permanent is never a TAP source -- but §2b can still EAT it: sacrificing
                // is not tapping, and "attack, then sac the attackers for mana" is the Skirk line
                // this deck is built on. With the lever off (or no outlet on the board) sac_outlet
                // is invalid and this is the historical short-circuit, LookupCached included.
                if (p.tapped && !sac_outlet.valid()) { continue; }
                // TWO pointers, deliberately. `real_def` is what this permanent actually is;
                // `def` is what it is FOR MANA, which under Brightcap Badger's grant may be the
                // shared synthetic tap-for-{G} face (a Saproling token has no definition at all,
                // and a Thallid has one with no mana ability).
                //
                // The §2b sacrifice-fodder branch below MUST read `real_def`: the synthetic face is
                // not a creature and carries none of the card's params, so feeding it there would
                // silently stop a tapped Fungus being legal fodder for Utopia Mycon. Keeping the
                // two apart is also what keeps §2b's own token blindness a SEPARATE defect with its
                // own measurement, rather than something this change quietly half-fixes.
                const CardDefinition* real_def = p.def_absent
                                               ? nullptr
                                               : CardDatabase::Instance().LookupCached(p.card);
                const CardDefinition* def = real_def;
                if (def == nullptr || !IsBaselineManaSource(*def))
                {
                    if (!p.tapped && GrantReaches(mana_grant, p)
                        && GrantedBodyCanTap(mana_grant, p, state.battlefield))
                    { def = &GrantedManaFace(mana_grant.color); }
                }
                // DO NOT BAIL ON A MISSING DEFINITION HERE. TAPPING needs one (the produces list,
                // the filter params); EATING does not -- the outlet is the permission and the body is
                // the resource. A Saproling token has no CardDefinition at all, so the old
                // `def == nullptr -> continue` (and the `real_def != nullptr` guard below) skipped
                // exactly the fodder §2b exists to spend, which is why MTG_SAC_OUTLET_PAY measured
                // cheaper AND worse: the searched actions were deleted and nothing replaced them.
                const bool tap_ok = def != nullptr && !p.tapped && usable(p, *def);
                const bool fodder = !tap_ok && fodder_ok(p, real_def);
                if (!tap_ok && !fodder) { continue; }
                int kind = 0;
                if (fodder)
                {
                    // §2b: the colours come from the OUTLET, not from the fodder's own (empty)
                    // produces -- the body is the resource, the outlet is the permission.
                    const std::vector<Color>& prod = SacPayOutletColors(sac_outlet.def->params);
                    bool makes = false;
                    if (any) { makes = !prod.empty(); }
                    else { for (Color c : prod) { if (c == needed) { makes = true; break; } } }
                    if (!makes) { continue; }
                    kind = 5;
                }
                else if (def->params.is_filter)
                {
                    if (any || needed == Color::Colorless) { kind = 3; }   // {C} mode covers generic/{C}
                    else
                    {
                        bool makes = false;
                        for (Color c : def->params.produces) { if (c == needed) { makes = true; break; } }
                        bool feedable = false;
                        if (makes)
                        {
                            for (Color c : def->params.produces)
                            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { feedable = true; break; } }
                            if (!feedable) { feedable = HasUntappedNonFilterSourceProducing(state, def->params.produces); }
                        }
                        if (makes && feedable) { kind = 2; } else { continue; }
                    }
                }
                else if (def->params.ramp_filter) { continue; }
                else if (def->params.any_color_filter)
                {
                    // Capital City. Its FREE "{T}: Add {C}" mode is the fast, common case and is
                    // exactly a kind-3 filter tap. Its FED mode (spend {1}, take one of any
                    // colour) is left to the backtracker, as ramp_filter's is: reaching a colour
                    // costs a second source, which is a joint decision the per-pip greedy cannot
                    // make. So the greedy pays generic pips from it and simply declines a coloured
                    // one -- a failure that falls through to the complete solver, never a wrong tap.
                    // Astrolabe (filter_no_free_colorless): NO free mode at all -- the greedy can
                    // never tap it; only the backtracker's fed branch may.
                    if (def->params.filter_no_free_colorless) { continue; }
                    if (any || needed == Color::Colorless) { kind = 3; } else { continue; }
                }
                else
                {
                    // Untap-land burst (Wirewood Lodge): with a TAPPED 2+ scaled Elf up and a feed
                    // mana ({G}) already floating, one Lodge tap is worth the Elf's full yield for
                    // one floating feed -- net (yield - 1), the net-cancellation model of "pay {G},
                    // untap it, tap it again" (UntapBurstBestYield). Offered for the feed colour or
                    // a generic pip; a strict {C} pip keeps the plain mode (G cannot pay it). The
                    // backtracker fallback completes the orderings the greedy cannot stage here
                    // (no feed floating yet, target not tapped yet).
                    if (def->params.untap_creature_cost.has_value()
                        && (any || needed != Color::Colorless))
                    {
                        const std::optional<Color> feed = UntapBurstFeedColor(*def);
                        if (feed.has_value() && (any || needed == *feed)
                            && UntapBurstBestYield(state, active, *def, /*require_tapped=*/true) >= 2)
                        {
                            ManaPool pr = floating;
                            if (ConsumeFloating(pr, *feed)) { kind = 4; }
                        }
                    }
                    if (kind == 0)
                    {
                    // Payment-legal produces (see pay_produces above): a colored_creature_only land
                    // is NOT selected for a coloured pip on a non-creature spell (but still pays a
                    // generic pip as {C}).
                    const std::vector<Color>& prod = pay_produces(*def, &p);
                    bool makes = false;
                    if (any) { makes = !prod.empty(); }
                    else { for (Color c : prod) { if (c == needed) { makes = true; break; } } }
                    if (!makes) { continue; }
                    kind = 1;
                    }
                }
                // §2b fodder is ranked by SacPayFodderRank, whose base 500 sits behind every real
                // source on the board -- the user's "highest level of deferral", expressed as a
                // tap-order rank rather than as a rule anyone has to enforce.
                int rank = (kind == 5) ? SacPayFodderRank(state, p, sac_outlet, &pay_doomed)
                                       : ResolveProvider(state).ManaSourceRank(state, *def);
                // Filter {C} mode on a generic/{C} pip: least flexible mana on the board, so it
                // spends just after a true {C}-only source and BEFORE any coloured land -- see
                // FilterCFirstEnabled (treasure_hunt s11: the old rank-25 read tapped the real
                // dual and stranded a feeder-less Cascade Bluffs as the last source up).
                // HUMAN PLAY ONLY (the human-line-vs-AI-average rule): measured on the suite the
                // unconditional tier was WORSE everywhere it moved (th +0.015 / hinata +0.011
                // summed on regression, zero cells faster) -- the dual-first spend preserves the
                // filter's CONVERSION for the turn's later casts, which the search exploits and a
                // per-pip flexibility argument cannot see. Rollouts (HumanPlaySuppress) keep the
                // measured order, so autonomous play and every GT stay byte-identical.
                // is_filter ONLY: for an any_color_filter the {C} mode is not "the least flexible
                // mana on the board", it is the mode that DESTROYS the land's conversion, so it
                // must keep the late rank-25 tier rather than spend first.
                if (kind == 3 && !def->params.any_color_filter
                    && FilterCFirstEnabled() && HumanPlayActive()) { rank = 6; }
                // SAC-FODDER PAYS FIRST (MTG_SAC_FODDER_PAYS; see SacFodderPaysEnabled in
                // SpellEffects.h for the st993 trace): the exact creature this payment's cast is
                // about to sacrifice pays before everything -- its body is already spent, so its
                // mana is the one truly free source on the board.
                if (g_pay_sac_victim != 0 && p.card.m_number == g_pay_sac_victim) { rank = -1000; }
                // CREATURE-ONLY MANA PAYS A CREATURE SPELL FIRST (Bruna sweep G, seed 77019 T4).
                // Somberwald Sage's three units can pay nothing but creature spells, so spending
                // them on this creature and leaving GENERAL sources up is never worse for the rest
                // of the turn -- the combat Arcanum Wings swap, main 2's noncreature casts and
                // activations can only use the general ones. Ranked last before, the greedy tapped
                // five lands/dorks for Bruna's {3}{W}{W}{U} and then the Sage for the final generic
                // pip: eight mana for a six-cost spell, two of them stranded as creature-only float.
                // Shared payer -> executor and rollout alike. No creature_mana_only source on the
                // board -> unreachable (byte-identical).
                //
                // EXCEPT A SOURCE WHOSE TAP COSTS AN ATTACK (2026-10-05 suite verdict). "Never worse
                // for the rest of the turn" holds only for a source with no OTHER use this turn. A
                // creature that could swing (CanAttackFull + power > 0 -- SacPayFodderCostsAttack,
                // the same test AvailableManaPoolNoAttackers and the fodder rank use) has one: a
                // pre-combat tap means it cannot be declared as an attacker, and vigilance does not
                // help (vigilance skips the attack TAP; it does not let a tapped creature attack).
                // Angels: Giada, Font of Hope (2/2 flying vigilance, Angel-only {W}) paid Righteous
                // Valkyrie first on T3 and sat out combat -- 146 slower regression games, +144 turns,
                // every one back to its GT turn with the rank off. Such a source keeps its provider
                // rank (the creature band, behind the lands), so it pays only when needed, exactly
                // as before G. Somberwald Sage (0/1) and Ancient Ziggurat (a land) cost no attack
                // and keep the creature-first rank.
                if (for_creature && def->params.creature_mana_only && kind == 1
                    && CreatureOnlyFirstEnabled()
                    && !(def->card.IsCreature() && SacPayFodderCostsAttack(state, p))) { rank = -500; }
                // BIG-SPELL-ONLY MANA PAYS A QUALIFYING SPELL FIRST (Troyan, Gutsy Explorer) -- the same
                // argument: usable() admits it only for a spell of mana value 5+ (or with {X}), it can
                // pay nothing else this turn, so spending it here and leaving the GENERAL sources up is
                // never worse -- with the same attack exception (a Troyan that could swing keeps its
                // provider rank). No such source on the board -> unreachable (byte-identical).
                if (kind == 1 && BigSpellOnlySource(def->params)
                    && !(def->card.IsCreature() && SacPayFodderCostsAttack(state, p))) { rank = -500; }
                // Reference-replay tap preference (--tap-pref; nulled by RevealLogPause -> real
                // payments only): a source the RECORDING tapped in this same (turn, phase)
                // outranks every unpinned source. Order bias only -- never legality; among
                // pinned sources the normal rank still decides.
                if (g_play_tap_pref_chooser && (*g_play_tap_pref_chooser)(state, p)) { rank -= 100000; }
                // Within-rank depletion tiebreak: among equal-rank depletion lands, spend the one
                // with MORE counters first (preserves per-turn burst -- see DepletionTapOrderEnabled).
                // Non-depletion sources all read 0, so equal-rank plain sources keep the historical
                // first-in-battlefield-order winner (byte-identical for every depletion-less board).
                const int dep = DepletionTapOrderEnabled() ? DepletionCountersOn(p) : 0;
                // Demand tiebreak (human play, generic pip -- see the header note above): higher
                // surplus = safer to spend. Direct sources only; the filter/burst kinds keep
                // their own tiers. Colourless-only reads maximally safe (its mana pays nothing
                // but generic anyway; rank 5 means it rarely reaches a tie).
                int sur = 0;
                if (hp_demand_live && kind == 1)
                {
                    int worst = 1 << 20; bool has_col = false;
                    for (Color c : pay_produces(*def, &p))
                    {
                        const int ci = static_cast<int>(c);
                        if (ci >= 5) { continue; }
                        has_col = true;
                        worst = std::min(worst, hp_supply[ci] - hp_demand[ci]);
                    }
                    sur = has_col ? worst : (1 << 20);
                }
                // Provider HOLD VALUE (ManaSourceHoldValue): among equal-rank candidates, the
                // one whose body is worth LESS this turn taps first -- the attack-turn creature
                // order (MTG_ATTACK_BODY_TAP_ORDER). Sits between the rank and the depletion /
                // demand tiebreaks: a creature and a depletion land never share a rank, and the
                // user's ruling puts colour flexibility AFTER attack value ("colour flexibility
                // only as a needs-based tiebreak"). 0 everywhere the lever is off -> the
                // comparator below is the historical one, byte-identical.
                const int hold = (kind == 1) ? ResolveProvider(state).ManaSourceHoldValue(state, p, *def) : 0;
                // Needs-based HARM (MTG_NEEDS_TAP_ORDER; ComputeNeedsDemandSupply): the unmet
                // demand this tap would create, over the colours the source produces. A direct
                // source's payment-legal colours; a filter's conversion outputs (tapping it for its
                // free {C} spends the conversion for the turn). 0 with the lever off.
                // A filter's {C} mode (kind 3) is charged its {C} only -- its conversion outputs
                // are not supply (see ComputeNeedsDemandSupply) -- and its fed coloured mode
                // (kind 2) is charged nothing here: the feeder it consumes is chosen by its own
                // scarcity pass, which carries that feeder's harm.
                int harm = 0;
                if (nd_live && (kind == 1 || kind == 3))
                {
                    static const std::vector<Color> kOnlyC{ Color::Colorless };
                    const std::vector<Color>& hp = (kind == 1) ? pay_produces(*def, &p) : kOnlyC;
                    int seen = 0;
                    for (Color c : hp)
                    {
                        const int ci = static_cast<int>(c);
                        if (ci > 5 || (seen & (1 << ci))) { continue; }
                        seen |= (1 << ci);
                        const int sf_now  = std::max(0, nd_D[ci] - nd_S[ci]);
                        const int sf_less = std::max(0, nd_D[ci] - (nd_S[ci] - kNdScale));
                        harm += sf_less - sf_now;
                    }
                    // PER MANA THIS PAYMENT CAN USE: a bounce land's one tap pays two pips (Izzet
                    // Boilerworks {U}{R}), so the colours it spends are spread over twice the mana
                    // and its tap saves another source. Charging it per colour tapped an Island
                    // instead and floated the bounce land's second mana (hinata d0 gi229: 4 lands
                    // for a 4-pip cast, the follow-up 2-drop lost, T7 -> T8). But only the units
                    // the REST OF THIS PAYMENT can consume count: per mana PRODUCED, Sandstone
                    // Needle's {R}{R} at half harm won a lone {R} pip over a Mountain, its second
                    // mana floated away and a depletion counter went with it (dragonstorm d0
                    // gi168/438/560, mirrorwing d0 gi579). The extra units can pay the generic
                    // pips still owed and the coloured pips of the colours the source yields.
                    {
                        const int yield = ManaProducedPerTap(*def);
                        int usable = 1;
                        if (yield > 1)
                        {
                            int rem[7];
                            for (int k = 0; k < 7; ++k) { rem[k] = nd_left[k]; }
                            { const int slot = any ? 6 : static_cast<int>(needed);
                              if (slot >= 0 && slot < 7 && rem[slot] > 0) { --rem[slot]; } }
                            int payable = rem[6], seen2 = 0;
                            for (Color c : hp)
                            {
                                const int ci = static_cast<int>(c);
                                if (ci < 0 || ci > 5 || (seen2 & (1 << ci))) { continue; }
                                seen2 |= (1 << ci);
                                payable += rem[ci];
                            }
                            usable = 1 + std::min(yield - 1, payable);
                        }
                        harm /= usable;
                    }
                    // A DRIP land's coloured tap gifts the opponent a life, priced in harm's own
                    // unit (kNdScale = one colour fully short): antilife d0 gi124, Grove paid {G}
                    // over Temple Garden because W was short, T5 -> T6. Static like the ladder's
                    // nudge (the Remedy-live drip is forced separately by TapDripLandsIfUseful,
                    // never by tap order). A DEPLETION land is NOT priced here: round 1 charged it
                    // half a shortfall and that outranked the ladder, so a dual paid ahead of the
                    // two-mana depletion land and the dual's other colour was stranded for the
                    // follow-up cast (th d0 gi50: Temple paid {1}{U} over Skerry, Land's Edge
                    // {1}{R}{R} uncastable, T4 -> T5; gi218, gi687 the same shape with Needle).
                    // Its finite counter stays the ladder's +1 nudge and the dep tiebreak, both of
                    // which still apply INSIDE equal harm.
                    if (def->params.tap_opponent_lifegain > 0) { harm += kNdScale; }
                }
                // Key order. PLAIN-LAND tiers (0 <= rank < 60: the flexibility ladder, the filter
                // and one-shot tiers, their nudges) compare NEEDS first, then the ladder: that is
                // what makes the order needs-based rather than flexibility-based. Every other
                // tier -- the reserves (60-63), the creature band (64+), §2b fodder (500+) and the
                // NEGATIVE pins (tap-pref, sac victim) -- keeps its absolute rank, so a creature
                // never jumps a land and a pinned source always pays first; inside one of those
                // tiers the body's hold value comes before needs (USER: colour flexibility "only
                // as a needs-based tiebreak" among attackers). With both levers off every key is
                // (0, 0, rank, 0) or (rank, 0, 0, 0) -> the historical rank comparator exactly.
                if (nd_live && (kind == 2 || kind == 3) && rank >= 0 && rank < 60)
                {
                    // Deferred (see best_f above): historical order among the filters themselves.
                    if (rank < best_f_rank
                        || (rank == best_f_rank
                            && (dep > best_f_dep || (dep == best_f_dep && sur > best_f_sur))))
                    { best_f = i; best_f_kind = kind; best_f_rank = rank; best_f_dep = dep; best_f_sur = sur; }
                    if (kind == 2 && FeedFilterFirstOn() && rank < ff_rank) { ff_rank = rank; ff_i = i; }
                    if (nddbg)
                    { std::fprintf(stderr, "[nddbg]   filter %s kind=%d rank=%d dep=%d sur=%d (deferred)\n",
                                   def->card.m_name.str().c_str(), kind, rank, dep, sur); }
                    continue;
                }
                if (nddbg)
                { std::fprintf(stderr, "[nddbg]   cand %s kind=%d rank=%d harm=%d hold=%d dep=%d sur=%d\n",
                               def->card.m_name.str().c_str(), kind, rank, harm, hold, dep, sur); }
                // "FIRST HAND, THEN DECK" (USER): harm's WHOLE pips (certain demand: the hand, the
                // board, a pump sink) outrank the ladder; its FRACTION (the library reveal's
                // expectation, pips / library size) only breaks a rank tie. Compared raw, a 2/64
                // vs 4/64 reveal fraction overrode the ladder and tapped Sandstone Needle's LAST
                // counter for a lone generic pip over an Island (th d3/d5 s3003 gi69, T5 -> T6).
                // hold is 0 for every plain-tier source (ManaSourceHoldValue is creature-only, and
                // creatures sit in the 64+ band), so the plain key's fourth slot is free for it.
                int key[4];
                if (kind != 5 && rank >= 0 && rank < 60)
                { key[0] = 0;    key[1] = harm / kNdScale; key[2] = rank; key[3] = harm % kNdScale; }
                else                                     { key[0] = rank; key[1] = hold; key[2] = harm; key[3] = 0;    }
                int cmp = 0;
                for (int k = 0; k < 4 && cmp == 0; ++k) { cmp = (key[k] < best_key[k]) ? -1 : (key[k] > best_key[k] ? 1 : 0); }
                const bool better = cmp < 0
                    || (cmp == 0 && (dep > best_dep || (dep == best_dep && sur > best_sur)));
                if (better)
                { best_rank = rank; best_i = i; best_kind = kind; best_dep = dep; best_sur = sur; best_hold = hold;
                  for (int k = 0; k < 4; ++k) { best_key[k] = key[k]; } }
                if (kind == 2 && FeedFilterFirstOn() && rank < ff_rank) { ff_rank = rank; ff_i = i; }
            }
            if (best_f >= 0)
            {
                // The deferred filter competes at the best direct source's own harm (its key[1] and key[3]),
                // so only the ladder rank, dep and sur decide between them -- the historical order.
                const int fkey[4] = { 0, best_i >= 0 ? best_key[1] : 0, best_f_rank, best_i >= 0 ? best_key[3] : 0 };
                int cmp = 0;
                for (int k = 0; k < 4 && cmp == 0; ++k) { cmp = (fkey[k] < best_key[k]) ? -1 : (fkey[k] > best_key[k] ? 1 : 0); }
                const bool better = best_i < 0 || cmp < 0
                    || (cmp == 0 && (best_f_dep > best_dep || (best_f_dep == best_dep && best_f_sur > best_sur)));
                if (better)
                { best_i = best_f; best_kind = best_f_kind; best_rank = best_f_rank; best_dep = best_f_dep;
                  best_sur = best_f_sur; best_hold = 0; for (int k = 0; k < 4; ++k) { best_key[k] = fkey[k]; } }
            }
            if (best_i < 0) { return false; }
            if (nddbg)
            { std::fprintf(stderr, "[nddbg]   -> %s kind=%d rank=%d key=(%d %d %d %d)\n",
                           CardDatabase::Instance().LookupCached(state.battlefield[best_i].card)->card.m_name.str().c_str(),
                           best_kind, best_rank, best_key[0], best_key[1], best_key[2], best_key[3]); }
            // Needs-based order: the chosen source leaves the supply for this payment's later pips.
            if (nd_live && nd_ready && (best_kind == 1 || best_kind == 3))
            {
                const Permanent& bp = state.battlefield[best_i];
                const CardDefinition* bd = CardDatabase::Instance().LookupCached(bp.card);
                if (bd != nullptr)
                {
                    static const std::vector<Color> kOnlyC{ Color::Colorless };
                    const std::vector<Color>& hp = (best_kind == 1) ? pay_produces(*bd, &bp) : kOnlyC;
                    int seen = 0;
                    for (Color c : hp)
                    {
                        const int ci = static_cast<int>(c);
                        if (ci > 5 || (seen & (1 << ci))) { continue; }
                        seen |= (1 << ci);
                        nd_S[ci] -= kNdScale;
                    }
                }
            }
            // Feed-aware reroute (MTG_FEED_FILTER_FIRST; see FeedFilterFirstOn in SpellEffects.h):
            // when the chosen DIRECT source is the LAST untapped non-filter source producing any of
            // the best filter candidate's colours, and none of those colours float, tapping it
            // directly strands the filter's fed mode for the rest of the turn. Route the SAME
            // source through the filter instead (the kind-2 path's feeder loop will pick it -- it
            // is the only feeder): same source spent, one MORE unit floated (the filter's dead {C}
            // option converted into a live coloured unit), and the turn's later casts keep their
            // feed. Preference only -- candidates and the complete backtracker fallback unchanged.
            if (best_kind == 1 && ff_i >= 0 && ff_i != best_i)
            {
                const CardDefinition* fd = CardDatabase::Instance().LookupCached(state.battlefield[ff_i].card);
                bool feed_floats = false;
                for (Color c : fd->params.produces)
                { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { feed_floats = true; break; } }
                if (!feed_floats)
                {
                    bool chosen_feeds = false; int feeders = 0;
                    for (int i = 0; i < bn; ++i)
                    {
                        const Permanent& s = state.battlefield[i];
                        if (s.controller_index != active || s.tapped) { continue; }
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                        if (!sd || IsManaConversionSource(sd->params) || !usable(s, *sd)) { continue; }
                        if (sd->params.creature_mana_only || BigSpellOnlySource(sd->params)) { continue; }   // never a filter's feed
                        bool m = false;
                        for (Color pc : EffectiveProducesFor(state, active, *sd, &s))
                        { for (Color ic : fd->params.produces) { if (pc == ic) { m = true; break; } } if (m) { break; } }
                        if (!m) { continue; }
                        ++feeders;
                        if (i == best_i) { chosen_feeds = true; }
                    }
                    // FILTER-CHAIN escape: another untapped filter-class source G whose output
                    // includes one of F's colours, and which is itself still feedable once the
                    // chosen source is spent, can re-feed F later -- so F is NOT stranded and the
                    // reroute must not fire (th d0 gi625 T5: routing the last non-filter U/R
                    // through the Bluffs consumed the Bluffs a later Land's Edge needed; Ferrous
                    // Lake, fed by a Reliquary Tower {C}, could have fed the Bluffs instead --
                    // the backtracker chains filters, so the greedy's strand test must too).
                    bool chain_feed = false;
                    for (int i = 0; i < bn && !chain_feed; ++i)
                    {
                        if (i == ff_i || i == best_i) { continue; }
                        const Permanent& g = state.battlefield[i];
                        if (g.controller_index != active || g.tapped) { continue; }
                        const CardDefinition* gd = CardDatabase::Instance().LookupCached(g.card);
                        if (!gd || !IsManaConversionSource(gd->params) || !usable(g, *gd)) { continue; }
                        bool makes_f = false;
                        for (Color gc : gd->params.produces)
                        { for (Color ic : fd->params.produces) { if (gc == ic) { makes_f = true; break; } } if (makes_f) { break; } }
                        if (!makes_f) { continue; }
                        // G's own feed: a ramp filter eats a GENERIC {1} (any third source pays it,
                        // a plain {C} included); an is_filter needs one of ITS colours from a third
                        // non-filter source. "Third" excludes the chosen source (spent in the
                        // no-reroute world), G itself, and F (held up for the later conversion).
                        for (int k = 0; k < bn; ++k)
                        {
                            if (k == i || k == ff_i || k == best_i) { continue; }
                            const Permanent& t = state.battlefield[k];
                            if (t.controller_index != active || t.tapped) { continue; }
                            const CardDefinition* td = CardDatabase::Instance().LookupCached(t.card);
                            if (!td || IsManaConversionSource(td->params) || !usable(t, *td)) { continue; }
                            if (gd->params.ramp_filter) { chain_feed = true; break; }
                            bool m = false;
                            for (Color pc : EffectiveProducesFor(state, active, *td, &t))
                            { for (Color gc : gd->params.produces) { if (pc == gc) { m = true; break; } } if (m) { break; } }
                            if (m) { chain_feed = true; break; }
                        }
                    }
                    if (chosen_feeds && feeders == 1 && !chain_feed) { best_i = ff_i; best_kind = 2; }
                }
            }
            Permanent& bp = state.battlefield[best_i];
            // MUST use the same resolver the SELECTION loop above used. This site re-derives the
            // definition for the chosen source, and a raw LookupCached returns null for a granted
            // Saproling token -- which the selection loop can now legitimately pick, so the bare
            // `*bdef` below would dereference null. One rule, two readers: they have to agree.
            const CardDefinition* bdef = ManaDefOf(state, bp, mana_grant);
            if (best_kind == 5)
            {
                // §2b: EAT one body through the outlet. Marked, not erased -- erasing mid-payment
                // invalidates the `Permanent&`s this loop holds and the reserved-mask indices it
                // derives from `&p - battlefield.data()`, so CommitPaySacSacrifices performs the
                // real sacrifice (graveyard + death triggers) on each success return. Exactly the
                // §2a Treasure contract, and a failed attempt restores the mark from bf_pre.
                const CardParams& op = sac_outlet.def->params;
                const std::vector<Color>& prod = SacPayOutletColors(op);
                // A generic pip takes a colour THIS LINE still owes rather than decklist order --
                // the same LineDemandAnyPipColor the direct-source tap uses, so a rainbow outlet
                // does not float a colour the rest of the turn cannot spend.
                const Color col = any ? LineDemandAnyPipColor(state, prod, floating, prod[0])
                                      : needed;
                const int amt = std::max(1, op.sac_outlet_add_mana_amount);
                bp.pay_sac_eaten = true;
                floating.Add(col, amt);
                if (available)
                {
                    // Lockstep with AddSacPayFodderToPool's credit: rainbow supply was banked as
                    // `wild`, a pinned outlet's as its letter.
                    if (prod.size() > 1) { available->wild = std::max(0, available->wild - amt); }
                    else                 { available->Add(col, -amt); }
                }
                return true;
            }
            if (best_kind == 1)
            {
                // {C}-only for a non-creature colored_creature_only land -> the generic tap uses {C}
                // (prod[0]) rather than a colour that could leak to pay a coloured pip.
                // LineDemandAnyPipColor (human play, default ON) replaces prod[0] -- decklist order --
                // with a colour the REST OF THIS LINE still owes; DripLandAnyPipColor still has the
                // last word, so a painland keeps its painless {C}. See its header (EDF seed 6 T4).
                const std::vector<Color>& prod = pay_produces(*bdef, &bp);
                const Color gen_pick = LineDemandAnyPipColor(state, prod, floating, prod[0]);
                tap_source(bp, *bdef, any ? DripLandAnyPipColor(state, active, *bdef, gen_pick) : needed);
                return true;
            }
            if (best_kind == 3)
            {
                bp.tapped = true;
                dmgev::MarkLandTap(state, bp);   // Manabarbs: a land tapped for mana (armed only)
                floating.Add(Color::Colorless, 1);
                if (available)
                {
                    if (available->colorless > 0)  { --available->colorless; }
                    else if (available->wild > 0)  { --available->wild; }
                }
                return true;
            }
            if (best_kind == 4)
            {
                // Untap-land burst: tap the Lodge, spend one floating feed mana, credit the best
                // tapped scaled Elf's full yield (the Elf's tapped state is unchanged -- see the
                // selection comment above). `available` was credited the NET (UntapLandBurstNet in
                // AddSourceToPool), so consume the net here to stay in lockstep with the pool.
                const std::optional<Color> feed = UntapBurstFeedColor(*bdef);
                const int by = UntapBurstBestYield(state, active, *bdef, /*require_tapped=*/true);
                bp.tapped = true;
                dmgev::MarkLandTap(state, bp);   // Manabarbs: a land tapped for mana (armed only)
                ConsumeFloating(floating, *feed);
                floating.Add(*feed, by);
                if (available) { available->Add(*feed, -(by - 1)); }
                return true;
            }
            // kind 2: filter coloured mode -- feed one of its colours (least-flexible feeder), yield 2.
            const Color out = needed;
            bool have_input = false;
            // Creature-only units in the float may not feed the filter (see `feeding` above).
            if (!restricted_in_float)
            for (Color c : bdef->params.produces)
            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { have_input = true; break; } }
            if (!have_input)
            {
                int fi = -1, frank = 1 << 30, fdep = -1; Color fcol = Color::Colorless;
                for (int i = 0; i < bn; ++i)
                {
                    Permanent& s = state.battlefield[i];
                    if (s.controller_index != active || s.tapped) { continue; }
                    const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                    if (!sd || IsManaConversionSource(sd->params) || !usable(s, *sd)) { continue; }
                    if (sd->params.creature_mana_only || BigSpellOnlySource(sd->params)) { continue; }   // never a filter's feed
                    bool m = false; Color match = Color::Colorless;
                    for (Color pc : EffectiveProducesFor(state, active, *sd, &s))
                    { for (Color ic : bdef->params.produces) { if (pc == ic) { m = true; match = ic; break; } } if (m) { break; } }
                    if (!m) { continue; }
                    int r = ResolveProvider(state).ManaSourceRank(state, *sd);
                    // Sac-fodder-first, same bias as the direct-source loop above.
                    if (g_pay_sac_victim != 0 && s.card.m_number == g_pay_sac_victim) { r = -1000; }
                    // Same depletion tiebreak as the direct-source loop above.
                    const int sdep = DepletionTapOrderEnabled() ? DepletionCountersOn(s) : 0;
                    if (r < frank || (r == frank && sdep > fdep))
                    { frank = r; fi = i; fcol = match; fdep = sdep; }
                }
                if (fi < 0) { return false; }
                Permanent& fs = state.battlefield[fi];
                tap_source(fs, *CardDatabase::Instance().LookupCached(fs.card), fcol);
            }
            for (Color c : bdef->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            bp.tapped = true;
            dmgev::MarkLandTap(state, bp);   // Manabarbs: a land tapped for mana (armed only)
            floating.Add(out, 2);
            if (available && available->wild > 0) { --available->wild; }
            return true;
        }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || IsManaConversionSource(def->params) || !usable(p, *def)) { continue; }
            // ProducesForPayment (RP-aware; identity for every non-colored_creature_only source).
            // NOTE: the pre-unification executor read EffectiveProduces here -- the unfixed twin of
            // the 6bb2791 coloured-pip fix, reachable only under MTG_TAP_LEGACY (see ManaPayment.h).
            const std::vector<Color>& prod = ProducesForPayment(state, active, *def, for_creature, &p);
            Color col;
            if (any)
            {
                if (prod.empty()) { continue; }
                // Same line-demand pick as the scarcity path above (human play, default ON), so the
                // MTG_TAP_LEGACY baseline and the shipped path cannot disagree about which colour a
                // choice source offers a generic pip. Grove {C} mode for generic still has last word.
                col = DripLandAnyPipColor(state, active, *def,
                                          LineDemandAnyPipColor(state, prod, floating, prod[0]));
            }
            else
            {
                bool match = false;
                for (Color c : prod) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                col = needed;
            }
            tap_source(p, *def, col);
            return true;
        }

        // 2) Filter land colourless mode ({T}: Add {C}) -- for a generic or {C} pip.
        //    any_color_filter (Capital City) has the same free mode and belongs here too.
        if (any || needed == Color::Colorless)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !(def->params.is_filter || def->params.any_color_filter)
                    || def->params.filter_no_free_colorless   // Astrolabe: no free {C} mode
                    || !usable(p, *def)) { continue; }
                p.tapped = true;
                dmgev::MarkLandTap(state, p);   // Manabarbs: a land tapped for mana (armed only)
                floating.Add(Color::Colorless, 1);
                if (available)
                {
                    if (available->colorless > 0)  { --available->colorless; }
                    else if (available->wild > 0)  { --available->wild; }
                }
                return true;
            }
        }

        // 3) Filter mode for a coloured pip: feed one of the filter's colours, yield 2.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
            if (restricted_in_float) { continue; }   // its feed would consume creature-only units
            Color out;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                out = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                out = needed;
            }
            // Need one of the filter's colours floating; feed it from a non-filter source.
            bool have_input = false;
            for (Color c : def->params.produces)
            {
                ManaPool probe = floating;
                if (ConsumeFloating(probe, c)) { have_input = true; break; }
            }
            if (!have_input)
            {
                bool fed = false;
                for (Color ic : def->params.produces)
                {
                    for (Permanent& s : state.battlefield)
                    {
                        if (s.controller_index != active || s.tapped) { continue; }
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                        if (!sd || IsManaConversionSource(sd->params) || !usable(s, *sd)) { continue; }
                        if (sd->params.creature_mana_only || BigSpellOnlySource(sd->params)) { continue; }   // never a filter's feed
                        bool m = false;
                        for (Color c : EffectiveProducesFor(state, active, *sd, &s)) { if (c == ic) { m = true; break; } }  // RP feeder
                        if (!m) { continue; }
                        tap_source(s, *sd, ic);
                        fed = true; break;
                    }
                    if (fed) { break; }
                }
                if (!fed) { continue; }  // can't feed this filter; try the next one
            }
            for (Color c : def->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            p.tapped = true;
            dmgev::MarkLandTap(state, p);   // Manabarbs: a land tapped for mana (armed only)
            floating.Add(out, 2);
            if (available && available->wild > 0) { --available->wild; }  // filter counted as 1 wild in the pool
            return true;
        }

        // 4) Ramp filter (e.g. Ferrous Lake: {1},{T}: Add {U}{R}). Pay {1} generic from any
        //    other untapped source (incl. a filter's {C}), then yield one of each produces
        //    colour. No free mode; allow_ramp=false in the feed call prevents ramp chains.
        if (allow_ramp)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.ramp_filter || !usable(p, *def)) { continue; }
                if (!any)
                {
                    bool match = false;
                    for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                    if (!match) { continue; }
                }
                else if (def->params.produces.empty()) { continue; }
                // Pay the {1}: use floating if any, else feed one mana from a non-ramp source.
                if (restricted_in_float) { continue; }   // the float may hold creature-only units
                if (floating.Total() == 0)
                {
                    feeding = true;
                    const bool fed_ok = self(self, Color::Colorless, true, false);
                    feeding = false;
                    if (!fed_ok) { continue; }
                }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                dmgev::MarkLandTap(state, p);   // Manabarbs: a land tapped for mana (armed only)
                for (Color c : def->params.produces) { floating.Add(c, 1); }
                if (available && available->wild > 0) { --available->wild; }  // ramp filter counted as 1 wild
                // A land Aura on the ramp filter (Wild Growth on Skycloud Expanse): the land WAS
                // tapped for mana, so the aura's additional mana arrives here too -- exactly as
                // TapSourceIntoFloat and the backtracker's activate() credit it. This branch was
                // the one payer site without it (no deck had ever paired the two). AddSourceToPool
                // credited the same units, so retire them from `available` likewise.
                if (LandAuraBonus(state, p) > 0)
                {
                    ManaPool bonus;
                    LandAuraAddToPool(bonus, state, p);
                    floating.AddPool(bonus);
                    if (available)
                    {
                        available->white -= bonus.white; available->blue      -= bonus.blue;
                        available->black -= bonus.black; available->red       -= bonus.red;
                        available->green -= bonus.green; available->colorless -= bonus.colorless;
                        available->wild  -= bonus.wild;
                    }
                }
                return true;
            }
        }

        // 5) Any-colour filter fed mode (Capital City: {1},{T}: Add one mana of any color).
        //    Same generic {1} feed as (4) and the same allow_ramp recursion guard, but it yields
        //    ONE mana of the caller's colour -- net zero, so it is only ever worth reaching for a
        //    COLOURED pip the board cannot make directly. The generic/{C} case was already served
        //    by the free {C} mode in (2), which is strictly better (no feed), so skip `any` here.
        if (allow_ramp && !any)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.any_color_filter || !usable(p, *def)) { continue; }
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                if (restricted_in_float) { continue; }   // the float may hold creature-only units
                if (floating.Total() == 0)
                {
                    feeding = true;
                    const bool fed_ok = self(self, Color::Colorless, true, false);
                    feeding = false;
                    if (!fed_ok) { continue; }
                }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                dmgev::MarkLandTap(state, p);   // Manabarbs: a land tapped for mana (armed only)
                floating.Add(needed, 1);
                if (available && available->wild > 0) { --available->wild; }   // counted as 1 wild
                return true;
            }
        }
        return false;
    };
    // The three-argument name the call sites use; threads the self-reference for them.
    auto produce = [&](Color needed, bool any, bool allow_ramp) -> bool
    { return produce_impl(produce_impl, needed, any, allow_ramp); };

    auto pay = [&](Color needed, bool any) -> bool
    {
        payledger::t_cur_pip_any = any;     // rollback audit: what kind of pip this tap pays
        const bool produced = produce(needed, any, true);
        payledger::t_cur_pip_any = false;
        if (!produced) { return false; }
        const bool ok = any ? [&]{ Color took; return ConsumeFloatingAny(floating, took); }()
                            : ConsumeFloating(floating, needed);
        // One pip landed: the turn-scope reserves (g_pay_remaining_mv's consumers) now see one
        // fewer pip still owed by this payment.
        if (ok && g_pay_remaining_mv > 0) { --g_pay_remaining_mv; }
        if (ok) { const int slot = any ? 6 : static_cast<int>(needed); if (slot >= 0 && slot < 7 && nd_left[slot] > 0) { --nd_left[slot]; } }
        return ok;
    };

    // Greedy-first, then a backtracking fallback for filter chains the greedy strands
    // (e.g. Throes of Chaos via a Cascade Bluffs + Ferrous Lake chain). Snapshot so the
    // greedy's success path is byte-identical (no GT churn) and only previously-FAILING
    // casts gain the chain solution. See TapForCostBacktrack.
    // Warm per-thread buffers rather than fresh vectors -- see PaySnapScratch.
    PaySnapScratch<PermPaySnap> _bf_pre_scratch;
    std::vector<PermPaySnap>&   bf_pre = _bf_pre_scratch.Buf();   // compact rollback -- see PermPaySnap's header
    SnapPayFields(state.battlefield, bf_pre);
    std::vector<Permanent> bf_pre_full;       // verify mode only (MTG_PAY_SNAP_VERIFY)
    if (g_pay_snap_verify) { bf_pre_full = state.battlefield; }
    const int life_pre = state.players[active].life;
    const int opp_pre = state.players[1 - active].life;
    const bool oll_pre = state.opponent_lost_life_this_turn;
    // Deathrite: a tap may exile a graveyard land; failed attempts must put it back.
    PaySnapScratch<Card> _gy_pre_scratch;
    std::vector<Card>&   gy_pre = _gy_pre_scratch.Buf();
    gy_pre = state.players[active].graveyard;
    // Aether Hub: a coloured tap SPENDS {E} (tap_source, the same activation-cost class as the
    // Deathrite exile above) -- a failed attempt must refund it. This leaked: the aura-host
    // reserved attempt tapped the Hub coloured, failed on the remainder, and the unreserved retry
    // found a spent-out Hub with no coloured mode left, silently dropping a payable cast (USER,
    // EDF s3 T2: "Fertile Ground -> Adarkar Wastes was silently ignored" -- the {G} was the Hub's
    // energy mode; hosting the Hub itself worked only because the reservation kept it untapped
    // through the failing first attempt).
    const int energy_pre = state.players[active].energy_counters;
    // MTG_ENERGY_REFUND=0 restores the leak (one-binary isolation hatch).
    static const bool s_energy_refund = EnvOn("MTG_ENERGY_REFUND", true);
    // Retain over-produced mana (forced filter/depletion over-tap) into the turn-scoped
    // reserve so a later same-(main-)phase cast can spend it (CR 500.4). state.floating_mana
    // already holds the un-spent reserve after SpendFloatingTowardCost; add the leftover on top.
    // Off (MTG_NO_FLOAT_LEFTOVER) -> no-op.
    // MTG_FLOAT_TRACE: what a payment LEFT BEHIND, per cast. The instrument that settled the
    // keep-the-flexible-mana bug (SpellEffects.cpp's out_leftover): the useful signal is not which
    // sources tapped but which mana SURVIVED, because that is what the rest of the main phase gets
    // to spend. Covers every success path including the backtracker's, which is where the interesting
    // assignments are made -- the greedy's tap-by-tap view (below) misses them entirely.
    // Restricted to real play (AllPlayHooksNull is false only there), so the search's millions of
    // speculative payments stay silent.
    auto commit_leftover = [&](const ManaPool& lo_in)
    { ManaPool lo = lo_in;
      // CREATURE-ONLY PROVENANCE of the leftover. Mana a creature_mana_only source produced keeps
      // its restriction after the payment (Somberwald Sage tapped for Mother of Runes' {W} leaves
      // {W}{W} spendable only on creature spells). Within THIS payment every unit was usable (it is
      // a creature spell), so which units count as spent is our attribution to make -- and spending
      // the restricted ones first is never worse. So the restricted leftover is
      // min(restricted produced, leftover), routed to floating_creature_mana; the rest is general.
      // Restricted production is read off the payment's own tap diff (bf_pre), which holds for the
      // greedy and both backtracker paths alike. Inert unless a creature payment tapped such a
      // source AND left mana over.
      if (for_creature && lo.Total() > 0)
      {
          int restricted = 0;
          const int nb = static_cast<int>(std::min(bf_pre.size(), state.battlefield.size()));
          for (int bi = 0; bi < nb; ++bi)
          {
              const Permanent& bp = state.battlefield[static_cast<std::size_t>(bi)];
              if (!bp.tapped || bf_pre[static_cast<std::size_t>(bi)].tapped) { continue; }
              if (bp.controller_index != active || bp.def_absent) { continue; }
              const CardDefinition* bd = CardDatabase::Instance().LookupCached(bp.card);
              if (bd == nullptr || !bd->params.creature_mana_only) { continue; }
              const int y = PermanentManaYield(state, bp, *bd);
              restricted += (y >= 0 ? y : ManaProducedPerTap(*bd));
          }
          restricted = std::min(restricted, lo.Total());
          if (restricted > 0)
          {
              // Take the restricted units from the colour holding the MOST leftover first: a
              // one-colour burst (three of one colour) is that colour whenever the leftover is
              // mostly its own. Ambiguous only when a general source was ALSO over-tapped in the
              // same payment -- a disclosed approximation (the Bruna ledger).
              ManaPool r;
              int* src[7] = { &lo.white, &lo.blue, &lo.black, &lo.red, &lo.green, &lo.colorless, &lo.wild };
              int* dst[7] = { &r.white,  &r.blue,  &r.black,  &r.red,  &r.green,  &r.colorless,  &r.wild };
              for (int k = 0; k < restricted; ++k)
              {
                  int best = -1;
                  for (int c = 0; c < 7; ++c) { if (*src[c] > 0 && (best < 0 || *src[c] > *src[best])) { best = c; } }
                  if (best < 0) { break; }
                  --*src[best]; ++*dst[best];
              }
              if (FloatLeftoverManaEnabled()) { state.floating_creature_mana.AddPool(r); }
          }
      }
      // BIG-SPELL-ONLY PROVENANCE (Troyan, Gutsy Explorer): its {G}{U} may pay only a spell of mana
      // value 5+ (or with {X}). Within THIS payment it was legal, but a unit it over-produced must not
      // survive as GENERAL float (the laundering class of sweep finding A-i). It FLOATS in the
      // big-spell-only reserve (GameState::floating_bigspell_mana) instead -- min(big-only yield this
      // payment tapped, leftover), taken from the colours it makes (G/U) first -- so a second
      // qualifying spell this phase can spend it (CR 106.4 / 500.4: mana empties only at step end).
      // Until 2026-10-06 it was DROPPED, which made that line inexpressible. Inert unless such a
      // source was tapped AND mana was left over.
      if (lo.Total() > 0)
      {
          int big = 0;
          const CardParams* big_pp = nullptr;
          const int nb = static_cast<int>(std::min(bf_pre.size(), state.battlefield.size()));
          for (int bi = 0; bi < nb; ++bi)
          {
              const Permanent& bp = state.battlefield[static_cast<std::size_t>(bi)];
              if (!bp.tapped || bf_pre[static_cast<std::size_t>(bi)].tapped) { continue; }
              if (bp.controller_index != active || bp.def_absent) { continue; }
              const CardDefinition* bd = CardDatabase::Instance().LookupCached(bp.card);
              if (bd == nullptr || !BigSpellOnlySource(bd->params)) { continue; }
              const int y = PermanentManaYield(state, bp, *bd);
              big += (y >= 0 ? y : ManaProducedPerTap(*bd));
              big_pp = &bd->params;
          }
          big = std::min(big, lo.Total());
          ManaPool bigp;
          int* order[7] = { &lo.green, &lo.blue, &lo.wild, &lo.white, &lo.black, &lo.red, &lo.colorless };
          int* dst[7]   = { &bigp.green, &bigp.blue, &bigp.wild, &bigp.white, &bigp.black, &bigp.red, &bigp.colorless };
          for (int k = 0; k < 7 && big > 0; ++k)
          { const int t = std::min(big, *order[k]); *order[k] -= t; *dst[k] += t; big -= t; }
          if (FloatLeftoverManaEnabled() && big_pp != nullptr) { AddBigSpellFloat(state, bigp, *big_pp); }
      }
      if (FloatLeftoverManaEnabled()) { state.floating_mana.AddPool(lo); }
      // NO GENERIC MANA IN A HUMAN-PLAY POOL (see ConcretiseHumanFloat). This is the REQUEST site,
      // not necessarily the commit: while a plan is still being applied, ConcreteDeferScope drops it
      // and the commitment happens once at the decision boundary instead -- committing between the
      // casts of one line strands a later cast of that same line (FiveColour s9_gi8 T4). It is left
      // here rather than deleted because it IS the right site for any payment that runs outside a
      // plan application, and it is separately levered (MTG_HUMAN_CONCRETE_LEFTOVER).
      // No-op outside human play and for a wild-free pool.
      ConcretiseHumanFloat(state, active, ConcreteSite::Leftover);
      if (g_float_trace && !AllPlayHooksNull())
      { std::fprintf(stderr, "[float] cost=%s leftover{w%d u%d b%d r%d g%d c%d *%d} -> float{w%d u%d b%d r%d g%d c%d *%d}\n",
                     cost.ToString().c_str(), lo.white, lo.blue, lo.black, lo.red, lo.green, lo.colorless, lo.wild,
                     state.floating_mana.white, state.floating_mana.blue, state.floating_mana.black,
                     state.floating_mana.red, state.floating_mana.green, state.floating_mana.colorless,
                     state.floating_mana.wild); } };
    auto greedy = [&]() -> bool
    {
        nd_left[0] = cost.white; nd_left[1] = cost.blue;  nd_left[2] = cost.black;     nd_left[3] = cost.red;
        nd_left[4] = cost.green; nd_left[5] = cost.colorless; nd_left[6] = cost.generic;
        // Pay coloured requirements first (most restrictive), then generic.
        for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) return false; }
        for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) return false; }
        for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) return false; }
        for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) return false; }
        for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) return false; }
        for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) return false; }
        // {S} pips are baked into `generic` (ManaCost::snow_pips); under STRICT snow payment
        // (mixed manabase -- see the scope above) settle them FIRST among the generic pips,
        // restricted to snow producers, so an unrestricted pip cannot strand one. On an all-snow
        // board strict is off, n_snow = 0, and this loop is the historical plain-generic loop
        // verbatim -- byte-identical for the Snow deck.
        const int n_snow  = snow_strict ? std::min<int>(cost.snow_pips, cost.generic) : 0;
        paying_snow = true;
        for (int i = 0; i < n_snow; ++i)
        { if (!pay(Color::Colorless, true)) { paying_snow = false; return false; } }
        paying_snow = false;
        for (int i = 0; i < cost.generic - n_snow; ++i)
        { if (!pay(Color::Colorless, true )) return false; }
        return true;
    };
    // §2a: a Treasure that paid is SACRIFICED, not left tapped. Deferred to here because erasing
    // mid-payment invalidates the source loops' references (see CommitPaySacSacrifices). Inert when off.
    // PREVENT DAMAGE: a greedy payment over the pain-aware policy's damage cap is treated as a
    // greedy that stranded -- the backtracker below (which honours the cap tap by tap) takes over.
    // PaymentOverCap is a constant false unless a policy is live (armed boards only).
    // The executor's `available` accounting pool is put back in that case: the greedy's taps are
    // being undone, and the backtracker (like the rollout's nullptr pool) never charges it, so a
    // double charge would make the executor refuse a later cast the rollout still pays.
    const bool cap_live = dmgev::t_pay_cap.live && state.dmg_events_armed;
    const ManaPool av_pre_greedy = (cap_live && available) ? *available : ManaPool{};
    if (greedy())
    {
        if (!dmgev::PaymentOverCap(state, active))
        { if (tapstats::Enabled()) { tapstats::g_pay_greedy_ok.fetch_add(1, std::memory_order_relaxed); }
          commit_leftover(floating); CommitPaySacSacrifices(state, active); return true; }
        if (cap_live && available) { *available = av_pre_greedy; }
    }
    // Greedy failed: try the backtracking solver from a clean board.
    g_pay_remaining_mv = pay_full_mv;   // the greedy's partial decrements are undone with its taps
    // OPPONENT life is part of the rollback (2026-08-21): a Grove-class drip land tapped by the
    // failed greedy arrangement has already paid the opponent's gain/loss, and without restoring
    // it the backtracker's own tap pays it AGAIN -- the opponent took Grove's drip twice for one
    // cast (found by the USER's off-by-one audit of a T3 "12-damage" main that legally totals 11).
    // The total-failure restore below always had these two lines; the mid-path restores missed them.
    RestorePayFields(state.battlefield, bf_pre);
    if (g_pay_snap_verify) { VerifyPaySnapRestore(state.battlefield, bf_pre_full, "once.greedy-fail"); }
    state.players[active].life = life_pre;
    state.players[active].graveyard = gy_pre;
    state.players[1 - active].life     = opp_pre;
    state.opponent_lost_life_this_turn = oll_pre;
    if (s_energy_refund) { state.players[active].energy_counters = energy_pre; }   // Aether Hub {E} (see energy_pre)
    // SNOW guard for the backtracking fallbacks: the backtracker is snow-BLIND (it assigns any
    // source to any pip), which is exact when every untapped source is snow (the Snow deck --
    // its complete fallback is preserved) and could construct an illegal {S} assignment on a
    // MIXED manabase -- there it is skipped (pessimistic-safe: a payable cast the greedy missed
    // fails instead of resolving illegally; disclosed in the ledger). Inert for snow_pips == 0.
    auto snow_backtrack_ok = [&]() -> bool { return !snow_strict; };
    ManaPool bt_leftover;
    if (tapstats::Enabled()) { tapstats::g_site_percast.fetch_add(1, std::memory_order_relaxed); }
    if (snow_backtrack_ok()
        && TapForCostBacktrack(state, cost, for_creature, ManaPool{}, nullptr, nullptr, &bt_leftover,
                               /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
    { commit_leftover(bt_leftover); CommitPaySacSacrifices(state, active); return true; }
    // Floating-fed filter retry: a filter / ramp-filter land (Ferrous Lake {1},{T}: Add {U}{R}) can be
    // FED by turn-scoped floating (a ritual's output, a depletion over-tap). SpendFloatingTowardCost
    // above spent that floating on the cost DIRECTLY, stranding the filter (no feeder left) -- so the
    // first backtracker, run on the REDUCED cost with an empty float pool, could not chain it. Retry
    // the backtracker with the ORIGINAL cost and the ORIGINAL reserve as feed, letting it choose
    // feed-vs-spend. Guarded by a non-empty reserve AND an untapped filter/ramp source, so it is only
    // reached in exactly that stranded-feeder case: a non-floating or filter-less board never enters it
    // (byte-identical), and any cast the greedy/first backtracker already paid never reaches a fallback.
    if (reserve_pre.Total() > 0 && AnyUntappedFilterSource(state) && snow_backtrack_ok())
    {
        RestorePayFields(state.battlefield, bf_pre);
        if (g_pay_snap_verify) { VerifyPaySnapRestore(state.battlefield, bf_pre_full, "once.filter-retry"); }
        state.players[active].life  = life_pre;
        state.players[active].graveyard = gy_pre;
        state.players[1 - active].life     = opp_pre;   // same drip rollback as above
        state.opponent_lost_life_this_turn = oll_pre;
        if (s_energy_refund) { state.players[active].energy_counters = energy_pre; }   // Aether Hub {E} (see energy_pre)
        ManaPool bt2_leftover;
        if (tapstats::Enabled()) { tapstats::g_site_percast_filter.fetch_add(1, std::memory_order_relaxed); }
        if (TapForCostBacktrack(state, cost_in, for_creature, reserve_pre, nullptr, nullptr,
                                &bt2_leftover, /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
        {
            state.floating_mana = ManaPool{};   // the whole reserve was re-allocated by the backtracker
            state.floating_creature_mana = cre_reserve_pre;   // ...which paid the FULL cost_in from it
            state.floating_bigspell_mana = big_reserve_pre;   // (same: the big-only reserve is untouched)
            commit_leftover(bt2_leftover);
            CommitPaySacSacrifices(state, active);
            return true;
        }
    }
    // Total failure: a cast that cannot be paid must leave the game exactly as it found it
    // (atomic rollback) -- restore the full pre-payment snapshot, not the greedy's partial-tap
    // end-state. Callers (cycling/sac loops, ill-ordered plans) rely on a failed payment being
    // side-effect-free; the old greedy-fail restore leaked tapped lands / spent counters.
    // (The executor's `available` accounting is deliberately NOT restored -- see ManaPayment.h.)
    RestorePayFields(state.battlefield, bf_pre);
    if (g_pay_snap_verify) { VerifyPaySnapRestore(state.battlefield, bf_pre_full, "once.total-fail"); }
    state.players[active].life         = life_pre;
    state.players[active].graveyard    = gy_pre;
    state.players[1 - active].life     = opp_pre;
    state.opponent_lost_life_this_turn = oll_pre;
    if (s_energy_refund) { state.players[active].energy_counters = energy_pre; }   // Aether Hub {E} (see energy_pre)
    state.floating_mana                = reserve_pre;   // payment failed -> return the reserve untouched
    state.floating_creature_mana       = cre_reserve_pre;
    state.floating_bigspell_mana       = big_reserve_pre;
    if (tapstats::Enabled()) { tapstats::g_pay_once_fail.fetch_add(1, std::memory_order_relaxed); }
    return false;
}

// See ManaPayment.h. HYBRID handling: a colour's flat int already includes hybrid pips baked into
// their first colour (see ManaCost), so a naive `--cost.red` on {R/W}{R/W}{R/W} would leave red=2
// with hybrid_count=3 -- an inconsistent cost ExpandHybrids would mis-expand. Consume a PLAIN pip
// first and only then retire a hybrid entry, which is also the strictly better choice for the
// player: {R}{R/W} minus one red should leave the FLEXIBLE {R/W}, not the rigid {R}.
void ApplyColoredPipReduction(ManaCost& cost, const ManaCost& reduction)
{
    auto reduce_colored_pip = [](ManaCost& c, Color col)
    {
        int* flat = nullptr;
        switch (col)
        {
            case Color::White: flat = &c.white; break;
            case Color::Blue:  flat = &c.blue;  break;
            case Color::Black: flat = &c.black; break;
            case Color::Red:   flat = &c.red;   break;
            case Color::Green: flat = &c.green; break;
            default: return;                     // {C} is not coloured mana
        }
        if (*flat <= 0) { return; }
        int baked = 0;                            // hybrid pips whose FIRST colour is `col`
        for (int i = 0; i < c.hybrid_count; ++i)
        { if (static_cast<Color>(c.hybrid_pair[i] >> 4) == col) { ++baked; } }
        if (*flat - baked > 0) { --*flat; return; }             // a plain pip exists: take it
        for (int i = 0; i < c.hybrid_count; ++i)                // else retire one hybrid entry
        {
            if (static_cast<Color>(c.hybrid_pair[i] >> 4) != col) { continue; }
            for (int j = i; j + 1 < c.hybrid_count; ++j) { c.hybrid_pair[j] = c.hybrid_pair[j + 1]; }
            c.hybrid_pair[--c.hybrid_count] = 0;
            --*flat;
            return;
        }
    };
    for (int k = 0; k < reduction.white; ++k) { reduce_colored_pip(cost, Color::White); }
    for (int k = 0; k < reduction.blue;  ++k) { reduce_colored_pip(cost, Color::Blue);  }
    for (int k = 0; k < reduction.black; ++k) { reduce_colored_pip(cost, Color::Black); }
    for (int k = 0; k < reduction.red;   ++k) { reduce_colored_pip(cost, Color::Red);   }
    for (int k = 0; k < reduction.green; ++k) { reduce_colored_pip(cost, Color::Green); }
}

// Thalia, Guardian of Thraben's SYMMETRIC tax on a noncreature spell: the sum of every battlefield
// permanent's noncreature_spell_tax, ANY controller. 0 for a creature spell and for every deck whose
// stamp (GameState::deck_has_spell_tax) says no taxer can ever reach a battlefield.
static int NoncreatureSpellTax(const CardDefinition& def, const GameState& state)
{
    if (!state.deck_has_spell_tax || def.card.IsCreature()) { return 0; }
    int tax = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.def_absent) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (pd) { tax += pd->params.noncreature_spell_tax; }
    }
    return tax;
}

ManaCost EffectiveSpellCost(const CardDefinition& def, const GameState& state, int copies)
{
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        // An alternative cost still takes cost increases (CR 118.9d).
        ManaCost sc = def.params.spectacle_cost.value();
        sc.generic += NoncreatureSpellTax(def, state);
        return sc;
    }
    ManaCost cost = def.card.m_mana_cost;
    // Splice onto Arcane: casting ONE base while splicing k = copies-1 OTHER copies adds each spliced
    // copy's SPLICE cost (params.splice_cost; unset -> the card's own printed cost) to the base's RAW
    // cost FIRST, so the Medallion/affinity/Hinata reductions below apply ONCE to the combined total
    // (a single floor at 0) -- NOT once per copy (which would over-subtract the reduction). With
    // splice_cost defaulting to the printed cost this is an exact (k+1)x multiply (byte-identical for
    // Desperate Ritual, splice {1}{R} == cast {1}{R}); a splice cost that differs is now priced right.
    // copies==1 (every non-spliced cast) adds nothing -> byte-identical for all other decks.
    if (copies != 1)
    {
        const ManaCost& sc = def.params.splice_cost.has_value()
                           ? def.params.splice_cost.value()
                           : def.card.m_mana_cost;
        const int k = copies - 1;
        cost.generic   += k * sc.generic;
        cost.white     += k * sc.white;
        cost.blue      += k * sc.blue;
        cost.black     += k * sc.black;
        cost.red       += k * sc.red;
        cost.green     += k * sc.green;
        cost.colorless += k * sc.colorless;
    }
    // ADDITIONAL COST "reveal a <subtype> card or pay <cost>" (Daring Buccaneer: Pirate / {2}).
    // CR 601.2f: total cost = mana cost + additional costs - reductions, so it joins the RAW cost
    // here, before every reducer below (a Warchief-style Pirate discount would take the {2} off too).
    // Paid only when no reveal is possible -- revealing is free and nothing reads what was revealed,
    // so paying {2} while a reveal exists is strictly dominated and never chosen. Priced LIVE off the
    // hand at every cast site (enumeration a.cost, apply_one, CastSpellFromHand, SubsetPayable-
    // Sequential); the enumerator adds the same-turn ORDER effect separately (a Pirate cast or
    // Vial-put earlier leaves the hand -- see SameSubsetRevealSurcharge). Param-gated: inert elsewhere.
    if (def.params.reveal_or_pay_cost.has_value() && !def.params.reveal_or_pay_subtype.empty()
        && !CanRevealForAdditionalCost(state, def))
    {
        const ManaCost& rc = *def.params.reveal_or_pay_cost;
        cost.generic   += rc.generic;
        cost.white     += rc.white;
        cost.blue      += rc.blue;
        cost.black     += rc.black;
        cost.red       += rc.red;
        cost.green     += rc.green;
        cost.colorless += rc.colorless;
    }
    // COST INCREASE -- Thalia, Guardian of Thraben: "Noncreature spells cost {1} more to cast."
    // SYMMETRIC (no controller filter: it taxes OUR noncreature spells too -- Aether Vial costs {2}
    // with a Thalia out). Joins the RAW cost before every reduction below (CR 601.2f: cost +
    // increases - reductions), like the additional cost above. Gated on the per-game deck stamp
    // (GameState::deck_has_spell_tax), so a deck without a taxer never walks the board here.
    cost.generic += NoncreatureSpellTax(def, state);
    if (def.params.affinity_for_subtype && !def.params.subtypes_affected.empty())
    {
        int reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            for (const std::string& sub : def.params.subtypes_affected)
            {
                bool matches = p.AnimatedAllTypes();
                if (!matches)
                {
                    for (const std::string& cs : p.card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++reduction; break; }
            }
        }
        cost.generic = std::max(0, cost.generic - reduction);
    }
    // THE THREE REDUCER WALKS BELOW RUN ONLY IF THE DECK CAN CONTAIN A REDUCER AT ALL.
    //
    // Each of them walks the whole battlefield with a LookupCached per permanent, and two are gated
    // only on `!def.card.m_subtypes.empty()` -- true of every creature and every Equipment -- so on
    // a deck with no Medallion / Warchief / Incubator / Ragemonger they are three provable no-ops
    // per cost computation. Measured (callgrind, KittyEquipment v2, 2026-10-03): this function was
    // the single largest caller of LookupCached, 33M of the run's 130M calls, plus 2.16% of all
    // instructions in its own right.
    //
    // The gate is a per-GAME deck stamp, not a CardDatabase presence bit: `cards.json` holds all 486
    // cards on every run, so a DB-wide "is any reducer loaded?" is TRUE for every deck and says
    // nothing. See GameState::deck_has_cost_reducer for the scanned set and
    // GoldFishRunner::StampDeckTraits for the predicate, which mirrors the three `continue` tests
    // below clause for clause. It defaults TRUE, so an unstamped state keeps all three walks.
    //
    // NOT the affinity walk above: that one is already gated on the CASTING card's own
    // `affinity_for_subtype`, which is the right gate and is unaffected by what else is in the deck.
    // ...AND THE THREE THAT REMAIN ARE ONE WALK, with ONE LookupCached per permanent.
    //
    // They were three separate `for (const Permanent& p : state.battlefield)` loops, each
    // re-deriving the same `pd` for the same permanent -- so a deck that DOES hold a reducer (and
    // KittyEquipment v2 does: Cid, Freeflier Pilot carries reduces_spell_subtype, which is why the
    // stamp above is inert there) paid 3N lookups per cost computation where N suffice.
    //
    // WHY FUSING IS EXACT, clause by clause, because this is a cost computation and a wrong answer
    // changes play rather than merely timing:
    //   * The two GENERIC reductions were applied as two floored subtractions. Summing them under
    //     ONE floor is identical: for a, b >= 0, max(0, max(0, g - a) - b) == max(0, g - a - b).
    //   * Ragemonger's reduction touches ONLY the coloured pips and the hybrid list, never
    //     `generic`; the two generic reductions read only `def.card.m_mana_cost` (the PRINTED cost)
    //     and `cost.generic`, never a colour field. The two halves are disjoint, so moving the
    //     coloured one from after the generic subtractions to inside the walk cannot be observed.
    //   * Ragemonger still applies in BATTLEFIELD ORDER, which is what it did before -- two copies
    //     take pips off in sequence and ApplyColoredPipReduction is not commutative with itself on
    //     a hybrid cost.
    if (state.deck_has_cost_reducer)
    {
        // Walk 1 (Medallion) was unconditional; walks 2 and 3 were gated on the SPELL having any
        // subtype. Keeping that distinction is why this is a per-clause test rather than one.
        const bool subtyped = !def.card.m_subtypes.empty();
        int generic_reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (!pd) { continue; }
            const CardParams& pp = pd->params;
            // (1) Ruby Medallion-style COLOUR cost reduction: each permanent you control whose
            // reduces_spell_color matches a colour in THIS spell's printed cost reduces its GENERIC
            // by 1 (floored at 0, stacks per copy). Without this on the EXECUTOR side it over-paid
            // red spells relative to the planner/rollout, so a committed Medallion-funded combo line
            // (T3 Dragonstorm) was unpayable at execution -> fd-diverge. (Same-turn-cast Medallions
            // are handled by ManaPruneBound's bail.)
            if (!pp.reduces_spell_color.empty())
            {
                const std::string& rc = pp.reduces_spell_color;
                const ManaCost&    mc = def.card.m_mana_cost;   // printed pips (colour unchanged by discounts)
                const bool spell_has_color =
                      (rc == "W" && mc.white > 0) || (rc == "U" && mc.blue  > 0)
                    || (rc == "B" && mc.black > 0) || (rc == "R" && mc.red   > 0)
                    || (rc == "G" && mc.green > 0);
                if (spell_has_color) { ++generic_reduction; }
            }
            // (2) Goblin Warchief-style SUBTYPE cost reduction: each permanent you control whose
            // reduces_spell_subtype matches a SUBTYPE of THIS spell reduces its GENERIC by 1
            // (floored at 0, stacks per copy). The subtype twin of (1). (Same-turn-cast Warchief is
            // handled by the in-order walk.)
            //
            // Urza's Incubator discounts only CREATURE spells of the chosen type; Goblin Warchief
            // and Dragonspeaker Shaman discount any spell carrying the subtype.
            if (subtyped && (!pp.reduces_spell_subtype.empty() || pp.chooses_creature_type)
                && !(pp.reduces_spell_subtype_creature_only && !def.card.IsCreature()))
            {
                // The chosen type (Incubator) or the printed one (Warchief/Dragonspeaker).
                const uint16_t rs = ReducerSubtypeId(*pd, p);
                // Per-reducer step: Warchief 1, Dragonspeaker/Incubator 2 ("cost {2} less").
                if (def.card.m_subtypes.HasId(rs))
                { generic_reduction += std::max(1, pp.reduces_spell_subtype_amount); }
            }
            // (3) Ragemonger-style SUBTYPE COLOURED cost reduction: "Minotaur spells you cast cost
            // {B}{R} less to cast. This effect reduces only the amount of COLORED mana you pay."
            // The coloured twin of (2) -- same subtype match, but it subtracts the reducer's
            // coloured pips instead of 1 generic, and never touches the generic. Stacks per copy
            // (two Ragemongers take {B}{B}{R}{R} off a Minotaur spell with that much colour to give).
            //
            // HYBRID handling: a colour's flat int already includes hybrid pips baked into their
            // first colour (see ManaCost), so a naive `--cost.red` on Boros Reckoner's
            // {R/W}{R/W}{R/W} would leave red=2 with hybrid_count=3 -- an inconsistent cost that
            // ExpandHybrids would mis-expand. ApplyColoredPipReduction therefore consumes a PLAIN
            // pip first and only falls back to retiring a hybrid entry, which is also the strictly
            // better choice for the player: {R}{R/W} minus one red should leave the FLEXIBLE {R/W},
            // not the rigid {R}.
            if (subtyped && !pp.reduces_subtype_colored_subtype.empty()
                && pp.reduces_subtype_colored_cost.has_value())
            {
                bool subtype_match = false;
                for (const std::string& cs : def.card.m_subtypes)
                { if (cs == pp.reduces_subtype_colored_subtype) { subtype_match = true; break; } }
                if (subtype_match)
                { ApplyColoredPipReduction(cost, pp.reduces_subtype_colored_cost.value()); }
            }
        }
        cost.generic = std::max(0, cost.generic - generic_reduction);
    }
    // Hollow One: "This spell costs {2} less to cast for each card you've cycled or discarded this
    // turn." Scaled by a PER-TURN counter rather than by a board state, which makes it the only
    // reduction here that GROWS WITHIN A TURN -- see the ManaPruneBound bail, which must treat it
    // like affinity or the mana prune drops a line that only becomes affordable after a few cycles.
    // Generic half only, floor 0 (a {5} Hollow One is free after three cycles).
    if (def.params.cost_less_per_cycle_or_discard > 0)
    {
        const int events = state.players[state.active_player_index].cards_cycled_or_discarded_this_turn;
        cost.generic = std::max(0, cost.generic
                                   - def.params.cost_less_per_cycle_or_discard * events);
    }
    // Hinata's per-target cost reduction (fixed-cost spells; {X} spells apply it at the X-cost
    // sites where the whole generic, incl. X, is known -- CastSpellFromHand / apply_one).
    if (!def.card.m_mana_cost.has_x)
    {
        cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
    }
    return cost;
}

// THE cast-order comparator (C1 unit 3): provider RANK first, then CHEAPEST-FIRST by the action's
// ACTUAL cost. The rank alone is not enough -- every mana ritual shares one rank, so their relative
// order was arbitrary, and the DEAREST could be attempted first, fail to be paid, and be silently
// dropped (CastSpellFromHand returns void). That strands the mana it would have produced and can
// leave the payoff short: Dragonstorm d0 seed 8585 led with Seething Song ({2}{R}) on two lands,
// skipped it, floated 7 off the cheap rituals and then could not pay Dragonstorm ({8}{R} = 9) -- the
// whole chain burned. A ritual chain funds itself cheapest-first (the principle BuildAccelPrefixOrder
// already uses for the ENUMERATION); this applies it to EXECUTION.
// It must key on Action::cost, NOT the card's printed cost: CastOrderRank only sees the
// CardDefinition, so it cannot see that a SPLICED Desperate Ritual really costs {2}{R}{R} rather
// than {1}{R}. Ranking by the printed cost put the spliced copy early and dropped it exactly like
// Seething Song. Was a byte-identical twin pair (TurnSolver's CastOrderLess / AIEngine's
// CastOrderLessAI); executor and rollout now share this definition.
bool CastOrderLessRanked(const GameState& state, const Action& a, int ra,
                                                 const Action& b, int rb)
{
    if (ra != rb) { return ra < rb; }
    if (LegacyCastTierOrder()) { return false; }                                   // stable: keep plan order
    if (!ResolveProvider(state).CastCheapestFirstWithinTier()) { return false; }   // stable: keep plan order
    // Reuse the action's memoized def (back-filled once per node, == Lookup(card_name)) instead of
    // re-hashing the name string on every comparison -- this comparator runs O(n log n) per sort.
    // Byte-identical; def==nullptr (an action from a path that didn't back-fill) falls back to Lookup.
    const CardDefinition* da = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    const CardDefinition* db = b.def ? b.def : CardDatabase::Instance().Lookup(b.card_name);
    // ONLY among mana accelerants. Applying it to every equal-rank tie also reordered CREATURES,
    // where cost is the wrong key and ETB order carries real value: Scourge of Valkas damages per
    // Dragon that enters, so "Lathliss then Scourge" and "Scourge then Lathliss" differ by 3 damage
    // (dragonstorm_overnight_d3_s7007 gi310 lost a turn to exactly that swap, with identical draws).
    if (!da || !db || !IsManaRitual(*da) || !IsManaRitual(*db)) { return false; }
    return a.cost.ManaValue() < b.cost.ManaValue();
}

bool OrderM1FirstEnabled()
{
    static const bool on = EnvOn("MTG_ORDER_M1_FIRST");
    return on;
}

int CastOrderKey(const GameState& state, const CardDefinition* def, int rank)
{
    // x4 leaves room for the three phase classes underneath each rank, so the rank stays the
    // primary key exactly as before and the phase only breaks its ties.
    const int key = rank * 4;
    if (!OrderM1FirstEnabled() || def == nullptr) { return key; }
    // ClassifyCastMainPhase: 0 = Main1, 1 = Main2, 2 = Both. Wanted order is Main1 < Both < Main2
    // -- "both" is a card the classifier declined to defer, so it belongs with the pre-combat
    // group rather than after the cards it positively judged post-combat.
    const int mp = TurnSolver::ClassifyCastMainPhase(state, *def);
    return key + (mp == 0 ? 0 : (mp == 2 ? 1 : 2));
}

// The definition an action's ORDER derives from. Under MTG_GARTH_ORDERED a Garth activation IS
// the cast of its conjured copy (WotC ruling: the copy is cast as the ability resolves -- there
// is no choosing when; USER 2026-08-19: "order his spells like the rest and he should tap at
// those times"), so it ranks, ladders and projects as the COPY, not as Garth's own card. Every
// other action keeps the historical resolution -- lever off is byte-identical.
static const CardDefinition* OrderDefOf(const Action& a)
{
    if (GarthOrderedEnabled() && a.kind == Action::Kind::GarthActivate)
    {
        if (const CardDefinition* cd = CardDatabase::Instance().Lookup(a.tutor_target))
        { return cd; }
    }
    return a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
}

bool CastOrderLess(const GameState& state, const Action& a, const Action& b)
{
    const CardDefinition* da = OrderDefOf(a);
    const CardDefinition* db = OrderDefOf(b);
    const int ra = CastOrderKey(state, da, da ? ResolveProvider(state).CastOrderRank(state, *da) : 20);
    const int rb = CastOrderKey(state, db, db ? ResolveProvider(state).CastOrderRank(state, *db) : 20);
    return CastOrderLessRanked(state, a, ra, b, rb);
}

// A cast whose resolution triggers a mid-turn re-solve breakpoint (draw / staging / cascade
// / retrace): the rest of the turn re-solves from the post-draw state, so the optimal cast
// ORDER around it is situation-dependent (mana left, what is revealed) -- a static rank
// can't capture it. The CastOrderLess reordering is therefore SKIPPED for any set that
// contains such a card; that set keeps its canonical plan/breakpoint order (the search owns
// the ambiguous ordering).
bool OrderingOpaque(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawUntilNonland
        || d->params.stages_cards
        || d->params.cascade_max_mv > 0
        || d->params.shuffle_reveal_freecast   // Creative Technique: mid-turn free cast
        || d->params.etb_exile_until_nonland   // Breaching Dragonstorm: enter-trigger free cast
        || d->params.retrace
        || d->params.expressive_iteration
        || d->params.impulse_exile > 0   // Apex of Power: staged exile -> search-owned breakpoint order
        || d->params.draw > 0
        // Zada/Mirrorwing solo-target trick: every trick in the deck cantrips (cast_draw), and a
        // magnet fan-out mass-draws mid-turn -- the post-draw re-solve owns the ordering.
        || d->params.solo_target_trick;
}

// ---- The cast-order RANGE and its fallback ladder ---------------------------------------------
// See ManaPayment.h for the shape and docs/design/cast-order-ideal-with-ranges.md for the design.

bool CastOrderRangeEnabled()
{
    static const bool on = EnvOn("MTG_ORDER_RANGE");
    return on;
}

bool OpaqueCastOrderEnabled()
{
    static const bool on = EnvOn("MTG_ORDER_OPAQUE");
    return on;
}

bool OpaqueCastOrderActive(const GameState& state)
{
    return OpaqueCastOrderEnabled() || ResolveProvider(state).OrderOpaqueCastsByRank();
}

CastOrderRange CastOrderRangeOf(const GameState& state, const CardDefinition& def)
{
    const int ideal = ResolveProvider(state).CastOrderRank(state, def);
    int cost_efficient = ideal;
    {
        IdealOrderSuppressScope _no_promotion;
        cost_efficient = ResolveProvider(state).CastOrderRank(state, def);
    }
    // A promotion can only move a card EARLIER, so the suppressed rank is the far end. The max()
    // is defensive: an archetype override that ignores the tier returns the same number twice,
    // which collapses the range to a point and makes the ladder inert for that card.
    return CastOrderRange{ ideal, std::max(ideal, cost_efficient) };
}

// Can this set's line be PROJECTED from a fixed per-cast cost? Two things make a stamped cost the
// wrong number to walk an order against:
//   * a DYNAMIC cost -- the Hinata / Soulfire per-target discounts depend on a board that changes
//     as the line resolves, so the same cast costs differently at a different position. ({X} is
//     NOT in this class: chosen_x is fixed at enumeration and Action::cost already carries the X
//     generic, which is why an X ritual is projectable where the batch pre-payment declines it --
//     the prepay commits real mana up front, this only picks an order.)
//   * a not-yet-live SPECTACLE cost, which is ORDER-DEPENDENT: Light Up the Stage is {2}{R} until
//     an opponent has lost life and {R} after, so its stamped cost is only right at the position
//     the enumeration gave it. That is precisely the dependency the range does not yet carry (the
//     worked example in the design doc), so a spectacle set keeps its current order rather than
//     being walked against a cost that is about to change underneath it.
// A PRODUCER (ritual float / same-turn rock ramp) is NOT a decline: the projection credits its
// output below. Declining it was measured to be the ladder's whole failure -- hinata's combo turns
// are producer turns, so exactly the lines where casting a cantrip first is catastrophic were the
// ones the ladder refused to look at, and the range arm scored identically to the promotion alone.
// Declining leaves `order` exactly as the caller sorted it.
static bool LadderProjectable(const GameState& state, const std::vector<Action>& acts,
                              const std::vector<int>& order)
{
    const int active = state.active_player_index;
    for (int i : order)
    {
        const Action& a = acts[i];
        if (a.alt_cost) { continue; }                       // pays no mana at all
        if (a.has_spectacle && !state.opponent_lost_life_this_turn) { return false; }
        const CardDefinition* d = OrderDefOf(a);   // Garth activation projects as its copy
        if (!d) { return false; }
        if (SoulfireOwnTargetDiscount(*d, state, active, a.soulfire_own_targets) > 0) { return false; }
        if (HinataGenericDiscount(*d, state, a.chosen_x) > 0) { return false; }
    }
    return true;
}

// §2a FRESH-HOLD: a minted Treasure is same-turn mana only when a copy-magnet is live (board) or
// the plan itself casts one -- otherwise the hold banks it, and crediting it below would promise
// mana the payer will refuse. Plan-wide magnet check, not order-position-exact: an over-credit
// here only ever picks a DIFFERENT legal order (see the FirstUnpayablePos header note), and the
// common fan shape is Zada-then-Gold-Rush inside one plan.
static bool MintedTreasureSpendable(const GameState& state, const std::vector<Action>& acts)
{
    if (!TreasurePaySourceEnabled() || !FreshHoldActive()) { return true; }
    if (CopyMagnetLive(state, state.active_player_index)) { return true; }
    // MTG_MINT_CREDIT_EXACT: the payer's own rule (PaySacSpendableNow) -- a live Heroism releases
    // the hold too (MTG_HEROISM_FRESH_HOLD), a clause this projection lacked.
    if (MintCreditExactOn() && FreshMintSpendableNow(state, state.active_player_index)) { return true; }
    for (const Action& a : acts)
    {
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (d && d->params.copies_solo_targeted_spells) { return true; }
    }
    return false;
}

// The ORDER POSITION of the first cast the line cannot pay for, or -1 when all of them pay.
// Projected against AvailableManaPool -- the same aggregate accounting pool the batch pre-payment
// uses, not a per-source solve, so it is approximate in the same direction and to the same degree.
// That is safe here in a way it would not be at a payment site: a wrong answer picks a DIFFERENT
// legal order, never an illegal one, because every rung of the ladder is an order the engine would
// have been willing to execute anyway.
// `hold` (MTG_MINT_CREDIT_EXACT): project as if this source were already tapped -- the pump
// target the payment layer is holding (see the hold-aware pass in ApplyCastOrderRangeLadder).
// MTG_PAYABLE_ORDER's simulated hand: the reveal-relevant cards still in hand as the walk proceeds
// (name + printed definition; hand placeholders carry no types, so the subtype test reads the def,
// exactly as CanRevealForAdditionalCost does). nullptr = the reveal-blind walk FirstUnpayablePos has
// always been.
struct PoHandCard { InternedName name; const CardDefinition* def; };

static bool PoIsRevealCostCast(const Action& a)
{
    if (a.kind != Action::Kind::CastFromHand || a.alt_cost || a.free_cast) { return false; }
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    return d != nullptr && d->params.reveal_or_pay_cost.has_value()
        && !d->params.reveal_or_pay_subtype.empty();
}

// The reveal-or-pay cast's price AT ITS POSITION: a.cost with the reveal surcharge normalised out
// (clamped -- a.cost was priced on the plan-start hand, which may or may not have had a reveal),
// then re-added iff no OTHER card of the subtype remains in the simulated hand (one copy of its own
// name is skipped as self, as CanRevealForAdditionalCost does).
static ManaCost PoRevealAwareCost(const Action& a, const std::vector<PoHandCard>& hand)
{
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    const ManaCost& rc = *d->params.reveal_or_pay_cost;
    ManaCost c = a.cost;
    c.generic   = std::max(0, c.generic   - rc.generic);
    c.white     = std::max(0, c.white     - rc.white);
    c.blue      = std::max(0, c.blue      - rc.blue);
    c.black     = std::max(0, c.black     - rc.black);
    c.red       = std::max(0, c.red       - rc.red);
    c.green     = std::max(0, c.green     - rc.green);
    c.colorless = std::max(0, c.colorless - rc.colorless);
    const std::string& want = d->params.reveal_or_pay_subtype;
    bool self_skipped = false;
    for (const PoHandCard& h : hand)
    {
        if (!self_skipped && h.name == d->card.m_name) { self_skipped = true; continue; }
        if (h.def && CardHasSubtype(h.def->card, want)) { return c; }
    }
    AddManaCost(c, rc);
    return c;
}

static void PoRemoveFromHand(std::vector<PoHandCard>& hand, const InternedName& n)
{
    for (std::size_t i = 0; i < hand.size(); ++i)
    { if (hand[i].name == n) { hand.erase(hand.begin() + static_cast<long>(i)); return; } }
}

// What a cast PRODUCES for the casts after it -- the credit half of FirstUnpayablePos's walk,
// factored out so MTG_PAYABLE_ORDER's aggregate bound credits exactly the same terms.
static void CreditCastOutput(const GameState& state, const std::vector<Action>& acts, const Action& a,
                             ManaPool& pool)
{
    // Credit what this cast PRODUCES, so the rest of the line is projected against the mana it
    // will actually have. Same two terms the enumeration's subset math credits (Action carries
    // both precisely so no per-node card lookup is needed), and the same colour semantics the
    // real float uses -- a ritual's own colour when it has one (the Dragonstorm rituals float
    // {R}, which cannot pay an off-colour pip), the searched colour for the chosen-colour
    // dimension, wild otherwise. Both terms are zero for every non-producer -> no cost.
    if (a.ritual_float > 0)
    {
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        const std::string& col = !a.chosen_float_color.str().empty()
                               ? a.chosen_float_color.str()
                               : (d ? d->params.ritual_float_color : std::string());
        AddColorToPool(pool, col, a.ritual_float);
    }
    if (a.rock_mana.Total() > 0) { pool.AddPool(a.rock_mana); }
    // Treasures minted by the cast (Gold Rush) are same-turn mana through the deferred
    // breakpoint re-solve (real SacForMana candidates), so the projection credits them as
    // wild -- BASE count only (a magnet fan-out mints one per copy, but the fan width is a
    // board fact this projection does not model). The under-credit is the safe direction:
    // it can only walk a funding spell one rung earlier than strictly needed, never project
    // an unpayable line as payable.
    {
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (d && d->params.creates_treasures > 0 && MintedTreasureSpendable(state, acts))
        {
            // MTG_MINT_CREDIT_EXACT: the exact width, re-read off the LIVE board (the ladder
            // runs after the enabler pass, so a Heroism this plan cast ahead of the minter is
            // on the battlefield here and its copy's Treasure counts -- SamePlanHeroismMint's
            // apply-side twin). The stamp (mint_gain, pre-plan board) is only the lever test.
            // 0 with the lever off -> the base count as before.
            const int n = a.mint_gain > 0
                        ? MintedTreasuresForCast(state, state.active_player_index, *d,
                                                 a.enchant_target, a.soulfire_own_targets)
                        : d->params.creates_treasures;
            AddColorToPool(pool, std::string(), n);
        }
    }
}

// The walk behind FirstUnpayablePos, over an explicit starting pool and an optional simulated hand.
// With `hand` == nullptr it is FirstUnpayablePos exactly (a.cost, no hand tracking).
static int FirstUnpayablePosImpl(const GameState& state, const std::vector<Action>& acts,
                                 const std::vector<int>& order, ManaPool pool,
                                 std::vector<PoHandCard>* hand)
{
    for (int pos = 0; pos < static_cast<int>(order.size()); ++pos)
    {
        const Action& a = acts[order[pos]];
        if (hand != nullptr)
        {
            // Reveal-aware (MTG_PAYABLE_ORDER): price at this position, then the card leaves hand
            // whether it paid mana or not (an alt-cost cast still leaves the hand).
            const bool rop = PoIsRevealCostCast(a);
            const ManaCost cost = rop ? PoRevealAwareCost(a, *hand) : a.cost;
            PoRemoveFromHand(*hand, a.card_name);
            if (a.alt_cost) { continue; }
            if (!pool.CanPay(cost)) { return pos; }
            PayFromPool(pool, cost);
        }
        else
        {
            if (a.alt_cost) { continue; }
            if (!pool.CanPay(a.cost)) { return pos; }
            PayFromPool(pool, a.cost);
        }
        CreditCastOutput(state, acts, a, pool);
    }
    return -1;
}

static int FirstUnpayablePos(const GameState& state, const std::vector<Action>& acts,
                             const std::vector<int>& order, const Permanent* hold = nullptr)
{
    // AvailableManaPool already includes the turn-scoped float.
    return FirstUnpayablePosImpl(state, acts, order, AvailableManaPool(state, hold), nullptr);
}

bool MintHoistAfterMagnets(const GameState& state, const std::vector<Action>& acts)
{
    if (!MintCreditExactOn()) { return false; }
    const DecisionProvider& prov = ResolveProvider(state);
    ManaCost hoisted, magnets, first_mint;
    int minters = 0; bool any_ena = false;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand || a.alt_cost || a.free_cast) { continue; }
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (!d) { continue; }
        if (d->params.creates_treasures > 0)
        {
            if (minters++ == 0 || a.cost.ManaValue() < first_mint.ManaValue()) { first_mint = a.cost; }
            continue;
        }
        if (a.sacrifice_land || prov.CastEnablerFirst(state, a.card_name))
        {
            hoisted = AddManaCosts(hoisted, a.cost);
            any_ena = true;
            if (d->params.copies_solo_targeted_spells) { magnets = AddManaCosts(magnets, a.cost); }
        }
    }
    if (minters == 0 || !any_ena) { return false; }
    const ManaPool pool = AvailableManaPool(state);
    if (pool.CanPay(AddManaCosts(hoisted, first_mint))) { return false; }   // the late slot pays: keep the reviewed order
    return pool.CanPay(AddManaCosts(magnets, first_mint));
}

bool MintLineCanCrack(const GameState& state, const std::vector<Action>& acts)
{
    if (!MintCreditExactOn() || !TreasurePaySourceEnabled()) { return false; }
    bool any_mint = false;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::CastFromHand && !a.alt_cost && !a.free_cast && a.mint_gain > 0)
        { any_mint = true; break; }
    }
    return any_mint && MintedTreasureSpendable(state, acts);
}

int HoistSortKey(const GameState& state, const Action& a, bool minter_hoisted)
{
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    if (!d) { return 0; }
    if (minter_hoisted && d->params.creates_treasures > 0) { return 11; }
    return ResolveProvider(state).CastOrderRank(state, *d) * 2;
}

bool HoistedMinterCast(const Action& a)
{
    if (a.kind != Action::Kind::CastFromHand || a.alt_cost || a.free_cast) { return false; }
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    return d != nullptr && d->params.creates_treasures > 0;
}

namespace
{
bool CopyMagnetDef(const Action& a)
{
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    return d != nullptr && d->params.copies_solo_targeted_spells;
}
}

void PlaceHoistedMinters(const GameState& state, const std::vector<Action>& acts, std::vector<int>& ena)
{
    std::vector<int> minters, rest;
    for (int i : ena) { (HoistedMinterCast(acts[i]) ? minters : rest).push_back(i); }
    if (minters.empty()) { return; }
    const ManaPool pool = AvailableManaPool(state);
    for (int m : minters)
    {
        // The magnets always precede (the earliest slot the user allowed); the walk starts after
        // them and stops at the first enabler the pool cannot pay together with the minter --
        // CanPay is monotone in the prefix, so the first failure is the last position.
        std::size_t lead = 0;
        ManaCost acc;
        while (lead < rest.size() && CopyMagnetDef(acts[rest[lead]]))
        { acc = AddManaCosts(acc, acts[rest[lead]].cost); ++lead; }
        std::size_t at = lead;
        for (std::size_t p = lead; p < rest.size(); ++p)
        {
            const ManaCost next = AddManaCosts(acc, acts[rest[p]].cost);
            if (!pool.CanPay(AddManaCosts(next, acts[m].cost))) { break; }
            acc = next;
            at  = p + 1;
        }
        rest.insert(rest.begin() + static_cast<std::ptrdiff_t>(at), m);
    }
    ena = rest;
}

bool MintHoistPrefixHeroism(const GameState& state, const std::vector<Action>& cands,
                            const std::vector<int>& hoisted, const ManaCost& first_mint,
                            const ManaPool& pool, int& copies, int& bodies)
{
    copies = 0; bodies = 0;
    auto count = [&](int j)
    {
        const Action& a = cands[j];
        if (a.kind != Action::Kind::CastFromHand || a.alt_cost) { return; }
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (d == nullptr || d->params.frontline_copy_tokens <= 0) { return; }
        copies += d->params.frontline_copy_tokens;
        bodies += d->params.etb_self_creates_tokens;
    };
    ManaCost all = first_mint, magnets = first_mint;
    for (int j : hoisted)
    {
        all = AddManaCosts(all, cands[j].cost);
        if (CopyMagnetDef(cands[j])) { magnets = AddManaCosts(magnets, cands[j].cost); }
    }
    if (pool.CanPay(all))                        // the reviewed order: the whole hoist precedes
    {
        for (int j : hoisted) { count(j); }
        return true;
    }
    if (!pool.CanPay(magnets)) { return false; }   // the hoist cannot fire and the late slot cannot pay
    std::vector<int> order = hoisted;             // the apply's hoist order (stable on equal keys)
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return HoistSortKey(state, cands[x], true) < HoistSortKey(state, cands[y], true); });
    ManaCost acc;
    for (int j : order)
    {
        if (CopyMagnetDef(cands[j])) { acc = AddManaCosts(acc, cands[j].cost); count(j); continue; }
        const ManaCost next = AddManaCosts(acc, cands[j].cost);
        if (!pool.CanPay(AddManaCosts(next, first_mint))) { break; }
        acc = next;
        count(j);
    }
    return true;
}

bool OrderRecheckEnabled()
{
    // ADOPTED default-on (USER, 2026-08-18): the Remedy/Silence alternation that makes the
    // Reverent+Remedy+Reverent rebuild executable. Scoped-arm evidence: train green-or-flat,
    // held-out 7 green / 5 flat / 0 red, per-game 12 faster / 0 slower. =0 reverts.
    static const bool on = EnvOn("MTG_ORDER_RECHECK", true);   // DEFAULT ON; =0 disables
    return on;
}

void ApplyEnablerWipeRecheck(const GameState& state, const std::vector<Action>& acts,
                             std::vector<int>& order)
{
    // Size gate is 2, not 4: with an enabler ALREADY live the smallest recheck case is
    // [wipe, enabler] -- "[Remedy already out] Silence, Remedy" keeps the fresh Remedy off the
    // board until after the wipe, and the 3-cast "[backed] Silence, Remedy, Silence" is the
    // POST-COMBAT kill shape under the enforced main split (the pre-combat 4-cast form was the
    // only one the old `< 4` fast-out let through; the m2 route's {Silence,Silence,Remedy}
    // subset was canonical-ordered Remedy-first, its wipe killed BOTH Remedies, and the second
    // Silence gifted the opponent 6 -- a T3 kill the search then could not see; g6006_285).
    if (!OrderRecheckEnabled() || order.size() < 2) { return; }

    std::vector<int> enablers, wipes;   // positions WITHIN `order`
    for (int pos = 0; pos < static_cast<int>(order.size()); ++pos)
    {
        const Action& a = acts[order[pos]];
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (!d) { continue; }
        if (d->params.lifegain_to_loss && d->card.IsCreature()) { return; }   // survives the wipe -> plain order is right
        if (d->params.lifegain_to_loss)          { enablers.push_back(pos); }
        if (d->params.destroy_all_enchantments)  { wipes.push_back(pos); }
    }
    // No wipe in the ordered set -> nothing to re-check. This is the hot-path exit for every
    // deck but Anti-Lifegain (Reverent Silence is the only destroy_all_enchantments card), and
    // it runs BEFORE the battlefield scans so lowering the size gate costs other decks only
    // this one pass over the plan's defs.
    if (wipes.empty()) { return; }

    // A CREATURE enabler survives the wipe (Plague Drone is not an enchantment), so with one of
    // those around every wipe is already backed and the plain order is right -- do nothing.
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.lifegain_to_loss && d->card.IsCreature()) { return; }
    }

    // Is an enabler ALREADY live? Then the first wipe is backed by the board and the plan's own
    // enabler is redundant where the rank put it (first) -- it is the REPLACEMENT, and its job is
    // to re-arm after the wipe. This is the case that actually occurs: a Reverent Silence is only
    // ever emitted with a Remedy already on the battlefield, so "Remedy, Silence, Remedy, Silence"
    // is really "[Remedy already out] Silence, Remedy, Silence".
    bool backed_now = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.lifegain_to_loss) { backed_now = true; break; }
    }

    if (wipes.size() < 2 && !(backed_now && wipes.size() >= 1 && !enablers.empty())) { return; }
    if (enablers.empty()) { return; }

    // Keep in place: the first enabler ONLY when nothing is live yet (it is what backs the first
    // wipe), and the first wipe always -- that pass is the order, and it is correct. Everything
    // left over is the recheck, alternating enabler/wipe so every wipe is paid for while an
    // enabler is live. Placed at the END, which is where the USER put it.
    const std::size_t first_enabler = backed_now ? 0 : 1;   // index into `enablers` to start moving
    std::vector<char> moved(order.size(), 0);
    std::vector<int>  tail;
    for (std::size_t k = 0; first_enabler + k < enablers.size() || k + 1 < wipes.size(); ++k)
    {
        if (first_enabler + k < enablers.size())
        { tail.push_back(order[enablers[first_enabler + k]]); moved[enablers[first_enabler + k]] = 1; }
        if (k + 1 < wipes.size())
        { tail.push_back(order[wipes[k + 1]]); moved[wipes[k + 1]] = 1; }
    }
    if (tail.empty()) { return; }
    std::vector<int> rebuilt;
    rebuilt.reserve(order.size());
    for (std::size_t pos = 0; pos < order.size(); ++pos)
    { if (!moved[pos]) { rebuilt.push_back(order[pos]); } }
    rebuilt.insert(rebuilt.end(), tail.begin(), tail.end());
    order.swap(rebuilt);

    if (EnvOn("MTG_ORDER_RANGE_PROBE"))
    {
        std::fprintf(stderr, "[order-recheck] turn=%d enablers=%d wipes=%d -> %d recheck casts\n",
                     state.turn_number, static_cast<int>(enablers.size()),
                     static_cast<int>(wipes.size()), static_cast<int>(tail.size()));
    }
}

static bool IsEtbTreasureMakerCast(const Action& a)
{
    // A Vial PUT of the maker counts too: puts are sequenced like casts (USER 2026-10-06), so the
    // hoist that moves a cast Corsair Captain ahead of the casts its Treasure funds moves a put one.
    if ((a.kind != Action::Kind::CastFromHand && a.kind != Action::Kind::ActivateVial)
        || a.alt_cost || a.rock_mana.Total() <= 0) { return false; }
    const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    return d != nullptr && d->params.etb_creates_treasures > 0;
}

void ApplyEtbTreasureFundingOrder(const GameState& state, const std::vector<Action>& acts,
                                  std::vector<int>& order)
{
    if (order.size() < 2) { return; }
    // Hot-path exit: the stamp exists only with the lever on and only on an etb_creates_treasures
    // card, so every other deck (Mirrorwing's Gold Rush included) leaves after this one pass.
    bool any = false;
    for (int i : order) { if (IsEtbTreasureMakerCast(acts[i])) { any = true; break; } }
    if (!any) { return; }
    std::vector<int> hoisted, rest;
    for (int i : order) { (IsEtbTreasureMakerCast(acts[i]) ? hoisted : rest).push_back(i); }
    if (rest.empty()) { return; }
    hoisted.insert(hoisted.end(), rest.begin(), rest.end());
    if (hoisted == order) { return; }
    if (FirstUnpayablePos(state, acts, order) < 0) { return; }     // the reviewed order pays: keep it
    if (FirstUnpayablePos(state, acts, hoisted) >= 0) { return; }  // hoisting does not rescue it either
    order.swap(hoisted);
}

// ---- PAYABLE-ORDER FALLBACK (MTG_PAYABLE_ORDER) ---------------------------------------------------
//
// USER 2026-09-27 (Pirates): hand Malcolm {U}{R}, Corsair Captain {2}{U}, Daring Buccaneer {R} ("reveal
// a Pirate card or pay {2}"), five lands. Six mana of cost; the Corsair's Treasure is the sixth. The
// reviewed order (Malcolm, Buccaneer revealing the Corsair, Corsair) brings the Treasure in LAST, so
// one cast is stranded; the maker hoist (Corsair, Malcolm, Buccaneer) leaves the Buccaneer nothing to
// reveal -> {2}{R}, still unpayable. Corsair, Buccaneer (revealing Malcolm), Malcolm casts all three
// and gives up only Malcolm's Clue. The USER's scope: "it really is just for the 'fail to pay' case,
// that this is worth doing" and "If we have mana for all of them, then there is no need to change the
// order." So: the given order stands whenever it projects payable; only when it does not is a
// permutation of the SAME casts taken, and only one that projects payable.
//
// The projection is FirstUnpayablePos's walk plus a simulated hand: a reveal-or-pay cast is charged
// its surcharge iff no other card of the subtype is left in hand at its position (the cards cast
// before it have gone). Same aggregate-pool colour semantics and producer credits, so it is exactly as
// colour-exact as FirstUnpayablePos -- a wrong answer picks a DIFFERENT legal order, never an illegal
// one.
//
// Search order when the given order fails: (1) the maker hoist ApplyEtbTreasureFundingOrder already
// performs (so every line it rescued today it still rescues, now judged reveal-aware); (2) the
// permutations of the order's casts, NEAREST first -- fewest pairwise inversions against the ANCHOR
// (the maker-hoisted order when a maker is present -- the reviewed doctrine that the Treasure maker
// goes first when its Treasure is needed -- else the given order), then fewest inversions against the
// given order, then lexicographic by position. Exhaustive for n <= 7 casts, single-element moves
// beyond that. None pays -> the given order, untouched.
//
// DOMAIN: an order holding a stamped ETB-Treasure maker (Action::rock_mana on an etb_creates_treasures
// cast) or a reveal-or-pay cast. Everything else returns after one pass over the order (a pure
// reorder of non-producing, reveal-free casts cannot change the aggregate verdict anyway).
bool PayableOrderOn()
{
    static const bool env_on = EnvOn("MTG_PAYABLE_ORDER", true);   // DEFAULT ON (adopted 2026-09-27, USER; =0 reverts)
    return heurarm::Flag(heurarm::PAYABLE_ORDER, env_on);
}

namespace
{
// MTG_PAYABLE_ORDER_STATS: fire counts, printed at exit. [0] = apply sites, [1] = enumeration.
struct PayableOrderStats
{
    std::atomic<long long> domain[2]{}, default_pays[2]{}, bound_fail[2]{}, hoist[2]{}, perm[2]{},
                           none[2]{};
    ~PayableOrderStats()
    {
        if (!EnvOn("MTG_PAYABLE_ORDER_STATS")) { return; }
        static const char* const kSite[2] = { "apply", "enum" };
        for (int k = 0; k < 2; ++k)
        {
            std::fprintf(stderr,
                         "[payable-order] site=%s in_domain=%lld default_pays=%lld bound_fail=%lld "
                         "hoist_rescue=%lld perm_rescue=%lld none_pays=%lld\n",
                         kSite[k], domain[k].load(), default_pays[k].load(), bound_fail[k].load(),
                         hoist[k].load(), perm[k].load(), none[k].load());
        }
    }
};
PayableOrderStats g_po_stats;

int Inversions(const std::vector<int>& perm, const std::vector<int>& rank_of)
{
    int inv = 0;
    for (std::size_t i = 0; i < perm.size(); ++i)
    { for (std::size_t j = i + 1; j < perm.size(); ++j) { if (rank_of[perm[i]] > rank_of[perm[j]]) { ++inv; } } }
    return inv;
}
}   // namespace

static void PayableCastOrderCore(const GameState& state, const std::vector<Action>& acts,
                                 std::vector<int>& order, const std::vector<InternedName>* hand_exits,
                                 int extra_wild, int site)
{
    if (order.size() < 2) { return; }
    bool any = false;
    for (int i : order)
    { if (IsEtbTreasureMakerCast(acts[i]) || PoIsRevealCostCast(acts[i])) { any = true; break; } }
    if (!any) { return; }
    g_po_stats.domain[site].fetch_add(1, std::memory_order_relaxed);

    ManaPool pool0 = AvailableManaPool(state);   // already includes the turn-scoped float
    if (extra_wild > 0) { pool0.wild += extra_wild; }
    thread_local std::vector<PoHandCard> hand0, hand;
    hand0.clear();
    for (const Card& c : state.ActivePlayer().hand)
    {
        if (c.m_is_staged) { continue; }
        hand0.push_back({ c.m_name, CardDatabase::Instance().LookupCached(c) });
    }
    if (hand_exits) { for (const InternedName& n : *hand_exits) { PoRemoveFromHand(hand0, n); } }
    auto pays = [&](const std::vector<int>& ord) -> bool
    {
        hand = hand0;
        return FirstUnpayablePosImpl(state, acts, ord, pool0, &hand) < 0;
    };
    if (pays(order)) { g_po_stats.default_pays[site].fetch_add(1, std::memory_order_relaxed); return; }

    // Order-free necessary condition: every cast at its cheapest (reveal granted) against the pool
    // plus EVERY producer's output. A sequential payment implies this aggregate one, so failing it
    // means no permutation pays -- the common case for a subset simply beyond the board.
    {
        ManaPool bound = pool0;
        ManaCost total{};
        for (int i : order)
        {
            const Action& a = acts[i];
            CreditCastOutput(state, acts, a, bound);
            if (a.alt_cost) { continue; }
            if (PoIsRevealCostCast(a))
            {
                // reveal granted: price against a hand holding one extra subtype card (never self)
                const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
                ManaCost c = a.cost;
                const ManaCost& rc = *d->params.reveal_or_pay_cost;
                c.generic   = std::max(0, c.generic   - rc.generic);
                c.white     = std::max(0, c.white     - rc.white);
                c.blue      = std::max(0, c.blue      - rc.blue);
                c.black     = std::max(0, c.black     - rc.black);
                c.red       = std::max(0, c.red       - rc.red);
                c.green     = std::max(0, c.green     - rc.green);
                c.colorless = std::max(0, c.colorless - rc.colorless);
                AddManaCost(total, c);
            }
            else { AddManaCost(total, a.cost); }
        }
        if (!bound.CanPay(total))
        { g_po_stats.bound_fail[site].fetch_add(1, std::memory_order_relaxed); return; }
    }

    // (1) The maker hoist (ApplyEtbTreasureFundingOrder's order), judged reveal-aware.
    std::vector<int> hoisted, rest;
    for (int i : order) { (IsEtbTreasureMakerCast(acts[i]) ? hoisted : rest).push_back(i); }
    const bool has_maker = !hoisted.empty() && !rest.empty();
    hoisted.insert(hoisted.end(), rest.begin(), rest.end());
    if (has_maker && hoisted != order && pays(hoisted))
    {
        order.swap(hoisted);
        g_po_stats.hoist[site].fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // (2) Nearest payable permutation. Work in POSITIONS of the given order (0..n-1).
    const int n = static_cast<int>(order.size());
    std::vector<int> def_rank(n), anc_rank(n);
    for (int p = 0; p < n; ++p) { def_rank[p] = p; }
    {
        // anchor = the hoisted order when a maker is present, else the given order
        std::vector<int> anc_pos;
        if (has_maker)
        {
            for (int p = 0; p < n; ++p) { if (IsEtbTreasureMakerCast(acts[order[p]])) { anc_pos.push_back(p); } }
            for (int p = 0; p < n; ++p) { if (!IsEtbTreasureMakerCast(acts[order[p]])) { anc_pos.push_back(p); } }
        }
        else { for (int p = 0; p < n; ++p) { anc_pos.push_back(p); } }
        for (int r = 0; r < n; ++r) { anc_rank[anc_pos[r]] = r; }
    }
    struct Cand { int k1, k2; std::vector<int> perm; };
    std::vector<Cand> cands;
    auto add = [&](const std::vector<int>& perm)
    { cands.push_back({ Inversions(perm, anc_rank), Inversions(perm, def_rank), perm }); };
    std::vector<int> perm(n);
    for (int p = 0; p < n; ++p) { perm[p] = p; }
    if (n <= 7)
    {
        // next_permutation from the identity walks every permutation in lexicographic order.
        while (std::next_permutation(perm.begin(), perm.end())) { add(perm); }
    }
    else
    {
        for (int from = 0; from < n; ++from)
        {
            for (int to = 0; to < n; ++to)
            {
                if (to == from) { continue; }
                std::vector<int> m(perm);
                const int x = m[from];
                m.erase(m.begin() + from);
                m.insert(m.begin() + to, x);
                add(m);
            }
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b)
    {
        if (a.k1 != b.k1) { return a.k1 < b.k1; }
        if (a.k2 != b.k2) { return a.k2 < b.k2; }
        return a.perm < b.perm;
    });
    std::vector<int> trial(n);
    for (const Cand& c : cands)
    {
        for (int p = 0; p < n; ++p) { trial[p] = order[c.perm[p]]; }
        if (trial == hoisted && has_maker) { continue; }   // already judged above
        if (pays(trial))
        {
            order = trial;
            g_po_stats.perm[site].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_po_stats.none[site].fetch_add(1, std::memory_order_relaxed);
}

void ApplyPayableCastOrder(const GameState& state, const std::vector<Action>& acts, std::vector<int>& order)
{
    if (!PayableOrderOn()) { ApplyEtbTreasureFundingOrder(state, acts, order); return; }
    PayableCastOrderCore(state, acts, order, nullptr, 0, /*site=*/0);
}

void ApplyPayableCastOrderAt(const GameState& state, const std::vector<Action>& acts,
                             std::vector<int>& order, const std::vector<InternedName>& hand_exits,
                             int extra_wild)
{
    if (!PayableOrderOn()) { ApplyEtbTreasureFundingOrder(state, acts, order); return; }
    PayableCastOrderCore(state, acts, order, &hand_exits, extra_wild, /*site=*/1);
}

void ApplyCastOrderRangeLadder(const GameState& state, const std::vector<Action>& acts,
                               std::vector<int>& order)
{
    // Entered by the global measurement arm (MTG_ORDER_RANGE) or by a provider that adopted the
    // reviewed full order (OrderOpaqueCastsByRank -- Mirrorwing's MTG_MW_ORDERED carries its
    // Gold Rush funding ladder through here).
    if ((!CastOrderRangeEnabled() && !ResolveProvider(state).OrderOpaqueCastsByRank())
        || order.size() < 2) { return; }

    // Every ordered cast's rank LADDER, keyed by ACTION index (the order vector is permuted
    // below, so positions are not a stable key). rungs[i][0] is the preferred key; step[i]
    // walks down the list. Two shapes feed it:
    //   * the ideal -> cost-efficient RANGE (a draw promoted early, walked LATER when its early
    //     position starves the line) -- the original two-point ladder;
    //   * a provider FUNDING ladder (CastOrderFallbackRanks -- a producer preferred LATE, walked
    //     EARLIER when the line starves without its output; Mirrorwing's Gold Rush 15->13->6).
    std::vector<std::vector<int>> rungs(acts.size());
    std::vector<int>              step(acts.size(), 0);
    bool any_ranged = false;
    for (int i : order)
    {
        const Action& a = acts[i];
        const CardDefinition* d = OrderDefOf(a);   // Garth activation ranks as its copy
        const std::vector<int> fb =
            d ? ResolveProvider(state).CastOrderFallbackRanks(state, *d) : std::vector<int>{};
        if (!fb.empty())
        {
            // Both ends carry the SAME phase term, so folding it in preserves the span exactly.
            for (int r : fb) { rungs[i].push_back(CastOrderKey(state, d, r)); }
        }
        else
        {
            const CastOrderRange r = d ? CastOrderRangeOf(state, *d) : CastOrderRange{ 20, 20 };
            rungs[i].push_back(CastOrderKey(state, d, r.ideal));
            if (r.Ranged()) { rungs[i].push_back(CastOrderKey(state, d, r.cost_efficient)); }
        }
        if (rungs[i].size() > 1) { any_ranged = true; }
    }
    // MTG_ORDER_RANGE_PROBE: one line per invocation, so "the ladder never fired" can be told
    // apart from "the ladder ran and the ideal order paid" -- the two look identical in play and
    // mean opposite things about whether the lever has a domain at all.
    static const bool s_probe = EnvOn("MTG_ORDER_RANGE_PROBE");
    auto probe = [&](const char* what)
    { if (s_probe) { std::fprintf(stderr, "[order-range] turn=%d n=%d %s\n",
                                  state.turn_number, static_cast<int>(order.size()), what); } };

    // No spell in this set has a range -> its order is already the only one the principles allow,
    // and today's sort produced it. Byte-identical, and the common case (this is the early-out
    // that keeps the ladder off the hot path).
    if (!any_ranged)                                   { probe("skip: no ranged spell");  return; }
    if (!LadderProjectable(state, acts, order))        { probe("skip: not projectable");  return; }
    probe("enter");
    const std::vector<int> base = order;   // re-sorted from here every rung, so "stable => plan
                                           // order breaks ties" keeps meaning plan order
    auto eff       = [&](int i) { return rungs[i][step[i]]; };
    auto can_step  = [&](int i) { return step[i] + 1 < static_cast<int>(rungs[i].size()); };
    // Total demotion steps available bounds the walk (one step per iteration that fails).
    std::size_t max_steps = 1;
    for (int i : base) { max_steps += rungs[i].size() - 1; }

    // HOLD-AWARE PROJECTION (MTG_MINT_CREDIT_EXACT). "Unpayable" above means "the whole untapped
    // board cannot pay" -- so a line the board pays only by TAPPING THE PUMP TARGET reads as paying
    // at its ideal rung, and the funding spell whose output would have spared that body is never
    // walked. Mirrorwing seed 700628 T3, {Frontline Heroism, Gold Rush -> Mystic, Oracle's
    // Restoration} on Forest + Needle(RR) + Forest + two Mystics: six board mana for six pips, so
    // the reviewed order (Oracle's 14, Gold Rush 15) pays and Oracle's takes the last Forest;
    // Gold Rush then has only the two Mystics and taps the target, which forfeits its own +6
    // (17 damage where the mint breakpoint's Heroism-then-Gold-Rush order realises 23). Gold
    // Rush first mints the Treasures that pay Oracle's and the drawn Draught, and the target stays
    // up. The whole-turn prepay judges every hold rung the same way (and declines this line to
    // the per-cast payer -- PP_MINT_HOLD); the ORDER is what has to change, so the ladder first
    // walks against the board WITHOUT the held target and only falls back to the plain projection
    // when no rung pays with the hold. Same "a wrong answer picks a DIFFERENT legal order" safety
    // as FirstUnpayablePos itself: pass 1 is exactly today's walk. Scoped to a line that mints and
    // may crack (MintLineCanCrack -- the only shape whose funding rung can spare the body) and to
    // a target that is actually a mana source (else the hold changes nothing and pass 0 would just
    // repeat pass 1). Lever off -> hold == nullptr -> the single plain pass, byte-identical.
    const Permanent* hold = nullptr;
    if (MintCreditExactOn() && PumpTargetHoldEnabled() && g_tap_keep_last_card != 0
        && MintLineCanCrack(state, acts))
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index == state.active_player_index && !p.tapped
                && p.card.m_number == g_tap_keep_last_card) { hold = &p; break; }
        }
        if (hold != nullptr
            && AvailableManaPool(state, hold).Total() == AvailableManaPool(state).Total())
        { hold = nullptr; }   // not a mana source: nothing to hold
    }
    for (int pass = (hold != nullptr ? 0 : 1); pass < 2; ++pass)
    {
    const Permanent* skip = (pass == 0) ? hold : nullptr;
    std::fill(step.begin(), step.end(), 0);
    for (std::size_t rung = 0; rung < max_steps + 1; ++rung)
    {
        order = base;
        std::stable_sort(order.begin(), order.end(), [&](int x, int y)
        { return CastOrderLessRanked(state, acts[x], eff(x), acts[y], eff(y)); });

        const int fail = FirstUnpayablePos(state, acts, order, skip);
        if (fail < 0)
        {
            probe(skip != nullptr
                      ? (rung == 0 ? "ideal order pays (target held)" : "stepped-down order pays (target held)")
                      : (rung == 0 ? "ideal order pays" : "stepped-down order pays"));
            return;   // this rung pays -- the most ideal order that does
        }

        // Walk down the ranged spell CLOSEST to the failure (searching the prefix up to and
        // including the failing cast, which is itself a candidate when it has a range). That is
        // the minimal deviation from ideal that can free the mana the failed cast wanted; one
        // step per spell per iteration, so the walk terminates at the all-fallen rung -- the
        // order the engine used before the promotion existed, which is known to be castable.
        int victim = -1;
        for (int pos = 0; pos <= fail && pos < static_cast<int>(order.size()); ++pos)
        {
            const int i = order[pos];
            if (can_step(i) && rungs[i][step[i] + 1] >= eff(i)) { victim = i; }
        }
        // No prefix victim: a FUNDING spell after the failure whose next rung moves it EARLIER
        // (Gold Rush late -> earlier) can put its output in front of the failing cast. Take the
        // one closest after the failure -- the minimal reorder that can fund it.
        //
        // MTG_MINT_CREDIT_EXACT: the FAILING cast itself is a candidate when it is the funder.
        // {Oracle's Restoration, Gold Rush} on Needle(RR)+Forest(G): Oracle's first takes the
        // Forest, Gold Rush (pos 1 = fail) then has no {G}; the only paying order is Gold Rush
        // first, Oracle's off the Treasure. The prefix loop above only walks a spell LATER and
        // this loop began one past the failure, so the funder was never walked and the ladder
        // stopped at its ideal rung -- the executor then DROPPED the minter the base had priced
        // (700252 T3). The same gap ships with the lever off; it is gated here only so the
        // flag-off binary stays byte-identical.
        if (victim < 0)
        {
            for (int pos = MintCreditExactOn() ? fail : fail + 1;
                 pos < static_cast<int>(order.size()); ++pos)
            {
                const int i = order[pos];
                if (can_step(i) && rungs[i][step[i] + 1] < eff(i)) { victim = i; break; }
            }
        }
        if (victim < 0) { break; }   // nothing left to walk: this is the terminal rung
        if (s_probe)
        {
            std::fprintf(stderr, "[order-range] turn=%d fail_pos=%d demote=%s %d->%d%s\n",
                         state.turn_number, fail, acts[victim].card_name.str().c_str(),
                         eff(victim), rungs[victim][step[victim] + 1],
                         skip != nullptr ? " (target held)" : "");
        }
        ++step[victim];
    }
    // The held pass ended at its terminal rung: no order pays while the target is held, so the
    // plain projection decides (pass 1) -- exactly the walk the line would have had without it.
    }
}

// THE accounting mana pool (C1 unit 4). Depletion lands contribute 2, multi-color lands 1 wild,
// filter lands (Cascade Bluffs) 1 wild when fed else 1 {C} -- see AddSourceToPool. Storage lands
// (Dwarven Hold / Mercadian Bazaar) yield their LIVE storage_counters via PermanentManaYield (0
// when uncharged): a dead sc=0 storage land must add nothing, not its static per-tap 1 (the
// rollout once over-credited dead storage lands vs the executor's Firebreathe, projecting the
// Dragonstorm combo kill a turn early -- fd-diverge). For non-storage sources PermanentManaYield
// == ManaProducedPerTap. The turn-scoped reserve (ritual float + retained over-production) is
// spendable on later same-phase casts, so it counts toward affordability; empty for non-floating
// decks, and MTG_NO_FLOAT_LEFTOVER restores the legacy board-only pool. Was a byte-identical twin
// pair (TurnSolver's BuildPool / AIEngine::BuildAvailableMana).
ManaPool AvailableManaPool(const GameState& state, const Permanent* skip)
{
    ManaPool pool;
    int gy_fuel = -1;   // Deathrite fuel: lazily counted, decremented per credited source
    const ManaGrant grant = LiveManaGrant(state, state.active_player_index);
    for (const Permanent& p : state.battlefield)
    {
        if (&p == skip) { continue; }   // "as if this source were tapped" (see the header note)
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        // Brightcap Badger's grant: a granted body has no CardDefinition, so this `continue`
        // was blind to it. ManaDefOf hands back the shared synthetic ManaDork face; the loop body
        // below is unchanged. Free when no grant is live.
        const CardDefinition* def = ManaDefOf(state, p, grant);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || def->params.mana_rock
                    || PaySacSpendableNow(state, p, *def);   // §2a (fresh-hold aware)
        if (!is_land && !is_dork) { continue; }
        // Deathrite: credit at most #graveyard-lands such sources (fuel-counted, lazily).
        if (def->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, state.active_player_index); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        AddSourceToPool(pool, state, *def, PermanentManaYield(state, p, *def), &p);
    }
    // §2b: bodies a live sac-for-mana outlet can eat are supply too (MTG_SAC_OUTLET_PAY). Inert --
    // one null test -- with the lever off or no outlet on the board.
    AddSacPayFodderToPool(pool, state, state.active_player_index,
                          LiveSacPayOutlet(state, state.active_player_index), skip);
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
    // The creature-only reserve is supply for the CREATURE side of the split (this pool is the total
    // pool; BuildNonCreaturePool never credits it, exactly as it drops creature_mana_only sources).
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_creature_mana); }
    // The big-spell-only reserve is supply for the QUALIFYING casts of a subset; the enumerator's
    // BigOnlySubsetPayable takes it back out for everything else (BuildBigOnlyCtx credits it).
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_bigspell_mana); }
    return pool;
}

ManaPool AvailableManaPoolNoAttackers(const GameState& state)
{
    // Same accounting as AvailableManaPool above, minus creature sources whose tap would cost a
    // real attack (CanAttackFull + effective power > 0, lord/domain bonus included). See the
    // header note; keep the two loops in lockstep when either changes.
    ManaPool pool;
    int gy_fuel = -1;
    const int active = state.active_player_index;
    const ManaGrant grant = LiveManaGrant(state, active);
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        // Brightcap Badger's grant: a granted body has no CardDefinition, so this `continue`
        // was blind to it. ManaDefOf hands back the shared synthetic ManaDork face; the loop body
        // below is unchanged. Free when no grant is live.
        const CardDefinition* def = ManaDefOf(state, p, grant);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || def->params.mana_rock
                    || PaySacSpendableNow(state, p, *def);   // §2a lockstep with AvailableManaPool (a Treasure never attacks)
        if (!is_land && !is_dork) { continue; }
        if (def->card.IsCreature() && CanAttackFull(p, state.battlefield, active))
        {
            const int power = p.EffectivePower()
                + ComputeLordBonus(p.card, state, active,
                                   /*all_creature_types=*/false, &p).first;
            if (power > 0) { continue; }   // its tap costs an attack -> not in this pool
        }
        if (def->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, active); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        AddSourceToPool(pool, state, *def, PermanentManaYield(state, p, *def), &p);
    }
    // §2b, minus the bodies whose loss would cost a real attack -- the same exclusion this pool
    // applies to a creature mana source, applied to fodder.
    AddSacPayFodderToPool(pool, state, active, LiveSacPayOutlet(state, active),
                          /*skip=*/nullptr, /*no_attackers=*/true);
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_creature_mana); }   // see AvailableManaPool
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_bigspell_mana); }   // see AvailableManaPool
    return pool;
}

// ---- Colour-exact subset affordability (MTG_COLOR_EXACT) --------------------------------------
// Rationale, soundness argument and the over-approximation policy: see ManaPayment.h.

// ADOPTED 2026-08-18 -- default ON, off-switch MTG_COLOR_EXACT=0 for the standing A/B. Held out on
// disjoint seeds three times over: smoke -0.0800 / 36 keys, regression -0.1353 / 60, overnight
// -0.3699 / 144 with only three keys worse and none by more than +0.0015. Soundness is measured, not
// argued: MTG_COLOR_EXACT_PROBE re-tests every rejection against the real payment path and found
// zero false rejects in 37k+ rejections.
static bool ColorExactEnabled()
{
    static const bool v = EnvOn("MTG_COLOR_EXACT", true);
    return v;
}

// Mirrors TurnSolver's s_cco_noncreature_pool, which decides whether the NON-CREATURE flat pool books
// a colored_creature_only source as {C} (correct) or as wild (the historical over-credit). The colour
// model must follow whichever the pool used, or the two would disagree about the same board.
// MTG_COLOR_SEQ: charge a plan's producers' own cost against the BOARD before spending what they
// make. Default off -- it is a strict tightening of an already-adopted gate, so it changes play.
static bool SeqProducerCreditEnabled()
{
    static const bool v = EnvOn("MTG_COLOR_SEQ");
    return v;
}

static bool CcoNoncreaturePoolEnabled()
{
    static const bool v = EnvOn("MTG_CCO_NONCREATURE_POOL");
    return v;
}

ManaPool PoolCredit(const ManaPool& base, const ManaPool& eff)
{
    ManaPool c;
    c.white     = std::max(0, eff.white     - base.white);
    c.blue      = std::max(0, eff.blue      - base.blue);
    c.black     = std::max(0, eff.black     - base.black);
    c.red       = std::max(0, eff.red       - base.red);
    c.green     = std::max(0, eff.green     - base.green);
    c.colorless = std::max(0, eff.colorless - base.colorless);
    c.wild      = std::max(0, eff.wild      - base.wild);
    return c;
}

ColorFeasibility BuildColorFeasibility(const GameState& state, bool noncreature,
                                       const Permanent* skip)
{
    ColorFeasibility f;
    if (!ColorExactEnabled()) { return f; }
    // A SCALED land (Three Tree City: "{2},{T}: add N of a chosen colour", N = creatures you control)
    // is the one conversion shape with no yield ceiling this model can bound -- ScaledManaNetYield
    // reports the NET over its basic {C} tap, not the gross, and under-crediting supply is the one
    // error that turns into a false reject. Stand the whole test down on such a board and leave it to
    // SubsetPayableWithFilters. No suite deck plays one, so this is inert today.
    const int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && IsScaledManaLand(*d)) { return f; }
        // Scaled mana DORK (Priest of Titania / Elvish Archdruid): its yield grows as elves cast
        // EARLIER IN THE SAME PLAN resolve, so the build-time credit can under-count supply -- the
        // one error that turns into a false reject. Same stand-down as the scaled land.
        if (d && IsScaledManaDork(*d)) { return f; }
        // Untap-land with a live burst (Wirewood Lodge + a 2+ scaled Elf, incl. a TAPPED one this
        // loop's untapped-only walk would miss): converts {C}-class supply into feed-colour supply
        // this model does not track -- same stand-down as the scaled shapes.
        if (d && d->params.untap_creature_cost.has_value()
            && UntapLandBurstNet(state, active, *d) > 0) { return f; }
    }

    // STATIC, like every other flag read in this file: BuildColorFeasibility runs per candidate
    // subset, so a live EnvOn() here re-walked `environ` on every call -- measured (callgrind
    // 2026-09-11) at 0.25-0.88% of an in-game run's TOTAL instructions across four decks, for a
    // value that cannot change inside a process.
    static const bool widen = EnvOn("MTG_DOMAIN_WIDEN", true);   // mirrors TurnSolver's DomainWidenEnabled
    bool has_multi   = false;
    int  gy_fuel     = -1;   // Deathrite fuel, counted lazily exactly as AvailableManaPool does

    auto add = [&](int mask, int count)
    {
        if (count <= 0) { return; }
        f.total += count;                          // colourless-only units still pay GENERIC
        if (mask == 0) { return; }                 // ... but never a coloured pip
        if ((mask & (mask - 1)) != 0) { has_multi = true; }
        for (unsigned s = 1; s < 32; ++s)
        { if (s & static_cast<unsigned>(mask)) { f.cover[s] += count; } }
    };
    // A FIXED BUNDLE source ("add one mana of EACH of these colours" -- a Karoo, a domain source, a
    // two-colour ramp filter) is NOT n free choices from its colour set. Crediting it as free choices
    // hands the test a second blue off an Izzet Boilerworks that can only ever make one, which is
    // exactly how a phantom survives. One single-colour unit per colour is the honest model, and it
    // is still an over-approximation of nothing -- reality supplies exactly this.
    auto add_one_of_each = [&](int mask)
    {
        if ((mask & (mask - 1)) != 0) { has_multi = true; }   // the SOURCE is still multi-colour
        for (int i = 0; i < 5; ++i) { if (mask & (1 << i)) { ++f.total; } }
        for (unsigned s = 1; s < 32; ++s)
        {
            int n = 0;
            for (int i = 0; i < 5; ++i) { if ((mask & (1 << i)) && (s & (1u << i))) { ++n; } }
            f.cover[s] += n;
        }
    };

    // Same source filter as AvailableManaPool -- the pool this test post-filters must be built from
    // exactly the same permanents, or it would prune lines the flat check paid for off a source it
    // never saw.
    const ManaGrant grant = LiveManaGrant(state, active);
    for (const Permanent& p : state.battlefield)
    {
        if (&p == skip) { continue; }   // "what would still be payable if this source were gone?"
        if (p.controller_index != active || p.tapped) { continue; }
        // Brightcap Badger's grant: a granted body has no CardDefinition, so this `continue`
        // was blind to it. ManaDefOf hands back the shared synthetic ManaDork face; the loop body
        // below is unchanged. Free when no grant is live.
        const CardDefinition* def = ManaDefOf(state, p, grant);
        if (!def) { continue; }
        // The NON-CREATURE pool drops creature-only sources entirely (Ancient Ziggurat) -- mirrors
        // BuildNonCreaturePool, whose flat pool this variant post-filters.
        if (noncreature && def->params.creature_mana_only) { continue; }
        const bool is_land = (def->tmpl == CardTemplate::BasicLand);
        const bool is_dork = (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                          || def->params.mana_rock
                          || PaySacSpendableNow(state, p, *def);   // §2a: see ComputeAvailableColors (fresh-hold aware)
        if (!is_land && !is_dork) { continue; }
        if (def->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, active); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        // A partially-creature-only source (Cavern of Souls) may pay a coloured pip only for a
        // creature spell; its unrestricted mode is "{T}: Add {C}". Mask 0 -> no coloured coverage.
        // Gated exactly as BuildNonCreaturePool gates it, so the two stay consistent by construction.
        if (noncreature && def->params.colored_creature_only && CcoNoncreaturePoolEnabled())
        { continue; }
        // CONVERSION sources (Cascade Bluffs "{U/R},{T}: add two of U/R"; Izzet Signet / Ferrous Lake
        // "{1},{T}: add <produces>"). The flat pool books their NET (+1 wild, or {C} when unfed), which
        // no colour set can express -- but their COLOURS are still a hard bound (a Bluffs can never
        // make white), and that bound is the whole value here. Credit the GROSS yield of their colours
        // and charge nothing for the feed: strictly more supply than reality, hence permissive, hence
        // it can still only prune. This is what lets the gate run on hinata / treasure_hunt at all,
        // where standing down previously left every phantom in place.
        if (IsManaConversionSource(def->params))
        {
            const std::vector<Color>& cprod = EffectiveProduces(state, active, *def);
            int cmask = 0;
            for (Color c : cprod)
            {
                const int ci = static_cast<int>(c);
                if (ci >= 0 && ci < 5) { cmask |= (1 << ci); }
            }
            if (def->params.any_color_filter)
            {
                // "{1},{T}: Add one mana of any color" -- ONE unit, free choice of its colours.
                // (The free {C} mode adds nothing here: this gate only reasons about pips.)
                add(cmask, 1);
            }
            else if (def->params.is_filter)
            {
                // "Add {U}{U}, {U}{R}, or {R}{R}" -- genuinely two FREE choices from its colours.
                add(cmask, 2);
            }
            else if (cprod.size() >= 2)
            {
                // "{1},{T}: Add {U}{R}" -- one of EACH, not two of either. A fixed bundle, so credit
                // it as such: exact, and it does not hand the test a second blue that cannot exist.
                add_one_of_each(cmask);
            }
            else
            {
                add(cmask, 2);            // "{1},{T}: Add {B}{B}" -- two of the one colour
            }
            continue;
        }
        int amt = PermanentManaYield(state, p, *def);
        if (amt < 0) { amt = ManaProducedPerTap(*def); }
        const std::vector<Color>& prod = EffectiveProduces(state, active, *def);
        int mask = 0;
        for (Color c : prod)
        {
            const int ci = static_cast<int>(c);
            if (ci >= 0 && ci < 5) { mask |= (1 << ci); }
        }
        // ONE-COLOUR BURST (Somberwald Sage): amt units of ONE chosen colour -- recorded for
        // Payable's exact colour choice instead of being credited as amt free choices (see the
        // ColorFeasibility note). Still generic supply in `total`. Overflow keeps the old credit.
        if (IsSingleColorBurstSource(*def) && prod.size() > 1 && f.nburst < ColorFeasibility::kMaxBurst)
        {
            f.total += amt;
            if ((mask & (mask - 1)) != 0) { has_multi = true; }
            f.burst_mask[f.nburst] = mask;
            f.burst_amt[f.nburst]  = amt;
            ++f.nburst;
            continue;
        }
        // Domain source (Faeburrow Elder / Bloom Tender): one mana of EACH colour among your
        // permanents -- a fixed bundle, not a free choice. Under MTG_DOMAIN_WIDEN a permanent cast
        // earlier in the SAME plan can widen the set, so open the BUNDLE to all five colours: still
        // one per colour, which is exactly the ceiling reality can reach, and far tighter than the
        // five free choices the old model handed out.
        if (def->params.domain_mana)
        {
            add_one_of_each(widen ? 0x1F : mask);
            continue;
        }
        // A KAROO ("{T}: Add {U}{R}", Izzet Boilerworks) is likewise one of each: its per-tap yield
        // equals its colour count. A plain dual has yield 1 and stays a free choice of one.
        // (MTG_LEGACY_KAROO restores the pre-fix free-choice model here too, so the legacy arm's gate
        //  and payment agree -- otherwise the gate would prune lines that arm can still pay.)
        if (static_cast<int>(prod.size()) > 1 && amt == static_cast<int>(prod.size())
            && def->params.etb_bounce_land && !LegacyKarooPay())
        {
            add_one_of_each(mask);
            continue;
        }
        // Land AURAS. `amt` ALREADY includes the aura's extra mana (PermanentManaYield adds
        // LandAuraBonus), so the count was never the bug -- the COLOUR was: those units were being
        // credited under the HOST's mask, so a Wild Growth on an Adarkar Wastes read as "two mana of
        // W/U/C" instead of "one of W/U/C plus one {G}". Split the credit so each part carries its
        // own colours. Do NOT also add(mask, amt) or the aura's mana is counted twice.
        const int aura_units = LandAuraBonus(state, p);
        const int aura_mask  = LandAuraColorMask(state, p);
        if (aura_units > 0 && aura_mask != 0)
        {
            add(mask, amt - aura_units);   // the host land's own tap
            add(aura_mask, aura_units);    // the aura's additional mana, in the AURA's colour
            continue;
        }
        add(mask, amt);
    }
    // §2b: fodder a live sac-for-mana outlet can eat (MTG_SAC_OUTLET_PAY). Outside the loop above
    // because fodder is not filtered on `tapped` -- a creature that already attacked can still be
    // sacrificed, and this gate must see the same supply the payer does or it prunes a payable line.
    if (const SacPayOutlet so = LiveSacPayOutlet(state, active); so.valid())
    {
        int smask = 0;
        for (Color c : SacPayOutletColors(so.def->params))
        { const int ci = static_cast<int>(c); if (ci >= 0 && ci < 5) { smask |= (1 << ci); } }
        const int per = std::max(1, so.def->params.sac_outlet_add_mana_amount);
        // Free choice of colour per activation, which is exactly what "add one mana of any color"
        // is -- so add(), not add_one_of_each().
        add(smask, SacPayFodderCount(state, active, so, skip) * per);
    }
    // The turn-scoped reserve is spendable on this phase's casts, so it is supply like any other.
    if (FloatLeftoverManaEnabled())
    {
        const ManaPool& fl = state.floating_mana;
        add(1 << 0, fl.white); add(1 << 1, fl.blue);  add(1 << 2, fl.black);
        add(1 << 3, fl.red);   add(1 << 4, fl.green);
        add(0x1F,   fl.wild);
        // The creature-only reserve pays creature spells only: credited to the CREATURE-side test
        // (noncreature == false), never to the noncreature one (mirrors BuildNonCreaturePool).
        if (!noncreature)
        {
            const ManaPool& fc = state.floating_creature_mana;
            add(1 << 0, fc.white); add(1 << 1, fc.blue);  add(1 << 2, fc.black);
            add(1 << 3, fc.red);   add(1 << 4, fc.green);
            add(0x1F,   fc.wild);  add(0,    fc.colorless);
        }
        // The big-spell-only reserve: credited to BOTH sides (a 5+ noncreature spell -- Colossification,
        // Eldrazi Conscription -- may spend it). Permissive is the safe direction for this gate, which
        // only ever prunes; BigOnlySubsetPayable owns the exact restriction.
        {
            const ManaPool& fb = state.floating_bigspell_mana;
            add(1 << 0, fb.white); add(1 << 1, fb.blue);  add(1 << 2, fb.black);
            add(1 << 3, fb.red);   add(1 << 4, fb.green);
            add(0x1F,   fb.wild);  add(0,    fb.colorless);
        }
    }
    // With no multi-colour source the flat pool holds no `wild` from the board and CanPayFlat is
    // already exact per colour -- running the matching could only reach the same verdict.
    f.usable = has_multi;
    // A land AURA still in HAND becomes supply the moment the plan casts it, which this state-only
    // build cannot see (see PendingLandAuraColorMask -- the presence gate has the same blindness).
    // Credited AFTER `usable` is settled on purpose: widening supply can only make this gate prune
    // LESS, whereas letting a pending Aura's multi-colour mask flip `has_multi` would switch the
    // exact test ON for boards where the flat pool is already exact -- a tightening, and the one
    // direction a pre-filter must never move in. Zero mask outside EDF -> byte-identical.
    int pend_units = 0;
    if (const int pend_mask = PendingLandAuraColorMask(state, &pend_units))
    { add(pend_mask, pend_units); }
    return f;
}

void BuildColorDemandIndex(const std::vector<Action>& cands, ColorDemandIndex& out)
{
    out.uniform = false;
    out.mono_mask = 0;
    const int m = static_cast<int>(cands.size());
    unsigned seen = 0;
    for (int j = 0; j < m; ++j)
    {
        const Action& a = cands[j];
        // A hybrid pip is stored baked into its first colour and has to be peeled against the other
        // half -- a two-colour demand by construction. Leave those to the general path.
        if (a.cost.hybrid_count != 0) { return; }
        const int p[5] = { a.cost.white, a.cost.blue, a.cost.black, a.cost.red, a.cost.green };
        for (int i = 0; i < 5; ++i) { if (p[i] > 0) { seen |= 1u << i; } }
        if ((seen & (seen - 1)) != 0) { return; }   // two colours demanded somewhere: not uniform
    }
    int colour = -1;
    for (int i = 0; i < 5; ++i) { if (seen & (1u << i)) { colour = i; break; } }
    out.mono_mask = seen;   // 0 when nothing coloured is cast; then every `pips` below is 0 too
    out.pips.assign(static_cast<size_t>(m), 0);
    out.prod_mv.assign(static_cast<size_t>(m), 0);
    out.flags.assign(static_cast<size_t>(m), 0);
    for (int j = 0; j < m; ++j)
    {
        const Action& a = cands[j];
        if (colour >= 0)
        {
            const int p[5] = { a.cost.white, a.cost.blue, a.cost.black, a.cost.red, a.cost.green };
            out.pips[static_cast<size_t>(j)] = p[colour];
        }
        unsigned char fl = 0;
        if (a.kind == Action::Kind::ActivateVial) { fl |= 1u; }
        if (a.is_noncreature)                     { fl |= 2u; }
        if (a.ritual_float > 0 || a.rock_mana.Total() > 0)
        {
            fl |= 4u;
            out.prod_mv[static_cast<size_t>(j)] = a.cost.ManaValue();
        }
        out.flags[static_cast<size_t>(j)] = fl;
    }
    out.uniform = true;
}

bool ColorFeasibility::Payable(const std::vector<Action>& cands, const std::vector<int>& sel,
                               const ManaPool& credit, bool noncreature_only,
                               const ColorDemandIndex* idx) const
{
    // UNIFORM fast path -- identical arithmetic to the general path below, reading the hoisted
    // arrays instead of re-walking 384-byte Actions. With one demand mask the Hall scan has exactly
    // one binding set (see the union argument there), so the whole test is one comparison.
    if (idx && idx->uniform)
    {
        int prod = 0;
        for (int j : sel) { prod += idx->prod_mv[static_cast<size_t>(j)]; }
        if (!SeqProducerCreditEnabled()) { prod = 0; }
        int need = 0;
        for (int j : sel)
        {
            const unsigned char fl = idx->flags[static_cast<size_t>(j)];
            if (fl & 1u) { continue; }                                   // ActivateVial: no mana cost
            if (noncreature_only && !(fl & 2u)) { continue; }
            if (prod > 0 && (fl & 4u)) { continue; }                     // charged via `prod` instead
            need += idx->pips[static_cast<size_t>(j)];
        }
        if (need < 2) { return true; }          // also covers mono_mask == 0 (nothing coloured cast)
        const unsigned s = idx->mono_mask;
        int have = cover[s] + credit.wild;
        // One demanded colour: a one-colour burst simply picks it (exact).
        for (int b = 0; b < nburst; ++b) { if (static_cast<unsigned>(burst_mask[b]) & s) { have += burst_amt[b]; } }
        const int cred_u[5] = { credit.white, credit.blue, credit.black, credit.red, credit.green };
        for (int i = 0; i < 5; ++i) { if (s & (1u << i)) { have += cred_u[i]; } }
        if (prod > 0) { have -= std::max(0, prod - (total - cover[s])); }
        return need <= have;
    }
    // Demands, keyed by the MASK of colours that may pay them. At most nine distinct masks (five
    // singletons plus up to four hybrid pairs), so the Hall scan below stays a short walk.
    int masks[16]; int counts[16]; int ndm = 0;
    int total_pips = 0;
    auto demand = [&](int mask, int n)
    {
        if (mask == 0 || n <= 0) { return; }
        total_pips += n;
        for (int i = 0; i < ndm; ++i) { if (masks[i] == mask) { counts[i] += n; return; } }
        // Unreachable at 16 (five singletons + at most four hybrid pairs = nine), and dropping a
        // demand only ever makes the test PASS, so an overflow could not turn into a false reject.
        if (ndm < 16) { masks[ndm] = mask; counts[ndm] = n; ++ndm; }
    };

    // SEQUENCED PRODUCER CREDIT (MTG_COLOR_SEQ). `credit` holds what the plan's own producers make,
    // and the caller may spend it freely -- but a producer's output does not exist until its own cost
    // is PAID, and that cost comes from the board. Hinata turn 1: {Sol Ring, Ponder} off a lone
    // Forbidden Orchard is admitted today because the Orchard covers Ponder's {U} and Sol Ring's
    // {C}{C} covers the total -- except the Orchard is also the only thing that can pay Sol Ring's
    // {1}, and {C}{C} can never pay {U}. Unpayable in either order, enumerated anyway, one cast then
    // silently dropped.
    //
    // The bound: paying the producers takes `prod_cost` units from the BOARD. Units outside a colour
    // set S can absorb at most (total - cover[S]) of that, so at least the remainder must come out of
    // S itself. Deduct it. Producers' own coloured pips are left OUT of the demand and their whole
    // mana value charged here instead, which under-constrains their colours -- permissive, so this
    // can still only prune. Zero producers => zero deduction => byte-identical.
    int prod_cost = 0;
    for (int j : sel)
    {
        const Action& a = cands[j];
        if (a.ritual_float > 0 || a.rock_mana.Total() > 0) { prod_cost += a.cost.ManaValue(); }
    }
    if (!SeqProducerCreditEnabled()) { prod_cost = 0; }

    for (int j : sel)
    {
        const Action& a = cands[j];
        if (a.kind == Action::Kind::ActivateVial) { continue; }   // no mana cost (mirrors SubsetPayable)
        // The non-creature variant asks the narrower question the flat pool asks: can the NONCREATURE
        // casts alone be paid without the creature-only sources? Same split as noncreature_combined.
        if (noncreature_only && !a.is_noncreature) { continue; }
        // A producer's own pips are charged via prod_cost above, not here (see the note).
        if (prod_cost > 0 && (a.ritual_float > 0 || a.rock_mana.Total() > 0)) { continue; }
        int pips[5] = { a.cost.white, a.cost.blue, a.cost.black, a.cost.red, a.cost.green };
        // Un-bake hybrid pips: Card.h stores each in its FIRST colour's flat int, so reading the
        // flat pips alone would demand that colour and false-reject a subset the other half pays
        // (Deathrite Shaman {B/G} off a green board). Same peel as SubsetPayable.
        for (int h = 0; h < a.cost.hybrid_count; ++h)
        {
            const int c1 = a.cost.hybrid_pair[h] >> 4;
            const int c2 = a.cost.hybrid_pair[h] & 0xF;
            int mask = 0;
            if (c1 >= 0 && c1 < 5) { --pips[c1]; mask |= (1 << c1); }
            if (c2 >= 0 && c2 < 5) { mask |= (1 << c2); }
            demand(mask, 1);
        }
        for (int i = 0; i < 5; ++i) { demand(1 << i, pips[i]); }
    }
    // One coloured pip is decided by PRESENCE alone, which SubsetPayable already tested.
    if (total_pips < 2) { return true; }

    const int cred[5] = { credit.white, credit.blue, credit.black, credit.red, credit.green };
    // Hall scan over the colour sets that can BIND -- and only a UNION OF DEMAND MASKS can. For any
    // set S, let S' be the union of the masks contained in S. Every mask inside S is inside S' and
    // vice versa, so need(S') == need(S) exactly; and have() is non-decreasing in S, because adding
    // a colour adds cover[] and cred[] while the producer deduction can grow by at most the cover
    // gain (max(0,x+d) - max(0,x) <= d), leaving the credit gain. So need(S) > have(S) implies
    // need(S') > have(S'): scanning the unions alone returns the same verdict as the full 31-set
    // walk, rejecting the same subsets.
    //
    // Why it is worth the branch: `usable` is armed by the SOURCE side alone (has_multi), so a deck
    // that merely OWNS a dual pays the full scan even when nothing it casts has two colours to
    // compete over. A mono-colour demand set is ndm == 1 -- one check instead of 31. Measured on
    // Fungus (mono-green, holding a Simic Growth Chamber and Utopia Mycon): 413M subsets through
    // the full scan to find 17 rejections, with Payable at 19.9% of the game.
    //
    // The closure is only taken while it is provably smaller than the scan it replaces: ndm <= 3
    // bounds it at 2^3-1 = 7 sets. Wider demand sets keep the flat walk.
    unsigned scan[8];
    int      nscan = 0;
    if (ndm <= 3)
    {
        unsigned seen = 0;
        for (int t = 1; t < (1 << ndm); ++t)
        {
            unsigned v = 0;
            for (int i = 0; i < ndm; ++i)
            { if (t & (1 << i)) { v |= static_cast<unsigned>(masks[i]); } }
            if (v != 0 && ((seen >> v) & 1u) == 0) { seen |= 1u << v; scan[nscan++] = v; }
        }
    }
    const int nsets = nscan ? nscan : 31;
    // One Hall scan for a FIXED colour choice of the one-colour bursts (`pick[b]` = the colour index
    // burst b makes, -1 = none demanded). With no burst this is the historical scan verbatim.
    int pick[ColorFeasibility::kMaxBurst];
    auto hall = [&]() -> bool
    {
        for (int k = 0; k < nsets; ++k)
        {
            const unsigned s = nscan ? scan[k] : static_cast<unsigned>(k + 1);
            int need = 0;
            for (int i = 0; i < ndm; ++i)
            { if ((static_cast<unsigned>(masks[i]) & ~s) == 0) { need += counts[i]; } }   // payable only from s
            if (need == 0) { continue; }
            int cov = cover[s];
            for (int b = 0; b < nburst; ++b) { if (pick[b] >= 0 && (s & (1u << pick[b]))) { cov += burst_amt[b]; } }
            int have = cov + credit.wild;
            for (int i = 0; i < 5; ++i) { if (s & (1u << i)) { have += cred[i]; } }
            // What the producers must draw out of S itself (see the note above).
            if (prod_cost > 0) { have -= std::max(0, prod_cost - (total - cov)); }
            if (need > have) { return false; }
        }
        return true;
    };
    if (nburst == 0) { return hall(); }
    // ONE-COLOUR BURSTS: the subset is payable iff SOME choice of one colour per burst passes Hall.
    // Only colours the subset actually demands can help (a burst into an undemanded colour adds only
    // generic supply, already in `total`), so each burst's options are mask & demanded, or {none}.
    unsigned demanded = 0;
    for (int i = 0; i < ndm; ++i) { demanded |= static_cast<unsigned>(masks[i]); }
    int opts[ColorFeasibility::kMaxBurst][5]; int nopt[ColorFeasibility::kMaxBurst];
    for (int b = 0; b < nburst; ++b)
    {
        nopt[b] = 0;
        for (int c = 0; c < 5; ++c)
        { if ((static_cast<unsigned>(burst_mask[b]) & demanded) & (1u << c)) { opts[b][nopt[b]++] = c; } }
        if (nopt[b] == 0) { opts[b][0] = -1; nopt[b] = 1; }
    }
    int odo[ColorFeasibility::kMaxBurst] = {0};
    for (;;)
    {
        for (int b = 0; b < nburst; ++b) { pick[b] = opts[b][odo[b]]; }
        if (hall()) { return true; }
        int b = 0;
        while (b < nburst && ++odo[b] >= nopt[b]) { odo[b] = 0; ++b; }
        if (b == nburst) { return false; }
    }
}

// Plan-scoped source reservation (see g_plan_reserved_sources). Stored as CARD NUMBERS, not
// battlefield indices: a main phase can push new permanents and remove others (a sacrifice, a Karoo
// bounce), so an index captured before the casts is not stable across them. Resolved to the
// bitmask the payment path wants here, where the board is in scope. Empty vector -> 0 -> the
// reserve-then-fallback below is byte-identical to before for every other plan.
thread_local std::vector<int> g_plan_reserved_sources;

// Line-scoped unpaid cost (see the header). Zero unless a plan application set it.
thread_local ManaCost g_line_unpaid_cost;
thread_local ManaCost g_act_line_paid;      // see ManaPayment.h (ActLinePassScope / ActLinePayScope)
thread_local ManaCost g_act_line_current;

void AddManaCost(ManaCost& dst, const ManaCost& add)
{
    dst.generic   += add.generic;   dst.white += add.white; dst.blue  += add.blue;
    dst.black     += add.black;     dst.red   += add.red;   dst.green += add.green;
    dst.colorless += add.colorless;
}

void SubManaCost(ManaCost& dst, const ManaCost& paid)
{
    auto s = [](int& d, int p) { d = std::max(0, d - p); };
    s(dst.generic, paid.generic); s(dst.white, paid.white); s(dst.blue, paid.blue);
    s(dst.black, paid.black);     s(dst.red, paid.red);     s(dst.green, paid.green);
    s(dst.colorless, paid.colorless);
}

ManaCost SinkCostWithLineHold(const ManaCost& own)
{
    // HUMAN PLAY ONLY, and the A/B is why (1800 games, slivers, seeds 2002/3003/4004, hold ON minus
    // hold OFF): +0.0484 / +0.0150 / +0.0233 turns -- the hold is WORSE on every seed. Greedy-max
    // replicate wins on average even when it eats a co-planned cast, because a replicate token is a
    // lord-buffed Sliver body and the card it squeezes out usually is not worth more. The USER's
    // instinct ("I can't think of an advantage to not replicating if we have the extra mana") is
    // right for the AI, and then some.
    //
    // It is NOT right for a HUMAN's declared line. There the casts are an intent, not a heuristic:
    // silently converting "cast Hatchery Sliver AND Thrumming Hivepool" into "cast the Sliver and
    // make tokens" is the reported bug, and it also makes the replicate dialog's offered 0..max a
    // lie (the max is only reachable by dropping something the human asked for). So the hold binds
    // exactly where a declared line exists, and autonomous play -- and therefore all ground truth --
    // is untouched by construction.
    //
    // MTG_LINE_HOLD=1 forces it on (the arm that produced the numbers above); MTG_NO_LINE_HOLD=1
    // forces it off, including in human play.
    static const bool s_no_hold    = EnvOn("MTG_NO_LINE_HOLD");
    static const bool s_force_hold = EnvOn("MTG_LINE_HOLD");
    if (s_no_hold) { return own; }
    if (!s_force_hold && !HumanPlayActive()) { return own; }
    ManaCost c = own;
    AddManaCost(c, g_line_unpaid_cost);
    return c;
}

static std::uint64_t PlanReserveMask(const GameState& state)
{
    if (g_plan_reserved_sources.empty()) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    std::uint64_t mask = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        for (int num : g_plan_reserved_sources)
        { if (p.card.m_number == num) { mask |= (1ull << i); break; } }
    }
    return mask;
}

// Per-payment ONE-SHOT hold (MTG_ONESHOT_RESERVE, §2b "waste is the trigger" -- see
// docs/design/mana-order-and-reserve-overhaul.md layer 2). The whole-turn ladder in
// BatchPrepayMainCasts covers multi-cast turns, but single-cast turns DECLINE the prepay
// (PP_FEW_CASTS, the majority shape) and the §2a rank-26 tier would then spend a pay-sac Treasure
// EAGERLY -- ahead of tri/rainbow lands -- on a turn with slack. Same reserve-then-fallback
// soundness as ReservableSpecialMask: the held attempt runs first, and a payment that genuinely
// needs the one-shot releases it via the unrestricted retry. The provider bias (SpendOneShotsFreely,
// e.g. Mirrorwing's go-off turns) zeroes the hold for the whole plan. Inert unless BOTH
// MTG_ONESHOT_RESERVE and MTG_TREASURE_PAY_SOURCE are on.
static std::uint64_t OneShotHoldMask(const GameState& state)
{
    if (!OneShotReserveEnabled() || !TreasurePaySourceEnabled()) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    const PlanTraits* pt = CurrentPlanTraits();
    // The per-payment hold fires ONLY under LIVE traits on a SINGLE-mana-cast plan -- where this
    // one payment IS the turn's whole demand, so "payable without the one-shot" is judged against
    // the turn and the hold is sound. Everywhere else it must stay out:
    //   * multi-cast plans: each cast paying "without the one-shot" locally is the retired
    //     MTG_RESERVE stranding (judged per payment, not per turn) -- the whole-turn prepay
    //     ladder owns those turns with ONE joint solve;
    //   * null traits (search-interior payments outside a plan apply): a bare hold here distorts
    //     the ROLLOUT'S simulated futures -- traced on MW gi75 (T+O, unbounded d5): the deep
    //     rollouts under the hold flipped a near-tie T2 pick (Hierarch -> Mystic) whose line then
    //     never assembles the T4 double-Gold-Rush kill. Conservative null-scope = exactly the
    //     PlanTraits contract ("null -> behave exactly as before").
    if (!pt || pt->mana_casts >= 2) { return 0; }
    if (ResolveProvider(state).SpendOneShotsFreely(state, *pt)) { return 0; }
    const int active = state.active_player_index;
    std::uint64_t mask = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && IsPaySacSource(*d)) { mask |= (1ull << i); }
    }
    return mask;
}

// M2-PAYLOAD RESERVE (MTG_M2_PAYLOAD_RESERVE, DEFAULT ON, adopted 2026-08-26 (=0 disables); overhaul
// ledger "fc96 s4"). The pre-combat payment is otherwise blind to the POST-combat payload in hand:
// fc96's T4 casts Hellkite, and under MTG_DORK_TAP_LAST's lands-first order the payment leaves
// Faeburrow+Deathrite = 6 post-combat mana where Unite the Coalition needs 7 -- while an EQUALLY
// LEGAL payment (spend Deathrite + the R land, spare two lands) leaves exactly 7 with all five
// colours via Faeburrow's widened domain. No plan/branch can recover that: every enumerated line
// is priced under the committed payment, so the m2 kill is foreclosed before the m2 is asked.
//
// The rule: while a PRE-COMBAT plan apply is paying (live traits, !main2), find the best hand card
// the plan does NOT cast that the leftover pool could still cast post-combat (the payload), and
// reserve a source set that keeps it payable: a DOMAIN dork as the colour anchor (its post-cast
// yield/colours include the plan's own permanent casts -- PlanTraits::cast_color_mask, computed by
// the one shared builder so executor and rollout cannot drift), plus plain untapped lands to cover
// the rest of its mana value. Same reserved-first / unrestricted-fallback soundness as every other
// mask here: a payment that genuinely needs the reserved sources gets them back on the retry, so no
// cast is ever lost -- the reserve only picks WHICH legal payment is committed.
//
// Scope guards (each load-bearing):
//  * null traits -> 0: search-interior payments outside a plan apply stay untouched (the MW gi75
//    rollout-distortion trap, same contract as OneShotHoldMask).
//  * main2 -> 0: in the post-combat main the payload is being cast (or not) NOW; holding for it is
//    meaningless there.
//  * requires a DOMAIN anchor whose post-cast colours cover the payload's pips: without one the
//    right reserve set is a joint colouring problem this greedy cannot solve; conservative 0 keeps
//    every non-domain deck byte-identical.
//  * NO cast is deferred or dropped by this rule -- a Greaves-hasted m1 Garth line (USER caveat,
//    2026-08-26) is still enumerated, paid and scored exactly as before; win-turn selection, not a
//    phase rule, decides between it and the hold-Garth line.
static std::uint64_t PayloadReserveMask(const GameState& state)
{
    static const bool s_on = EnvOn("MTG_M2_PAYLOAD_RESERVE", true);
    if (!s_on) { return 0; }
    const PlanTraits* pt = CurrentPlanTraits();
    if (!pt || pt->main2) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    const int active = state.active_player_index;

    // Post-cast domain: today's colours plus the plan's permanent casts'.
    int domain_mask = 0;
    for (Color c : DomainColors(state, active)) { domain_mask |= (1 << static_cast<int>(c)); }
    domain_mask |= pt->cast_color_mask;

    // One pass over the untapped sources: total yield (feasibility precheck), the best domain
    // anchor (max post-cast yield, first-index tiebreak), and the plain-land filler candidates.
    int total_yield = 0, anchor = -1, anchor_yield = 0;
    bool anchor_vig = false;
    int lands[64]; int n_lands = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool dork = d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)
                          && GraveyardFuelLive(state, active, *d);
        const bool land = p.card.IsLand();
        if (!dork && !land && !d->params.mana_rock) { continue; }
        int y = PermanentManaYield(state, p, *d);
        if (d->params.domain_mana && dork)
        {
            // Widened post-cast yield: one mana of each colour the m2 board will show.
            int wide = 0;
            for (int ci = 0; ci < 5; ++ci) { if (domain_mask & (1 << ci)) { ++wide; } }
            y = wide;
            // Anchor preference: VIGILANT first, then yield. A vigilant domain dork does double
            // duty -- it attacks without tapping and its mana still pays the m2 payload (the
            // USER's Faeburrow doctrine) -- so reserving the non-vigilant twin instead (Bloom
            // over Faeburrow, fc96's first-build failure) pushes the vigilant body into the m1
            // payment and forfeits its attack for nothing.
            const bool vig = p.card.HasKeyword(Keyword::Vigilance);
            if ((vig && !anchor_vig) || (vig == anchor_vig && y > anchor_yield))
            { anchor = i; anchor_yield = y; anchor_vig = vig; }
        }
        if (y > 0) { total_yield += y; }
        if (land && !d->params.domain_mana && n_lands < 64) { lands[n_lands++] = i; }
    }
    if (anchor < 0) { return 0; }

    // The payload: a hand card the plan does not cast that converts to DAMAGE on the turn it is
    // cast (a goldfish m2's only same-turn value -- a creature is summoning-sick and a walker's
    // loyalty is next-turn value, so reserving for those holds mana that buys nothing this turn;
    // the first build's max-MV selector picked Nicol Bolas over Unite for exactly that trap),
    // highest MV among those, whose coloured pips the anchor's post-cast domain covers and whose
    // cost still fits the pool after the m1 spends. Copies beyond the plan's cast count still
    // qualify (name-count exclusion).
    int consumed[PlanTraits::kMaxCastNames] = {};
    const Card* payload = nullptr;
    int payload_mv = 0;
    for (const Card& c : state.players[active].hand)
    {
        if (c.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        if (d->params.damage <= 0 && d->params.modal_damage_per_choice <= 0) { continue; }
        bool is_cast = false;
        for (int k = 0; k < pt->cast_name_count; ++k)
        {
            if (&c.m_name.str() == pt->cast_names[k] && consumed[k] == 0)
            { consumed[k] = 1; is_cast = true; break; }
        }
        if (is_cast) { continue; }
        const ManaCost& mc = d->card.m_mana_cost;
        if (mc.has_x) { continue; }              // X cost: no fixed payload size to reserve for
        const int mv = mc.ManaValue();
        if (mv <= payload_mv) { continue; }      // keep the biggest payoff
        if (pt->cast_mv_total + mv > total_yield) { continue; }   // cannot fit even before combat
        // Coloured-pip coverage by the anchor's post-cast domain (hybrids: either half).
        const int pips[5] = { mc.white, mc.blue, mc.black, mc.red, mc.green };
        bool covered = true;
        for (int ci = 0; ci < 5 && covered; ++ci)
        { if (pips[ci] > 0 && !(domain_mask & (1 << ci))) { covered = false; } }
        for (int h = 0; h < mc.hybrid_count && covered; ++h)
        {
            const int c1 = mc.hybrid_pair[h] >> 4, c2 = mc.hybrid_pair[h] & 0xF;
            const bool ok1 = c1 >= 0 && c1 < 5 && (domain_mask & (1 << c1));
            const bool ok2 = c2 >= 0 && c2 < 5 && (domain_mask & (1 << c2));
            if (!ok1 && !ok2) { covered = false; }
        }
        if (!covered) { continue; }
        payload = &c; payload_mv = mv;
    }
    if (payload == nullptr) { return 0; }

    // Reserve = the anchor + plain untapped lands (battlefield order) until the payload's mana
    // value is covered. Lands are the cheapest thing to promise the m2 (a held dork has other
    // uses); if the lands run out the payload provably needed sources the m1 also needs, and the
    // conservative answer is no reserve at all (the fallback would fire anyway).
    std::uint64_t mask = 1ull << anchor;
    int reserved_yield = anchor_yield;
    for (int li = 0; li < n_lands && reserved_yield < payload_mv; ++li)
    {
        mask |= 1ull << lands[li];
        ++reserved_yield;
    }
    if (reserved_yield < payload_mv) { return 0; }
    {   // Diagnostic (MTG_PAYLOAD_TRACE=1, default off): which payload/anchor/mask fired.
        static const bool s_tr = EnvOn("MTG_PAYLOAD_TRACE");
        if (s_tr)
        {
            std::fprintf(stderr, "[pldr] T%d payload=%s mv=%d anchor=%s mask=%llx\n",
                         state.turn_number, payload->m_name.c_str(), payload_mv,
                         state.battlefield[anchor].card.m_name.c_str(),
                         static_cast<unsigned long long>(mask));
        }
    }
    return mask;
}

// SCARCE-COLOR HOLD (MTG_SCARCE_COLOR_HOLD -- see ScarceColorHoldEnabled in SpellEffects.h for the
// mw326 trace). While a plan apply is paying ONE cast of a multi-cast plan, hold every untapped
// source that is a SCARCE provider of a colour the plan's OTHER casts still need: if the untapped
// providers of colour c number no more than the plan's remaining c-pips beyond this payment's own
// cost, every one of them will be needed, so this payment must route around them if it can. The
// remaining-need estimate is conservative (already-paid casts still count -- an over-hold costs one
// extra solve attempt, never a cast, by the held-first/unrestricted-retry contract). Unlike the
// whole-turn prepay this also protects the shapes the prepay DECLINES -- mw326's joint cost is only
// payable via the mid-turn mint, so the prepay fails up front and only this per-cast hold can keep
// the greedy from burning the lone {R} land on a colour-flexible cost.
static std::uint64_t ScarceColorHoldMask(const GameState& state, const ManaCost& cost)
{
    // SPLIT LEVER (2026-08-26): the mask half sits behind its own opt-in, SEPARATE from the rank
    // half (MTG_SCARCE_COLOR_HOLD). The rank half is the measured mw326 fix; this mask targets
    // the prepay-declined multi-cast class, which has NO motivating case yet -- and the held-out
    // mirrorwing A/B of the combined lever ran net-worse, so the unproven half must be
    // independently measurable (and stays off) until a case motivates it.
    static const bool s_mask_on = EnvOn("MTG_SCARCE_COLOR_MASK");
    if (!s_mask_on) { return 0; }
    const PlanTraits* pt = CurrentPlanTraits();
    if (!pt || pt->mana_casts < 2) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    // Pips the plan's OTHER casts need, beyond this payment's own cost.
    const int cur[5] = { cost.white, cost.blue, cost.black, cost.red, cost.green };
    int need[5]; bool any = false;
    for (int c = 0; c < 5; ++c)
    {
        need[c] = pt->cast_pips[c] - cur[c];
        if (need[c] > 0) { any = true; }
    }
    if (!any) { return 0; }
    const int active = state.active_player_index;
    // Untapped providers per colour (each source counted once per colour it can make).
    int cnt[5] = {};
    std::uint64_t prov[5] = {};
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool dork = d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)
                          && GraveyardFuelLive(state, active, *d);
        if (!dork && !p.card.IsLand() && !d->params.mana_rock) { continue; }
        int seen = 0;
        for (Color c : EffectiveProducesFor(state, active, *d, &p))
        {
            const int ci = static_cast<int>(c);
            if (ci >= 5 || (seen & (1 << ci))) { continue; }
            seen |= (1 << ci);
            ++cnt[ci]; prov[ci] |= (1ull << i);
        }
    }
    std::uint64_t mask = 0;
    for (int c = 0; c < 5; ++c)
    {
        if (need[c] > 0 && cnt[c] > 0 && cnt[c] <= need[c]) { mask |= prov[c]; }
    }
    return mask;
}

// THE public payment entry (C1 unit 5) -- reserved-first retry around TapForCostSharedOnce.
// Snapshot everything a payment can touch (incl. the executor's `available` accounting pool, when
// present) so a reserved MISS restores byte-identically before the normal attempt (which must
// reproduce the pre-reservation payment exactly).
static bool TapForCostSharedImpl(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 ManaPool* available, bool honor_legacy_cco);

// Payment NESTING depth (the hybrid wrapper re-enters TapForCostShared once per colour assignment),
// so the damage-event flush below runs once, at the OUTERMOST successful exit -- i.e. when the
// payment has committed. See core/DamageEvents.h.
static thread_local int t_pay_nest = 0;
static bool TapForCostSharedDiag(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 ManaPool* available, bool honor_legacy_cco);

bool TapForCostShared(GameState& state, const ManaCost& cost_in, bool for_creature,
                      ManaPool* available, bool honor_legacy_cco)
{
    // Nesting is counted on BOTH branches now (it used to be the armed one only): the rollback
    // audit below wants the OUTERMOST failure, the same depth-1 notion the damage flush uses.
    ++t_pay_nest;
    struct NestGuard { ~NestGuard() { --t_pay_nest; } } nest_guard;
    bool ok;
    if (!state.dmg_events_armed)
    { ok = TapForCostSharedDiag(state, cost_in, for_creature, available, honor_legacy_cco); }
    else
    {
        // PREVENT DAMAGE (armed only). The payment's taps recorded their triggers on the permanents
        // (Permanent::mana_tap_mark); resolve them once the OUTERMOST payment has succeeded -- after
        // the mana abilities, before the spell resolves, which is where the rules put those triggers
        // (they go on the stack above the spell being cast). A failed payment restored every mark.
        // (The pain-aware payment policy runs per ATTEMPT, inside TapForCostSharedOnce -- see there.)
        ok = TapForCostSharedDiag(state, cost_in, for_creature, available, honor_legacy_cco);
        if (ok && t_pay_nest == 1) { dmgev::FlushDamageEvents(state); }
    }
    // Rollback audit (count-only, real play): would one same-turn re-pay have rescued this?
    if (!ok && t_pay_nest == 1 && payledger::On() && g_real_resolution)
    { payledger::NoteFailure(state, cost_in); }
    // MANA-PAYMENT ROLLBACK (MTG_PAY_ROLLBACK): the outermost payment failed -- if one earlier
    // same-turn pip can be re-paid so the colour this cost lacks comes free, do it and pay again.
    // Once (TryRescue caps itself at one rescue per turn), never nested (depth 1 only).
    if (!ok && t_pay_nest == 1 && PayRollbackOn() && payroll::TryRescue(state, cost_in, nullptr))
    {
        ok = TapForCostSharedDiag(state, cost_in, for_creature, available, honor_legacy_cco);
        if (ok && state.dmg_events_armed) { dmgev::FlushDamageEvents(state); }
        // The retry still failed: a failed payment must leave the game exactly as it found it,
        // so the re-pay is undone too (its own taps were restored by the failure path above).
        if (!ok) { payroll::UndoLast(state); }
    }
    return ok;
}

static bool TapForCostSharedDiag(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 ManaPool* available, bool honor_legacy_cco)
{
    // TEMP DIAGNOSTIC (MTG_TAPDBG, default off): every REAL payment -- cost, outcome, energy delta,
    // which lands went from untapped to tapped. Reads clean because real payments are rare.
    static const bool s_paydbg = EnvOn("MTG_TAPDBG");
    if (!(s_paydbg && g_real_resolution))
    { return TapForCostSharedImpl(state, cost_in, for_creature, available, honor_legacy_cco); }
    const int e0 = state.players[state.active_player_index].energy_counters;
    std::vector<uint8_t> was_untapped;
    was_untapped.reserve(state.battlefield.size());
    for (const Permanent& p : state.battlefield) { was_untapped.push_back(!p.tapped); }
    const bool ok = TapForCostSharedImpl(state, cost_in, for_creature, available, honor_legacy_cco);
    std::string taps;
    for (size_t i = 0; i < state.battlefield.size() && i < was_untapped.size(); ++i)
    { if (was_untapped[i] && state.battlefield[i].tapped)
      { taps += " " + state.battlefield[i].card.m_name.str(); } }
    std::fprintf(stderr, "[paydbg] cost=%s ok=%d energy %d->%d taps:%s\n",
                 cost_in.ToString().c_str(), ok ? 1 : 0, e0,
                 state.players[state.active_player_index].energy_counters, taps.c_str());
    return ok;
}

// ---- FAIL-FAST TOTAL-MANA BOUND (MTG_PAY_BOUND, default ON) ----------------------------------
//
// A payment ATTEMPT is expensive before it has looked at a single pip: TapForCostSharedImpl runs five
// O(battlefield) reservation scans (each with a LookupCached per permanent), then -- when any of them
// returns a mask -- snapshots the battlefield, runs a whole held attempt, restores, and runs a second
// unrestricted one. Each of those attempts copies the battlefield AGAIN for its own rollback, walks
// the per-pip greedy, and on failure enters the backtracker, which builds a source list, an
// O(sources^2) identical-sibling chain, a 128-bit mana-cache key and a max-flow oracle before it can
// answer. On EldraziDisplacerFlicker every one of those is paid over and over for costs the board
// cannot cover AT ALL, because the dominant caller is a payability PROBE: SubsetPayableWithFilters
// pays a subset's casts in sequence on one copied board (a pending land Aura opens that path for this
// deck -- PendingLandAuraColorMask), so the LAST casts of a long subset routinely ask a board whose
// sources are already spent.
//
// The bound is the backtracker's own branch-and-bound gate, hoisted to the front door: total mana is
// the one quantity that is cheap to bound exactly and that no amount of colour-fixing can create.
// Filters convert colour, they do not add mana; every source taps at most once (`usable` skips a
// tapped permanent); one tap adds at most SourceMaxNetLive to the pool. So a false from
//     PaymentManaCovers(state, for_creature, cost.ManaValue() - state.floating_mana.Total())
// -- i.e. UntappedManaUpperBound short of the cost's remaining pips -- proves the cost unpayable by
// the greedy, by the backtracker, by the held attempt, by the unrestricted attempt and by every
// hybrid expansion (ExpandHybrids preserves ManaValue). Bounded with reserved_mask 0 -- i.e.
// crediting even the sources the reservation audit would hold back -- so it can never be tighter
// than what any of those attempts is allowed to spend. See PaymentManaCovers for why the test is
// cheap enough to run on every payment: it is what the bound COSTS, not what it saves, that decides
// this (the first cut called LandAuraBonus -- itself a battlefield rescan -- per source, which on
// this aura-heavy deck made the whole thing O(n^2) and measured net -0.4%).
//
// BYTE-IDENTICAL, and the argument has exactly one moving part: what a FAILED payment leaves behind.
// TapForCostSharedOnce's total-failure path restores battlefield, both players' life,
// opponent_lost_life_this_turn, the graveyard (a Deathrite tap's exile is a graveyard erase, nothing
// more), the energy counters and the floating reserve; CommitPaySacSacrifices runs only on success.
// So a failed payment is state-neutral -- with ONE deliberate exception, documented in ManaPayment.h:
// the executor's `available` accounting pool is NOT rolled back, so a failed greedy leaves it
// decremented by whatever it managed to tap. That is observable behaviour, so the fast path is
// restricted to `available == nullptr` -- which is the rollout/search world (TapForCostDirect) where
// all the cost is, and excludes the executor's real-play payments entirely.
// The thread_local payment caches are unaffected: skipping a solve skips a negative cache STORE,
// which can only cost a later hit, never change an answer.
static bool PayBoundEnabled()
{ static const bool v = EnvOn("MTG_PAY_BOUND", true); return v; }

// LINE {C} HOLD (MTG_LINE_C_HOLD -- see LineCHoldEnabled in SpellEffects.h for the s8 trace).
// While a plan apply is paying, hold as many untapped {C} PROVIDERS as the plan's own activation
// still needs beyond this payment's colourless pips and the {C} already floating, NARROWEST
// provider first (a {C}-only Mariposa before a painland whose coloured mode a later cast may want;
// tie: lower battlefield index -- deterministic). Unlike ScarceColorHoldMask this holds a COUNT,
// not "every provider when scarce": generic pips can be paid by any source, so the payer needs no
// {C} at all and the only question is how many providers to keep out of its reach. Same
// reserved-first / unrestricted-retry contract as the other masks (a cast that needs them gets
// them back). Public: BatchPrepayMainCasts adds the same mask as a rung of the whole-turn ladder.
std::uint64_t LineColorlessHoldMask(const GameState& state, const ManaCost& cost)
{
    if (!LineCHoldEnabled()) { return 0; }
    // HUMAN PLAY: stand down. The human's line is paid as recorded (their --tap-pref, else the
    // shipped payer): a new hold here changes WHICH lands a replayed reference taps, and every
    // later frame of that reference then enumerates a different board -- the replay drifts off
    // its recorded picks (scripts/ref_handoff.py walked straight past its frame the first time
    // this mask ran unguarded). Autonomous play only, like the search's other tap heuristics.
    if (HumanPlayActive()) { return 0; }
    const PlanTraits* pt = CurrentPlanTraits();
    if (!pt || pt->act_c_pips <= 0) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    int need = pt->act_c_pips - cost.colorless - state.floating_mana.colorless;
    if (need <= 0) { return 0; }
    const int active = state.active_player_index;
    struct Prov { int idx; int breadth; };
    Prov prov[64]; int np = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool dork = d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)
                          && GraveyardFuelLive(state, active, *d);
        if (!dork && !p.card.IsLand() && !d->params.mana_rock) { continue; }
        bool makes_c = false; int seen = 0;
        for (Color c : EffectiveProducesFor(state, active, *d, &p))
        {
            if (c == Color::Colorless) { makes_c = true; continue; }
            seen |= (1 << static_cast<int>(c));
        }
        if (!makes_c) { continue; }
        int breadth = 0;
        for (int b = 0; b < 5; ++b) { if (seen & (1 << b)) { ++breadth; } }
        prov[np++] = { i, breadth };
    }
    if (np == 0) { return 0; }
    std::sort(prov, prov + np, [](const Prov& a, const Prov& b)
    { return a.breadth != b.breadth ? a.breadth < b.breadth : a.idx < b.idx; });
    std::uint64_t mask = 0;
    for (int k = 0; k < np && k < need; ++k) { mask |= (1ull << prov[k].idx); }
    return mask;
}

// ACTIVATION LINE HOLD (MTG_ACT_LINE_HOLD -- see ActLineHoldEnabled in SpellEffects.h for the
// measured drop rates this closes). While a plan apply is paying, hold back BOTH halves of what the
// plan's own trailing activations still owe:
//   * their {T} SOURCES, so a cast payment cannot tap one for mana and thereby nullify the {T} half
//     of that activation's own cost (the source ends up TAPPED and the branch no-ops silently);
//   * enough providers of each COLOUR their costs need, beyond this payment's own pips and the
//     float, narrowest provider first (a mono source before a dual whose other colour a later cast
//     may want; tie: lower battlefield index -- deterministic).
// Counts, not "every provider when scarce": LineColorlessHoldMask's shape rather than
// ScarceColorHoldMask's, because holding more than the activation needs only makes the held attempt
// fail and costs a second solve. Same reserved-first / unrestricted-retry contract as every mask
// here, so a cast that genuinely needs a held source still gets it and no cast is ever lost.
//
// NOT gated on HumanPlayActive(). LineColorlessHoldMask above does stand down there, because its
// hold is a tap-ORDER preference and a replayed reference would drift off its recorded picks. This
// one is different in kind: the human explicitly CHOSE a plan containing the activation, so paying
// that plan in a way that silently deletes the activation is not a preference being overridden, it
// is the plan not being executed. The reference corpus is verified instead of standing down.
std::uint64_t ActLineHoldMask(const GameState& state, const ManaCost& cost)
{
    if (!ActLineHoldEnabled()) { return 0; }
    const PlanTraits* pt = CurrentPlanTraits();
    if (!pt) { return 0; }
    if (pt->act_src_count == 0
        && pt->act_pips[0] + pt->act_pips[1] + pt->act_pips[2]
         + pt->act_pips[3] + pt->act_pips[4] == 0) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    const int active = state.active_player_index;

    // How many providers of each colour the activations still want. An activation's pips are demand
    // ALONGSIDE this payment's own, never demand this payment satisfies -- so `cost` is NOT
    // subtracted. Only float that SURVIVES this payment is supply the activation can spend without
    // tapping anything; every other pip it owes has to come from a source held back here.
    //
    // THE `- cur[c]` THIS REPLACED IS WHY THE LEVER MEASURED AS A LOSS (found 2026-09-30; see
    // docs/design/trailing-activation-payment-hole.md). It treated the cast's COMPETING demand as
    // supply, so whenever a cast wanted at least as many pips of a colour as the activation did --
    // the common case -- the coloured half of the hold silently did NOTHING while part (a) above
    // still held the {T} source. That is the worst of the three states: enough hold to spoil the
    // casts' tap assignment, not enough to make the activation payable. The {T} then gets paid and
    // the mana half rolls back -- case B, MANUFACTURED by the fix meant to close case A. It is why
    // `unpaid` inflated on every deck whose activations want a coloured pip (snow 62,479 ->
    // 440,066, selesnya 3,270 -> 11,352) while Prevent Damage, whose activations want none, was the
    // one deck it fell on. Worked repro: SelesnyaLifegain seed 4011 (batch s4004 gi 7) d0 T3 held a
    // Wirewood Lodge for its "{G}, {T}: untap target Elf", paid {1}{G}{G} by tapping the attacking
    // Priest of Titania instead of the Lodge, then could not raise the {G} -- no untap AND no
    // attack, and the win slipped T6 -> T7.
    //
    // The prepay rung calls this with an EMPTY cost, so that call site is byte-identical; only the
    // per-cast path (which a single-cast turn always takes -- the prepay declines, ManaPayment.cpp
    // "single-cast turns DECLINE the prepay") changes behaviour.
    const int cur[5] = { cost.white, cost.blue, cost.black, cost.red, cost.green };
    const int fl[5]  = { state.floating_mana.white, state.floating_mana.blue,
                         state.floating_mana.black, state.floating_mana.red,
                         state.floating_mana.green };
    // PENDING demand only: the pass's already-paid activations and the one being paid right now
    // are not demand this hold protects (see g_act_line_paid / g_act_line_current in ManaPayment.h).
    const int paid[5] = { g_act_line_paid.white + g_act_line_current.white,
                          g_act_line_paid.blue  + g_act_line_current.blue,
                          g_act_line_paid.black + g_act_line_current.black,
                          g_act_line_paid.red   + g_act_line_current.red,
                          g_act_line_paid.green + g_act_line_current.green };
    int need[5]; bool any_pip = false;
    for (int c = 0; c < 5; ++c)
    {
        const int float_left = fl[c] > cur[c] ? fl[c] - cur[c] : 0;
        need[c] = pt->act_pips[c] - paid[c] - float_left;
        if (need[c] < 0) { need[c] = 0; }
        if (need[c] > 0) { any_pip = true; }
    }

    std::uint64_t mask = 0;
    // (a) the {T} sources. Untapped only: an already-tapped source is not supply, and -- the part
    // that makes this self-limiting -- an activation that has already fired this trailing pass owns
    // a tapped source, so it drops out and only the PENDING activations are held.
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        for (int k = 0; k < pt->act_src_count; ++k)
        { if (p.card.m_number == pt->act_src_nums[k]) { mask |= (1ull << i); break; } }
    }
    if (!any_pip) { return mask; }

    // (b) the coloured pips. Build the untapped provider list once with each source's breadth, then
    // let each colour take its `need` narrowest. A source already held by (a) is skipped rather than
    // counted: its {T} is owed to an activation, so it is not supply for a pip either.
    struct Prov { int idx; int breadth; int colors; };
    Prov prov[64]; int np = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool dork = d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)
                          && GraveyardFuelLive(state, active, *d);
        if (!dork && !p.card.IsLand() && !d->params.mana_rock) { continue; }
        int seen = 0, breadth = 0;
        for (Color c : EffectiveProducesFor(state, active, *d, &p))
        {
            const int ci = static_cast<int>(c);
            if (ci >= 5 || (seen & (1 << ci))) { continue; }
            seen |= (1 << ci); ++breadth;
        }
        if (seen == 0) { continue; }
        prov[np++] = { i, breadth, seen };
    }
    if (np == 0) { return mask; }
    std::sort(prov, prov + np, [](const Prov& a, const Prov& b)
    { return a.breadth != b.breadth ? a.breadth < b.breadth : a.idx < b.idx; });
    for (int c = 0; c < 5; ++c)
    {
        int left = need[c];
        for (int k = 0; k < np && left > 0; ++k)
        {
            if (!(prov[k].colors & (1 << c))) { continue; }
            const std::uint64_t bit = 1ull << prov[k].idx;
            if (mask & bit) { continue; }        // already held -- owed to another pip or a {T}
            mask |= bit; --left;
        }
    }
    return mask;
}

static bool TapForCostSharedImpl(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 ManaPool* available, bool honor_legacy_cco)
{
    if (tapstats::Enabled()) { tapstats::g_pay_impl.fetch_add(1, std::memory_order_relaxed); }
    // The fail-fast bound (see the block above). MTG_PAY_BOUND=0 restores the old behaviour, and
    // under MTG_TAP_STATS the OFF arm still records what the bound WOULD have pruned, so the rate is
    // measurable on the unmodified engine.
    if (available == nullptr)
    {
        const bool pb = PayBoundEnabled();
        if (pb || tapstats::Enabled())
        {
            // The creature-only reserve is supply for a creature payment (spent first, see
            // TapForCostSharedOnce), never for anything else.
            const int creature_float = for_creature ? static_cast<int>(state.floating_creature_mana.Total()) : 0;
            // The big-spell-only reserve likewise, for a payment that may spend it (permissive on an
            // unset identity: this is a BOUND).
            const int big_float = BigFloatMayBeUsable(state) ? static_cast<int>(state.floating_bigspell_mana.Total()) : 0;
            if (!PaymentManaCovers(state, for_creature,
                                   cost_in.ManaValue() - state.floating_mana.Total() - creature_float - big_float))
            {
                if (tapstats::Enabled())
                { (pb ? tapstats::g_bound_prune : tapstats::g_bound_probe)
                      .fetch_add(1, std::memory_order_relaxed); }
                if (pb) { return false; }
            }
        }
    }

    // Two-colour hybrid pips ({B/G}, Deathrite Shaman): expand into concrete-colour assignments
    // and try each through the (hybrid-unaware) full pipeline below. bits==0 IS the flat cost the
    // historical first-colour collapse produced, tried first and UNsnapshotted -- so whenever it
    // succeeds or every assignment fails, behaviour (including the executor's deliberately-not-
    // restored accounting pool -- the smoke suite caught churn when it was restored) is
    // byte-identical to the pre-hybrid engine. Alternative assignments run snapshot/restored in
    // between; on total failure the bits==0 attempt is REPLAYED so its historical failure side
    // effects land exactly as before.
    if (cost_in.hybrid_count > 0)
    {
        const int a = state.active_player_index;
        PaySnapScratch<PermPaySnap> _bf_snap_scratch;                        // see PaySnapScratch
        std::vector<PermPaySnap>&   bf_snap = _bf_snap_scratch.Buf();        // compact rollback -- see PermPaySnap's header
        SnapPayFields(state.battlefield, bf_snap);
        std::vector<Permanent> bf_snap_full;
        if (g_pay_snap_verify) { bf_snap_full = state.battlefield; }
        const ManaPool               fm_snap = state.floating_mana;
        const ManaPool               fcm_snap = state.floating_creature_mana;
        const ManaPool               fbm_snap = state.floating_bigspell_mana;
        const ManaPool               av_snap = available ? *available : ManaPool{};
        PaySnapScratch<Card>         _gy_snap_scratch;
        std::vector<Card>&           gy_snap = _gy_snap_scratch.Buf();       // Deathrite exile
        gy_snap = state.players[a].graveyard;
        const int  la  = state.players[a].life;
        const int  lo  = state.players[1 - a].life;
        const bool oll = state.opponent_lost_life_this_turn;
        auto restore = [&]()
        {
            RestorePayFields(state.battlefield, bf_snap);
            if (g_pay_snap_verify)
            { VerifyPaySnapRestore(state.battlefield, bf_snap_full, "impl.hybrid"); }
            state.floating_mana                = fm_snap;
            state.floating_creature_mana       = fcm_snap;
            state.floating_bigspell_mana       = fbm_snap;
            if (available) { *available = av_snap; }
            state.players[a].graveyard         = gy_snap;
            state.players[a].life              = la;
            state.players[1 - a].life          = lo;
            state.opponent_lost_life_this_turn = oll;
        };
        if (TapForCostShared(state, cost_in.ExpandHybrids(0), for_creature,
                             available, honor_legacy_cco)) { return true; }
        for (unsigned bits = 1; bits < (1u << cost_in.hybrid_count); ++bits)
        {
            restore();
            if (TapForCostShared(state, cost_in.ExpandHybrids(bits), for_creature,
                                 available, honor_legacy_cco)) { return true; }
        }
        restore();
        // Total failure: replay the flat attempt (deterministic) so the state ends exactly as the
        // historical single-attempt failure left it.
        TapForCostShared(state, cost_in.ExpandHybrids(0), for_creature, available, honor_legacy_cco);
        return false;
    }

    // `act_hold` is split out of the fold ONLY so MTG_ACT_DROP_AUDIT can price the lever (see
    // g_act_pay_calls in GameLogger.h): the extra solve a failed held attempt buys is the whole cost
    // question, and attributing it needs to know whether this mask was rmask's sole contributor.
    // The value folded in is identical, and with the audit off nothing below it executes.
    const std::uint64_t act_hold = ActLineHoldMask(state, cost_in);
    const std::uint64_t rmask = ReservableSpecialMask(state) | PlanReserveMask(state)
                              | OneShotHoldMask(state) | PayloadReserveMask(state)
                              | ScarceColorHoldMask(state, cost_in)
                              | LineColorlessHoldMask(state, cost_in)
                              | act_hold;
    if (ActDropAuditOn())
    {
        g_act_pay_calls.fetch_add(1, std::memory_order_relaxed);
        if (act_hold != 0)
        {
            g_act_hold_mask.fetch_add(1, std::memory_order_relaxed);
            if (rmask == act_hold) { g_act_hold_solo.fetch_add(1, std::memory_order_relaxed); }
        }
    }
    // One HELD attempt: pay with `mask` reserved; on failure restore everything the attempt
    // touched (battlefield pay fields, float, the executor's accounting pool, the graveyard --
    // a Deathrite tap's exile -- both lives and the opponent-lost-life flag) and report false.
    auto held_attempt = [&](std::uint64_t mask, const char* tag) -> bool
    {
        const int a = state.active_player_index;
        PaySnapScratch<PermPaySnap> _bf_snap_scratch;                        // see PaySnapScratch
        std::vector<PermPaySnap>&   bf_snap = _bf_snap_scratch.Buf();        // compact rollback -- see PermPaySnap's header
        SnapPayFields(state.battlefield, bf_snap);
        std::vector<Permanent> bf_snap_full;
        if (g_pay_snap_verify) { bf_snap_full = state.battlefield; }
        const ManaPool               fm_snap  = state.floating_mana;
        const ManaPool               fcm_snap = state.floating_creature_mana;
        const ManaPool               fbm_snap = state.floating_bigspell_mana;
        const ManaPool               av_snap  = available ? *available : ManaPool{};
        PaySnapScratch<Card>         _gy_snap_scratch;
        std::vector<Card>&           gy_snap = _gy_snap_scratch.Buf();       // Deathrite exile
        gy_snap = state.players[a].graveyard;
        const int  la  = state.players[a].life;
        const int  lo  = state.players[1 - a].life;
        const bool oll = state.opponent_lost_life_this_turn;
        if (TapForCostSharedOnce(state, cost_in, for_creature, mask, available, honor_legacy_cco))
        { return true; }
        RestorePayFields(state.battlefield, bf_snap);
        if (g_pay_snap_verify)
        { VerifyPaySnapRestore(state.battlefield, bf_snap_full, tag); }
        state.floating_mana                = fm_snap;
        state.floating_creature_mana       = fcm_snap;
        state.floating_bigspell_mana       = fbm_snap;
        if (available) { *available = av_snap; }
        state.players[a].graveyard         = gy_snap;
        state.players[a].life              = la;
        state.players[1 - a].life          = lo;
        state.opponent_lost_life_this_turn = oll;
        return false;
    };
    if (rmask != 0)
    {
        if (held_attempt(rmask, "impl.rmask")) { return true; }
        // Fell through: this payment now costs a SECOND full solve. Attribute it (audit only).
        if (ActDropAuditOn() && act_hold != 0)
        {
            g_act_hold_retry.fetch_add(1, std::memory_order_relaxed);
            if (rmask == act_hold) { g_act_solo_retry.fetch_add(1, std::memory_order_relaxed); }
        }
    }
    // PUMP-TARGET NARROW RUNG (MTG_MINT_CREDIT_EXACT). The whole-turn prepay's reserve ladder
    // retreats from "hold every dork" to "hold the projected pump target alone" before it
    // releases everything (ReserveCreatureHold); this per-cast path had no such rung, so on a
    // turn the prepay declines (a mint line -- PP_MINT_HOLD -- or any single-cast turn) a payment
    // that needs ONE body's mana fell straight to the unrestricted greedy, whose equal-rank dork
    // tie is battlefield order: the older Mystic, which is the body the trick targets. Mirrorwing
    // seed 700628 T3: Gold Rush {1}{G} first (the hold-aware ladder above), Forest + two Mystics
    // up, and the greedy tapped the target for the generic pip with an identical untargeted
    // Mystic beside it -- the +6 the cast just paid for, forfeited by its own payment. Same
    // reserve-then-fallback contract as the rung above: a payment that genuinely needs the target
    // still gets it on the unrestricted retry. The backtracker already reaches for this body last
    // (g_tap_keep_last_card's partition); this is the greedy's twin. Lever off -> byte-identical.
    if (MintCreditExactOn() && PumpTargetHoldEnabled() && g_tap_keep_last_card != 0)
    {
        const int n = static_cast<int>(state.battlefield.size());
        std::uint64_t narrow = 0;
        for (int i = 0; i < n && i < 64; ++i)
        {
            const Permanent& p = state.battlefield[static_cast<std::size_t>(i)];
            if (p.controller_index != state.active_player_index || p.tapped
                || p.card.m_number != g_tap_keep_last_card) { continue; }
            // Only a body that IS a mana source can be held (a Dragon target is never in the tap
            // set, and holding it would just repeat the unrestricted attempt below).
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d != nullptr && d->tmpl == CardTemplate::ManaDork) { narrow = 1ull << i; }
            break;
        }
        if (narrow != 0 && narrow != rmask && held_attempt(narrow, "impl.narrow")) { return true; }
    }
    return TapForCostSharedOnce(state, cost_in, for_creature, /*reserved_mask=*/0, available,
                                honor_legacy_cco);
}

// (AnimateLandsShared / ActivateTapTokensShared -- the greedy post-cast mana sinks -- DELETED
// 2026-09-30, USER HARD RULE docs/design/no-greedy-in-search-window.md. Mutavault's animate and the
// tap-token abilities are enumerated plan actions (Action::Kind::AnimateLand / TapForTokenPay) for
// autonomous play as well as human play, realised by ApplyPlanDirect and the executor's trailing
// pass; Basri's exert is pruned by DecisionProvider::OffersExertTokenActivation.)

// ---- The viewer's MANUAL TAP/PAY fallback (docs/design/viewer-manual-tap-pay.md) ---------------
// See ManaPayment.h for the token format and the one-parser rule.

// Colour letters, indexed by static_cast<int>(Color) -- W U B R G C, matching Card.h's enum order.
// The token format's alphabet, the decision JSON's `taps` alphabet and the rejection messages all
// read from this one array, so they cannot drift.
static const char kPreTapColorLetters[] = "WUBRGC";

bool IsHumanPreTapToken(const std::string& tok)
{
    return tok.compare(0, 4, "tap=") == 0;
}

// ---- The viewer's QUEUED-CONTINUATION untap demand (docs/design/viewer-line-macros.md) --------
// See ManaPayment.h for the token format and why the continuation has to be DECLARED.

thread_local int g_human_untap_need = 0;

bool IsHumanUntapNeedToken(const std::string& tok)
{
    return tok.compare(0, 5, "need=") == 0;
}

int ParseHumanUntapNeedToken(const std::string& tok)
{
    if (!IsHumanUntapNeedToken(tok)) { return 0; }
    int mask = 0;
    // Reads the SAME letter alphabet the pre-tap token and the decision JSON's `taps` affordance
    // use, so the three cannot drift. An unrecognised letter is skipped rather than rejected: a
    // demand is a PREFERENCE (it biases one untap pick), so the worst a typo can do is ask for
    // less than the human meant -- unlike a pre-tap, where guessing is the failure being fixed.
    for (std::size_t i = 5; i < tok.size(); ++i)
    {
        const char* hit = std::strchr(kPreTapColorLetters, tok[i]);
        if (hit != nullptr && *hit != '\0') { mask |= 1 << static_cast<int>(hit - kPreTapColorLetters); }
    }
    return mask;
}

bool ParseHumanPreTapToken(const std::string& tok, TurnSolver::PreTap& out)
{
    if (!IsHumanPreTapToken(tok)) { return false; }
    std::string val = tok.substr(4);
    TurnSolver::PreTap t;
    // ':' then '#', both split from the RIGHT and neither of which can occur in an MTG card name --
    // the same argument `equip=`'s '#'/'@' and `blink=`'s '@'/'*' suffixes already rely on. An
    // unrecognised colour letter leaves `color` at -1 (rejected downstream with a real message)
    // rather than silently defaulting to white: guessing is the failure this fallback exists to fix.
    const std::size_t colon = val.rfind(':');
    if (colon != std::string::npos)
    {
        std::string c = val.substr(colon + 1);
        val = val.substr(0, colon);
        // The colour FIELD may carry '+'-separated extras: the land's own face, then one colour per
        // ANY-COLOUR land Aura on it ("W+U" = tap for {W}, take the Fertile Ground's bonus as {U}).
        // Split happens only here, INSIDE the field, so a card name containing '+' ("+2 Mace") is
        // untouched -- everything left of the final ':' was already taken as the name.
        //
        // An unrecognised letter leaves the slot at -1 and is rejected downstream WITH A MESSAGE
        // rather than silently defaulting to white. Same rule for the aura slots as for the face:
        // guessing is the failure this fallback exists to fix, and an aura slot the human did not
        // write at all is a different thing entirely (absent -> the historical `wild` credit).
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t plus = c.find('+', start);
            if (plus == std::string::npos) { fields.push_back(c.substr(start)); break; }
            fields.push_back(c.substr(start, plus - start));
            start = plus + 1;
        }
        auto letter_to_color = [](const std::string& s) -> int
        {
            if (s.size() != 1) { return -1; }
            const char* hit = std::strchr(kPreTapColorLetters, s[0]);
            return hit != nullptr ? static_cast<int>(hit - kPreTapColorLetters) : -1;
        };
        if (!fields.empty()) { t.color = letter_to_color(fields[0]); }
        for (std::size_t i = 1; i < fields.size(); ++i)
        { t.aura_colors.push_back(letter_to_color(fields[i])); }
    }
    const std::size_t hash = val.rfind('#');
    if (hash != std::string::npos)
    { t.num = std::atoi(val.c_str() + hash + 1); val = val.substr(0, hash); }
    if (val.empty()) { return false; }
    t.name = val;
    out = std::move(t);
    return true;
}

std::string HumanPreTapFaces(const GameState& state, const Permanent& p)
{
    const int active = state.active_player_index;
    if (p.controller_index != active || p.tapped) { return std::string(); }
    // Brightcap Badger's grant. This MUST match the payer, not merely approximate it: the viewer's
    // tap legality and the engine's are the same question asked twice, and a human offered fewer
    // faces than the search can use would be shown a board they cannot play.
    // NOT hoisted, unlike the two rollout pools: this function is asked about ONE permanent at a
    // time and its callers are viewer-only (HumanPlayActive), so the per-call battlefield walk is
    // paid a handful of times per human decision rather than millions of times per rollout. If a
    // non-viewer caller is ever added, hoist the grant into it -- see the note in
    // BuildNonCreaturePool for what the per-permanent form costs on a 364-permanent board.
    const CardDefinition* def = ManaDefOf(state, p, LiveManaGrant(state, active));
    if (def == nullptr) { return std::string(); }
    const CardParams& q = def->params;
    // The same source test the payment's `usable()` applies -- MINUS the classes a hand-driven tap
    // into the float cannot honestly carry, each excluded for a stated reason rather than by
    // omission. These stay ENGINE-OWNED and the human simply leaves them to the allocator:
    //   * PaySacSpendableNow (a Treasure, a Lotus Bloom): the "tap" destroys the permanent, and
    //     which one-shot source a line spends is a payment decision with its own accounting.
    //   * mana-CONVERSION sources (Cascade Bluffs, Arcum's Astrolabe, Ferrous Lake): they consume
    //     a feeder unit per activation, so the tap is a two-source transaction this cannot express.
    //   * a scaled mana LAND (Three Tree City's "{2},{T}"): the activation has a generic feed cost
    //     that a pre-tap does not pay, so tapping it here would mint mana the card cannot make.
    //   * `creature_mana_only` sources, and the COLOURED faces of a `colored_creature_only` land
    //     (Cavern of Souls / Unclaimed Territory): that mana may be spent only on a creature spell,
    //     and the float carries no such restriction -- pre-tapping it would launder a restricted
    //     unit into a general one. Their painless "{T}: Add {C}" mode is fine and stays offered.
    const bool is_src = (def->tmpl == CardTemplate::BasicLand)
                     || (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                     || q.mana_rock;
    if (!is_src) { return std::string(); }
    // Big-spell-only mana (Troyan) is engine-owned for the same laundering reason.
    if (IsManaConversionSource(q) || IsScaledManaLand(*def) || q.creature_mana_only
        || BigSpellOnlySource(q))
    { return std::string(); }
    if (!StorageSourceLive(p, *def))               { return std::string(); }
    if (!GraveyardFuelLive(state, active, *def))   { return std::string(); }
    if (!ManaSubtypeGateLive(state, active, *def)) { return std::string(); }
    std::string faces;
    for (Color c : EffectiveProduces(state, active, *def))
    {
        if (q.colored_creature_only && c != Color::Colorless) { continue; }
        const char letter = kPreTapColorLetters[static_cast<int>(c)];
        if (faces.find(letter) == std::string::npos) { faces.push_back(letter); }
    }
    return faces;
}

// The ANY-COLOUR land Auras on `p`, (name, legal letters), in AnyColorLandAuras order -- see
// ManaPayment.h. WUBRG and deliberately NOT {C}: "one mana of any COLOR" and {C} is not a colour
// (CR 105.1), exactly as LandAuraColorMask sets five bits and LandAuraAddToPool credits `wild`
// without `wild_c`. Offered only for a land the human may tap at all, so the dialog cannot ask for
// an Aura colour on a tap the engine is about to refuse for an unrelated reason.
std::vector<HumanPreTapAura>
HumanPreTapAuraFaces(const GameState& state, const Permanent& p)
{
    std::vector<HumanPreTapAura> out;
    if (HumanPreTapFaces(state, p).empty()) { return out; }
    for (const Permanent* a : AnyColorLandAuras(state, p))
    { out.push_back({ a->card.m_name.str(), a->card.m_number, "WUBRG" }); }
    return out;
}

// The stderr witness (MTG_PRE_TAP_TRACE, default OFF, zero cost when off). Without it a pre-tap is
// unobservable from outside: the float it produces is spent by the very next payment, so "the human
// dictated this tap" and "the allocator happened to pick the same land anyway" look identical from
// the board alone. test/manual_tap_check.py asserts on exactly these lines.
static bool PreTapTraceOn()
{
    static const bool v = EnvOn("MTG_PRE_TAP_TRACE");
    return v;
}

std::string ApplyHumanPreTap(GameState& state, const TurnSolver::PreTap& t)
{
    auto reject = [&](const std::string& why) -> std::string
    {
        if (PreTapTraceOn())
        { std::fprintf(stderr, "[pre-tap] REJECT %s#%d: %s\n", t.name.c_str(), t.num, why.c_str()); }
        return why;
    };
    if (t.name.empty()) { return reject("a tap= token names no source"); }
    if (t.color < 0 || t.color >= 6)
    {
        return reject("tap of '" + t.name + "' names no colour -- write "
                      "tap=<name>#<num>:<W|U|B|R|G|C>");
    }
    const char letter = kPreTapColorLetters[t.color];
    const int  active = state.active_player_index;
    Permanent* found = nullptr;
    bool name_seen = false, id_seen = false, tapped_seen = false;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.card.m_name != t.name) { continue; }
        name_seen = true;
        if (t.num != 0 && p.card.m_number != t.num) { continue; }   // 0 = any copy of the name
        id_seen = true;
        if (p.tapped) { tapped_seen = true; continue; }
        found = &p;
        break;
    }
    if (found == nullptr)
    {
        if (tapped_seen) { return reject("'" + t.name + "' is already tapped"); }
        if (name_seen && !id_seen)
        { return reject("you control no copy of '" + t.name + "' with that id"); }
        return reject("you control no untapped '" + t.name + "'");
    }
    const std::string faces = HumanPreTapFaces(state, *found);
    if (faces.empty())
    {
        return reject("'" + t.name + "' cannot be tapped by hand -- a filter, a feed-cost source, a "
                      "one-shot sacrifice source or restricted mana; the engine's payment owns those");
    }
    if (faces.find(letter) == std::string::npos)
    {
        return reject(std::string("'") + t.name + "' cannot produce {" + letter + "} (it makes {"
                      + faces + "})");
    }
    // THE SAME CHOKEPOINT HumanPreTapFaces JUST USED, not a second raw lookup.
    //
    // USER-REPORTED, twice (candidate-B Fungus, 2026-09-25): "Brightcap Badger turning critters into
    // mana producers is not working", then -- after the grant itself had been verified end to end
    // through the payer -- "Tapping with Badger out is still not working?" Both were this line.
    //
    // The faces check above resolves the Badger's grant through ManaDefOf, so a granted Saproling
    // correctly reports "G" and the viewer draws it as a tappable {G} source. This line then threw
    // it away and asked CardDatabase directly -- and EVERY Saproling is a TOKEN, so LookupCached
    // returns null and the hand-forced tap died with "has no card definition" on exactly the
    // population the card exists to create. Offered by one half of the question and refused by the
    // other: `tap=1/1 Saproling Token#1000:G` verdict ILLEGAL on a board whose own chip said {G}.
    //
    // Nothing else could see it. The 22 assertions that closed the first report drove
    // AvailableManaPool / UntappedManaUpperBound / TapForCostShared / HumanPreTapFaces -- the payer
    // and the OFFER -- and every one of them was and is correct; the executor half had no test at
    // all, and the manual tap is human-play-only so no digest, GT cell or protocol replay reaches it.
    //
    // `def` is used for exactly one thing below -- TapSourceIntoFloat -- which asks only mana
    // questions of it (produces, energy, depletion, pay-sac), so the synthetic granted face is the
    // right answer here. See the ManaDefOf header for the caution that applies to callers which ask
    // NON-mana questions of the result; this is not one of them.
    const CardDefinition* def = ManaDefOf(state, *found, LiveManaGrant(state, active));
    if (def == nullptr) { return reject("'" + t.name + "' has no card definition"); }
    // ---- the land Aura's colour, when the human stated one ------------------------------------
    // "Whenever enchanted land is tapped for mana, its controller adds an additional one mana of
    // any color" is a CHOICE the rules make at resolution (CR 106.1b), and before this the engine
    // made it for them by crediting `wild`. Every rejection below reports its reason verbatim, for
    // the same reason the face's do: this fallback exists because a silently-made mana choice is
    // the defect, so a silently-DROPPED one would be the same defect wearing a different hat.
    const std::vector<HumanPreTapAura> auras = HumanPreTapAuraFaces(state, *found);
    if (!t.aura_colors.empty())
    {
        if (auras.empty())
        {
            return reject("'" + t.name + "' carries no land Aura that adds one mana of any colour, "
                          "so there is no Aura colour to choose");
        }
        if (t.aura_colors.size() > auras.size())
        {
            return reject("'" + t.name + "' carries " + std::to_string(auras.size())
                          + " any-colour land Aura(s) but the tap names "
                          + std::to_string(t.aura_colors.size()) + " Aura colour(s)");
        }
        for (std::size_t i = 0; i < t.aura_colors.size(); ++i)
        {
            const int ac = t.aura_colors[i];
            if (ac < 0 || ac >= 6)
            {
                return reject("the Aura bonus on '" + t.name + "' (" + auras[i].name
                              + ") names no colour -- write tap=<name>#<num>:<FACE>+<W|U|B|R|G>");
            }
            const char al = kPreTapColorLetters[ac];
            if (auras[i].faces.find(al) == std::string::npos)
            {
                return reject(std::string("'") + auras[i].name + "' on '" + t.name
                              + "' adds one mana of any COLOR, so it cannot add {" + al
                              + "} (it makes {" + auras[i].faces + "})");
            }
        }
    }
    TapSourceIntoFloat(state, active, *found, *def, static_cast<Color>(t.color),
                       state.floating_mana, /*available=*/nullptr, /*for_creature=*/false,
                       t.aura_colors.empty() ? nullptr : &t.aura_colors);
    if (PreTapTraceOn())
    {
        const ManaPool& f = state.floating_mana;
        std::string extra;
        for (int ac : t.aura_colors)
        { extra += '+'; extra += (ac >= 0 && ac < 6) ? kPreTapColorLetters[ac] : '?'; }
        std::fprintf(stderr,
                     "[pre-tap] tap %s#%d as %c%s -> float{w%d u%d b%d r%d g%d c%d *%d}\n",
                     t.name.c_str(), found->card.m_number, letter, extra.c_str(),
                     f.white, f.blue, f.black, f.red, f.green, f.colorless, f.wild);
    }
    return std::string();
}
