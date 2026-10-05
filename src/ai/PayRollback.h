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
#include <atomic>
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

struct Dumper
{
    ~Dumper()
    {
        if (!PayRollbackOn() && g_tried.load() == 0) { return; }
        std::fprintf(stderr, "\n=== PAY-ROLLBACK: tried=%llu fired=%llu   REAL play: tried=%llu fired=%llu ===\n",
                     (unsigned long long)g_tried.load(), (unsigned long long)g_fired.load(),
                     (unsigned long long)g_tried_real.load(), (unsigned long long)g_fired_real.load());
    }
};
inline Dumper g_dumper;
}
