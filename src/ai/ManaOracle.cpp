// MANA COMPLETENESS ORACLE -- MTG_MANA_ORACLE. See ManaOracle.h for the mechanism and
// docs/design/mana-completeness-oracle.md for the invariant, the classes and what is not modelled yet.
#include "ManaOracle.h"
#include "EngineFlags.h"
#include "EscStats.h"
#include "HeuristicArm.h"
#include "ManaPayment.h"
#include "Combat.h"
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
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
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
        // A SUBTYPE restriction (Giada, Font of Hope: "Spend this mana only to cast an Angel spell"). The creature-only
        // pool the unit lands in would spend it on ANY creature spell, so the tap itself is refused for a spell
        // outside the subtype -- the engine's own predicate on the paying spell's scope (permissive when unset,
        // exactly as the engine's payer is). Angels seed 1100020 gi19 T3: the oracle paid Bishop of Wings (a Human
        // Cleric) with Giada's {W} and reported a plan no legal payment realises as an ENUM-GAP.
        if (!ability && !q.mana_only_subtype.empty() && !SubtypeManaOk(q))
        { return "'" + t.name + "' makes mana only for " + q.mana_only_subtype + " spells"; }
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
    // A SAFE hook (RealPayScope) that has given up on its script, or run past it, hands every step back.
    if (h->safe && (h->abandoned || h->next >= h->script.size())) { return true; }
    return h->next < h->script.size() ? h->script[h->next].engine : h->all_engine;
}

bool TakeEngineStep(const GameState& s, int count)
{
    // A SAFE hook whose next entry is not a tap-ahead gives up on its script here (the engine taps ahead).
    Hook* const sh = t_hook;
    if (count > 0 && sh != nullptr && sh->target == &s && sh->safe && !sh->ended && !sh->abandoned
        && sh->next < sh->script.size() && !sh->script[sh->next].engine && sh->script[sh->next].what != "tap-ahead")
    { sh->abandoned = true; }
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
    C_CONT_DIFFERS, C_DROP_COVERED, C_UNMODELED, C_CAPPED, C_HIDDEN_INFO, C_N
};
const char* const kClsName[C_N] = {
    "ORACLE-GAP", "ENUM-GAP", "APPLY-DROP", "APPLY-DROP-ORDER", "PHANTOM",
    "DOMINATED-CREATURE", "DOMINATED-ONESHOT", "DOMINATED-HASTE", "DOMINATED-LIFE", "DOMINATED-OTHER",
    "DOMINATED-LAND", "CONTINUATION-DIFFERS", "DROP-COVERED", "UNMODELED", "CAPPED", "HIDDEN-INFO"
};
// A MISS is a class the ENGINE owes a fix for (I2). DOMINATED-LAND is only a potential one -- whether
// the land mattered is the follow-up question (Stage 3) -- so it is counted, and printed at level 2.
bool IsMiss(Cls c)
{
    return c == C_ENUM_GAP || c == C_APPLY_DROP || c == C_APPLY_DROP_ORDER || c == C_DOM_CREATURE
        || c == C_DOM_ONESHOT || c == C_DOM_HASTE || c == C_DOM_LIFE || c == C_DOM_OTHER;
}
// HIDDEN-INFO: a gate-rejected plan whose only paying realisations spend mana from cards taken off the top of the
// library during the plan (Genesis Wave's reveal, a cantrip's draw) -- clairvoyant, so no gate could have known.
bool QuietClass(Cls c)
{ return c == C_DOM_LAND || c == C_PHANTOM || c == C_CONT_DIFFERS || c == C_DROP_COVERED || c == C_HIDDEN_INFO; }

std::atomic<long long> g_cls[C_N];
std::atomic<long long> g_decisions{0}, g_plans{0}, g_plans_skipped{0}, g_applies{0}, g_frontiers{0},
                       g_pruned{0}, g_options{0}, g_engine_drop{0}, g_ms{0},
                       g_gate_rejects{0}, g_gate_rejects_checked{0}, g_gate_rejects_skipped{0};
// The ENUM-GAP pass's rejections of a paying realisation (EnumGapPlan): it skipped one of the plan's actions and the
// plan without it is on the menu or checked on its own (noop); it tapped a card taken off the top of the library
// (hidden); the menu reaches its board with every land Aura's host aside and at least its resources (host_blind).
// (cont_land): a land drop the plan did not make paid for it.
std::atomic<long long> g_eg_noop{0}, g_eg_hidden{0}, g_eg_host_blind{0}, g_eg_cont_land{0};
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
        std::fprintf(stderr, "=== MANA ORACLE enum-gap rejections: noop=%lld hidden=%lld host_blind=%lld cont_land=%lld ===\n",
                     g_eg_noop.load(), g_eg_hidden.load(), g_eg_host_blind.load(), g_eg_cont_land.load());
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
        if (p.aura_attached_to > 0) { bf += "@" + std::to_string(p.aura_attached_to); }
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
        if (a.enchant_target > 0) { s += "@" + std::to_string(a.enchant_target); }
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
std::string ClassKey(const Permanent& p, bool age = true)
{
    std::string k = p.card.m_name.str();
    k += "|"; k += p.card.IsLand() ? "L" : (p.card.IsCreature() ? "C" : "O");
    k += "|"; k += p.counters.KeyFragment();
    k += "|"; k += std::to_string(p.storage_counters) + "," + std::to_string(p.charge_counters);
    if (age) { k += "|"; k += p.entered_this_turn ? "new" : "old"; }
    k += "|"; k += p.is_token ? "tok" : "card";
    return k;
}

// The class of a permanent as a RESOURCE (an untapped source, a hasted body): ClassKey, except that a land which can
// never be a creature forgets whether it entered this turn. The marker only matters to summoning sickness (CR 302.6),
// so an untapped new Mountain is an untapped old Mountain -- but the resource tables read them as two keys, and a
// realisation leaving the other copy up compared as INCOMPARABLE with the menu's board instead of reaching it
// (Colossus Hammer eg1, `U:Mountain|L||0,0|old|card 1>0`; Slivers, the new against the old Sliver Hive). A manland
// (Mutavault, can_animate) keeps it: an animated Mutavault that entered this turn cannot attack.
std::string ResClassKey(const Permanent& p)
{
    if (p.card.IsLand() && !p.card.IsCreature() && !p.is_animated)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d != nullptr && !d->params.can_animate) { return ClassKey(p, /*age=*/false); }
    }
    return ClassKey(p);
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

// What is attached to `p` (Auras, Equipment), sorted -- the part of a permanent's resource value ClassKey
// cannot see from the permanent alone. An enchanted land is not its bare twin's equal: EDF seed 3003 T5, Aether
// Hub #3 carrying Wild Growth and the plan's new Fertile Ground taps for three, the bare Hub #4 for one, and the
// prune below read "Hub #3 untapped" as covered by "Hub #4 untapped" (one more Brushland and a life up) -- so it
// dropped the board the engine's line continues from and reported nine ORACLE-GAPs (BuildSources already keys
// such a copy apart for the payment table).
std::string AttachedKey(const GameState& s, const Permanent& p)
{
    std::vector<std::string> on;
    for (const Permanent& q : s.battlefield)
    {
        if (q.aura_attached_to == p.card.m_number || q.equipped_to == p.card.m_number)
        { on.push_back(q.card.m_name.str()); }
    }
    if (on.empty()) { return std::string(); }
    std::sort(on.begin(), on.end());
    std::string k = "|on:";
    for (const std::string& x : on) { k += x + ";"; }
    return k;
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
        const std::string k = ResClassKey(p) + AttachedKey(s, p);
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

// Covers(Resources(x, with_float), Resources(y, with_float), strict), read off the two boards
// without building x's table: when they hold the same permanents in the same places with the same class keys
// (ClassKey + AttachedKey) and x untaps at least every permanent y does, the "U:" counts cover class by class and
// are strictly larger exactly where x untaps one y tapped; "H:", life, energy and the pools compare as numbers. Any
// other pair of boards takes the tables. The refine compares a re-payment against the engine's own payment of one
// bill from one board, thousands of times a game (EDF seed 1130464 gi463: 3.1M candidates, the tables ~18% of the
// game's time -- AttachedKey walks the battlefield per permanent). MTG_ORACLE_COVERS_VERIFY=1 checks every fast
// answer against the tables.
bool CoversBoards(const GameState& x, const GameState& y, bool with_float, bool* strict)
{
    const int a = x.active_player_index;
    bool fast = a == y.active_player_index && x.battlefield.size() == y.battlefield.size();
    bool st = false;
    for (std::size_t i = 0; fast && i < x.battlefield.size(); ++i)
    {
        const Permanent& p = x.battlefield[i];
        const Permanent& q = y.battlefield[i];
        if (p.card.m_number != q.card.m_number || p.controller_index != q.controller_index
            || p.aura_attached_to != q.aura_attached_to || p.equipped_to != q.equipped_to
            || p.card.m_name_hash != q.card.m_name_hash) { fast = false; break; }
        if (p.controller_index != a) { continue; }
        if (p.temp_haste != q.temp_haste || p.storage_counters != q.storage_counters
            || p.charge_counters != q.charge_counters || p.entered_this_turn != q.entered_this_turn
            || p.is_token != q.is_token || p.card.IsLand() != q.card.IsLand() || p.card.IsCreature() != q.card.IsCreature()
            || (p.tapped && !q.tapped)
            || ((!p.counters.empty() || !q.counters.empty()) && p.counters.KeyFragment() != q.counters.KeyFragment()))
        { fast = false; break; }
        if (!p.tapped && q.tapped) { st = true; }
    }
    if (!fast) { return Covers(Resources(x, with_float), Resources(y, with_float), strict); }
    const Player& px = x.players[a];
    const Player& py = y.players[a];
    bool ok = px.life >= py.life && px.energy_counters >= py.energy_counters;
    st = st || px.life > py.life || px.energy_counters > py.energy_counters;
    if (ok && with_float)
    {
        const ManaPool* xs[3] = { &x.floating_mana, &x.floating_creature_mana, &x.floating_bigspell_mana };
        const ManaPool* ys[3] = { &y.floating_mana, &y.floating_creature_mana, &y.floating_bigspell_mana };
        for (int i = 0; i < 3 && ok; ++i)
        {
            const ManaPool& f = *xs[i];
            const ManaPool& g = *ys[i];
            const int u[7] = { f.white, f.blue, f.black, f.red, f.green, f.colorless, f.wild };
            const int v[7] = { g.white, g.blue, g.black, g.red, g.green, g.colorless, g.wild };
            for (int c = 0; c < 7; ++c) { if (u[c] < v[c]) { ok = false; break; } if (u[c] > v[c]) { st = true; } }
        }
        if (x.floating_haste_c < y.floating_haste_c) { ok = false; }
        if (x.floating_haste_c > y.floating_haste_c) { st = true; }
    }
    static const bool s_verify = EnvOn("MTG_ORACLE_COVERS_VERIFY");
    if (s_verify)
    {
        bool st2 = false;
        const bool ok2 = Covers(Resources(x, with_float), Resources(y, with_float), &st2);
        if (ok2 != ok || (ok && st2 != st))
        {
            std::lock_guard<std::mutex> lk(g_print_mu);
            std::fprintf(stderr, "[oracle-covers] MISMATCH fast %d/%d tables %d/%d\n", ok ? 1 : 0, st ? 1 : 0, ok2 ? 1 : 0, st2 ? 1 : 0);
        }
    }
    if (ok && strict != nullptr) { *strict = st; }
    return ok;
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
    // Where each NEW Aura went. A breakpoint's continuation is chosen by a solver inside the apply,
    // from the board the payments left, so two payments can send the same Wild Growth to two lands
    // (EDF seed 3129 T3: the engine's Living Wish payment floats a wild unit, the oracle's a concrete
    // colour, and the continuation hosts the Aura on the fetched Wastes or on the Coast). Those are two
    // continuations, not one line paid two ways. A plan's own Aura casts name their host, so every
    // realisation of the plan agrees on it.
    std::set<int> old;
    for (const Permanent& p : root.battlefield) { old.insert(p.card.m_number); }
    std::vector<std::string> hosts;
    for (const Permanent& p : fin.battlefield)
    {
        if (p.aura_attached_to == 0 || old.count(p.card.m_number) != 0) { continue; }
        for (const Permanent& h : fin.battlefield)
        {
            if (h.card.m_number != p.aura_attached_to) { continue; }
            hosts.push_back(p.card.m_name.str() + ">" + h.card.m_name.str() + (h.entered_this_turn ? "*" : ""));
            break;
        }
    }
    std::sort(hosts.begin(), hosts.end());
    for (const std::string& h : hosts) { k += "|" + h; }
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
    // The tap turns exactly its own source from untapped to tapped and nothing else (no sacrifice, no
    // other permanent tapped or untapped). RefinePayment's surplus bound counts released sources by it.
    bool regular  = false;
    int  wild     = 0;                       // measured: the `wild` units the tap adds (every pool)
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

// A warm per-thread scratch board (RampScratch / PayScratch's shape): the probes below copy-ASSIGN into a reused
// GameState, so its zone vectors keep their capacity instead of being allocated per probe -- the refine runs a
// table, verifies and replays per triggered payment, a few thousand board copies a game. A deque, so a nested
// probe never relocates a board an outer frame holds.
struct OracleScratch
{
    explicit OracleScratch(const GameState& src)
    {
        static thread_local std::deque<GameState> pool;
        static thread_local std::size_t           depth = 0;
        while (pool.size() <= depth) { pool.emplace_back(); }
        m_state = &pool[depth];
        m_depth = &depth;
        ++depth;
        *m_state = src;
    }
    ~OracleScratch() { --(*m_depth); }
    OracleScratch(const OracleScratch&)            = delete;
    OracleScratch& operator=(const OracleScratch&) = delete;
    GameState& Board() const { return *m_state; }
private:
    GameState*   m_state = nullptr;
    std::size_t* m_depth = nullptr;
};

// Measure a face on a scratch copy: what the tap adds. A fed face is asked with a generous pre-fill so
// its feed can always be met; its net is then (outputs - feed).
bool MeasureFace(const GameState& state, Face& f)
{
    OracleScratch scr(state);
    GameState& sc = scr.Board();
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
    f.regular = sc.battlefield.size() == state.battlefield.size();
    for (std::size_t i = 0; i < sc.battlefield.size() && f.regular; ++i)
    {
        const Permanent& p0 = state.battlefield[i];
        const Permanent& p1 = sc.battlefield[i];
        if (p0.card.m_number != p1.card.m_number) { f.regular = false; }
        else if (p0.card.m_number == f.pt.num) { f.regular = !p0.tapped && p1.tapped; }
        else if (p0.tapped != p1.tapped) { f.regular = false; }
    }
    f.wild = std::max(0, all_pools(sc).wild - fb.wild);
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

// The permanents the plan being realised taps or sacrifices itself (Realize's plan_srcs). Such a copy is
// not its twins' equal -- tapping it for mana strands that action -- so BuildSources keys it apart. EDF
// seed 3508 T4: Wild Growth, then Conservatory #13's {4},{T}: investigate. The fold tapped #13 (the lower
// of two Conservatories) for the Wild Growth, the activation lost its source, and the oracle missed the
// painless line the engine pays with #14 (an ORACLE-GAP, "life engine 20 oracle 19").
thread_local const std::set<int>* t_plan_srcs = nullptr;

// THE ENUM-GAP PASS PAYS AS THE ENGINE CAN. A SacAction source -- Generator Servant, an on-board Lotus Bloom or Black
// Lotus -- makes its mana only through a plan's own SacForMana action (both apply worlds crack it in their pre-pass;
// the payer never does, ManaPayment.cpp "SAC ACTION"). The viewer's pre-tap offers it at payment time, so a realisation
// of a gate-rejected plan WITHOUT the action could crack it mid-payment and "pay" a line the engine cannot express:
// NivMizzetCombo eg1, 95 of 106 ENUM-GAPs (Tandem Lookout off Servant's {C}{C}; Curiosity cast onto the Servant the
// payment then cracked; a second Servant cast with the first's haste {C} and cracked the same turn). The executable
// form is the plan WITH the sac action, which the observe-mode enumeration holds and checks on its own -- or
// rejects structurally (SubsetHasHostSacrificedForMana) or cannot emit (a Servant cast this turn), each a dominated
// line. So the pass leaves the route out (AuditPlan keeps it: it is how the oracle finds a better crack timing).
thread_local bool t_no_sac_action = false;
struct NoSacActionScope
{
    bool saved;
    NoSacActionScope() : saved(t_no_sac_action) { t_no_sac_action = true; }
    ~NoSacActionScope() { t_no_sac_action = saved; }
    NoSacActionScope(const NoSacActionScope&)            = delete;
    NoSacActionScope& operator=(const NoSacActionScope&) = delete;
};

// Do the first two Aura colour picks on `land` commute? They do when the first two Auras LandAuraAddToPoolOfLand
// credits a pick to (attached, controlled alike, adding one mana of any colour on this board) add the same amount.
bool AuraPicksSymmetric(const GameState& s, const Permanent& land)
{
    if (!land.card.IsLand() || !s.deck_has_land_aura) { return false; }
    int amt[2] = { 0, 0 };
    int n = 0;
    for (const Permanent& a : s.battlefield)
    {
        if (a.aura_attached_to != land.card.m_number || a.controller_index != land.controller_index) { continue; }
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if (ad == nullptr || !ad->params.is_land_aura || ad->params.land_aura_extra_mana <= 0) { continue; }
        if (!LandAuraProducesFor(*ad, a).empty()) { continue; }
        amt[n++] = ad->params.land_aura_extra_mana;
        if (n == 2) { break; }
    }
    return n == 2 && amt[0] == amt[1];
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
        if (t_no_sac_action && o.route == HumanPreTapOffer::Route::SacAction) { continue; }   // see t_no_sac_action
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        // RESTRICTED sources: offered to a creature spell through the oracle's own route -- a subtype-restricted one
        // (Giada) only to a spell of that subtype (the paying-spell scope: MinimalPayments sets it from the frontier).
        std::string restricted;   // the coloured faces the creature route adds ("" = none)
        const bool ability_ok = d != nullptr && d->params.colored_creature_ability_ok && PayingCreatureAbility();
        const bool subtype_ok = d == nullptr || d->params.mana_only_subtype.empty() || SubtypeManaOk(d->params);
        if (d != nullptr && CanTapNow(p, state.battlefield)
            && ((for_creature && subtype_ok && (d->params.creature_mana_only || d->params.colored_creature_only))
                || ability_ok))
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
            // Restricted mana for a noncreature spell is simply not supply -- not a gap; nor is a subtype-restricted
            // source's for a creature spell outside its subtype.
            if (unmodeled != nullptr && for_creature && subtype_ok && o.why_not.find("may be spent only") != std::string::npos)
            { unmodeled->push_back("restricted-mana source " + p.card.m_name.str()); }
            continue;
        }
        // The spell half of a subtype restriction is modelled (above, and OracleCreatureTap); the ABILITY half
        // (Smokebraider: "...or activate abilities of Elementals") is not.
        if (d != nullptr && !d->params.mana_only_subtype.empty() && d->params.mana_only_subtype_abilities
            && unmodeled != nullptr)
        { unmodeled->push_back("subtype-restricted mana from " + p.card.m_name.str()); }
        const std::size_t n_auras = HumanPreTapAuraFaces(state, p, o).size();
        // THE SAME TAP, SPELLED TWICE. The Auras' colours multiply the faces (Brushland under two Fertile Grounds:
        // 3 faces x 25 colour pairs, EDF seed 1130464), and when the two Auras are interchangeable a pair and its
        // swap are one tap: LandAuraAddToPoolOfLand credits the i-th pick to the i-th any-colour Aura, its
        // land_aura_extra_mana units, into one bonus pool, and nothing else reads the picks. So such a pair is
        // taken once, in order (the first of the two the enumeration reaches): a set holding the swap pays as the
        // set holding the pair, so it could only re-prove a result already recorded, and spend the probes that
        // bound the table.
        const bool sym_auras = n_auras >= 2 && OracleFaceFoldOn() && AuraPicksSymmetric(state, p);
        std::string key = p.card.m_name.str() + "|" + o.free_faces + "|" + restricted + "|";
        // Whatever is attached (a land Aura's bonus -- Utopia Sprawl, Wild Growth) is part of the yield,
        // so a copy carrying one is a different source from its bare twins.
        for (const Permanent& q : state.battlefield)
        { if (q.aura_attached_to == p.card.m_number) { key += "+" + q.card.m_name.str(); } }
        for (const std::string& f : o.fed_faces) { key += f + ","; }
        key += "|" + p.counters.KeyFragment() + "|" + std::to_string(p.storage_counters) + ","
             + std::to_string(p.charge_counters) + "|" + std::to_string(n_auras)
             + (p.entered_this_turn ? "|new" : "|old");
        if (t_plan_srcs != nullptr && t_plan_srcs->count(p.card.m_number) != 0)
        { key += "|plan#" + std::to_string(p.card.m_number); }
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
                        for (int c : aura_cols)
                        {
                            if (sym_auras && !pk.empty() && c < pk.back()) { continue; }
                            std::vector<int> q = pk; q.push_back(c); next.push_back(q);
                        }
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
// MTG_PAY_REFINE_TRACE: one [refine-trc] line per refine -- the engine's taps, the verifies by origin (the DFS,
// ExtendFed, ExtendSurplus), the parked/expanded surplus slots and the choice.
thread_local long t_trc_v[3] = {0, 0, 0};
thread_local int  t_trc_site = 0;

// The payment search for ONE set-aside: classes in battlefield order, each tapped 0..n times with a
// non-decreasing face sequence (copies are interchangeable). At an OPTIMISTIC cover the candidate is
// paid for REAL; if it pays it is recorded, once per leftover (Verify), and not extended with free taps
// (a superset only taps more of what could be tapped later) -- except by one tap when it holds a
// multi-unit source (ExtendSurplus) -- and it IS extended with FED conversions, because a converter
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
    bool                            whole_float = false;   // MinimalPayments' fast form: no leftover split either
    std::vector<Pick>               cur;
    // RefinePayment's LAZY SURPLUS: a paying set's one-more-tap supersets are parked here (the set, and where in
    // `out` they would have gone) instead of being verified at once -- see RefinePaymentImpl.
    struct Slot { std::vector<Pick> picks; std::size_t pos; };
    std::vector<Slot>*              defer = nullptr;

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

    // Pay for real on a scratch copy; record a NEW result; then the same taps once per OTHER leftover.
    // WHICH UNITS FLOAT ON IS A CHOICE TOO (EDF seed 3353 T8): the float-only payer spends a generic pip
    // in a fixed colour order, so a tap set that makes more than the bill always left the same unit --
    // never the {U} a Trace of Abundance's bonus could have floated for the Training Grounds cast two
    // payments later, which the engine's line did (its bonus floats as wild). A set-aside may hold
    // units the taps just made (ScriptedPay subtracts it after the taps), so every leftover of the
    // same size is its own option. Plain units only: a haste-tagged {C} or a wild unit in the pool
    // keeps the payer's own leftover.
    bool Verify(const std::vector<Pick>& picks)
    {
        ++t_trc_v[t_trc_site];
        if (++probes > cap) { capped = true; return false; }
        const PayOption o = Build(picks);
        ManaPool offered, left;
        if (!PayAndRecord(o, &offered, &left)) { return false; }
        // Only a multi-unit tap makes a leftover of its own: a set of one-unit taps pays exactly, and
        // the floating pool it was offered is already split by the outer set-aside enumeration.
        if (whole_float || !HasMultiUnit(picks)) { return true; }
        const int n = left.white + left.blue + left.black + left.red + left.green + left.colorless;
        if (n == 0 || offered.wild != 0 || left.wild != 0) { return true; }
        const int have[6] = { offered.white, offered.blue, offered.black, offered.red, offered.green, offered.colorless };
        const int was[6]  = { left.white, left.blue, left.black, left.red, left.green, left.colorless };
        int k[6] = { 0, 0, 0, 0, 0, 0 };
        std::function<void(int, int)> rec = [&](int c, int rest)
        {
            if (capped) { return; }
            if (c == 6)
            {
                if (rest != 0 || std::equal(k, k + 6, was)) { return; }
                if (++probes > cap) { capped = true; return; }
                PayOption o2 = o;
                o2.stash.white += k[0]; o2.stash.blue += k[1]; o2.stash.black += k[2];
                o2.stash.red   += k[3]; o2.stash.green += k[4]; o2.stash.colorless += k[5];
                PayAndRecord(o2, nullptr, nullptr);
                return;
            }
            for (int v = 0; v <= std::min(have[c], rest); ++v) { k[c] = v; rec(c + 1, rest - v); }
            k[c] = 0;
        };
        rec(0, n);
        return true;
    }

    // `offered` / `left` (optional): the general float the payment was offered (after the taps, less
    // the set-aside; `wild` set when a haste-tagged {C} or a wild unit is in it) and what it left of it.
    bool PayAndRecord(PayOption o, ManaPool* offered, ManaPool* left)
    {
        OracleScratch scr(state);
        GameState& sc = scr.Board();
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
        if (offered != nullptr)
        {
            *offered = sf;
            if (sc.floating_haste_c > 0) { offered->wild = std::max(1, offered->wild); }
        }
        t_float_only = true;
        const bool paid = TapForCostShared(sc, cost, for_creature, nullptr, true);
        t_float_only = false;
        if (!paid) { return why("the float-only payment failed"); }
        if (left != nullptr) { *left = sf; }
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

    // A SUPERSET WORTH TAPPING (EDF seed 3101 T4): a source that makes two or more units in one tap
    // (an Aura'd land, Aether Hub under Trace of Abundance) may pay part of this bill and float the
    // rest -- but only if another tap covers what that rest would have paid. Conservatory {G} + the
    // Hub's {C}+{U} pays Overgrowth's {2}{G} exactly, so the minimal search never taps Mariposa with
    // them; the engine does, keeps the Hub's {U} for Training Grounds, and leaves Overgrowth's host
    // untapped for its {G}{G}. So a paying set holding a multi-unit tap is also tried with ONE more
    // free one-unit tap (its leftovers enumerated by Verify). One, not more, and one-unit: deeper
    // supersets are not generated, a limit of this table (unbounded, they multiplied EDF seed 3353's
    // turn-8 table -- an Aether Hub under two Overgrowths, five Aura'd lands -- by ~40x).
    bool HasMultiUnit(const std::vector<Pick>& picks) const
    {
        for (const Pick& pk : picks)
        {
            const Face& f = srcs[static_cast<std::size_t>(pk.cls)].faces[static_cast<std::size_t>(pk.face)];
            if (!f.fed && f.net >= 2) { return true; }
        }
        return false;
    }
    void ExtendSurplus()
    {
        if (!HasMultiUnit(cur)) { return; }
        if (defer != nullptr) { defer->push_back({ cur, out.size() }); return; }
        ExtendSurplusNow();
    }
    void ExtendSurplusNow()
    {
        for (std::size_t c = 0; c < srcs.size() && !capped; ++c)
        {
            const SrcClass& sc = srcs[c];
            if (UsedCopies(static_cast<int>(c)) >= static_cast<int>(sc.nums.size())) { continue; }
            for (std::size_t fi = 0; fi < sc.faces.size() && !capped; ++fi)
            {
                // The added tap stands in for the unit that floats on, so a ONE-unit face: a second
                // multi-unit tap is a deeper superset (see above).
                if (sc.faces[fi].fed || sc.faces[fi].net != 1) { continue; }
                cur.push_back({ static_cast<int>(c), static_cast<int>(fi) });
                t_trc_site = 2; Verify(cur); t_trc_site = 0;
                cur.pop_back();
            }
        }
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
                const int site = t_trc_site; t_trc_site = 1;
                const bool vok = Verify(cur);
                t_trc_site = site;
                if (vok) { ExtendFed(c); }
                cur.pop_back();
            }
        }
    }

    void Dfs(std::size_t ci, const int col[6], int total)
    {
        if (capped) { return; }
        if (MaybeCovers(col, total, cost) && Verify(cur)) { ExtendFed(0); ExtendSurplus(); return; }
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
                                       std::vector<std::string>* unmodeled, bool creature_ability, bool whole_float,
                                       const Card* spell)
{
    // The payment's context, as the frontier saw it: a creature's activated ability being paid (the
    // Secluded Courtyard clause), the spell being paid for (a subtype restriction). Replayed around every
    // measure and verification below.
    std::optional<CreatureAbilityPayScope> _cap;
    if (creature_ability) { _cap.emplace(); }
    std::optional<SpellSubtypePayScope> _ssp;
    if (spell != nullptr) { _ssp.emplace(spell); }
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
    for (const PayOption& st : whole_float ? std::vector<PayOption>(1) : StashChoices(state, unmodeled))
    {
        const ManaPool& f = state.floating_mana;
        int col[6] = { f.white - st.stash.white, f.blue - st.stash.blue, f.black - st.stash.black,
                       f.red - st.stash.red, f.green - st.stash.green, f.colorless - st.stash.colorless };
        for (int c = 0; c < 6; ++c) { col[c] += extra[c]; }
        int total = col[0] + col[1] + col[2] + col[3] + col[4] + col[5] + f.wild;
        if (f.wild > 0) { for (int c = 0; c < 5; ++c) { col[c] += f.wild; } }
        PayEnum pe{ state, cost, for_creature, srcs, suffix_max, st, seen, out };
        pe.whole_float = whole_float;
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

bool PerformPayTaps(GameState& state, const PayOption& o) { return PerformTaps(state, o); }

// ---- PAY REFINE (ManaOracle.h) ------------------------------------------------------------------------
// WHY A SUBSET IS THE WHOLE QUESTION. The census's DOMINATED classes compare resources, and a tapped source
// is a resource: an oracle payment can only dominate the engine's if it taps nothing the engine's left
// untapped. So the better payment, when there is one, is always made of the engine's own taps -- fewer of
// them (a Plains beside a Remote Farm for Giada's {1}{W}), or the same ones on cheaper faces (Llanowar
// Wastes' {C} where the bill had a generic pip). The minimal payments of the bill on a board where ONLY
// those sources are untapped are exactly the candidates; each is then made on a copy of the REAL board (the
// mask only narrows the table: an Arbor Elf's untap target reads differently on it) and kept only if it
// dominates.
namespace
{
// Taps, set-aside and the float-only payment: MinimalPayments' own verification, on `sc`.
bool PayOptionOn(GameState& sc, const PayOption& o, const ManaCost& cost, bool for_creature)
{
    if (!PerformTaps(sc, o)) { return false; }
    ManaPool& sf = sc.floating_mana;
    if (sf.white < o.stash.white || sf.blue < o.stash.blue || sf.black < o.stash.black
        || sf.red < o.stash.red || sf.green < o.stash.green || sf.colorless < o.stash.colorless
        || sc.floating_haste_c < o.stash_haste)
    { return false; }
    sf.white -= o.stash.white; sf.blue -= o.stash.blue; sf.black -= o.stash.black;
    sf.red -= o.stash.red; sf.green -= o.stash.green; sf.colorless -= o.stash.colorless;
    sc.floating_haste_c -= o.stash_haste;
    sf.snow_units = std::min(sf.snow_units, sf.Total());
    t_float_only = true;
    const bool paid = TapForCostShared(sc, cost, for_creature, nullptr, true);
    t_float_only = false;
    sf.white += o.stash.white; sf.blue += o.stash.blue; sf.black += o.stash.black;
    sf.red += o.stash.red; sf.green += o.stash.green; sf.colorless += o.stash.colorless;
    sc.floating_haste_c += o.stash_haste;
    return paid;
}

// Can mana abilities alone take `c` to at least `e`'s resources, floating pools included? Only by tapping the
// sources `c` left untapped that `e` tapped -- each on any of its faces (BuildSources' table for them, every
// colour useful). Bounded: a few sources, a few faces.
bool ReachesByTapping(const GameState& c, const GameState& e)
{
    const int a = c.active_player_index;
    std::vector<int> released;
    OracleScratch mscr(c);
    GameState& masked = mscr.Board();
    for (std::size_t i = 0; i < masked.battlefield.size(); ++i)
    {
        Permanent& p = masked.battlefield[i];
        if (p.controller_index != a || p.tapped) { continue; }
        if (e.battlefield[i].tapped) { released.push_back(p.card.m_number); continue; }
        p.tapped = true;
    }
    if (released.empty()) { return CoversBoards(c, e, true, nullptr); }
    if (released.size() > 4) { return false; }
    // (for_creature: the table then also holds a restricted source's creature-only route.)
    const std::vector<SrcClass> srcs = BuildSources(masked, /*for_creature=*/true, nullptr, 31);
    std::vector<std::vector<std::pair<TurnSolver::PreTap, bool>>> faces_of;   // per released source: (tap, restricted)
    for (int num : released)
    {
        std::vector<std::pair<TurnSolver::PreTap, bool>> fs;
        for (const SrcClass& sc : srcs)
        {
            if (std::find(sc.nums.begin(), sc.nums.end(), num) == sc.nums.end()) { continue; }
            for (const Face& f : sc.faces) { TurnSolver::PreTap t = f.pt; t.num = num; fs.emplace_back(t, f.creature); }
        }
        if (fs.empty()) { return false; }
        faces_of.push_back(std::move(fs));
    }
    std::vector<std::size_t> k(faces_of.size(), 0);
    for (int guard = 0; guard < 256; ++guard)
    {
        OracleScratch cscr(c);
        GameState& c2 = cscr.Board();
        bool ok = true;
        for (std::size_t r = 0; r < faces_of.size() && ok; ++r)
        {
            const auto& f = faces_of[r][k[r]];
            ok = (f.second ? OracleCreatureTap(c2, f.first) : ApplyHumanPreTap(c2, f.first)).empty();
        }
        if (ok && CoversBoards(c2, e, true, nullptr)) { return true; }
        std::size_t r = 0;
        while (r < k.size() && ++k[r] == faces_of[r].size()) { k[r] = 0; ++r; }
        if (r == k.size()) { break; }
    }
    return false;
}
} // namespace

namespace
{
std::atomic<long long> g_refine_wild_out{0};   // refines the wild cap answered (MTG_PAY_REFINE_STATS)
}

static bool RefinePaymentImpl(const GameState& pre, const GameState& post, const ManaCost& cost, bool for_creature,
                              bool creature_ability, int useful, bool no_trade, PayOption& out)
{
    const int a = pre.active_player_index;
    const std::size_t n = pre.battlefield.size();
    if (post.battlefield.size() != n) { return false; }
    std::set<int> taps;
    for (std::size_t i = 0; i < n; ++i)
    {
        const Permanent& p0 = pre.battlefield[i];
        const Permanent& p1 = post.battlefield[i];
        if (p0.card.m_number != p1.card.m_number) { return false; }
        if (p0.controller_index == a && !p0.tapped && p1.tapped) { taps.insert(p0.card.m_number); }
    }
    if (taps.empty()) { return false; }
    OracleScratch mscr(pre);
    GameState& masked = mscr.Board();
    for (Permanent& p : masked.battlefield)
    { if (p.controller_index == a && !p.tapped && taps.count(p.card.m_number) == 0) { p.tapped = true; } }
    std::optional<CreatureAbilityPayScope> _cap;
    if (creature_ability) { _cap.emplace(); }
    // MinimalPayments' whole-float table on the masked board, its PayEnum held here so the surplus supersets can
    // be verified lazily (below).
    const std::vector<SrcClass> srcs = BuildSources(masked, for_creature, nullptr, useful);
    bool all_regular = true;
    for (const SrcClass& sc : srcs) { for (const Face& fx : sc.faces) { if (!fx.regular) { all_regular = false; } } }
    // A FLOAT NO PAYMENT OF THESE TAPS CAN MAKE. Every acceptance below ends in covering the engine's floating pools
    // key by key -- the candidate itself, or the candidate with its released sources re-tapped (ReachesByTapping) --
    // and a `wild` unit is a key of its own: the payer credits an any-colour land Aura's bonus as wild, where the
    // table's faces take a concrete colour. Those taps (every face regular: a candidate and its re-taps tap the
    // engine's own sources, each once) can float no more wild than was floating already plus each source's wildest
    // face. When the engine's payment leaves more than that, no candidate covers it and there is nothing to search:
    // EDF seed 1130464 gi463, 136,104 of 248,589 refines (Brushland under Fertile Ground / Trace of Abundance).
    if (all_regular && PayRefineWildOutOn())
    {
        auto wild_all = [](const GameState& g)
        { return g.floating_mana.wild + g.floating_creature_mana.wild + g.floating_bigspell_mana.wild; };
        int wild_cap = wild_all(pre);
        for (const SrcClass& sc : srcs)
        {
            int mw = 0;
            for (const Face& fx : sc.faces) { mw = std::max(mw, fx.wild); }
            wild_cap += mw * static_cast<int>(sc.nums.size());
        }
        if (wild_all(post) > wild_cap) { g_refine_wild_out.fetch_add(1, std::memory_order_relaxed); return false; }
    }
    std::vector<int> suffix_max(srcs.size() + 1, 0);
    for (std::size_t i = srcs.size(); i-- > 0;)
    { suffix_max[i] = suffix_max[i + 1] + srcs[i].max_net * static_cast<int>(srcs[i].nums.size()); }
    const ManaPool& fc  = masked.floating_creature_mana;
    const ManaPool& fbg = masked.floating_bigspell_mana;
    const ManaPool& f   = masked.floating_mana;
    int col[6] = { f.white + (for_creature ? fc.white : 0) + fbg.white, f.blue + (for_creature ? fc.blue : 0) + fbg.blue,
                   f.black + (for_creature ? fc.black : 0) + fbg.black, f.red + (for_creature ? fc.red : 0) + fbg.red,
                   f.green + (for_creature ? fc.green : 0) + fbg.green,
                   f.colorless + (for_creature ? fc.colorless : 0) + fbg.colorless };
    const int total = col[0] + col[1] + col[2] + col[3] + col[4] + col[5] + f.wild;
    if (f.wild > 0) { for (int c = 0; c < 5; ++c) { col[c] += f.wild; } }
    // THE LAZY SURPLUS. A paying set holding a multi-unit tap is also tried with one more one-unit tap (PayEnum's
    // ExtendSurplus) -- the bulk of the work on a big board (FiveColour's Nicol Bolas over six taps: 830 sets,
    // 3,267 supersets, and the 4,096-probe cap hit). A superset taps one more of the engine's own sources, so with
    // every face REGULAR (it taps its own source and nothing else) it releases exactly |taps| - |set| - 1, and the
    // choice below takes the most released first: a set whose supersets cannot reach the best release found is
    // never extended. The rest are verified in their own place in the order, so the choice is the one the eager
    // enumeration makes (an irregular face anywhere -- a sacrifice, an untap -- keeps the eager order).
    std::unordered_set<std::uint64_t> seen;
    std::vector<PayOption> opts;
    const PayOption st0;
    std::vector<PayEnum::Slot> slots;
    PayEnum pe{ masked, cost, for_creature, srcs, suffix_max, st0, seen, opts };
    pe.whole_float = true;
    if (PayRefineLazyOn() && all_regular) { pe.defer = &slots; }
    t_trc_v[0] = t_trc_v[1] = t_trc_v[2] = 0;
    pe.Dfs(0, col, total);
    static const bool s_trc = EnvOn("MTG_PAY_REFINE_TRACE");
    std::size_t expanded = 0;
    bool found = false;
    int  best_rel = -1, best_life = 0, best_energy = 0;
    // Order of the eager enumeration: option i of the first pass sits at (i, 1, 0); the j-th superset of the set
    // parked at `pos` at (pos, 0, j) -- after the set and its fed extensions, before the next option.
    std::tuple<std::size_t, int, std::size_t> best_at{ 0, 0, 0 };
    std::string trc_taps;
    if (s_trc)
    {
        for (const Permanent& p : pre.battlefield)
        { if (taps.count(p.card.m_number) != 0) { trc_taps += (trc_taps.empty() ? "" : ", ") + p.card.m_name.str(); } }
        trc_taps += " | float " + PoolText(pre.floating_mana, pre.floating_haste_c);
    }
    struct TrcOut { std::size_t T, nopt, nslot; const std::size_t& nexp; const ManaCost& c; int* rel; bool* f;
        const std::string& taps; const PayOption& out;
        ~TrcOut() { if (s_trc) { std::lock_guard<std::mutex> lk(g_print_mu);
            std::fprintf(stderr, "[refine-trc] T=%zu bill=%s v=%ld/%ld/%ld opts=%zu slots=%zu/%zu found=%d rel=%d taps=[%s] -> %s\n", T,
                         c.ToString().c_str(), t_trc_v[0], t_trc_v[1], t_trc_v[2], nopt, nexp, nslot, *f ? 1 : 0, *rel,
                         taps.c_str(), *f ? TapsText(out).c_str() : "-"); } } };
    TrcOut _trc{ taps.size(), opts.size(), slots.size(), expanded, cost, &best_rel, &found, trc_taps, out };
    auto consider = [&](const PayOption& o, std::tuple<std::size_t, int, std::size_t> at)
    {
        OracleScratch cscr(pre);
        GameState& c = cscr.Board();
        if (!PayOptionOn(c, o, cost, for_creature) || c.battlefield.size() != n) { return; }
        int rel = 0;
        bool subset = true;
        for (std::size_t i = 0; i < n && subset; ++i)
        {
            if (c.battlefield[i].tapped && !post.battlefield[i].tapped) { subset = false; }
            if (!c.battlefield[i].tapped && post.battlefield[i].tapped) { ++rel; }
        }
        if (!subset) { return; }
        const int life = c.players[a].life, energy = c.players[a].energy_counters;
        // Only a candidate the choice would take is worth proving: the acceptance below is the costly part (the
        // resource tables, the re-tapping), and a candidate it would not take changes nothing either way.
        const bool better = !found || rel > best_rel
                         || (rel == best_rel && (life > best_life || (life == best_life
                             && (energy > best_energy || (energy == best_energy && at < best_at)))));
        if (!better) { return; }
        // Covers(end) reads life and energy as they are: less of either is no cover.
        if (life < post.players[a].life || energy < post.players[a].energy_counters) { return; }
        // Strictly better somewhere: a source given back, or more of what the payment spends (life, energy;
        // a haste-tagged {C} still floating is a mid-turn resource).
        bool strict_end = false, strict_mid = false;
        if (!CoversBoards(c, post, false, &strict_end)) { return; }
        if (rel == 0 && !strict_end && !(CoversBoards(c, post, true, &strict_mid) && strict_mid)) { return; }
        // NO FLOAT FOR A SOURCE WHERE A SAME-PHASE UNTAPPER LIVES (GameState::deck_has_mana_untapper). An
        // untapped source is worth the mana it would float only while nothing can untap it again this phase:
        // Cloud of Faeries' enter untaps two lands, so the engine's {1}{U} off Brushland AND Adarkar Wastes
        // (one unit floating) comes back with both lands up AND the unit, where Adarkar alone leaves Brushland
        // up and nothing floating -- one mana down, and the go-off turn short (EDF seed 1170039 gi38 and
        // seed 2110 gi108, T3 -> T4 at d8 b0). There the candidate must keep the engine's float outright.
        if (no_trade ? !CoversBoards(c, post, true, nullptr) : !ReachesByTapping(c, post)) { return; }
        found = true; best_rel = rel; best_life = life; best_energy = energy; best_at = at; out = o;
    };
    const std::size_t first = opts.size();
    for (std::size_t i = 0; i < first; ++i) { consider(opts[i], { i, 1, 0 }); }
    for (const PayEnum::Slot& sl : slots)
    {
        const int reach = static_cast<int>(taps.size()) - static_cast<int>(sl.picks.size()) - 1;
        if (found && reach < best_rel) { continue; }
        ++expanded;
        pe.cur = sl.picks;
        const std::size_t from = opts.size();
        pe.ExtendSurplusNow();
        for (std::size_t j = from; j < opts.size(); ++j) { consider(opts[j], { sl.pos, 0, j - from }); }
    }
    return found;
}

// THE REFINE IS A FUNCTION OF ITS BOARDS, so it is memoised. The search pays the same bill from the same board
// over and over (FiveColour seed 1110001: 432 of 903 refines were one Nicol Bolas payment, ~3,200 verified
// candidates each). Keyed on the engine's own mid-turn identity of the board before, plus every field a
// payment moves, read on BOTH boards -- the restricted pools, which that identity does not fold, among them --
// and on the bill, its context, the payer's own context (PayContextDigest: the line it pays in decides which
// floating units a float-only payment spends) and the useful-colour mask (BuildSources folds faces by it; the
// identity reads a library by size and front only). A plan-source mask (the oracle's own Realize) is not keyed: not memoised.
// MTG_PAY_REFINE_MEMO=0 turns it off; MTG_PAY_REFINE_MEMO_VERIFY=1 recomputes every hit and counts mismatches.
namespace
{
std::uint64_t RefineMix(std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h * 0xBF58476D1CE4E5B9ULL;
}
std::uint64_t PoolWord(const ManaPool& f)
{
    return (static_cast<std::uint64_t>(f.white & 0xFF) << 56) ^ (static_cast<std::uint64_t>(f.blue & 0xFF) << 48)
         ^ (static_cast<std::uint64_t>(f.black & 0xFF) << 40) ^ (static_cast<std::uint64_t>(f.red & 0xFF) << 32)
         ^ (static_cast<std::uint64_t>(f.green & 0xFF) << 24) ^ (static_cast<std::uint64_t>(f.colorless & 0xFF) << 16)
         ^ (static_cast<std::uint64_t>(f.wild & 0xFF) << 8) ^ static_cast<std::uint64_t>(f.snow_units & 0xFF);
}
std::uint64_t PayFieldsDigest(const GameState& s)
{
    std::uint64_t h = 0x51ED27A1ULL;
    h = RefineMix(h, s.battlefield.size());
    for (const Permanent& p : s.battlefield)
    {
        h = RefineMix(h, static_cast<std::uint32_t>(p.card.m_number));
        h = RefineMix(h, (p.tapped ? 1u : 0u) | (p.temp_haste ? 2u : 0u)
                         | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.controller_index)) << 2));
        h = RefineMix(h, (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.charge_counters)) << 32)
                         | static_cast<std::uint32_t>(p.storage_counters));
    }
    const ManaPool* pools[3] = { &s.floating_mana, &s.floating_creature_mana, &s.floating_bigspell_mana };
    for (const ManaPool* f : pools)
    {
        h = RefineMix(h, PoolWord(*f));
        h = RefineMix(h, (static_cast<std::uint64_t>(f->wild_c & 0xFFFF) << 32) ^ (static_cast<std::uint64_t>(f->wild_phantom & 0xFFFF) << 16)
                         ^ static_cast<std::uint64_t>(f->wild_conv & 0xFFFF));
    }
    h = RefineMix(h, static_cast<std::uint32_t>(s.floating_haste_c));
    for (const Player& pl : s.players)
    {
        h = RefineMix(h, static_cast<std::uint32_t>(pl.life));
        h = RefineMix(h, static_cast<std::uint32_t>(pl.energy_counters));
        h = RefineMix(h, pl.graveyard.size());
    }
    h = RefineMix(h, s.opponent_lost_life_this_turn ? 1u : 0u);
    h = RefineMix(h, static_cast<std::uint32_t>(s.active_player_index));
    return h;
}
struct RefineKey
{
    std::uint64_t board = 0, pay_pre = 0, pay_post = 0, payer = 0;
    std::uint64_t bill = 0;
    int           ctx = 0;
    bool operator==(const RefineKey& o) const
    {
        return board == o.board && pay_pre == o.pay_pre && pay_post == o.pay_post && payer == o.payer && ctx == o.ctx
            && bill == o.bill;
    }
};
struct RefineKeyHash
{
    std::size_t operator()(const RefineKey& k) const
    {
        return static_cast<std::size_t>(k.board ^ (k.pay_pre * 0x9E3779B97F4A7C15ULL) ^ (k.pay_post * 0xC2B2AE3D27D4EB4FULL)
                                        ^ (k.payer * 0x165667B19E3779F9ULL) ^ (k.bill * 0x27D4EB2F165667C5ULL));
    }
};
struct RefineMemoEntry { bool found = false; PayOption opt; };
thread_local std::unordered_map<RefineKey, RefineMemoEntry, RefineKeyHash> t_refine_memo;
std::atomic<long long> g_rmemo_hit{0}, g_rmemo_miss{0}, g_rmemo_bad{0};
struct RefineMemoReport
{
    ~RefineMemoReport()
    {
        static const bool s_on = EnvOn("MTG_PAY_REFINE_STATS");
        if (!s_on) { return; }
        std::fprintf(stderr, "=== PAY REFINE MEMO: hits=%lld misses=%lld verify_mismatch=%lld wild_out=%lld ===\n",
                     g_rmemo_hit.load(), g_rmemo_miss.load(), g_rmemo_bad.load(), g_refine_wild_out.load());
    }
} g_refine_memo_report;
bool SameOption(bool fa, const PayOption& a, bool fb, const PayOption& b)
{
    if (fa != fb) { return false; }
    if (!fa) { return true; }
    return TapsText(a) == TapsText(b) && a.creature_route == b.creature_route && a.stash == b.stash
        && a.stash_haste == b.stash_haste;
}
} // namespace

bool RefinePayment(const GameState& pre, const GameState& post, const ManaCost& cost, bool for_creature,
                   bool creature_ability, PayOption& out)
{
    static const bool s_verify = EnvOn("MTG_PAY_REFINE_MEMO_VERIFY");
    // (The masked board the refine builds its table on differs from `pre` only in tapped flags: one useful-colour mask.)
    const int useful = UsefulColorMask(pre, cost);
    const bool no_trade = pre.deck_has_mana_untapper;
    if (!PayRefineMemoOn() || t_plan_srcs != nullptr)
    { return RefinePaymentImpl(pre, post, cost, for_creature, creature_ability, useful, no_trade, out); }
    RefineKey k;
    k.board    = TurnSolver::MidTurnStateKeyOf(pre);
    k.pay_pre  = PayFieldsDigest(pre);
    k.pay_post = PayFieldsDigest(post);
    k.payer    = PayContextDigest();
    k.bill     = ManaCostDigest(cost);
    k.ctx      = (for_creature ? 1 : 0) | (creature_ability ? 2 : 0) | (PayingCreatureAbility() ? 4 : 0)
               | (useful << 3) | (no_trade ? (1 << 12) : 0);
    auto it = t_refine_memo.find(k);
    if (it != t_refine_memo.end())
    {
        g_rmemo_hit.fetch_add(1, std::memory_order_relaxed);
        if (s_verify)
        {
            PayOption o2;
            const bool f2 = RefinePaymentImpl(pre, post, cost, for_creature, creature_ability, useful, no_trade, o2);
            if (!SameOption(it->second.found, it->second.opt, f2, o2))
            {
                g_rmemo_bad.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(g_print_mu);
                std::fprintf(stderr, "[pay-refine-memo] MISMATCH bill %s: memo %s, fresh %s\n", cost.ToString().c_str(),
                             it->second.found ? TapsText(it->second.opt).c_str() : "-", f2 ? TapsText(o2).c_str() : "-");
            }
        }
        if (it->second.found) { out = it->second.opt; }
        return it->second.found;
    }
    g_rmemo_miss.fetch_add(1, std::memory_order_relaxed);
    RefineMemoEntry e;
    e.found = RefinePaymentImpl(pre, post, cost, for_creature, creature_ability, useful, no_trade, e.opt);
    if (e.found) { out = e.opt; }
    if (t_refine_memo.size() >= 8192) { t_refine_memo.clear(); }
    const bool found = e.found;
    t_refine_memo.emplace(std::move(k), std::move(e));
    return found;
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
    int                      hit = -1;    // stop_at_full: the index of the outcome the search stopped at
};
// A caller's further test of a full outcome (Realize's `accept`): false = not this caller's hit, search on.
using OutcomeTest = std::function<bool(const Outcome&)>;

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
    bool                   has_spell = false;   // the spell the frontier payment is for (Hook::f_spell)
    Card                   spell;
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
// `accept` (with stop_at_full): ...and that this test passes (Realization::hit names the one it stopped at).
Realization Realize(const GameState& root, const TurnSolver::Plan& plan, bool pre, bool stop_at_full,
                    const std::vector<PayOption>& prefix = {}, bool all_engine = false,
                    const std::multiset<std::string>* want = nullptr, const OutcomeTest* accept = nullptr)
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
    struct PlanSrcScope
    {
        const std::set<int>* saved;
        explicit PlanSrcScope(const std::set<int>* s) : saved(t_plan_srcs) { t_plan_srcs = s; }
        ~PlanSrcScope() { t_plan_srcs = saved; }
    } _plan_src_scope(&plan_srcs);
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
                fr.has_spell    = h.f_has_spell;
                if (h.f_has_spell) { fr.spell = h.f_spell; }
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
            const bool full = stop_at_full && o.Clean() && o.unscripted.empty()
                           && (want == nullptr || Includes(o.left, *want)) && (accept == nullptr || (*accept)(o));
            r.outcomes.push_back(std::move(o));
            if (full) { r.hit = static_cast<int>(r.outcomes.size()) - 1; return r; }
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
                                               + "t" + std::to_string(fronts[i].tapahead)
                                               + (fronts[i].has_spell ? "|" + fronts[i].spell.m_name.str() : ""));
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
                : MinimalPayments(f.st, f.cost, f.for_creature, &r.unmodeled, f.creature_ability, /*whole_float=*/false,
                                  f.has_spell ? &f.spell : nullptr);
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
        if (p.aura_attached_to != q.aura_attached_to || p.equipped_to != q.equipped_to)
        {
            add(nm + " on #" + std::to_string(p.aura_attached_to != 0 ? p.aura_attached_to : p.equipped_to)
                + " by engine, #" + std::to_string(q.aura_attached_to != 0 ? q.aura_attached_to : q.equipped_to)
                + " by oracle");
        }
    }
    for (const Permanent& q : o.battlefield)
    { if (seen.count(q.card.m_number) == 0) { add(q.card.m_name.str() + "#" + std::to_string(q.card.m_number) + " only in oracle"); } }
    if (e.players[a].life != o.players[a].life)
    { add("life engine " + std::to_string(e.players[a].life) + " oracle " + std::to_string(o.players[a].life)); }
    if (e.players[a].energy_counters != o.players[a].energy_counters) { add("energy differs"); }
    if (e.players[a].hand.size() != o.players[a].hand.size())  { add("hand size differs"); }
    else
    {
        std::multiset<std::string> eh, oh;
        for (const Card& c : e.players[a].hand) { eh.insert(c.m_name.str()); }
        for (const Card& c : o.players[a].hand) { oh.insert(c.m_name.str()); }
        if (eh != oh) { add("hand cards differ"); }
    }
    if (e.players[a].sideboard.size() != o.players[a].sideboard.size()) { add("sideboard size differs"); }
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

// ---- ENUM-GAP: what a paying realisation of a gate-rejected plan must show (EnumGapPlan) ----------------------------
// The hand cards a line really USED -- cast, put, pitched: LeftHand less any card that went back to the LIBRARY.
// NivMizzetCombo seed 1100005 gi4 T3: Brainstorm put the plan's Generator Servant back, its breakpoint continuation cast
// Izzet Signet instead, and "left the hand" read the Servant as cast.
std::multiset<std::string> UsedFromHand(const GameState& root, const GameState& fin)
{
    const int a = fin.active_player_index;
    std::multiset<int> now, lib;
    for (const Card& c : fin.players[a].hand) { now.insert(c.m_number); }
    // (RawAt: identities only, and the oracle is no game decision -- see Library.h's index-carry note.)
    const Library& fl = fin.players[a].library;
    for (std::size_t i = 0; i < fl.size(); ++i) { lib.insert(fl.RawAt(i).m_number); }
    std::multiset<std::string> out;
    for (const Card& c : root.players[root.active_player_index].hand)
    {
        auto it = now.find(c.m_number);
        if (it != now.end()) { now.erase(it); continue; }
        auto jt = lib.find(c.m_number);
        if (jt != lib.end()) { lib.erase(jt); continue; }
        out.insert(c.m_name.str());
    }
    return out;
}

// HIDDEN INFORMATION. The cards a realisation took off the TOP of the library -- the root library's leading cards
// that are gone at the end, up to the first one still there: a reveal, a dig or a draw takes them in order, a search
// (a fetch land, Rampant Growth) takes its card from anywhere. A payment that taps one of those spent mana no player
// could plan on when the plan was chosen, because the library's ORDER is hidden (a top card Courser of Kruphix
// reveals is not). Selesnya / Landfall eg1, 11 ENUM-GAPs: Genesis Wave, then Feed the Clan / Rampant Growth off the
// lands the Wave put onto the battlefield (Forest #21, Brushland #19, Wirewood Lodge #60...). `what`: the card.
bool TapsHiddenCard(const GameState& root, const Outcome& o, std::string* what)
{
    const int a = root.active_player_index;
    const Library& lib0 = root.players[a].library;
    const Library& lib1 = o.st.players[a].library;
    if (lib1.size() >= lib0.size()) { return false; }
    std::set<int> still;
    for (std::size_t i = 0; i < lib1.size(); ++i) { still.insert(lib1.RawAt(i).m_number); }
    std::set<int> top;
    for (std::size_t i = 0; i < lib0.size() && still.count(lib0.RawAt(i).m_number) == 0; ++i)
    { top.insert(lib0.RawAt(i).m_number); }
    if (!top.empty() && LibraryTopVisible(root)) { top.erase(lib0.RawAt(0).m_number); }
    if (top.empty()) { return false; }
    for (const PayOption& p : o.script)
    {
        for (const TurnSolver::PreTap& t : p.taps)
        {
            if (top.count(t.num) == 0) { continue; }
            if (what != nullptr) { *what = t.name + "#" + std::to_string(t.num); }
            return true;
        }
    }
    return false;
}

// A LAND DROP THE PLAN DID NOT MAKE. A plan decides its land (`land=X`, or `no land`); a breakpoint's continuation,
// re-solved inside the apply, may then play a land from hand itself. Mana from that land is the continuation's, not
// the plan's: the plan with that land drop is its own entry of the observe-mode enumeration, checked on its own.
// NivMizzetCombo seed 1100005 gi4 T3, "no land + Izzet Signet + crack Generator Servant + Servant + Curiosity +
// Brainstorm": after Brainstorm the continuation played Island #19, and the Island paid for the plan's own Curiosity.
// True (and `what` = the land, `land` = its name) when the script taps a land card from the root hand other than the
// plan's own drop. A plan whose land is left to the heuristic (`land_decided` false) is not asked.
bool TapsContinuationLand(const GameState& root, const TurnSolver::Plan& plan, const Outcome& o, std::string* what,
                          std::string* land)
{
    if (!plan.land_decided) { return false; }
    std::map<int, std::string> hand_lands;
    for (const Card& c : root.players[root.active_player_index].hand)
    {
        if (PlayableAsLand(CardDatabase::Instance().LookupCached(c))) { hand_lands[c.m_number] = c.m_name.str(); }
    }
    if (hand_lands.empty()) { return false; }
    int own = plan.land_to_play.empty() ? 0 : 1;   // copies of the plan's own land the script may tap
    std::set<int> seen;
    for (const PayOption& p : o.script)
    {
        for (const TurnSolver::PreTap& t : p.taps)
        {
            const auto it = hand_lands.find(t.num);
            if (it == hand_lands.end() || !seen.insert(t.num).second) { continue; }
            if (own > 0 && it->second == plan.land_to_play) { --own; continue; }
            if (what != nullptr) { *what = t.name + "#" + std::to_string(t.num); }
            if (land != nullptr) { *land = it->second; }
            return true;
        }
    }
    return false;
}

// One apply of `plan` on a copy of `root` with exactly this payment script and nothing past it. True when it ran to
// the end -- no frontier, no refused tap, every entry consumed, no drop -- with `out` the board it ended on.
bool ReplayScript(const GameState& root, bool pre, const TurnSolver::Plan& plan, const std::vector<PayOption>& script,
                  GameState& out)
{
    out = root;
    Hook h;
    h.target = &out;
    h.script = script;
    std::vector<std::string> cast_drops, act_drops;
    std::vector<std::string>* saved_cast = g_play_dropped_cast_sink;
    std::vector<std::string>* saved_act  = g_act_drop_sink;
    g_play_dropped_cast_sink = &cast_drops;
    g_act_drop_sink          = &act_drops;
    Hook* const saved_hook = t_hook;
    t_hook = &h;
    TurnSolver::ApplyPlan(out, plan, pre);
    t_hook = saved_hook;
    g_play_dropped_cast_sink = saved_cast;
    g_act_drop_sink          = saved_act;
    g_applies.fetch_add(1, std::memory_order_relaxed);
    return h.tap_error.empty() && !h.frontier && h.next == script.size() && cast_drops.empty() && act_drops.empty();
}

// THE ACTIONS A REALISATION REALLY PERFORMED. An apply can skip an action and report no drop: a TapForTokenPay or an
// AnimateLand whose source a scripted payment had tapped (ApplyPlanDirect's trailing pass `break`s on a stale source --
// Slivers eg1, 34 ENUM-GAPs: Sliver Hive's {5},{T} after the Hive paid Plated Sliver), an equip onto the creature the
// Equipment's own entry trigger attached it to (Sigarda's Aid -- Colossus Hammer, 4), a cast a breakpoint's
// continuation replaced (Brainstorm put the card back -- NivMizzetCombo, 2). Such an outcome realises the plan WITHOUT
// that action, paid this way. Test: an action whose removal, replayed with the same payment script, ends on the same
// board -- exact identity, every resource, the floating pools, the whole script consumed -- did nothing here. Returns
// the plan less every such action (the plan itself when it performed them all).
TurnSolver::Plan PerformedPlan(const GameState& root, bool pre, const TurnSolver::Plan& plan, const Outcome& o)
{
    TurnSolver::Plan cur = plan;
    const std::uint64_t k0 = CoreKey(o.st, /*canonical=*/false);
    const Res           r0 = Resources(o.st, /*with_float=*/true);
    GameState out;
    for (std::size_t i = 0; i < cur.actions.size();)
    {
        const Action& a = cur.actions[i];
        if (a.kind == Action::Kind::PlayLand || a.sacrifice_land) { ++i; continue; }
        TurnSolver::Plan t = cur;
        t.actions.erase(t.actions.begin() + static_cast<std::ptrdiff_t>(i));
        if (ReplayScript(root, pre, t, o.script, out) && CoreKey(out, /*canonical=*/false) == k0
            && Resources(out, /*with_float=*/true) == r0)
        { cur = std::move(t); continue; }
        ++i;
    }
    return cur;
}

// A LAND AURA'S HOST IS A CHOICE OF THE PLAN, NOT A RESOURCE. A mana land Aura (Wild Growth, Fertile Ground, Trace of
// Abundance, Overgrowth) adds its bonus whenever its host is tapped for mana, so two boards that differ only in which
// land carries it make the same mana from the next untap on; this turn, what differs is whether the bonus is still
// available -- its host untapped. EDF seed 1100009 gi8 T2 (2 ENUM-GAPs): "Wild Growth on Brushland, then Cloud of
// Faeries" pays only Cloud-first, while the menu's Wild Growth on Adarkar Wastes pays Wild Growth off Brushland's {G}
// and Cloud off the Wastes' {U}+{G} and ends with a land more untapped. USER: "With Adarkar Wastes and Brushland you
// would play Wild Growth first ... I see no need to change the order for that case".
bool IsManaLandAura(const Permanent& q)
{
    if (q.aura_attached_to <= 0) { return false; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(q.card);
    return d != nullptr && d->params.is_land_aura && d->params.land_aura_extra_mana > 0;
}
bool HasManaLandAura(const GameState& s)
{
    for (const Permanent& q : s.battlefield)
    { if (q.controller_index == s.active_player_index && IsManaLandAura(q)) { return true; } }
    return false;
}
// CoreKey with every mana land Aura's host forgotten.
std::uint64_t HostBlindCore(const GameState& s)
{
    GameState n = s;
    for (Permanent& q : n.battlefield) { if (IsManaLandAura(q)) { q.aura_attached_to = 0; } }
    return CoreKey(n);
}
// Resources with every mana land Aura's host forgotten: a land is keyed without those Auras, and each Aura whose host
// is untapped counts as a resource of its own ("AU:"), the bonus still to be had.
Res HostBlindRes(const GameState& s)
{
    Res r;
    const int a = s.active_player_index;
    std::map<int, const Permanent*> by_num;
    for (const Permanent& p : s.battlefield) { by_num[p.card.m_number] = &p; }
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != a) { continue; }
        std::vector<std::string> on;
        for (const Permanent& q : s.battlefield)
        {
            if ((q.aura_attached_to == p.card.m_number && !IsManaLandAura(q)) || q.equipped_to == p.card.m_number)
            { on.push_back(q.card.m_name.str()); }
        }
        std::sort(on.begin(), on.end());
        std::string k = ResClassKey(p);
        if (!on.empty()) { k += "|on:"; for (const std::string& x : on) { k += x + ";"; } }
        if (!p.tapped)    { ++r["U:" + k]; }
        if (p.temp_haste) { ++r["H:" + k]; }
        if (IsManaLandAura(p))
        {
            const auto it = by_num.find(p.aura_attached_to);
            if (it != by_num.end() && !it->second->tapped) { ++r["AU:" + p.card.m_name.str()]; }
        }
    }
    r["L:life"]   = s.players[a].life;
    r["E:energy"] = s.players[a].energy_counters;
    return r;
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
        // ...but a same-line realisation only REACHED the engine's line; it is not the oracle's best payment.
        // EDF seed 3946 T4 (Conservatory, Eldrazi Displacer; the engine's continuation casts Training
        // Grounds): the painless payments the engine's own matches -- Brushland {C} + Trace of Abundance,
        // Mariposa + Fertile Ground -- float a {U}, and the continuation solver on THAT board cast nothing
        // more, while a one-tap-wider payment (a land more, 1 life more) happened to reach the engine's line.
        // Judged against that one alone, the plan read as a gap. When a different-line realisation is at
        // least the engine's life, the payment question is open on the continuation, not on the mana.
        const int e_life = e.players[static_cast<std::size_t>(e.active_player_index)].life;
        for (const Outcome& o : r.outcomes)
        {
            if (!o.Full() || o.line == e_line) { continue; }
            if (o.st.players[static_cast<std::size_t>(o.st.active_player_index)].life < e_life) { continue; }
            Emit(C_CONT_DIFFERS, root, pre, plan,
                 "the realisations on the engine's line do not cover it, and one at least as healthy continued "
                 "with different cards (the engine's continuation solver saw a different board)");
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

// ---- ENUM-GAP: one plan a MANA GATE kept off the menu ---------------------------------------------------------------
// What the menu offers, as the per-plan check reads it.
struct MenuView
{
    std::set<std::string>              on_menu;    // SiblingKey of every menu entry
    std::set<std::string>              observed;   // ...of every plan the observe-mode enumeration holds
    std::multimap<std::uint64_t, Res>  cores;      // every menu entry's engine end board: CoreKey -> resources
    std::vector<const GameState*>      boards;     // ...the boards themselves
    // The same, every mana land Aura's host forgotten (HostBlindCore / HostBlindRes; the entry's index in `boards`):
    // built on first use.
    mutable bool                                                        hb_built = false;
    mutable std::multimap<std::uint64_t, std::pair<Res, std::size_t>>   hb_cores;
};

// Level-3 trace of the ENUM-GAP pass's rejections (which rule set a paying realisation aside, and why).
void EgTrace(const GameState& root, bool pre, const TurnSolver::Plan& t, const std::string& what)
{
    if (Level() < 3 || (TraceTurn() != 0 && TraceTurn() != root.turn_number)) { return; }
    std::lock_guard<std::mutex> lk(g_print_mu);
    std::fprintf(stderr, "[mana-oracle]   enum-gap %s plan={%s}: %s\n", Ctx(root, pre).c_str(), PlanText(t).c_str(),
                 what.c_str());
}
MenuView MakeMenuView(const std::vector<std::string>& keys, const std::vector<EngineReal>& real,
                      const std::vector<TurnSolver::Plan>& observed)
{
    MenuView mv;
    mv.on_menu.insert(keys.begin(), keys.end());
    for (const TurnSolver::Plan& p : observed) { mv.observed.insert(SiblingKey(p)); }
    for (const EngineReal& e : real)
    {
        mv.cores.emplace(CoreKey(e.st), Resources(e.st, /*with_float=*/false));
        mv.boards.push_back(&e.st);
    }
    return mv;
}

struct GapVerdict
{
    Cls         cls = C_N;    // C_N: nothing to report
    std::string detail;
};

// The verdict on one paying realisation `o` of `t` (an order of the gate-rejected plan `p`; `reordered`: not p's own
// order): C_N when it is no missing option -- it did not use the plan's cards or cast its spells, or the menu reaches
// its board with at least its resources (exactly, or with the land Auras' hosts aside) -- else the class and text.
GapVerdict JudgeHit(const GameState& state, bool pre, const TurnSolver::Plan& t, bool reordered, const Outcome& o,
                    const std::multiset<std::string>& want, int n_casts, const MenuView& mv)
{
    GapVerdict v;
    // Every hand card the plan names really used (not put back on the library), and every cast really cast (an
    // action can also be skipped silently -- its card no longer in hand -- which no drop sink reports).
    if (!Includes(UsedFromHand(state, o.st), want)) { return v; }
    if (o.st.spells_cast_this_turn - state.spells_cast_this_turn < n_casts) { return v; }
    const auto range = mv.cores.equal_range(CoreKey(o.st));
    const Res hr = Resources(o.st, /*with_float=*/false);
    // The menu reaches this board already, with at least the resources.
    for (auto it = range.first; it != range.second; ++it) { if (Covers(it->second, hr, nullptr)) { return v; } }
    // ...or reaches it with a land Aura on another host and still at least the resources (see IsManaLandAura).
    if (HasManaLandAura(o.st))
    {
        if (!mv.hb_built)
        {
            for (std::size_t i = 0; i < mv.boards.size(); ++i)
            { mv.hb_cores.emplace(HostBlindCore(*mv.boards[i]), std::make_pair(HostBlindRes(*mv.boards[i]), i)); }
            mv.hb_built = true;
        }
        const auto hb = mv.hb_cores.equal_range(HostBlindCore(o.st));
        const Res hbr = HostBlindRes(o.st);
        for (auto it = hb.first; it != hb.second; ++it)
        {
            if (!Covers(it->second.first, hbr, nullptr)) { continue; }
            g_eg_host_blind.fetch_add(1, std::memory_order_relaxed);
            EgTrace(state, pre, t, "host-blind: paying it with " + ScriptText(o.script) + " ends " + BoardText(o.st)
                                   + "; menu entry #" + std::to_string(it->second.second) + " ends "
                                   + BoardText(*mv.boards[it->second.second]));
            return v;
        }
    }
    // The menu reaches this board but with LESS of something, and nothing more: that is a payment dominating a menu
    // entry's -- the same finding, and the same class, as a DOMINATED-* on the entry itself (an extra untapped land
    // stays DOMINATED-LAND, counted, not flagged).
    for (auto it = range.first; it != range.second; ++it)
    {
        if (!Covers(hr, it->second, nullptr)) { continue; }
        std::string what;
        v.cls    = DominanceClass(hr, it->second, pre, &what);
        v.detail = what + " -- a mana gate kept this plan off the menu, and paying it with " + ScriptText(o.script)
                 + " reaches a menu entry's board with more";
        return v;
    }
    std::string order;
    if (reordered)
    {
        for (const Action& a : t.actions)
        { if (a.kind == Action::Kind::CastFromHand) { order += (order.empty() ? "" : " > ") + a.card_name.str(); } }
    }
    v.cls    = C_ENUM_GAP;
    v.detail = "a mana gate kept this plan off the menu, but " + (order.empty() ? std::string("it") : "casting " + order)
             + " pays every action with " + ScriptText(o.script)
             + (range.first != range.second
                    ? " (the menu reaches this board only with fewer resources: " + MoreText(hr, range.first->second) + ")"
                    : "");
    return v;
}

// Is the gate-rejected plan `p` an option the menu lacks? Each order of it is realised, every payment, until one pays
// every action; that realisation is then judged (JudgeHit). A paying realisation that is no realisation of `p` itself
// does not stop the search: one that skipped an action of the plan (PerformedPlan) is the plan without it -- on the
// menu, or a plan of the observe-mode enumeration checked on its own; one that tapped a land a continuation played
// (TapsContinuationLand) is the plan with that land drop, checked on its own too; one that tapped a card it took off
// the top of the library (TapsHiddenCard) is clairvoyant, and reported as HIDDEN-INFO when nothing else pays.
GapVerdict EnumGapPlan(const GameState& state, bool pre, const TurnSolver::Plan& p, const MenuView& mv)
{
    GapVerdict v;
    // Combo machinery the oracle does not drive: a combo-route macro, or a LOOP-COUNT variant (a chosen_x on a spell
    // with no {X} -- AccomplishedAlchemist's Galvanic Alchemist untap loops carry their iteration count there,
    // x1000000 meaning "until done").
    for (const Action& a : p.actions)
    {
        const bool combo = a.kind == Action::Kind::ComboRoute
                        || (a.chosen_x > 0 && a.def != nullptr && !a.def->card.m_mana_cost.has_x
                            && a.def->card.m_mana_cost.x_pips == 0);
        if (combo) { NoteUnmodeled("gate-rejected loop / combo variant (not driven)"); return v; }
    }
    const std::multiset<std::string> want = PlanHandCards(p);
    int n_casts = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n_casts; } }
    NoSacActionScope _no_sac_route;
    GapVerdict hidden;   // the first clairvoyant realisation that would otherwise have been reported
    std::vector<TurnSolver::Plan> tries{ p };
    for (const TurnSolver::Plan& alt : OtherOrders(p)) { tries.push_back(alt); }
    for (std::size_t ti = 0; ti < tries.size(); ++ti)
    {
        const TurnSolver::Plan& t = tries[ti];
        // The first full outcome ends this order's search, as it always has -- and when its verdict is nothing to
        // report (the menu reaches it), that is the order's answer. Only an outcome that WOULD be reported is
        // asked the costlier questions (a replay per action), and one that fails them lets the search go on.
        GapVerdict w;
        const OutcomeTest accept = [&](const Outcome& o) -> bool
        {
            w = JudgeHit(state, pre, t, ti > 0, o, want, n_casts, mv);
            if (w.cls == C_N) { return true; }
            const TurnSolver::Plan done = PerformedPlan(state, pre, t, o);
            if (done.actions.size() < t.actions.size())
            {
                const std::string dk = SiblingKey(done);
                if (mv.on_menu.count(dk) != 0 || mv.observed.count(dk) != 0)
                {
                    g_eg_noop.fetch_add(1, std::memory_order_relaxed);
                    EgTrace(state, pre, t, "noop: paying it with " + ScriptText(o.script) + " realises {" + PlanText(done)
                                           + "}, which is " + (mv.on_menu.count(dk) != 0 ? "on the menu" : "enumerated"));
                    return false;
                }
            }
            std::string card, land;
            if (TapsContinuationLand(state, t, o, &card, &land))
            {
                TurnSolver::Plan with_land = t;
                with_land.land_to_play = land;
                const std::string lk = SiblingKey(with_land);
                if (mv.on_menu.count(lk) != 0 || mv.observed.count(lk) != 0)
                {
                    g_eg_cont_land.fetch_add(1, std::memory_order_relaxed);
                    EgTrace(state, pre, t, "cont-land: paying it with " + ScriptText(o.script) + " taps " + card
                                           + ", a land from hand the plan did not play; the plan playing it is "
                                           + (mv.on_menu.count(lk) != 0 ? "on the menu" : "enumerated"));
                    return false;
                }
            }
            if (TapsHiddenCard(state, o, &card))
            {
                g_eg_hidden.fetch_add(1, std::memory_order_relaxed);
                EgTrace(state, pre, t, "hidden: paying it with " + ScriptText(o.script) + " taps " + card);
                if (hidden.cls == C_N)
                {
                    hidden.cls    = C_HIDDEN_INFO;
                    hidden.detail = "the only payments found tap " + card + ", a card the plan took off the top of "
                                    "the library (hidden information, not a gate's to know): " + w.detail;
                }
                return false;
            }
            return true;
        };
        const Realization r = Realize(state, t, pre, /*stop_at_full=*/true, {}, false, nullptr, &accept);
        if (r.hit < 0 || w.cls == C_N) { continue; }   // another order may reach a board the menu does not
        if (w.cls == C_ENUM_GAP) { g_rb[RB_OFF_MENU].fetch_add(1, std::memory_order_relaxed); }
        return w;
    }
    return hidden;
}
} // namespace

bool CoversEngineOutcome(const GameState& state, bool pre, const TurnSolver::Plan& plan)
{
    RevealLogPause pause;
    const EngineReal er = EngineApply(state, pre, plan);
    if (!er.Full()) { return false; }
    const Realization r = Realize(state, plan, pre, /*stop_at_full=*/false);
    const std::string e_line = LineKey(state, er.st);
    const std::uint64_t ek = CoreKey(er.st);
    const Res eres = Resources(er.st, /*with_float=*/false);
    for (const Outcome& o : r.outcomes)
    {
        if (!o.Full() || o.line != e_line || CoreKey(o.st) != ek) { continue; }
        if (Covers(Resources(o.st, /*with_float=*/false), eres, nullptr)) { return true; }
    }
    return false;
}

// ---- THE EXECUTOR'S PAYMENT SCRIPT (ManaOracle.h) -------------------------------------------------------
}   // namespace manaoracle
// TurnSolver.cpp (next to SimulateOneCombat, sharing its pre-declaration body CombatPreDeclare): the rollout combat's
// DECLARATION alone, on `state` (mutated -- call on a copy); indices into `state` as it stands afterwards.
std::vector<int> RolloutCombatDeclaration(GameState& state);
namespace manaoracle
{
// MTG_REAL_PAY_ATTACK_PIN: real declarations the scored-combat pin changed (AIEngine::DeclareAttackers).
std::atomic<long long> g_real_pay_atk_pin_changed{0};
namespace
{
std::atomic<long long> g_rps_asked{0}, g_rps_dom{0}, g_rps_served{0}, g_rps_abandoned{0}, g_rps_us{0};
struct RpsReport
{
    ~RpsReport()
    {
        static const bool s_on = EnvOn("MTG_PAY_REFINE_STATS");
        if (!s_on) { return; }
        std::fprintf(stderr, "=== REAL PAY SCRIPT: asked=%lld dominating=%lld served-to-end=%lld abandoned=%lld "
                     "atk-pin-changed=%lld ms=%lld ===\n",
                     g_rps_asked.load(), g_rps_dom.load(), g_rps_served.load(), g_rps_abandoned.load(),
                     g_real_pay_atk_pin_changed.load(), g_rps_us.load() / 1000);
    }
} g_rps_report;
}

bool PlanPaymentScript(const GameState& state, const TurnSolver::Plan& plan, bool pre,
                       std::vector<PayOption>& script, std::string* what,
                       std::vector<int>* attack_pin, bool* attack_pin_ok)
{
    if (attack_pin_ok != nullptr) { *attack_pin_ok = false; }
    if (attack_pin != nullptr)    { attack_pin->clear(); }
    if (t_hook != nullptr || state.dmg_events_armed || plan.actions.empty()) { return false; }
    for (const Action& x : plan.actions) { if (x.kind == Action::Kind::ComboRoute) { return false; } }
    g_rps_asked.fetch_add(1, std::memory_order_relaxed);
    struct Clock
    {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~Clock()
        {
            g_rps_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - t0).count(), std::memory_order_relaxed);
        }
    } clock;
    RevealLogPause quiet;
    const int a = state.active_player_index;
    const EngineReal er = EngineApply(state, pre, plan);
    // A line that draws, reveals or casts what the plan did not name continues on cards nobody knew when it
    // was paid for: its later payments are not the oracle's to fix (the real continuation is searched).
    if (er.st.players[a].library.size() != state.players[a].library.size()) { return false; }
    {
        std::multiset<std::string> named = PlanHandCards(plan);
        if (plan.land_decided && !plan.land_to_play.empty()) { named.insert(plan.land_to_play); }
        if (!Includes(named, LeftHand(state, er.st))) { return false; }
    }
    const bool full = er.Full();
    const bool spent = er.st.players[a].life < state.players[a].life
                    || er.st.players[a].energy_counters < state.players[a].energy_counters;
    if (full && !spent && AllFloat(er.st) == 0) { return false; }
    struct CapScope
    {
        int saved;
        explicit CapScope(int c) : saved(t_node_cap) { t_node_cap = c; }
        ~CapScope() { t_node_cap = saved; }
    };
    static const int s_cap = std::max(1, EnvInt("MTG_REAL_PAY_NODES", 600));
    // Run the script on the REAL (unmasked) board, one apply: what it reaches, if it reaches the end.
    auto replay = [&](const std::vector<PayOption>& sc, Outcome& out) -> bool
    {
        CapScope one(1);
        Realization rr = Realize(state, plan, pre, /*stop_at_full=*/false, sc);
        if (rr.outcomes.size() != 1) { return false; }
        out = std::move(rr.outcomes.front());
        return out.Clean() && out.unscripted.empty();
    };
    // A DROPPED action is not this script's to pay. The search scored the plan by exactly this apply -- the cast it
    // drops included -- so paying it plays a line nobody evaluated. The A/B said so plainly (seed 1130001, 39
    // decks, 20,910 paired games): 17 slower, 0 faster, 15 of them this branch -- Preordain cast beside Izzet
    // Signet (NivMizzetCombo gi214 T4 -> T5), Dragonlord Kolaghan after Pyretic Ritual (Dragonstorm gi234 T5 ->
    // T7), Predatory Sliver's replicate traded for Aether Vial (slivers gi27 T4 -> T5). A drop is the search's
    // apply to fix, where the evaluation sees it.
    if (!full) { return false; }
    // Every action paid: a realisation that DOMINATES the engine's, out of the sources the engine's line tapped
    // (a dominating one can tap nothing else -- see RefinePayment). The table is narrowed by masking every source
    // the line left untapped; each candidate is then replayed on the real board and compared there.
    GameState masked = state;
    for (std::size_t i = 0; i < masked.battlefield.size() && i < er.st.battlefield.size(); ++i)
    {
        Permanent& p = masked.battlefield[i];
        if (p.controller_index != a || p.tapped || er.st.battlefield[i].card.m_number != p.card.m_number) { continue; }
        if (!er.st.battlefield[i].tapped && IsManaSourceNow(state, p)) { p.tapped = true; }
    }
    Realization r;
    {
        CapScope cap(s_cap);
        r = Realize(masked, plan, pre, /*stop_at_full=*/false);
    }
    const std::string   e_line = LineKey(state, er.st);
    const std::uint64_t ek     = CoreKey(er.st);
    const Res           eres   = Resources(er.st, /*with_float=*/false);
    bool found = false;
    int  best_untapped = -1, best_life = 0, best_energy = 0;
    for (const Outcome& o : r.outcomes)
    {
        if (!o.Clean() || !o.unscripted.empty()) { continue; }
        Outcome real;
        if (!replay(o.script, real) || real.line != e_line || CoreKey(real.st) != ek) { continue; }
        bool strict = false;
        if (!Covers(Resources(real.st, /*with_float=*/false), eres, &strict) || !strict) { continue; }
        int untapped = 0;
        for (const Permanent& p : real.st.battlefield) { if (p.controller_index == a && !p.tapped) { ++untapped; } }
        const int life = real.st.players[a].life, energy = real.st.players[a].energy_counters;
        if (!found || untapped > best_untapped || (untapped == best_untapped
            && (life > best_life || (life == best_life && energy > best_energy))))
        {
            found = true; best_untapped = untapped; best_life = life; best_energy = energy;
            script = real.script;
        }
    }
    if (found && what != nullptr) { *what = "dominates the engine's payment"; }
    if (found) { g_rps_dom.fetch_add(1, std::memory_order_relaxed); }
    // THE SCORED COMBAT (MTG_REAL_PAY_ATTACK_PIN). The search priced this line on er.st -- the engine's own taps --
    // and its combat declared the attackers the provider chose THERE. Declared on the script's board instead, an
    // attack rule that reads untapped mana sees the sources the script saved and can hold a creature the scored
    // line swung (FiveColour s1130288 gi287 T5: Breeding Pool left untapped -> 2 mana -> Bloom Tender {1}{G} is
    // castable -> Deathrite Shaman held; the line scored at 8 life realised 9 and the T6 kill slipped to T7). Read
    // under the plan's own searched dork-combat variant (Plan::atk_dork_release), as the search's combat was.
    if (found && pre && attack_pin != nullptr)
    {
        const int saved_ov = g_dork_atk_override;
        const int rel = plan.atk_dork_release;
        if (DorkAtkSearchEnabled() && rel >= 1)
        { g_dork_atk_override = (rel == 1) ? 1 : (rel == 2 ? 0 : (rel == 3 ? 3 : -1)); }
        // One entry per creature we control on the scored board: +number = it attacked there, -number = it did
        // not. A body that is not listed (one a beginning-of-combat effect creates) is left to the heuristic.
        // Declared the way the search's combat declares (TurnSolver RolloutCombatDeclaration: the pool empties first
        // -- a float the engine's payment left is gone by the declaration, and the attack rule must not count it).
        bool ok = er.st.pending_extra_combats == 0;   // an extra combat runs first: the boards part after it
        GameState cb = er.st;
        const std::vector<int> atk = ok ? ::RolloutCombatDeclaration(cb) : std::vector<int>{};
        for (int i = 0; ok && i < static_cast<int>(cb.battlefield.size()); ++i)
        {
            const Permanent& q = cb.battlefield[static_cast<std::size_t>(i)];
            if (q.controller_index != a) { continue; }
            const bool attacked = std::find(atk.begin(), atk.end(), i) != atk.end();
            if (!attacked && !q.card.IsCreature() && !q.is_animated) { continue; }
            if (q.card.m_number == 0) { ok = false; break; }   // an unnumbered body cannot be re-found
            attack_pin->push_back(attacked ? q.card.m_number : -q.card.m_number);
        }
        g_dork_atk_override = saved_ov;
        static const bool s_rpat = EnvOn("MTG_REAL_PAY_ATK_TRACE");   // DIG TRACE (default off)
        if (s_rpat)
        {
            std::string eb, rb;
            for (const Permanent& q : er.st.battlefield)
            { if (q.controller_index == a) { eb += std::to_string(q.card.m_number) + (q.tapped ? "T " : " "); } }
            Outcome real;
            if (replay(script, real))
            {
                for (const Permanent& q : real.st.battlefield)
                { if (q.controller_index == a) { rb += std::to_string(q.card.m_number) + (q.tapped ? "T " : " "); } }
            }
            std::fprintf(stderr, "[rp-atk] engine-apply board: %s| script board: %s| opp %d\n", eb.c_str(), rb.c_str(),
                         er.st.players[1 - a].life);
        }
        if (!ok) { attack_pin->clear(); }
        if (attack_pin_ok != nullptr) { *attack_pin_ok = ok; }
    }
    return found;
}

RealPayScope::RealPayScope(GameState& s, std::vector<PayOption> script)
{
    h.target = &s;
    h.script = std::move(script);
    h.safe   = true;
    saved    = t_hook;
    t_hook   = &h;
}

RealPayScope::~RealPayScope()
{
    t_hook = saved;
    if (h.abandoned) { g_rps_abandoned.fetch_add(1, std::memory_order_relaxed); }
    else if (h.next >= h.script.size()) { g_rps_served.fetch_add(1, std::memory_order_relaxed); }
}

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
    // every payment): one that pays in full is an option the search never had (EnumGapPlan).
    std::vector<TurnSolver::Plan> observed;
    {
        t_gate_observe = true;
        observed = TurnSolver::EnumerateMainPlans(state, is_pre_combat);
        t_gate_observe = false;
    }
    // The OPTIONS the menu really offers are outcomes, not action lists: a gate-rejected plan whose
    // payable outcome is a board some menu entry already reaches (Ghalta, Stampede Tyrant cast alone
    // puts the Hulk and the Craterhoof it would otherwise have cast) is no missing option -- if it is
    // reached with at least the resources (life, energy, haste, sources left untapped): the same board
    // with less is still an option the menu does not have.
    const MenuView mv = MakeMenuView(keys, real, observed);
    std::set<std::string> done;
    int checked = 0;
    for (const TurnSolver::Plan& p : observed)
    {
        const std::string k = SiblingKey(p);
        if (mv.on_menu.count(k) != 0 || !done.insert(k).second) { continue; }
        g_gate_rejects.fetch_add(1, std::memory_order_relaxed);
        if (MaxGateChecks() > 0 && checked >= MaxGateChecks())
        { g_gate_rejects_skipped.fetch_add(1, std::memory_order_relaxed); continue; }
        ++checked;
        g_gate_rejects_checked.fetch_add(1, std::memory_order_relaxed);
        const GapVerdict v = EnumGapPlan(state, is_pre_combat, p, mv);
        if (v.cls != C_N) { Emit(v.cls, state, is_pre_combat, p, v.detail); }
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

std::string EnumGapVerdict(const GameState& state, bool pre, const TurnSolver::Plan& plan,
                           const std::vector<TurnSolver::Plan>& menu, const std::vector<TurnSolver::Plan>* observed,
                           std::string* detail)
{
    RevealLogPause pause;
    std::vector<EngineReal> real;
    std::vector<std::string> keys;
    real.reserve(menu.size());
    for (const TurnSolver::Plan& m : menu)
    {
        real.push_back(EngineApply(state, pre, m));
        keys.push_back(SiblingKey(m));
    }
    std::vector<TurnSolver::Plan> obs;
    if (observed != nullptr) { obs = *observed; }
    obs.push_back(plan);
    const MenuView mv = MakeMenuView(keys, real, obs);
    const GapVerdict v = EnumGapPlan(state, pre, plan, mv);
    if (detail != nullptr) { *detail = v.detail; }
    return v.cls == C_N ? std::string() : std::string(kClsName[v.cls]);
}
} // namespace manaoracle
