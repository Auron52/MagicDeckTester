#pragma once
// CONTROL-EFFECT INJECTION (docs/design/control-inject-harness.md is the interface spec; the design is
// docs/design/control-scoring-mode.md sections 4, 7 and 8). DEFAULT OFF: inert unless a --batch job carries
// "ctrl_inject" (a schedule of effects) or "ctrl_record": true (instrumentation only). With neither, no hook below
// is reached and play is byte-identical.
//
// It disrupts the GOLDFISHING deck as if an opponent's control spell had resolved: a schedule of effects, each
// "<kind>:<rounds>[:<field>...]", ';'-separated. Rounds are the engine's ROUNDS (GameState::turn_number). The
// goldfisher's search is never told in advance; after an effect changes anything, the real game drops its
// committed line and re-searches. Only the REAL game is disrupted, never a mulligan / bottoming trial or a
// rollout on the shared engine (AIEngine::InRolloutForCtrlInject).
//
// Timing (all from the control player's side, who is on the play: its turn of round t precedes the goldfisher's):
//   * board / hand / life / skip / tax effects of round t apply at the START of the goldfisher's turn of round t,
//     after the round counter advances and before its untap step (a sorcery on the control player's turn t);
//     exile_until returns happen at that point too, before that round's new effects;
//   * `tap` applies after the goldfisher's untap step, before its upkeep (Rishadan Port at upkeep);
//   * counters apply as the goldfisher's spell is about to resolve, any time during round t.
// Grammar, every kind's exact semantics and the output formats: docs/design/control-inject-harness.md.
#include <cstdint>
#include <string>
#include <vector>

struct GameState;
struct StackEntry;

namespace ctrlinject
{
enum class Kind : std::uint8_t
{
    Skip,           // the goldfisher takes no turn (an opponent's extra turn / a lock)
    Destroy,        // destroy permanent(s)
    Exile,          // exile permanent(s) for good
    ExileUntil,     // exile permanent(s); they return at the start of round `ret` as new objects
    Bounce,         // return permanent(s) to their owner's hand
    Tap,            // tap permanent(s) after the untap step (a land tapped down)
    Discard,        // discard hand card(s)
    Tax,            // the goldfisher's spells cost `amount` more (generic)
    Life,           // the passive opponent (the control deck) gains `amount` life
    Counter,        // hard counter: the spell goes to its owner's graveyard
    CounterToHand,  // Remand: to its owner's hand
    CounterToTop,   // Memory Lapse: on top of its owner's library
};

// Which objects are candidates. Permanents: the goldfisher's, in battlefield order. Hand: its non-staged hand
// cards, in hand order. Spells: the goldfisher's spells reaching resolution that round, in resolution order.
enum class Class : std::uint8_t { Perm, Land, Creature, Nonland, Artifact, Enchantment, Noncreature, Card };

// How a candidate is chosen. Index = the i-th candidate (0-based); an index at or past the candidate count is a
// no-op logged applied=0, so a sweep over i = 0..count-1 is exact (the clairvoyant target choice).
enum class Sel : std::uint8_t { Index, Big, MvAtLeast, Name, All };

enum class TaxClass : std::uint8_t { All, Noncreature, Creature };

struct Effect
{
    Kind        kind      = Kind::Skip;
    int         from      = 0;   // first round (inclusive)
    int         to        = 0;   // last round (inclusive); == from for a single round
    Class       cls       = Class::Perm;
    Sel         sel       = Sel::Index;
    int         index     = 0;
    int         mv        = 0;
    std::string name;
    int         amount    = 0;   // tax {n} / life L
    int         ret       = 0;   // exile_until: the return round
    TaxClass    tax_class = TaxClass::All;
    std::string text;            // the effect as written, echoed in every log line
};

using Spec = std::vector<Effect>;

// Parse a ctrl_inject string. Throws std::runtime_error naming the bad effect. Empty -> empty spec.
Spec Parse(const std::string& s);
const char* KindName(Kind k);

// ---- per game (the batch worker thread; thread_local state) ------------------------------------------------
// Installed by the batch worker just before RunGame, cleared after FinishGame. `record` = ctrl_record.
void Install(const Spec& spec, bool record, const char* job, int gi, const GameState& state);
void Clear();
bool Armed();   // this game carries effects or recording

struct RoundStart { bool changed = false; bool skip = false; };
// The start of a new round's goldfisher turn (not an extra / free turn), before the untap step.
RoundStart OnRoundStart(GameState& state);
// After the goldfisher's untap step, before its upkeep: `tap` effects. Returns true if anything changed.
bool OnAfterUntap(GameState& state);
// A stack entry is about to resolve (GameEngine::ResolveStack). Returns true if it was countered: the entry has
// been moved to its destination and must not resolve.
bool OnSpellResolving(GameState& state, const StackEntry& entry);
// The end of a goldfisher turn (after the opponent's turn that follows it). `skipped` = the turn was skipped.
void OnTurnEnd(const GameState& state, bool skipped);
// Is a benefit still to come that the goldfisher's search cannot see (an exiled card due back, a tax due to
// end)? While it is, the executor must not end the game on the search's proven_no_win.
bool BenefitPending(const GameState& state);
// The game is over: finalize unmatched counters and print the per-game record (ctrl_record).
void FinishGame(const GameState& state, int win_turn);
}  // namespace ctrlinject
