#pragma once

// End-of-turn STATE DOMINANCE -- the sound core of docs/design/eot-dominance-pruning.md.
//
// ============================================================================================
// STATUS: NOT ADOPTED (user decision, 2026-08-15). BOTH FLAGS DEFAULT OFF; this file is DORMANT.
//
// It passed every gate -- must-find 12/12 decks (zero wins lost at unbounded budget), train and
// held-out regression A/Bs (0 and 2 slower, the latter both classified `churn`), clean-env smoke
// byte-identical. It was not adopted because the BENEFIT DOES NOT APPEAR IN THE CONDITIONS THIS
// PROJECT ACTUALLY RUNS IN:
//   * at a FIXED per-decision budget (what every real workload uses -- 10/20/40/80/200/3000 ms)
//     the search spends its budget either way, so the prune can only show up as better quality,
//     and that measured at noise level (net -0.0002 turns across both seed sets);
//   * at UNBOUNDED budget it is a genuine 1.59x on fivecolour (39.2% fewer states, deterministic)
//     -- but nothing in production searches unbounded, so that speedup is unreachable in practice;
//   * on mirrorwing, the Class B monster that MOTIVATED the whole design, it saves 0.15% of the
//     tree and costs ~5%.
// Do not re-enable on the strength of the passing gates alone -- re-measure the benefit first.
// ============================================================================================
//
// !!! MAINTENANCE HAZARD -- READ BEFORE ADDING A FIELD TO Permanent / Player / GameState !!!
//
// The "fails closed" discipline below is enforced by the AUTHOR, not by the compiler. Build()
// folds an EXPLICIT list of fields, so a NEWLY ADDED field is not "undeclared -> exact match" --
// it is INVISIBLE, which is fail-OPEN: two states differing only in the new field compare EQUAL
// or DOMINATING, and the prune deletes a reachable line. The static_asserts below are a tripwire
// for exactly that: if one fires, a struct grew, and you must classify every new field into one
// of the three categories (directional axis / exact match / boundary assertion) and fold it in
// Build() before updating the expected size. Do NOT just bump the number.
//
// This hazard is dormant while the flags are off, and it is the main reason re-enabling this file
// is not free.
//
// Two states reached by different lines of the SAME decision, at the SAME end-of-turn boundary,
// with the SAME draws consumed, can be ordered: if A holds every resource B holds and more, then
// under goldfish rules (the opponent never attacks and never blocks, so no resource can be turned
// against us) every future available to B is available to A. B can therefore be dropped without
// dropping a strictly-earlier win. That is the whole claim, and it is why this file is so
// conservative about what counts as "every resource".
//
// ONE comparator, TWO consumers. `MTG_DOM_CENSUS` buckets siblings and prunes nothing (pricing);
// `MTG_DOM_PRUNE` drops the dominated ones. They share this code deliberately: the first pricing
// (2026-08-15) was measured by a probe whose board axis never executed -- `aura_attached_to >= 0`
// is always true, so every permanent fell into the fail-closed exact bucket and the reported
// floors were hand-subset-plus-life only. A census that can measure something the prune does not
// use is worse than no census, so there is exactly one implementation.
//
// SOUNDNESS DISCIPLINE (the reason for the shape of DomSnap):
//   * EVERY field of Permanent / Player / GameState is either a DECLARED DIRECTIONAL AXIS, an
//     EXACT-MATCH field, or a BOUNDARY ASSERTION. There is no fourth category and nothing is
//     skipped -- an omitted field is how identity keys grow holes (the 2026-08-14 storage-counter
//     find), and a hole in a PRUNE silently deletes reachable wins rather than merely colliding
//     two memo entries.
//   * Anything undeclared fails closed to exact match, so the failure mode of forgetting a field
//     is lost prune REACH, never a lost line.
//   * The boundary is ENFORCED, not assumed (the doc's hard rule): mid-turn state has no
//     monotonicity argument at all, so a state carrying floats / storm / until-EOT effects is
//     refused outright rather than compared.
//
// Directions come from the deck's DecisionProvider (generic monotone table in the base class,
// per-deck overrides in the archetype) -- see DecisionProvider.h's DomAxis / DomDir.

#include "../core/GameState.h"
#include "DecisionProvider.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// TRIPWIRE for the maintenance hazard documented at the top of this file. Build() folds an
// explicit field list, so a struct that GROWS silently gains a field this comparator cannot see --
// fail-OPEN, i.e. a deleted reachable line rather than merely lost reach. These asserts turn that
// silent unsoundness into a BUILD FAILURE. When one fires: classify each new field as a directional
// axis, an exact-match field, or a boundary assertion; fold it into Build(); THEN update the size.
// Bumping the number without doing that re-opens the hole these asserts exist to close.
//
// Sizes are a proxy, not a proof (a same-size field swap slips through, and padding is
// ABI-dependent), so they are a prompt to re-check, not a guarantee of correctness.
static_assert(sizeof(Permanent) == 256,
              "Permanent changed size -- fold any new field into dominance::Build() (see the "
              "MAINTENANCE HAZARD note at the top of Dominance.h) before updating this number.");
static_assert(sizeof(Player) == 160,
              "Player changed size -- fold any new field into dominance::Build() (see the "
              "MAINTENANCE HAZARD note at the top of Dominance.h) before updating this number.");
// 688 -> 696 (2026-09-01): ManaPool gained `wild_c`, and GameState embeds one as floating_mana.
// Classification: NOT a new axis and NOT a new exact-match field -- it is a SUBSET COUNT of
// ManaPool::wild, so wild_c > 0 implies wild > 0 implies floating_mana.Total() > 0, which the
// boundary assertion at :285 below already stands the whole comparator down on. Nothing to fold.
static_assert(sizeof(GameState) == 696,
              "GameState changed size -- fold any new field into dominance::Build() (see the "
              "MAINTENANCE HAZARD note at the top of Dominance.h) before updating this number.");

namespace dominance
{

// 64-bit mix used for every exact-match fold. Not a security hash: it only has to separate
// distinct field tuples, and a collision here would merge two states -- so it is a full
// avalanching mix rather than a cheap xor-sum.
inline std::uint64_t Mix(std::uint64_t a, std::uint64_t b)
{
    a ^= b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2);
    a ^= a >> 30; a *= 0xBF58476D1CE4E5B9ull;
    a ^= a >> 27; a *= 0x94D049BB133111EBull;
    a ^= a >> 31;
    return a;
}

// --- learned-model zone readers ---------------------------------------------------------------
// The graveyard/exile axes cannot be settled from the decklist alone: ExtractMidGameFeatures feeds
// GraveyardSize and ExileSize into the learned eval / value models, and UseValueModel() is adopted
// default-ON, so an attached model that BRANCHES on either feature makes that zone future-
// determining for a deck whose cards never touch it.
//
// "Branches on" is exactly checkable, and it is per-model rather than per-deck-name: a GBDT reads a
// feature only where some tree SPLITS on it (or a linear coef is nonzero). Measured across the
// twelve shipped sidecars, seven split on graveyard_size (antilife, dragonstorm, fivecolour, hinata,
// mirrorwing, burn, th) and five do not (auras, creature_giving, goblins, knights, slivers); NONE
// splits on exile_size. So this check is what lets goblins keep the graveyard-ignored reach while
// hinata correctly does not.
//
// The mask is computed ONCE where the models are attached and carried on GameState
// (m_model_feat_mask); see that field for why it is stamped rather than memoised on the pointers.
inline std::uint64_t ModelFeatureMask(const MidGameEvaluator* m)
{
    if (m == nullptr) { return 0; }
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < m->coefs.size() && i < 64; ++i)
    { if (m->coefs[i] != 0) { bits |= (1ull << i); } }
    for (const std::vector<MidGameTreeNode>& t : m->trees)
    {
        for (const MidGameTreeNode& n : t)
        { if (n.feature >= 0 && n.feature < 64) { bits |= (1ull << n.feature); } }
    }
    return bits;
}

// Does either attached model branch on `f`? Both are counted whenever attached, without consulting
// UseValueModel()/UseLearnedEval(): the conservative reading costs only reach, and the value leaf is
// default-ON anyway.
inline bool ModelReadsFeature(const GameState& s, MidGameFeature f)
{
    const int i = static_cast<int>(f);
    return i >= 0 && i < 64 && ((s.m_model_feat_mask >> i) & 1ull) != 0;
}

// A permanent's directional axes, NORMALISED so that BIGGER always dominates: a FewerDominates
// axis is stored negated. That normalisation is what lets domination be a plain componentwise
// `>=` and lets the group matcher below sort. EqualRequired axes are NOT here -- they are folded
// into DomPerm::match instead, which is the same statement expressed as an exact-match field.
using DomVec = std::array<std::int32_t, static_cast<std::size_t>(DomAxis::_Count) + 2>;
// The two trailing slots beyond DomAxis are engine-owned (never provider-overridable) because
// they are monotone under the rules themselves rather than per deck:
constexpr std::size_t kVecTapped = static_cast<std::size_t>(DomAxis::_Count);      // untapped dominates
constexpr std::size_t kVecSick   = static_cast<std::size_t>(DomAxis::_Count) + 1;  // not-sick dominates

struct DomPerm
{
    std::uint64_t match = 0;   // every exact-match field of this permanent, folded
    DomVec        v{};         // normalised directional axes (bigger dominates)
};

// True if `a` dominates-or-equals `b` on every normalised axis.
inline bool VecCovers(const DomVec& a, const DomVec& b)
{
    for (std::size_t i = 0; i < a.size(); ++i) { if (a[i] < b[i]) { return false; } }
    return true;
}

// One (match-key) group of permanents: the members are interchangeable apart from their axes.
struct DomGroup
{
    std::uint64_t       match = 0;
    std::vector<DomVec> members;   // sorted ascending, so the matcher can align tops
};

// A comparable snapshot of one end-of-turn state.
struct DomSnap
{
    // False when the state is NOT at a clean end-of-turn boundary, or carries something this
    // comparator refuses to reason about. Such a snapshot compares with NOTHING -- not even
    // itself -- so an unsupported state can never be pruned and can never prune another.
    bool comparable = false;

    // Caller-supplied identity key (the canon sim key). Used ONLY by the census's "identical"
    // bucket, for continuity with the first pricing; the dominance relation never reads it.
    std::uint64_t ident_h1 = 0, ident_h2 = 0;

    std::uint64_t exact = 0;   // every exact-match scalar / zone, folded into one value

    // The rollout win turn this state actually scored, filled in by the caller AFTER the rollout.
    // Census diagnostics only (the `harm` counter): -1 = not yet scored.
    int win_turn = -1;

    int life_self = 0;         // MORE dominates
    int life_opp  = 0;         // FEWER dominates

    std::vector<std::uint64_t> hand;    // sorted; SUPERSET dominates (holding a card is optional)
    std::vector<DomGroup>      ours;    // our battlefield, grouped by match key
    std::vector<DomGroup>      theirs;  // opponent's battlefield, grouped by match key
    DomDir                     opp_dir = DomDir::EqualRequired;   // how `theirs` compares
};

// --- group matching -------------------------------------------------------------------------
//
// Within one match group the members differ only in their directional axes, so "A's group covers
// B's group" is an injective assignment of each B member to a distinct dominating A member -- a
// bipartite matching under a partial order. The matcher below is GREEDY over both sides sorted
// ascending, aligned at the top: it can only FAIL to find an assignment that exists, never invent
// one, so it under-reports dominance and stays sound. On a single directional axis (the realistic
// case -- two Sandstone Needles at different depletion counts) sorted-aligned greedy is exact.
inline bool GroupCovers(const std::vector<DomVec>& a, const std::vector<DomVec>& b)
{
    if (a.size() < b.size()) { return false; }
    const std::size_t off = a.size() - b.size();   // align B against A's largest |B| members
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        if (!VecCovers(a[i + off], b[i])) { return false; }
    }
    return true;
}

// Does side `a` cover side `b` under `dir`? MoreDominates: a superset (per group, per axis).
// FewerDominates: the mirror. EqualRequired: exact multiset equality.
inline bool SideCovers(const std::vector<DomGroup>& a, const std::vector<DomGroup>& b, DomDir dir)
{
    if (dir == DomDir::FewerDominates) { return SideCovers(b, a, DomDir::MoreDominates); }
    if (dir == DomDir::EqualRequired)
    {
        if (a.size() != b.size()) { return false; }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].match != b[i].match || a[i].members != b[i].members) { return false; }
        }
        return true;
    }
    // MoreDominates: every group of b must be covered by the same-keyed group of a. Extra groups
    // in a are fine -- that is what "more" means. Both are sorted by match key.
    std::size_t i = 0;
    for (const DomGroup& gb : b)
    {
        while (i < a.size() && a[i].match < gb.match) { ++i; }
        if (i >= a.size() || a[i].match != gb.match)              { return false; }
        if (!GroupCovers(a[i].members, gb.members))               { return false; }
        ++i;
    }
    return true;
}

// Does A dominate-or-equal B? Mutual coverage means the two states are equivalent for our
// purposes; A-covers-B without the reverse is strict dominance.
inline bool Covers(const DomSnap& A, const DomSnap& B)
{
    if (!A.comparable || !B.comparable) { return false; }
    if (A.exact != B.exact)             { return false; }
    if (A.life_self < B.life_self)      { return false; }
    if (A.life_opp  > B.life_opp)       { return false; }

    // Hand: B's multiset must be contained in A's (both sorted). A card in hand is never a
    // liability in a goldfish -- it is an option we may decline -- so a superset dominates. This
    // is also what makes a WASTED cast prunable: the line that held the spell is a superset of
    // the line that cast it for no board change.
    {
        std::size_t i = 0;
        for (std::uint64_t h : B.hand)
        {
            while (i < A.hand.size() && A.hand[i] < h) { ++i; }
            if (i >= A.hand.size() || A.hand[i] != h)  { return false; }
            ++i;
        }
    }

    if (!SideCovers(A.ours, B.ours, DomDir::MoreDominates)) { return false; }
    if (!SideCovers(A.theirs, B.theirs, A.opp_dir))         { return false; }
    return true;
}

// --- snapshot construction ------------------------------------------------------------------

// Is the state at a CLEAN end-of-turn boundary? The doc's hard rule: dominance cannot be soundly
// judged mid-turn, because every monotonicity argument here assumes the ephemeral state has washed
// out. Enforced, not assumed -- these must all hold at the post-SimulateEndAndStartNextTurn point,
// and the one that realistically can NOT is floating mana (an echo cost paid off a lumpy source
// leaves float behind when UpkeepFloatClearEnabled is off), which is exactly why it is checked.
inline bool AtCleanBoundary(const GameState& s)
{
    if (!s.stack.empty())                     { return false; }
    if (s.floating_mana.Total() > 0)          { return false; }
    // NOTE the line that is NOT here: `spells_cast_this_turn != 0`. It was an assertion at first
    // and cost dragonstorm 26.5% of its siblings, because CastOffSuspend counts a Lotus Bloom
    // arriving at THIS upkeep as a cast (CR 702.62e) -- so storm is legitimately 1 at a perfectly
    // clean boundary. The distinction this function draws is therefore: an assertion covers state
    // that must have WASHED OUT for the monotonicity argument to hold at all (the stack, floats,
    // until-EOT permanent state); a turn-scoped COUNTER that is legitimately nonzero is an
    // exact-match field instead (see Build), which still compares two siblings that share it.
    for (const Permanent& p : s.battlefield)
    {
        // Until-end-of-turn state that cleanup is supposed to have cleared. If any survives, the
        // state is not where we think it is and no comparison is safe.
        if (p.damage != 0 || p.pending_death_trigger != 0)          { return false; }
        if (p.temp_power_bonus != 0 || p.temp_tough_bonus != 0)     { return false; }
        if (p.temp_haste || p.is_animated || p.exile_at_end)        { return false; }
        if (p.marked_for_destruction)                               { return false; }
    }
    return true;
}

// Build the comparable snapshot. `ident_h1/h2` is the caller's canon sim key (census bucket only).
inline DomSnap Build(const GameState& s, const DecisionProvider& prov,
                     std::uint64_t ident_h1 = 0, std::uint64_t ident_h2 = 0)
{
    DomSnap n;
    n.ident_h1 = ident_h1;
    n.ident_h2 = ident_h2;
    if (!AtCleanBoundary(s)) { return n; }   // comparable stays false

    const int me  = s.active_player_index;
    const int opp = 1 - me;

    // Resolve the deck's axis directions once per snapshot (a switch per axis, not per permanent).
    std::array<DomDir, static_cast<std::size_t>(DomAxis::_Count)> dir{};
    for (std::size_t i = 0; i < dir.size(); ++i)
    {
        dir[i] = prov.DominanceAxisDirection(static_cast<DomAxis>(i));
    }
    n.opp_dir = prov.DominanceOpponentBoard();

    // ---- exact-match scalars and zones -------------------------------------------------------
    std::uint64_t e = 0x0D0117ull;
    auto fold = [&e](std::uint64_t v) { e = Mix(e, v); };

    fold(static_cast<std::uint64_t>(s.turn_number));
    fold(static_cast<std::uint64_t>(s.active_player_index));
    fold(static_cast<std::uint64_t>(s.priority_player_index));
    fold(static_cast<std::uint64_t>(s.phase));
    fold(static_cast<std::uint64_t>(s.step));
    fold(static_cast<std::uint64_t>(s.consecutive_passes));
    fold(s.player_lost_on_draw ? 1u : 0u);
    // Deck-out is terminal, so two states differing here are not comparable. Guarded on the deck
    // having an opponent library at all: an unconditional fold would shift every key for every
    // deck and give up byte-identity to record a field that is permanently false for them.
    if (s.opponent_library_dealt) { fold(s.opponent_decked ? 0xD3CDull : 0xD3CEull); }
    fold(s.on_the_play ? 1u : 0u);
    fold(s.opponent_lost_life_this_turn ? 1u : 0u);
    fold(static_cast<std::uint64_t>(s.vial_target_mv));
    fold(s.deck_feeds_combat ? 1u : 0u);
    fold(s.search_count);
    // Turn-scoped counters that are legitimately nonzero at a clean boundary (see AtCleanBoundary):
    // storm from a Lotus Bloom arriving off suspend, and the per-turn pins. Exact-match rather than
    // asserted, so two siblings sharing the same value still compare.
    fold(static_cast<std::uint64_t>(s.spells_cast_this_turn));
    fold(static_cast<std::uint64_t>(s.casts_remaining_this_turn));
    fold(static_cast<std::uint64_t>(s.free_casts_available));
    fold(static_cast<std::uint64_t>(s.scripted_cheat_choice));
    fold(static_cast<std::uint64_t>(s.scripted_discard_choice));
    // scripted_vial_charge is LIVE across the end-of-turn boundary by design (set during the
    // turn's apply, consumed at the NEXT turn's upkeep -- see its GameState note), so a pending
    // searched charge is future-determining and must fold exact-match like its sibling pins.
    fold(static_cast<std::uint64_t>(s.scripted_vial_charge));
    // Post-combat productivity markers (GameState::hand_size_at_combat). Turn-scoped scratch: reset
    // to -1 at turn start, stamped by SimulateCombat, and read ONLY by the post-combat main -- so at
    // a clean END-OF-TURN boundary they carry no future value and could defensibly be ignored.
    // Folded EXACT anyway, per this file's "there is no fourth category" discipline: the cost of
    // exact-matching a dead field is lost prune REACH, and the cost of guessing wrong about which
    // fields are dead is a deleted reachable line. Cheap to revisit if this file is ever adopted
    // and the reach loss shows up in a census.
    fold(static_cast<std::uint64_t>(s.hand_size_at_combat));
    fold(static_cast<std::uint64_t>(s.battlefield_at_combat));
    // ZONE OBSERVABILITY (graveyard / exile). Conditional, exactly like the storm counter: on for
    // the cards, decks and models that can read the zone, IGNORED otherwise -- because most cards
    // cannot read either one (USER, 2026-08-15). And what matters is the TYPE of cards in the
    // graveyard, not the zone as a whole (USER: "is there Throes of Chaos (retrace) in TH, or
    // fetchlands (for DRS) in fivecolour?"), so this folds a PROJECTION rather than the contents:
    //   * deck_gy_readers   -- GyReader bits from an ENGINE-side audit of every graveyard-reading
    //     site (GoldFishRunner::DeckGraveyardReaders). Deathrite is why fivecolour observes type
    //     counts (USER, 2026-08-15); retrace is why th observes its Throes/Flame Jab names;
    //   * ModelReadsFeature -- the attached learned model branching on graveyard_size / exile_size.
    //     GraveyardSize is the ONLY graveyard-derived feature in the whole set, so a model observes
    //     the SIZE and nothing else -- never which cards are there.
    // Both are future-stable (a deck constant and a per-game constant), which is what makes
    // ignoring the unobserved part SOUND: a per-STATE test would not be, since a card type that is
    // unreadable now can become readable later in the same line.
    //
    // The selectors are themselves folded, so two states are only compared when they AGREE about
    // what is observable -- a disagreement fails closed rather than comparing on mixed rules.
    const std::uint32_t gy_bits  = s.deck_gy_readers;
    const bool          gy_size  = ModelReadsFeature(s, MidGameFeature::GraveyardSize);
    // Nothing in the engine reads state.exile's CONTENTS -- only its size, and only as a model
    // feature (audited 2026-08-15; Apex/Light-Up staging lives in Player::staged_cards, suspend in
    // suspended_cards, and both of those are folded unconditionally below).
    const bool exile_live = ModelReadsFeature(s, MidGameFeature::ExileSize);
    fold(0x11A0ull + gy_bits);
    fold(gy_size ? 0x11Aull : 0x11Bull);
    fold(exile_live ? 0x21Aull : 0x21Bull);
    if (exile_live)
    {
        // ORDER-INSENSITIVE (size + commutative content sum): two lines can sweep the same tokens
        // in different battlefield order, and an ordered fold would call those states incomparable
        // for a difference no future can observe.
        fold(static_cast<std::uint64_t>(s.exile.size()));
        std::uint64_t acc = 0;
        for (const Card& c : s.exile) { acc += c.m_name_hash; }
        fold(acc);
    }
    // NOT folded, deliberately: m_card_scores -- and this classification covers the whole family of
    // borrowed profile pointers it joins (m_provider, m_required_pieces, m_evaluator,
    // m_value_model). Each is stamped once per game from the deck's profile and is then the SAME
    // pointer in every state of that game, siblings included, so it cannot distinguish two futures
    // and folding it would fold a constant. What the pointed-to data DOES to play is already visible
    // in the fields it moves (a different discard leaves a different hand, which is folded).
    // NOT folded, deliberately: next_token_number. It only hands out ids to FUTURE tokens, and an
    // id decides nothing about an outcome -- two siblings that made different numbers of tokens
    // reach the same position with different counters. (Where an id DOES matter -- attachment
    // wiring -- the board fold below picks it up via card.m_number, and fails closed.)
    // NOT folded, deliberately: m1_hand / m1_hand_n / m1_hand_turn (the order-condemnation
    // snapshot). Per-turn SCRATCH: its only consumer (the post-combat CollectActions
    // condemnation filter) guards on m1_hand_turn == turn_number, and the pre-combat entry of
    // every turn re-stamps it before that guard can pass again -- so at any END-OF-TURN
    // boundary the field is dead until overwritten and cannot distinguish two futures. Folding
    // it would only split states whose play is provably identical (lost reach, no soundness).

    for (int pi = 0; pi < 2; ++pi)
    {
        const Player& p = s.players[pi];
        fold(0x9100ull + static_cast<std::uint64_t>(pi));
        // Same draws consumed: without equal library POSITION the comparison is unsound (an extra
        // draw changes every future). The full order is folded, not just the size, so the
        // comparison holds under MTG_SEARCH_SHUFFLE too, where size no longer implies content.
        //
        // The OPPONENT's library is skipped unless the deck was actually dealt one
        // (opponent_library_dealt). Two reasons, and the first is not just tidiness: folding 53
        // extra card hashes per key, on every dominance key, is real cost on the hot path for
        // every deck that can never touch that zone. The second is that skipping makes those decks
        // byte-identical BY CONSTRUCTION rather than by an argument about hash equality classes.
        // When the opponent DOES have a library its size and order are future-determining -- a mill
        // deck's whole clock lives there -- so it is folded in full, same rule as ours.
        if (pi == 0 || s.opponent_library_dealt)
        {
            fold(static_cast<std::uint64_t>(p.library.size()));
            for (const Card& c : p.library) { fold(c.m_name_hash); }
        }
        // Per-turn counters: equal across siblings at the same boundary, folded so they cannot
        // silently differ.
        fold(static_cast<std::uint64_t>(p.lands_played_this_turn));
        fold(static_cast<std::uint64_t>(p.bonus_land_drops_this_turn));
        fold(static_cast<std::uint64_t>(p.cards_drawn_this_turn));
        fold(static_cast<std::uint64_t>(p.life_gained_this_turn));
        fold(static_cast<std::uint64_t>(p.poison_counters));
        // Staged (Light Up the Stage) and suspended (Lotus Bloom) cards are future-determining
        // zones with timers; exact match rather than a subset rule (a timer is not monotone).
        fold(static_cast<std::uint64_t>(p.staged_cards.size()));
        for (const StagedCard& sc : p.staged_cards)
        { fold(sc.card.m_name_hash); fold(static_cast<std::uint64_t>(sc.expiry_turn)); }
        fold(static_cast<std::uint64_t>(p.suspended_cards.size()));
        for (const SuspendedCard& sc : p.suspended_cards)
        { fold(sc.card.m_name_hash); fold(static_cast<std::uint64_t>(sc.arrive_turn)); }
        // GRAVEYARD PROJECTION -- fold only what this deck's readers can observe (see gy_bits).
        // Every accumulator is order-insensitive: a graveyard's ORDER is not future-determining.
        // Each is EXACT rather than the doc's proposed subset rule, because the model reader makes
        // the zone non-monotone (a GBDT can push the estimate either way on size), so there is no
        // direction to declare.
        if (gy_size)
        {
            // The model observes SIZE only -- no card identity, no types.
            fold(0x5123ull); fold(static_cast<std::uint64_t>(p.graveyard.size()));
        }
        if (gy_bits != GyR_None)
        {
            std::uint64_t names = 0;                 // commutative sum of the OBSERVABLE names
            std::uint64_t n_land = 0, n_inst_sorc = 0, n_creature = 0;
            std::uint64_t pips[5] = { 0, 0, 0, 0, 0 };
            for (const Card& gc : p.graveyard)
            {
                const CardDefinition* gd = CardDatabase::Instance().LookupCached(gc);
                const Card& zc = gd ? gd->card : gc;   // ZoneCard: a zone copy's masks are empty
                if (gy_bits & GyR_TypeCounts)
                {
                    if (zc.IsLand())                          { ++n_land; }
                    if (zc.IsInstant() || zc.IsSorcery())     { ++n_inst_sorc; }
                    if (zc.IsCreature())                      { ++n_creature; }
                }
                if (gy_bits & GyR_ColorDemand)
                {
                    if (!zc.IsLand())
                    {
                        const ManaCost& mc = zc.m_mana_cost;
                        pips[0] += static_cast<std::uint64_t>(mc.white);
                        pips[1] += static_cast<std::uint64_t>(mc.blue);
                        pips[2] += static_cast<std::uint64_t>(mc.black);
                        pips[3] += static_cast<std::uint64_t>(mc.red);
                        pips[4] += static_cast<std::uint64_t>(mc.green);
                    }
                }
                // Name-level readers: a card's IDENTITY is observable only if some reader can name
                // it. Anything else in the graveyard is invisible and must not block a comparison.
                bool named = (gy_bits & GyR_AllNames) != 0;
                if (!named && gd)
                {
                    const CardParams& gp = gd->params;
                    named = ((gy_bits & GyR_RetraceNames)  && gp.retrace)
                         || ((gy_bits & GyR_SelfCopyNames) && (gp.gy_self_power_bonus > 0
                                                            || gp.ritual_float_gy_self_bonus))
                         || ((gy_bits & GyR_LandsEdgeNames) && gp.discard_land_damage > 0)
                         || ((gy_bits & GyR_MulticolorNames) && zc.IsMulticolored());
                }
                else if (!named && !gd)
                {
                    named = true;                     // unknown card -> observe it (fail closed)
                }
                if (named) { names += gc.m_name_hash; }
            }
            if (names != 0) { fold(0x67A7ull); fold(names); }
            if (gy_bits & GyR_TypeCounts)
            { fold(0x7C05ull); fold(n_land); fold(n_inst_sorc); fold(n_creature); }
            if (gy_bits & GyR_ColorDemand)
            { fold(0xC01Dull); for (std::uint64_t v : pips) { fold(v); } }
        }
        // The OPPONENT's hand/life are folded here only for the non-active player's hand; our own
        // hand is the directional axis below.
        if (pi != me)
        {
            fold(static_cast<std::uint64_t>(p.hand.size()));
            std::uint64_t acc = 0;                       // order-insensitive, as for the graveyard
            for (const Card& c : p.hand) { acc += c.m_name_hash; }
            fold(acc);
        }
    }
    n.exact     = e;
    n.life_self = s.players[me].life;
    n.life_opp  = s.players[opp].life;

    // ---- our hand: directional (superset dominates) -------------------------------------------
    n.hand.reserve(s.players[me].hand.size());
    for (const Card& c : s.players[me].hand)
    {
        // A staged copy is a DIFFERENT resource from a plain one (it expires), so its expiry rides
        // the element hash and a staged card can only ever match another with the same expiry.
        std::uint64_t h = c.m_name_hash;
        if (c.m_is_staged) { h = Mix(h ^ 0x57A6E0ull, static_cast<std::uint64_t>(c.m_staged_expiry)); }
        n.hand.push_back(h);
    }
    std::sort(n.hand.begin(), n.hand.end());

    // ---- battlefield --------------------------------------------------------------------------
    // Attachment wiring is CROSS-permanent state (which body carries the Aura decides combat), so
    // it cannot be expressed per permanent. On a board with ANY attachment every permanent folds
    // its stable copy id plus its outgoing links -- which makes each permanent unique and collapses
    // the board to exact match. That is the fail-closed answer and it mirrors BuildSimKey's rule.
    bool wired = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.aura_attached_to != 0 || p.equipped_to != 0) { wired = true; break; }
    }

    std::vector<DomPerm> mine, others;
    for (const Permanent& p : s.battlefield)
    {
        DomPerm dp;
        std::uint64_t m = Mix(0xB1F1ull, p.card.m_name_hash);
        auto mfold = [&m](std::uint64_t v) { m = Mix(m, v); };
        mfold(static_cast<std::uint64_t>(p.controller_index));
        mfold(static_cast<std::uint64_t>(p.owner_index));
        // Token-ness is read by Lathliss's "nontoken Dragon" gate, so a token copy and a real one
        // are NOT interchangeable.
        mfold(p.is_token ? 1u : 0u);
        // Once-per-turn / obligation flags. All but is_token reset at the untap that just ran for
        // OUR permanents, so they are constant across siblings there; the opponent's are not, and
        // either way they are future-determining, so they match exactly.
        mfold(p.echo_resolved ? 1u : 0u);
        mfold(p.colored_cast_lifegain_used_this_turn ? 1u : 0u);
        mfold(p.loyalty_activated_this_turn ? 1u : 0u);
        mfold(p.storage_hold_this_turn ? 1u : 0u);
        mfold(static_cast<std::uint64_t>(p.garth_chosen_mask));
        // Chosen creature type (Urza's Incubator): decides WHICH spells this permanent discounts, so
        // two differently-choosing copies are not interchangeable. A deck-constant today (see the
        // BuildSimKey note), folded so a future searched choice cannot merge them. Nonzero only for
        // a type-choosing permanent -> every other deck's key is unchanged.
        if (p.chosen_subtype_id != 0) { mfold(0xC7BEull); mfold(static_cast<std::uint64_t>(p.chosen_subtype_id)); }
        if (wired)
        {
            mfold(0xA77Aull);
            mfold(static_cast<std::uint64_t>(p.card.m_number));
            mfold(static_cast<std::uint64_t>(p.aura_attached_to));
            mfold(static_cast<std::uint64_t>(p.equipped_to));
        }

        // Directional axes. Sum the generic counter vector by type first (a permanent may carry
        // several entries of one type).
        std::array<std::int64_t, static_cast<std::size_t>(DomAxis::_Count)> raw{};
        raw[static_cast<std::size_t>(DomAxis::ChargeCounters)]  = p.charge_counters;
        raw[static_cast<std::size_t>(DomAxis::StorageCounters)] = p.storage_counters;
        raw[static_cast<std::size_t>(DomAxis::VerseCounters)]   = p.verse_counters;
        raw[static_cast<std::size_t>(DomAxis::AgeCounters)]     = p.age_counters;
        raw[static_cast<std::size_t>(DomAxis::Loyalty)]         = p.loyalty;
        for (const Counter& c : p.counters)
        {
            switch (c.type)
            {
                case Counter::Type::PlusOnePlusOne:
                    raw[static_cast<std::size_t>(DomAxis::PlusOnePlusOne)]   += c.count; break;
                case Counter::Type::MinusOneMinusOne:
                    raw[static_cast<std::size_t>(DomAxis::MinusOneMinusOne)] += c.count; break;
                case Counter::Type::Loyalty:
                    raw[static_cast<std::size_t>(DomAxis::LoyaltyCounter)]   += c.count; break;
                case Counter::Type::Poison:
                    raw[static_cast<std::size_t>(DomAxis::PoisonCounter)]    += c.count; break;
                case Counter::Type::Depletion:
                    raw[static_cast<std::size_t>(DomAxis::DepletionCounter)] += c.count; break;
                // No default: adding a Counter::Type must not compile until it is dispositioned
                // here AND given a direction in DominanceAxisDirection.
            }
        }
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            switch (dir[i])
            {
                case DomDir::MoreDominates:
                    dp.v[i] =  static_cast<std::int32_t>(raw[i]); break;
                case DomDir::FewerDominates:
                    dp.v[i] = -static_cast<std::int32_t>(raw[i]); break;
                case DomDir::EqualRequired:
                    // Not a comparable axis for this deck -> an exact-match field instead.
                    mfold(0xE00ull + i);
                    mfold(static_cast<std::uint64_t>(raw[i]));
                    break;
            }
        }
        // Engine-owned axes: untapped dominates tapped, un-sick dominates summoning-sick. Both are
        // monotone under the rules rather than per deck (an untapped permanent can do everything a
        // tapped one can and more, including banking a storage counter at cleanup), so neither is
        // provider-overridable. Stored negated: fewer dominates.
        dp.v[kVecTapped] = p.tapped            ? -1 : 0;
        dp.v[kVecSick]   = p.entered_this_turn ? -1 : 0;

        dp.match = m;
        (p.controller_index == me ? mine : others).push_back(dp);
    }

    auto group = [](std::vector<DomPerm>& in, std::vector<DomGroup>& out)
    {
        std::sort(in.begin(), in.end(), [](const DomPerm& a, const DomPerm& b)
                  { return a.match != b.match ? a.match < b.match : a.v < b.v; });
        for (const DomPerm& dp : in)
        {
            if (out.empty() || out.back().match != dp.match) { out.push_back({dp.match, {}}); }
            out.back().members.push_back(dp.v);   // already ascending within the group
        }
    };
    group(mine, n.ours);
    group(others, n.theirs);

    n.comparable = true;
    return n;
}

}   // namespace dominance
