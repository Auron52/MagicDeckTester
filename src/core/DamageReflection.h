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
//   1. replacement (Shaman en-Kor source pulls, Batch::pulls, then en-Kor redirection shields, Batch::shields --
//      CR 614.9 / 616.1; see AddPullRider / AddShieldRider);
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
#include <optional>
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

// An en-Kor redirection shield (Nomads / Shaman en-Kor's {0}, CardParams::redirect_shield): "the next 1
// damage that would be dealt to [on] this turn is dealt to [to] instead". Both are Card::m_number ids.
struct Shield
{
    int on = 0;   // the en-Kor
    int to = 0;   // the target creature we control
};

// A Shaman en-Kor SOURCE PULL (CardParams::redirect_pull_cost, the {1}{W} ability): "the next time a source of
// your choice [src] would deal damage to target creature [from] this turn, that damage is dealt to [on]
// instead". ALL the damage that source deals that creature in the event moves, once (CR 614.9 / 614.5). `on` and
// `from` are Card::m_number ids, `src` an index into Batch::src.
struct Pull
{
    int on   = 0;   // the Shaman en-Kor
    int from = 0;   // the target creature (any controller)
    int src  = 0;   // the source of our choice (our sweeper)
};

// ONE simultaneous damage event: its sources and its assignments.
struct Batch
{
    std::vector<SourceRef> src;
    std::vector<DmgAssign> hits;
    // Redirection shields that apply to THIS event (design 2.2 step 1 / 2.3). Hosted as a rider on our own
    // creature sweeper (AddShieldRider), so every shield is created for, and lives only inside, the event it
    // rides: the allocation never exceeds the damage the en-Kor is about to be dealt, so no shield is ever
    // left over for later damage this turn and no per-turn GameState list is needed (design section 14,
    // step 8). Empty on every other event.
    std::vector<Shield> shields;
    // Source pulls (Shaman en-Kor's {1}{W}), applied BEFORE the shields (see ApplyOne step 1). Same hosting and
    // lifetime as `shields`: made for, paid at, and spent inside the sweep they ride (section 14, step 9).
    std::vector<Pull> pulls;
};

// A trigger raised by an event and not yet resolved. Everything is held by value (LKI).
struct PendingTrig
{
    CardParams::ReflectTo kind = CardParams::ReflectTo::None;
    SourceRef src;              // the reflector as it last was
    int       self_number = 0;  // its m_number ("any OTHER target" excludes it; may be gone already)
    int       amount      = 0;  // "that much damage"
    bool      lock_life   = false;
    int       fixed_player = -1;  // ReflectTo::ChosenPlayer: the Doll's locked player index (LKI), else -1
    int       aura_host    = 0;   // Guilty Conscience (aura_host_damage_back): the host's m_number ("that
                                  // creature"); nonzero marks a GC trigger (kind None, src = the GC by LKI)
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

// How the LAST top-level drain on this thread ended (diagnostic / tests; never read by play).
enum class DrainStop : std::uint8_t { Drained = 0, GameOver, Cap, NoProgress };
struct DrainStats { int resolutions = 0; DrainStop stop = DrainStop::Drained; };
inline thread_local DrainStats t_last_drain;

inline const CardDefinition* Def(const Permanent& p)
{ return p.def_absent ? nullptr : CardDatabase::Instance().LookupCached(p.card); }

// Spiteful Sliver (CardParams::grants_reflect_to_subtype). The granting permanents on the battlefield, in
// battlefield order (empty on every board without one -- the common case, one def lookup per permanent).
inline std::vector<const Permanent*> Granters(const GameState& s)
{
    std::vector<const Permanent*> g;
    for (const Permanent& q : s.battlefield)
    {
        const CardDefinition* d = Def(q);
        if (d != nullptr && d->params.grants_reflect_to_subtype != CardParams::ReflectTo::None) { g.push_back(&q); }
    }
    return g;
}
// Does granter g's "<subtypes_affected> creatures you control have ..." cover creature q? Same controller,
// and q has one of the listed subtypes (an all-types animation counts, the subtype counters' convention).
// The granter itself is covered when it has the subtype ("Sliver creatures you control" includes it).
inline bool GrantCovers(const Permanent& g, const Permanent& q)
{
    if (g.controller_index != q.controller_index || !q.card.IsCreature()) { return false; }
    const CardDefinition* d = Def(g);
    if (d == nullptr) { return false; }
    for (const std::string& sub : d->params.subtypes_affected)
    { if (q.AnimatedAllTypes() || CardHasSubtype(q.card, sub)) { return true; } }
    return false;
}
// How many granted reflection instances creature q carries right now (design 4: mult(T)).
inline int GrantedInstances(const GameState& s, const Permanent& q)
{
    int n = 0;
    for (const Permanent* g : Granters(s)) { if (GrantCovers(*g, q)) { ++n; } }
    return n;
}

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

// Is any Guilty Conscience (aura_host_damage_back) on the battlefield? One cheap scan; false on every
// board without one, so the per-source bookkeeping below costs nothing elsewhere.
inline bool AnyAuraHostDamageBack(const GameState& s)
{
    for (const Permanent& q : s.battlefield)
    {
        if (q.aura_attached_to == 0) { continue; }
        const CardDefinition* d = Def(q);
        if (d != nullptr && d->params.aura_host_damage_back) { return true; }
    }
    return false;
}
// "Whenever enchanted creature deals damage, Guilty Conscience deals that much damage to that creature":
// one trigger per GC attached to `host` (battlefield order), for `amount`, appended to `out`.
inline void RaiseAuraHostDamageBack(const GameState& s, int host, int amount, std::vector<PendingTrig>& out)
{
    for (const Permanent& q : s.battlefield)
    {
        if (q.aura_attached_to != host) { continue; }
        const CardDefinition* d = Def(q);
        if (d == nullptr || !d->params.aura_host_damage_back) { continue; }
        PendingTrig t;
        t.src       = SourceRef{ q.card, q.controller_index, false };
        t.self_number = q.card.m_number;
        t.amount    = amount;
        t.aura_host = host;
        out.push_back(std::move(t));
    }
}

// Steps 1-7 for one batch. Pushes the triggers it raised onto t_frame.stack.
inline BatchResult ApplyOne(GameState& s, const Batch& b)
{
    BatchResult r;
    // 1. Replacement: the en-Kor redirection shields (CR 614.9 redirection effects; the order among them is
    //    ours to choose, CR 616.1, and we apply them in list order). Each shield moves ONE point of damage
    //    from its en-Kor onto its target and is then spent (CR 614.5: a replacement applies once to an event).
    //    The redirected point keeps its SOURCE and its combat flag (en-Kor ruling: redirected combat damage is
    //    still combat damage). A point moved onto a creature that carries its own shields can be moved again
    //    by a DIFFERENT shield (the passes repeat until none applies; each shield is spent at most once, so
    //    this terminates). CR 614.9: if either creature is no longer on the battlefield, or no longer a
    //    creature, when the damage would be redirected, the effect does nothing -- and (Q-R1, USER question,
    //    default) the shield is NOT spent: it simply did not apply, and the damage stays on the en-Kor.
    //
    //    SOURCE PULLS FIRST (Shaman en-Kor's {1}{W}, Batch::pulls). Each moves ALL the damage its source would deal
    //    its target creature onto the Shaman, once (the target may be any creature, either side). ORDER (CR 616.1:
    //    the affected object's controller chooses which applicable replacement applies first; CR 616.1f / 616.2:
    //    after one applies, the ones that NOW apply are considered -- so damage pulled onto the Shaman can then be
    //    moved on by the Shaman's own {0} shields, which is the point of the card). The only overlap is a pull on a
    //    creature that also carries {0} shields: that creature is ours, so the order is ours, and pull-first loses
    //    nothing -- "shields move k, the pull takes the rest" is reached as "no shields, the Shaman's shields move
    //    k" (the shields are free and may target any creature we control). A pull whose Shaman or target is no
    //    longer a creature on the battlefield does nothing (CR 614.9) and is not spent (the Q-R1 default).
    std::vector<DmgAssign> redirected;
    const std::vector<DmgAssign>* hits = &b.hits;
    if (!b.pulls.empty())
    {
        redirected = b.hits;
        for (const Pull& pl : b.pulls)
        {
            const Permanent* e = FindPerm(s, pl.on);
            const Permanent* t = FindPerm(s, pl.from);
            if (e == nullptr || !e->card.IsCreature() || t == nullptr || !t->card.IsCreature() || pl.on == pl.from) { continue; }
            for (DmgAssign& a : redirected)
            {
                if (a.to.player || a.to.id != pl.from || a.src != pl.src || a.amount <= 0) { continue; }
                a.to = Recipient{ false, pl.on };   // keeps its source, amount and combat flag
            }
        }
        hits = &redirected;
    }
    if (!b.shields.empty())
    {
        if (b.pulls.empty()) { redirected = b.hits; }
        std::vector<char> spent(b.shields.size(), 0);
        for (bool moved = true; moved; )
        {
            moved = false;
            for (std::size_t i = 0; i < b.shields.size(); ++i)
            {
                if (spent[i]) { continue; }
                const Shield& sh = b.shields[i];
                const Permanent* e = FindPerm(s, sh.on);
                const Permanent* t = FindPerm(s, sh.to);
                if (e == nullptr || !e->card.IsCreature() || t == nullptr || !t->card.IsCreature()) { continue; }
                std::size_t at = redirected.size();
                for (std::size_t k = 0; k < redirected.size(); ++k)
                {
                    const DmgAssign& a = redirected[k];
                    if (!a.to.player && a.to.id == sh.on && a.amount > 0) { at = k; break; }
                }
                if (at == redirected.size()) { continue; }   // no damage would be dealt to it (yet)
                DmgAssign moved_pt = redirected[at];
                --redirected[at].amount;
                moved_pt.to     = Recipient{ false, sh.to };
                moved_pt.amount = 1;
                redirected.push_back(moved_pt);
                spent[i] = 1;
                moved = true;
            }
        }
        hits = &redirected;
    }

    // 2. Aggregate per recipient. Creature recipients keep first-appearance order; a recipient that
    //    is no longer a creature on the battlefield is dropped (damage to "that creature" does nothing).
    struct CreatureTotal { int number; int amount; };
    struct PlayerTotal   { int src; int player; int amount; bool combat; bool lock_life; };
    std::vector<CreatureTotal> ct;
    std::vector<PlayerTotal>   pt;
    // Guilty Conscience: the damage each SOURCE deals in this event ("deals damage", any recipient), counted
    // only while a GC is on the battlefield and some source is a creature (empty otherwise: nothing to raise).
    std::vector<int> src_dealt;
    if (AnyAuraHostDamageBack(s))
    {
        for (const SourceRef& r0 : b.src) { if (r0.is_creature) { src_dealt.assign(b.src.size(), 0); break; } }
    }
    for (const DmgAssign& a : *hits)
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
        if (!src_dealt.empty()) { src_dealt[static_cast<std::size_t>(a.src)] += a.amount; }
    }

    // 3. Snapshot the triggers (LKI), before anything changes, in BATTLEFIELD order (the canonical
    //    order, design 4.7): one reflection trigger per damaged reflector for the event's total
    //    (rulings: "triggers once ... that much damage"), raised even when the damage is lethal.
    std::vector<PendingTrig> raised;
    if (!ct.empty())
    {
        // Spiteful Sliver (grants_reflect_to_subtype): every granting permanent on the battlefield NOW --
        // before apply and SBA, so a granter this very event kills still grants its instance for this
        // event (design 2.4: the granted ability existed when the damage was dealt).
        const std::vector<const Permanent*> granters = Granters(s);
        for (const Permanent& q : s.battlefield)
        {
            if (!q.card.IsCreature()) { continue; }
            int amt = 0;
            for (const CreatureTotal& c : ct) { if (c.number == q.card.m_number) { amt = c.amount; break; } }
            if (amt <= 0) { continue; }
            const CardDefinition* d = Def(q);
            // The granted instances, ONE per granter that covers q (CR 113.2c: multiple instances of the
            // same triggered ability each trigger; two Spiteful Slivers -> each Sliver reflects twice).
            // Raised AFTER q's own reflection, in the granters' battlefield order (design 4.7).
            auto raise_granted = [&]() {
                for (const Permanent* g : granters)
                {
                    if (!GrantCovers(*g, q)) { continue; }
                    raised.push_back({ Def(*g)->params.grants_reflect_to_subtype,
                                       SourceRef{ q.card, q.controller_index, true },
                                       q.card.m_number, amt, false, -1 });
                }
            };
            if (d == nullptr || d->params.reflect_dealt_damage == CardParams::ReflectTo::None)
            { raise_granted(); continue; }
            // Stuffy Doll: the recipient is the player its as-enters choice locked (read NOW -- LKI, the
            // Doll may die to this very event). A Doll staged onto the battlefield without the as-enters
            // replacement (chosen_player -1; no current entry path does this) takes the opponent, the
            // design-4.6 fold's pick.
            const int fixed = d->params.reflect_dealt_damage != CardParams::ReflectTo::ChosenPlayer ? -1
                            : q.chosen_player >= 0 ? q.chosen_player : 1 - q.controller_index;
            raised.push_back({ d->params.reflect_dealt_damage, SourceRef{ q.card, q.controller_index, true },
                               q.card.m_number, amt, d->params.reflect_player_cant_gain_life, fixed });
            raise_granted();
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
        if (!src_dealt.empty()) { src_dealt[static_cast<std::size_t>(p.src)] += dealt; }
        // Screaming Nemesis: "If a player is dealt damage this way, they can't gain life for the rest
        // of the game" -- only damage actually DEALT (a prevented hit sets nothing).
        if (p.lock_life && dealt > 0)
        { s.cant_gain_life_rest_of_game_mask = static_cast<std::uint8_t>(s.cant_gain_life_rest_of_game_mask | (1u << p.player)); }
    }

    // 3b. Guilty Conscience triggers, AFTER the reflection triggers (the canonical order, design 4.7). Read
    //     here -- damage marked, SBA not yet run -- so every GC is still attached to the host that dealt the
    //     damage (a host this event kills still triggers its GC; the GC trigger then finds "that creature"
    //     gone and deals nothing).
    if (!src_dealt.empty())
    {
        for (std::size_t i = 0; i < b.src.size(); ++i)
        {
            if (!b.src[i].is_creature || src_dealt[i] <= 0 || b.src[i].card.m_number == 0) { continue; }
            RaiseAuraHostDamageBack(s, b.src[i].card.m_number, src_dealt[i], raised);
        }
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
    DrainStop stop = DrainStop::Drained;
    while (t_frame.stack.size() > base)
    {
        if (GameOver(s)) { t_frame.stack.resize(base); stop = DrainStop::GameOver; break; }
        if (++resolutions > kResolutionCap)
        {
            Log(s, "trigger", "rfx-cap: trigger resolution cap reached -- the remaining triggers are dropped");
            t_frame.stack.resize(base);
            stop = DrainStop::Cap;
            --resolutions;
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
            stop = DrainStop::NoProgress;
            break;
        }
    }
    if (stop == DrainStop::Drained && GameOver(s)) { stop = DrainStop::GameOver; }
    t_last_drain = DrainStats{ resolutions, stop };
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

// The en-Kor shield rider on our own sweeper (defined below, after the pin machinery it uses).
inline void AddShieldRider(GameState& s, int controller, int dmg, Batch& b);

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
    AddShieldRider(s, controller, dmg, b);   // en-Kor shields (design 4.2); no-op without an en-Kor of ours
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
    if (t.kind == RT::ChosenPlayer) { return { PlayerId(t.fixed_player) }; }   // a fixed recipient, not a target
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
// THE COMBAT SUFFIX (step 5b). Plan::reflect_targets is [main-phase pins] [kCombatPinMark combat pins]:
// the choices a Guilty Conscience trigger raises INSIDE COMBAT (a GC'd Boros Reckoner / Screaming Nemesis
// attacks, GC hits it, its reflection picks a target) are pinned after the mark. The main-phase apply reads
// only the prefix (t_pins_n); WritePostApplyStatePins copies the suffix onto the STATE
// (GameState::scripted_combat_reflect, the Wanderwine Prophets state-pin route), and CombatDamageDealt --
// shared by the rollout's SimulateCombat and the executor's combat -- installs it for the combat drain.
// Packing both into one vector keeps sizeof(Plan) and every plan fold site unchanged (each already folds
// the whole vector).
inline constexpr int kCombatPinMark = -0x40000000;   // never a stable id (players are -1/-2, cards > 0)
inline thread_local std::size_t t_pins_n = 0;         // the installed vector's main-phase prefix length
inline std::size_t MainPinCount(const std::vector<int>& v)
{
    const auto it = std::find(v.begin(), v.end(), kCombatPinMark);
    return static_cast<std::size_t>(it - v.begin());
}
inline bool HasCombatPins(const std::vector<int>& v) { return MainPinCount(v) < v.size(); }
inline std::vector<int> CombatPinsOf(const std::vector<int>& v)
{
    const std::size_t at = MainPinCount(v);
    return at < v.size() ? std::vector<int>(v.begin() + static_cast<std::ptrdiff_t>(at) + 1, v.end())
                         : std::vector<int>{};
}
using TargetProbe = std::function<void(const std::vector<int>&)>;
inline thread_local TargetProbe* t_probe = nullptr;
inline thread_local const std::vector<int>* t_probe_owner = nullptr;
inline thread_local int t_probe_fan_depth = -1;
// The legal set of the choice the viewer is being asked about (stable ids, front = the engine's
// default), read by the human-play target chooser (main.cpp) while it builds the prompt.
inline thread_local const std::vector<int>* t_viewer_legal = nullptr;
// True while t_viewer_legal is a REFLECTION trigger's target list (ResolveTrig), false for the Doll's
// as-enters choice. main.cpp cannot tell them apart by the def: a Spiteful Sliver's GRANTED reflection
// resolves with the damaged Sliver's def, whose own reflect_dealt_damage is None.
inline thread_local bool t_viewer_reflection = false;

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
    std::size_t old_n;
    TargetProbe* old_probe;
    const std::vector<int>* old_probe_owner;
    int old_probe_depth;
    explicit PinScope(const std::vector<int>& pins)
        : old(t_pins), cursor(t_pin_cursor), old_n(t_pins_n), old_probe(t_probe), old_probe_owner(t_probe_owner),
          old_probe_depth(t_probe_fan_depth)
    {
        t_pins = &pins; t_pin_cursor = 0; t_pins_n = MainPinCount(pins);
        if (t_probe && t_probe_owner != &pins) { t_probe = nullptr; t_probe_owner = nullptr; t_probe_fan_depth = -1; }
    }
    ~PinScope()
    {
        t_pins = old; t_pin_cursor = cursor; t_pins_n = old_n;
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
// Step 4 adds Stuffy Doll's {T} self-ping (self_ping_damage, PermAbilityMode::SelfPing -> SelfPingAt).
// Step 5 adds Guilty Conscience (aura_host_damage_back): a plan that casts one onto a creature makes that
// creature's damage come back to it. Combat damage is the remaining route (CombatDamageDealt): it is NOT
// probed by the main-phase fan, and needs no entry here -- step 5b's COMBAT FAN (AppendSubdecisionAxes) has
// its own pre-check (a GC on the battlefield or in the plan, and a chooser), since in the goldfish combat
// damage reaches a creature only through a GC trigger.
inline bool DefCanDamageCreatures(const CardDefinition& d)
{
    return d.params.damage_all_creatures > 0 || d.params.damage_all_creatures_count_creatures
        || d.params.x_damage_each_creature_and_player || d.params.ping_all_amount > 0
        || d.params.ping_all_cost.has_value() || d.params.self_ping_damage > 0
        || d.params.aura_host_damage_back;
}
// Does `d` carry a reflection trigger whose target can be a CHOICE? Mogg Maniac's "target opponent"
// is always the singleton {opponent} (one opponent pool, also in 2HG), so it never needs the fan.
inline bool DefReflectsWithChoice(const CardDefinition& d)
{
    using RT = CardParams::ReflectTo;
    // Stuffy Doll's ChosenPlayer is a FIXED recipient (the as-enters choice), never a choice here either.
    auto choice = [](RT k) { return k != RT::None && k != RT::TargetOpponent && k != RT::ChosenPlayer; };
    // Spiteful Sliver's GRANTED "target player" reflection: a choice on whichever creature it covers, and
    // only a granter on the battlefield or in the plan can raise one -- so testing the granter suffices.
    return choice(d.params.reflect_dealt_damage) || choice(d.params.grants_reflect_to_subtype);
}
// Does `d` raise an en-Kor shield CHOICE (AddShieldRider) when our own sweeper hits it? The main-phase fan's
// pre-check counts it as a chooser on the battlefield or in the plan, beside DefReflectsWithChoice.
inline bool DefOffersShieldChoice(const CardDefinition& d) { return d.params.redirect_shield || d.params.redirect_pull_cost.has_value(); }

// ONE searched choice on the pin stream (a list of more than one option): takes the next ordinal, reports the
// list to the fan's probe, and returns the plan's pin for that ordinal when it is legal, else the FRONT (the
// permitted one-shot policy; see the axis note above). Shared by the reflection targets and the en-Kor shields.
inline int PinnedChoice(const std::vector<int>& legal)
{
    int pick = legal.front();
    const std::size_t ordinal = t_pin_cursor++;
    if (t_probe) { (*t_probe)(legal); }
    if (t_pins && ordinal < t_pins_n)   // the main-phase prefix only (never the combat suffix)
    {
        const int pin = (*t_pins)[ordinal];
        if (std::find(legal.begin(), legal.end(), pin) != legal.end()) { pick = pin; }
    }
    return pick;
}

inline void ResolveTrig(GameState& s, const PendingTrig& t)
{
    using RT = CardParams::ReflectTo;
    if (t.aura_host != 0)   // Guilty Conscience: no target, no choice -- "that creature" (gone -> nothing)
    {
        if (FindPerm(s, t.aura_host) == nullptr) { return; }
        Batch b;
        b.src.push_back(t.src);
        b.hits.push_back({ 0, { false, t.aura_host }, t.amount, false, false });
        if (g_play_event_sink != nullptr && !TapSpeculatingNow())
        {
            const Permanent* q = FindPerm(s, t.aura_host);
            EmitPlayEvent(s.turn_number, "trigger",
                          "\xE2\x9A\xA1 " + t.src.card.m_name.str() + " -- its creature dealt damage; it deals "
                          + std::to_string(t.amount) + " to " + q->card.m_name.str());
        }
        DealDamageBatch(s, b);
        return;
    }
    if (t.kind != RT::TargetOpponent && t.kind != RT::AnyTarget && t.kind != RT::AnyOtherTarget
        && t.kind != RT::TargetPlayer && t.kind != RT::ChosenPlayer)
    {
        std::fprintf(stderr, "[rfx] FATAL: unresolvable trigger kind %d from %s\n",
                     static_cast<int>(t.kind), t.src.card.m_name.str().c_str());
        std::abort();
    }
    const std::vector<int> engine_legal = LegalReflectTargets(s, t);
    std::vector<int> legal = engine_legal;
    // Stuffy Doll: "it deals that much damage to the chosen player" -- no target, no hook, no prompt.
    if (!HumanPlayActive() && t.kind != RT::ChosenPlayer)   // human play sees every legal target (the provider may not prune it)
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
        pick = PinnedChoice(legal);
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
                const bool saved_rx = t_viewer_reflection;
                t_viewer_legal = &legal;
                t_viewer_reflection = true;
                const auto chosen = (*g_play_target_chooser)(s, *def, t.src.controller, 1, t.amount, { to_ct(pick) });
                t_viewer_legal = saved;
                t_viewer_reflection = saved_rx;
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

// ---- COMBAT (design step 5): Guilty Conscience on a creature that dealt combat damage ----------------
// Called by both combat-damage paths (Combat.cpp; executor and rollout share them) AFTER a damage step's
// damage to the player is applied, on reflect_armed decks only. `nums`/`amounts` are the step's connecting
// sources (card m_number) and what each dealt. In the goldfish combat damage only ever reaches the
// opponent (nothing blocks), so no creature is dealt combat damage and no reflection trigger arises from
// the step itself; what remains is each host's GC trigger, raised at this snapshot in battlefield order and
// drained as one top-level event (cap and no-progress rule included). A lethal step raises nothing.
//
// THE COMBAT-TIME REFLECTION CHOICE (step 5b). A GC trigger in combat can hit a reflector with a target
// CHOICE (Boros Reckoner: any target; Screaming Nemesis: any other target). That choice is SEARCHED: the
// plan's combat suffix (kCombatPinMark above) rides GameState::scripted_combat_reflect, installed here for
// the drain; consumed pins are erased from the front, so a first-strike step and the regular step (and an
// additional combat) continue one ordinal stream. AppendSubdecisionAxes' COMBAT FAN probes the plan's
// apply + SimulateCombat under t_combat_probe (reported only for the probing state, so a nested copy's
// combat cannot add speculative choices) and clones the plan once per admitted option at the first
// unpinned combat ordinal. An unpinned choice takes the list's front -- only beyond the horizon, in a
// speculative apply, or where the executor's state drifted from the plan's (the main-phase rule).
struct CombatProbe
{
    TargetProbe* probe = nullptr;
    const GameState* state = nullptr;
    int depth = -1;
};
inline thread_local CombatProbe t_combat_probe;
struct CombatProbeScope
{
    CombatProbe saved = t_combat_probe;
    CombatProbeScope(TargetProbe& probe, const GameState& s, int depth) { t_combat_probe = { &probe, &s, depth }; }
    ~CombatProbeScope() { t_combat_probe = saved; }
    CombatProbeScope(const CombatProbeScope&) = delete;
    CombatProbeScope& operator=(const CombatProbeScope&) = delete;
};

inline void CombatDamageDealt(GameState& s, const std::vector<int>& nums, const std::vector<int>& amounts)
{
    if (!s.reflect_armed || nums.empty() || GameOver(s) || !AnyAuraHostDamageBack(s)) { return; }
    std::vector<PendingTrig> raised;
    for (std::size_t k = 0; k < nums.size() && k < amounts.size(); ++k)
    {
        if (nums[k] == 0 || amounts[k] <= 0) { continue; }
        RaiseAuraHostDamageBack(s, nums[k], amounts[k], raised);
    }
    if (raised.empty()) { return; }
    Frame saved;
    std::swap(saved, t_frame);
    t_frame.state = &s;
    t_frame.depth = 1;
    for (std::size_t i = raised.size(); i-- > 0; ) { t_frame.stack.push_back(std::move(raised[i])); }
    std::size_t used = 0;
    {
        // The combat suffix of the plan that led here (empty: every choice takes the front). The PinScope
        // also detaches any main-phase probe, so a main fan's probe never counts a combat choice.
        const std::vector<int> pins = s.scripted_combat_reflect;
        PinScope pin_scope(pins);
        std::optional<TargetProbeScope> probing;
        if (t_combat_probe.probe != nullptr && t_combat_probe.state == &s)
        { probing.emplace(*t_combat_probe.probe, pins, t_combat_probe.depth); }
        Drain(s, 0);
        used = t_pin_cursor;
    }
    if (used > 0 && !s.scripted_combat_reflect.empty())
    {
        s.scripted_combat_reflect.erase(s.scripted_combat_reflect.begin(),
            s.scripted_combat_reflect.begin() + static_cast<std::ptrdiff_t>(std::min(used, s.scripted_combat_reflect.size())));
    }
    std::swap(saved, t_frame);
}

// ---- STUFFY DOLL (design 4.6 / step 4) ----------------------------------------------------------------
//
// THE CHOSEN-PLAYER FOLD (SOUND, design 4.6; the F1 argument made deck-level). "As this creature enters,
// choose a player": the choice is locked for the object's lifetime, and its ONLY reader is the Doll's own
// reflection ("it deals that much damage to the chosen player", ReflectTo::ChosenPlayer). Choosing the
// opponent sends every point the Doll is ever dealt to the opponent instead of to us. Proof that this
// weakly dominates choosing ourselves, for the rest of the game: damage to the opponent is monotone good
// in the goldfish, and our own life is read DOWNWARD by nothing in the engine (F1's proof at
// OwnFaceDominated: every own-life reader is a floor) -- EXCEPT the cards that turn damage to us into
// something (Purity, Tamanoa, and the other dmgev arming terms). Because the choice outlives the board it
// was made on, the fold tests the DECK, not the board: GameState::dmg_events_armed is the deck-constant
// superset of every such card (main + sideboard), so while it is false none can ever reach our
// battlefield. Lifted under human play (the viewer asks) and MTG_UNPRUNE=reflecttarget. When it does not
// hold, the cast is fanned on the ETB CHOICE AXIS (CollectActions' post-pass: chosen_x 1 = the opponent,
// 2 = us), so the search -- not a default -- picks.
inline bool ChosenPlayerFoldHolds(const GameState& s)
{
    if (HumanPlayActive() || DecisionUnpruned(UnprunedGate::ReflectTarget)) { return false; }
    return !s.dmg_events_armed;
}

// The as-enters replacement itself, called from FireOwnEtbTriggersBody (both worlds) ahead of every enter
// trigger. etb_kx is the ETB CHOICE AXIS pin (1 = the opponent, 2 = the controller; anything else = no
// pin). With no pin the opponent is taken: under the fold that IS the dominant choice; without it, an
// unpinned entry is one the fan does not score (a rollout leaf beyond the horizon, the depth-0 runner, a
// speculative apply) -- the sanctioned front-candidate greedy. A put onto the battlefield (no current
// route puts a Doll) also arrives unpinned. Human play is asked through the viewer's `target` prompt with
// the two players (t_viewer_legal), worded by main.cpp as the Doll's choice.
inline void EtbChoosePlayer(GameState& s, int controller, int entered_index, const CardDefinition& def,
                            int etb_kx)
{
    if (s.battlefield[static_cast<std::size_t>(entered_index)].chosen_player >= 0) { return; }
    const int opp = 1 - controller;
    int pick = (etb_kx == 2) ? controller : opp;
    if (g_play_target_chooser != nullptr && !TapSpeculatingNow())
    {
        const std::vector<int> legal{ PlayerId(opp), PlayerId(controller) };
        const std::vector<int>* saved = t_viewer_legal;
        const bool saved_rx = t_viewer_reflection;
        t_viewer_legal = &legal;
        t_viewer_reflection = false;   // the as-enters choice, not a reflection target
        const auto chosen = (*g_play_target_chooser)(s, def, controller, 1, 0,
                                                     { ChosenTarget{ 0, pick, 0 } });
        t_viewer_legal = saved;
        t_viewer_reflection = saved_rx;
        if (chosen.size() == 1 && chosen[0].kind == 0 && (chosen[0].index == 0 || chosen[0].index == 1))
        { pick = chosen[0].index; }
    }
    s.battlefield[static_cast<std::size_t>(entered_index)].chosen_player = static_cast<int8_t>(pick);
    if (g_play_event_sink != nullptr && !TapSpeculatingNow())
    {
        EmitPlayEvent(s.turn_number, "choose_player",
                      def.card.m_name.str() + " -- chose " + (pick == controller ? "you" : "the opponent"));
    }
}

// ---- EN-KOR SHIELDS: the rider on our own sweeper (design 4.2 / step 8) ---------------------------------
// "{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you
// control instead" (Nomads en-Kor; Shaman en-Kor's first ability). Free and unlimited, so the decision is an
// ALLOCATION: how many of the d points our sweeper is about to deal the en-Kor E move, and onto what.
//
// HOSTING (timing fold, design 4.2). Shields last "this turn", and nothing can happen between our sorcery-
// speed cast and the sweeper's resolution, so activating them in response to our own sweeper is the same as
// activating them just before it resolves. The allocation is therefore made AT the sweep (dmgrx::Sweep, both
// worlds) as a choice on the shared pin stream (PinnedChoice): ordinals in resolution order, BEFORE the
// sweep's reflection choices, fanned by AppendSubdecisionAxes' main-phase probe exactly like a reflection
// target, replayed by the executor from Plan::reflect_targets. Shields against other damage (a reflection
// aimed at an en-Kor, a Guilty Conscience on an en-Kor) are not offered: the reflection can be aimed at the
// shield's target directly (F2). PROVISIONAL deferral pending the USER (Q-U1), disclosed on the cards.
//
// THE OPTION SET (design 4.2), per en-Kor E of the caster's with d > 0, in battlefield order:
//   F4 (SOUND fold): when E carries no reflection of its own and some creature R we control is a SINK
//     (indestructible, or doomed: the damage already marked plus what this event deals it reaches its
//     lethal toughness) with >= 1 reflection instance, "all d onto such an R" weakly dominates every other
//     allocation for E: each point on R reaches a player through R's reflection(s), a point left on E (or
//     on a non-reflector) reaches nothing, E dealt 0 survives at least as well, R's own survival does not
//     change, and nothing else differs. Partial splits between two sinks are dominated by the better end
//     (the amounts are additive). Among sinks, the FACE-ONLY ones (every instance can only hit the
//     opponent's face: Mogg Maniac, a Doll that chose the opponent, a granted "target player" under F1, no
//     Guilty Conscience attached) are interchangeable up to their multiplicity, so ONE representative with the
//     highest multiplicity is offered; every other sink class (a choice of target, a GC'd reflector, a Doll
//     that chose us) is offered separately (identity-folded). Options: {all d -> each such class}.
//   otherwise (no sink reflector, or E reflects itself) the design's LANDMARKS: {0}; per reflector class R,
//     {all d -> R}, {just enough to keep E alive -> R}, {the most R survives -> R}; and {all d -> one non-en-
//     Kor sink with no reflection} when that keeps E alive (such sinks are interchangeable: the points change
//     nothing but E's survival). Redirecting onto a LIVE non-reflector (another en-Kor included) is not
//     offered autonomously (the design's landmark bound, disclosed); human play gets the full allocation.
//   SYMMETRY: under F4 consecutive interchangeable en-Kors with the same list choose a non-decreasing option
//     index (the plan carries a multiset; swapping two identical en-Kors' allocations is the same end state).
// The FRONT is the default where nothing pins (beyond the horizon, the d0 runner, speculative applies):
// under F4 the highest-multiplicity sink, otherwise {0} (no shield, the legacy outcome).
//
// Option ids on the pin stream: kShieldPinBase - (dest * 1024 + amount), dest = the target's m_number (0 for
// {0}). Below every player id (-1/-2), above kCombatPinMark, never a card id.
inline constexpr int kShieldPinBase = -0x10000000;
inline int ShieldCode(int dest, int amount)
{
    if (dest < 0 || dest >= (1 << 17) || amount < 0 || amount >= 1024)
    {
        std::fprintf(stderr, "[rfx] FATAL: en-Kor shield option (dest %d, amount %d) cannot be encoded\n", dest, amount);
        std::abort();
    }
    return kShieldPinBase - (dest * 1024 + amount);
}
inline std::pair<int, int> ShieldDecode(int code)   // {dest, amount}
{
    const int v = kShieldPinBase - code;
    return { v / 1024, v % 1024 };
}

// The human-play `divide` prompt for one en-Kor (read by main.cpp's target chooser while it builds it):
// E's d points divided among E itself (= not redirected) and every other creature its controller controls.
struct ShieldPrompt
{
    int enkor = -1;                 // battlefield index of E
    int total = 0;                  // d
    std::vector<int> legal;         // battlefield indices, E first
    std::vector<int> defaults;      // the engine's allocation, aligned to legal
};
inline thread_local const ShieldPrompt* t_viewer_shield = nullptr;

// How many reflection instances creature q carries now (its own + one per covering granter): mult(q).
inline int ReflectInstances(const GameState& s, const Permanent& q)
{
    const CardDefinition* d = Def(q);
    const int own = (d != nullptr && d->params.reflect_dealt_damage != CardParams::ReflectTo::None) ? 1 : 0;
    return own + GrantedInstances(s, q);
}
inline int AuraHostDamageBackOn(const GameState& s, int number)
{
    int n = 0;
    for (const Permanent& a : s.battlefield)
    {
        if (a.aura_attached_to != number) { continue; }
        const CardDefinition* d = Def(a);
        if (d != nullptr && d->params.aura_host_damage_back) { ++n; }
    }
    return n;
}
// Is every reflection instance on q (controlled by `ctrl`) able to hit ONLY the opponent's face right now?
inline bool ReflectsFaceOnly(const GameState& s, const Permanent& q)
{
    using RT = CardParams::ReflectTo;
    if (AuraHostDamageBackOn(s, q.card.m_number) > 0) { return false; }
    const int ctrl = q.controller_index, opp = 1 - ctrl;
    const CardDefinition* d = Def(q);
    const RT own = d != nullptr ? d->params.reflect_dealt_damage : RT::None;
    if (own == RT::AnyTarget || own == RT::AnyOtherTarget || own == RT::TargetPlayer) { return false; }
    if (own == RT::ChosenPlayer && q.chosen_player >= 0 && q.chosen_player != opp) { return false; }
    PendingTrig probe;
    probe.src = SourceRef{ q.card, ctrl, true };
    for (const Permanent* g : Granters(s))
    {
        if (!GrantCovers(*g, q)) { continue; }
        const RT k = Def(*g)->params.grants_reflect_to_subtype;
        if (k == RT::TargetOpponent) { continue; }
        if (k == RT::TargetPlayer && OwnFaceDominated(s, probe)) { continue; }   // F1: {opponent}
        return false;
    }
    return true;
}

struct ShieldOpt { int dest = 0; int amount = 0; };

// The option list for en-Kor E under a sweep of `dmg` to each creature (see the block note). `extra` holds
// what earlier en-Kors of this rider already moved onto each destination (m_number, amount). `f4` reports
// whether the F4 fold produced the list. `base` (step 9), when given, is the damage the sweep deals each
// creature AFTER the rider's source pulls (m_number, amount; a creature not listed is dealt 0): E's own d is
// then base(E) -- its sweep damage plus everything pulled onto it -- and a pulled-from creature is dealt 0.
inline std::vector<ShieldOpt> ShieldOptions(const GameState& s, const Permanent& e, int dmg,
                                            const std::vector<std::pair<int, int>>& extra, bool& f4,
                                            const std::vector<std::pair<int, int>>* base = nullptr)
{
    f4 = false;
    std::vector<ShieldOpt> out;
    auto base_on = [&](int num) {
        if (base == nullptr) { return dmg; }
        for (const auto& x : *base) { if (x.first == num) { return x.second; } }
        return 0;
    };
    const int d = base_on(e.card.m_number);
    if (d <= 0) { return out; }
    auto extra_on = [&](int num) { int n = 0; for (const auto& x : extra) { if (x.first == num) { n += x.second; } } return n; };
    auto is_enkor = [&](const Permanent& q) { const CardDefinition* qd = Def(q); return qd != nullptr && qd->params.redirect_shield; };
    struct Cand { const Permanent* q; int mult; bool sink; bool face_only; int lt; int room; };
    std::vector<Cand> cands;
    for (const Permanent& q : s.battlefield)
    {
        if (!q.card.IsCreature() || q.controller_index != e.controller_index || q.card.m_number == e.card.m_number) { continue; }
        const int lt = LethalToughness(q, s);
        const bool indes = q.HasKeyword(Keyword::Indestructible);
        const int incoming = q.damage + base_on(q.card.m_number) + extra_on(q.card.m_number);
        const bool sink = indes || lt <= 0 || incoming >= lt;
        const int mult = ReflectInstances(s, q);
        cands.push_back({ &q, mult, sink, mult > 0 && ReflectsFaceOnly(s, q), lt, indes ? 1 << 20 : lt - 1 - incoming });
    }
    // Identity class of a candidate (two candidates with equal keys give the same end state).
    auto same_class = [&](const Cand& a, const Cand& b) {
        const CardDefinition* da = Def(*a.q);
        const CardDefinition* db = Def(*b.q);
        return a.q->card.m_name == b.q->card.m_name && da == db && a.mult == b.mult && a.sink == b.sink
            && a.q->HasKeyword(Keyword::Indestructible) == b.q->HasKeyword(Keyword::Indestructible)
            && a.q->chosen_player == b.q->chosen_player
            && AuraHostDamageBackOn(s, a.q->card.m_number) == AuraHostDamageBackOn(s, b.q->card.m_number)
            && (a.sink || (a.lt == b.lt && a.room == b.room));
    };
    auto push = [&](int dest, int amount) {
        for (const ShieldOpt& o : out) { if (o.dest == dest && o.amount == amount) { return; } }
        out.push_back({ dest, amount });
    };
    const int mult_e = ReflectInstances(s, e);
    bool any_sink_reflector = false;
    for (const Cand& c : cands) { if (c.sink && c.mult > 0) { any_sink_reflector = true; break; } }
    if (mult_e == 0 && any_sink_reflector)
    {
        f4 = true;
        // the face-only representative: the highest multiplicity, first in battlefield order
        const Cand* face = nullptr;
        for (const Cand& c : cands)
        { if (c.sink && c.mult > 0 && c.face_only && (face == nullptr || c.mult > face->mult)) { face = &c; } }
        std::vector<const Cand*> picks;
        if (face != nullptr) { picks.push_back(face); }
        for (const Cand& c : cands)
        {
            if (!c.sink || c.mult <= 0 || c.face_only) { continue; }
            bool dup = false;
            for (const Cand* p : picks) { if (!p->face_only && same_class(*p, c)) { dup = true; break; } }
            if (!dup) { picks.push_back(&c); }
        }
        std::stable_sort(picks.begin(), picks.end(), [](const Cand* a, const Cand* b) { return a->mult > b->mult; });
        for (const Cand* p : picks) { push(p->q->card.m_number, d); }
        return out;
    }
    // Landmarks.
    push(0, 0);
    const int lt_e = LethalToughness(e, s);
    const bool e_dies = !e.HasKeyword(Keyword::Indestructible) && (lt_e <= 0 || e.damage + d >= lt_e);
    // points that must leave E for it to survive: e.damage + (d - k) < lt_e  <=>  k >= e.damage + d - lt_e + 1
    const int keep_e = e_dies && lt_e > 0 ? std::max(1, e.damage + d - lt_e + 1) : 0;
    std::vector<const Cand*> seen;
    for (const Cand& c : cands)
    {
        if (c.mult <= 0) { continue; }
        bool dup = false;
        for (const Cand* p : seen) { if (same_class(*p, c)) { dup = true; break; } }
        if (dup) { continue; }
        seen.push_back(&c);
        const int num = c.q->card.m_number;
        push(num, d);
        if (keep_e > 0 && keep_e < d) { push(num, keep_e); }
        if (!c.sink && c.room > 0 && c.room < d) { push(num, c.room); }
    }
    if (e_dies)
    {
        for (const Cand& c : cands)
        {
            if (c.mult == 0 && c.sink && !is_enkor(*c.q)) { push(c.q->card.m_number, d); break; }
        }
    }
    return out;
}

// ---- SHAMAN EN-KOR'S SOURCE PULL: the {1}{W} half of the rider (design 4.3 / section 14 step 9) -------------
// "{1}{W}: The next time a source of your choice would deal damage to target creature this turn, that damage is
// dealt to this creature instead" (CardParams::redirect_pull_cost). Hosted on the same sweeper rider as the {0}
// shields, by the same timing fold (nothing happens between our sorcery-speed cast and its resolution, so
// activating in response is activating as it resolves), and the SOURCE OF OUR CHOICE is that sweeper (CR 609.7a:
// a spell on the stack is a legal choice). PROVISIONAL deferral (USER Q-U1): no other source, no other window.
//
// THE CHOICE, per puller P of the caster's (battlefield order): which creatures P pulls from, each pull one
// activation. Made as a SEQUENCE of choices on the shared pin stream (PinnedChoice), BEFORE the shield choices
// (the shields then see what was pulled): {stop} + one representative per target CLASS, until stop. Pulls on the
// same target are pointless (the first takes all of that source's damage to it; the next has nothing left to
// replace, and with rider-only hosting no later damage this turn reaches it), so each creature is pulled at most
// once, by one puller. The FRONT is {stop}: no pull, no mana -- the legacy outcome wherever nothing pins.
// THE MANA IS PAID AT THE SWEEP, in BOTH worlds, through TapForCostDirect (the Mihail / Arcanum Wings shape:
// lockstep by construction, the executor re-derives its pool per action): a pull is offered only while a COPY of
// the state can pay the cumulative bill (every earlier pull of this sweep + this one, EffectiveActivationCost per
// puller), and the whole bill is then paid at once before the damage. A plan therefore cannot overspend: what the
// pulls tap is gone for every later action of the plan, in the search and in the executor alike.
// SOUND FOLDS (stated in design section 14, step 9):
//   P0  a creature the sweep deals no damage to is not offered (the pull would replace nothing; no later damage
//       this turn under rider-only hosting) -- dominated by not paying.
//   P1  our own {0}-shield en-Kor (the puller itself included) is not offered: its own free shields can move any
//       of its points onto the puller (a creature we control) or straight on to wherever the puller would forward
//       them -- the same end state without the mana. (Itself: "dealt to this creature instead" changes nothing.)
//   P2  an INDESTRUCTIBLE reflector of ours whose every instance can only hit the opponent's face, with no less
//       multiplicity than any creature we control, is not offered: its survival cannot change, the points it
//       reflects mult(T) x d to the face cannot be beaten by any destination the puller's shields can reach, and
//       the pull costs mana. (Every OTHER reflector stays: pulling off a doomed Mogg Maniac keeps it alive.)
//   P3  identical targets (same name, definition, controller, token-ness, damage, lethal toughness, power, counters,
//       tapped / summoning-sick state, nothing attached, same reflection multiplicity / chosen player, same damage
//       incoming) are interchangeable: one representative per class, and a puller's successive picks take a
//       non-decreasing class index (its pulls form a multiset).
// Human play: one `target` prompt per puller (any number of creatures up to what the mana affords, either side,
// every creature but the puller itself and those an earlier puller took; default = the engine's picks).
// Option ids on the pin stream: kPullPinBase - from (from = the target's m_number; 0 = stop). Disjoint from the
// shield codes (above kPullPinBase - 2^17 > kCombatPinMark, below every shield code) and from every player/card id.
inline constexpr int kPullPinBase = -0x20000000;
inline int PullCode(int from)
{
    if (from < 0 || from >= (1 << 17))
    {
        std::fprintf(stderr, "[rfx] FATAL: Shaman en-Kor pull target %d cannot be encoded\n", from);
        std::abort();
    }
    return kPullPinBase - from;
}
inline int PullDecode(int code) { return kPullPinBase - code; }

// The human-play `target` prompt for one puller (read by main.cpp's target chooser while it builds it).
struct PullPrompt
{
    int puller = -1;                // battlefield index of P
    int max = 0;                    // how many pulls the mana affords now
    std::vector<int> legal;         // battlefield indices
    std::vector<int> defaults;      // the engine's picks (battlefield indices)
};
inline thread_local const PullPrompt* t_viewer_pull = nullptr;

// Can `s` pay `bill` now? (On a COPY: the probe never taps the real state.)
inline bool PullAffordable(const GameState& s, const ManaCost& bill)
{
    GameState probe = s;
    return TapForCostDirect(probe, bill, false);
}

// Choose (search / pin / ask) and PAY the pulls of every puller of `controller`'s on this sweep. `base` is the
// damage the sweep deals each creature (m_number, amount) and is updated in place: a pulled creature's amount
// moves onto its puller. Appends to b.pulls (source = b.src[0], the sweeper).
inline void AddPullRider(GameState& s, int controller, Batch& b, std::vector<std::pair<int, int>>& base, bool ask)
{
    auto base_ref = [&](int num) -> int* { for (auto& x : base) { if (x.first == num) { return &x.second; } } return nullptr; };
    auto pulled = [&](int num) { for (const Pull& p : b.pulls) { if (p.from == num) { return true; } } return false; };
    std::vector<int> pullers;   // battlefield indices, battlefield order
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    {
        const Permanent& q = s.battlefield[i];
        if (!q.card.IsCreature() || q.controller_index != controller) { continue; }
        const CardDefinition* d = Def(q);
        if (d != nullptr && d->params.redirect_pull_cost.has_value()) { pullers.push_back(static_cast<int>(i)); }
    }
    if (pullers.empty()) { return; }
    ManaCost bill;                 // every pull chosen so far on this sweep
    std::vector<Pull> chosen;      // in order
    for (int pi : pullers)
    {
        const Permanent& P = s.battlefield[static_cast<std::size_t>(pi)];
        const CardDefinition* pd = Def(P);
        const ManaCost cost = EffectiveActivationCost(s, controller, P.card, *pd->params.redirect_pull_cost);
        auto taken = [&](int num) {
            if (pulled(num)) { return true; }
            for (const Pull& c : chosen) { if (c.from == num) { return true; } }
            return false;
        };
        int max_mult_ours = 0;
        for (const Permanent& q : s.battlefield)
        { if (q.card.IsCreature() && q.controller_index == controller) { max_mult_ours = std::max(max_mult_ours, ReflectInstances(s, q)); } }
        // Candidate classes (P0-P3), battlefield order of their first member.
        struct Cls { std::vector<int> members; };
        std::vector<Cls> classes;
        auto same_class = [&](const Permanent& a, const Permanent& c) {
            auto attached = [&](const Permanent& x) {
                for (const Permanent& y : s.battlefield)
                { if (y.aura_attached_to == x.card.m_number || y.equipped_to == x.card.m_number) { return true; } }
                return false;
            };
            const int* ia = base_ref(a.card.m_number);
            const int* ic = base_ref(c.card.m_number);
            return a.card.m_name == c.card.m_name && Def(a) == Def(c) && a.controller_index == c.controller_index
                && a.is_token == c.is_token && a.damage == c.damage && a.tapped == c.tapped
                && a.entered_this_turn == c.entered_this_turn
                && LethalToughness(a, s) == LethalToughness(c, s) && CombatPowerOf(a, s) == CombatPowerOf(c, s)
                && a.HasKeyword(Keyword::Indestructible) == c.HasKeyword(Keyword::Indestructible)
                && a.counters.KeyFragment() == c.counters.KeyFragment()
                && ReflectInstances(s, a) == ReflectInstances(s, c) && a.chosen_player == c.chosen_player
                && !attached(a) && !attached(c)
                && ia != nullptr && ic != nullptr && *ia == *ic;
        };
        for (const Permanent& q : s.battlefield)
        {
            if (!q.card.IsCreature() || q.card.m_number == P.card.m_number || taken(q.card.m_number)) { continue; }
            const int* inc = base_ref(q.card.m_number);
            if (inc == nullptr || *inc <= 0) { continue; }                                            // P0
            const CardDefinition* qd = Def(q);
            if (q.controller_index == controller && qd != nullptr && qd->params.redirect_shield) { continue; }   // P1
            const int mult = ReflectInstances(s, q);
            if (q.controller_index == controller && q.HasKeyword(Keyword::Indestructible) && mult > 0
                && mult >= max_mult_ours && ReflectsFaceOnly(s, q)) { continue; }                     // P2
            bool placed = false;
            for (Cls& c : classes)
            {
                const Permanent* rep = FindPerm(s, c.members.front());
                if (rep != nullptr && same_class(*rep, q)) { c.members.push_back(q.card.m_number); placed = true; break; }
            }
            if (!placed) { classes.push_back(Cls{ { q.card.m_number } }); }
        }
        // The searched sequence (P3: non-decreasing class index).
        std::vector<int> picks;   // m_numbers
        std::vector<std::size_t> took(classes.size(), 0);
        std::size_t cls_from = 0;
        ManaCost run = bill;
        for (;;)
        {
            std::vector<int> legal{ PullCode(0) };
            const ManaCost next = run.PlusAdditionalCost(cost);
            bool any_left = false;
            for (std::size_t c = cls_from; c < classes.size(); ++c) { if (took[c] < classes[c].members.size()) { any_left = true; break; } }
            if (any_left && PullAffordable(s, next))
            {
                for (std::size_t c = cls_from; c < classes.size(); ++c)
                { if (took[c] < classes[c].members.size()) { legal.push_back(PullCode(classes[c].members[took[c]])); } }
            }
            if (legal.size() == 1) { break; }
            const int pick = PinnedChoice(legal);
            if (pick == PullCode(0)) { break; }
            const int from = PullDecode(pick);
            for (std::size_t c = cls_from; c < classes.size(); ++c)
            {
                if (took[c] < classes[c].members.size() && classes[c].members[took[c]] == from) { ++took[c]; cls_from = c; break; }
            }
            picks.push_back(from);
            run = next;
        }
        if (ask)   // human play: any set of creatures (either side, not P, not taken) up to what the mana affords
        {
            PullPrompt pr;
            pr.puller = pi;
            for (std::size_t i = 0; i < s.battlefield.size(); ++i)
            {
                const Permanent& q = s.battlefield[i];
                if (static_cast<int>(i) != pi && q.card.IsCreature() && !taken(q.card.m_number)) { pr.legal.push_back(static_cast<int>(i)); }
            }
            ManaCost probe_bill = bill;
            while (pr.max < static_cast<int>(pr.legal.size()))
            {
                probe_bill = probe_bill.PlusAdditionalCost(cost);
                if (!PullAffordable(s, probe_bill)) { break; }
                ++pr.max;
            }
            for (int num : picks)
            {
                for (int i : pr.legal) { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { pr.defaults.push_back(i); } }
            }
            std::sort(pr.defaults.begin(), pr.defaults.end());   // battlefield order (the prompt's option order)
            if (pr.max > 0 && !pr.legal.empty())
            {
                std::vector<ChosenTarget> dflt;
                for (int i : pr.defaults) { dflt.push_back({ 1, i, 0 }); }
                const PullPrompt* saved = t_viewer_pull;
                t_viewer_pull = &pr;
                const auto got = (*g_play_target_chooser)(s, *pd, controller, pr.max, 0, dflt);
                t_viewer_pull = saved;
                std::vector<int> human;
                bool ok = static_cast<int>(got.size()) <= pr.max;
                for (const ChosenTarget& c : got)
                {
                    if (!ok) { break; }
                    if (c.kind != 1 || std::find(pr.legal.begin(), pr.legal.end(), c.index) == pr.legal.end()) { ok = false; break; }
                    const int num = s.battlefield[static_cast<std::size_t>(c.index)].card.m_number;
                    if (std::find(human.begin(), human.end(), num) != human.end()) { ok = false; break; }
                    human.push_back(num);
                }
                if (ok) { picks = human; }
            }
        }
        for (int from : picks)
        {
            chosen.push_back({ P.card.m_number, from, 0 });
            bill = bill.PlusAdditionalCost(cost);
        }
    }
    if (chosen.empty()) { return; }
    // PAY (both worlds, the same payer as the probes; the last probe paid exactly this bill on a copy).
    if (!TapForCostDirect(s, bill, false)) { return; }   // unreachable: the probe of this very bill succeeded
    for (const Pull& c : chosen)
    {
        b.pulls.push_back(c);
        int* from_amt = base_ref(c.from);
        int* on_amt   = base_ref(c.on);
        const int amt = from_amt != nullptr ? *from_amt : 0;
        if (from_amt != nullptr) { *from_amt = 0; }
        if (on_amt != nullptr) { *on_amt += amt; } else { base.push_back({ c.on, amt }); }
        if (g_play_event_sink != nullptr && !TapSpeculatingNow())
        {
            const Permanent* e = FindPerm(s, c.on);
            const Permanent* t = FindPerm(s, c.from);
            const CardDefinition* ed = e != nullptr ? Def(*e) : nullptr;
            EmitPlayEvent(s.turn_number, "redirect",
                          "\xE2\x86\xAA " + (e != nullptr ? e->card.m_name.str() : std::string("en-Kor")) + " "
                          + (ed != nullptr ? ed->params.redirect_pull_cost->ToString() : std::string("{1}{W}"))
                          + ": the " + std::to_string(amt) + " damage " + b.src[0].card.m_name.str() + " would deal to "
                          + (t != nullptr ? t->card.m_name.str() : std::string("a creature"))
                          + (t != nullptr && t->controller_index != controller ? " (opponent's)" : "")
                          + " is dealt to it instead");
        }
    }
}

inline void AddShieldRider(GameState& s, int controller, int dmg, Batch& b)
{
    if (dmg <= 0) { return; }
    std::vector<int> enkors;   // battlefield indices, battlefield order
    bool any_puller = false;
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    {
        const Permanent& q = s.battlefield[i];
        if (!q.card.IsCreature() || q.controller_index != controller) { continue; }
        const CardDefinition* d = Def(q);
        if (d != nullptr && d->params.redirect_shield) { enkors.push_back(static_cast<int>(i)); }
        if (d != nullptr && d->params.redirect_pull_cost.has_value()) { any_puller = true; }
    }
    if (enkors.empty() && !any_puller) { return; }
    const bool ask = g_play_target_chooser != nullptr && !TapSpeculatingNow();   // the viewer (EtbChoosePlayer's test)
    // The damage this sweep deals each creature (m_number, amount), before any replacement; the pulls (step 9)
    // move whole amounts onto their pullers, and the shields below then see the result.
    std::vector<std::pair<int, int>> base;
    for (const DmgAssign& a : b.hits) { if (!a.to.player && a.src == 0) { base.push_back({ a.to.id, a.amount }); } }
    if (any_puller) { AddPullRider(s, controller, b, base, ask); }
    std::vector<std::pair<int, int>> moved;   // (dest m_number, amount) so far
    std::vector<ShieldOpt> prev_opts;
    const Permanent* prev = nullptr;
    std::size_t prev_pos = 0;
    bool prev_f4 = false;
    for (int ei : enkors)
    {
        const Permanent& e = s.battlefield[static_cast<std::size_t>(ei)];
        bool f4 = false;
        const std::vector<ShieldOpt> opts = ShieldOptions(s, e, dmg, moved, f4, &base);
        if (opts.empty()) { continue; }
        int de = 0;   // the damage about to be dealt to E (its sweep damage plus whatever was pulled onto it)
        for (const auto& x : base) { if (x.first == e.card.m_number) { de = x.second; break; } }
        // SYMMETRY (F4 only): an interchangeable en-Kor right after one with the same list starts at its pick.
        std::size_t from = 0;
        if (f4 && prev_f4 && prev != nullptr && prev->card.m_name == e.card.m_name && prev->damage == e.damage
            && LethalToughness(*prev, s) == LethalToughness(e, s)
            && prev->HasKeyword(Keyword::Indestructible) == e.HasKeyword(Keyword::Indestructible)
            && AuraHostDamageBackOn(s, prev->card.m_number) == AuraHostDamageBackOn(s, e.card.m_number)
            && prev_opts.size() == opts.size()
            && std::equal(opts.begin(), opts.end(), prev_opts.begin(),
                          [](const ShieldOpt& a, const ShieldOpt& c) { return a.dest == c.dest && a.amount == c.amount; }))
        { from = prev_pos; }
        std::vector<int> legal;
        for (std::size_t k = from; k < opts.size(); ++k) { legal.push_back(ShieldCode(opts[k].dest, opts[k].amount)); }
        int pick = legal.front();
        if (legal.size() > 1) { pick = PinnedChoice(legal); }   // the viewer, if asking, defaults to it
        const auto [pdest, pamt] = ShieldDecode(pick);
        std::size_t pos = from;
        for (std::size_t k = from; k < opts.size(); ++k) { if (opts[k].dest == pdest && opts[k].amount == pamt) { pos = k; break; } }
        std::vector<std::pair<int, int>> alloc;   // this en-Kor's (dest, amount)
        if (pamt > 0) { alloc.push_back({ pdest, pamt }); }
        if (ask)   // human play: the full allocation through the viewer's `divide` prompt
        {
            ShieldPrompt pr;
            pr.enkor = ei;
            pr.total = de;
            pr.legal.push_back(ei);
            for (std::size_t i = 0; i < s.battlefield.size(); ++i)
            {
                const Permanent& q = s.battlefield[i];
                if (static_cast<int>(i) != ei && q.card.IsCreature() && q.controller_index == e.controller_index)
                { pr.legal.push_back(static_cast<int>(i)); }
            }
            pr.defaults.assign(pr.legal.size(), 0);
            pr.defaults[0] = de - pamt;
            for (std::size_t k = 1; k < pr.legal.size(); ++k)
            { if (s.battlefield[static_cast<std::size_t>(pr.legal[k])].card.m_number == pdest) { pr.defaults[k] = pamt; } }
            const CardDefinition* ed = Def(e);
            if (pr.legal.size() > 1 && ed != nullptr)
            {
                std::vector<ChosenTarget> dflt;
                for (std::size_t k = 0; k < pr.legal.size(); ++k)
                { if (pr.defaults[k] > 0) { dflt.push_back({ 1, pr.legal[k], pr.defaults[k] }); } }
                const ShieldPrompt* saved = t_viewer_shield;
                t_viewer_shield = &pr;
                const auto chosen = (*g_play_target_chooser)(s, *ed, controller, 0, de, dflt);
                t_viewer_shield = saved;
                int sum = 0;
                std::vector<std::pair<int, int>> got;
                bool ok = true;
                for (const ChosenTarget& c : chosen)
                {
                    if (c.kind != 1 || c.amount < 0) { ok = false; break; }
                    if (std::find(pr.legal.begin(), pr.legal.end(), c.index) == pr.legal.end()) { ok = false; break; }
                    sum += c.amount;
                    if (c.index != ei && c.amount > 0)
                    { got.push_back({ s.battlefield[static_cast<std::size_t>(c.index)].card.m_number, c.amount }); }
                }
                if (ok && sum == de) { alloc = got; }
            }
        }
        for (const auto& [dest, amt] : alloc)
        {
            for (int k = 0; k < amt; ++k) { b.shields.push_back({ e.card.m_number, dest }); }
            moved.push_back({ dest, amt });
            if (g_play_event_sink != nullptr && !TapSpeculatingNow())
            {
                const Permanent* t = FindPerm(s, dest);
                EmitPlayEvent(s.turn_number, "redirect",
                              "\xE2\x86\xAA " + e.card.m_name.str() + " {0} x" + std::to_string(amt) + ": the next "
                              + std::to_string(amt) + " damage to it is dealt to "
                              + (t != nullptr ? t->card.m_name.str() : std::string("a creature")) + " instead");
            }
        }
        prev = &e; prev_opts = opts; prev_pos = pos; prev_f4 = f4;
    }
}

// "{T}: This creature deals N damage to itself" (PermAbilityMode::SelfPing; the {T} is already paid by
// the caller). ONE damage event, the Doll both source and recipient, through the shared pipeline, so its
// own reflection (and any later damage watcher) sees it -- in both worlds, which share ApplyPermAbility.
// Not combat damage. Indestructible keeps the Doll alive whatever accumulates (CR 702.12b).
inline void SelfPingAt(GameState& s, int controller, int idx, const CardDefinition& def)
{
    const int n = def.params.self_ping_damage;
    if (n <= 0) { return; }
    const Permanent& me = s.battlefield[static_cast<std::size_t>(idx)];
    Batch b;
    b.src.push_back({ me.card, controller, me.card.IsCreature() });
    b.hits.push_back({ 0, { false, me.card.m_number }, n, false, false });
    if (g_play_event_sink != nullptr && !TapSpeculatingNow())
    {
        EmitPlayEvent(s.turn_number, "damage",
                      "\xE2\x9B\x88 " + me.card.m_name.str() + " deals " + std::to_string(n) + " damage to itself");
    }
    DealDamageBatch(s, b);
}

}   // namespace dmgrx
