#pragma once
#include <cstdint>
#include <cstddef>

// The caller's return address, for MTG_INDEX_CARRY_SITES (MSVC has no __builtin_return_address).
#if defined(_MSC_VER)
#include <intrin.h>
#define MTG_RETURN_ADDRESS() _ReturnAddress()
#elif defined(__GNUC__) || defined(__clang__)
#define MTG_RETURN_ADDRESS() __builtin_return_address(0)
#else
#define MTG_RETURN_ADDRESS() nullptr
#endif

// INDEX-CARRY RECORDER -- which card INDICES a game could ever have seen.
//
// USER's idea (2026-10-13): *"keep track of the cards that are relevant (drawn, available) in each
// game and only replay the ones that have changed cards"* ... *"we would do it based on card index,
// so if index 50 is never accessible and that is the one we changed we can skip that and record the
// same result as the run before the change"* ... *"Truly inaccessible."* Design, rulings and the
// soundness argument: docs/design/index-carry-screening.md.
//
// WHAT IT IS. A screen re-measures N games per arm against a base differing by a handful of cards.
// If a changed index was never accessible in game g, base and arm played the IDENTICAL game, so the
// paired delta is EXACTLY 0 -- known, not estimated. The estimator stays byte-identical to the full
// run and `se` is still computed over all N games. Pure compute saving, zero statistical cost.
// The skip test is one instruction: `(accessible & changed) == 0`.
//
// WHY A BITMASK IS ENOUGH. `Library::ShuffleByKey` sorts by a key that is a pure function of
// (seed, m_number), and `inherit_numbering` holds every arm's number set at {1..60}, so the drawn
// INDEX sequence is identical across arms and only the card AT an edited index differs.
// USER: *"we can just store a 64-bit integer (with 60 bit-flags) ... It doesn't need to be anything
// complex."* Maindeck only -- USER: *"I don't think we need the sideboard. Sideboards only represent
// cards reached from tutors in the list"*, and the tutor predicate already forces those games to be
// PLAYED, so a sideboard card never needs a bit in either direction.
//
// OVER-APPROXIMATE, ALWAYS. Any route, zone or effect this does not model counts as ACCESSIBLE.
// The failure direction is asymmetric and vicious: a false "inaccessible" silently substitutes a
// stale result while reporting a successful scoped re-run. That exact shape was already latent in
// the sibling feature (2026-08-12: an empty touched-set is disjoint from every changed-card set, so
// every cell read as provably-untouched). Hence `MarkAll()` on any whole-library read, and hence the
// mandatory verification mode that asserts per-game IDENTITY rather than similar aggregates.
//
// OFF BY DEFAULT AND BYTE-IDENTICAL WHEN OFF: a null thread_local sink plus one
// predicted-not-taken branch, exactly the `RolloutTouch` precedent.
namespace indexcarry
{

// 60 maindeck bits. Bit position is a COMPACTED index, never the raw m_number: `base_numbering`'s
// own docstring warns that m_number "is only ever equality-compared, never an index", and
// `inherit_numbering` hands out fresh numbers above the current high on a genuine size increase.
constexpr int kMaxBits = 64;

struct Sink
{
    // Union over EVERY route and EVERY search branch. Because all branches share the game's
    // shuffle, the union of their library reads is a PREFIX -- setting each read card's own bit
    // represents that prefix exactly, so no separate depth field is needed.
    std::uint64_t accessible = 0;

    // The win-turn relaxation (USER proposal 2026-10-13, APPROVED AS A MEASUREMENT only).
    // `at_turn_start` is `accessible` snapshotted at the start of the current turn, so a consumer
    // can evaluate the relaxed predicates without a second pass. Retrofitting a per-turn snapshot
    // later is the expensive order, which is why it is here from the first build even though the
    // relaxed predicates ship OFF. See the doc's "win-turn relaxation" section for P0/P1/P2 and
    // why the relaxation is WINS-ONLY (on a capped game the unplayed card is exactly what might
    // have won it).
    std::uint64_t at_turn_start = 0;
    std::uint64_t touched_this_turn = 0;

    // Set when something read the library as a WHOLE (an iteration). Such a read makes every card's
    // identity relevant even though none was drawn, so it forces the conservative answer. Counted
    // rather than collapsed to a bool so the diagnostic can say WHICH games lost their carry and
    // how often -- a silent 0% saving is otherwise indistinguishable from the feature being off.
    std::uint64_t whole_library_reads = 0;

    // A tutor was accessible. USER: *"check whether there is a tutor that can find your card (if
    // there is a pruning heuristic that could also prevent it) and then also play the games that
    // have those tutors."* A tutor searches the whole library and so breaks the prefix property.
    bool tutor_seen = false;

    void Mark(int bit)
    {
        if (bit >= 0 && bit < kMaxBits) { accessible |= (1ull << bit); touched_this_turn |= (1ull << bit); }
    }
    void MarkAll()
    {
        ++whole_library_reads;
        accessible = ~0ull;
        touched_this_turn = ~0ull;
    }
    void BeginTurn() { at_turn_start = accessible; touched_this_turn = 0; }
};

// Null unless a run opted in. One load + a predicted-not-taken branch at each call site.
extern thread_local Sink* t_sink;

// m_number -> compact bit. Kept as a plain lookup rather than a map: numbering is dense {1..60} in
// every screen (inherit_numbering frees as many numbers as it adds), but the recorder must not
// ASSUME that, so an out-of-range number degrades to the conservative answer instead of aliasing
// onto another card's bit -- aliasing would be a false "inaccessible", the one unsafe direction.
inline int BitForNumber(std::int32_t number)
{
    const int b = static_cast<int>(number) - 1;
    return (b >= 0 && b < kMaxBits) ? b : -1;
}

inline void NoteNumber(std::int32_t number)
{
    Sink* s = t_sink;
    if (s == nullptr) { return; }
    const int b = BitForNumber(number);
    if (b < 0) { s->MarkAll(); return; }   // unknown numbering -> conservative
    s->Mark(b);
}

// MTG_INDEX_CARRY_SITES -- name the CALL SITES that saturate a game's mask. A whole-library read
// forces the conservative answer, so "acc=ffff... everywhere" is the expected failure mode and the
// only actionable question is WHICH site did it; grepping for `for (... : ...library)` finds dozens
// of candidates and cannot rank them. Prints each distinct return address once, for addr2line.
void NoteWholeLibraryReadSite(const void* ret);

inline void NoteWholeLibraryRead()
{
    Sink* s = t_sink;
    if (s == nullptr) { return; }
    s->MarkAll();
    NoteWholeLibraryReadSite(MTG_RETURN_ADDRESS());
}

}   // namespace indexcarry
