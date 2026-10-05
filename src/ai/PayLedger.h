#pragma once
// PAYMENT LEDGER -- the measurement instrument for the mana-payment ROLLBACK (MTG_PAY_ROLLBACK_AUDIT,
// default OFF = zero cost; see docs/design/mana-payment-rollback.md, "Order of work" step 2).
//
// USER 2026-10-05: *"record which mana we threw away on generic costs and potential colours it could
// have generated instead. Then on a future line we can consider rolling back our mana spend only
// when it is required"*, and *"The tricky part with the backtrack idea is we need to be super
// careful to avoid it becoming degenerate. Hence the idea that it is an uncommonly required fix."*
//
// So before any rollback may FIRE, we count how often it WOULD: this records, in REAL play only
// (g_real_resolution -- the executor's committed taps, never a rollout's), every mana tap this turn
// together with the alternative sources that could have paid the same pip at that moment, and at
// the two places a payment is lost for want of a COLOUR it asks the ledger whether one same-turn
// re-pay would have rescued it:
//   (1) a real payment that fails outright (TapForCostShared's outermost false), and
//   (2) a dig's found card declined as unpayable on colour (TurnSolver::SnowLookFoundPlayable --
//       the Snow s4_gi3 class: the dig's own payment spent the {U} the find needed).
// "Rescued" = for every missing coloured pip X there is a distinct ledger entry whose tapped source
// produces X and whose recorded alternative is still untapped and can pay that entry's pip
// instead (a generic pip: any producer; a coloured one: a producer of that colour) -- the user's
// two classes, a colour spent on generic and a multi-colour source tapped for one colour.
// Count-only: nothing here changes a tap, a rank or a plan, so every digest is byte-identical with
// the audit on or off. Per-process totals print at exit; MTG_PAY_ROLLBACK_AUDIT=2 also prints one
// line per failure with the rescue found (or not).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../core/EnvFlags.h"
#include "../core/GameState.h"
#include "../core/GameLogger.h"     // g_real_resolution (diagnostic contract: no game logic branches on it)
#include "../core/SpellEffects.h"   // CanTapNow, EffectiveProducesFor, GraveyardFuelLive
#include "../cards/CardDatabase.h"

namespace payledger
{
inline int Level() { static const int v = EnvInt("MTG_PAY_ROLLBACK_AUDIT", 0); return v; }
inline bool On()   { return Level() > 0; }

struct Entry
{
    int               source_num;   // the tapped permanent's card number
    Color             produced;     // the colour it made
    bool              generic;      // the pip was generic (any colour would have done)
    std::vector<int>  alts;         // card numbers of the untapped sources that could have paid it
};
inline thread_local std::vector<Entry> t_entries;
inline thread_local int  t_turn   = -1;
inline thread_local int  t_player = -1;
inline thread_local bool t_cur_pip_any = false;   // published by the payer's pip loop around a tap

inline std::atomic<std::uint64_t> g_taps{0}, g_fail{0}, g_fail_color{0}, g_fail_rescuable{0},
                                  g_look{0}, g_look_rescuable{0};

// Is `p` (ours, untapped) a direct mana source right now? Lands, dorks that can tap, rocks.
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

inline void Record(const GameState& s, int active, const Permanent& tapped,
                   const CardDefinition& def, Color col, bool generic)
{
    if (s.turn_number != t_turn || active != t_player)
    { t_entries.clear(); t_turn = s.turn_number; t_player = active; }
    Entry e; e.source_num = tapped.card.m_number; e.produced = col; e.generic = generic;
    for (const Permanent& q : s.battlefield)
    {
        if (&q == &tapped) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (qd == nullptr || !IsLiveSource(s, active, q, *qd)) { continue; }
        // Could q have paid this pip? Generic: any mana at all; coloured: that colour.
        const std::vector<Color>& prod = EffectiveProducesFor(s, active, *qd, &q);
        if (prod.empty()) { continue; }
        if (!generic && !Produces(s, active, *qd, q, col)) { continue; }
        e.alts.push_back(q.card.m_number);
    }
    if (def.params.produces.empty() && !generic) { /* pay-sac one-shot: nothing to learn */ }
    t_entries.push_back(std::move(e));
    g_taps.fetch_add(1, std::memory_order_relaxed);
}

inline const Permanent* FindPerm(const GameState& s, int active, int num)
{
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == active && p.card.m_number == num) { return &p; } }
    return nullptr;
}

// Would ONE same-turn re-pay per missing coloured pip have made `cost` payable from what is
// untapped now? Writes a one-line explanation to `why`.
inline bool WouldRescue(const GameState& s, int active, const ManaCost& cost, std::string& why)
{
    if (s.turn_number != t_turn || active != t_player) { why = "no ledger this turn"; return false; }
    // Untapped supply per colour (a multi-colour source counts toward each -- optimistic, so a
    // colour read as covered here may still be short; that only UNDER-counts rescues).
    int supply[5] = { 0, 0, 0, 0, 0 };
    for (const Permanent& p : s.battlefield)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !IsLiveSource(s, active, p, *d)) { continue; }
        for (Color c : EffectiveProducesFor(s, active, *d, &p))
        { const int ci = static_cast<int>(c); if (ci < 5) { ++supply[ci]; } }
    }
    const int need[5] = { cost.white, cost.blue, cost.black, cost.red, cost.green };
    int missing[5]; int total_missing = 0;
    for (int i = 0; i < 5; ++i) { missing[i] = std::max(0, need[i] - supply[i]); total_missing += missing[i]; }
    if (total_missing == 0) { why = "not a colour shortfall"; return false; }
    static const char* kC = "WUBRG";
    std::vector<int> used_alts;
    std::string plan;
    for (int ci = 0; ci < 5 && total_missing > 0; ++ci)
    {
        for (int k = 0; k < missing[ci]; ++k)
        {
            bool found = false;
            for (const Entry& e : t_entries)
            {
                const Permanent* src = FindPerm(s, active, e.source_num);
                if (src == nullptr || !src->tapped) { continue; }
                const CardDefinition* sd = CardDatabase::Instance().LookupCached(src->card);
                if (sd == nullptr || !Produces(s, active, *sd, *src, static_cast<Color>(ci))) { continue; }
                for (int an : e.alts)
                {
                    bool reused = false;
                    for (int u : used_alts) { if (u == an) { reused = true; break; } }
                    if (reused) { continue; }
                    const Permanent* alt = FindPerm(s, active, an);
                    if (alt == nullptr) { continue; }
                    const CardDefinition* ad = CardDatabase::Instance().LookupCached(alt->card);
                    if (ad == nullptr || !IsLiveSource(s, active, *alt, *ad)) { continue; }
                    // The alternative must pay the ORIGINAL pip, and must not itself be a producer
                    // of the missing colour (then it would already be supply, not a swap).
                    if (!e.generic && !Produces(s, active, *ad, *alt, e.produced)) { continue; }
                    if (Produces(s, active, *ad, *alt, static_cast<Color>(ci))) { continue; }
                    used_alts.push_back(an);
                    plan += std::string(plan.empty() ? "" : "; ") + "re-pay " + (e.generic ? "generic" : std::string(1, kC[static_cast<int>(e.produced)]))
                          + " from " + ad->card.m_name.str() + " instead of " + sd->card.m_name.str()
                          + " -> frees " + kC[ci];
                    found = true; break;
                }
                if (found) { break; }
            }
            if (!found) { why = "no re-pay frees " + std::string(1, kC[ci]) + (plan.empty() ? "" : " (partial: " + plan + ")"); return false; }
            --total_missing;
        }
    }
    why = plan;
    return true;
}

inline void NoteFailure(const GameState& s, const ManaCost& cost)
{
    const int active = s.active_player_index;
    g_fail.fetch_add(1, std::memory_order_relaxed);
    std::string why;
    const bool ok = WouldRescue(s, active, cost, why);
    if (why != "not a colour shortfall" && why != "no ledger this turn") { g_fail_color.fetch_add(1, std::memory_order_relaxed); }
    if (ok) { g_fail_rescuable.fetch_add(1, std::memory_order_relaxed); }
    if (Level() >= 2)
    { std::fprintf(stderr, "[payroll] t%d PAY-FAIL cost=%s rescuable=%d: %s\n",
                   s.turn_number, cost.ToString().c_str(), ok ? 1 : 0, why.c_str()); }
}

inline void NoteLookDeclined(const GameState& s, const ManaCost& cost, const char* card)
{
    const int active = s.active_player_index;
    g_look.fetch_add(1, std::memory_order_relaxed);
    std::string why;
    const bool ok = WouldRescue(s, active, cost, why);
    if (ok) { g_look_rescuable.fetch_add(1, std::memory_order_relaxed); }
    if (Level() >= 2)
    { std::fprintf(stderr, "[payroll] t%d LOOK-DECLINED %s cost=%s rescuable=%d: %s\n",
                   s.turn_number, card, cost.ToString().c_str(), ok ? 1 : 0, why.c_str()); }
}

struct Dumper
{
    ~Dumper()
    {
        if (!On()) { return; }
        std::fprintf(stderr, "\n=== PAY-ROLLBACK AUDIT: taps=%llu  pay-fail=%llu (colour=%llu, rescuable=%llu)"
                             "  look-declined=%llu (rescuable=%llu) ===\n",
                     (unsigned long long)g_taps.load(), (unsigned long long)g_fail.load(),
                     (unsigned long long)g_fail_color.load(), (unsigned long long)g_fail_rescuable.load(),
                     (unsigned long long)g_look.load(), (unsigned long long)g_look_rescuable.load());
    }
};
inline Dumper g_dumper;
}
