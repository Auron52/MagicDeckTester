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
#include <cstdio>
#include <cstdlib>
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

    // 3. Snapshot the triggers (LKI), before anything changes. Trigger kinds: design step 2.
    std::vector<PendingTrig> raised;

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
        (void)dealt;   // the Nemesis life lock reads it (design step 2)
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

inline void ResolveTrig(GameState& s, const PendingTrig& t)
{
    (void)s;
    // No trigger kind is raised before design step 2 (the snapshot above pushes nothing), so reaching
    // here is a pipeline defect, not a play decision.
    std::fprintf(stderr, "[rfx] FATAL: unresolvable trigger kind %d from %s\n",
                 static_cast<int>(t.kind), t.src.card.m_name.str().c_str());
    std::abort();
}

}   // namespace dmgrx
