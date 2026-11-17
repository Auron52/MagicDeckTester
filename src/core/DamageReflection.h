#pragma once
// ---- DAMAGE REFLECTION: the unified creature-damage EVENT pipeline (Stuffy Doll deck) ----------------
//
// Design: docs/design/damage-reflection-pipeline.md (sections 2-3). INCLUDED ONLY FROM SpellEffects.h,
// late enough to see LethalToughness, CounterPingSbaDead, OnCreatureDies, FireOppCreatureDies,
// SweepOrphanedAuras, RecordNoncombatDamageToPlayer and EmitPlayEvent.
//
// WHY THIS EXISTS. "Whenever this creature is dealt damage, it deals that much damage to ..." (Stuffy
// Doll, Mogg Maniac, Boros Reckoner, Screaming Nemesis, Spiteful Sliver) makes "a creature was dealt
// damage" an EVENT that triggers things. The engine had no such event: about fifteen inline sites mark
// creature damage and kill creatures, each its own way (design section 1.1). This is that event, ONCE,
// as one function both worlds call, so executor and rollout cannot drift.
//
// ONE CALL = ONE DAMAGE EVENT (CR 120.3; the Reckoner / Nemesis / Sliver rulings: "if it is dealt damage
// by multiple sources at once, its ability triggers once"). DealDamageBatch runs, in order:
//   1. replacement (en-Kor redirection shields -- design step 8; empty until then);
//   2. aggregation per recipient (13 + 13 redirected onto one creature = ONE "dealt 26");
//   3. the trigger SNAPSHOT, taken before anything changes (last-known information, CR 603.10a: a
//      reflector killed by this very event still triggers);
//   4. apply: creature damage is marked, player damage goes through ApplyDamageToPlayer;
//   5. SbaPass -- the one shared state-based-action pass (CounterPingSbaDead, controller's graveyard,
//      Equipment detach, Aura links cleared and SweepOrphanedAuras run inline, OnCreatureDies,
//      FireOppCreatureDies, then the life SBA);
//   6. game over? Then nothing goes on the stack (the Stuffy Doll ruling: a player at 0 loses before
//      the reflection trigger is put on the stack);
//   7. push the snapshot's triggers onto an explicit LIFO (the RunDrawPingStack shape, not recursion);
//   8. the TOP-LEVEL call resolves that stack: each popped trigger builds a one-assign batch and calls
//      DealDamageBatch again, which pushes its own new triggers on top -- depth-first, exactly as the
//      real stack would resolve them.
//
// TERMINATION (design section 3). kResolutionCap trigger resolutions per top-level event, plus the
// no-progress rule: kNoProgressLimit consecutive resolutions that change no player's life and kill no
// creature stop the stack. A loop that can kill removes >= 1 life per <= 2 resolutions, so neither
// limit can cut a reachable win (life <= 30 in 2HG).
//
// GATING. Reached only through routed sites that test GameState::reflect_armed first (a deck constant,
// GoldFishRunner::StampDeckTraits). Every other deck keeps its legacy bodies byte-for-byte.
//
// DISCLOSED LIMITS (none is on this deck's paths): damage-source keywords (lifelink, deathtouch, infect,
// wither) are not modelled -- no source in the pool has one; on a board that ALSO arms dmgev
// (Tamanoa) a noncreature sweeper's CREATURE damage does not raise Tamanoa (player damage does, through
// dmgev::DealDamageEvent); an animated land is not dealt sweep damage (the legacy sweep's and the
// executor SBA's own `card.IsCreature()` predicate, kept so both worlds keep agreeing).
#include "EnvFlags.h"
#include "GameState.h"
#include "GameLogger.h"
#include "../cards/CardDatabase.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dmgrx
{

// MTG_RFX_VERIFY=1 (diagnostic, default OFF; the MTG_DMG_EVENT_VERIFY style): assert at every decision
// boundary (the dmgev backstop sites) that no pipeline frame is open and no trigger is pending. A hit
// means a damage event leaked past the point both worlds treat as quiescent.
inline bool VerifyOn() { static const bool v = EnvOn("MTG_RFX_VERIFY"); return v; }

constexpr int kResolutionCap   = 1 << 16;   // trigger resolutions per top-level event
constexpr int kNoProgressLimit = 64;        // consecutive resolutions with no life change and no death

// A damage SOURCE, captured BY VALUE at event time (last-known information): a reflector that died in
// the event that raised its trigger still deals its damage as the creature it last was.
struct SourceRef
{
    Card card;
    int  controller  = 0;
    bool is_creature = false;
};

// A damage RECIPIENT: a player (id = player index) or a permanent (id = its Card::m_number, a stable id
// that survives other permanents leaving the battlefield mid-cascade).
struct Recipient
{
    bool player = false;
    int  id     = 0;
};

struct DmgAssign
{
    int       src    = 0;       // index into Batch::src
    Recipient to;
    int       amount = 0;
    bool      combat = false;   // redirected damage keeps it (en-Kor ruling); a trigger's never is
    bool      lock_life = false;   // Screaming Nemesis: a player dealt damage this way can't gain life
};

// ONE simultaneous damage event: its sources and its assignments.
struct Batch
{
    std::vector<SourceRef> src;
    std::vector<DmgAssign> hits;
};

// A trigger raised by an event and not yet resolved. Everything is held by value (LKI).
struct PendingTrig
{
    CardParams::ReflectTo kind = CardParams::ReflectTo::None;
    SourceRef src;              // the reflector as it last was
    int       self_number = 0;  // its m_number ("any OTHER target" excludes it; may be gone already)
    int       amount      = 0;  // "that much damage"
    bool      lock_life   = false;
};

struct BatchResult
{
    int creatures_hit = 0;   // distinct creatures dealt damage
    int deaths        = 0;   // creatures this event's own SBA pass put into a graveyard
};

// The open pipeline frame (one per thread). A NESTED DealDamageBatch on the same state (a trigger's
// resolution) only pushes its new triggers; the top-level call owns the drain. A call on a DIFFERENT
// state while a frame is open (no current path does this) runs as its own top level, saving the outer.
struct Frame
{
    GameState*               state = nullptr;
    int                      depth = 0;
    int                      deaths = 0;   // monotone death counter for the no-progress rule
    std::vector<PendingTrig> stack;
};
inline thread_local Frame t_frame;

inline const CardDefinition* Def(const Permanent& p)
{ return p.def_absent ? nullptr : CardDatabase::Instance().LookupCached(p.card); }

inline void Log(const GameState& s, const char* kind, const std::string& text)
{
    if (g_play_event_sink != nullptr && !TapSpeculatingNow()) { EmitPlayEvent(s.turn_number, kind, text); }
}

// Is the game over for the purposes of putting / resolving triggers? The opponent has lost (life,
// poison or decking), or -- on a deck where our own life can lose (own_death_live) -- we have.
inline bool GameOver(const GameState& s)
{
    if (dmgev::OpponentLostRaw(s)) { return true; }
    return s.own_death_live && s.ActivePlayer().life <= 0;
}

// Damage to a PLAYER. On a dmgev-armed board (Purity / Tamanoa / Manabarbs decks) it is that model's
// event (prevention, the won-lock, Tamanoa for a noncreature source); otherwise the raw legacy shape:
// life down, the opponent-lost-life bit (spectacle), and the noncombat-damage record (Chandra's
// Incinerator) for noncombat damage. Returns the damage actually dealt.
inline int ApplyDamageToPlayer(GameState& s, const SourceRef& src, int player, int n, bool combat)
{
    if (n <= 0 || player < 0 || player > 1) { return 0; }
    if (s.dmg_events_armed)
    {
        const int to_self = (player == src.controller) ? n : 0;
        const int to_opp  = (player != src.controller) ? n : 0;
        const std::string nm = src.card.m_name.str();
        return dmgev::DealDamageEvent(s, src.controller, src.is_creature, combat, to_self, to_opp, 0,
                                      nm.c_str());
    }
    // The won-lock (DamageEvents.h point 4): once the opponent has lost, damage to us is a no-op.
    if (s.own_death_live && player == s.active_player_index && dmgev::WonLocked(s)) { return 0; }
    s.players[player].life -= n;
    if (player != s.active_player_index) { s.opponent_lost_life_this_turn = true; }
    if (!combat) { RecordNoncombatDamageToPlayer(s, src.controller, player, n); }
    return n;
}

// THE shared state-based-action pass for every pipeline step, in BOTH worlds (the rollout runs no SBA
// pass of its own, and the executor's GameEngine::CheckStateBasedActions afterwards finds nothing to
// do). Mirrors that executor pass -- the CounterPingSbaDead predicate (toughness <= 0 kills through
// indestructible, lethal damage does not), Equipment detach, OnCreatureDies after the erase loop,
// repeated until nothing dies -- with one deliberate difference: the card goes to its OWNER's graveyard
// (CR 400.3: an object that would go to a graveyard other than its owner's goes to its owner's), as the
// legacy sweep already did; the executor pass's controller's-graveyard push is a pre-existing deviation
// that only a stolen creature can observe (tokens included either way). Plus what the
// executor leaves to later checkpoints but this pipeline needs NOW: Aura links cleared and
// SweepOrphanedAuras run inline (an Aura whose host died is in the graveyard before any trigger
// resolves), FireOppCreatureDies for an opponent's creature (the legacy sweep's clause), and the life
// SBA (dmgev::SbaCheckpoint, own_death_live decks only -- it parks our life at kLostLife).
// Returns the number of creatures that died.
inline int SbaPass(GameState& s)
{
    struct Dead { Card card; int controller; bool was_token; int minus_counters; int total_counters; };
    int total = 0;
    for (;;)
    {
        std::vector<Dead> died;
        for (std::size_t i = 0; i < s.battlefield.size(); )
        {
            const Permanent& q = s.battlefield[i];
            if (!CounterPingSbaDead(q, s)) { ++i; continue; }
            died.push_back({ q.card, q.controller_index, q.is_token, MinusCountersOn(q), TotalCountersOn(q) });
            s.players[q.owner_index].graveyard.push_back(RevealedCard(q.card));
            s.battlefield.erase(s.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (died.empty()) { break; }
        for (const Dead& d : died)
        {
            for (Permanent& e : s.battlefield)
            {
                if (e.equipped_to      == d.card.m_number) { e.equipped_to      = 0; }   // CR 301.5c
                if (e.aura_attached_to == d.card.m_number) { e.aura_attached_to = 0; }
            }
        }
        SweepOrphanedAuras(s);   // CR 704.5m, inline: simultaneous with the deaths that orphaned them
        for (const Dead& d : died)
        {
            Log(s, "death", "\xE2\x98\xA0 " + d.card.m_name.str() + " dies (lethal damage)");
            OnCreatureDies(s, d.controller, d.card, d.was_token, d.minus_counters, nullptr, d.total_counters);
            if (d.controller != s.active_player_index) { FireOppCreatureDies(s, d.controller); }
        }
        total += static_cast<int>(died.size());
    }
    if (s.own_death_live) { dmgev::SbaCheckpoint(s); }
    return total;
}

inline Permanent* FindPerm(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

// Resolve ONE pending trigger (design step 8). Trigger kinds arrive with the cards (design step 2).
inline void ResolveTrig(GameState& s, const PendingTrig& t);

// Steps 1-7 for one batch. Pushes the triggers it raised onto t_frame.stack.
inline BatchResult ApplyOne(GameState& s, const Batch& b)
{
    BatchResult r;
    // 1. Replacement (en-Kor redirection shields): design step 8. Nothing replaces damage yet.

    // 2. Aggregate per recipient. Creature recipients keep first-appearance order; a recipient that
    //    is no longer a creature on the battlefield is dropped (damage to "that creature" does nothing).
    struct CreatureTotal { int number; int amount; };
    struct PlayerTotal   { int src; int player; int amount; bool combat; bool lock_life; };
    std::vector<CreatureTotal> ct;
    std::vector<PlayerTotal>   pt;
    for (const DmgAssign& a : b.hits)
    {
        if (a.amount <= 0) { continue; }
        if (a.to.player)
        {
            bool merged = false;
            for (PlayerTotal& p : pt)
            {
                if (p.src == a.src && p.player == a.to.id && p.combat == a.combat && p.lock_life == a.lock_life)
                { p.amount += a.amount; merged = true; break; }
            }
            if (!merged) { pt.push_back({ a.src, a.to.id, a.amount, a.combat, a.lock_life }); }
            continue;
        }
        const Permanent* q = FindPerm(s, a.to.id);
        if (q == nullptr || !q->card.IsCreature()) { continue; }
        bool merged = false;
        for (CreatureTotal& c : ct) { if (c.number == a.to.id) { c.amount += a.amount; merged = true; break; } }
        if (!merged) { ct.push_back({ a.to.id, a.amount }); }
    }

    // 3. Snapshot the triggers (LKI), before anything changes, in BATTLEFIELD order (the canonical
    //    order, design 4.7): one reflection trigger per damaged reflector for the event's total
    //    (rulings: "triggers once ... that much damage"), raised even when the damage is lethal.
    std::vector<PendingTrig> raised;
    if (!ct.empty())
    {
        for (const Permanent& q : s.battlefield)
        {
            if (!q.card.IsCreature()) { continue; }
            int amt = 0;
            for (const CreatureTotal& c : ct) { if (c.number == q.card.m_number) { amt = c.amount; break; } }
            if (amt <= 0) { continue; }
            const CardDefinition* d = Def(q);
            if (d == nullptr || d->params.reflect_dealt_damage == CardParams::ReflectTo::None) { continue; }
            raised.push_back({ d->params.reflect_dealt_damage, SourceRef{ q.card, q.controller_index, true },
                               q.card.m_number, amt, d->params.reflect_player_cant_gain_life });
        }
    }

    // 4. Apply.
    for (const CreatureTotal& c : ct)
    {
        Permanent* q = FindPerm(s, c.number);
        if (q == nullptr) { continue; }
        q->damage += c.amount;
        ++r.creatures_hit;
    }
    for (const PlayerTotal& p : pt)
    {
        const int dealt = ApplyDamageToPlayer(s, b.src[static_cast<std::size_t>(p.src)], p.player, p.amount,
                                              p.combat);
        // Screaming Nemesis: "If a player is dealt damage this way, they can't gain life for the rest
        // of the game" -- only damage actually DEALT (a prevented hit sets nothing).
        if (p.lock_life && dealt > 0)
        { s.cant_gain_life_rest_of_game_mask = static_cast<std::uint8_t>(s.cant_gain_life_rest_of_game_mask | (1u << p.player)); }
    }

    // 5. State-based actions.
    r.deaths = SbaPass(s);
    t_frame.deaths += r.deaths;

    // 6. Game over -> nothing goes on the stack, and nothing already on it resolves.
    if (GameOver(s)) { t_frame.stack.clear(); return r; }

    // 7. Push in reverse, so the FIRST raised trigger is on top and resolves first (the canonical
    //    order: battlefield order of the reflectors -- design section 4.7).
    for (std::size_t i = raised.size(); i-- > 0; ) { t_frame.stack.push_back(std::move(raised[i])); }
    return r;
}

// Step 8: resolve the stack the top-level event raised, depth-first, under the cap and the
// no-progress rule (design section 3).
inline void Drain(GameState& s, std::size_t base)
{
    int resolutions = 0;
    int idle = 0;
    while (t_frame.stack.size() > base)
    {
        if (GameOver(s)) { t_frame.stack.resize(base); break; }
        if (++resolutions > kResolutionCap)
        {
            Log(s, "trigger", "rfx-cap: trigger resolution cap reached -- the remaining triggers are dropped");
            t_frame.stack.resize(base);
            break;
        }
        const PendingTrig t = std::move(t_frame.stack.back());
        t_frame.stack.pop_back();
        const int life0 = s.players[0].life, life1 = s.players[1].life, deaths0 = t_frame.deaths;
        ResolveTrig(s, t);
        const bool progress = s.players[0].life != life0 || s.players[1].life != life1
                           || t_frame.deaths != deaths0;
        idle = progress ? 0 : idle + 1;
        if (idle >= kNoProgressLimit)
        {
            Log(s, "trigger", "rfx-noprogress: no life changed and no creature died for "
                              + std::to_string(kNoProgressLimit) + " resolutions -- the loop stops");
            t_frame.stack.resize(base);
            break;
        }
    }
}

// THE entry point: one damage event. See the header note for the steps.
inline BatchResult DealDamageBatch(GameState& s, const Batch& b)
{
    if (t_frame.depth > 0 && t_frame.state == &s)
    {
        // Nested (a trigger resolving inside an open event on this state): apply and push only; the
        // top-level call's Drain resolves what this raised, on top of the stack.
        ++t_frame.depth;
        const BatchResult r = ApplyOne(s, b);
        --t_frame.depth;
        return r;
    }
    Frame saved;
    std::swap(saved, t_frame);   // a different state's open frame (none today) is restored below
    t_frame.state = &s;
    t_frame.depth = 1;
    const BatchResult r = ApplyOne(s, b);
    Drain(s, 0);
    std::swap(saved, t_frame);
    return r;
}

// A symmetric "N damage to each creature" sweep (Pyroclasm's damage_all_creatures; Blasphemous Act,
// Chain Reaction) as ONE event from the spell (a noncreature source). The armed body of
// PerformDamageAllCreatures, which keeps its legacy body for every unarmed deck.
inline void Sweep(GameState& s, int controller, const CardDefinition& def, int dmg)
{
    if (dmg <= 0) { return; }
    Batch b;
    b.src.push_back({ def.card, controller, def.card.IsCreature() });
    for (const Permanent& q : s.battlefield)
    {
        if (!q.card.IsCreature()) { continue; }
        b.hits.push_back({ 0, { false, q.card.m_number }, dmg, false, false });
    }
    const BatchResult r = DealDamageBatch(s, b);
    if (r.creatures_hit > 0 && g_play_event_sink)   // the legacy sweep's event, same text
    {
        EmitPlayEvent(s.turn_number, "damage",
                      "\xE2\x9B\x88 " + def.card.m_name.str() + ": " + std::to_string(dmg)
                      + " to each creature (" + std::to_string(r.creatures_hit) + " hit, "
                      + std::to_string(r.deaths) + " died)");
    }
}

// MTG_RFX_VERIFY backstop, beside the dmgev decision-boundary backstops: nothing may be in flight.
inline void VerifyQuiescent(const GameState& s, const char* site)
{
    if (!s.reflect_armed || !VerifyOn()) { return; }
    if (t_frame.depth != 0 || !t_frame.stack.empty())
    {
        std::fprintf(stderr, "[rfx-verify] pipeline NOT quiescent at %s (turn %d): depth=%d pending=%zu\n",
                     site, s.turn_number, t_frame.depth, t_frame.stack.size());
        std::abort();
    }
}

// Stable target ids: a player p is -1 - p; a permanent is its Card::m_number (always > 0 on a board).
inline int PlayerId(int p) { return -1 - p; }

// F1 (SOUND engine fold, design 4.1): our own face is dominated by the opponent's while nothing rewards
// damage to us. Proof: our life is read DOWNWARD by nothing in the engine (every own-life reader is a
// floor -- etb_life_floor, activate_min_life, the land-search life cost, pay-life untaps, the
// start-life tap restriction -- so less life is never better), it matters only through dying, and the
// same N to the opponent is monotone good in the goldfish. The two cards that DO reward damage to us
// break the proof and keep the option: Purity (our noncombat damage becomes lifegain) and Tamanoa for
// a NONCREATURE source (every reflector is a creature, so that clause is future-proofing). Off under
// human play and MTG_UNPRUNE=reflecttarget, which see every legal target.
inline bool OwnFaceDominated(const GameState& s, const PendingTrig& t)
{
    if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::ReflectTarget)) { return false; }
    const int ctrl = t.src.controller;
    if (dmgev::PurityProtects(s, ctrl)) { return false; }
    if (!t.src.is_creature && dmgev::CountTamanoa(s, ctrl) > 0) { return false; }
    return true;
}

// The trigger's legal targets at resolution, opponent's face FIRST (the rollout's permitted greedy
// takes the front). Planeswalkers are not offered (disclosed PARTIAL on the cards: the goldfish
// opponent never controls one, and damage to our own only removes loyalty).
inline std::vector<int> LegalReflectTargets(const GameState& s, const PendingTrig& t)
{
    using RT = CardParams::ReflectTo;
    const int ctrl = t.src.controller, opp = 1 - ctrl;
    std::vector<int> legal{ PlayerId(opp) };
    if (t.kind == RT::TargetOpponent) { return legal; }
    if (!OwnFaceDominated(s, t)) { legal.push_back(PlayerId(ctrl)); }
    if (t.kind == RT::AnyTarget || t.kind == RT::AnyOtherTarget)
    {
        for (const Permanent& q : s.battlefield)
        {
            if (!q.card.IsCreature()) { continue; }
            if (t.kind == RT::AnyOtherTarget && q.card.m_number == t.self_number) { continue; }
            legal.push_back(q.card.m_number);
        }
    }
    return legal;
}

// ---- THE SEARCHED REFLECTION-TARGET AXIS (design 4.1 / step 3; the Incinerator shape) -------------
// A trigger whose target list (after the engine's sound folds and the provider's restrict/order) holds
// MORE THAN ONE option is a CHOICE. Choices are numbered in resolution order, and the plan carries one
// pin per choice (TurnSolver::Plan::reflect_targets, a stable id: player p = -1 - p, a permanent its
// Card::m_number -- never a rank, since ranks shift as creatures die). A singleton is not a choice and
// takes no ordinal, so a plan whose cascade offers no choice carries no pin.
//
//   * t_pins / t_pin_cursor: the installed plan's pins and the next choice ordinal. ApplyPlanDirect, the
//     executor's committed plan (AIEngine) and a breakpoint continuation (ContPinScope) install them;
//     the breakpoint capture/resume carries the cursor as prefix state.
//   * t_probe: AppendSubdecisionAxes' probe-and-fan. While it applies a plan it is told each choice's
//     list, and clones the plan once per option at the first ordinal the plan does not pin yet. So every
//     plan the search scores inside the window has a pin for every choice its apply reaches: each
//     admitted option is BRANCHED, none is picked.
//   * an ordinal without a (legal) pin takes the list's FRONT. That is reached only where a one-shot
//     policy is permitted -- beyond the horizon (the rollout's greedy playout and the depth-0 runner,
//     whose plans the fan does not see), a speculative apply that is not scoring a line (another axis's
//     probe, this axis's own probe), and the executor if its state drifted from the plan's -- exactly as
//     for Chandra's Incinerator (g_incinerator_pins, SpellEffects.h), the precedent this mirrors.
//   * human play is asked through the viewer's `target` prompt (g_play_target_chooser) with the full
//     legal set (F1 and the provider prune are lifted there); the fan does not run under human play.
inline thread_local const std::vector<int>* t_pins = nullptr;
inline thread_local std::size_t t_pin_cursor = 0;
using TargetProbe = std::function<void(const std::vector<int>&)>;
inline thread_local TargetProbe* t_probe = nullptr;
inline thread_local const std::vector<int>* t_probe_owner = nullptr;
inline thread_local int t_probe_fan_depth = -1;
// The legal set of the choice the viewer is being asked about (stable ids, front = the engine's
// default), read by the human-play target chooser (main.cpp) while it builds the prompt.
inline thread_local const std::vector<int>* t_viewer_legal = nullptr;

// Probe ownership follows the plan VECTOR (IncineratorTargetProbeScope): a nested continuation with its
// own pins searches its own targets and cannot add speculative choices to its parent's fan.
struct TargetProbeScope
{
    TargetProbe* old_probe = t_probe;
    const std::vector<int>* old_owner = t_probe_owner;
    int old_depth = t_probe_fan_depth;
    TargetProbeScope(TargetProbe& probe, const std::vector<int>& owner, int depth)
    { t_probe = &probe; t_probe_owner = &owner; t_probe_fan_depth = depth; }
    ~TargetProbeScope() { t_probe = old_probe; t_probe_owner = old_owner; t_probe_fan_depth = old_depth; }
    TargetProbeScope(const TargetProbeScope&) = delete;
    TargetProbeScope& operator=(const TargetProbeScope&) = delete;
};
struct PinScope
{
    const std::vector<int>* old;
    std::size_t cursor;
    TargetProbe* old_probe;
    const std::vector<int>* old_probe_owner;
    int old_probe_depth;
    explicit PinScope(const std::vector<int>& pins)
        : old(t_pins), cursor(t_pin_cursor), old_probe(t_probe), old_probe_owner(t_probe_owner),
          old_probe_depth(t_probe_fan_depth)
    {
        t_pins = &pins; t_pin_cursor = 0;
        if (t_probe && t_probe_owner != &pins) { t_probe = nullptr; t_probe_owner = nullptr; t_probe_fan_depth = -1; }
    }
    ~PinScope()
    {
        t_pins = old; t_pin_cursor = cursor;
        t_probe = old_probe; t_probe_owner = old_probe_owner; t_probe_fan_depth = old_probe_depth;
    }
    PinScope(const PinScope&) = delete;
    PinScope& operator=(const PinScope&) = delete;
};

// Can resolving / activating `d` deal damage to creatures through this pipeline (and so raise a
// reflection trigger)? EVERY route into DealDamageBatch must be listed here: the fan's per-plan
// pre-check (AppendSubdecisionAxes) skips the probe for a plan none of whose actions passes, which is
// sound only while this list is complete. Today the one route is PerformDamageAllCreatures: the
// damage_all_creatures sweeps (Pyroclasm, Blasphemous Act), Chain Reaction's counted X, Rolling
// Earthquake (x_damage_each_creature_and_player) and Pyrohemia's activation (ping_all_amount).
// (Design steps 4-5 add Stuffy Doll's self-ping, Guilty Conscience and combat: extend it there.)
inline bool DefCanDamageCreatures(const CardDefinition& d)
{
    return d.params.damage_all_creatures > 0 || d.params.damage_all_creatures_count_creatures
        || d.params.x_damage_each_creature_and_player || d.params.ping_all_amount > 0
        || d.params.ping_all_cost.has_value();
}
// Does `d` carry a reflection trigger whose target can be a CHOICE? Mogg Maniac's "target opponent"
// is always the singleton {opponent} (one opponent pool, also in 2HG), so it never needs the fan.
inline bool DefReflectsWithChoice(const CardDefinition& d)
{
    using RT = CardParams::ReflectTo;
    return d.params.reflect_dealt_damage != RT::None && d.params.reflect_dealt_damage != RT::TargetOpponent;
}

inline void ResolveTrig(GameState& s, const PendingTrig& t)
{
    using RT = CardParams::ReflectTo;
    if (t.kind != RT::TargetOpponent && t.kind != RT::AnyTarget && t.kind != RT::AnyOtherTarget
        && t.kind != RT::TargetPlayer)
    {
        std::fprintf(stderr, "[rfx] FATAL: unresolvable trigger kind %d from %s\n",
                     static_cast<int>(t.kind), t.src.card.m_name.str().c_str());
        std::abort();
    }
    const std::vector<int> engine_legal = LegalReflectTargets(s, t);
    std::vector<int> legal = engine_legal;
    if (!HumanPlayActive())   // human play sees every legal target (the provider may not prune it)
    {
        ResolveProvider(s).ReflectTargetCandidates(s, t.self_number, t.src.controller, t.amount,
                                                   static_cast<int>(t.kind), legal);
        // The hook may only RESTRICT or ORDER: drop anything it added, and a mandatory trigger with a
        // legal target cannot be emptied (CR 603.3d) -- an emptied list falls back to the engine's.
        legal.erase(std::remove_if(legal.begin(), legal.end(), [&](int id)
                    { return std::find(engine_legal.begin(), engine_legal.end(), id) == engine_legal.end(); }),
                    legal.end());
        if (legal.empty()) { legal = engine_legal; }
    }
    if (legal.empty()) { return; }   // no legal target: the trigger is removed (CR 603.3d)
    int pick = legal.front();
    if (legal.size() > 1)   // a CHOICE: searched (the pin), probed (the fan), or asked (the viewer)
    {
        const std::size_t ordinal = t_pin_cursor++;
        if (t_probe) { (*t_probe)(legal); }
        if (t_pins && ordinal < t_pins->size())
        {
            const int pin = (*t_pins)[ordinal];
            if (std::find(legal.begin(), legal.end(), pin) != legal.end()) { pick = pin; }
        }
        if (g_play_target_chooser)
        {
            const CardDefinition* def = CardDatabase::Instance().LookupCached(t.src.card);
            if (def)
            {
                auto to_ct = [&](int id) -> ChosenTarget {
                    if (id < 0) { return { 0, -1 - id, 0 }; }
                    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
                    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == id) { return { 1, i, 0 }; } }
                    return { 0, 1 - t.src.controller, 0 };
                };
                const std::vector<int>* saved = t_viewer_legal;
                t_viewer_legal = &legal;
                const auto chosen = (*g_play_target_chooser)(s, *def, t.src.controller, 1, t.amount, { to_ct(pick) });
                t_viewer_legal = saved;
                if (chosen.size() == 1)
                {
                    int id = 0;
                    if (chosen[0].kind == 0 && (chosen[0].index == 0 || chosen[0].index == 1)) { id = PlayerId(chosen[0].index); }
                    else if (chosen[0].kind == 1 && chosen[0].index >= 0
                             && chosen[0].index < static_cast<int>(s.battlefield.size()))
                    { id = s.battlefield[static_cast<std::size_t>(chosen[0].index)].card.m_number; }
                    if (id != 0 && std::find(legal.begin(), legal.end(), id) != legal.end()) { pick = id; }
                }
            }
        }
    }
    Batch b;
    b.src.push_back(t.src);
    DmgAssign a;
    a.src       = 0;
    a.to        = pick < 0 ? Recipient{ true, -1 - pick } : Recipient{ false, pick };
    a.amount    = t.amount;
    a.combat    = false;   // reflection damage is never combat damage (ruling)
    a.lock_life = t.lock_life;
    b.hits.push_back(a);
    if (g_play_event_sink != nullptr && !TapSpeculatingNow())
    {
        std::string to;
        if (pick < 0) { to = (-1 - pick) == t.src.controller ? "you" : "the opponent"; }
        else
        {
            const Permanent* q = FindPerm(s, pick);
            to = q != nullptr ? q->card.m_name.str() : std::string("a creature");
        }
        EmitPlayEvent(s.turn_number, "trigger",
                      "\xE2\x9A\xA1 " + t.src.card.m_name.str() + " was dealt " + std::to_string(t.amount)
                      + " damage -- it deals " + std::to_string(t.amount) + " to " + to);
    }
    DealDamageBatch(s, b);
}

}   // namespace dmgrx
