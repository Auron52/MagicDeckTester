// MANA COMPLETENESS ORACLE -- MTG_MANA_ORACLE. See ManaOracle.h for the mechanism and
// docs/design/mana-completeness-oracle.md for the invariant, the classes and what is not modelled yet.
#include "ManaOracle.h"
#include "EngineFlags.h"
#include "EscStats.h"
#include "HeuristicArm.h"
#include "ManaPayment.h"
#include "../cards/CardDatabase.h"
#include "../core/EnvFlags.h"
#include "../core/GameLogger.h"
#include "../core/GameState.h"
#include "../core/SpellEffects.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace manaoracle
{
thread_local Hook* t_hook         = nullptr;
thread_local bool  t_float_only   = false;
thread_local bool  t_gate_observe = false;
thread_local int   t_seq_step     = -1;

int Level()
{
    static const int v = EnvOn("MTG_MANA_ORACLE") ? std::max(1, EnvInt("MTG_MANA_ORACLE", 1)) : 0;
    return v;
}

std::string OracleCreatureTap(GameState& state, const TurnSolver::PreTap& t)
{
    const int a = state.active_player_index;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != a || p.card.m_number != t.num) { continue; }
        if (p.tapped) { return "'" + t.name + "' is already tapped"; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { return "'" + t.name + "' has no definition"; }
        const CardParams& q = d->params;
        if (!q.creature_mana_only && !q.colored_creature_only) { return "'" + t.name + "' is not a creature-only source"; }
        if (!CanTapNow(p, state.battlefield)) { return "'" + t.name + "' is summoning sick"; }
        if (t.color < 0 || t.color > 5) { return "no colour"; }
        const Color c = static_cast<Color>(t.color);
        const std::vector<Color>& prod = EffectiveProducesFor(state, a, *d, &p);
        if (std::find(prod.begin(), prod.end(), c) == prod.end()) { return "'" + t.name + "' cannot make that colour"; }
        // Paying a CREATURE'S ACTIVATED ABILITY (Secluded Courtyard's clause, colored_creature_ability_ok):
        // that payment never reads the creature-only pool, so the unit goes where it is about to be spent.
        const bool ability = q.colored_creature_ability_ok && PayingCreatureAbility();
        TapSourceIntoFloat(state, a, p, *d, c, ability ? state.floating_mana : state.floating_creature_mana,
                           /*available=*/nullptr, /*for_creature=*/!ability);
        return std::string();
    }
    return "no untapped '" + t.name + "' with that id";
}

bool NextIsEngineStep(const GameState& s)
{
    const Hook* h = t_hook;
    if (h == nullptr || h->target != &s || h->ended) { return false; }
    return h->next < h->script.size() ? h->script[h->next].engine : h->all_engine;
}

bool TakeEngineStep(const GameState& s, int count)
{
    if (count <= 0 || !NextIsEngineStep(s)) { return false; }
    ++t_hook->next;
    t_hook->pay_steps.push_back(t_seq_step);
    return true;
}

EngineStepScope::EngineStepScope(GameState& s, bool active)
{
    if (!active || t_hook == nullptr || t_hook->target != &s) { return; }
    h   = t_hook;
    st  = &s;
    ta0 = g_tap_ahead_calls;
    for (const Permanent& p : s.battlefield) { if (!p.tapped) { untapped.push_back(p.card.m_number); } }
}

EngineStepScope::~EngineStepScope()
{
    if (h == nullptr) { return; }
    h->engine_tapaheads += g_tap_ahead_calls - ta0;
    for (const Permanent& p : st->battlefield)
    {
        if (p.tapped && std::find(untapped.begin(), untapped.end(), p.card.m_number) != untapped.end())
        { h->tapped_by_script.push_back(p.card.m_number); }
    }
}

EngineStepSpan::EngineStepSpan(GameState& s, bool active)
{
    if (!active || !NextIsEngineStep(s)) { return; }
    h    = t_hook;
    step = h->next++;
    h->pay_steps.push_back(t_seq_step);
    es.emplace(s, true);
    t_hook = nullptr;
}

void EngineStepSpan::End(bool ok, bool made)
{
    if (h == nullptr) { return; }
    t_hook = h;
    es.reset();
    if (!made) { h->next = step; h->pay_steps.resize(step); }
    else if (!ok && h->first_fail < 0) { h->first_fail = static_cast<int>(step); }
    h = nullptr;
}

void ScriptedTapAhead(GameState& state, int count, bool lands_only)
{
    Hook& h = *t_hook;
    if (h.ended || count <= 0) { return; }
    if (h.next >= h.script.size())
    {
        h.ended         = true;
        h.frontier      = true;
        if (h.f_state != nullptr) { *h.f_state = state; }
        h.f_cost        = ManaCost{};
        h.f_tapahead    = count;
        h.f_lands_only  = lands_only;
        return;
    }
    const PayOption& opt = h.script[h.next++];
    h.pay_steps.push_back(t_seq_step);
    if (opt.fail) { return; }
    for (std::size_t i = 0; i < opt.taps.size(); ++i)
    {
        const TurnSolver::PreTap& t = opt.taps[i];
        const bool restricted = i < opt.creature_route.size() && opt.creature_route[i] != 0;
        const std::string why = restricted ? OracleCreatureTap(state, t) : ApplyHumanPreTap(state, t);
        if (!why.empty()) { h.ended = true; h.tap_error = why; return; }
        h.tapped_by_script.push_back(t.num);
    }
}

namespace
{
const char kLetters[] = "WUBRGC";

// Per-plan apply ceiling (each node of the payment search is one full ApplyPlan from the decision
// board). A plan that hits it is reported CAPPED, never silently treated as complete.
thread_local int t_node_cap = 0;   // > 0: AuditLine's own ceiling (a viewer validation stays interactive)
int NodeCap()
{
    static const int v = std::max(1, EnvInt("MTG_MANA_ORACLE_NODES", 2000));
    return t_node_cap > 0 ? t_node_cap : v;
}
// Level-3 trace filter: 0 = every turn, else only this turn number.
int TraceTurn()
{
    static const int v = EnvInt("MTG_MANA_ORACLE_TURN", 0);
    return v;
}
// Per decision: how many mana-gate-rejected plans the ENUM-GAP pass realises (0 = all). Each one that
// has no payable line costs a full payment search to prove it, so this bounds a degenerate menu.
int MaxGateChecks()
{
    static const int v = std::max(0, EnvInt("MTG_MANA_ORACLE_GATE_MAX", 400));
    return v;
}
// 0 = audit every plan on the menu.
int MaxPlans()
{
    static const int v = std::max(0, EnvInt("MTG_MANA_ORACLE_MAX_PLANS", 0));
    return v;
}

// ---- classes, counters, the exit summary ----------------------------------------------------------
enum Cls
{
    C_ORACLE_GAP, C_ENUM_GAP, C_APPLY_DROP, C_APPLY_DROP_ORDER, C_PHANTOM,
    C_DOM_CREATURE, C_DOM_ONESHOT, C_DOM_HASTE, C_DOM_LIFE, C_DOM_OTHER, C_DOM_LAND,
    C_CONT_DIFFERS, C_DROP_COVERED, C_UNMODELED, C_CAPPED, C_N
};
const char* const kClsName[C_N] = {
    "ORACLE-GAP", "ENUM-GAP", "APPLY-DROP", "APPLY-DROP-ORDER", "PHANTOM",
    "DOMINATED-CREATURE", "DOMINATED-ONESHOT", "DOMINATED-HASTE", "DOMINATED-LIFE", "DOMINATED-OTHER",
    "DOMINATED-LAND", "CONTINUATION-DIFFERS", "DROP-COVERED", "UNMODELED", "CAPPED"
};
// A MISS is a class the ENGINE owes a fix for (I2). DOMINATED-LAND is only a potential one -- whether
// the land mattered is the follow-up question (Stage 3) -- so it is counted, and printed at level 2.
bool IsMiss(Cls c)
{
    return c == C_ENUM_GAP || c == C_APPLY_DROP || c == C_APPLY_DROP_ORDER || c == C_DOM_CREATURE
        || c == C_DOM_ONESHOT || c == C_DOM_HASTE || c == C_DOM_LIFE || c == C_DOM_OTHER;
}
bool QuietClass(Cls c)
{ return c == C_DOM_LAND || c == C_PHANTOM || c == C_CONT_DIFFERS || c == C_DROP_COVERED; }

std::atomic<long long> g_cls[C_N];
std::atomic<long long> g_decisions{0}, g_plans{0}, g_plans_skipped{0}, g_applies{0}, g_frontiers{0},
                       g_pruned{0}, g_options{0}, g_engine_drop{0}, g_ms{0},
                       g_gate_rejects{0}, g_gate_rejects_checked{0}, g_gate_rejects_skipped{0};
// ROLLBACK CENSUS (USER 2026-10-27: "how many lines are fully inaccessible and which are accessible only
// with rollbacks to the previous line"). One bucket per LOST line -- a plan the oracle realises in full that
// the search cannot reach. DEPTH k = how many of the engine's mana decisions before the one that failed
// have to be re-made: 0 = the failing payment itself had a way (a payer miss), 1 = the payment before it
// (keep blue up instead of white), ... The rest name lines no payment rollback in this order reaches.
enum Rb
{
    RB_D0, RB_D1, RB_D2, RB_D3P, RB_INCONSISTENT, RB_CAPPED, RB_UNFAITHFUL, RB_NO_FAIL, RB_UNMODELED, RB_ORDER,
    RB_OFF_MENU, RB_N
};
const char* const kRbName[RB_N] = {
    "depth0", "depth1", "depth2", "depth3+", "inconsistent", "capped", "unfaithful", "no-failed-payment",
    "unmodeled", "reorder", "off-menu"
};
std::atomic<long long> g_rb[RB_N];
// ...whether the action it lost was declared IN THE PLAN before the payment that must be re-made (so a
// smarter first payment could have known), and whether MTG_PAY_ROLLBACK (forced on) recovers the line.
std::atomic<long long> g_rb_need_plan{0}, g_rb_need_later{0}, g_rb_rescue_tried{0}, g_rb_rescued{0};
std::mutex             g_print_mu;
std::mutex             g_reason_mu;
std::map<std::string, long long> g_unmodeled_reasons;

struct Reporter
{
    ~Reporter()
    {
        if (!On()) { return; }
        long long miss = 0;
        for (int c = 0; c < C_N; ++c) { if (IsMiss(static_cast<Cls>(c))) { miss += g_cls[c].load(); } }
        std::fprintf(stderr,
                     "=== MANA ORACLE: decisions=%lld plans=%lld skipped=%lld applies=%lld frontiers=%lld "
                     "pruned=%lld options=%lld engine_drops=%lld gate_rejects=%lld(checked %lld, skipped %lld) "
                     "MISS=%lld ORACLE-GAP=%lld UNMODELED=%lld CAPPED=%lld ms=%lld ===\n",
                     g_decisions.load(), g_plans.load(), g_plans_skipped.load(), g_applies.load(),
                     g_frontiers.load(), g_pruned.load(), g_options.load(), g_engine_drop.load(),
                     g_gate_rejects.load(), g_gate_rejects_checked.load(), g_gate_rejects_skipped.load(), miss,
                     g_cls[C_ORACLE_GAP].load(), g_cls[C_UNMODELED].load(), g_cls[C_CAPPED].load(),
                     g_ms.load());
        std::string by;
        for (int c = 0; c < C_N; ++c)
        {
            if (g_cls[c].load() == 0) { continue; }
            by += std::string(" ") + kClsName[c] + "=" + std::to_string(g_cls[c].load());
        }
        std::fprintf(stderr, "=== MANA ORACLE classes:%s ===\n", by.empty() ? " (none)" : by.c_str());
        for (const auto& kv : g_unmodeled_reasons)
        { std::fprintf(stderr, "=== MANA ORACLE unmodeled: %lld x %s ===\n", kv.second, kv.first.c_str()); }
        long long lost = 0;
        std::string rb;
        for (int k = 0; k < RB_N; ++k)
        { lost += g_rb[k].load(); rb += std::string(" ") + kRbName[k] + "=" + std::to_string(g_rb[k].load()); }
        std::fprintf(stderr, "=== MANA ORACLE rollback census: lost=%lld%s need_in_plan=%lld need_later=%lld "
                             "pay_rollback_rescued=%lld/%lld ===\n",
                     lost, rb.c_str(), g_rb_need_plan.load(), g_rb_need_later.load(), g_rb_rescued.load(),
                     g_rb_rescue_tried.load());
    }
};
Reporter g_reporter;   // defined after everything it reads, so it is destroyed first

void NoteUnmodeled(const std::string& why)
{
    std::lock_guard<std::mutex> lk(g_reason_mu);
    ++g_unmodeled_reasons[why];
}

// Every flag is counted; the first 25 of each class print (all of them at level 2; the quiet classes
// only at level 2).
bool Count(Cls c)
{
    const long long n = g_cls[c].fetch_add(1, std::memory_order_relaxed) + 1;
    if (QuietClass(c)) { return Level() >= 2; }
    return Level() >= 2 || n <= 25;
}

// ---- text helpers -------------------------------------------------------------------------------
std::string PoolText(const ManaPool& f, int haste)
{
    std::string s;
    const int v[6] = { f.white, f.blue, f.black, f.red, f.green, f.colorless };
    for (int c = 0; c < 6; ++c)
    { for (int k = 0; k < v[c]; ++k) { s += "{"; s.push_back(kLetters[c]); s += "}"; } }
    if (f.wild > 0) { s += "{*x" + std::to_string(f.wild) + "}"; }
    if (haste > 0)  { s += "(haste " + std::to_string(haste) + ")"; }
    return s.empty() ? "-" : s;
}

std::string BoardText(const GameState& s)
{
    const int a = s.active_player_index;
    std::string bf, hand;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != a) { continue; }
        if (!bf.empty()) { bf += ", "; }
        bf += p.card.m_name.str() + "#" + std::to_string(p.card.m_number);
        if (p.tapped)            { bf += "(T)"; }
        if (p.entered_this_turn) { bf += "(new)"; }
    }
    for (const Card& c : s.players[a].hand) { if (!hand.empty()) { hand += ", "; } hand += c.m_name.str(); }
    std::string fl = "float=" + PoolText(s.floating_mana, s.floating_haste_c);
    if (s.floating_creature_mana.Total() > 0) { fl += " creature-float=" + PoolText(s.floating_creature_mana, 0); }
    return "board=[" + bf + "] hand=[" + hand + "] " + fl + " life=" + std::to_string(s.players[a].life);
}

const char* KindText(Action::Kind k)
{
    static const char* kNames[] = {
        "cast", "retrace", "vial", "landsedge", "playland", "dig", "suspend", "sac", "tapfortokens",
        "sacoutlet", "channel", "garth", "loyalty", "equip", "gy-return", "gy-play", "gy-exile",
        "attachall", "putfromhand", "jitte", "revealtop", "pump", "animate", "tapfortokenpay", "untapcreature",
        "blink", "act", "pod", "gy-exilegrow", "comboroute", "eternalize", "attachallfree", "auraswap",
        "untap"
    };
    const int i = static_cast<int>(k);
    return (i >= 0 && i < static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) ? kNames[i] : "kind?";
}

// The plan as EVERY action it holds (a one-line menu summary can elide some).
std::string PlanText(const TurnSolver::Plan& p)
{
    std::string s = !p.land_decided ? "land=(heuristic)"
                  : (p.land_to_play.empty() ? "no land" : "land=" + p.land_to_play);
    if (p.searched_order) { s += " [ordered]"; }
    for (const Action& a : p.actions)
    {
        s += " + ";
        s += KindText(a.kind);
        s += ":";
        s += a.card_name.str();
        if (a.chosen_x > 0) { s += "(x" + std::to_string(a.chosen_x) + ")"; }
    }
    return s;
}

std::string TapsText(const PayOption& o)
{
    if (o.engine) { return "(the engine's own)"; }
    if (o.fail) { return "(no payment exists)"; }
    std::string s;
    for (std::size_t i = 0; i < o.taps.size(); ++i)
    {
        const TurnSolver::PreTap& t = o.taps[i];
        if (!s.empty()) { s += " "; }
        s += t.name + "#" + std::to_string(t.num) + ":";
        if (!t.combo.empty()) { for (int c : t.combo) { s.push_back(kLetters[c]); } }
        else if (t.color >= 0 && t.color < 6) { s.push_back(kLetters[t.color]); }
        for (int ac : t.aura_colors) { s += "+"; s.push_back(kLetters[ac]); }
        if (i < o.creature_route.size() && o.creature_route[i]) { s += "(creature-only)"; }
    }
    if (o.stash.Total() > 0) { s += (s.empty() ? "" : " ") + std::string("keep-floating ") + PoolText(o.stash, o.stash_haste); }
    return s.empty() ? "(float only)" : s;
}

std::string ScriptText(const std::vector<PayOption>& script)
{
    std::string s;
    for (std::size_t i = 0; i < script.size(); ++i)
    { s += (i ? " ; " : "") + std::string("pay") + std::to_string(i + 1) + "[" + TapsText(script[i]) + "]"; }
    return s.empty() ? "(no payments)" : s;
}

std::string Ctx(const GameState& s, bool pre)
{
    char buf[256];
    std::snprintf(buf, sizeof buf, "seed=%llu job=%s gi=%d T%d m%d",
                  static_cast<unsigned long long>(s.game_seed),
                  escstats::t_job != nullptr ? escstats::t_job : "-", escstats::t_gi, s.turn_number,
                  pre ? 1 : 2);
    return buf;
}

// ---- outcome identity and resources ----------------------------------------------------------------
bool IsPaySacPermanent(const Permanent& p)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    return d != nullptr && IsPaySacSource(*d);
}

// A permanent's interchangeability class: what makes two copies the same resource for every purpose
// the oracle compares (its tapped flag and temp haste are the resource, so they are NOT in it).
std::string ClassKey(const Permanent& p)
{
    std::string k = p.card.m_name.str();
    k += "|"; k += p.card.IsLand() ? "L" : (p.card.IsCreature() ? "C" : "O");
    k += "|"; k += p.counters.KeyFragment();
    k += "|"; k += std::to_string(p.storage_counters) + "," + std::to_string(p.charge_counters);
    k += "|"; k += p.entered_this_turn ? "new" : "old";
    k += "|"; k += p.is_token ? "tok" : "card";
    return k;
}

// Everything but the resources: a COPY with the resources cleared, folded by the engine's own full
// mid-turn identity -- so a field the oracle never thought about still separates two outcomes
// (fail closed: an unthought-of difference makes outcomes INCOMPARABLE, never wrongly dominating).
// `canonical` (end boards) additionally forgets WHICH copy of identical permanents is where.
std::uint64_t CoreKey(const GameState& s, bool canonical = true)
{
    GameState n = s;
    const int a = n.active_player_index;
    n.battlefield.erase(std::remove_if(n.battlefield.begin(), n.battlefield.end(),
                                       [&](const Permanent& p)
                                       { return p.controller_index == a && IsPaySacPermanent(p); }),
                        n.battlefield.end());
    for (Permanent& p : n.battlefield)
    {
        if (p.controller_index != a) { continue; }
        p.tapped        = false;
        p.temp_haste    = false;
        p.mana_tap_mark = 0;
        p.pay_sac_eaten = false;
    }
    n.floating_mana.Clear();
    n.floating_creature_mana.Clear();
    n.floating_bigspell_mana.Clear();
    n.floating_haste_c = 0;
    n.players[a].life            = 0;
    n.players[a].energy_counters = 0;
    n.pay_ledger = PayRollbackLedger{};
    // Mid-plan (the frontier's Pareto prune) ids DO matter: a later action can name a permanent by
    // number (a sac source), so two states that differ only in which copy is where may not finish alike.
    if (!canonical) { return TurnSolver::MidTurnStateKeyOf(n); }
    // IDENTITY, NOT IDS. The engine's key mixes each permanent's m_number and the zone ORDER (it
    // replays index-encoded plans), so two boards that differ only in which copy is where -- Shard
    // Volley sacrificing Mountain #21 or the identical #22, two creatures cast in the other order --
    // would read as two boards. Put the copy in a canonical order and renumber every permanent by its
    // place in it, remapping the pointers between permanents (Aura host, Equipment, pair, a token's
    // creator) so the wiring is kept.
    std::map<int, std::string> by_num;
    for (const Permanent& p : n.battlefield) { by_num[p.card.m_number] = p.card.m_name.str(); }
    auto desc = [&](const Permanent& p)
    {
        std::string d = std::to_string(p.controller_index) + "|" + ClassKey(p) + "|";
        for (int r : { p.aura_attached_to, p.equipped_to, p.paired_with, p.created_by_number })
        {
            const auto it = by_num.find(r);
            d += (it == by_num.end() ? std::string("-") : it->second) + ",";
        }
        std::vector<std::string> on_me;   // what is attached to / paired with this one
        for (const Permanent& q : n.battlefield)
        {
            if (q.aura_attached_to == p.card.m_number || q.equipped_to == p.card.m_number
                || q.paired_with == p.card.m_number)
            { on_me.push_back(q.card.m_name.str()); }
        }
        std::sort(on_me.begin(), on_me.end());
        for (const std::string& x : on_me) { d += x + ";"; }
        return d;
    };
    {
        std::vector<std::pair<std::string, std::size_t>> order;
        for (std::size_t i = 0; i < n.battlefield.size(); ++i) { order.emplace_back(desc(n.battlefield[i]), i); }
        std::stable_sort(order.begin(), order.end(),
                         [](const auto& x, const auto& y) { return x.first < y.first; });
        std::vector<Permanent> bf;
        bf.reserve(n.battlefield.size());
        std::map<int, int> renum;
        for (std::size_t j = 0; j < order.size(); ++j)
        {
            bf.push_back(n.battlefield[order[j].second]);
            renum[bf.back().card.m_number] = 1000000000 + static_cast<int>(j);
        }
        auto re = [&](int& r) { const auto it = renum.find(r); if (it != renum.end()) { r = it->second; } };
        for (Permanent& p : bf)
        {
            re(p.card.m_number);
            re(p.aura_attached_to);
            re(p.equipped_to);
            re(p.paired_with);
            re(p.created_by_number);
        }
        n.battlefield.swap(bf);
    }
    auto by_name = [](const Card& x, const Card& y) { return x.m_name.str() < y.m_name.str(); };
    for (Player& pl : n.players)
    {
        std::stable_sort(pl.hand.begin(), pl.hand.end(), by_name);
        std::stable_sort(pl.graveyard.begin(), pl.graveyard.end(), by_name);
    }
    std::stable_sort(n.exile.begin(), n.exile.end(), by_name);
    return TurnSolver::MidTurnStateKeyOf(n);
}

// The resources. `with_float`: mid-plan (a frontier) the floating pools are resources; at the END of
// the apply they are not -- the plan is the whole main phase, and the pool empties when it ends.
using Res = std::map<std::string, int>;
Res Resources(const GameState& s, bool with_float)
{
    Res r;
    const int a = s.active_player_index;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != a) { continue; }
        const std::string k = ClassKey(p);
        if (!p.tapped)    { ++r["U:" + k]; }
        if (p.temp_haste) { ++r["H:" + k]; }
    }
    r["L:life"]   = s.players[a].life;
    r["E:energy"] = s.players[a].energy_counters;
    if (with_float)
    {
        const ManaPool* pools[3] = { &s.floating_mana, &s.floating_creature_mana, &s.floating_bigspell_mana };
        const char* tag[3] = { "F:", "FC:", "FB:" };
        for (int i = 0; i < 3; ++i)
        {
            const ManaPool& f = *pools[i];
            const int v[7] = { f.white, f.blue, f.black, f.red, f.green, f.colorless, f.wild };
            for (int c = 0; c < 7; ++c) { if (v[c] != 0) { r[std::string(tag[i]) + "WUBRGC*"[c]] = v[c]; } }
        }
        if (s.floating_haste_c != 0) { r["F:h"] = s.floating_haste_c; }
    }
    return r;
}

int Get(const Res& r, const std::string& k) { auto it = r.find(k); return it == r.end() ? 0 : it->second; }

// a >= b on every resource. `strict` is set when some resource is strictly larger.
bool Covers(const Res& a, const Res& b, bool* strict)
{
    bool st = false;
    for (const auto& kv : b) { if (Get(a, kv.first) < kv.second) { return false; } }
    for (const auto& kv : a)
    {
        const int y = Get(b, kv.first);
        if (kv.second < y) { return false; }
        if (kv.second > y) { st = true; }
    }
    if (strict != nullptr) { *strict = st; }
    return true;
}

// Where `a` has MORE than `b`, as "key a>b" -- what a "fewer resources" verdict is about.
std::string MoreText(const Res& a, const Res& b)
{
    std::string s;
    for (const auto& kv : a)
    {
        const int y = Get(b, kv.first);
        if (kv.second > y) { s += (s.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second) + ">" + std::to_string(y); }
    }
    return s;
}

// The names of the hand cards a line used (cast, put, pitched): in the root hand, gone at the end.
std::multiset<std::string> LeftHand(const GameState& root, const GameState& fin)
{
    std::multiset<int> now;
    for (const Card& c : fin.players[fin.active_player_index].hand) { now.insert(c.m_number); }
    std::multiset<std::string> out;
    for (const Card& c : root.players[root.active_player_index].hand)
    {
        auto it = now.find(c.m_number);
        if (it != now.end()) { now.erase(it); continue; }
        out.insert(c.m_name.str());
    }
    return out;
}

// Which LINE a realisation played: the hand cards it used, how many spells it cast, how deep it drew.
// Two realisations of one plan that differ here took different continuations after a draw.
std::string LineKey(const GameState& root, const GameState& fin)
{
    std::string k;
    for (const std::string& n : LeftHand(root, fin)) { k += n + ","; }
    k += "|" + std::to_string(fin.spells_cast_this_turn);
    k += "|" + std::to_string(fin.players[fin.active_player_index].library.size());
    return k;
}

bool Includes(const std::multiset<std::string>& big, const std::multiset<std::string>& small)
{
    std::multiset<std::string> b = big;
    for (const std::string& s : small)
    {
        auto it = b.find(s);
        if (it == b.end()) { return false; }
        b.erase(it);
    }
    return true;
}

// ---- the mana table: minimal payments ----------------------------------------------------------
struct Face
{
    TurnSolver::PreTap pt;
    int  col[6]   = { 0, 0, 0, 0, 0, 0 };   // OPTIMISTIC colours it can supply (a fed face: its outputs)
    int  net      = 0;                       // measured net units (a fed face: outputs minus its feed)
    bool fed      = false;
    bool creature = false;                   // the restricted route (OracleCreatureTap)
};
struct SrcClass
{
    bool              land = false;
    std::string       name;
    std::vector<int>  nums;     // untapped copies, ascending m_number
    std::vector<Face> faces;
    int               max_net = 0;
};

int LetterIndex(char ch)
{
    const char* hit = std::strchr(kLetters, ch);
    return (hit == nullptr || ch == '\0') ? -1 : static_cast<int>(hit - kLetters);
}

int AllFloat(const GameState& s)
{ return s.floating_mana.Total() + s.floating_creature_mana.Total() + s.floating_bigspell_mana.Total(); }

// Measure a face on a scratch copy: what the tap adds. A fed face is asked with a generous pre-fill so
// its feed can always be met; its net is then (outputs - feed).
bool MeasureFace(const GameState& state, Face& f)
{
    GameState sc = state;
    if (f.fed)
    {
        sc.floating_mana.white += 2; sc.floating_mana.blue  += 2; sc.floating_mana.black += 2;
        sc.floating_mana.red   += 2; sc.floating_mana.green += 2; sc.floating_mana.colorless += 3;
    }
    const int before = AllFloat(sc);
    // Every pool the tap could have filled (a restricted tap lands in the creature-only pool, or in the
    // general one when it pays a creature's ability), summed colour by colour.
    auto all_pools = [](const GameState& g)
    {
        ManaPool t = g.floating_mana;
        t.AddPool(g.floating_creature_mana);
        t.AddPool(g.floating_bigspell_mana);
        return t;
    };
    const ManaPool fb = all_pools(sc);
    const std::string why = f.creature ? OracleCreatureTap(sc, f.pt) : ApplyHumanPreTap(sc, f.pt);
    if (!why.empty()) { return false; }
    f.net = AllFloat(sc) - before;
    if (!f.fed)
    {
        const ManaPool fa = all_pools(sc);
        f.col[0] = fa.white - fb.white; f.col[1] = fa.blue - fb.blue; f.col[2] = fa.black - fb.black;
        f.col[3] = fa.red - fb.red;     f.col[4] = fa.green - fb.green; f.col[5] = fa.colorless - fb.colorless;
        for (int& c : f.col) { c = std::max(0, c); }
        // A wild unit can be any colour: optimistic.
        if (fa.wild > fb.wild) { for (int c = 0; c < 5; ++c) { f.col[c] += fa.wild - fb.wild; } }
    }
    else
    {
        if (!f.pt.combo.empty()) { for (int c : f.pt.combo) { ++f.col[c]; } }
        else if (f.pt.color >= 0) { f.col[f.pt.color] += std::max(1, f.net + 1); }
        for (int ac : f.pt.aura_colors) { ++f.col[ac]; }
    }
    return true;
}

// THE USEFUL COLOURS (the USER's "table of possible useful mana combinations"): the colours any card the
// player owns can ever spend -- the bill's own, and every coloured pip (hybrid halves included) across
// hand, library, graveyard, battlefield, sideboard and the staged zones. A colour outside this set can
// only ever pay a generic pip, so every such face of one source is the SAME payment: the oracle keeps
// one representative of them and {C} itself (a painland's {C} is painless, Aether Hub's costs no {E}).
// (Not exact for an effect that counts the colours SPENT -- converge, sunburst -- none in the suite.)
void AddCostColors(int& m, const ManaCost& c)
{
    if (c.white > 0) { m |= 1; }
    if (c.blue  > 0) { m |= 2; }
    if (c.black > 0) { m |= 4; }
    if (c.red   > 0) { m |= 8; }
    if (c.green > 0) { m |= 16; }
    for (int h = 0; h < c.hybrid_count; ++h)
    {
        const int a = c.hybrid_pair[h] >> 4, b = c.hybrid_pair[h] & 15;
        if (a >= 0 && a < 5) { m |= 1 << a; }
        if (b >= 0 && b < 5) { m |= 1 << b; }
    }
}
int UsefulColorMask(const GameState& s, const ManaCost& cost)
{
    int m = 0;
    AddCostColors(m, cost);
    // A card in a GAME's zones carries its name and number, not its cost (the cost lives on the
    // definition: EDF seed 3002 T3 read every hand card's cost as empty, so the table kept only the bill's
    // own colours and folded away the {U} a Fertile Ground bonus floats for a later Training Grounds -- an
    // ORACLE-GAP). Read the definition, falling back to the card's own field (a unit test's card has both).
    auto add = [&](const Card& c)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        AddCostColors(m, d != nullptr ? d->card.m_mana_cost : c.m_mana_cost);
    };
    const Player& pl = s.players[s.active_player_index];
    for (const Card& c : pl.hand)      { add(c); }
    for (const Card& c : pl.library)   { add(c); }
    for (const Card& c : pl.graveyard) { add(c); }
    for (const Card& c : pl.sideboard) { add(c); }
    for (const StagedCard& sc : pl.staged_cards) { add(sc.card); }
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == s.active_player_index && !p.def_absent) { add(p.card); } }
    return m;
}

std::vector<SrcClass> BuildSources(const GameState& state, bool for_creature, std::vector<std::string>* unmodeled,
                                   int useful)
{
    std::vector<SrcClass> out;
    std::map<std::string, std::size_t> by_key;
    const int a = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != a || p.tapped) { continue; }
        const HumanPreTapOffer o = HumanPreTapOfferFor(state, p);
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        // RESTRICTED sources: offered to a creature spell through the oracle's own route.
        std::string restricted;   // the coloured faces the creature route adds ("" = none)
        const bool ability_ok = d != nullptr && d->params.colored_creature_ability_ok && PayingCreatureAbility();
        if (d != nullptr && CanTapNow(p, state.battlefield)
            && ((for_creature && (d->params.creature_mana_only || d->params.colored_creature_only)) || ability_ok))
        {
            for (Color c : EffectiveProducesFor(state, a, *d, &p))
            {
                if (d->params.colored_creature_only && c == Color::Colorless) { continue; }   // its plain {C} face
                const char l = kLetters[static_cast<int>(c)];
                if (restricted.find(l) == std::string::npos) { restricted.push_back(l); }
            }
        }
        if (o.free_faces.empty() && o.fed_faces.empty() && restricted.empty())
        {
            // Restricted mana for a noncreature spell is simply not supply -- not a gap.
            if (unmodeled != nullptr && for_creature && o.why_not.find("may be spent only") != std::string::npos)
            { unmodeled->push_back("restricted-mana source " + p.card.m_name.str()); }
            continue;
        }
        if (d != nullptr && !d->params.mana_only_subtype.empty() && unmodeled != nullptr)
        { unmodeled->push_back("subtype-restricted mana from " + p.card.m_name.str()); }
        const std::size_t n_auras = HumanPreTapAuraFaces(state, p).size();
        std::string key = p.card.m_name.str() + "|" + o.free_faces + "|" + restricted + "|";
        // Whatever is attached (a land Aura's bonus -- Utopia Sprawl, Wild Growth) is part of the yield,
        // so a copy carrying one is a different source from its bare twins.
        for (const Permanent& q : state.battlefield)
        { if (q.aura_attached_to == p.card.m_number) { key += "+" + q.card.m_name.str(); } }
        for (const std::string& f : o.fed_faces) { key += f + ","; }
        key += "|" + p.counters.KeyFragment() + "|" + std::to_string(p.storage_counters) + ","
             + std::to_string(p.charge_counters) + "|" + std::to_string(n_auras)
             + (p.entered_this_turn ? "|new" : "|old");
        auto it = by_key.find(key);
        if (it != by_key.end()) { out[it->second].nums.push_back(p.card.m_number); continue; }
        SrcClass sc;
        sc.land = p.card.IsLand();
        sc.name = p.card.m_name.str();
        sc.nums.push_back(p.card.m_number);
        struct Base { TurnSolver::PreTap pt; bool fed; bool creature; };
        std::vector<Base> bases;
        // Is this face worth its own branch? A colour no card can spend is generic-only, so the first
        // such face of each route stands for all of them (see UsefulColorMask).
        auto useful_letter = [&](int c) { return c == 5 || (c >= 0 && c < 5 && ((useful >> c) & 1)); };
        bool rep_free = false, rep_fed = false, rep_cre = false;
        for (char ch : o.free_faces)
        {
            TurnSolver::PreTap t; t.name = sc.name; t.num = p.card.m_number; t.color = LetterIndex(ch);
            if (t.color < 0) { continue; }
            if (!useful_letter(t.color)) { if (rep_free) { continue; } rep_free = true; }
            bases.push_back({ t, false, false });
        }
        for (const std::string& f : o.fed_faces)
        {
            // A one-letter fed face spelled like a free face is the FREE face to ApplyHumanPreTap.
            if (f.size() == 1 && o.free_faces.find(f[0]) != std::string::npos) { continue; }
            TurnSolver::PreTap t; t.name = sc.name; t.num = p.card.m_number;
            bool any_useful = false;
            if (f.size() == 1) { t.color = LetterIndex(f[0]); any_useful = useful_letter(t.color); }
            else
            {
                for (char ch : f)
                { const int c = LetterIndex(ch); t.combo.push_back(c); if (useful_letter(c)) { any_useful = true; } }
            }
            if (!any_useful) { if (rep_fed) { continue; } rep_fed = true; }
            bases.push_back({ t, true, false });
        }
        for (char ch : restricted)
        {
            TurnSolver::PreTap t; t.name = sc.name; t.num = p.card.m_number; t.color = LetterIndex(ch);
            if (!useful_letter(t.color)) { if (rep_cre) { continue; } rep_cre = true; }
            bases.push_back({ t, false, true });
        }
        if (n_auras > 2 && unmodeled != nullptr)
        { unmodeled->push_back("more than two any-colour land Auras on " + sc.name); }
        for (const Base& b : bases)
        {
            // Every any-colour Aura's colour is a choice too (never left `wild`: that is not real mana).
            // The restricted route takes no Aura colour (TapSourceIntoFloat credits it as the payer does).
            std::vector<std::vector<int>> picks(1);
            if (!b.creature)
            {
                // The Aura's colour: every useful colour plus one representative of the rest.
                std::vector<int> aura_cols;
                bool rep = false;
                for (int c = 0; c < 5; ++c)
                {
                    if (!((useful >> c) & 1)) { if (rep) { continue; } rep = true; }
                    aura_cols.push_back(c);
                }
                for (std::size_t ai = 0; ai < std::min<std::size_t>(n_auras, 2); ++ai)
                {
                    std::vector<std::vector<int>> next;
                    for (const std::vector<int>& pk : picks)
                        for (int c : aura_cols) { std::vector<int> q = pk; q.push_back(c); next.push_back(q); }
                    picks.swap(next);
                }
            }
            for (const std::vector<int>& pk : picks)
            {
                Face f; f.pt = b.pt; f.pt.aura_colors = pk; f.fed = b.fed; f.creature = b.creature;
                if (MeasureFace(state, f)) { sc.max_net = std::max(sc.max_net, f.net); sc.faces.push_back(f); }
            }
        }
        if (sc.faces.empty()) { continue; }
        by_key[key] = out.size();
        out.push_back(sc);
    }
    return out;
}

// OPTIMISTIC: could these units pay `cost`? Colour pips by colour, {C} pips by {C}, the rest by total.
// (Hybrid pips are counted under their first colour by ManaCost; read as generic here, which can only
// over-accept.) Every candidate is then paid for real, so optimism only costs a probe.
bool MaybeCovers(const int col[6], int total, const ManaCost& cost)
{
    int need[6] = { cost.white, cost.blue, cost.black, cost.red, cost.green, cost.colorless };
    for (int h = 0; h < cost.hybrid_count; ++h)
    {
        const int first = cost.hybrid_pair[h] >> 4;
        if (first >= 0 && first < 5 && need[first] > 0) { --need[first]; }
    }
    for (int c = 0; c < 6; ++c) { if (col[c] < need[c]) { return false; } }
    return total >= cost.ManaValue();
}

bool PerformTaps(GameState& sc, const PayOption& o)
{
    for (std::size_t i = 0; i < o.taps.size(); ++i)
    {
        const bool cr = i < o.creature_route.size() && o.creature_route[i] != 0;
        if (!(cr ? OracleCreatureTap(sc, o.taps[i]) : ApplyHumanPreTap(sc, o.taps[i])).empty()) { return false; }
    }
    return true;
}

struct Pick { int cls; int face; };

// The payment search for ONE set-aside: classes in battlefield order, each tapped 0..n times with a
// non-decreasing face sequence (copies are interchangeable). At an OPTIMISTIC cover the candidate is
// paid for REAL; if it pays it is recorded and not extended with free taps (a superset only taps more
// of what could be tapped later) -- but it IS extended with FED conversions, because a converter
// (Cascade Bluffs, an Izzet Signet) needs a feeder that may be gone by the time it is wanted: the
// Island that feeds Bluffs now can pay Preordain AND float the {U} Curiosity needs. If the real
// payment fails, the optimistic arithmetic was wrong (a yield it over-counted) and the search goes on
// extending, so no minimal set hides behind an optimistic one.
struct PayEnum
{
    const GameState&                state;
    const ManaCost&                 cost;
    bool                            for_creature;
    const std::vector<SrcClass>&    srcs;
    const std::vector<int>&         suffix_max;
    const PayOption&                stash;
    std::unordered_set<std::uint64_t>& seen;
    std::vector<PayOption>&         out;
    std::size_t                     probes = 0;
    std::size_t                     cap    = 4096;
    bool                            capped = false;
    std::vector<Pick>               cur;

    PayOption Build(const std::vector<Pick>& picks) const
    {
        PayOption o = stash;
        // Free taps first, then the restricted route, then fed taps (a feed is paid from what is
        // already floating); within a class the lowest-numbered copies tap first.
        std::map<int, int> used;
        std::vector<TurnSolver::PreTap> fed, cre;
        for (const Pick& pk : picks)
        {
            const SrcClass& sc = srcs[static_cast<std::size_t>(pk.cls)];
            const Face& fcx = sc.faces[static_cast<std::size_t>(pk.face)];
            TurnSolver::PreTap t = fcx.pt;
            t.num = sc.nums[static_cast<std::size_t>(used[pk.cls]++)];
            (fcx.fed ? fed : (fcx.creature ? cre : o.taps)).push_back(t);
        }
        o.creature_route.assign(o.taps.size(), 0);
        for (const TurnSolver::PreTap& t : cre) { o.taps.push_back(t); o.creature_route.push_back(1); }
        for (const TurnSolver::PreTap& t : fed) { o.taps.push_back(t); o.creature_route.push_back(0); }
        return o;
    }

    // Pay for real on a scratch copy; record a NEW result. Returns whether it paid.
    bool Verify(const std::vector<Pick>& picks)
    {
        if (++probes > cap) { capped = true; return false; }
        PayOption o = Build(picks);
        GameState sc = state;
        auto why = [&](const char* w)
        {
            if (Level() >= 4)
            {
                std::lock_guard<std::mutex> lk(g_print_mu);
                std::fprintf(stderr, "[mana-oracle]         verify %s -> %s (float after taps %s)\n",
                             TapsText(o).c_str(), w, PoolText(sc.floating_mana, sc.floating_haste_c).c_str());
            }
            return false;
        };
        if (!PerformTaps(sc, o)) { return why("a tap was refused"); }
        ManaPool& sf = sc.floating_mana;
        if (sf.white < o.stash.white || sf.blue < o.stash.blue || sf.black < o.stash.black
            || sf.red < o.stash.red || sf.green < o.stash.green || sf.colorless < o.stash.colorless)
        { return false; }
        sf.white -= o.stash.white; sf.blue -= o.stash.blue; sf.black -= o.stash.black;
        sf.red -= o.stash.red; sf.green -= o.stash.green; sf.colorless -= o.stash.colorless;
        sc.floating_haste_c -= o.stash_haste;
        sf.snow_units = std::min(sf.snow_units, sf.Total());
        t_float_only = true;
        const bool paid = TapForCostShared(sc, cost, for_creature, nullptr, true);
        t_float_only = false;
        if (!paid) { return why("the float-only payment failed"); }
        sf.white += o.stash.white; sf.blue += o.stash.blue; sf.black += o.stash.black;
        sf.red += o.stash.red; sf.green += o.stash.green; sf.colorless += o.stash.colorless;
        sc.floating_haste_c += o.stash_haste;
        // One option per distinct RESULT: two set-asides the payment never touched are one payment.
        if (seen.insert(TurnSolver::MidTurnStateKeyOf(sc)).second) { out.push_back(std::move(o)); }
        return true;
    }

    int UsedCopies(int cls) const
    {
        int n = 0;
        for (const Pick& pk : cur) { if (pk.cls == cls) { ++n; } }
        return n;
    }

    // Add fed conversions to a paying set, one at a time, classes from `from` on.
    void ExtendFed(std::size_t from)
    {
        for (std::size_t c = from; c < srcs.size() && !capped; ++c)
        {
            const SrcClass& sc = srcs[c];
            if (UsedCopies(static_cast<int>(c)) >= static_cast<int>(sc.nums.size())) { continue; }
            for (std::size_t fi = 0; fi < sc.faces.size() && !capped; ++fi)
            {
                if (!sc.faces[fi].fed) { continue; }
                cur.push_back({ static_cast<int>(c), static_cast<int>(fi) });
                if (Verify(cur)) { ExtendFed(c); }
                cur.pop_back();
            }
        }
    }

    void Dfs(std::size_t ci, const int col[6], int total)
    {
        if (capped) { return; }
        if (MaybeCovers(col, total, cost) && Verify(cur)) { ExtendFed(0); return; }
        if (ci >= srcs.size()) { return; }
        if (total + suffix_max[ci] < cost.ManaValue()) { return; }
        Dfs(ci + 1, col, total);   // skip this class...
        const SrcClass& sc = srcs[ci];
        std::vector<int> faces;
        for (std::size_t m = 1; m <= sc.nums.size() && !capped; ++m)
        {
            faces.assign(m, 0);
            while (true)
            {
                int c2[6]; std::copy(col, col + 6, c2);
                int t2 = total;
                const std::size_t mark = cur.size();
                for (int fi : faces)
                {
                    const Face& f = sc.faces[static_cast<std::size_t>(fi)];
                    for (int c = 0; c < 6; ++c) { c2[c] += f.col[c]; }
                    t2 += f.net;
                    cur.push_back({ static_cast<int>(ci), fi });
                }
                Dfs(ci + 1, c2, t2);
                cur.resize(mark);
                if (capped) { return; }
                int pos = static_cast<int>(m) - 1;
                while (pos >= 0 && faces[static_cast<std::size_t>(pos)] == static_cast<int>(sc.faces.size()) - 1) { --pos; }
                if (pos < 0) { break; }
                const int v = faces[static_cast<std::size_t>(pos)] + 1;
                for (std::size_t q = static_cast<std::size_t>(pos); q < m; ++q) { faces[q] = v; }
            }
        }
    }
};

// The distinct set-asides of the general floating pool: every sub-multiset of its typed units, the
// haste-tagged {C} split from the plain {C}. Capped; a pool too large to enumerate is reported.
std::vector<PayOption> StashChoices(const GameState& s, std::vector<std::string>* unmodeled)
{
    std::vector<PayOption> out;
    const ManaPool& f = s.floating_mana;
    const int haste = std::min(s.floating_haste_c, f.colorless);
    const int plain_c = f.colorless - haste;
    const int cnt[7] = { f.white, f.blue, f.black, f.red, f.green, plain_c, haste };
    long long combos = 1;
    for (int c : cnt) { combos *= (c + 1); }
    if (combos > 256)
    {
        if (unmodeled != nullptr) { unmodeled->push_back("floating pool too large to enumerate set-asides"); }
        out.push_back(PayOption{});
        return out;
    }
    int k[7] = { 0, 0, 0, 0, 0, 0, 0 };
    while (true)
    {
        PayOption o;
        o.stash.white = k[0]; o.stash.blue = k[1]; o.stash.black = k[2]; o.stash.red = k[3]; o.stash.green = k[4];
        o.stash.colorless = k[5] + k[6];
        o.stash_haste = k[6];
        out.push_back(o);
        int i = 0;
        while (i < 7 && k[i] == cnt[i]) { k[i] = 0; ++i; }
        if (i == 7) { break; }
        ++k[i];
    }
    return out;
}

} // namespace

std::vector<PayOption> MinimalPayments(const GameState& state, const ManaCost& cost, bool for_creature,
                                       std::vector<std::string>* unmodeled, bool creature_ability)
{
    // The payment's context, as the frontier saw it: a creature's activated ability being paid (the
    // Secluded Courtyard clause). Replayed around every measure and verification below.
    std::optional<CreatureAbilityPayScope> _cap;
    if (creature_ability) { _cap.emplace(); }
    std::vector<PayOption> out;
    const std::vector<SrcClass> srcs = BuildSources(state, for_creature, unmodeled, UsefulColorMask(state, cost));
    if (Level() >= 4)
    {
        std::lock_guard<std::mutex> lk(g_print_mu);
        std::fprintf(stderr, "[mana-oracle]       table for %s%s%s:\n", cost.ToString().c_str(),
                     for_creature ? " creature" : "", creature_ability ? " creature-ability" : "");
        for (const SrcClass& c : srcs)
        {
            std::string fs;
            for (const Face& f : c.faces)
            {
                PayOption o; o.taps.push_back(f.pt); o.creature_route.push_back(f.creature ? 1 : 0);
                fs += " [" + TapsText(o) + (f.fed ? " fed" : "") + " net" + std::to_string(f.net) + "]";
            }
            std::fprintf(stderr, "[mana-oracle]         %s x%zu:%s\n", c.name.c_str(), c.nums.size(), fs.c_str());
        }
    }
    std::vector<int> suffix_max(srcs.size() + 1, 0);
    for (std::size_t i = srcs.size(); i-- > 0;)
    { suffix_max[i] = suffix_max[i + 1] + srcs[i].max_net * static_cast<int>(srcs[i].nums.size()); }
    // Restricted floating pools (creature-only, big-spell-only) are the payer's to drain -- it spends
    // them first where they are legal, which is never worse -- so they count as supply but are never
    // set aside.
    const ManaPool& fc  = state.floating_creature_mana;
    const ManaPool& fbg = state.floating_bigspell_mana;
    const int extra[6] = { (for_creature ? fc.white : 0) + fbg.white, (for_creature ? fc.blue : 0) + fbg.blue,
                           (for_creature ? fc.black : 0) + fbg.black, (for_creature ? fc.red : 0) + fbg.red,
                           (for_creature ? fc.green : 0) + fbg.green,
                           (for_creature ? fc.colorless : 0) + fbg.colorless };
    std::unordered_set<std::uint64_t> seen;
    for (const PayOption& st : StashChoices(state, unmodeled))
    {
        const ManaPool& f = state.floating_mana;
        int col[6] = { f.white - st.stash.white, f.blue - st.stash.blue, f.black - st.stash.black,
                       f.red - st.stash.red, f.green - st.stash.green, f.colorless - st.stash.colorless };
        for (int c = 0; c < 6; ++c) { col[c] += extra[c]; }
        int total = col[0] + col[1] + col[2] + col[3] + col[4] + col[5] + f.wild;
        if (f.wild > 0) { for (int c = 0; c < 5; ++c) { col[c] += f.wild; } }
        PayEnum pe{ state, cost, for_creature, srcs, suffix_max, st, seen, out };
        pe.Dfs(0, col, total);
        if (pe.capped && unmodeled != nullptr) { unmodeled->push_back("payment enumeration capped"); }
    }
    return out;
}

// The TAP-AHEAD frontier's options: tap nothing ahead, or any set of up to `count` sources (lands only for
// an ETB "untap N lands"), each for any useful face -- every one verified by performing it on a copy.
std::vector<PayOption> TapAheadOptions(const GameState& state, int count, bool lands_only,
                                       std::vector<std::string>* unmodeled)
{
    std::vector<PayOption> out;
    out.push_back(PayOption{});   // tap nothing ahead
    std::vector<SrcClass> srcs = BuildSources(state, /*for_creature=*/false, nullptr, UsefulColorMask(state, ManaCost{}));
    if (lands_only)
    {
        srcs.erase(std::remove_if(srcs.begin(), srcs.end(), [](const SrcClass& c) { return !c.land; }), srcs.end());
    }
    std::unordered_set<std::uint64_t> seen;
    seen.insert(TurnSolver::MidTurnStateKeyOf(state));
    const std::size_t cap = 512;
    std::vector<Pick> cur;
    bool capped = false;
    std::function<void(std::size_t, int)> rec = [&](std::size_t ci, int left)
    {
        if (out.size() >= cap) { capped = true; return; }
        if (!cur.empty())
        {
            PayOption o;
            std::map<int, int> used;
            std::vector<TurnSolver::PreTap> fed;
            for (const Pick& pk : cur)
            {
                const SrcClass& sc = srcs[static_cast<std::size_t>(pk.cls)];
                const Face& f = sc.faces[static_cast<std::size_t>(pk.face)];
                TurnSolver::PreTap t = f.pt;
                t.num = sc.nums[static_cast<std::size_t>(used[pk.cls]++)];
                (f.fed ? fed : o.taps).push_back(t);
            }
            for (const TurnSolver::PreTap& t : fed) { o.taps.push_back(t); }
            GameState sc = state;
            if (PerformTaps(sc, o) && seen.insert(TurnSolver::MidTurnStateKeyOf(sc)).second) { out.push_back(o); }
        }
        if (left <= 0) { return; }
        for (std::size_t c = ci; c < srcs.size(); ++c)
        {
            const SrcClass& sc = srcs[c];
            // m copies of class c, faces non-decreasing, then recurse past c.
            std::vector<int> faces;
            for (std::size_t m = 1; m <= sc.nums.size() && static_cast<int>(m) <= left; ++m)
            {
                faces.assign(m, 0);
                while (true)
                {
                    const std::size_t mark = cur.size();
                    for (int fi : faces) { cur.push_back({ static_cast<int>(c), fi }); }
                    rec(c + 1, left - static_cast<int>(m));
                    cur.resize(mark);
                    if (out.size() >= cap) { capped = true; return; }
                    int pos = static_cast<int>(m) - 1;
                    while (pos >= 0 && faces[static_cast<std::size_t>(pos)] == static_cast<int>(sc.faces.size()) - 1) { --pos; }
                    if (pos < 0) { break; }
                    const int v = faces[static_cast<std::size_t>(pos)] + 1;
                    for (std::size_t q = static_cast<std::size_t>(pos); q < m; ++q) { faces[q] = v; }
                }
            }
        }
    };
    rec(0, count);
    if (capped && unmodeled != nullptr) { unmodeled->push_back("tap-ahead enumeration capped"); }
    return out;
}

namespace
{
struct Outcome
{
    GameState                  st;
    std::vector<std::string>   cast_drops;
    std::vector<std::string>   act_drops;
    std::multiset<std::string> left;        // hand cards the line used
    std::string                line;        // LineKey
    std::vector<PayOption>     script;
    std::vector<int>           pay_steps;   // Hook::pay_steps: the human-sequence step of each payment
    std::string                unscripted;  // a mana source the ENGINE tapped outside any payment
    int                        first_fail = -1;  // the first ENGINE step that failed (the census replay)
    // No drop reported: the engine-comparable sense (an optional payment with no option fails the same
    // way in the engine's own realisation, so such an outcome is still the engine's line).
    bool Full() const { return cast_drops.empty() && act_drops.empty(); }
    // ...AND no payment that had no option: the sense a CLAIM needs ("every action pays"), because an
    // unpayable step that is not a cast -- a suspend, an aura swap -- fails without a drop-sink entry.
    bool Clean() const
    {
        if (!Full()) { return false; }
        for (const PayOption& p : script) { if (p.fail) { return false; } }
        return true;
    }
};

struct Realization
{
    std::vector<Outcome>     outcomes;
    std::vector<std::string> unmodeled;
    bool                     capped = false;
};

bool IsManaSourceNow(const GameState& s, const Permanent& p)
{
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, p);
    return !o.free_faces.empty() || !o.fed_faces.empty() || o.why_not.find("may be spent only") != std::string::npos;
}

struct FrontierRec
{
    std::vector<PayOption> script;
    GameState              st;
    ManaCost               cost;
    bool                   for_creature = false;
    std::size_t            index = 0;
    int                    tapahead = 0;
    bool                   lands_only = true;
    bool                   creature_ability = false;
};

// Apply `plan` to a copy of `root` once per payment script, LAYERED by payment index: every script of
// length k is applied, the frontier boards they reach are Pareto-pruned (same core, dominated
// resources INCLUDING floating mana -> dropped; under goldfish monotonicity a board with every
// resource another has reaches everything that one reaches), and the survivors' minimal payments
// make layer k+1. `stop_at_full`: return at the first outcome that realises every action.
// The ROLLBACK CENSUS's two modes: `prefix` -- every script starts with these steps (k ENGINE steps: keep
// the engine's first k mana decisions, enumerate the rest); `all_engine` -- one apply whose every step is
// the engine's, i.e. the engine's own line replayed through the hook.
// `want` (with stop_at_full): stop only at a full outcome that used at least these hand cards.
Realization Realize(const GameState& root, const TurnSolver::Plan& plan, bool pre, bool stop_at_full,
                    const std::vector<PayOption>& prefix = {}, bool all_engine = false,
                    const std::multiset<std::string>* want = nullptr)
{
    Realization r;
    std::vector<std::vector<PayOption>> layer(1, prefix);
    int nodes = 0;
    // Battlefield permanents the plan itself uses (a {T}-cost activation, a sacrifice): an engine tap
    // of one of THOSE is the action, not an unscripted payment.
    std::set<int> plan_srcs;
    std::set<std::string> plan_names;
    for (const Action& a : plan.actions)
    {
        if (a.sac_source_id != 0) { plan_srcs.insert(a.sac_source_id); }
        if (a.sac_victim_id != 0) { plan_srcs.insert(a.sac_victim_id); }
        if (a.kind != Action::Kind::CastFromHand) { plan_names.insert(a.card_name.str()); }
    }
    const int act = root.active_player_index;
    while (!layer.empty())
    {
        std::vector<FrontierRec> fronts;
        for (std::vector<PayOption>& script : layer)
        {
            if (nodes >= NodeCap()) { r.capped = true; break; }
            ++nodes;
            g_applies.fetch_add(1, std::memory_order_relaxed);
            GameState copy = root;
            FrontierRec fr;
            Hook h;
            h.target  = &copy;
            h.script  = script;
            h.f_state = &fr.st;
            h.all_engine = all_engine;
            std::vector<std::string> cast_drops, act_drops;
            std::vector<std::string>* saved_cast = g_play_dropped_cast_sink;
            std::vector<std::string>* saved_act  = g_act_drop_sink;
            g_play_dropped_cast_sink = &cast_drops;
            g_act_drop_sink          = &act_drops;
            const long long ta0 = g_tap_ahead_calls;
            const long long uc0 = g_untap_choice_calls;
            t_hook = &h;
            TurnSolver::ApplyPlan(copy, plan, pre);
            t_hook = nullptr;
            // (A tap-ahead inside an ENGINE step is the engine's step, not a tap outside the script.)
            const bool engine_tapahead = (g_tap_ahead_calls - ta0) != h.engine_tapaheads;
            const bool engine_untap    = g_untap_choice_calls != uc0;
            g_play_dropped_cast_sink = saved_cast;
            g_act_drop_sink          = saved_act;
            if (!h.tap_error.empty())
            {
                r.unmodeled.push_back("scripted payment refused: " + h.tap_error);
                continue;
            }
            if (h.frontier)
            {
                g_frontiers.fetch_add(1, std::memory_order_relaxed);
                fr.script       = std::move(script);
                fr.cost         = h.f_cost;
                fr.for_creature = h.f_for_creature;
                fr.index        = h.next;
                fr.tapahead     = h.f_tapahead;
                fr.lands_only   = h.f_lands_only;
                fr.creature_ability = h.f_creature_ability;
                fronts.push_back(std::move(fr));
                continue;
            }
            Outcome o;
            o.cast_drops = std::move(cast_drops);
            o.act_drops  = std::move(act_drops);
            o.script     = script;
            o.pay_steps  = h.pay_steps;
            o.first_fail = h.first_fail;
            o.left       = LeftHand(root, copy);
            o.line       = LineKey(root, copy);
            // Did the ENGINE tap a mana source on its own (a tap-ahead, an untap effect's choice)? The
            // oracle did not choose that tap, so it cannot vouch for completeness there.
            if (engine_tapahead) { o.unscripted = "(an engine tap-ahead before an untap effect)"; }
            // ...or chose WHICH lands an untap effect brought back (Cloud of Faeries, Reality Spasm, an
            // untap ritual): a heuristic pick among different lands that the oracle does not branch on.
            if (engine_untap)
            { o.unscripted += (o.unscripted.empty() ? "" : ", ") + std::string("(an untap effect's choice of lands)"); }
            std::set<int> by_script(h.tapped_by_script.begin(), h.tapped_by_script.end());
            for (const Permanent& p0 : root.battlefield)
            {
                if (p0.controller_index != act || p0.tapped || by_script.count(p0.card.m_number) != 0) { continue; }
                if (plan_srcs.count(p0.card.m_number) != 0 || plan_names.count(p0.card.m_name.str()) != 0) { continue; }
                for (const Permanent& p1 : copy.battlefield)
                {
                    if (p1.card.m_number != p0.card.m_number) { continue; }
                    if (p1.tapped && IsManaSourceNow(root, p0))
                    { o.unscripted += (o.unscripted.empty() ? "" : ", ") + p0.card.m_name.str(); }
                    break;
                }
            }
            o.st = std::move(copy);
            // (An outcome resting on a mana choice the engine made on its own is no caller's hit, so it does
            // not end the search either: a scripted payment further on may still realise the line.)
            const bool full = o.Clean() && o.unscripted.empty() && (want == nullptr || Includes(o.left, *want));
            r.outcomes.push_back(std::move(o));
            if (stop_at_full && full) { return r; }
        }
        if (r.capped) { break; }
        // ---- Pareto-prune the frontier boards of this layer -----------------------------------
        const std::size_t n = fronts.size();
        std::vector<std::uint64_t> keys(n);
        std::vector<Res> res(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            keys[i] = CoreKey(fronts[i].st, /*canonical=*/false) ^ (0x9e3779b97f4a7c15ULL * (fronts[i].index + 1))
                    ^ std::hash<std::string>{}(fronts[i].cost.ToString() + (fronts[i].for_creature ? "c" : "n")
                                               + (fronts[i].creature_ability ? "a" : "s")
                                               + "t" + std::to_string(fronts[i].tapahead));
            res[i]  = Resources(fronts[i].st, /*with_float=*/true);
        }
        std::vector<char> keep(n, 1);
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = 0; j < n && keep[i]; ++j)
            {
                if (i == j || !keep[j] || keys[i] != keys[j]) { continue; }
                bool strict = false;
                if (Covers(res[j], res[i], &strict) && (strict || j < i)) { keep[i] = 0; }
            }
        }
        std::vector<std::vector<PayOption>> next;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!keep[i]) { g_pruned.fetch_add(1, std::memory_order_relaxed); continue; }
            FrontierRec& f = fronts[i];
            std::vector<PayOption> opts = f.tapahead > 0
                ? TapAheadOptions(f.st, f.tapahead, f.lands_only, &r.unmodeled)
                : MinimalPayments(f.st, f.cost, f.for_creature, &r.unmodeled, f.creature_ability);
            g_options.fetch_add(static_cast<long long>(opts.size()), std::memory_order_relaxed);
            if (Level() >= 3 && (TraceTurn() == 0 || TraceTurn() == root.turn_number))
            {
                std::lock_guard<std::mutex> lk(g_print_mu);
                std::fprintf(stderr, "[mana-oracle]   frontier pay%zu %s%s after %s | %s\n", f.index + 1,
                             f.tapahead > 0 ? ("TAP-AHEAD up to " + std::to_string(f.tapahead)).c_str()
                                            : ("cost=" + f.cost.ToString()).c_str(),
                             f.for_creature ? " (creature)" : "",
                             ScriptText(f.script).c_str(), BoardText(f.st).c_str());
                for (const PayOption& o : opts)
                { std::fprintf(stderr, "[mana-oracle]     option %s\n", TapsText(o).c_str()); }
            }
            if (opts.empty())
            {
                // No way to pay this one: the cast is dropped, as the engine would drop it, and the rest
                // of the line is still explored.
                PayOption fail;
                fail.fail = true;
                opts.push_back(fail);
            }
            for (PayOption& o : opts)
            {
                std::vector<PayOption> s = f.script;
                o.what = f.tapahead > 0 ? std::string("tap-ahead") : f.cost.ToString();
                s.push_back(std::move(o));
                next.push_back(std::move(s));
            }
        }
        layer.swap(next);
    }
    return r;
}

// What separates the engine's outcome from an oracle outcome, field by field (an ORACLE-GAP is only
// actionable if it says WHICH field the two realisations disagree on).
std::string DiffText(const GameState& e, const GameState& o)
{
    std::string s;
    const int a = e.active_player_index;
    auto add = [&](const std::string& t) { s += (s.empty() ? "" : "; ") + t; };
    std::map<int, const Permanent*> om;
    for (const Permanent& p : o.battlefield) { om[p.card.m_number] = &p; }
    std::set<int> seen;
    for (const Permanent& p : e.battlefield)
    {
        seen.insert(p.card.m_number);
        auto it = om.find(p.card.m_number);
        const std::string nm = p.card.m_name.str() + "#" + std::to_string(p.card.m_number);
        if (it == om.end()) { add(nm + " only in engine"); continue; }
        const Permanent& q = *it->second;
        if (p.tapped != q.tapped)
        { add(nm + (p.tapped ? std::string(" tapped by engine only") : std::string(" tapped by oracle only"))); }
        if (p.counters.KeyFragment() != q.counters.KeyFragment())
        { add(nm + " counters engine{" + p.counters.KeyFragment() + "} oracle{" + q.counters.KeyFragment() + "}"); }
        if (p.storage_counters != q.storage_counters) { add(nm + " storage counters differ"); }
        if (p.temp_haste != q.temp_haste) { add(nm + " haste differs"); }
        if (p.temp_power_bonus != q.temp_power_bonus || p.temp_tough_bonus != q.temp_tough_bonus)
        { add(nm + " pump differs"); }
    }
    for (const Permanent& q : o.battlefield)
    { if (seen.count(q.card.m_number) == 0) { add(q.card.m_name.str() + "#" + std::to_string(q.card.m_number) + " only in oracle"); } }
    if (e.players[a].life != o.players[a].life)
    { add("life engine " + std::to_string(e.players[a].life) + " oracle " + std::to_string(o.players[a].life)); }
    if (e.players[a].energy_counters != o.players[a].energy_counters) { add("energy differs"); }
    if (e.players[a].hand.size() != o.players[a].hand.size())  { add("hand size differs"); }
    if (e.players[a].graveyard.size() != o.players[a].graveyard.size()) { add("graveyard size differs"); }
    if (e.players[a].library.size() != o.players[a].library.size()) { add("library size differs"); }
    if (e.players[1 - a].life != o.players[1 - a].life) { add("opponent life differs"); }
    if (e.spells_cast_this_turn != o.spells_cast_this_turn) { add("spells cast differs"); }
    if (CoreKey(e) == CoreKey(o)) { add("same core"); }
    else if (s.empty()) { add("core keys differ in a field not listed here"); }
    add("float engine " + PoolText(e.floating_mana, e.floating_haste_c) + " oracle " + PoolText(o.floating_mana, o.floating_haste_c));
    return s;
}

std::string DropText(const std::vector<std::string>& c, const std::vector<std::string>& a)
{
    std::string s;
    for (const std::string& x : c) { s += (s.empty() ? "" : ", ") + std::string("cast ") + x; }
    for (const std::string& x : a) { s += (s.empty() ? "" : ", ") + std::string("act ") + x; }
    return s;
}

// The resources o has that e does not, as text and as a class.
Cls DominanceClass(const Res& o, const Res& e, bool pre, std::string* what)
{
    Cls worst = C_DOM_LAND;
    auto rank = [](Cls c)
    {
        switch (c)
        {
            case C_DOM_CREATURE: return 5;
            case C_DOM_ONESHOT:  return 4;
            case C_DOM_HASTE:    return 3;
            case C_DOM_LIFE:     return 2;
            case C_DOM_OTHER:    return 1;
            default:             return 0;
        }
    };
    for (const auto& kv : o)
    {
        const std::string& k = kv.first;
        const int d = kv.second - Get(e, k);
        if (d <= 0) { continue; }
        Cls c = C_DOM_OTHER;
        std::string w;
        if (k.rfind("U:", 0) == 0)
        {
            const std::string body = k.substr(2);
            const std::string name = body.substr(0, body.find('|'));
            const bool crea = body.find("|C|") != std::string::npos;
            const bool newb = body.find("|new|") != std::string::npos;
            const CardDefinition* d0 = CardDatabase::Instance().Lookup(name);
            const bool oneshot = d0 != nullptr && IsPaySacSource(*d0);
            if (oneshot)                   { c = C_DOM_ONESHOT;  w = "keeps " + name; }
            else if (crea && pre && !newb) { c = C_DOM_CREATURE; w = "keeps " + name + " untapped (attacker)"; }
            else                           { c = C_DOM_LAND;     w = "keeps " + name + " untapped"; }
            if (d > 1) { w += " x" + std::to_string(d); }
        }
        else if (k.rfind("H:", 0) == 0) { c = C_DOM_HASTE; w = "haste on " + k.substr(2, k.find('|') - 2); }
        else if (k == "L:life")          { c = C_DOM_LIFE;  w = "+" + std::to_string(d) + " life"; }
        else if (k == "E:energy")        { c = C_DOM_OTHER; w = "+" + std::to_string(d) + " energy"; }
        else                             { c = C_DOM_OTHER; w = "+" + std::to_string(d) + " " + k; }
        if (what != nullptr) { *what += (what->empty() ? "" : "; ") + w; }
        if (rank(c) > rank(worst)) { worst = c; }
    }
    return worst;
}

void Emit(Cls c, const GameState& root, bool pre, const TurnSolver::Plan& plan, const std::string& detail)
{
    if (!Count(c)) { return; }
    std::lock_guard<std::mutex> lk(g_print_mu);
    std::fprintf(stderr, "[mana-oracle] %s %s plan={%s} | %s | %s\n", kClsName[c], Ctx(root, pre).c_str(),
                 PlanText(plan).c_str(), detail.c_str(), BoardText(root).c_str());
}

// A plan's casts permuted (searched_order: the apply casts them in vector order).
// One action as text: what it is, and every choice that makes two actions on the same card differ.
std::string ActionKey(const Action& x)
{
    std::string t = std::string(KindText(x.kind)) + ":" + x.card_name.str();
    if (x.chosen_x > 0)        { t += "(x" + std::to_string(x.chosen_x) + ")"; }
    if (x.enchant_target > 0)  { t += "->" + std::to_string(x.enchant_target); }
    if (!x.tutor_target.empty()) { t += ">" + x.tutor_target; }
    if (x.sac_source_id > 0)   { t += "@" + std::to_string(x.sac_source_id); }
    return t;
}

// Every other order of a plan's casts -- and, with `with_acts`, of its activations too (the land
// drop and a sacrifice-a-land cast keep their places). `*all_tried` says whether that is EVERY order:
// past five movable actions none is generated, so a caller claiming "no order realises it" must not.
std::vector<TurnSolver::Plan> OtherOrders(const TurnSolver::Plan& plan, bool with_acts = false,
                                          bool* all_tried = nullptr)
{
    std::vector<TurnSolver::Plan> out;
    std::vector<std::size_t> slots;
    for (std::size_t i = 0; i < plan.actions.size(); ++i)
    {
        const Action& a = plan.actions[i];
        if (a.kind == Action::Kind::PlayLand || a.sacrifice_land) { continue; }
        if (a.kind == Action::Kind::CastFromHand || with_acts) { slots.push_back(i); }
    }
    if (all_tried != nullptr) { *all_tried = slots.size() <= 5; }
    if (slots.size() < 2 || slots.size() > 5) { return out; }
    std::vector<std::size_t> perm(slots.size());
    for (std::size_t i = 0; i < perm.size(); ++i) { perm[i] = i; }
    std::set<std::string> seen;   // two copies of a card swapped are the same order
    {
        std::string k;
        for (const Action& a : plan.actions) { k += ActionKey(a) + "|"; }
        seen.insert(k);
    }
    while (std::next_permutation(perm.begin(), perm.end()))
    {
        TurnSolver::Plan p = plan;
        for (std::size_t i = 0; i < slots.size(); ++i) { p.actions[slots[i]] = plan.actions[slots[perm[i]]]; }
        std::string k;
        for (const Action& a : p.actions) { k += ActionKey(a) + "|"; }
        if (!seen.insert(k).second) { continue; }
        p.searched_order = true;
        out.push_back(p);
    }
    return out;
}

// The engine's own realisation of a plan (no hook: exactly what the search scores).
struct EngineReal
{
    GameState                st;
    std::vector<std::string> cast, act;
    bool Full() const { return cast.empty() && act.empty(); }
};
EngineReal EngineApply(const GameState& root, bool pre, const TurnSolver::Plan& plan)
{
    EngineReal e{ root, {}, {} };
    std::vector<std::string>* saved_cast = g_play_dropped_cast_sink;
    std::vector<std::string>* saved_act  = g_act_drop_sink;
    g_play_dropped_cast_sink = &e.cast;
    g_act_drop_sink          = &e.act;
    TurnSolver::ApplyPlan(e.st, plan, pre);
    g_play_dropped_cast_sink = saved_cast;
    g_act_drop_sink          = saved_act;
    return e;
}

// A plan's ACTIONS as a set (order, and an order variant's searched_order, ignored) plus its land:
// two plans with the same key offer the search the same option.
std::string SiblingKey(const TurnSolver::Plan& p)
{
    std::vector<std::string> a;
    for (const Action& x : p.actions) { a.push_back(ActionKey(x)); }
    std::sort(a.begin(), a.end());
    std::string k = !p.land_decided ? "land?" : ("land=" + p.land_to_play);
    for (const std::string& t : a) { k += "|" + t; }
    return k;
}

// The hand cards a plan means to use (a cast, a Vial put, a put-from-hand).
std::multiset<std::string> PlanHandCards(const TurnSolver::Plan& p)
{
    std::multiset<std::string> m;
    for (const Action& x : p.actions)
    {
        if (x.kind == Action::Kind::CastFromHand || x.kind == Action::Kind::ActivateVial
            || x.kind == Action::Kind::PutFromHandAbility)
        { m.insert(x.card_name.str()); }
    }
    return m;
}

// ---- the ROLLBACK CENSUS for one lost line (an APPLY-DROP) ------------------------------------------
// The engine's line is replayed through the hook with every mana decision the ENGINE's own (an ENGINE
// step: ManaOracle.h) -- it must reproduce the engine's line exactly, or the census has no verdict -- and
// the first engine payment that failed is step f. Then for k = f, f-1, ..., 0: keep the engine's first k
// decisions and let the oracle re-make the rest. The largest k that pays for every action gives the
// DEPTH f - k: how far back a rollback has to reach. RollbackCore measures; RollbackCensus counts it and
// returns the verdict for the flag's detail; AuditLine hands it to the viewer.
struct Census
{
    Rb                     bucket = RB_INCONSISTENT;
    int                    keep = -1;      // the largest k that pays (a depth bucket)
    int                    failed = -1;    // f
    std::vector<PayOption> script;         // ...and how it pays (k engine steps, then the oracle's)
    std::string            detail;         // why, for every bucket that is not a depth
    bool                   later = false;  // a lost cast the plan did not declare
    bool                   rescued = false;// MTG_PAY_ROLLBACK (forced on) recovers it
};
Census RollbackCore(const GameState& root, bool pre, const TurnSolver::Plan& plan, const EngineReal& er,
                    const std::multiset<std::string>& e_want)
{
    Census c;
    // Was every lost action declared in the plan (so it was known before any of the plan's payments)?
    // Anything else -- an activation or cast the plan does not name -- came later.
    std::multiset<std::string> declared;
    for (const Action& a : plan.actions) { declared.insert(a.kind == Action::Kind::CastFromHand ? a.card_name.str() : ActionKey(a)); }
    for (const std::string& x : er.cast) { if (declared.count(x) == 0) { c.later = true; } }
    // MTG_PAY_ROLLBACK, forced on for one engine apply: does the shipped one-swap rescue recover it?
    // (Its ledger starts at this decision: a rescue reaching an earlier decision's payment is not seen.)
    const std::int8_t saved_arm = heurarm::t_arm[heurarm::PAY_ROLLBACK];
    heurarm::t_arm[heurarm::PAY_ROLLBACK] = 1;
    const EngineReal rb = EngineApply(root, pre, plan);
    heurarm::t_arm[heurarm::PAY_ROLLBACK] = saved_arm;
    c.rescued = rb.Full() && Includes(LeftHand(root, rb.st), e_want);
    const Realization all = Realize(root, plan, pre, /*stop_at_full=*/false, {}, /*all_engine=*/true);
    auto sorted = [](std::vector<std::string> v) { std::sort(v.begin(), v.end()); return v; };
    if (all.outcomes.size() != 1 || all.outcomes[0].st.battlefield.size() != er.st.battlefield.size()
        || CoreKey(all.outcomes[0].st, /*canonical=*/false) != CoreKey(er.st, /*canonical=*/false)
        || Resources(all.outcomes[0].st, true) != Resources(er.st, true)
        || sorted(all.outcomes[0].cast_drops) != sorted(er.cast) || sorted(all.outcomes[0].act_drops) != sorted(er.act))
    {
        c.bucket = RB_UNFAITHFUL;
        c.detail = all.outcomes.size() == 1
            ? "the hooked replay of the engine's own payments ended elsewhere (" + DiffText(er.st, all.outcomes[0].st) + ")"
            : "the hooked replay of the engine's own payments did not finish (" + std::to_string(all.outcomes.size())
              + " outcomes" + (all.unmodeled.empty() ? "" : ", " + all.unmodeled.front()) + ")";
        return c;
    }
    c.failed = all.outcomes[0].first_fail;
    if (c.failed < 0)
    {
        c.bucket = RB_NO_FAIL;
        c.detail = "no engine payment failed -- the action dropped for another reason";
        return c;
    }
    bool capped = false;
    std::string unscripted;   // a paying outcome that rests on a mana choice the oracle does not drive
    for (int k = c.failed; k >= 0; --k)
    {
        std::vector<PayOption> prefix(static_cast<std::size_t>(k));
        for (PayOption& o : prefix) { o.engine = true; }
        const Realization rk = Realize(root, plan, pre, /*stop_at_full=*/true, prefix, false, &e_want);
        capped = capped || rk.capped;
        for (const Outcome& o : rk.outcomes)
        {
            if (!o.Clean() || !Includes(o.left, e_want)) { continue; }
            if (!o.unscripted.empty()) { if (unscripted.empty()) { unscripted = o.unscripted; } continue; }
            const int d = c.failed - k;
            c.bucket = d == 0 ? RB_D0 : d == 1 ? RB_D1 : d == 2 ? RB_D2 : RB_D3P;
            c.keep   = k;
            c.script = o.script;
            return c;
        }
    }
    // Every paying line found rests on an engine choice the oracle does not drive (the blink loop's
    // tap-ahead, an untap effect's choice of lands): the audit's APPLY-DROP accepts such a line, the census
    // cannot vouch for a depth -- UNMODELED, not a disagreement.
    c.bucket = capped ? RB_CAPPED : !unscripted.empty() ? RB_UNMODELED : RB_INCONSISTENT;
    c.detail = capped ? "the payment search hit its cap before a prefix was found"
             : !unscripted.empty() ? "every paying line rests on a mana choice the oracle does not drive: " + unscripted
                                   : "no prefix of the engine's decisions reaches it, not even none -- the census disagrees with the audit";
    return c;
}
std::string RollbackCensus(const GameState& root, bool pre, const TurnSolver::Plan& plan, const EngineReal& er,
                           const std::multiset<std::string>& e_want)
{
    const Census c = RollbackCore(root, pre, plan, er, e_want);
    (c.later ? g_rb_need_later : g_rb_need_plan).fetch_add(1, std::memory_order_relaxed);
    g_rb_rescue_tried.fetch_add(1, std::memory_order_relaxed);
    if (c.rescued) { g_rb_rescued.fetch_add(1, std::memory_order_relaxed); }
    g_rb[c.bucket].fetch_add(1, std::memory_order_relaxed);
    const std::string t = c.keep < 0 ? c.detail
        : "keep the engine's first " + std::to_string(c.keep) + " mana decision" + (c.keep == 1 ? "" : "s")
          + ", re-make the rest (decision #" + std::to_string(c.failed + 1) + " is the payment that failed) "
          + "and every action pays: " + ScriptText(c.script);
    return std::string(" || ROLLBACK ") + kRbName[c.bucket] + ": " + t
         + (c.later ? "; a lost action was not declared in the plan" : "")
         + (c.rescued ? "; MTG_PAY_ROLLBACK (forced on) recovers it" : "; MTG_PAY_ROLLBACK (forced on) does not recover it");
}

void AuditPlan(const GameState& root, bool pre, const TurnSolver::Plan& plan, const EngineReal& er_in,
               const std::vector<const EngineReal*>& full_sibs)
{
    g_plans.fetch_add(1, std::memory_order_relaxed);
    for (const Action& a : plan.actions)
    {
        if (a.kind != Action::Kind::ComboRoute) { continue; }
        NoteUnmodeled("combo-route macro");
        Emit(C_UNMODELED, root, pre, plan, "a combo-route macro (a loop with its own payment and tap-ahead "
                                           "machinery) -- not driven by the oracle");
        return;
    }
    // ---- the engine's own realisation (no hook: exactly what the search scores) --------------------
    const GameState& e = er_in.st;
    const std::vector<std::string>& e_cast = er_in.cast;
    const std::vector<std::string>& e_act  = er_in.act;
    const bool e_full = er_in.Full();
    if (!e_full) { g_engine_drop.fetch_add(1, std::memory_order_relaxed); }
    if (Level() >= 3 && (TraceTurn() == 0 || TraceTurn() == root.turn_number))
    {
        std::lock_guard<std::mutex> lk(g_print_mu);
        std::fprintf(stderr, "[mana-oracle] PLAN %s plan={%s} engine-ended %s%s\n", Ctx(root, pre).c_str(),
                     PlanText(plan).c_str(), BoardText(e).c_str(),
                     e_full ? "" : (" DROPPED[" + DropText(e_cast, e_act) + "]").c_str());
    }
    // What the engine's line meant to use from hand: what it did use, plus the casts it dropped.
    std::multiset<std::string> e_want = LeftHand(root, e);
    for (const std::string& c : e_cast) { e_want.insert(c); }
    // ---- the complete side, in the engine's own order ----------------------------------------------
    Realization r = Realize(root, plan, pre, /*stop_at_full=*/false);
    for (const std::string& u : r.unmodeled) { NoteUnmodeled(u); }
    if (r.capped)
    {
        Emit(C_CAPPED, root, pre, plan, "payment search hit MTG_MANA_ORACLE_NODES=" + std::to_string(NodeCap())
                                        + " applies; the complete side is partial for this plan");
    }
    std::string unscripted;
    for (const Outcome& o : r.outcomes) { if (!o.unscripted.empty()) { unscripted = o.unscripted; break; } }
    if (!unscripted.empty())
    {
        NoteUnmodeled(unscripted.find("untap effect's choice") != std::string::npos
                          ? "untap effect chose among different lands"
                          : "engine tapped a mana source outside any payment");
        Emit(C_UNMODELED, root, pre, plan, "a mana choice the oracle does not drive: " + unscripted);
    }

    if (!e_full)
    {
        const std::string dropped = "engine dropped [" + DropText(e_cast, e_act) + "]";
        const bool partial = r.capped || !r.unmodeled.empty() || !unscripted.empty();
        // Another menu entry with the SAME actions (a cast-order variant, a land position) that pays for
        // them all answers this drop only if it reaches every board a full payment of THIS order
        // reaches, with at least the resources. Order variants exist because order can matter.
        std::vector<std::pair<std::uint64_t, Res>> sib;
        for (const EngineReal* s : full_sibs) { sib.emplace_back(CoreKey(s->st), Resources(s->st, false)); }
        auto sib_covers = [&](const Outcome& o)
        {
            const std::uint64_t k = CoreKey(o.st);
            const Res orr = Resources(o.st, false);
            for (const auto& [sk, sr] : sib) { if (sk == k && Covers(sr, orr, nullptr)) { return true; } }
            return false;
        };
        // A full oracle realisation that used everything the engine's line meant to use.
        const Outcome* full = nullptr;
        const Outcome* uncovered = nullptr;
        bool other_full = false;   // full, but a continuation chose different cards
        for (const Outcome& o : r.outcomes)
        {
            if (!o.Clean()) { continue; }
            if (!Includes(o.left, e_want)) { other_full = true; continue; }
            if (full == nullptr) { full = &o; }
            if (uncovered == nullptr && !sib_covers(o)) { uncovered = &o; }
        }
        if (full != nullptr && !sib.empty() && uncovered == nullptr)
        {
            Emit(C_DROP_COVERED, root, pre, plan, dropped + "; this order pays them all too, and every board it "
                                                  "reaches, another menu entry with the same actions reaches "
                                                  "with at least the resources");
            return;
        }
        if (full != nullptr)
        {
            const Outcome* show = uncovered != nullptr ? uncovered : full;
            std::string why = dropped + "; every action pays with " + ScriptText(show->script);
            if (!sib.empty())
            {
                why += ", reaching a board the menu's other order of these actions does not ("
                     + DiffText(full_sibs.front()->st, show->st) + ")";
            }
            Emit(C_APPLY_DROP, root, pre, plan, why + RollbackCensus(root, pre, plan, er_in, e_want));
            return;
        }
        if (!sib.empty())
        {
            if (partial) { return; }   // no verdict on a partial side (CAPPED / UNMODELED said so above)
            Emit(C_DROP_COVERED, root, pre, plan, dropped + "; no payment of this order realises them all, "
                                                  "and another menu entry with the same actions does");
            return;
        }
        bool all_tried = true, other_partial = false;
        for (const TurnSolver::Plan& alt : OtherOrders(plan, /*with_acts=*/true, &all_tried))
        {
            Realization ra = Realize(root, alt, pre, /*stop_at_full=*/true, {}, false, &e_want);
            if (ra.capped || !ra.unmodeled.empty()) { other_partial = true; }
            for (const Outcome& o : ra.outcomes)
            {
                if (!o.unscripted.empty()) { other_partial = true; }
                if (!o.Clean() || !Includes(o.left, e_want)) { continue; }
                std::string order;
                for (const Action& a : alt.actions)
                {
                    if (a.kind == Action::Kind::PlayLand) { continue; }
                    order += (order.empty() ? "" : " > ")
                           + (a.kind == Action::Kind::CastFromHand ? a.card_name.str() : ActionKey(a));
                }
                g_rb[RB_ORDER].fetch_add(1, std::memory_order_relaxed);
                Emit(C_APPLY_DROP_ORDER, root, pre, plan,
                     dropped + "; the order " + order + " pays every action with " + ScriptText(o.script));
                return;
            }
        }
        if (other_full)
        {
            Emit(C_CONT_DIFFERS, root, pre, plan, dropped + "; the oracle's full realisations continued with different cards");
            return;
        }
        if (partial || other_partial) { return; }   // no verdict on a partial side
        // PHANTOM is a NO claim -- "no order and no payment realises it" -- so it needs every order tried.
        if (!all_tried)
        {
            NoteUnmodeled("too many actions to try every order");
            Emit(C_UNMODELED, root, pre, plan, dropped + "; no payment of this order realises it, and it has too "
                                               "many actions to try every order");
            return;
        }
        Emit(C_PHANTOM, root, pre, plan, dropped + "; no order or payment realises it");
        return;
    }
    // ---- the engine realised the whole plan: I1 (cover) and I2 (domination) --------------------------
    // Only the oracle outcomes that played the SAME LINE are comparable: a plan cut at a draw
    // (a breakpoint) is continued by a solver inside the apply, from the board the payments left, so
    // two payments can lead to two different continuations. Those are different plans, not a mana
    // verdict on this one.
    const std::multiset<std::string> e_left = LeftHand(root, e);
    const std::string e_line = LineKey(root, e);
    bool same_line = false, more_line = false;
    for (const Outcome& o : r.outcomes)
    {
        if (!o.Full()) { continue; }
        if (o.line == e_line) { same_line = true; }
        else if (Includes(o.left, e_left)) { more_line = true; }
    }
    if (!same_line && !r.outcomes.empty() && !r.capped && unscripted.empty())
    {
        Emit(C_CONT_DIFFERS, root, pre, plan,
             std::string(more_line ? "every oracle realisation continued with MORE cards than the engine's"
                                   : "every oracle realisation continued with different cards")
             + " (the engine's continuation solver saw a different board)");
        return;
    }
    const std::uint64_t ek = CoreKey(e);
    const Res er = Resources(e, /*with_float=*/false);
    bool covered = false;
    const Outcome* best = nullptr;
    Res best_res;
    for (const Outcome& o : r.outcomes)
    {
        if (!o.Full() || o.line != e_line || CoreKey(o.st) != ek) { continue; }
        const Res orr = Resources(o.st, /*with_float=*/false);
        bool strict = false;
        if (Covers(orr, er, &strict))
        {
            covered = true;
            if (strict && (best == nullptr || Covers(orr, best_res, nullptr))) { best = &o; best_res = orr; }
        }
    }
    if (!covered)
    {
        if (r.capped || !unscripted.empty()) { return; }
        // A gap the oracle can NAME (a source class it does not drive yet) is UNMODELED, not a defect in
        // what it does drive; only an unexplained gap is an ORACLE-GAP.
        if (!r.unmodeled.empty())
        {
            Emit(C_UNMODELED, root, pre, plan, "the engine reached a board the oracle cannot ("
                                               + r.unmodeled.front() + ")");
            return;
        }
        std::string why = r.outcomes.empty() ? std::string("the oracle reached no outcome at all")
                                             : std::to_string(r.outcomes.size()) + " oracle outcomes, none covers the engine's";
        for (const Outcome& o : r.outcomes)
        {
            if (!o.Full() || o.line != e_line) { continue; }
            why += " -- nearest " + ScriptText(o.script) + ": " + DiffText(e, o.st);
            break;
        }
        Emit(C_ORACLE_GAP, root, pre, plan, why + "; engine ended " + BoardText(e));
        return;
    }
    if (best != nullptr)
    {
        std::string what;
        const Cls c = DominanceClass(best_res, er, pre, &what);
        Emit(c, root, pre, plan, what + " -- by paying " + ScriptText(best->script) + "; the engine ended "
                                 + BoardText(e));
    }
}
} // namespace

void AuditDecision(const GameState& state, bool is_pre_combat)
{
    if (!On() || t_hook != nullptr) { return; }
    const auto t0 = std::chrono::steady_clock::now();
    g_decisions.fetch_add(1, std::memory_order_relaxed);
    RevealLogPause pause;   // quiet: no logger, no viewer sink, not the real game (g_real_resolution)
    std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(state, is_pre_combat);
    const std::size_t cap = MaxPlans() > 0 ? static_cast<std::size_t>(MaxPlans()) : plans.size();
    if (plans.size() > cap)
    {
        g_plans_skipped.fetch_add(static_cast<long long>(plans.size() - cap), std::memory_order_relaxed);
        plans.resize(cap);
    }
    if (Level() >= 2)
    {
        std::lock_guard<std::mutex> lk(g_print_mu);
        std::fprintf(stderr, "[mana-oracle] DECISION %s plans=%zu\n", Ctx(state, is_pre_combat).c_str(), plans.size());
    }
    // The engine's realisation of every menu entry first: a drop is only a miss when no entry with the
    // same actions realises them.
    std::vector<EngineReal> real;
    real.reserve(plans.size());
    std::vector<std::string> keys;
    for (const TurnSolver::Plan& p : plans)
    {
        real.push_back(EngineApply(state, is_pre_combat, p));
        keys.push_back(SiblingKey(p));
    }
    std::map<std::string, std::vector<const EngineReal*>> full_by_key;
    for (std::size_t i = 0; i < plans.size(); ++i) { if (real[i].Full()) { full_by_key[keys[i]].push_back(&real[i]); } }
    static const std::vector<const EngineReal*> kNone;
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        const auto it = full_by_key.find(keys[i]);
        AuditPlan(state, is_pre_combat, plans[i], real[i], it == full_by_key.end() ? kNone : it->second);
    }

    // ---- ENUM-GAP: the plans a MANA GATE kept off the menu ----------------------------------------
    // The same enumeration with every mana gate in observe mode is "every plan the non-mana rules
    // allow"; what it holds that the menu does not was refused on mana. Each is realised (every order,
    // every payment): one that pays in full is an option the search never had.
    std::vector<TurnSolver::Plan> observed;
    {
        t_gate_observe = true;
        observed = TurnSolver::EnumerateMainPlans(state, is_pre_combat);
        t_gate_observe = false;
    }
    std::set<std::string> on_menu(keys.begin(), keys.end());
    // The OPTIONS the menu really offers are outcomes, not action lists: a gate-rejected plan whose
    // payable outcome is a board some menu entry already reaches (Ghalta, Stampede Tyrant cast alone
    // puts the Hulk and the Craterhoof it would otherwise have cast) is no missing option.
    // ...reached with at least the resources (life, energy, haste, sources left untapped): the same
    // board with less is still an option the menu does not have.
    std::multimap<std::uint64_t, Res> menu_cores;
    for (const EngineReal& e : real) { menu_cores.emplace(CoreKey(e.st), Resources(e.st, /*with_float=*/false)); }
    std::set<std::string> done;
    int checked = 0;
    for (const TurnSolver::Plan& p : observed)
    {
        const std::string k = SiblingKey(p);
        if (on_menu.count(k) != 0 || !done.insert(k).second) { continue; }
        g_gate_rejects.fetch_add(1, std::memory_order_relaxed);
        if (MaxGateChecks() > 0 && checked >= MaxGateChecks())
        { g_gate_rejects_skipped.fetch_add(1, std::memory_order_relaxed); continue; }
        ++checked;
        g_gate_rejects_checked.fetch_add(1, std::memory_order_relaxed);
        // Combo machinery the oracle does not drive: a combo-route macro, or a LOOP-COUNT variant (a
        // chosen_x on a spell with no {X} -- AccomplishedAlchemist's Galvanic Alchemist untap loops
        // carry their iteration count there, x1000000 meaning "until done").
        bool combo = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ComboRoute) { combo = true; }
            if (a.chosen_x > 0 && a.def != nullptr && !a.def->card.m_mana_cost.has_x
                && a.def->card.m_mana_cost.x_pips == 0) { combo = true; }
        }
        if (combo) { NoteUnmodeled("gate-rejected loop / combo variant (not driven)"); continue; }
        const std::multiset<std::string> want = PlanHandCards(p);
        int n_casts = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n_casts; } }
        std::vector<TurnSolver::Plan> tries{ p };
        for (const TurnSolver::Plan& alt : OtherOrders(p)) { tries.push_back(alt); }
        for (const TurnSolver::Plan& t : tries)
        {
            Realization r = Realize(state, t, is_pre_combat, /*stop_at_full=*/true);
            const Outcome* hit = nullptr;
            for (const Outcome& o : r.outcomes)
            {
                if (!o.Clean() || !o.unscripted.empty() || !Includes(o.left, want)) { continue; }
                // Every cast really cast (an action can also be skipped silently -- its card no longer
                // in hand -- which no drop sink reports).
                if (o.st.spells_cast_this_turn - state.spells_cast_this_turn < n_casts) { continue; }
                hit = &o;
                break;
            }
            if (hit == nullptr) { continue; }
            const auto range = menu_cores.equal_range(CoreKey(hit->st));
            const Res hr = Resources(hit->st, /*with_float=*/false);
            bool reached = false;
            for (auto it = range.first; it != range.second && !reached; ++it) { reached = Covers(it->second, hr, nullptr); }
            // The menu reaches this board already, with at least the resources -- but another order may
            // reach one it does not, so keep trying.
            if (reached) { continue; }
            // The menu reaches this board but with LESS of something, and nothing more: that is a payment
            // dominating a menu entry's -- the same finding, and the same class, as a DOMINATED-* on the
            // entry itself (an extra untapped land stays DOMINATED-LAND, counted, not flagged).
            const Res* dominated = nullptr;
            for (auto it = range.first; it != range.second && dominated == nullptr; ++it)
            { if (Covers(hr, it->second, nullptr)) { dominated = &it->second; } }
            if (dominated != nullptr)
            {
                std::string what;
                const Cls c = DominanceClass(hr, *dominated, is_pre_combat, &what);
                Emit(c, state, is_pre_combat, p, what + " -- a mana gate kept this plan off the menu, and paying it "
                                                 "with " + ScriptText(hit->script) + " reaches a menu entry's board with more");
                break;
            }
            std::string order;
            if (&t != &tries.front())
            {
                for (const Action& a : t.actions)
                { if (a.kind == Action::Kind::CastFromHand) { order += (order.empty() ? "" : " > ") + a.card_name.str(); } }
            }
            g_rb[RB_OFF_MENU].fetch_add(1, std::memory_order_relaxed);
            Emit(C_ENUM_GAP, state, is_pre_combat, p,
                 "a mana gate kept this plan off the menu, but " + (order.empty() ? std::string("it") : "casting " + order)
                 + " pays every action with " + ScriptText(hit->script)
                 + (range.first != range.second
                        ? " (the menu reaches this board only with fewer resources: " + MoreText(hr, range.first->second) + ")"
                        : ""));
            break;
        }
    }
    g_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                   std::memory_order_relaxed);
}

// ---- the viewer's line check (ManaOracle.h) ---------------------------------------------------------
// The same questions AuditPlan asks of an APPLY-DROP, for one plan, answered as data: the engine's own
// apply first (the common case, one apply, and the only cost a line the search pays for ever pays), then
// a same-actions menu entry, then the oracle in this order with the rollback census, then the other orders.
LineAudit AuditLine(const GameState& state, bool pre, const TurnSolver::Plan& plan,
                    const std::vector<TurnSolver::Plan>* menu, int node_cap, bool deep)
{
    LineAudit a;
    if (t_hook != nullptr) { a.partial = "inside an oracle apply"; return a; }
    for (const Action& x : plan.actions)
    {
        if (x.kind == Action::Kind::ComboRoute)
        { a.partial = "a combo-route macro (the oracle does not drive its payments)"; return a; }
    }
    // Quiet and chooser-free, like the commit preview's trial applies (PrepareHumanCommit): no logger, no
    // viewer sink, no human chooser asked anything, not the real game.
    ComboOffApplyPause co_quiet;
    RevealLogPause     quiet;
    struct CapScope
    {
        int saved;
        explicit CapScope(int v) : saved(t_node_cap) { t_node_cap = std::max(1, v); }
        ~CapScope() { t_node_cap = saved; }
    } cap_scope(node_cap);
    a.audited = true;
    const EngineReal er = EngineApply(state, pre, plan);
    a.dropped = DropText(er.cast, er.act);
    if (er.Full()) { a.engine_full = true; return a; }
    if (menu != nullptr)
    {
        const std::string key = SiblingKey(plan);
        for (std::size_t i = 0; i < menu->size(); ++i)
        {
            const TurnSolver::Plan& m = (*menu)[i];
            if (&m == &plan || SiblingKey(m) != key) { continue; }
            if (EngineApply(state, pre, m).Full()) { a.sibling = static_cast<int>(i); return a; }
        }
    }
    if (!deep) { a.partial = "not checked (the oracle is asked about the first few dropping lines only)"; return a; }
    // What the engine's line meant to use from hand: what it did use, plus the casts it dropped.
    std::multiset<std::string> want = LeftHand(state, er.st);
    for (const std::string& c : er.cast) { want.insert(c); }
    auto to_pays = [](const std::vector<PayOption>& script, const std::vector<int>& steps)
    {
        std::vector<LinePayment> v;
        for (std::size_t k = 0; k < script.size(); ++k)
        {
            const PayOption& o = script[k];
            LinePayment p;
            p.step   = k < steps.size() ? steps[k] : -1;
            p.what   = o.what;
            p.engine = o.engine;
            p.fail   = o.fail;
            for (std::size_t i = 0; i < o.taps.size(); ++i)
            {
                const TurnSolver::PreTap& t = o.taps[i];
                std::string face;
                if (!t.combo.empty()) { for (int c : t.combo) { face.push_back(kLetters[c]); } }
                else if (t.color >= 0 && t.color < 6) { face.push_back(kLetters[t.color]); }
                std::string tok = "tap=" + t.name + "#" + std::to_string(t.num) + ":" + face;
                std::string txt = t.name + " for " + face;
                for (int ac : t.aura_colors) { tok += "+"; tok.push_back(kLetters[ac]); txt += "+"; txt.push_back(kLetters[ac]); }
                if (i < o.creature_route.size() && o.creature_route[i]) { txt += " (creature-only mana)"; }
                p.taps.push_back(std::move(tok));
                p.text += (p.text.empty() ? "" : ", ") + txt;
            }
            if (o.stash.Total() > 0) { p.keep_floating = PoolText(o.stash, o.stash_haste); }
            v.push_back(std::move(p));
        }
        return v;
    };
    // A payment the oracle can VOUCH for: every action paid, the cards the line meant to use used, and no
    // mana choice the engine made on its own along the way.
    auto hit_of = [&](const Realization& r) -> const Outcome*
    {
        if (a.partial.empty() && r.capped)
        { a.partial = "the payment search hit its cap (" + std::to_string(NodeCap()) + " applies)"; }
        if (a.partial.empty() && !r.unmodeled.empty()) { a.partial = r.unmodeled.front(); }
        for (const Outcome& o : r.outcomes)
        {
            if (!o.unscripted.empty())
            {
                if (a.partial.empty()) { a.partial = "a mana choice the oracle does not drive: " + o.unscripted; }
                continue;
            }
            if (o.Clean() && Includes(o.left, want)) { return &o; }
        }
        return nullptr;
    };
    const Realization r = Realize(state, plan, pre, /*stop_at_full=*/true, {}, false, &want);
    if (const Outcome* hit = hit_of(r))
    {
        a.payable = true;
        a.partial.clear();
        a.pays    = to_pays(hit->script, hit->pay_steps);
        const Census c = RollbackCore(state, pre, plan, er, want);
        a.rollback     = kRbName[c.bucket];
        a.keep         = c.keep;
        a.failed       = c.failed;
        a.later        = c.later;
        a.pay_rollback = c.rescued;
        return a;
    }
    for (const TurnSolver::Plan& alt : OtherOrders(plan, /*with_acts=*/true))
    {
        const Realization ra = Realize(state, alt, pre, /*stop_at_full=*/true, {}, false, &want);
        const Outcome* hit = hit_of(ra);
        if (hit == nullptr) { continue; }
        a.payable = true;
        a.partial.clear();
        a.pays    = to_pays(hit->script, hit->pay_steps);
        for (const Action& x : alt.actions)
        {
            if (x.kind == Action::Kind::PlayLand) { continue; }
            a.other_order += (a.other_order.empty() ? "" : " > ")
                           + (x.kind == Action::Kind::CastFromHand ? x.card_name.str() : ActionKey(x));
        }
        return a;
    }
    return a;
}
} // namespace manaoracle
