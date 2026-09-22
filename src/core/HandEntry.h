#pragma once
#include "GameState.h"
#include "EnvFlags.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

// ================================================================================================
// ONE CHOKE POINT FOR EVERY CARD THAT ENTERS A HAND
// ================================================================================================
//
// WHY THIS EXISTS. A breakpoint exists so the turn can be re-decided once new castable material
// appears. The engine used to decide whether to arm one by asking **"what card was CAST?"** -- a
// hand-maintained taxonomy of CAUSES, one bit per card class (DrawUntilNonland, cascade_max_mv,
// stages_cards, impulse_exile, tutor_to_hand, etb_dig_count, ...). The thing it approximates -- "a
// card became available mid-phase" -- is an EFFECT. Every time the two drift apart a line becomes
// unreachable at ANY depth or budget, and the failure is SILENT: the game still plays a sensible
// turn, and no win-turn average says "we drew a card with mana open and did not cast it."
//
// USER, 2026-09-19: *"So that way there is no question about when the breakpoints should open. It
// is exactly when there are NEW OPTIONS TO CONSIDER"* -- *"either hand status or ability status has
// changed"*. This header is the HAND half of that rule. (The ability half is the delta rule in
// TurnSolver::PostEntryActivationPending, MTG_BP_ABILITY_DELTA.)
//
// THE STRUCTURAL REASON THE TAXONOMY DRIFTED, and what this fixes: `hand.push_back(...)` was
// open-coded at 30 sites and no `EnterHand()` helper existed, so there was nowhere for the question
// to be asked once. Every new card that put a card in hand by a new route needed a human to
// remember to extend a whitelist in two files. Routing every site through here means the observation
// happens whether or not anyone remembered.
//
// SEE docs/design/breakpoints-should-key-on-hand-entry.md for the audit that motivates it -- all 30
// sites enumerated and classified, and the live instances (Goblin Lackey putting a Goblin Matron
// into play, whose ETB tutors into a hand nothing was watching).
//
// ------------------------------------------------------------------------------------------------
// WHAT THIS HEADER IS AND IS NOT
// ------------------------------------------------------------------------------------------------
//
// It is an OBSERVER, not a policy. `EnterHand` appends the card exactly as the open-coded
// `push_back` did -- the two overloads below preserve move-vs-copy at every call site, so this step
// is byte-identical on its own -- and additionally bumps a thread-local sequence number. Deciding
// what to DO about a hand entry stays where it already lives (TurnSolver's arming sites, AIEngine's
// second pass); this only makes the fact observable from one place.
//
// It does NOT live in GameState. The sequence is deliberately thread-local and outside the state:
// folding it into the state would change every state key, every dominance fold and every sim-key
// digest, for an instrument that no rule reads.
//
// ------------------------------------------------------------------------------------------------
// HOW THE SEQUENCE IS MEANT TO BE CONSUMED -- read this before writing a new consumer
// ------------------------------------------------------------------------------------------------
//
// `g_hand_entry_seq` is a MONOTONE COUNTER, and the only sound question to ask of it is
// *"is it the same value it was at point S?"*. Two properties make that safe and one makes the
// obvious stronger reading unsafe:
//
//   SOUND  -- `seq == seq_at_S` proves NO new material entered the active player's hand on this
//             thread since S. So it is a valid CHEAP PRE-FILTER in front of the exact test.
//   UNSOUND-- `seq != seq_at_S` does NOT prove this state's hand gained a card. The rollout
//             speculates on COPIES of GameState on the same thread, and an entry into a copy bumps
//             the same counter. So a changed sequence means "ask the exact question", never "arm".
//
// The exact question is `TurnSolver::HandGainedACard(before, state)`, a CONTENT diff on the live
// state, which is immune to speculation by construction because it reads the state object it was
// snapshotted from. The pairing is deliberate: the counter makes the common no-entry case O(1)
// instead of O(hand x snapshot), and the diff keeps the answer exact. Do not "optimise" the diff
// away.
//
// ------------------------------------------------------------------------------------------------
// WHICH ENTRIES COUNT AS NEW MATERIAL
// ------------------------------------------------------------------------------------------------
//
// Three reasons are deliberately excluded from the sequence, because arming on them would be arming
// where the option set did not change:
//
//   Opening     -- the opening hand and every mulligan redraw are game setup: there is no plan yet
//                  to re-decide. Routed anyway so the inventory of hand-entry routes is complete
//                  and auditable in one place, which is the whole point of a choke point.
//   DrawStep    -- the turn's draw happens BEFORE the main phase, so every plan is already
//                  enumerated against the drawn card. Arming here would re-solve a turn nobody had
//                  begun. (The audit's table has always had this row as "n/a".)
//   StagedMerge -- `staged_cards -> hand` at the top of a phase is BOOKKEEPING, not acquisition.
//                  The card was already castable as a staged card on the turn it was staged; moving
//                  it between two representations of the same availability is not a new option.
//
// Every other reason is new material. The enum is for the census and for future provider pruning
// (USER: *"Activated ability breakpoints could potentially be pruned by a provider heuristic"*);
// the arming decision reads only the `IsNewMaterial` split, so adding a reason cannot silently
// change play.
//
// ONLY THE ACTIVE PLAYER'S HAND bumps the sequence. `HandGainedACard` reads the active player's
// hand, so counting an opponent-side entry would create a pre-filter miss with no matching exact
// test -- a false "ask again", harmless but pointless. The goldfish opponent never reaches these
// routes today; the guard is so that it stays correct if one ever does.
// ================================================================================================

enum class HandEntryReason : uint8_t
{
    Opening,       // the opening hand / a mulligan redraw -- game setup; NOT new material
    DrawStep,      // the turn's draw step (CR 121) -- precedes the main phase; NOT new material
    Draw,          // a CR-121 draw during a phase: a cantrip, a cycle/sac dig, an ETB draw
    Reveal,        // reveal-and-put-into-hand (Treasure Hunt, Scrying Sheets, a declined free cast)
    Tutor,         // searched out of the library into hand
    Dig,           // look at the top N, take one (the rest bottomed / exiled)
    Stage,         // impulse / staged exile that lives in `hand` flagged m_is_staged
    Bounce,        // a permanent returned to its owner's hand
    Recur,         // graveyard to hand
    StagedMerge,   // bookkeeping: staged_cards -> hand at a phase boundary; NOT new material
    Count
};

inline const char* HandEntryReasonName(HandEntryReason r)
{
    switch (r)
    {
        case HandEntryReason::Opening:     return "opening     (opening hand / mulligan -- excluded)";
        case HandEntryReason::DrawStep:    return "draw_step   (the turn's draw -- excluded)";
        case HandEntryReason::Draw:        return "draw        (cantrip / dig / ETB draw)";
        case HandEntryReason::Reveal:      return "reveal      (Treasure Hunt / snow / free-cast decline)";
        case HandEntryReason::Tutor:       return "tutor       (library -> hand)";
        case HandEntryReason::Dig:         return "dig         (look at top N, take one)";
        case HandEntryReason::Stage:       return "stage       (impulse / staged exile)";
        case HandEntryReason::Bounce:      return "bounce      (permanent -> owner's hand)";
        case HandEntryReason::Recur:       return "recur       (graveyard -> hand)";
        case HandEntryReason::StagedMerge: return "staged_merge(bookkeeping -- excluded)";
        case HandEntryReason::Count:       break;
    }
    return "?";
}

// See "WHICH ENTRIES COUNT AS NEW MATERIAL" above. This is the ONLY thing the arming path reads
// off the reason, deliberately -- so adding a reason cannot change play by accident.
inline bool HandEntryIsNewMaterial(HandEntryReason r)
{
    return r != HandEntryReason::Opening
        && r != HandEntryReason::DrawStep
        && r != HandEntryReason::StagedMerge;
}

// THE SEQUENCE. Thread-local; see the consumption rules above. Never folded into a state key.
inline thread_local uint32_t g_hand_entry_seq = 0;

namespace handentry
{
    // MTG_HAND_ENTRY_CENSUS=1 -- "size the hole first" (the design doc's own discipline): how often
    // each route actually fires, per deck, so the scope of the arming change is a number rather
    // than an argument. Diagnostic only; the counters are atomics read by nothing but the report.
    inline bool CensusOn()
    {
        static const bool on = EnvOn("MTG_HAND_ENTRY_CENSUS");
        return on;
    }

    // Nesting depth of cast applies on this thread; see HandEntryCastScope below.
    inline thread_local int g_in_cast_apply = 0;

    struct Census
    {
        std::atomic<uint64_t> n[static_cast<int>(HandEntryReason::Count)]{};
        std::atomic<uint64_t> in_cast[static_cast<int>(HandEntryReason::Count)]{};
        ~Census()
        {
            if (!CensusOn()) { return; }
            uint64_t total = 0, material = 0, mat_in = 0;
            for (int i = 0; i < static_cast<int>(HandEntryReason::Count); ++i)
            {
                const uint64_t v = n[i].load(std::memory_order_relaxed);
                total += v;
                if (HandEntryIsNewMaterial(static_cast<HandEntryReason>(i)))
                { material += v; mat_in += in_cast[i].load(std::memory_order_relaxed); }
            }
            if (total == 0) { return; }
            std::fprintf(stderr,
                         "[hand-entry] %llu entries; %llu new material, of which %llu inside a cast"
                         " apply and %llu OUTSIDE it (%.2f%% -- the unarmed hole)\n",
                         static_cast<unsigned long long>(total),
                         static_cast<unsigned long long>(material),
                         static_cast<unsigned long long>(mat_in),
                         static_cast<unsigned long long>(material - mat_in),
                         material ? 100.0 * static_cast<double>(material - mat_in)
                                          / static_cast<double>(material) : 0.0);
            std::fprintf(stderr, "[hand-entry] %-52s %12s %12s %12s\n",
                         "route", "total", "in-cast", "OUTSIDE");
            for (int i = 0; i < static_cast<int>(HandEntryReason::Count); ++i)
            {
                const uint64_t v = n[i].load(std::memory_order_relaxed);
                if (v == 0) { continue; }
                const uint64_t c = in_cast[i].load(std::memory_order_relaxed);
                std::fprintf(stderr, "[hand-entry]   %-50s %12llu %12llu %12llu\n",
                             HandEntryReasonName(static_cast<HandEntryReason>(i)),
                             static_cast<unsigned long long>(v),
                             static_cast<unsigned long long>(c),
                             static_cast<unsigned long long>(v - c));
            }
        }
    };
    inline Census g_census;

    inline void Note(const GameState& state, int player_index, HandEntryReason why)
    {
        if (player_index == state.active_player_index && HandEntryIsNewMaterial(why))
        { ++g_hand_entry_seq; }
        if (CensusOn())
        {
            g_census.n[static_cast<int>(why)].fetch_add(1, std::memory_order_relaxed);
            if (g_in_cast_apply > 0)
            { g_census.in_cast[static_cast<int>(why)].fetch_add(1, std::memory_order_relaxed); }
        }
    }
}

// THE SPLIT THAT SIZES THE HOLE. Today's arming can only see a hand entry that happens INSIDE a
// cast's apply window: both the param-keyed sites and the general site-10 rule hang off a
// before/after comparison bracketing one cast (`hand_at_cast` in the rollout, `rdb_hand` in the
// executor). An entry outside that window -- a combat-damage trigger, an activated ability, a death
// trigger, an upkeep put -- arms NOTHING, at any depth or budget.
//
// So `total - in_cast` is the size of the remaining hole, per route, per deck, and it is the number
// that decides how much of step 2 is worth building. The design doc asks for exactly this before
// the refactor ("Size the hole first ... a counter at the unarmed routes, run over all suite decks,
// says which of them actually fire and how often. That decides whether this is a Goblins fix or an
// everything fix"). Diagnostic only, and gated, so a ship config never touches the counter.
struct HandEntryCastScope
{
    const bool on;
    HandEntryCastScope() : on(handentry::CensusOn()) { if (on) { ++handentry::g_in_cast_apply; } }
    ~HandEntryCastScope()                            { if (on) { --handentry::g_in_cast_apply; } }
    HandEntryCastScope(const HandEntryCastScope&) = delete;
    HandEntryCastScope& operator=(const HandEntryCastScope&) = delete;
};

// THE CHOKE POINT. Two overloads rather than a by-value parameter so that a call site that moved
// still moves and one that copied still copies -- this step has to be byte-identical AND free.
inline void EnterHand(GameState& state, int player_index, Card&& card, HandEntryReason why)
{
    handentry::Note(state, player_index, why);
    state.players[player_index].hand.push_back(std::move(card));
}

inline void EnterHand(GameState& state, int player_index, const Card& card, HandEntryReason why)
{
    handentry::Note(state, player_index, why);
    state.players[player_index].hand.push_back(card);
}

// THE SECOND ROUTE, and the reason the design doc's audit undercounted: `Library::DrawN` appends
// straight into the destination vector, so `p.library.DrawN(n, p.hand)` puts cards in a hand
// WITHOUT ever writing `hand.push_back`. A grep for the append therefore misses it -- and it is not
// a rare shape: it is how nearly every cast_draw / ETB-draw / trigger-draw in the engine draws.
// Any future hand-entry route must land in one of these two functions or it is invisible again.
//
// Returns what DrawN returns (cards actually drawn -- fewer than n on an empty library), because
// several call sites add it to cards_drawn_this_turn.
inline int DrawIntoHand(GameState& state, int player_index, int n, HandEntryReason why)
{
    Player& p = state.players[player_index];
    const int drew = p.library.DrawN(n, p.hand);
    for (int i = 0; i < drew; ++i) { handentry::Note(state, player_index, why); }
    return drew;
}
