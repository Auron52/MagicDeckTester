#pragma once
// MANA-PAYMENT ROLLBACK (MTG_PAY_ROLLBACK, default OFF; design: docs/design/mana-payment-rollback.md).
//
// USER 2026-10-05: *"record some options for mana/land retention we did not consider, particularly
// surrounding colours and retry there only when we need that option in a subsequent step. Even
// better, if we retry there with what we need to retain in mind it should be easier to manage."* And
// on its place in the design: *"we want good heuristics for choosing mana sources, but it seems like
// this alone is not consistent enough for the needs of fully accurate search. The rollback is a backup
// which attempts to bridge this consistency gap."*
//
// WHAT IT DOES. The committed payment taps of the current turn are recorded on the GameState
// (PayRollbackLedger: source, colour made, generic-or-coloured pip). When a later line of the SAME
// turn is short of a colour -- a real payment fails outright, or a dig's found card has the mana but
// not the colour -- TryRescue looks for, per missing coloured pip X, one earlier tap of a source A
// that produces X whose pip could instead have been paid by a source B that is still untapped and
// cannot itself make X. It then re-pays: A untaps, B taps. Mana is conserved (A and B each make one
// mana; A's unit was consumed by the earlier pip, B's unit now stands in for it) and the new line
// pays X from A. The user's two classes both land here: a colour spent on a generic pip (E.generic)
// and a multi-colour source tapped for one of its colours (E.produced, B must make that colour).
//
// WHY IT IS SOUND, AND THE GUARDS THAT MAKE IT SO. A retroactive re-pay is legal exactly when the
// resulting tapped set was reachable by some payment order this turn. Within a turn a source only
// goes untapped -> tapped, so "B is untapped now" means "B was untapped when A paid" UNLESS an
// untap EFFECT fired this turn (Peregrine Drake, Wirewood Lodge, a ritual untap, a snow untap) --
// those set PayRollbackLedger::untap_effect and disable the rescue for the turn. B must never have
// been a payment source this turn (its mana would be double-counted). Both A and B must be
// SIDE-EFFECT-FREE one-mana direct sources (no pain, drip, energy, depletion counter, graveyard
// exile, Treasure sacrifice, storage, conversion, untap burst): nothing to refund on A, nothing to
// replay on B. Nothing may be FLOATING: an earlier tap's unit that is still unspent cannot be proven
// consumed, so A might be untapped with its mana still in the pool.
//
// WHY IT STAYS RARE. At most ONE rescue per turn (no nested or repeated rollback), one swap per
// missing pip, no search: a linear scan of the ledger (<= 12 entries) per missing pip. The census
// (g_fired / g_tried) is the success metric of the HEURISTIC layer -- a rising rate is a defect
// report against the tap order, not a sign the rollback is working (mana-payment-rollback.md,
// "It must stay RARE").
//
// LOCKSTEP. The ledger lives on the state, so every rollout branch carries its own; TryRescue is
// called from the shared payment entry and from both dig-gate twins (executor / rollout), so the two
// worlds re-pay identically. Deterministic: ledger order, then battlefield order.
//
// CLAIRVOYANCE. Every swap this performs is DOMINANT in the design doc's sense: B cannot make the
// colour the line needs and could have paid the earlier pip, so keeping A up was weakly better
// whatever the later reveal showed. A non-dominated swap (U vs G when either might have been
// needed) never qualifies, because then B would produce X and be supply, not a swap.
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../core/EnvFlags.h"
#include "../core/GameState.h"
#include "../core/GameLogger.h"     // g_reveal_logger: the viewer's event stream
#include "../core/SpellEffects.h"   // CanTapNow, EffectiveProducesFor, GraveyardFuelLive, IsPaySacSource
#include "../cards/CardDatabase.h"
#include "EngineFlags.h"            // PayRollbackOn

namespace payroll
{
// Both worlds (rollouts included) -- and REAL play alone, which is the census that matters: the
// rollout counts scale with search effort, the real-play count is "how often the heuristics needed
// the backup", per game.
inline std::atomic<std::uint64_t> g_tried{0}, g_fired{0}, g_tried_real{0}, g_fired_real{0};

// The swaps the LAST TryRescue applied (battlefield indices + the ledger entry it rewrote), so a
// caller whose retry still fails can put the state back exactly: a failed payment must be
// side-effect-free (TapForCostSharedOnce's atomic-rollback contract), swap included.
struct AppliedSwap { int a; int b; int entry; int prev_source_num; };
inline thread_local AppliedSwap t_last[5];
inline thread_local int         t_last_n = 0;

// A one-mana direct source whose tap has no side effect to refund or replay.
inline bool SwapEligible(const CardDefinition& d)
{
    const CardParams& q = d.params;
    if (!(d.tmpl == CardTemplate::BasicLand || d.tmpl == CardTemplate::ManaDork || q.mana_rock)) { return false; }
    if (IsManaConversionSource(q) || IsPaySacSource(d) || q.storage_land) { return false; }
    if (q.tap_self_damage > 0 || q.tap_opponent_lifegain > 0 || q.energy_per_colored_tap > 0
        || q.gy_land_exile_mana || q.enters_tapped_with_depletion > 0 || q.taps_spawn_opp_token
        || q.untap_creature_cost.has_value()) { return false; }
    return ManaProducedPerTap(d) == 1;
}

inline bool IsLiveSource(const GameState& s, int active, const Permanent& p, const CardDefinition& d)
{
    if (p.controller_index != active || p.tapped) { return false; }
    const bool dork = d.tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield)
                   && GraveyardFuelLive(s, active, d);
    return dork || p.card.IsLand() || d.params.mana_rock;
}

inline bool Produces(const GameState& s, int active, const CardDefinition& d, const Permanent& p, Color c)
{
    for (Color x : EffectiveProducesFor(s, active, d, &p)) { if (x == c) { return true; } }
    return false;
}

inline int FindPermIdx(const GameState& s, int active, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { const Permanent& p = s.battlefield[i]; if (p.controller_index == active && p.card.m_number == num) { return i; } }
    return -1;
}

// Re-pay earlier same-turn pips so `cost` becomes colour-payable from what is untapped. Returns
// true iff a rescue was APPLIED (the caller then re-tries its payment / gate). `for_card` is the
// event label's beneficiary (may be null).
// Decline-reason trace (MTG_PAY_ROLLBACK_AUDIT>=2, real play only): which guard refused.
inline void Declined(const GameState& s, const char* why, const char* for_card)
{
    if (!g_real_resolution || EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) < 2) { return; }
    std::fprintf(stderr, "[payroll] t%d ROLLBACK-DECLINED%s%s: %s\n", s.turn_number,
                 for_card ? " for " : "", for_card ? for_card : "", why);
}

inline bool TryRescue(GameState& s, const ManaCost& cost, const char* for_card)
{
    const int active = s.active_player_index;
    PayRollbackLedger& L = s.pay_ledger;
    L.Stamp(s.turn_number, active);
    if (L.n == 0)           { Declined(s, "empty ledger", for_card); return false; }
    if (L.untap_effect)     { Declined(s, "an untap effect fired this turn", for_card); return false; }
    if (L.rollbacks >= 1)   { Declined(s, "already rolled back this turn", for_card); return false; }
    if (s.floating_mana.Total() > 0) { Declined(s, "mana floating", for_card); return false; }
    // The restricted floats are a committed tap's surplus too: re-assigning that tap would leave its
    // floated units behind (counted twice). Same refusal.
    if (s.floating_creature_mana.Total() > 0 || s.floating_bigspell_mana.Total() > 0)
    { Declined(s, "restricted mana floating", for_card); return false; }
    g_tried.fetch_add(1, std::memory_order_relaxed);
    if (g_real_resolution) { g_tried_real.fetch_add(1, std::memory_order_relaxed); }

    int supply[5] = { 0, 0, 0, 0, 0 };
    int total = 0;
    const int bn = static_cast<int>(s.battlefield.size());
    for (int i = 0; i < bn; ++i)
    {
        const Permanent& p = s.battlefield[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !IsLiveSource(s, active, p, *d)) { continue; }
        total += ManaProducedPerTap(*d);
        int seen = 0;
        for (Color c : EffectiveProducesFor(s, active, *d, &p))
        { const int ci = static_cast<int>(c); if (ci < 5 && !(seen & (1 << ci))) { seen |= 1 << ci; ++supply[ci]; } }
    }
    if (total < cost.ManaValue()) { Declined(s, "total mana short (not a colour problem)", for_card); return false; }
    const int need[5] = { cost.white, cost.blue, cost.black, cost.red, cost.green };
    int missing[5]; int total_missing = 0;
    for (int i = 0; i < 5; ++i) { missing[i] = std::max(0, need[i] - supply[i]); total_missing += missing[i]; }
    if (total_missing == 0) { Declined(s, "no colour missing", for_card); return false; }

    struct Swap { int a; int b; int entry; };
    Swap swaps[5]; int ns = 0;
    auto is_ledger_source = [&](int num) -> bool
    { for (int k = 0; k < L.n; ++k) { if (L.e[k].source_num == num) { return true; } } return false; };
    auto used = [&](int idx) -> bool
    { for (int k = 0; k < ns; ++k) { if (swaps[k].a == idx || swaps[k].b == idx) { return true; } } return false; };
    for (int ci = 0; ci < 5; ++ci)
    {
        for (int k = 0; k < missing[ci]; ++k)
        {
            bool found = false;
            for (int ei = 0; ei < L.n && !found; ++ei)
            {
                const PayRollbackLedger::Entry& e = L.e[ei];
                const int ai = FindPermIdx(s, active, e.source_num);
                if (ai < 0 || used(ai)) { continue; }
                const Permanent& a = s.battlefield[ai];
                const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
                if (!a.tapped || ad == nullptr || !SwapEligible(*ad)) { continue; }
                if (!Produces(s, active, *ad, a, static_cast<Color>(ci))) { continue; }
                for (int bi = 0; bi < bn && !found; ++bi)
                {
                    if (bi == ai || used(bi)) { continue; }
                    const Permanent& b = s.battlefield[bi];
                    const CardDefinition* bd = CardDatabase::Instance().LookupCached(b.card);
                    if (bd == nullptr || !IsLiveSource(s, active, b, *bd) || !SwapEligible(*bd)) { continue; }
                    if (is_ledger_source(b.card.m_number)) { continue; }
                    const std::vector<Color>& bp = EffectiveProducesFor(s, active, *bd, &b);
                    if (bp.empty()) { continue; }
                    if (!e.generic && !Produces(s, active, *bd, b, static_cast<Color>(e.produced))) { continue; }
                    if (Produces(s, active, *bd, b, static_cast<Color>(ci))) { continue; }   // would be supply, not a swap
                    swaps[ns++] = Swap{ ai, bi, ei };
                    found = true;
                }
            }
            if (!found) { Declined(s, "no eligible swap for a missing colour", for_card); return false; }
        }
    }
    // APPLY. A untaps, B taps; the earlier pip is now B's unit. The event is explicit (USER: "a way
    // in the viewer to backtrack payments like this so the user can see what the engine does").
    static const char* kC = "WUBRG";
    t_last_n = 0;
    for (int k = 0; k < ns; ++k)
    {
        Permanent& a = s.battlefield[swaps[k].a];
        Permanent& b = s.battlefield[swaps[k].b];
        a.tapped = false;
        b.tapped = true;
        t_last[t_last_n++] = AppliedSwap{ swaps[k].a, swaps[k].b, swaps[k].entry, L.e[swaps[k].entry].source_num };
        const PayRollbackLedger::Entry& e = L.e[swaps[k].entry];
        if (g_reveal_logger != nullptr || EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2)
        {
            std::string text = "re-tapped: " + b.card.m_name.str() + " for the "
                             + (e.generic ? std::string("generic") : std::string(1, kC[static_cast<int>(e.produced)]))
                             + " pip instead of " + a.card.m_name.str()
                             + (for_card ? std::string(" (needs ") + for_card + ")" : std::string());
            if (g_reveal_logger != nullptr) { g_reveal_logger->LogAbility(a.card.m_number, a.card.m_name.str(), text); }
            if (EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2 && g_real_resolution)
            { std::fprintf(stderr, "[payroll] t%d ROLLBACK %s\n", s.turn_number, text.c_str()); }
        }
        // The re-paid entry now names B, so a second look this turn (there is none: one rescue per
        // turn) and the audit's own bookkeeping stay consistent.
        L.e[swaps[k].entry].source_num = b.card.m_number;
    }
    L.rollbacks = static_cast<signed char>(L.rollbacks + 1);
    g_fired.fetch_add(1, std::memory_order_relaxed);
    if (g_real_resolution) { g_fired_real.fetch_add(1, std::memory_order_relaxed); }
    return true;
}

// Revert the last TryRescue on `s` (same state, nothing tapped or untapped in between -- the
// retry's own taps were already restored by its failure path). Restores the ledger entry too.
inline void UndoLast(GameState& s)
{
    PayRollbackLedger& L = s.pay_ledger;
    for (int k = t_last_n - 1; k >= 0; --k)
    {
        const AppliedSwap& w = t_last[k];
        if (w.a < static_cast<int>(s.battlefield.size())) { s.battlefield[w.a].tapped = true; }
        if (w.b < static_cast<int>(s.battlefield.size())) { s.battlefield[w.b].tapped = false; }
        if (w.entry < L.n) { L.e[w.entry].source_num = w.prev_source_num; }
    }
    if (t_last_n > 0 && L.rollbacks > 0) { L.rollbacks = static_cast<signed char>(L.rollbacks - 1); }
    if (t_last_n > 0) { g_fired.fetch_sub(1, std::memory_order_relaxed);
                        if (g_real_resolution) { g_fired_real.fetch_sub(1, std::memory_order_relaxed); } }
    t_last_n = 0;
}

// ---- MTG_PAY_ROLLBACK_DEPLETION: the held depletion land as the swap-in B (sub-flag of MTG_PAY_ROLLBACK) ----
//
// USER 2026-10-22 (Mirrorwing, after the MTG_PAY_DEPLETION_HOLD A/B): *"you might keep up a depletion land and
// then realize after the next breakpoint that you need different colours. In that case you roll back to the
// previous one to spend the depletion land instead."* -- *"The two-pip case is the clean one: the depletion
// land's two mana replace two lands from the earlier payment, and both come back untapped."* -- and on the
// one-pip case: *"allow it and leave the cast decision to the search, but credit the extra mana only where it
// really existed"*: the land's second unit is float in the SAME phase as the earlier payment, lost across a
// phase boundary.
//
// TRIGGER (TapForCostShared, depth 1, before the payment -- both worlds): this payment has a coloured pip X that
// no untapped NON-CREATURE source can make (a creature -- a mana dork -- is the payer's last resort, and on
// Mirrorwing the dork is an attacker; with no source at all the payment would fail), while this turn's ledger
// holds an earlier pip paid by a one-mana side-effect-free source A that makes X and that an untapped
// depletion land B (counters left, never a payment source this turn) could have paid instead: generic, or B's
// own colour. Cheap test first: the ledger is empty on most payments and B absent on most boards.
//
// APPLY. B taps and loses a counter (DecrementDepletionOnTap, exactly as a payment tap -- the emptied land is
// sacrificed by the same state-based check that sacrifices it after any payment); up to yield(B) = 2 earlier
// pips move onto B, the first an A making X, the second any other ledger pip B can pay; their sources untap.
// One pip only: B's second unit is credited to the floating pool iff the rescue runs in the same phase as
// that earlier payment (state.phase, tagged on the ledger entry), else it is lost. Same RARE discipline as
// TryRescue: one rescue per turn (shared counter), no nesting, nothing floating, no untap effect this turn.
//
// DOMINANCE (flagged for the USER, not decided): a re-pay is dominant WITHIN the turn only when every freed
// source makes B's colour (the freed set can then pay anything B could); trading B's {R}{R} for Game Trail +
// a Forest is not dominant -- it is right because the cards the breakpoint revealed want green. Counted
// separately (g_dep_nondominant), and B's counter is a cross-turn resource either way.
inline std::atomic<std::uint64_t> g_dep_tried{0}, g_dep_fired{0}, g_dep_fired_real{0}, g_dep_two{0},
                                  g_dep_one_same{0}, g_dep_one_lost{0}, g_dep_nondominant{0},
                                  g_dep_nondominant_real{0}, g_dep_undone{0};
struct AppliedDepletion
{
    bool active = false;
    int  b = -1;            // battlefield index of the depletion land
    int  a[2] = { -1, -1 };
    int  entry[2] = { -1, -1 };
    int  prev_num[2] = { 0, 0 };
    int  na = 0;
    int  credited = -1;     // colour index credited as float (one-pip, same phase), else -1
};

inline bool PlainSourceForRescue(const GameState& s, int active, const Permanent& p, const CardDefinition& d)
{
    if (p.controller_index != active || p.tapped || d.card.IsCreature()) { return false; }
    return p.card.IsLand() || d.params.mana_rock || IsPaySacSource(d);
}

inline int DepletionColourMask(const GameState& s, int active, const CardDefinition& d, const Permanent& p)
{
    int m = 0;
    for (Color x : EffectiveProducesFor(s, active, d, &p)) { const int ci = static_cast<int>(x); if (ci >= 0 && ci < 5) { m |= 1 << ci; } }
    return m;
}

// Could TryDepletionRescue fire for SOME cost on this board? (The enumerators' castability admission, built once
// per enumeration: a subset the colour gates reject is re-asked by the real sequential payment only when this
// is true -- so every board without a held depletion land and a same-turn ledger pays nothing.)
inline bool DepletionRescueLive(const GameState& s)
{
    const int active = s.active_player_index;
    const PayRollbackLedger& L = s.pay_ledger;
    if (L.turn != s.turn_number || L.player != active || L.n == 0 || L.untap_effect || L.rollbacks >= 1) { return false; }
    if (s.floating_mana.Total() > 0 || s.floating_creature_mana.Total() > 0 || s.floating_bigspell_mana.Total() > 0)
    { return false; }
    int bmask = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active || p.tapped || DepletionCountersOn(p) <= 0 || !p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || d->params.enters_tapped_with_depletion <= 0) { continue; }
        bool in_ledger = false;
        for (int k = 0; k < L.n; ++k) { if (L.e[k].source_num == p.card.m_number) { in_ledger = true; } }
        if (!in_ledger) { bmask |= DepletionColourMask(s, active, *d, p); }
    }
    if (bmask == 0) { return false; }
    const int draws_now = s.players[active].cards_drawn_this_turn;
    for (int k = 0; k < L.n; ++k)
    {
        const PayRollbackLedger::Entry& e = L.e[k];
        if (e.draws >= draws_now) { continue; }   // no draw since this pip: not a breakpoint rescue
        if (!(e.generic || (e.produced >= 0 && e.produced < 5 && (bmask & (1 << e.produced))))) { continue; }
        const int ai = FindPermIdx(s, active, e.source_num);
        if (ai < 0) { continue; }
        const Permanent& a = s.battlefield[ai];
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if (a.tapped && ad != nullptr && SwapEligible(*ad) && (DepletionColourMask(s, active, *ad, a) & ~bmask) != 0)
        { return true; }   // freeing it would add a colour the depletion land does not make
    }
    return false;
}

inline bool TryDepletionRescue(GameState& s, const ManaCost& cost, const char* for_card, AppliedDepletion* rec)
{
    // The applied swap is reported to the CALLER's record (never a shared thread_local: a nested payment between
    // this rescue and its undo -- a declined prepay's per-cast fallback -- would overwrite it).
    AppliedDepletion t_dep_last;
    if (rec != nullptr) { *rec = AppliedDepletion{}; }
    const int active = s.active_player_index;
    PayRollbackLedger& L = s.pay_ledger;
    // Cheap tests first: nothing recorded this turn is the common case.
    if (L.turn != s.turn_number || L.player != active || L.n == 0 || L.untap_effect || L.rollbacks >= 1) { return false; }
    const int need[5] = { cost.white, cost.blue, cost.black, cost.red, cost.green };
    if (need[0] + need[1] + need[2] + need[3] + need[4] == 0) { return false; }
    if (s.floating_mana.Total() > 0 || s.floating_creature_mana.Total() > 0 || s.floating_bigspell_mana.Total() > 0)
    { return false; }
    const int bn = static_cast<int>(s.battlefield.size());
    // B: an untapped depletion land with counters, never a payment source this turn, otherwise side-effect-free.
    int bi = -1, bmask = 0, byield = 0;
    for (int i = 0; i < bn && bi < 0; ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (p.controller_index != active || p.tapped || DepletionCountersOn(p) <= 0) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || d->params.enters_tapped_with_depletion <= 0 || !p.card.IsLand()) { continue; }
        const CardParams& q = d->params;
        if (q.tap_self_damage > 0 || q.tap_opponent_lifegain > 0 || q.energy_per_colored_tap > 0
            || IsManaConversionSource(q) || q.storage_land) { continue; }
        bool in_ledger = false;
        for (int k = 0; k < L.n; ++k) { if (L.e[k].source_num == p.card.m_number) { in_ledger = true; } }
        if (in_ledger) { continue; }
        bi = i; bmask = DepletionColourMask(s, active, *d, p); byield = ManaProducedPerTap(*d);
    }
    if (bi < 0 || byield < 1) { return false; }
    // The short colour: a pip no untapped non-creature source makes (B counts for its own colours).
    int supply[5] = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < bn; ++i)
    {
        const Permanent& p = s.battlefield[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !PlainSourceForRescue(s, active, p, *d)) { continue; }
        if (IsPaySacSource(*d)) { for (int c = 0; c < 5; ++c) { ++supply[c]; } continue; }
        const int y = std::max(1, ManaProducedPerTap(*d));
        const int m = DepletionColourMask(s, active, *d, p);
        for (int c = 0; c < 5; ++c) { if (m & (1 << c)) { supply[c] += y; } }
    }
    int xc = -1;
    for (int c = 0; c < 5 && xc < 0; ++c) { if (need[c] > supply[c]) { xc = c; } }
    if (xc < 0 || (bmask & (1 << xc))) { return false; }
    g_dep_tried.fetch_add(1, std::memory_order_relaxed);
    // A1: a ledger pip whose source makes X; A2 (optional): any other ledger pip B can pay.
    // Only a pip paid BEFORE a draw this turn (USER: "realize after the next breakpoint that you need different
    // colours"): demand the line already named when it paid was the hold's to weigh, and the hold weighed it.
    const int draws_now = s.players[active].cards_drawn_this_turn;
    auto b_can_pay = [&](const PayRollbackLedger::Entry& e) -> bool
    { return e.draws < draws_now && (e.generic || (e.produced >= 0 && e.produced < 5 && (bmask & (1 << e.produced)))); };
    int a_idx[2] = { -1, -1 }, a_ent[2] = { -1, -1 }, na = 0;
    for (int pass = 0; pass < 2 && na < std::min(2, byield); ++pass)
    {
        for (int ei = 0; ei < L.n && na < std::min(2, byield); ++ei)
        {
            const PayRollbackLedger::Entry& e = L.e[ei];
            if (!b_can_pay(e) || (na == 1 && a_ent[0] == ei)) { continue; }
            const int ai = FindPermIdx(s, active, e.source_num);
            if (ai < 0 || ai == bi || (na == 1 && a_idx[0] == ai)) { continue; }
            const Permanent& a = s.battlefield[ai];
            const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
            if (!a.tapped || ad == nullptr || !SwapEligible(*ad)) { continue; }
            const bool makes_x = Produces(s, active, *ad, a, static_cast<Color>(xc));
            if (pass == 0 && !makes_x) { continue; }          // the first freed source must make X
            if (pass == 1 && na == 0) { break; }
            a_idx[na] = ai; a_ent[na] = ei; ++na;
            if (pass == 0) { break; }                          // one X-maker, then any second pip
        }
    }
    if (na == 0) { Declined(s, "depletion rescue: no ledger pip the depletion land could have paid makes the short colour", for_card); return false; }
    // APPLY.
    Permanent& b = s.battlefield[bi];
    b.tapped = true;
    DecrementDepletionOnTap(b);
    t_dep_last.active = true; t_dep_last.b = bi; t_dep_last.na = na;
    bool dominant = true;
    static const char* kC = "WUBRG";
    std::string freed;
    for (int k = 0; k < na; ++k)
    {
        Permanent& a = s.battlefield[a_idx[k]];
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if ((DepletionColourMask(s, active, *ad, a) & bmask) != bmask) { dominant = false; }
        a.tapped = false;
        t_dep_last.a[k] = a_idx[k]; t_dep_last.entry[k] = a_ent[k]; t_dep_last.prev_num[k] = L.e[a_ent[k]].source_num;
        L.e[a_ent[k]].source_num = b.card.m_number;
        freed += (k ? " + " : "") + a.card.m_name.str();
    }
    if (na == 1 && byield >= 2)
    {
        // One pip: B's second unit existed only in the earlier payment's phase.
        const int c = std::countr_zero(static_cast<unsigned>(bmask));
        if (L.e[a_ent[0]].phase == static_cast<int>(s.phase))
        {
            switch (c) { case 0: ++s.floating_mana.white; break; case 1: ++s.floating_mana.blue; break;
                         case 2: ++s.floating_mana.black; break; case 3: ++s.floating_mana.red; break;
                         default: ++s.floating_mana.green; break; }
            t_dep_last.credited = c;
            g_dep_one_same.fetch_add(1, std::memory_order_relaxed);
        }
        else { g_dep_one_lost.fetch_add(1, std::memory_order_relaxed); }
    }
    else { g_dep_two.fetch_add(1, std::memory_order_relaxed); }
    L.rollbacks = static_cast<signed char>(L.rollbacks + 1);
    g_dep_fired.fetch_add(1, std::memory_order_relaxed);
    if (!dominant) { g_dep_nondominant.fetch_add(1, std::memory_order_relaxed); }
    if (g_real_resolution)
    {
        g_dep_fired_real.fetch_add(1, std::memory_order_relaxed);
        if (!dominant) { g_dep_nondominant_real.fetch_add(1, std::memory_order_relaxed); }
    }
    if (rec != nullptr) { *rec = t_dep_last; }
    {
        static const int s_lvl = EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0);
        if (s_lvl >= 3)
        { std::fprintf(stderr, "[payroll-any] t%d real=%d DEPLETION-RESCUE cost=%s freed=%d short=%c\n", s.turn_number,
                       g_real_resolution ? 1 : 0, cost.ToString().c_str(), na, "WUBRG"[xc]); }
    }
    if (g_reveal_logger != nullptr || EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2)
    {
        std::string text = "re-tapped: " + b.card.m_name.str() + " for the earlier pip"
                         + (na > 1 ? "s" : "") + " instead of " + freed + " (needs {"
                         + std::string(1, kC[xc]) + "}" + (for_card ? std::string(" for ") + for_card : std::string())
                         + (na == 1 && byield >= 2 ? (t_dep_last.credited >= 0 ? "; second unit floats" : "; second unit lost (earlier phase)") : "")
                         + (dominant ? "" : "; not dominant") + ")";
        if (g_reveal_logger != nullptr) { g_reveal_logger->LogAbility(b.card.m_number, b.card.m_name.str(), text); }
        if (EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2 && g_real_resolution)
        { std::fprintf(stderr, "[payroll] t%d DEPLETION-ROLLBACK %s\n", s.turn_number, text.c_str()); }
    }
    return true;
}

// The castability twin's board (MTG_RB_DEP_CAST=2): apply the rescue a payment short of colour X would make,
// for the first colour X (WUBRG order) that a freeable ledger source makes, the depletion land does not, and no
// untapped non-creature source makes now. Returns false (board untouched) when there is no such colour.
inline bool ApplyGenericDepletionRescue(GameState& s)
{
    const int active = s.active_player_index;
    const PayRollbackLedger& L = s.pay_ledger;
    int bmask = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active || p.tapped || DepletionCountersOn(p) <= 0 || !p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d != nullptr && d->params.enters_tapped_with_depletion > 0) { bmask |= DepletionColourMask(s, active, *d, p); }
    }
    int plain = 0;
    for (const Permanent& p : s.battlefield)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !PlainSourceForRescue(s, active, p, *d)) { continue; }
        plain |= IsPaySacSource(*d) ? 31 : DepletionColourMask(s, active, *d, p);
    }
    int freeable = 0;
    for (int k = 0; k < L.n; ++k)
    {
        const int ai = FindPermIdx(s, active, L.e[k].source_num);
        if (ai < 0) { continue; }
        const Permanent& a = s.battlefield[ai];
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if (a.tapped && ad != nullptr && SwapEligible(*ad)) { freeable |= DepletionColourMask(s, active, *ad, a); }
    }
    for (int c = 0; c < 5; ++c)
    {
        if (!(freeable & (1 << c)) || (bmask & (1 << c)) || (plain & (1 << c))) { continue; }
        ManaCost x;
        switch (c) { case 0: x.white = 1; break; case 1: x.blue = 1; break; case 2: x.black = 1; break;
                     case 3: x.red = 1; break; default: x.green = 1; break; }
        AppliedDepletion rec;
        return TryDepletionRescue(s, x, nullptr, &rec);
    }
    return false;
}

// Revert the last TryDepletionRescue (the payment after it failed: a failed payment is side-effect-free).
inline void UndoDepletion(GameState& s, AppliedDepletion& u)
{
    if (!u.active) { return; }
    PayRollbackLedger& L = s.pay_ledger;
    if (u.b >= 0 && u.b < static_cast<int>(s.battlefield.size()))
    {
        Permanent& b = s.battlefield[u.b];
        b.tapped = false;
        for (Counter& ctr : b.counters) { if (ctr.type == Counter::Type::Depletion) { ++ctr.count; break; } }
    }
    for (int k = 0; k < u.na; ++k)
    {
        if (u.a[k] >= 0 && u.a[k] < static_cast<int>(s.battlefield.size())) { s.battlefield[u.a[k]].tapped = true; }
        if (u.entry[k] >= 0 && u.entry[k] < L.n) { L.e[u.entry[k]].source_num = u.prev_num[k]; }
    }
    if (u.credited >= 0)
    {
        int* f[5] = { &s.floating_mana.white, &s.floating_mana.blue, &s.floating_mana.black, &s.floating_mana.red, &s.floating_mana.green };
        if (*f[u.credited] > 0) { --*f[u.credited]; }
    }
    if (L.rollbacks > 0) { L.rollbacks = static_cast<signed char>(L.rollbacks - 1); }
    g_dep_undone.fetch_add(1, std::memory_order_relaxed);
    u.active = false;
}

// ---- MTG_PAY_ROLLBACK_FLOATBACK: pay an earlier same-phase pip back from mana floating now (sub-flag) ----
//
// After a payment leaves mana floating (a depletion land's or a bounce land's second unit the cost could not
// use), an earlier pip of the SAME phase that a one-mana side-effect-free source A paid -- generic, or the
// floating colour -- is re-paid from the float when A itself makes that colour: A untaps, the float shrinks by
// one. STRICTLY dominant within the turn: A can make that colour at any later point (plus its other colours),
// while the float empties at the end of the phase. Realisable: the source that produced the float was untapped
// at the earlier payment (taps only go one way within a turn absent an untap effect, which disables this as it
// disables every rollback), so it could have been tapped then with this unit floating forward. Mirrorwing d3
// gi=14488 shape: Ancestral Anger {R} off Game Trail (the Needles held), then two Needles for Frontline Heroism
// {2}{R} float an {R} -- it pays the Anger back, Game Trail comes up for the drawn Fortifying Draught's {G}.
// The USER's strict-dominance bar ("if it saves tapping a land that is strictly better"), across payments.
inline std::atomic<std::uint64_t> g_fb_fired{0}, g_fb_fired_real{0};
inline int TryFloatBack(GameState& s)
{
    const int active = s.active_player_index;
    PayRollbackLedger& L = s.pay_ledger;
    if (L.turn != s.turn_number || L.player != active || L.n == 0 || L.untap_effect) { return 0; }
    if (s.floating_mana.Total() <= 0) { return 0; }
    int* f[5] = { &s.floating_mana.white, &s.floating_mana.blue, &s.floating_mana.black, &s.floating_mana.red,
                  &s.floating_mana.green };
    int fired = 0;
    static const char* kC = "WUBRG";
    for (int c = 0; c < 5; ++c)
    {
        for (int ei = 0; ei < L.n && *f[c] > 0; ++ei)
        {
            PayRollbackLedger::Entry& e = L.e[ei];
            if (e.phase != static_cast<int>(s.phase) || !(e.generic || e.produced == c)) { continue; }
            const int ai = FindPermIdx(s, active, e.source_num);
            if (ai < 0) { continue; }
            Permanent& a = s.battlefield[ai];
            const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
            if (!a.tapped || ad == nullptr || !SwapEligible(*ad) || !Produces(s, active, *ad, a, static_cast<Color>(c)))
            { continue; }
            a.tapped = false;
            --*f[c];
            e.source_num = 0;   // the pip is now the float's: no permanent to untap for it again
            ++fired;
            g_fb_fired.fetch_add(1, std::memory_order_relaxed);
            if (g_real_resolution) { g_fb_fired_real.fetch_add(1, std::memory_order_relaxed); }
            if (g_reveal_logger != nullptr || EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2)
            {
                std::string text = "re-paid an earlier pip from the floating {" + std::string(1, kC[c]) + "}: "
                                 + a.card.m_name.str() + " untaps";
                if (g_reveal_logger != nullptr) { g_reveal_logger->LogAbility(a.card.m_number, a.card.m_name.str(), text); }
                if (EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0) >= 2 && g_real_resolution)
                { std::fprintf(stderr, "[payroll] t%d FLOAT-BACK %s\n", s.turn_number, text.c_str()); }
            }
        }
    }
    return fired;
}

struct Dumper
{
    ~Dumper()
    {
        if (!PayRollbackOn() && g_tried.load() == 0) { return; }
        std::fprintf(stderr, "\n=== PAY-ROLLBACK: tried=%llu fired=%llu   REAL play: tried=%llu fired=%llu ===\n",
                     (unsigned long long)g_tried.load(), (unsigned long long)g_fired.load(),
                     (unsigned long long)g_tried_real.load(), (unsigned long long)g_fired_real.load());
        if (g_fb_fired.load() > 0)
        { std::fprintf(stderr, "=== PAY-ROLLBACK-FLOATBACK: fired=%llu   REAL play: fired=%llu ===\n",
                       (unsigned long long)g_fb_fired.load(), (unsigned long long)g_fb_fired_real.load()); }
        if (g_dep_tried.load() > 0)
        { std::fprintf(stderr, "=== PAY-ROLLBACK-DEPLETION: tried=%llu fired=%llu (two-pip %llu, one-pip same-phase %llu, one-pip lost %llu, "
                       "not-dominant %llu, undone %llu)   REAL play: fired=%llu not-dominant=%llu ===\n",
                       (unsigned long long)g_dep_tried.load(), (unsigned long long)g_dep_fired.load(),
                       (unsigned long long)g_dep_two.load(), (unsigned long long)g_dep_one_same.load(),
                       (unsigned long long)g_dep_one_lost.load(), (unsigned long long)g_dep_nondominant.load(),
                       (unsigned long long)g_dep_undone.load(), (unsigned long long)g_dep_fired_real.load(),
                       (unsigned long long)g_dep_nondominant_real.load()); }
    }
};
inline Dumper g_dumper;
}
