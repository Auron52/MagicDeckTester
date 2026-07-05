#pragma once
#include "GameState.h"
#include "ManaPool.h"
#include "GameLogger.h"                // g_reveal_logger: capture scry/dig reveals (real play only)
#include "../cards/CardDatabase.h"
#include "../ai/DecisionProviders.h"   // ResolveProvider: route deck decisions through the provider
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_set>

// A/B opt-out (default ON): retain leftover / over-produced mana (depletion + filter
// lands forced to over-tap) into the turn-scoped reserve state.floating_mana, so a later
// same-(main-)phase cast can spend it (CR 500.4). MTG_NO_FLOAT_LEFTOVER=1 disables the
// whole change -> byte-identical to the legacy "waste the leftover" behaviour. Read once.
inline bool FloatLeftoverManaEnabled()
{
    static const bool on = std::getenv("MTG_NO_FLOAT_LEFTOVER") == nullptr;
    return on;
}

// A/B opt-out (default ON): let the plan enumerator credit a mana rock cast THIS turn
// (Sol Ring -> {C}{C}) toward the rest of the same subset, so lines like
// "Sol Ring -> Ornithopter of Paradise" off one land are enumerated. MTG_NO_ROCK_RAMP=1
// disables it -> byte-identical to the legacy board-only enumeration. Read once.
inline bool RockRampEnumEnabled()
{
    static const bool on = std::getenv("MTG_NO_ROCK_RAMP") == nullptr;
    return on;
}

// Land's Edge firing heuristic: how many lands to discard to a Land's Edge of the
// given `rate` this activation. Fire all when it is lethal; otherwise fire only the
// excess over the max hand size (so those lands are not simply discarded to the
// end-of-turn cleanup for nothing); otherwise hold. Shared by the real engine
// (AIEngine::ActivateLandsEdge) and the search's inline executor (ApplyPlanDirect) so
// both model the same Land's Edge damage. Does NOT include the real engine's depth>0
// rollout comparison (fire-all-if-faster) -- that is a real-game-only refinement layered
// on top of this base policy.
inline int LandsEdgeHeuristicFireCount(const GameState& state, int rate)
{
    if (rate <= 0) { return 0; }
    const Player& ap  = state.players[state.active_player_index];
    const Player& opp = state.players[1 - state.active_player_index];

    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (def ? def->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }
    if (lands_in_hand == 0) { return 0; }

    bool unlimited_hand = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }
    int max_hand = unlimited_hand ? std::numeric_limits<int>::max() : 7;

    int lethal_lands = (opp.life + rate - 1) / rate;
    if (lands_in_hand >= lethal_lands) { return lands_in_hand; }
    int excess = std::max(0, static_cast<int>(ap.hand.size()) - max_hand);
    return std::min(excess, lands_in_hand);
}

// Cleanup discard-victim selection. SHARED by the real engine (AIEngine::ChooseDiscard) and
// the search rollout (TurnSolver::SimulateEndAndStartNextTurn) so both shed the SAME card when
// discarding to hand size -- previously they diverged (the rollout had no required-piece
// protection, so it shed high-MV spells and hoarded lands, over-counting a Land's Edge flood
// the real game never accumulates: gi=220 predicted a phantom T4 lethal vs the realised T7).
//
// Policy (mirrors the old ChooseDiscard exactly): when a Land's Edge land outlet exists
// (provider DiscardLandsFirst) the lands are ammunition -> discard the first non-staged land.
// Otherwise discard the highest-MV non-staged card that is NOT a required combo piece; required
// pieces and staged cards are shed only as a last resort. Returns an index into the active
// player's hand, or -1 if the hand is empty. `required_pieces` may be null (no protection).
inline int SelectCleanupDiscardIndex(const GameState& state,
                                     const std::vector<std::string>* required_pieces)
{
    const Player& ap = state.players[state.active_player_index];
    if (ap.hand.empty()) { return -1; }

    const bool has_land_outlet = ResolveProvider(state).DiscardLandsFirst(state);
    if (has_land_outlet)
    {
        for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
        {
            if (ap.hand[i].m_is_staged) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(ap.hand[i]);
            bool is_land = def ? def->card.IsLand() : ap.hand[i].IsLand();
            if (is_land) { return i; }
        }
    }

    // Highest-MV non-staged card that is not a required combo piece.
    int best_idx = -1, best_mv = -1;
    for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        bool is_req = false;
        if (required_pieces)
        {
            for (const std::string& piece : *required_pieces)
            {
                if (ap.hand[i].m_name == piece) { is_req = true; break; }
            }
        }
        if (is_req) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(ap.hand[i]);
        int mv = def ? def->card.m_mana_cost.ManaValue() : ap.hand[i].m_mana_cost.ManaValue();
        if (mv > best_mv) { best_mv = mv; best_idx = i; }
    }
    if (best_idx >= 0) { return best_idx; }

    // Last resort: max-MV including staged + required, staged preferred for discard
    // (mirrors ChooseDiscard's staged-aware comparator).
    best_idx = 0;
    for (int i = 1; i < static_cast<int>(ap.hand.size()); ++i)
    {
        const Card& a = ap.hand[best_idx];
        const Card& b = ap.hand[i];
        if (a.m_is_staged != b.m_is_staged) { if (a.m_is_staged) { best_idx = i; } continue; }
        const CardDefinition* da = CardDatabase::Instance().LookupCached(a);
        const CardDefinition* db = CardDatabase::Instance().LookupCached(b);
        int mv_a = da ? da->card.m_mana_cost.ManaValue() : a.m_mana_cost.ManaValue();
        int mv_b = db ? db->card.m_mana_cost.ManaValue() : b.m_mana_cost.ManaValue();
        if (mv_a < mv_b) { best_idx = i; }
    }
    return best_idx;
}

// Forward declaration: CreateToken is defined further down but used by FireOnCastTriggers.
inline void CreateToken(GameState&, int, int, int, const std::vector<std::string>&);

// ---- Anti-Lifegain (Tainted Remedy / Aria of Flame) shared helpers ----------
//
// These back the Anti-Lifegain deck, whose entire damage output flows through "an
// opponent gains life" effects that a Tainted Remedy (or Plague Drone's Rot Fly) turns
// into life LOSS. All of them are used identically by the real engine (EffectHandler /
// AIEngine) and the search rollout (TurnSolver) so both model the reversal the same way.

// True if `controller_index` controls a permanent whose ability replaces opponent life
// GAIN with life LOSS (Tainted Remedy; Plague Drone's Rot Fly).
inline bool RemedyActive(const GameState& state, int controller_index)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.lifegain_to_loss) { return true; }
    }
    return false;
}

// True if the named card is a lifegain_to_loss enabler (Tainted Remedy / Plague Drone). Used
// to cast enablers FIRST within a turn so same-turn payloads resolve with the enabler active.
inline bool IsLifegainToLossCard(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    return d && d->params.lifegain_to_loss;
}

// Apply "the opponent gains `amount` life" from an effect `controller_index` controls.
// With a Tainted Remedy / Plague Drone in play the gain is replaced by an equal life LOSS
// (CR 614.12) -- toward the goldfish win. Marks opponent_lost_life_this_turn when life
// actually decreases (spectacle bookkeeping). Without the replacement the opponent simply
// gains the life (setting the clock back), which is faithful: casting these riders without
// a Remedy out genuinely helps the opponent, and the clairvoyant search will sequence the
// Remedy first.
// True while the mana-payment BACKTRACKER is speculatively tapping sources (it taps a drip land,
// recurses, and UNDOES on failure). Life events emitted during speculation are phantom -- suppress
// them; the greedy primary payment path (outside the backtracker) still emits the real drip. RAII so
// nested recursion keeps it set. Display-only; never affects decisions.
inline thread_local bool g_tap_speculating = false;
struct TapSpeculationScope { bool prev; TapSpeculationScope() : prev(g_tap_speculating) { g_tap_speculating = true; } ~TapSpeculationScope() { g_tap_speculating = prev; } };

inline void OpponentGainsLife(GameState& state, int controller_index, int amount,
                              const std::string& source = std::string())
{
    if (amount <= 0) { return; }
    int opp = 1 - controller_index;
    const bool remedied = RemedyActive(state, controller_index);
    if (remedied)
    {
        state.players[opp].life -= amount;
        state.opponent_lost_life_this_turn = true;
    }
    else
    {
        state.players[opp].life += amount;
    }
    // Play-viewer history (real play only): enumerate the life swing + its source. Under a Remedy the
    // "gain" is flipped to a loss, which is exactly the case that confused a by-hand life count.
    // Suppressed during speculative payment backtracking (phantom taps that get undone).
    if (g_play_event_sink && !g_tap_speculating)
    {
        std::string src = !source.empty()
            ? (" (" + source + (remedied ? ", via Tainted Remedy)" : ")"))
            : (remedied ? " (via Tainted Remedy)" : "");
        EmitPlayEvent(state.turn_number, remedied ? "lifeloss" : "lifegain",
                      std::string(remedied ? "🩸 opponent −" : "＋ opponent +") + std::to_string(amount)
                      + " life" + src);
    }
}

// Grove of the Burnwillows drip when the gift is USEFUL (OpponentLifegainUseful): Grove's coloured
// tap makes "each opponent gain 1", which e.g. a Tainted Remedy / Plague Drone reverses into 1 DAMAGE.
// Once that is live the player should tap such a land EVERY turn for the free ping even with nothing
// to cast -- but the normal mana path only taps it when a spell needs the mana, so a turn with no cast
// wasted the drip. Tap every still-UNTAPPED tap_opponent_lifegain land the player controls and apply
// the drip. No-op when the provider says the gift is NOT useful (it would HELP the opponent -- the
// default, and the clairvoyant search avoids that) or without an untapped drip land. Deterministic
// end-of-pre-combat-main action (NOT a search choice), so the rollout (ApplyPlanDirect) and the real
// executor (AIEngine::TakeTurn) both call it once per turn at the same point and stay in lockstep.
// Tapping untapped lands only makes it idempotent within a turn (a multi-segment claude-play main never
// double-drips). Inert for every deck without a tap_opponent_lifegain land + a useful-lifegain combo.
inline void TapDripLandsIfUseful(GameState& state, int controller_index)
{
    if (!ResolveProvider(state).OpponentLifegainUseful(state, controller_index)) { return; }
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index || p.tapped) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.tap_opponent_lifegain <= 0) { continue; }
        p.tapped = true;
        if (def->params.tap_self_damage > 0) { state.players[controller_index].life -= def->params.tap_self_damage; }
        OpponentGainsLife(state, controller_index, def->params.tap_opponent_lifegain);
    }
}

// Colour a Grove-of-the-Burnwillows-style drip land (tap_opponent_lifegain > 0) should produce
// when tapped for a GENERIC (any-colour) pip. Grove has two abilities: "{T}: Add {C}" (painless)
// and "{T}: Add {R} or {G}. Each opponent gains 1 life." A generic pip only needs {C}, so when the
// gift is NOT useful (the default -- OpponentLifegainUseful false) we tap the painless {C} mode --
// Colorless signals "no drip" to tap_source. When the provider says the gift IS useful (e.g. a Remedy
// reverses "gain 1" into 1 DAMAGE) the drip is BENEFICIAL, so we keep tapping the coloured mode
// (`colored_pick`) to fire it. A non-drip land (every source in every other deck) always returns
// `colored_pick`, so this is inert outside a drip-land deck. Specific coloured pips (R/G) never route
// through here -- they always tap coloured and drip (the real, unavoidable cost of that colour).
// Shared by both tap_source lambdas (TurnSolver + AIEngine) so rollout and executor agree (lockstep).
inline Color DripLandAnyPipColor(const GameState& state, int active,
                                 const CardDefinition& def, Color colored_pick)
{
    if (def.params.tap_opponent_lifegain > 0 && !ResolveProvider(state).OpponentLifegainUseful(state, active))
    { return Color::Colorless; }
    return colored_pick;
}

// True if `controller_index` controls a permanent with the given subtype (e.g. "Forest").
// Backs the alt-cost condition "If you control a Forest, rather than pay this spell's mana
// cost ...". Land subtypes (Forest/Plains/...) are stored in the card's m_subtypes.
inline bool ControlsSubtype(const GameState& state, int controller_index,
                            const std::string& subtype)
{
    if (subtype.empty()) { return true; }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        const SubtypeSet& subs = def ? def->card.m_subtypes : p.card.m_subtypes;
        for (const std::string& s : subs) { if (s == subtype) { return true; } }
    }
    return false;
}

// True if `card`'s card-type set includes the named type ("Enchantment"/"Artifact"/
// "Creature"/"Land"/"Instant"/"Sorcery"). Used by tutor type filters.
inline bool CardMatchesTypeName(const Card& card, const std::string& type_name)
{
    if (type_name == "Enchantment") { return card.HasType(CardType::Enchantment); }
    if (type_name == "Artifact")    { return card.HasType(CardType::Artifact); }
    if (type_name == "Creature")    { return card.IsCreature(); }
    if (type_name == "Land")        { return card.IsLand(); }
    if (type_name == "Instant")     { return card.IsInstant(); }
    if (type_name == "Sorcery")     { return card.IsSorcery(); }
    return false;
}

// Tutor decision (Idyllic / Enlightened): returns the ORDERED list of library card NAMES the
// tutor should consider fetching. This is the first instance of the intended general pattern --
// a deck/archetype heuristic returns a CANDIDATE SET, and the caller treats it as:
//   - exactly ONE candidate  => the heuristic made a CLEAR decision (no search), or
//   - MORE THAN ONE candidate => the heuristic is UNSURE; the search enumerates each as a
//     mutually-exclusive plan variant and keeps the best ("search sometimes").
// (Down the road this heuristic moves to a deck/archetype file behind a decision interface;
// for now it lives here.) Returns {} when the library holds no legal target (tutor whiffs).
//
// The anti-lifegain rule: fetch a combo ENABLER (lifegain_to_loss) while we have none we can
// get online soon; otherwise fetch the WINCON (verse_damage = Aria). "Online soon" = a Remedy
// ACTIVE, or one in HAND we can AFFORD (Tainted Remedy {2}{B} is cheap; Plague Drone {3}{B} we
// cannot yet pay for is NOT). The one genuinely uncertain case -- a held-but-unaffordable
// Plague Drone as our ONLY enabler -- is returned as TWO candidates (enabler + wincon) for the
// search to decide, rather than guessed.
inline std::vector<std::string> TutorCandidates(const GameState& state, int controller_index,
                                                const CardParams& pp)
{
    const Player& ap = state.players[controller_index];

    // Unpruned audit (MTG_UNPRUNED): return EVERY legal tutor target (distinct names)
    // so the search branches over all of them, instead of the heuristic-narrowed pick.
    if (DecisionUnpruned(UnprunedGate::Tutor))
    {
        std::vector<std::string>        all;
        std::unordered_set<std::string> seen;
        for (const Card& lc : ap.library)
        {
            const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
            const Card&           card = def ? def->card : lc;
            bool type_ok = false;
            for (const std::string& t : pp.tutor_types)
            { if (CardMatchesTypeName(card, t)) { type_ok = true; break; } }
            if (type_ok && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
        }
        return all;
    }

    // Best enabler / wincon / any matching card available in the library.
    std::string enabler_name, wincon_name, any_name;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
        const Card& card = def ? def->card : lc;
        bool type_ok = false;
        for (const std::string& t : pp.tutor_types) { if (CardMatchesTypeName(card, t)) { type_ok = true; break; } }
        if (!type_ok) { continue; }
        if (any_name.empty()) { any_name = lc.m_name; }
        if (def && def->params.lifegain_to_loss && enabler_name.empty()) { enabler_name = lc.m_name; }
        if (def && def->params.verse_damage      && wincon_name.empty())  { wincon_name  = lc.m_name; }
    }

    // Do we already have an enabler we can get online soon?
    bool ready_enabler = RemedyActive(state, controller_index);
    bool unaffordable_enabler_in_hand = false;
    if (!ready_enabler)
    {
        int sources = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller_index) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && (d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock)) { ++sources; }
        }
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d || !d->params.lifegain_to_loss) { continue; }
            if (sources >= d->card.m_mana_cost.ManaValue()) { ready_enabler = true; break; }
            unaffordable_enabler_in_hand = true;
        }
    }

    std::vector<std::string> out;
    if (ready_enabler)
    {
        // Clear: have an enabler coming -> fetch the wincon (fall back to enabler/any).
        const std::string& pick = !wincon_name.empty() ? wincon_name
                                : !enabler_name.empty() ? enabler_name : any_name;
        if (!pick.empty()) { out.push_back(pick); }
    }
    else if (unaffordable_enabler_in_hand && !enabler_name.empty() && !wincon_name.empty())
    {
        // UNSURE: only enabler is a held-but-unaffordable Plague Drone -> let the search pick
        // between coming online cheaper now (enabler) vs. having the payoff ready (wincon).
        out.push_back(enabler_name);
        out.push_back(wincon_name);
    }
    else
    {
        // Clear: no enabler available -> fetch one (fall back to wincon/any).
        const std::string& pick = !enabler_name.empty() ? enabler_name
                                : !wincon_name.empty() ? wincon_name : any_name;
        if (!pick.empty()) { out.push_back(pick); }
    }
    return out;
}

// Whether mid-game library SEARCHES (fetchland / tutor) shuffle the remaining library.
// Real MTG always shuffles on a search (CR 701.19); the model historically skipped it
// (a clairvoyance simplification -- the search read the fixed game-start order to predict
// draws). Now ON by default (rules-correct, and removes post-search draw clairvoyance
// from the search). The shuffle is DETERMINISTIC (SearchShuffleSeed) so the search
// rollout and the real executor reproduce the IDENTICAL post-search order -> they stay in
// lockstep, and the same seed reproduces the same game across runs / similar decklists.
// Opt-out MTG_NO_SEARCH_SHUFFLE restores the old no-shuffle behaviour for A/B. See
// search-quality-first-roadmap.
inline bool SearchShuffleEnabled()
{
    static const bool on = std::getenv("MTG_NO_SEARCH_SHUFFLE") == nullptr;
    return on;
}

// Deterministic seed for the shuffle a library SEARCH triggers. Keyed on (game_seed,
// search-event index) -- NOT on library contents or RNG-call-count -- so it is stable
// across runs and across similar decklists (an A/B of two near-identical lists shuffles
// the same way at the same search index). splitmix64 finaliser over a well-separated mix
// keeps it clear of the mulligan reshuffle seeds (game_seed + mulligan_count).
inline uint64_t SearchShuffleSeed(uint64_t game_seed, uint64_t search_index)
{
    uint64_t x = game_seed * 0x9E3779B97F4A7C15ull
               + (search_index + 1) * 0xD1B54A32D192ED03ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

// Shuffle the controller's remaining library after a search, then bump the per-game
// search counter (so the NEXT search uses a fresh deterministic seed). No-op when the
// feature is off. Call AFTER the searched card has been removed from the library and
// BEFORE any "put on top" placement (rules order: shuffle, then put on top).
inline void ShuffleAfterSearch(GameState& state, int controller_index)
{
    if (!SearchShuffleEnabled()) { return; }
    // Decoupling instrument: the SEARCH evaluation shuffles with shuffle_salt_search, the real
    // executor with shuffle_salt. Equal by default -> byte-identical / lockstep. See g_shuffle_eval.
    const uint64_t salt = g_shuffle_eval ? state.shuffle_salt_search : state.shuffle_salt;
    state.players[controller_index].library.Shuffle(
        SaltSeed(SearchShuffleSeed(state.game_seed, state.search_count), salt));
    ++state.search_count;
}

// Execute a tutor (Idyllic / Enlightened): fetch `target_name` from the library and move it to
// hand (to_hand) or the top of the library (to_top). When target_name is empty, fall back to
// the heuristic's top candidate (TutorCandidates) -- so any path that doesn't carry a searched
// choice still plays the heuristic. The library is treated as already shuffled (the remaining
// order past the tutored card is a goldfish-irrelevant simplification that keeps the real game
// and rollout byte-consistent). Shared by EffectHandler (real) and ApplyPlanDirect (rollout).
inline void PerformTutor(GameState& state, int controller_index, const CardParams& pp,
                         const std::string& target_name = "",
                         const std::string& source_name = "Tutor")
{
    std::string want = target_name;
    if (want.empty())
    {
        std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, controller_index, pp);
        if (cands.empty()) { return; }
        want = cands.front();
    }
    Player& ap = state.players[controller_index];
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
    {
        if (ap.library[i].m_name == want) { idx = i; break; }
    }
    if (idx < 0) { return; }   // chosen target no longer present (search/real drift guard)
    Card c = ap.library[idx];
    const int         fetched_num  = c.m_number;   // capture before the move into hand/library
    const std::string fetched_name = c.m_name;
    ap.library.erase(ap.library.begin() + idx);
    // Searching the library shuffles it (CR 701.19) -- BEFORE a "put on top" placement
    // (you shuffle, then put the card on top). Deterministic + lockstep; no-op unless
    // MTG_SEARCH_SHUFFLE is set.
    ShuffleAfterSearch(state, controller_index);
    if (pp.tutor_to_hand)     { ap.hand.push_back(std::move(c)); }
    else if (pp.tutor_to_top) { ap.library.insert(ap.library.begin(), std::move(c)); }

    // Record WHAT was searched up so the replay viewer shows it (real play only -- the reveal
    // logger is null during search/rollout, so this is byte-identical to the suite). Modelled as
    // a one-card reveal "kept" by the tutor (the fetched-to-hand/top card).
    if (g_reveal_logger)
    {
        g_reveal_logger->LogReveal(source_name + " (searched)",
                                   { fetched_num }, { fetched_name }, { fetched_num }, {});
    }

    // Gamble: "then discard a card at random." Deterministic seed (game_seed / turn /
    // search_count) so the rollout and the real executor pick the IDENTICAL victim -- the
    // search resolves it clairvoyantly (the engine-wide known-library simplification), but it
    // can still hit the just-tutored card (the real Gamble risk on a small hand). Only fires
    // for to-hand tutors that set the flag; off everywhere else.
    if (pp.discard_random_after_tutor && pp.tutor_to_hand && !ap.hand.empty())
    {
        uint64_t mix = state.game_seed * 0x9E3779B97F4A7C15ull
                     + (static_cast<uint64_t>(state.turn_number) + 1) * 0xD1B54A32D192ED03ull
                     + (state.search_count + 1) * 0xCA5A826395121157ull
                     + ap.hand.size();
        mix ^= mix >> 30; mix *= 0xBF58476D1CE4E5B9ull;
        mix ^= mix >> 27; mix *= 0x94D049BB133111EBull;
        mix ^= mix >> 31;
        mix = SaltSeed(mix, g_shuffle_eval ? state.shuffle_salt_search : state.shuffle_salt);   // shuffle-variance: a mid-game random event
        int victim = static_cast<int>(mix % ap.hand.size());
        const int         victim_num  = ap.hand[victim].m_number;
        const std::string victim_name = ap.hand[victim].m_name;
        // Discard goes to the graveyard (CR 701.8 / a discarded card is put into its owner's
        // graveyard) -- the prior code erased it from hand without rezoning, so the card silently
        // left the game and never showed up in the graveyard zone. Inert for the search on every
        // current deck (no Gamble deck reads graveyard contents -- no retrace/delve/escape/
        // threshold), so this only restores the correct zone + surfaces the card to the viewer.
        ap.graveyard.push_back(ap.hand[victim]);
        ap.hand.erase(ap.hand.begin() + victim);
        if (g_reveal_logger) { g_reveal_logger->LogDiscard(victim_num, victim_name); }
    }
}

// Exalted (Ignoble Hierarch): "Whenever a creature you control attacks ALONE, that creature
// gets +1/+1 until end of turn" -- once per Exalted ability the controller has. Returns the
// total +1/+1 bonus to apply to a lone attacker = the number of Exalted permanents controlled.
// Returns 0 (inert) for any deck whose permanents have no Exalted keyword. Callers apply it
// only when exactly one creature attacks.
inline int CountExalted(const std::vector<Permanent>& battlefield, int controller_index)
{
    int n = 0;
    for (const Permanent& p : battlefield)
    {
        if (p.controller_index == controller_index && p.card.HasKeyword(Keyword::Exalted)) { ++n; }
    }
    return n;
}

// Forward declaration: CanAttackFull is defined later in this header.
inline bool CanAttackFull(const Permanent&, const std::vector<Permanent>&, int);

// Battlefield index of the controller's best attacker (highest effective power among creatures
// that can attack this turn), or -1. Used to target an own-creature pump (Invigorate) at the
// creature whose extra damage matters most.
inline int FindBestOwnAttacker(const GameState& state, int controller_index)
{
    int best = -1, best_pw = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller_index) { continue; }
        if (!CanAttackFull(p, state.battlefield, controller_index)) { continue; }
        int pw = p.EffectivePower();
        if (pw > best_pw) { best_pw = pw; best = i; }
    }
    return best;
}

// Target selection for the controller-lifegain removal (Swords to Plowshares). Its rider makes the
// EXILED creature's controller gain life equal to its power, which a Tainted Remedy / Plague Drone
// (RemedyActive) turns into that much life LOSS on the opponent. So against a PASSIVE goldfish opponent
// the spell is worth casting ONLY while such an enabler is in play, and then it should hit the
// opponent's LARGEST-power creature (max life loss). Returns that creature's battlefield index, or -1 =
// "do not cast" (no enabler in play, or no opponent creature). Used in lockstep by the enumeration gate,
// the search rollout, and the real executor so all three agree on the target.
//
// NB this is a GOLDFISHING heuristic: against a real opponent a creature is worth exiling on its own
// even without an enabler (and you might prefer a specific threat over the largest) -- revisit for
// Phase 2. Only Swords carries controller_lifegain_equals_power, so every other deck is untouched.
inline int FindLifegainRemovalTarget(const GameState& state, int active)
{
    if (!RemedyActive(state, active)) { return -1; }
    int best = -1, best_pw = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == active || !p.card.IsCreature()) { continue; }
        int pw = p.EffectivePower();
        if (pw > best_pw) { best_pw = pw; best = i; }
    }
    return best;
}

// Target selection for a creature-targeting burn that carries a "when that creature dies" rider
// (Searing Blood: 2 to a creature, then 3 to its controller if it dies this turn). The 3-to-face
// only lands if the target actually DIES, so among the opponent's creatures we prefer one this
// spell KILLS -- EffectiveToughness() <= `damage` -- over an arbitrary first creature it would only
// bruise. Falls back to the first opponent creature when none is killable (still a legal target;
// the spell deals its `damage` but the death rider does not fire), and returns -1 when the opponent
// controls no creature at all (uncastable). Used in lockstep by the value model's reach estimate,
// the search rollout's damage apply, and the real executor so all three agree on the target AND on
// whether the death rider fires. Goldfishing scope: the passive opponent never blocks/attacks, so
// which creature dies is irrelevant beyond enabling this rider (revisit for a real opponent).
inline int FindBurnKillTarget(const GameState& state, int active, int damage)
{
    int first = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == active || !p.card.IsCreature()) { continue; }
        if (first < 0) { first = i; }
        if (p.EffectiveToughness() <= damage) { return i; }   // killable -> the rider fires
    }
    return first;   // none killable (or -1 = no opponent creature)
}

// A spell that requires a target is UNCASTABLE with no legal target (CR 601.2c: choosing
// targets is part of casting; a spell with no legal target cannot be put on the stack). This
// gates every alt-cost payload's cast so we never emit a targetless line. Keyed on the spell's
// targeting mode, not on a card name, so it is generic: among the current alt-cost payloads only
// creature-targeted ones (Invigorate, "target creature") need a target -- Skyshroud Cutter (a
// creature that just enters) and Reverent Silence (destroy-all) take none, so they pass trivially.
// "Target creature" is any creature on the battlefield (own OR opponent); the model's
// target_own_creature is a RESOLUTION heuristic (which creature to pump), not a legality bound.
inline bool AltPayloadTargetLegal(const GameState& state, const CardDefinition& def)
{
    if (def.params.targeting != Targeting::Creature) { return true; }   // no target to satisfy
    for (const Permanent& p : state.battlefield)
    {
        if (p.card.IsCreature()) { return true; }
    }
    return false;
}

// True if a free alt-cost payload `def` in `controller_index`'s hand should be AUTO-FIRED now:
// it has an alt lifegain cost, is SAFE (not Reverent's destroy-all-enchantments, which stays a
// search decision), a Remedy is active so the opponent's "gain" becomes damage, the alt-cost
// subtype (a Forest) is controlled, and -- if it needs an own creature (Invigorate's pump) --
// one exists to target. Firing such a payload is strictly good in a goldfish (free face
// damage), so it is applied deterministically rather than enumerated as a search choice (which
// would blow up the plan count, since free actions are never mana-pruned). Shared by the
// rollout (ApplyPlanDirect) and the real engine (AIEngine) so both fire the same payloads.
inline bool CanAutoFireAltPayload(const GameState& state, int controller_index,
                                  const CardDefinition& def)
{
    if (def.params.alt_lifegain_cost <= 0)        { return false; }
    if (def.params.destroy_all_enchantments)      { return false; }   // risky -> searched
    if (!RemedyActive(state, controller_index))   { return false; }
    if (!ControlsSubtype(state, controller_index, def.params.alt_cost_requires_subtype)) { return false; }
    if (!AltPayloadTargetLegal(state, def)) { return false; }   // uncastable with no legal target
    if (def.params.target_own_creature && FindBestOwnAttacker(state, controller_index) < 0) { return false; }
    return true;
}

// Destroy all enchantment permanents (Reverent Silence) -- including the caster's own Aria of
// Flame / Tainted Remedy. Each goes to its owner's graveyard. Shared by both paths.
inline void DestroyAllEnchantments(GameState& state)
{
    for (std::vector<Permanent>::iterator it = state.battlefield.begin();
         it != state.battlefield.end(); )
    {
        if (it->card.HasType(CardType::Enchantment))
        {
            state.players[it->owner_index].graveyard.push_back(it->card);
            it = state.battlefield.erase(it);
        }
        else { ++it; }
    }
}

// Fires on-cast triggers from all permanents on the battlefield (e.g. Eidolon of the
// Great Revel; Worthy Knight). Called at cast time from both AIEngine (real game) and
// ApplyPlanDirect (lookahead). Two trigger families:
//   - on_cast_trigger_*: deal damage to the active player when they cast a spell with
//     MV <= on_cast_trigger_max_mv (Eidolon of the Great Revel).
//   - cast_trigger_creates_tokens: create tokens when they cast a spell whose subtypes
//     include cast_trigger_subtype (Worthy Knight: cast a Knight -> 1/1 Human token).
// Aether Vial deployment is NOT a cast, so it never reaches here -- correct (CR 601.2).
inline void FireOnCastTriggers(GameState& state, const CardDefinition& cast_def)
{
    int mv = cast_def.card.m_mana_cost.ManaValue();
    int active = state.active_player_index;

    // Token specs are collected first: CreateToken push_backs onto the battlefield, which
    // would invalidate a range-for over it. Iterate the original size by index, then create.
    struct TokenSpec { int n, p, t; std::vector<std::string> subs; };
    std::vector<TokenSpec> to_create;

    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        const Permanent& p = state.battlefield[i];
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }

        if (def->params.on_cast_trigger_max_mv > 0 && mv <= def->params.on_cast_trigger_max_mv)
        {
            state.players[active].life -= def->params.on_cast_trigger_damage;
        }

        // Aria of Flame verse engine: casting an instant or sorcery puts a verse counter on
        // this enchantment, then it deals (verse counters) damage to the opponent. This is
        // real damage (not a lifegain event), so Tainted Remedy does not touch it. Aria is
        // not on the battlefield when it is itself cast, so it never self-triggers.
        if (def->params.verse_damage
            && (cast_def.card.IsInstant() || cast_def.card.IsSorcery()))
        {
            state.battlefield[i].verse_counters += 1;
            state.players[1 - active].life -= state.battlefield[i].verse_counters;
            state.opponent_lost_life_this_turn = true;
        }

        if (def->params.cast_trigger_creates_tokens > 0 && !def->params.cast_trigger_subtype.empty())
        {
            bool subtype_match = false;
            for (const std::string& cs : cast_def.card.m_subtypes)
            {
                if (cs == def->params.cast_trigger_subtype) { subtype_match = true; break; }
            }
            if (subtype_match)
            {
                to_create.push_back({def->params.cast_trigger_creates_tokens,
                                     def->params.cast_token_power,
                                     def->params.cast_token_toughness,
                                     def->params.cast_token_subtypes});
            }
        }
    }

    for (const TokenSpec& s : to_create)
    {
        for (int k = 0; k < s.n; ++k) { CreateToken(state, active, s.p, s.t, s.subs); }
    }
}

// Returns the total {power_bonus, toughness_bonus} granted to `creature` by all
// lord_effect permanents that `controller_index` controls on `battlefield`.
// Matches on creature.m_subtypes (e.g. "Sliver") against params.subtypes_affected.
// Pass all_creature_types = true for animated permanents (e.g. Mutavault) which have
// all creature types and therefore match any lord.
// For lords with scales_per_matching = true (e.g. Predatory Sliver), the bonus scales
// with the number of other matching permanents on the battlefield.
// `self` (when non-null) is the permanent whose bonus we are computing; a lord with
// lord_excludes_self ("Other ... get +1/+1") does not apply to itself. Pass the
// battlefield permanent at combat/damage time; pass nullptr for hand-card evaluation
// (the card is not yet a lord on the battlefield, so there is nothing to exclude).
inline std::pair<int,int> ComputeLordBonus(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false,
    const Permanent*               self               = nullptr)
{
    int pb = 0, tb = 0;
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(lord.card);
        if (!ldef || ldef->tmpl != CardTemplate::LordEffect) { continue; }

        // "Other ..." lords do not buff themselves (CR layer 7c): skip when the lord IS
        // the creature being evaluated. Identity by address within this same battlefield.
        if (ldef->params.lord_excludes_self && self != nullptr && &lord == self) { continue; }

        bool matches = false;
        if (ldef->params.affects_all_creatures)
        {
            matches = true;   // anthem for every creature you control (Benalish Marshal)
        }
        else if (all_creature_types && !ldef->params.subtypes_affected.empty())
        {
            matches = true;
        }
        else
        {
            for (const std::string& sub : ldef->params.subtypes_affected)
            {
                if (matches) { break; }
                for (const std::string& cs : creature.m_subtypes)
                {
                    if (cs == sub) { matches = true; break; }
                }
            }
        }
        if (!matches) { continue; }

        if (ldef->params.scales_per_matching)
        {
            // Bonus per Sliver = power_bonus * (number of other matching Slivers on board).
            // Count all matching permanents (including animated) then subtract 1 for "other".
            int matching_count = 0;
            for (const Permanent& other : battlefield)
            {
                if (other.controller_index != controller_index) { continue; }
                bool other_matches = false;
                if (other.is_animated && !ldef->params.subtypes_affected.empty())
                {
                    other_matches = true;
                }
                else
                {
                    for (const std::string& sub : ldef->params.subtypes_affected)
                    {
                        for (const std::string& cs : other.card.m_subtypes)
                        {
                            if (cs == sub) { other_matches = true; break; }
                        }
                        if (other_matches) { break; }
                    }
                }
                if (other_matches) { ++matching_count; }
            }
            int scale = std::max(0, matching_count - 1);
            pb += ldef->params.power_bonus * scale;
            tb += ldef->params.tough_bonus * scale;
        }
        else
        {
            pb += ldef->params.power_bonus;
            tb += ldef->params.tough_bonus;
        }
    }
    return {pb, tb};
}

// Returns true if any lord on the battlefield grants double strike to creature's subtype.
inline bool HasDoubleStrikeFromLords(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false)
{
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(lord.card);
        if (!ldef || !ldef->params.grants_double_strike) { continue; }
        if (all_creature_types && !ldef->params.subtypes_affected.empty()) { return true; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : creature.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Returns true if any lord on the battlefield grants haste to creature's subtype.
// all_creature_types: the attacker is an animated land (e.g. Mutavault), which has
// EVERY creature type, so it matches any typed haste lord (e.g. Cloudshredder Sliver's
// Sliver haste) — needed because the land's printed m_subtypes is empty.
inline bool HasHasteFromLords(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false)
{
    for (const Permanent& lord : battlefield)
    {
        if (lord.controller_index != controller_index) { continue; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(lord.card);
        if (!ldef || !ldef->params.grants_haste) { continue; }
        if (all_creature_types && !ldef->params.subtypes_affected.empty()) { return true; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : creature.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Returns true if the permanent can attack, considering lord-granted haste as well as
// card-level haste and animation. Use this in place of Permanent::CanAttack() anywhere
// the battlefield context is available (declare attackers, combat lookahead).
inline bool CanAttackFull(
    const Permanent&               p,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index)
{
    if (!p.card.IsCreature() && !p.is_animated) { return false; }
    if (p.tapped)                               { return false; }
    if (p.card.HasKeyword(Keyword::Defender))   { return false; }
    // Summoning sickness applies to animated lands too: an animated Mutavault may attack
    // only if the LAND has been controlled since before this turn (entered_this_turn is
    // false), or it has haste. Mutavault grants NO haste itself, so a land animated the
    // turn it was played cannot attack unless a haste lord (e.g. Cloudshredder Sliver,
    // which hastes the animated land as it is every creature type) grants it.
    if (!p.entered_this_turn)                   { return true; }
    if (p.card.HasKeyword(Keyword::Haste))      { return true; }
    return HasHasteFromLords(p.card, battlefield, controller_index, p.is_animated);
}

// Returns the total LIFE the opponent LOSES from attack triggers (e.g. Leeching Sliver:
// "defending player loses 1 life" per attacking Sliver). This is life loss, NOT combat
// damage -- it is unaffected by damage prevention/replacement and does not trigger
// "deals damage" effects or lifelink (none modelled, but kept distinct so a future
// damage-interaction card cannot wrongly include it). Iterates the battlefield for
// permanents with attack_trigger_life_loss > 0, counting attackers matching subtypes.
inline int CountAttackTriggerLifeLoss(
    const std::vector<Permanent>&         battlefield,
    int                                   controller_index,
    const std::vector<const Permanent*>&  attackers)
{
    int total = 0;
    for (const Permanent& src : battlefield)
    {
        if (src.controller_index != controller_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(src.card);
        if (!def || def->params.attack_trigger_life_loss <= 0) { continue; }

        int count = 0;
        for (const Permanent* atk : attackers)
        {
            for (const std::string& sub : def->params.subtypes_affected)
            {
                bool matches = atk->is_animated;  // animated = all creature types
                if (!matches)
                {
                    for (const std::string& cs : atk->card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++count; break; }
            }
        }
        total += def->params.attack_trigger_life_loss * count;
    }
    return total;
}

// Creates a creature token with the given stats and adds it to the active battlefield.
// The token enters with entered_this_turn = true (subject to summoning sickness unless
// given haste by a lord). Tokens have no card number and an auto-generated name.
inline void CreateToken(
    GameState&                       state,
    int                              controller_index,
    int                              power,
    int                              toughness,
    const std::vector<std::string>&  subtypes)
{
    Permanent token;
    std::string token_name = std::to_string(power) + "/" + std::to_string(toughness);
    if (!subtypes.empty()) { token_name += " " + subtypes[0]; }
    token_name            += " Token";
    token.card.m_name      = token_name;   // single intern of the full token name
    token.card.RehashName();
    token.card.AddType(CardType::Creature);
    token.card.m_subtypes  = subtypes;
    token.card.m_power     = power;
    token.card.m_toughness = toughness;
    token.controller_index = controller_index;
    token.owner_index      = controller_index;
    token.entered_this_turn = true;
    state.battlefield.push_back(token);
}

// Forbidden Orchard: "{T}: Add one mana of any color. Whenever you tap this land for mana, target
// opponent creates a 1/1 colorless Spirit creature token." Modelled (per user) as: assume each
// Orchard the active player controls is tapped for mana EVERY turn it is in play -> the opponent
// gets one 1/1 colourless Spirit per Orchard per turn. The Spirit is a real opponent creature, so it
// is a first-class target for Soulfire (extra dig) / Crackle (discount) / removal automatically.
// In the passive-opponent goldfish (Hinata flies) the Spirits never block -- they are pure targets.
inline bool IsForbiddenOrchard(const CardDefinition* d)
{
    return d && d->params.taps_spawn_opp_token;
}

// Create one 1/1 colourless Spirit for the opponent (the Forbidden Orchard token).
inline void SpawnOpponentSpirit(GameState& state)
{
    CreateToken(state, 1 - state.active_player_index, 1, 1, std::vector<std::string>{"Spirit"});
}

// Turn-start spawn: one Spirit per Orchard the ACTIVE player already controls (re-tapped this turn).
// Called where opponent_spawns are materialised, in BOTH the rollout and the executor -> lockstep.
// Count first (CreateToken appends to battlefield) so we don't iterate over the new tokens.
inline void SpawnForbiddenOrchardTokensTurnStart(GameState& state)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index
            && IsForbiddenOrchard(CardDatabase::Instance().LookupCached(p.card))) { ++n; }
    }
    for (int i = 0; i < n; ++i) { SpawnOpponentSpirit(state); }
}

// Number of creatures `controller_index` controls (including itself and tokens, plus
// animated lands). Used for characteristic-defining power (Adeline: power = creatures
// you control).
inline int CreatureCount(const GameState& state, int controller_index)
{
    int count = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        if (p.card.IsCreature() || p.is_animated)   { ++count; }
    }
    return count;
}

// Extra base power from a characteristic-defining ability (Adeline: power = number of
// creatures you control). Returns 0 for ordinary creatures. The card's printed power is
// 0 (printed *), so this is the whole base power before counters/temp/lords.
inline int DynamicBasePower(const CardDefinition& def, const GameState& state, int controller_index)
{
    if (def.params.power_equals_creature_count) { return CreatureCount(state, controller_index); }
    return 0;
}

// Fires "whenever you attack, create N tokens tapped and attacking" triggers (Adeline,
// Resplendent Cathar: one 1/1 white Human per opponent = 1 in a single-opponent goldfish).
// Only call when the active player is actually attacking (>= 1 declared attacker). Creates
// the tokens TAPPED (they are "tapped and attacking") and returns the battlefield index
// where the new tokens begin, so the caller adds [start, end) to this combat's attackers
// (they bypass summoning sickness for this attack, then persist and attack normally next
// turn). Token specs are gathered before creation to avoid range invalidation.
inline int FireAttackCreateTokens(GameState& state, int controller_index)
{
    int start = static_cast<int>(state.battlefield.size());

    struct TokenSpec { int n, p, t; std::vector<std::string> subs; };
    std::vector<TokenSpec> to_create;
    for (int i = 0; i < start; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.attack_creates_tokens <= 0) { continue; }
        to_create.push_back({def->params.attack_creates_tokens,
                             def->params.attack_token_power,
                             def->params.attack_token_toughness,
                             def->params.attack_token_subtypes});
    }
    for (const TokenSpec& s : to_create)
    {
        for (int k = 0; k < s.n; ++k)
        {
            CreateToken(state, controller_index, s.p, s.t, s.subs);
            state.battlefield.back().tapped = true;   // tapped and attacking
        }
    }
    return start;
}

// Legend rule (CR 704.5j) for `controller_index`: if they control two or more legendary
// permanents with the same name, all but one are put into the graveyard. Goldfish-minimal:
// keep the OLDEST (lowest battlefield index) of each name and sacrifice the rest. Decks
// with no legendaries (burn, slivers) are untouched (no legendary permanents -> no-op).
// Called at combat start so duplicate legendary lords (e.g. Haytham Kenway x3) cannot
// double-count their continuous buffs in the damage step.
inline void EnforceLegendRule(GameState& state, int controller_index)
{
    std::vector<std::string> seen;
    for (std::vector<Permanent>::iterator it = state.battlefield.begin();
         it != state.battlefield.end(); )
    {
        if (it->controller_index != controller_index
            || !it->card.HasSupertype(Supertype::Legendary))
        {
            ++it;
            continue;
        }
        bool duplicate = false;
        for (const std::string& nm : seen) { if (nm == it->card.m_name) { duplicate = true; break; } }
        if (duplicate)
        {
            state.players[it->owner_index].graveyard.push_back(it->card);
            it = state.battlefield.erase(it);
        }
        else
        {
            seen.push_back(it->card.m_name);
            ++it;
        }
    }
}

// ETB library dig (Acclaimed Contender: "if you control another Knight, look at the top
// five, you may reveal a Knight and put it into your hand; put the rest on the bottom").
// `self` is the permanent that just entered (excluded from the "control another <subtype>"
// condition). Operates on `controller_index`'s library/hand. Deterministic: takes the
// FIRST library card (top-down) whose subtype is in etb_dig_subtypes into hand, then puts
// the other examined cards on the bottom in examined order (printed "random order" is
// unobservable in a goldfish). Returns true if a card was put into hand. Used identically
// by the real game (EffectHandler at resolution) and the rollout (ApplyPlanDirect) so both
// reach the same hand/library state; the dug card is cast on a later turn (no re-solve).
inline bool PerformEtbDig(GameState& state, int controller_index,
                          const CardParams& pp, const Permanent* self)
{
    if (pp.etb_dig_count <= 0) { return false; }

    // Condition: control another creature whose subtype is in etb_dig_requires_subtypes.
    if (!pp.etb_dig_requires_subtypes.empty())
    {
        bool have = false;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller_index) { continue; }
            if (&p == self)                              { continue; }
            if (!p.card.IsCreature())                    { continue; }
            for (const std::string& want : pp.etb_dig_requires_subtypes)
            {
                for (const std::string& cs : p.card.m_subtypes)
                {
                    if (cs == want) { have = true; break; }
                }
                if (have) { break; }
            }
            if (have) { break; }
        }
        if (!have) { return false; }
    }

    Player& ap = state.players[controller_index];

    std::vector<Card> examined;
    int n = std::min(pp.etb_dig_count, static_cast<int>(ap.library.size()));
    for (int i = 0; i < n; ++i)
    {
        examined.push_back(ap.library.front());
        ap.library.erase(ap.library.begin());
    }

    // Legal candidates: every examined card whose subtype matches the dig filter (Knight). The
    // heuristic takes the FIRST; under --claude-play the human picks which one (or declines).
    std::vector<int> legal;
    for (int i = 0; i < static_cast<int>(examined.size()); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(examined[i]);
        const SubtypeSet& subs = d ? d->card.m_subtypes : examined[i].m_subtypes;
        bool match = false;
        for (const std::string& want : pp.etb_dig_subtypes)
        {
            for (const std::string& cs : subs) { if (cs == want) { match = true; break; } }
            if (match) { break; }
        }
        if (match) { legal.push_back(i); }
    }
    int take = legal.empty() ? -1 : legal.front();   // heuristic: first match (byte-identical search)

    // Human play: with at least one legal candidate, let the player choose WHICH match enters hand
    // (or take nothing). Nulled by RevealLogPause for search/rollout, so this never fires during
    // hypothetical scoring -- only the real ETB. An out-of-range reply falls back to the heuristic.
    if (!legal.empty() && g_play_dig_chooser)
    {
        const std::string src = self ? self->card.m_name.str() : std::string("dig");
        int picked = (*g_play_dig_chooser)(state, controller_index, src, examined, legal, take);
        bool ok = (picked == -1);
        for (int li : legal) { if (li == picked) { ok = true; break; } }
        take = ok ? picked : take;
    }

    bool took = false;
    if (take >= 0) { ap.hand.push_back(examined[take]); took = true; }
    for (int i = 0; i < static_cast<int>(examined.size()); ++i)
    {
        if (i == take) { continue; }
        ap.library.push_back(std::move(examined[i]));   // rest to the bottom
    }
    return took;
}

// Hand-aware Aether Vial charge decision: should the active player add a charge counter
// to `vial` this upkeep? The Vial deploys (in the main phase) a creature whose mana value
// EQUALS the counter count, so the optimal level depends on the actual hand, not a fixed
// deck target:
//   - HOLD if the hand has an undeployed creature whose MV == current counters: ticking
//     would strand it (covers "multiple 2-drops, stay on 2" and "1-drops, stay on 1").
//   - else TICK if the hand has a creature with MV > current counters: climb toward it
//     (lets the Vial reach MV 4 for Haytham Kenway when no cheaper creature competes).
//   - else (no relevant creature in hand) fall back to speculative pre-charging toward the
//     deck's dominant MV (state.vial_target_mv) — preserves the old "charge the Vial early
//     while you have no creatures yet, so it is ready when you draw one" behavior.
// Shared by the real engine (AIEngine::DecideVialCharge) and the rollout
// (SimulateBeginningPhase) so both model the same charge policy. Does NOT resolve the
// lethal/tempo tradeoff (deploy a cheaper creature now vs. climb to a lethal bigger one) —
// that is left to a future bounded search branch.
inline bool WantVialCharge(const GameState& state, const Permanent& vial)
{
    const int c      = vial.charge_counters;
    const int target = state.vial_target_mv;     // deck's productive ("dominant MV") ceiling
    if (target <= 0) { return false; }
    const Player& ap = state.players[state.active_player_index];

    // Only creatures at or above the current counter matter: the counter never goes back
    // down, so a creature whose MV is BELOW c can no longer be deployed by this Vial and is
    // irrelevant to the decision (e.g. once we have climbed to 3 for a 3-drop, leftover
    // 2-drops in hand are unreachable and must not keep us from climbing to a finisher).
    int count_at    = 0;   // creatures in hand of exactly the current MV
    int count_next  = 0;   // creatures at exactly the next level (c+1) — the climb target
    int count_above = 0;   // creatures of any higher MV (includes count_next)
    for (const Card& card : ap.hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(card);
        if (!def || !def->card.IsCreature()) { continue; }
        int mv = def->card.m_mana_cost.ManaValue();
        if (mv == c)          { ++count_at; }
        else if (mv == c + 1) { ++count_next; ++count_above; }
        else if (mv > c)      { ++count_above; }
        // mv < c: unreachable (counter only climbs) — ignored.
    }

    // We can deploy a creature at this level (and it is worth holding the counter low for —
    // a 1-drop is trivially hard-cast, so never hold below MV 2):
    if (count_at >= 1 && c >= 2)
    {
        // Prefer climbing to the NEXT level when it has a creature to deploy, since getting
        // the bigger body down sooner usually beats banking one more cheap free deploy
        // (the cheap creature is easy to hard-cast). EXCEPT hold when we have significantly
        // more creatures stuck at THIS level (margin of 2 — the "lots of 2-drops" carve-out:
        // then the free deploys we'd forgo by climbing outweigh the tempo). If the next
        // level is EMPTY (e.g. 2-drops + a far finisher with no 3-drop), we hold and deploy
        // here, climbing toward the finisher only later once this level is exhausted — never
        // wasting a turn climbing through an empty level. The exact margin is a heuristic
        // knob; the fine tempo call (and lethal/value of a specific creature) is search work.
        constexpr int SIGNIFICANTLY_MORE = 2;
        if (count_next >= 1 && count_at < count_next + SIGNIFICANTLY_MORE) { return true; }
        return false;   // hold and deploy at this level
    }

    // Nothing (worth holding) to deploy at this level: climb toward any bigger creature in
    // hand (reaches a finisher like Haytham once the cheaper levels are exhausted), keyed off
    // the current counter so unreachable below-c creatures are ignored.
    if (count_above >= 1) { return true; }

    // No creature at or above this level: pre-charge toward the deck's productive target
    // (keeps the Vial climbing while creature-light so it is ready when one is drawn).
    return c < target;
}

// Returns true if the creature `def` gets replicate when cast this turn.
// Two sources: the card itself has `has_replicate`, or a permanent on the battlefield
// has `grants_replicate_to_subtypes` and the creature matches its subtypes_affected.
inline bool CanReplicate(
    const CardDefinition&          def,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index)
{
    if (def.params.has_replicate) { return true; }
    for (const Permanent& p : battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(p.card);
        if (!ldef || !ldef->params.grants_replicate_to_subtypes) { continue; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : def.card.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
    }
    return false;
}

// Mana produced per tap by a source (depletion lands produce 2). Always >= 1.
inline int ManaProducedPerTap(const CardDefinition& def)
{
    return std::max(1, def.params.produces_amount);
}

// Mana value of the top card of the active player's library (0 if empty). Soulfire Eruption's
// clairvoyant face damage = this. Read identically by the planner, rollout, and executor.
inline int TopLibraryMV(const GameState& state)
{
    const Player& ap = state.ActivePlayer();
    if (ap.library.empty()) { return 0; }
    const Card& top = ap.library.front();
    const CardDefinition* d = CardDatabase::Instance().LookupCached(top);
    return d ? d->card.m_mana_cost.ManaValue() : top.m_mana_cost.ManaValue();
}

// Soulfire Eruption: exile the top card of the active player's library (it dealt its MV in
// damage). "You may play the exiled card" is not modelled, so it is simply removed.
inline void ExileTopLibrary(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (!ap.library.empty()) { ap.library.erase(ap.library.begin()); }
}

// Soulfire Eruption's DIG: "you may play the exiled cards until the end of your next turn." Instead
// of losing the card (ExileTopLibrary), move the top library card to hand as a STAGED card playable
// through next turn (expiry = turn+1, the same primitive Light Up the Stage uses; SimulateToEndImpl
// expires it). This is the card-advantage the deck digs for (find Hinata / combo pieces). Shared by
// EffectHandler (executor) and apply_one (rollout) so the realised dig matches exactly (lockstep).
inline void StageTopLibraryCard(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.library.empty()) { return; }
    Card c = ap.library.front();
    ap.library.erase(ap.library.begin());
    c.m_is_staged     = true;
    c.m_staged_expiry = state.turn_number + 1;
    ap.hand.push_back(std::move(c));
}

// ---- Soulfire Eruption: bounded-heuristic MULTI-TARGET model -----------------------------------
// "Choose any number of target creatures/.../players. For each, exile the top card and deal its
// mana value to that target; you may play the exiled cards until end of your next turn."
//
// Bounded target set: ALWAYS the opponent (face) + each OPPONENT creature; PLUS yourself unless your
// life <= 9 (a self-flip could be a lethal MV-9 Soulfire); PLUS a SEARCHED count of your OWN creatures
// (expendable-first, Hinata last -- killing the lynchpin removes her discount, so the search avoids it).
// Choosing the target SET is a legal non-clairvoyant decision. What is NOT modelled (deliberately, as
// of the faithfulness pass) is steering the random flips: the exiled cards are assigned to targets in
// a FIXED canonical order -- face gets the TOP card, then you, then opponent creatures (board order),
// then your own creatures. The controller CANNOT route the highest-MV flip onto the face, because the
// flips are a random event chosen blind. Creature targets take their card's MV and are DESTROYED if
// lethal (so they leave the target pool for later target-dependent spells -- the faithful interaction).
// ALL exiled cards are staged (the dig of N, the deck's real payoff). Shared exile/stage/assign so
// EffectHandler (real) and ApplyPlanDirect (rollout) realise the identical board (lockstep), and
// SoulfireFaceDamage (the Solve projection) returns the same top-card face damage.
struct SoulfireResult { int face_damage = 0; int self_damage = 0; };

// Number of BASE Soulfire targets (opponent face + opp creatures + self-if-safe). Own creatures are
// NOT counted here -- they are the SEARCHED extra (soulfire_own_targets), added at the call site.
inline int SoulfireTargetCount(const GameState& state, int controller, bool& target_self_out)
{
    const int opp = 1 - controller;
    int n = 1;   // the opponent's face
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index == opp && p.card.IsCreature()) { ++n; } }
    // Self-target only with life > 9: the worst exiled card is Soulfire itself (MV 9), so at 9
    // life a self-target could be lethal -- require >= 10 to survive even the max-MV self-hit.
    target_self_out = state.players[controller].life > 9;
    if (target_self_out) { ++n; }
    return n;
}

// True if this permanent is Hinata (the cost-reducer lynchpin). Soulfire targets own creatures
// most-expendable-FIRST and Hinata LAST, so only the maximal own-target count risks her -- and the
// clairvoyant search rejects that line when losing her makes the rest of the combo unaffordable.
inline bool IsHinataPermanent(const Permanent& p)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    return d && d->params.hinata_cost_reducer;
}

// Battlefield indices of the controller's creatures in Soulfire target order (non-Hinata before
// Hinata; ties keep battlefield order). SoulfireDig targets the FIRST `own_targets` of these.
inline std::vector<int> SoulfireOwnCreatureOrder(const GameState& state, int controller)
{
    std::vector<int> idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == controller && p.card.IsCreature()) { idx.push_back(i); }
    }
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return (IsHinataPermanent(state.battlefield[a]) ? 1 : 0)
             < (IsHinataPermanent(state.battlefield[b]) ? 1 : 0);
    });
    return idx;
}

// How many own creatures the controller could add as Soulfire targets (the search range is 0..K).
inline int SoulfireOwnCreatureCount(const GameState& state, int controller)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index == controller && p.card.IsCreature()) { ++n; } }
    return n;
}

// Battlefield indices of the OPPONENT's creatures in board order. Soulfire targets them after the
// face + you; each is dealt the next exiled card (positionally) and destroyed if that is lethal.
inline std::vector<int> SoulfireOppCreatureOrder(const GameState& state, int controller)
{
    const int opp = 1 - controller;
    std::vector<int> idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == opp && p.card.IsCreature()) { idx.push_back(i); }
    }
    return idx;
}

// Estimate (no mutation): face damage = the mana value of the TOP card of the library. The first
// exiled card ALWAYS goes to the opponent's face -- Soulfire's flips are a random event, so the
// controller cannot steer the highest-MV card onto the face. own_targets no longer affects the face
// (it only deepens the dig + Hinata discount). Matches SoulfireDig's positional assignment (lockstep).
inline int SoulfireFaceDamage(const GameState& state, int controller, int /*own_targets*/ = 0)
{
    const Player& ap = state.players[controller];
    if (ap.library.empty()) { return 0; }
    const Card& c = ap.library.front();
    const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
    return d ? d->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
}

inline bool HinataInPlay(const GameState& state);   // defined below; used by the discount helper

// EXTRA Hinata generic discount from targeting `own_targets` own creatures (each extra target =
// {1} less). EffectiveCost already removed the BASE discount min(cap, base targets); this returns
// the DELTA min(cap, base+own) - min(cap, base) so the caller subtracts it once more. 0 with no
// Hinata. Applied identically at the enumeration, rollout, and executor cost sites (lockstep).
inline int SoulfireOwnTargetDiscount(const CardDefinition& def, const GameState& state,
                                     int controller, int own_targets)
{
    if (!def.params.damage_equals_top_mv || own_targets <= 0) { return 0; }
    if (!HinataInPlay(state)) { return 0; }
    const int cap = def.params.discount_max_targets;
    if (cap <= 0) { return 0; }
    bool dummy = false;
    const int base = SoulfireTargetCount(state, controller, dummy);
    return std::min(cap, base + own_targets) - std::min(cap, base);   // >= 0
}

// Apply (mutates): exile the top N cards and STAGE them all (playable through next turn -- the deck's
// real dig of N), assigning the exiled mana values to targets in a FIXED, NON-clairvoyant order:
//   1. opponent's face   (the TOP card -- the controller cannot steer the biggest flip here)
//   2. you               (next card, only if life > 9 so a self-flip can't be lethal)
//   3. opponent creatures (board order; each dealt the next card, DESTROYED if lethal -> they leave
//                          the target pool for later target-dependent spells, the faithful interaction)
//   4. your own creatures (expendable-first, Hinata last; the SEARCHED own_targets count; destroyed
//                          if lethal -- the search weighs the deeper dig + discount against the loss)
// Returns face/self damage. Deterministic given (state, controller, own_targets) so the rollout
// (ApplyPlanDirect) and executor (EffectHandler) realise the identical board (lockstep). Choosing
// the target SET (which/how-many own creatures, self if safe) is a legal non-clairvoyant decision;
// what is removed here is steering the unknown random flip onto the best target.
inline SoulfireResult SoulfireDig(GameState& state, int controller, int own_targets = 0)
{
    Player& ap = state.players[controller];

    // Target list in canonical order. Self only when safe (a self-exiled Soulfire is MV 9).
    const bool target_self = ap.life > 9;
    std::vector<int> opp_creatures = SoulfireOppCreatureOrder(state, controller);   // board order
    std::vector<int> own           = SoulfireOwnCreatureOrder(state, controller);   // expendable-first
    {
        const int k = std::max(0, own_targets);
        if (static_cast<int>(own.size()) > k)
        {
            std::vector<int> chosen(own.begin(), own.begin() + k);   // heuristic default: first k
            // Human play (claude-play): let the player pick WHICH k own creatures are targeted; the
            // chooser is nulled during search/rollout so the expendable-first default stands there.
            if (g_play_soulfire_chooser && k > 0)
            {
                std::vector<int> pick =
                    (*g_play_soulfire_chooser)(state, controller, "Soulfire Eruption", own, k, chosen);
                // Accept only a well-formed subset: exactly k distinct candidates; else keep default.
                bool ok = (static_cast<int>(pick.size()) == k);
                std::vector<int> seen;
                for (int bi : pick)
                {
                    if (!ok) { break; }
                    if (std::find(own.begin(), own.end(), bi) == own.end()
                        || std::find(seen.begin(), seen.end(), bi) != seen.end()) { ok = false; break; }
                    seen.push_back(bi);
                }
                if (ok)
                {
                    // Keep canonical (heuristic) order among the chosen so the positional card
                    // assignment stays deterministic regardless of the player's click order.
                    std::vector<int> ordered;
                    for (int bi : own) { if (std::find(pick.begin(), pick.end(), bi) != pick.end()) { ordered.push_back(bi); } }
                    chosen = ordered;
                }
            }
            own = chosen;
        }
    }

    const int n = 1 + (target_self ? 1 : 0)
                + static_cast<int>(opp_creatures.size()) + static_cast<int>(own.size());

    // Exile n cards top-down; every exiled card is staged (the dig). Keep per-flip MV in flip order.
    const bool               capture = (g_reveal_logger != nullptr);
    std::vector<int>         flip_nums;
    std::vector<std::string> flip_names;
    std::vector<int>         mvs;
    for (int i = 0; i < n && !ap.library.empty(); ++i)
    {
        Card c = ap.library.front();
        ap.library.erase(ap.library.begin());
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        mvs.push_back(d ? d->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue());
        if (capture) { flip_nums.push_back(c.m_number); flip_names.push_back(c.m_name); }
        c.m_is_staged     = true;
        c.m_staged_expiry = state.turn_number + 1;
        ap.hand.push_back(std::move(c));
    }

    // Assign cards to targets POSITIONALLY (top card -> first target, in canonical order).
    SoulfireResult r;
    std::vector<std::string> disp(mvs.size());
    int slot = 0;
    auto label = [&](const std::string& tgt) {
        if (capture && slot < static_cast<int>(mvs.size()))
        { disp[slot] = "→ " + tgt + " (" + std::to_string(mvs[slot]) + ")"; }
    };

    if (slot < static_cast<int>(mvs.size())) { label("opponent face"); r.face_damage = mvs[slot]; ++slot; }
    if (target_self && slot < static_cast<int>(mvs.size())) { label("you"); r.self_damage = mvs[slot]; ++slot; }

    // Creature targets take real damage; lethal ones are destroyed (SBA) so later casts recompute
    // costs from the new board and later target-dependent spells see one fewer target.
    std::vector<int> dead;
    auto hit_creature = [&](int bi, const char* who) {
        if (slot >= static_cast<int>(mvs.size())) { return; }
        const std::string cname = state.battlefield[bi].card.m_name;   // InternedName -> string
        label(std::string(who) + " " + cname);
        const int dmg = mvs[slot];
        ++slot;
        Permanent& cre = state.battlefield[bi];
        cre.damage += dmg;
        if (cre.damage >= cre.EffectiveToughness()) { dead.push_back(bi); }
    };
    for (int bi : opp_creatures) { hit_creature(bi, "opponent"); }
    for (int bi : own)           { hit_creature(bi, "your"); }

    std::sort(dead.begin(), dead.end(), std::greater<int>());   // descending so erase stays valid
    for (int bi : dead)
    {
        Permanent& cre = state.battlefield[bi];
        state.players[cre.owner_index].graveyard.push_back(cre.card);
        state.battlefield.erase(state.battlefield.begin() + bi);
    }

    if (capture && !flip_nums.empty())
    {
        g_reveal_logger->LogReveal("Soulfire Eruption (exiled)",
                                   flip_nums, flip_names, flip_nums, {}, disp);
    }
    return r;
}

// Untap X of the active player's tapped mana sources (Reality Spasm). Deterministic order
// (lowest battlefield index first) so the rollout and executor agree.
inline void UntapManaSources(GameState& state, int count)
{
    const int active = state.active_player_index;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()) && count > 0; ++i)
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != active || !p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        bool is_src = d && (d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock);
        if (!is_src) { continue; }
        p.tapped = false;
        --count;
    }
}

// Reality Spasm ritual: with Hinata in play (who makes its {X} free), untapping X of your mana
// sources lets you tap them a SECOND time this turn -> +their output. Returns that extra mana =
// the sum of the outputs of the `count` highest-output mana sources the active player controls.
// Modelled as FLOATING mana (NOT a literal untap), so the planner's feasibility credit and the
// executor/rollout resolution both call this one function and agree EXACTLY -> no rollout<->
// executor divergence. Conservative: counts only sources a normal tap could use (lands, mana
// rocks, and non-summoning-sick dorks -- the same set BuildPool draws from).
inline int RitualRefloatMana(const GameState& state, int count)
{
    if (count <= 0) { return 0; }
    const int active = state.active_player_index;
    std::vector<int> outs;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool is_src = d->card.IsLand()
                         || (d->tmpl == CardTemplate::ManaDork && p.CanTap())
                         || d->params.mana_rock;
        if (!is_src) { continue; }
        outs.push_back(ManaProducedPerTap(*d));
    }
    std::sort(outs.begin(), outs.end(), [](int a, int b) { return a > b; });
    int total = 0;
    for (int i = 0; i < static_cast<int>(outs.size()) && i < count; ++i) { total += outs[i]; }
    return total;
}

// Number of mana sources the active player controls (the natural chosen X for Reality Spasm:
// untap them ALL, which Hinata makes free). Used to size the ritual's X in the planner.
inline int ManaSourceCount(const GameState& state)
{
    const int active = state.active_player_index;
    int n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (d->card.IsLand() || (d->tmpl == CardTemplate::ManaDork && p.CanTap()) || d->params.mana_rock) { ++n; }
    }
    return n;
}

// Is `def` a mana RITUAL that floats mana for a same-turn payoff (Reality Spasm's untap-refloat,
// or a fixed mana burst like Irencrag Feat)? Drives the planner's ritual credit and the eager-
// float resolution. False for every non-ritual card -> inert for other decks.
inline bool IsManaRitual(const CardDefinition& def)
{
    return def.params.untap_x_mana_sources || def.params.ritual_floating_mana > 0;
}

// Gross floating mana the ritual `def` adds when cast this turn (its X = `chosen_x` for untap
// rituals). Reality Spasm -> RitualRefloatMana(chosen_x); a fixed burst -> its ritual_floating_mana.
inline int RitualFloatAmount(const GameState& state, const CardDefinition& def, int chosen_x)
{
    if (def.params.untap_x_mana_sources) { return RitualRefloatMana(state, chosen_x); }
    if (def.params.ritual_floating_mana > 0) { return def.params.ritual_floating_mana; }
    return 0;
}

// Apply a ritual's floating mana ON RESOLUTION (shared by EffectHandler -- the real executor --
// and the rollout's apply_one). Adds the gross float as WILD to the turn-scoped reserve
// (state.floating_mana) so a later same-turn cast (Crackle) can spend it. Modelled as floating,
// NOT a literal untap: the planner credits this exact amount, so the search's predicted combo
// and the executed combo never diverge. No-op for non-ritual cards.
inline void ApplyRitualFloat(GameState& state, const CardDefinition& def, int chosen_x)
{
    const int amt = RitualFloatAmount(state, def, chosen_x);
    if (amt > 0) { state.floating_mana.wild += amt; }
}

// Forward decl: HinataInPlay (defined just below). Used by HinataRitualNetBonus.
inline bool HinataInPlay(const GameState& state);

// NET floating mana the active player's in-hand rituals would add THIS turn = gross float minus
// the ritual's own cast cost, assuming Hinata is in play (so an untap ritual's {X} is free). The
// planner adds this to a payoff X-spell's (Crackle's) affordable pool so its max X reaches the
// combo's lethal value. 0 with no Hinata or no ritual in hand -> the X-enum is unchanged for
// every non-Hinata deck. (Gross is credited per-subset in Solve::consider; the base cost lives
// in `combined` there, so pool+gross-cost == pool+net == this -- exact, conservative.)
inline int HinataRitualNetBonus(const GameState& state)
{
    const Player& ap = state.ActivePlayer();
    const bool hinata = HinataInPlay(state);
    int net = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !IsManaRitual(*d)) { continue; }
        // An untap ritual (Reality Spasm) only NETS mana when Hinata makes its {X} free; without
        // her, untapping X costs X mana to recast (break-even). A fixed burst (Irencrag) nets always.
        if (d->params.untap_x_mana_sources && !hinata) { continue; }
        const int count = d->params.untap_x_mana_sources ? ManaSourceCount(state) : 0;
        const int gross = RitualFloatAmount(state, *d, count);
        const int base  = d->card.m_mana_cost.ManaValue();   // RS {X}{U}{U}->2 (X ignored); Irencrag->5
        net += std::max(0, gross - base);
    }
    return net;
}

// Does the active player control a Hinata (cost-reduction static)?
inline bool HinataInPlay(const GameState& state)
{
    const int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.hinata_cost_reducer) { return true; }
    }
    return false;
}

// Count the beneficial targets `def` would choose on the current board to maximise Hinata's
// per-target discount: the opponent (every discounting spell here can point at them) + your
// creatures (extra spread-damage/tap targets) + yourself (if discount_self_safe -- a non-lethal
// per-target effect) + every permanent (incl. the opponent's lands) when the spell targets
// permanents (Magma's "tap two", Reality Spasm's "untap X"). NOT capped here (the cap is the
// spell's max targets / chosen X -- applied by the caller).
inline int HinataAvailableTargets(const CardDefinition& def, const GameState& state)
{
    const int active = state.active_player_index;
    // Soulfire Eruption: the discount must count exactly the targets the resolution heuristic
    // actually chooses (opponent + opp creatures + self-if-life>9), NOT own creatures -- otherwise
    // it would be discounted for targets it never uses (a free, rules-illegal discount). Own
    // creatures are added by the searched own-target count at the call site (see SoulfireDig).
    if (def.params.damage_equals_top_mv)
    {
        bool dummy = false;
        return SoulfireTargetCount(state, active, dummy);
    }
    int avail = 1;                                   // the opponent (a player target)
    if (def.params.discount_self_safe) { avail += 1; }   // yourself
    for (const Permanent& p : state.battlefield)
    {
        if (def.params.discount_targets_permanents) { avail += 1; }   // any permanent (both sides)
        else if (p.card.IsCreature())               { avail += 1; }   // creatures only (both sides)
    }
    (void)active;
    return avail;
}

// Hinata, Dawn-Crowned: "Spells you cast cost {1} less to cast for each target." Returns the
// GENERIC reduction for `def` cast by the active player at the chosen X, or 0 with no Hinata in
// play. discount = min(cap, available targets): cap = discount_max_targets, or the chosen X when
// discount_targets_scale_x (Crackle up to X, Reality Spasm X -> its whole {X} cancels). Applied
// identically at every cast-cost finalization site so planner/rollout/executor agree.
inline int HinataGenericDiscount(const CardDefinition& def, const GameState& state, int chosen_x)
{
    int cap = def.params.discount_targets_scale_x ? chosen_x : def.params.discount_max_targets;
    if (cap <= 0) { return 0; }
    if (!HinataInPlay(state)) { return 0; }
    int avail = HinataAvailableTargets(def, state);
    return cap < avail ? cap : avail;
}

// ---- Reflecting Pool: "{T}: Add one mana of any type that a LAND you control could produce."
// Reflecting Pool has no inherent colour: it can make only what your OTHER lands can make, and
// nothing at all if it is your only land (so a solo / all-Reflecting-Pool hand is effectively
// manaless -> mulligan). Other Reflecting Pools don't count (the rule is circular and adds no
// colour). `ReflectedColors` returns the union of colours the controller's other non-reflecting
// LANDS could produce; `EffectiveProduces` returns this for a reflecting source and the plain
// static produces[] for every other source. Tapped state is irrelevant (a tapped land still
// "could produce" its colour). Scans the battlefield (in_hand=false) or the controller's hand
// (in_hand=true, for mulligan evaluation).
//
// NB: ReflectedColors returns a reference to a thread_local buffer, valid until the next call.
// All call sites consume it within a single source's loop before evaluating another source, and
// the union is built only from NON-reflecting lands (no recursion), so at most one buffer is
// ever live -- safe. Do not hold the returned reference across another EffectiveProduces call.
inline const std::vector<Color>& ReflectedColors(const GameState& state, int controller,
                                                 bool in_hand)
{
    static thread_local std::vector<Color> buf;
    bool seen[6] = { false, false, false, false, false, false };  // W,U,B,R,G,C
    auto mark = [&](const CardDefinition* def)
    {
        if (!def || def->params.reflecting || !def->card.IsLand()) { return; }
        for (Color c : def->params.produces) { seen[static_cast<int>(c)] = true; }
    };
    if (in_hand)
    {
        for (const Card& c : state.players[controller].hand)
        { mark(CardDatabase::Instance().LookupCached(c)); }
    }
    else
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller) { continue; }
            mark(CardDatabase::Instance().LookupCached(p.card));
        }
    }
    buf.clear();
    const Color order[6] = { Color::White, Color::Blue, Color::Black,
                             Color::Red, Color::Green, Color::Colorless };
    for (Color c : order) { if (seen[static_cast<int>(c)]) { buf.push_back(c); } }
    return buf;
}

// The colours a mana source currently produces. Identical to def.params.produces (by const ref,
// zero cost, byte-identical) for every normal source; the dynamic Reflecting-Pool union only for
// a `reflecting` source. controller = the source's controller (active player on the battlefield).
inline const std::vector<Color>& EffectiveProduces(const GameState& state, int controller,
                                                   const CardDefinition& def, bool in_hand = false)
{
    if (!def.params.reflecting) { return def.params.produces; }
    return ReflectedColors(state, controller, in_hand);
}

// Hand-context variant for mulligan / bottoming evaluation, where the relevant "other lands" are
// the ones in the passed-in candidate `hand` (not state's hand). A Reflecting Pool reflects the
// union of the OTHER non-reflecting lands in this hand -- so an all-Reflecting-Pool hand reads as
// manaless (-> mulligan). Returns the static produces[] for every non-reflecting card.
inline const std::vector<Color>& ReflectedColorsInHand(const std::vector<Card>& hand)
{
    static thread_local std::vector<Color> buf;
    bool seen[6] = { false, false, false, false, false, false };
    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || def->params.reflecting || !def->card.IsLand()) { continue; }
        for (Color col : def->params.produces) { seen[static_cast<int>(col)] = true; }
    }
    buf.clear();
    const Color order[6] = { Color::White, Color::Blue, Color::Black,
                             Color::Red, Color::Green, Color::Colorless };
    for (Color c : order) { if (seen[static_cast<int>(c)]) { buf.push_back(c); } }
    return buf;
}

inline const std::vector<Color>& EffectiveProducesInHand(const std::vector<Card>& hand,
                                                         const CardDefinition& def)
{
    if (!def.params.reflecting) { return def.params.produces; }
    return ReflectedColorsInHand(hand);
}

// True if `state.active_player` controls an untapped NON-filter mana source producing
// one of `colors`. A filter land (e.g. Cascade Bluffs) can only make its colours when
// such a feeder exists, since its filter ability requires a coloured mana input.
inline bool HasUntappedNonFilterSourceProducing(const GameState& state,
                                                 const std::vector<Color>& colors)
{
    int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.is_filter || def->params.ramp_filter) { continue; }
        bool is_src = (def->tmpl == CardTemplate::BasicLand)
                   || (def->tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def->params.mana_rock;
        if (!is_src) { continue; }
        for (Color pc : EffectiveProduces(state, active, *def))   // RP -> union of other lands
        {
            for (Color want : colors)
            {
                if (pc == want) { return true; }
            }
        }
    }
    return false;
}

// Scarcity-first mana-payment ordering (DEFAULT ON). The greedy source-selection in TapForCost /
// TapForCostDirect pays each pip from the LEAST flexible qualifying source (rank via the provider's
// ManaSourceRank hook) so the flexible (rainbow) sources stay available -- collapsing the exponential
// TapForCostBacktrack fallback toward "never entered". The backtracker remains the COMPLETE fallback,
// so this only picks WHICH legal payment is committed, never whether one is found. MTG_TAP_LEGACY
// reverts to the old battlefield-order greedy (the standing with/without A/B lever -- deliberately NOT
// tied to MTG_UNPRUNED, since tapping was never in the branch space UNPRUNED opens).
inline bool TapScarcityEnabled()
{
    static const bool v = std::getenv("MTG_TAP_LEGACY") == nullptr;
    return v;
}

// Mana-source RESERVATION (default ON; MTG_NO_RESERVE reverts to no-reservation = byte-identical A/B
// baseline). "Leaving sources up": before paying a cost, the tap functions FIRST try to pay while
// HOLDING back a few special sources ({C}-only manlands, DEPLETION lands, INFLEXIBLE dorks); only if
// the cost cannot be paid WITHOUT them do they get tapped. Slack-only, so weakly dominant -- verified
// strictly improvement-only on depletion decks (treasure_hunt: +wins, 0 games slower). For {C}-
// manlands and inflexible dorks it is inert on the current decks (the scarcity-first tap order
// already spends them last), but harmless (byte-identical) and correct if a future deck differs. The
// real win is DEPLETION: conserving a counter when the plan does not need the extra mana lets the
// land survive to ramp a later turn. It is NOT a search branch: tapping stays one committed choice,
// realised identically by TurnSolver::TapForCostDirect (rollout) and AIEngine::TapForCost (executor)
// so they stay in lockstep. Completeness is unaffected -- the reserved attempt is a first try; the
// normal (reserved=0) payment, with the complete TapForCostBacktrack fallback, still runs when there
// is no slack. NOTE: reserving a FLEXIBLE dork (dual/tri/rainbow) is NOT dominant -- it trades away
// colour-fixing the turn's other casts may need (it regressed Anti-Lifegain) -- so
// ReservableSpecialMask deliberately excludes those.
// SUPERSEDED by whole-turn batch pre-payment (TurnSolver::BatchPrepayMainCasts): pre-loading the
// turn's combined mana leaves every unneeded source untapped for free, so the per-payment reservation
// two-tier is no longer the mechanism. Default OFF (inert -> ReservableSpecialMask returns 0 -> the
// tap wrappers make a single normal attempt, byte-identical to the pre-reservation code). Opt back in
// with MTG_RESERVE only for isolated A/B of the old per-payment scheme.
inline bool ReserveEnabled()
{
    static const bool v = std::getenv("MTG_RESERVE") != nullptr;
    return v;
}

// Whole-turn depletion reservation ("leave out if you can"), applied in
// TurnSolver::BatchPrepayMainCasts: a depletion land's counter is spent the moment it taps, so hold
// back every depletion land the turn's COMBINED main-cast cost can be paid without. Sound because it
// is judged against the whole turn (not a single cast) and only LEAVES A SOURCE UNTAPPED -- never
// stranded, since a post-draw re-solve can still tap it if genuinely needed. Default ON; off-switch
// MTG_NO_DEPLETION_RESERVE for A/B. Distinct from the superseded per-payment ReserveEnabled scheme.
inline bool DepletionReserveEnabled()
{
    static const bool v = std::getenv("MTG_NO_DEPLETION_RESERVE") == nullptr;
    return v;
}

// Whole-turn "hold your beater" reservation (BatchPrepayMainCasts): don't tap the controller's
// GREATEST-power attacker for mana when the turn is payable without it, so it stays untapped to swing
// (and is the creature a pump would land on -- reserving it makes an own-creature pump's target the
// one left up, without needing the target chosen before payment). Only bites MANA-SOURCE creatures
// (dorks/manlands); a non-mana beater is never in the tap set, so reserving it is inert. Default ON;
// off-switch MTG_NO_ATTACKER_RESERVE for A/B. Same "leave out if you can" soundness as depletion.
inline bool AttackerReserveEnabled()
{
    static const bool v = std::getenv("MTG_NO_ATTACKER_RESERVE") == nullptr;
    return v;
}

// Adds one untapped source's mana contribution to an accounting ManaPool, consistent
// with the floating-pool payment logic in TapForCost / TapForCostDirect:
//   - depletion / high-yield lands contribute produces_amount of their colour,
//   - single-colour sources contribute 1 of their colour,
//   - multi-colour (dual) sources contribute 1 wild,
//   - filter lands (Cascade Bluffs) contribute 1 wild when a non-filter feeder of one
//     of their colours exists, else 1 {C} (the colourless-only mode).
// True if the controller has any untapped mana source that can pay a generic {1} WITHOUT
// itself being a ramp filter -- i.e. a feeder for a ramp filter's activation cost. A
// basic land / mana dork pays directly; a filter (Cascade Bluffs) pays via its {C} mode.
inline bool HasUntappedRampFeeder(const GameState& state)
{
    int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.ramp_filter) { continue; }
        bool is_src = (def->tmpl == CardTemplate::BasicLand)
                   || (def->tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def->params.mana_rock;
        if (is_src) { return true; }
    }
    return false;
}

inline void AddSourceToPool(ManaPool& pool, const GameState& state, const CardDefinition& def)
{
    if (def.params.is_filter)
    {
        if (HasUntappedNonFilterSourceProducing(state, def.params.produces)) { ++pool.wild; }
        else { pool.Add(Color::Colorless); }
        return;
    }
    if (def.params.ramp_filter)
    {
        // {1},{T}: Add (each produces colour). Net +1 mana iff a feeder pays the {1};
        // no free mode, so contributes nothing when nothing else is untapped to feed it.
        if (HasUntappedRampFeeder(state)) { ++pool.wild; }
        return;
    }
    int amt = ManaProducedPerTap(def);
    // Reflecting Pool: its colours are the union of the controller's other lands (empty -> adds
    // nothing, the solo-RP dead case). For every normal source this is the static produces[].
    const std::vector<Color>& prod = EffectiveProduces(state, state.active_player_index, def);
    if (prod.size() == 1)      { pool.Add(prod[0], amt); }
    else if (!prod.empty())    { pool.wild += amt; }
}

// Decrements one mana of colour `c` from a floating pool. Returns true on success.
inline bool ConsumeFloating(ManaPool& floating, Color c)
{
    switch (c)
    {
        case Color::White:     if (floating.white     > 0) { --floating.white;     return true; } break;
        case Color::Blue:      if (floating.blue      > 0) { --floating.blue;      return true; } break;
        case Color::Black:     if (floating.black     > 0) { --floating.black;     return true; } break;
        case Color::Red:       if (floating.red       > 0) { --floating.red;       return true; } break;
        case Color::Green:     if (floating.green     > 0) { --floating.green;     return true; } break;
        case Color::Colorless: if (floating.colorless > 0) { --floating.colorless; return true; } break;
    }
    return false;
}

// Consumes one mana of ANY colour from a floating pool (for a generic pip), returning
// the colour drained via `took`. Returns false if the pool is empty.
inline bool ConsumeFloatingAny(ManaPool& floating, Color& took)
{
    const Color order[] = { Color::Colorless, Color::White, Color::Blue,
                            Color::Black, Color::Red, Color::Green };
    for (Color c : order)
    {
        if (ConsumeFloating(floating, c)) { took = c; return true; }
    }
    return false;
}

// Spend pre-produced RESERVE ("floating") mana toward a cost, BEFORE any permanent is tapped.
// Mutates both `reserve` (drained) and `cost` (reduced by what the reserve paid). Colour pips
// are paid by matching colour first, then a wild (any-colour) reserve mana; {C} pips only by
// colourless reserve; generic pips by colourless, then any colour, then wild. A no-op when the
// reserve is empty -> byte-identical for every non-ritual deck. Shared by the executor
// (AIEngine::TapForCost) and the rollout (TurnSolver::TapForCostDirect) so a ritual's floating
// mana is realised identically in both (lockstep). See GameState::floating_mana.
inline void SpendFloatingTowardCost(ManaPool& reserve, ManaCost& cost)
{
    if (reserve.Total() == 0) { return; }
    auto drain = [](int& pip, int& pool) { while (pip > 0 && pool > 0) { --pip; --pool; } };
    // 1) Exact colour matches.
    drain(cost.white,     reserve.white);
    drain(cost.blue,      reserve.blue);
    drain(cost.black,     reserve.black);
    drain(cost.red,       reserve.red);
    drain(cost.green,     reserve.green);
    drain(cost.colorless, reserve.colorless);
    // 2) Wild reserve mana covers any remaining COLOUR pip (not {C}).
    drain(cost.white, reserve.wild);
    drain(cost.blue,  reserve.wild);
    drain(cost.black, reserve.wild);
    drain(cost.red,   reserve.wild);
    drain(cost.green, reserve.wild);
    // 3) Generic pips: WILD first, then colourless, then each colour. Wild-first matters for the
    //    whole-turn batch pre-payment (BatchPrepayMainCasts), which pre-loads floating as the turn's
    //    combined cost with COLOURED pips pinned to their colours and the generic portion as `wild`:
    //    draining generic from wild first keeps each colour reserved for its own coloured pip, so an
    //    earlier cast's generic pip can never strand a later cast's coloured pip within the pool.
    //    Inert for non-batch floats (ritual output is wild -> drained first either way; a pool with
    //    no wild falls straight through to colourless/colours in the original order).
    drain(cost.generic, reserve.wild);
    drain(cost.generic, reserve.colorless);
    drain(cost.generic, reserve.white);
    drain(cost.generic, reserve.blue);
    drain(cost.generic, reserve.black);
    drain(cost.generic, reserve.red);
    drain(cost.generic, reserve.green);
}

// Decides whether a land enters tapped and applies any "as this land enters"
// payments/choices made on entry. Call while the land card is still in the player's
// hand (the reveal check scans the hand). Returns true if the land enters tapped.
//   - Shock land (etb_pay_life_to_untap): the AI pays the life to enter untapped
//     whenever it can keep at least 1 life — early speed dominates in a goldfish.
//   - Reveal land (etb_untap_reveal_subtypes): enters untapped iff a card of a listed
//     subtype (e.g. Island/Mountain) is in hand (Frostboil Snarl).
//   - Otherwise: the plain enters_tapped flag.
inline bool LandEntersTapped(GameState& state, const CardDefinition& def, bool allow_pay_life = true)
{
    const CardParams& pp = def.params;

    if (pp.etb_pay_life_to_untap > 0)
    {
        Player& ap = state.ActivePlayer();
        // allow_pay_life=false (human play, no mana needed this turn) -> take the free tapped
        // entry instead of paying the shock life for mana you would not use.
        if (allow_pay_life && ap.life > pp.etb_pay_life_to_untap)
        {
            ap.life -= pp.etb_pay_life_to_untap;
            return false;
        }
        return true;
    }

    if (!pp.etb_untap_reveal_subtypes.empty())
    {
        const Player& ap = state.ActivePlayer();
        for (const Card& c : ap.hand)
        {
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
            const SubtypeSet& subs = cdef ? cdef->card.m_subtypes : c.m_subtypes;
            for (const std::string& cs : subs)
            {
                for (const std::string& want : pp.etb_untap_reveal_subtypes)
                {
                    if (cs == want) { return false; }
                }
            }
        }
        return true;
    }

    return pp.enters_tapped;
}

// Shared "dig when stuck" gate (cycling / sacrifice-to-draw lands, e.g. Lonely Sandbar,
// Forgotten Cave, Fiery Islet). Decides whether the active player should consider
// spending a surplus land to draw toward action (chiefly Treasure Hunt). The SAME gate
// is consulted by the search (CollectActions, so the clairvoyant rollout models the dig)
// and by the depth-0 executor heuristic (UseSurplusLandAbilities), keeping the rollout
// and the real game consistent.
//
// We dig only when there is no better card source already in hand/yard, and we do NOT
// dig when we can already win. Crucially we DO still dig with a Land's Edge in hand or
// play (we need Treasure Hunt to refill its ammo) UNLESS the hand already holds enough
// lands to be lethal through Land's Edge — then we fire rather than dig.
inline bool ShouldConsiderDig(const GameState& state)
{
    const Player& ap  = state.ActivePlayer();
    const int opp_idx = 1 - state.active_player_index;

    int lands_in_hand = 0;
    int le_rate       = 0;     // Land's Edge damage-per-land, from EITHER zone (in play, or
                               // in hand and about to be deployed -- both make it our clock).
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        if (d->tmpl == CardTemplate::DrawUntilNonland) { return false; }  // Treasure Hunt: cast it
        if (d->params.cascade_max_mv > 0)              { return false; }  // Throes: another engine
        if (d->params.discard_land_damage > 0) { le_rate = std::max(le_rate, d->params.discard_land_damage); }
        if (d->card.IsLand())                  { ++lands_in_hand; }
    }
    for (const Card& c : ap.graveyard)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.retrace) { return false; }   // retrace engine available
    }

    int lands_controlled = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (p.card.IsLand()) { ++lands_controlled; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.discard_land_damage > 0)
        {
            le_rate = std::max(le_rate, d->params.discard_land_damage);
        }
    }
    if (lands_controlled < 2) { return false; }          // don't strand ourselves on mana

    // With a Land's Edge available (in play OR in hand to deploy) we ALREADY have a clock:
    // fire lands for damage every turn. The dig is for FINDING Treasure Hunt to refill ammo,
    // so it is only worth a turn when we are SHORT on ammo. Don't dig once the hand alone
    // already carries a serious threat -- "enough lands in hand" (the user's carve-out):
    //   - lethal now (lands * rate >= opp life), or
    //   - at least half the opponent's life already in Land's Edge ammo.
    // Without any Land's Edge, lands have no outlet, so we always dig to find one / a TH.
    if (le_rate > 0)
    {
        const int opp_life = state.players[opp_idx].life;
        if (lands_in_hand * le_rate >= opp_life)     { return false; }  // already lethal
        if (lands_in_hand * le_rate * 2 >= opp_life) { return false; }  // >= half: enough clock
    }
    return true;
}

// Picks the dig source to use this iteration, given the gate (ShouldConsiderDig) has
// already passed and `pool` is the currently available mana. Prefers cycling a land from
// hand (no permanent lost) over sacrificing a land in play; within each, takes the first
// affordable one in zone order (deterministic). Returns "" if none is affordable.
// out_is_sac distinguishes sac-draw (battlefield) from cycling (hand). The caller pays
// the cost and performs the discard/sacrifice + draw via its own mana path.
inline std::string SelectDigSource(const GameState& state, const ManaPool& pool, bool& out_is_sac)
{
    const Player& ap = state.ActivePlayer();
    out_is_sac = false;
    // Cycling: a land in hand whose cycling cost is affordable.
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !d->params.cycling_cost.has_value()) { continue; }
        if (!pool.CanPay(d->params.cycling_cost.value())) { continue; }
        return c.m_name;
    }
    // Sacrifice-to-draw: an untapped land in play (e.g. Fiery Islet) whose cost is affordable.
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->params.sacrifice_draw_cost.has_value()) { continue; }
        if (!pool.CanPay(d->params.sacrifice_draw_cost.value())) { continue; }
        out_is_sac = true;
        return p.card.m_name;
    }
    return "";
}

// Cheap precondition: does the active player have ANY dig source at all (a cycling land
// in hand or a sac-draw land in play)? Decks without these (burn, slivers) skip the dig
// machinery entirely, keeping their behavior byte-identical.
inline bool HasAnyDigSource(const GameState& state)
{
    const Player& ap = state.ActivePlayer();
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.cycling_cost.has_value()) { return true; }
    }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.sacrifice_draw_cost.has_value()) { return true; }
    }
    return false;
}

// ---- "Look at the top N" disposition: shared apply + heuristic + enumeration ----------------
// Scry / Surveil / Ponder-reorder all reduce to: remove the top `look` cards into a `looked`
// vector (look order), then redistribute them via a TopDisposition (which indices go on top and
// in what order; the rest go to bottom / graveyard / are shuffled). ApplyTopDisposition is the
// single placement routine; HeuristicTopDisposition reproduces the provider-heuristic choice
// (so the autonomous search/normal play is byte-identical); the human chooser supplies its own
// disposition. EnumerateTopDispositions lists the legal player options for the claude-play UI.

// Place the looked-at cards back per `disp`. `looked` is consumed (moved from). The `look`
// cards must ALREADY be removed from the library front by the caller.
inline void ApplyTopDisposition(GameState& state, std::vector<Card>& looked,
                                const TopDisposition& disp, LookKind kind)
{
    Player& ap = state.ActivePlayer();
    const int m = static_cast<int>(looked.size());
    if (kind == LookKind::Reorder && disp.shuffle)
    {
        // "you may shuffle": put everything back on top (order irrelevant), then shuffle the
        // whole library (deterministic, lockstep ShuffleAfterSearch).
        for (std::vector<Card>::reverse_iterator it = looked.rbegin(); it != looked.rend(); ++it)
        { ap.library.insert(ap.library.begin(), std::move(*it)); }
        ShuffleAfterSearch(state, state.active_player_index);
        return;
    }
    std::vector<char> on_top(m, 0);
    std::vector<int>  top_seq;
    for (int idx : disp.top_order)
    { if (idx >= 0 && idx < m && !on_top[idx]) { on_top[idx] = 1; top_seq.push_back(idx); } }
    // Ponder cannot bottom/bin: any looked card the player didn't explicitly order still stays
    // on top (below the ordered ones, in look order).
    if (kind == LookKind::Reorder)
    { for (int i = 0; i < m; ++i) { if (!on_top[i]) { on_top[i] = 1; top_seq.push_back(i); } } }
    // Place the on-top sequence so top_seq[0] ends up at the very top (drawn first).
    for (std::vector<int>::reverse_iterator it = top_seq.rbegin(); it != top_seq.rend(); ++it)
    { ap.library.insert(ap.library.begin(), std::move(looked[*it])); }
    // Cards not kept on top: Scry -> library bottom, Surveil -> graveyard (in look order).
    for (int i = 0; i < m; ++i)
    {
        if (on_top[i]) { continue; }
        if (kind == LookKind::Surveil) { ap.graveyard.push_back(std::move(looked[i])); }
        else                           { ap.library.push_back(std::move(looked[i])); }
    }
}

// The provider-heuristic disposition -- reproduces the ORIGINAL ScryTop/SurveilTop/
// ReorderTopOrShuffle behaviour so the autonomous search and normal play stay byte-identical.
// keep_decision applies to Reorder only (-1 legacy heuristic, 0 forced shuffle, 1 forced keep).
inline TopDisposition HeuristicTopDisposition(const GameState& state, const std::vector<Card>& looked,
                                              LookKind kind, int keep_decision = -1)
{
    const int m = static_cast<int>(looked.size());
    TopDisposition disp;
    if (kind == LookKind::Reorder)
    {
        const bool do_shuffle = (keep_decision == 0)
                             || (keep_decision == -1 && !ResolveProvider(state).KeepReorderTop(state, looked));
        if (do_shuffle) { disp.shuffle = true; return disp; }
        // Wanted (ScryKeepOnTop) first, ordered most-wanted-first (stable); then the rest in
        // look order. Nothing is bottomed -- everything stays on top (the real Ponder drawback).
        std::vector<int> wanted, rest;
        for (int i = 0; i < m; ++i)
        { (ResolveProvider(state).ScryKeepOnTop(state, looked[i]) ? wanted : rest).push_back(i); }
        std::stable_sort(wanted.begin(), wanted.end(), [&](int a, int b) {
            return ResolveProvider(state).SituationalCardRank(state, looked[a])
                 > ResolveProvider(state).SituationalCardRank(state, looked[b]); });
        disp.top_order = wanted;
        disp.top_order.insert(disp.top_order.end(), rest.begin(), rest.end());
        return disp;
    }
    // Scry / Surveil: keep the wanted cards on top in LOOK order; the rest are bottomed / binned.
    for (int i = 0; i < m; ++i)
    { if (ResolveProvider(state).ScryKeepOnTop(state, looked[i])) { disp.top_order.push_back(i); } }
    return disp;
}

// Enumerate the legal player dispositions (for the claude-play decision menu). Scry/Surveil:
// every ordered subset kept on top (rest away). Reorder: every ordering of all N on top, plus a
// shuffle option. N is tiny (1-3) for every modelled card, so the factorial blowup is bounded.
struct TopOption { TopDisposition disp; std::string label; };
inline std::vector<TopOption> EnumerateTopDispositions(LookKind kind, const std::vector<Card>& looked)
{
    const int m = static_cast<int>(looked.size());
    std::vector<TopOption> opts;
    auto name = [&](int i) { return looked[i].m_name.str(); };
    const char* away = (kind == LookKind::Surveil) ? "graveyard"
                     : (kind == LookKind::Scry)     ? "bottom" : "below";
    if (kind == LookKind::Reorder)
    {
        std::vector<int> perm(m);
        for (int i = 0; i < m; ++i) { perm[i] = i; }
        do {
            TopOption o; o.disp.top_order = perm;
            std::string lbl = "Top: ";
            for (int i = 0; i < m; ++i) { lbl += (i ? ", " : ""); lbl += name(perm[i]); }
            o.label = lbl;
            opts.push_back(std::move(o));
        } while (std::next_permutation(perm.begin(), perm.end()));
        TopOption sh; sh.disp.shuffle = true; sh.label = "Shuffle them away";
        opts.push_back(std::move(sh));
        return opts;
    }
    // Scry / Surveil: ordered subsets kept on top.
    for (int mask = 0; mask < (1 << m); ++mask)
    {
        std::vector<int> sub;
        for (int i = 0; i < m; ++i) { if (mask & (1 << i)) { sub.push_back(i); } }
        std::sort(sub.begin(), sub.end());
        do {
            TopOption o; o.disp.top_order = sub;
            std::string lbl;
            if (sub.empty()) { lbl = std::string("All to ") + away; }
            else
            {
                lbl = "Top: ";
                for (size_t i = 0; i < sub.size(); ++i) { lbl += (i ? ", " : ""); lbl += name(sub[i]); }
                if (static_cast<int>(sub.size()) < m) { lbl += std::string("  (rest to ") + away + ")"; }
            }
            o.label = lbl;
            opts.push_back(std::move(o));
        } while (std::next_permutation(sub.begin(), sub.end()));
    }
    return opts;
}

// Scry N (e.g. Temple of Epiphany): look at the top N cards and bottom the unwanted ones using a
// deck-aware heuristic (HeuristicTopDisposition), then keep the rest on top in look order. Under
// --claude-play g_play_top_chooser is set (and RevealLogPause nulls it during the search, so this
// is REAL resolution only) and the human picks the disposition instead. Byte-identical for search.
inline void ScryTop(GameState& state, int n, const std::string& source = "Scry")
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 0) { return; }

    std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);
    TopDisposition disp = (g_play_top_chooser)
        ? (*g_play_top_chooser)(state, source, looked, LookKind::Scry)
        : HeuristicTopDisposition(state, looked, LookKind::Scry);

    // Reveal capture (real play only; null during search): seen cards in look order, and which
    // were kept on top vs bottomed -- read from the chosen disposition BEFORE looked is consumed.
    if (g_reveal_logger)
    {
        std::vector<int> seen_nums, kept_nums, bottom_nums;
        std::vector<std::string> seen_names;
        std::vector<char> on_top(look, 0);
        for (int idx : disp.top_order) { if (idx >= 0 && idx < look) { on_top[idx] = 1; } }
        for (int i = 0; i < look; ++i)
        {
            seen_nums.push_back(looked[i].m_number);
            seen_names.push_back(looked[i].m_name);
            (on_top[i] ? kept_nums : bottom_nums).push_back(looked[i].m_number);
        }
        g_reveal_logger->LogReveal(source, seen_nums, seen_names, kept_nums, bottom_nums);
    }

    for (int _e = 0; _e < look; ++_e) { ap.library.erase(ap.library.begin()); }
    ApplyTopDisposition(state, looked, disp, LookKind::Scry);
}

// Ponder-style "look at the top N, put them back in ANY ORDER, you may shuffle, then draw".
// Unlike Scry this CANNOT bottom cards: it is all-or-nothing. We keep all N on top (wanted
// cards first, so the immediately-following draw takes a wanted one and the rest stay on
// top -- the real Ponder drawback) WHEN at least one of the top N is wanted; otherwise we
// shuffle the whole library (deterministic, lockstep ShuffleAfterSearch) so the draw is a
// fresh card instead of a dead one. The keep-vs-shuffle call uses the same ScryKeepOnTop
// predicate the provider already exposes, so it is deck-aware without a new hook.
// keep_decision: -1 = legacy heuristic (shuffle iff NONE of the top N is wanted); 0 = the SEARCH
// chose to shuffle them away; 1 = the SEARCH chose to keep them on top. Per the user's design, the
// keep-vs-shuffle CALL is searched (a 2-way cast branch, ponder_keep) while the provider's
// ScryKeepOnTop only supplies the ORDER (wanted cards first) within the keep branch. Both the
// rollout and the executor pass the same carried decision -> identical realised library (lockstep).
inline void ReorderTopOrShuffle(GameState& state, int n, const std::string& source = "Ponder",
                                int keep_decision = -1)
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 0) { return; }

    std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);
    // Human play (claude-play) picks the reorder/shuffle; otherwise the searched keep_decision +
    // provider heuristic do (byte-identical to the original -- HeuristicTopDisposition reproduces
    // the wanted-first ordering, and in legacy mode the shuffle branch still only fires with an
    // empty `wanted`, so the pre-shuffle order is the same look order).
    TopDisposition disp = (g_play_top_chooser)
        ? (*g_play_top_chooser)(state, source, looked, LookKind::Reorder)
        : HeuristicTopDisposition(state, looked, LookKind::Reorder, keep_decision);

    if (g_reveal_logger)
    {
        std::vector<int> seen_nums; std::vector<std::string> seen_names;
        for (const Card& c : looked) { seen_nums.push_back(c.m_number); seen_names.push_back(c.m_name); }
        if (disp.shuffle)
        {
            g_reveal_logger->LogReveal(source + " (shuffle)", seen_nums, seen_names,
                                       /*kept*/ std::vector<int>{}, /*bottomed*/ seen_nums);
        }
        else
        {
            // Final top order = ordered indices, then any unordered ones (Ponder keeps all on top).
            std::vector<int> kept_nums; std::vector<char> placed(look, 0);
            for (int idx : disp.top_order)
            { if (idx >= 0 && idx < look && !placed[idx]) { placed[idx] = 1; kept_nums.push_back(looked[idx].m_number); } }
            for (int i = 0; i < look; ++i) { if (!placed[i]) { kept_nums.push_back(looked[i].m_number); } }
            g_reveal_logger->LogReveal(source, seen_nums, seen_names, kept_nums, /*bottomed*/ std::vector<int>{});
        }
    }

    for (int _e = 0; _e < look; ++_e) { ap.library.erase(ap.library.begin()); }
    ApplyTopDisposition(state, looked, disp, LookKind::Reorder);
}

// Expressive Iteration {U}{R}: "Look at the top three cards of your library. Put one into your hand,
// put one on the bottom of your library, and exile one. You may play the exiled card this turn."
// Model: look at the top 3, rank by ScryKeepOnTop (wanted first); the most-wanted goes to HAND
// (banked), the next is EXILED and STAGED playable THIS TURN ONLY (m_staged_expiry = turn_number),
// the least goes to the BOTTOM. Shared by the executor (ResolveDrawSpell) and the rollout -> lockstep.
// (NOT modelled as draw-2: the second card can only be played this turn, not banked -- the prior
// draw:2 + cast_scry:3 entry over-rated it.)
inline void ResolveExpressiveIteration(GameState& state)
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(3, static_cast<int>(ap.library.size()));
    if (look == 0) { return; }

    std::vector<Card>        cards;
    std::vector<int>         seen_nums;
    std::vector<std::string> seen_names;
    const bool capture = (g_reveal_logger != nullptr);
    for (int i = 0; i < look; ++i)
    {
        Card c = ap.library.front();
        ap.library.erase(ap.library.begin());
        if (capture) { seen_nums.push_back(c.m_number); seen_names.push_back(c.m_name); }
        cards.push_back(std::move(c));
    }
    // Heuristic default split (most-wanted-first by the provider's situational ranking): the
    // best card -> HAND, the next -> EXILE (playable this turn), the least -> BOTTOM. Computed as
    // indices INTO `cards` (look order preserved) so the human chooser can reference the cards as
    // shown. For a provider without a situational override the rank is ScryKeepOnTop?1:0, so the
    // default order is byte-identical to the old binary; HinataProvider supplies the fine
    // combo-aware order. A stable sort of an index list keeps look order within a rank tier.
    std::vector<int> order(look);
    for (int i = 0; i < look; ++i) { order[i] = i; }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return ResolveProvider(state).SituationalCardRank(state, cards[a])
             > ResolveProvider(state).SituationalCardRank(state, cards[b]);
    });
    int hand_idx   = order[0];
    int exile_idx  = (look >= 2) ? order[1] : -1;

    // Human play (claude-play): let the player choose WHICH looked card is banked to hand and
    // which is exiled to play this turn (the remaining one -> bottom). Nulled in search/rollout
    // (RevealLogPause) -> the heuristic split there, byte-identical. Validate the reply: distinct,
    // in range; fall back to the heuristic on anything malformed.
    if (g_play_ei_chooser && look >= 2)
    {
        std::pair<int,int> ch = (*g_play_ei_chooser)(state, cards, hand_idx, exile_idx);
        if (ch.first >= 0 && ch.first < look && ch.second >= 0 && ch.second < look
            && ch.first != ch.second)
        { hand_idx = ch.first; exile_idx = ch.second; }
    }

    // Apply the split. Move by index: hand_idx -> hand, exile_idx -> staged exile (this turn only),
    // the remaining index (if look == 3) -> bottom. Mark which indices are placed so the leftover
    // is unambiguous.
    std::vector<int> kept_nums, bottom_nums;
    kept_nums.push_back(cards[hand_idx].m_number);
    ap.hand.push_back(cards[hand_idx]);                       // [hand_idx] -> hand (banked)
    if (exile_idx >= 0)
    {
        Card s = cards[exile_idx];
        s.m_is_staged     = true;
        s.m_staged_expiry = state.turn_number;   // this turn only (vs turn+1 for Light Up / Soulfire)
        kept_nums.push_back(s.m_number);
        ap.hand.push_back(std::move(s));                      // [exile_idx] -> exiled, playable now
    }
    for (int i = 0; i < look; ++i)                            // the leftover -> bottom
    {
        if (i == hand_idx || i == exile_idx) { continue; }
        bottom_nums.push_back(cards[i].m_number);
        ap.library.push_back(cards[i]);
    }
    if (capture && !seen_nums.empty())
    {
        g_reveal_logger->LogReveal("Expressive Iteration", seen_nums, seen_names, kept_nums, bottom_nums);
    }
}

// Karoo bounce land ETB (Izzet Boilerworks): return one of `controller`'s lands to hand. The
// bounce is MANDATORY (CR -- "return a land you control"), so there is always a victim. Deterministic
// so the rollout and executor agree: prefer one of the controller's OTHER lands, tapped first
// (already spent this turn -> no mana lost) then the lowest-index other land; only if the karoo is
// the controller's ONLY land does it return ITSELF (self_index, the just-entered karoo, always the
// last-pushed battlefield element). A self-bounce nets no land in play AND consumes the land drop,
// so the land-selection search correctly avoids playing a Karoo as your only land -- the old code
// bounced NOTHING here, which masked that cost and let the AI play a lone Karoo (a rules-illegal
// free land; see Hinata seed 1009 T1).
inline void BounceKarooLand(GameState& state, int controller, int self_index)
{
    // Choose which of our lands to return to hand. Preference, best first:
    //   (1) NEVER bounce another Karoo bounce land -- replaying it just triggers ANOTHER
    //       ETB bounce (a tempo-negative loop), so avoid it unless it is the only option;
    //   (2) prefer a land that is already TAPPED (spent this turn) so returning it costs no
    //       mana this turn -- the play-at-end timing (ApplyPlanDirect / AIEngine defer the
    //       Karoo until after the main casts) means the lands we needed are already tapped;
    //   (3) among those, prefer a land that ENTERS UNTAPPED when replayed (a basic / untapped
    //       dual) over one that enters tapped (a tapland / another Karoo), so the forced
    //       replay gives mana immediately rather than wasting next turn's tempo.
    // Lexicographic via additive weights; ties break to the lowest index (deterministic).
    auto land_score = [&](int i) -> long
    {
        const Permanent& p = state.battlefield[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        const bool is_karoo = d && d->params.etb_bounce_land;
        const bool enters_untapped =
            !(d && (d->params.enters_tapped || d->params.enters_tapped_with_depletion > 0));
        long s = 0;
        if (is_karoo)        { s -= 1000; }   // (1) avoid the bounce loop
        if (p.tapped)        { s += 100;  }   // (2) no mana lost this turn
        if (enters_untapped) { s += 10;   }   // (3) clean replay
        return s;
    };
    // Legal returnable lands (controller's lands other than the karoo), best-scored first kept as
    // the heuristic default; the karoo itself is the forced fallback when it is the only land.
    std::vector<int> legal;
    int  pick = -1;
    long best = 0;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        if (i == self_index) { continue; }
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller || !p.card.IsLand()) { continue; }
        legal.push_back(i);
        const long s = land_score(i);
        if (pick < 0 || s > best) { pick = i; best = s; }   // strict > => lowest index wins ties
    }
    if (pick < 0) { pick = self_index; legal.push_back(self_index); }  // mandatory: only the karoo
    // Human play (claude-play): let the player choose which land to return. The chooser gets the
    // legal battlefield indices + the heuristic's pick (as an index INTO `legal`); RevealLogPause
    // nulls it for search/enumeration, so the autonomous heuristic above stands there.
    if (g_play_bounce_chooser && legal.size() > 1)
    {
        int hidx = 0;
        for (size_t i = 0; i < legal.size(); ++i) { if (legal[i] == pick) { hidx = static_cast<int>(i); break; } }
        const std::string src = (self_index >= 0 && self_index < static_cast<int>(state.battlefield.size()))
                              ? state.battlefield[self_index].card.m_name.str() : std::string("Bounce land");
        int chosen = (*g_play_bounce_chooser)(state, controller, src, legal, hidx);
        if (chosen >= 0 && chosen < static_cast<int>(legal.size())) { pick = legal[chosen]; }
    }
    if (pick < 0 || pick >= static_cast<int>(state.battlefield.size())) { return; }  // defensive
    Card c = state.battlefield[pick].card;
    c.m_is_staged = false;
    c.m_def = nullptr;
    state.players[controller].hand.push_back(c);
    state.battlefield.erase(state.battlefield.begin() + pick);
}

// Surveil N (e.g. Thundering Falls): like ScryTop, but unwanted cards go to the GRAVEYARD
// instead of the library bottom -- true deck thinning. Uses the same deck-aware keep/bin
// heuristic as ScryTop: keep nonlands always; keep lands only while they are still useful
// (a DrawUntilNonland in hand wants land fuel, or fewer than two lands are in play),
// otherwise bin the surplus land to the graveyard to dig toward action.
inline void SurveilTop(GameState& state, int n, const std::string& source = "Surveil")
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 0) { return; }

    std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);
    TopDisposition disp = (g_play_top_chooser)
        ? (*g_play_top_chooser)(state, source, looked, LookKind::Surveil)
        : HeuristicTopDisposition(state, looked, LookKind::Surveil);

    const bool capture = (g_reveal_logger != nullptr);
    if (capture)
    {
        std::vector<int> seen_nums, kept_nums, grave_nums;
        std::vector<std::string> seen_names;
        std::vector<char> on_top(look, 0);
        for (int idx : disp.top_order) { if (idx >= 0 && idx < look) { on_top[idx] = 1; } }
        for (int i = 0; i < look; ++i)
        {
            seen_nums.push_back(looked[i].m_number);
            seen_names.push_back(looked[i].m_name);
            (on_top[i] ? kept_nums : grave_nums).push_back(looked[i].m_number);
        }
        // For surveil the "bottomed" set is really the graveyard set; the viewer labels
        // the disposition from the source, so reusing the bottomed slot is fine.
        g_reveal_logger->LogReveal(source, seen_nums, seen_names, kept_nums, grave_nums);
    }

    for (int _e = 0; _e < look; ++_e) { ap.library.erase(ap.library.begin()); }
    ApplyTopDisposition(state, looked, disp, LookKind::Surveil);
    return;
}

// ---- Fetchland resolution (Windswept Heath etc.) ------------------------------
//
// Puts a land described by `def` onto the active player's battlefield, resolving its
// enters-tapped / shock / reveal-untap / depletion / scry / surveil effects. Does NOT
// touch the hand or the land-drop count -- it only constructs the permanent and applies
// its on-entry effects, so it can serve both a hand land drop (not currently wired this
// way to keep that path byte-identical) and a fetchland pulling a land from the library.
// `card_number` (when >= 0) stamps the permanent's card number for real-game logging.
inline void EnterLand(GameState& state, const CardDefinition& def, int card_number = -1)
{
    bool tapped = LandEntersTapped(state, def);
    Permanent perm;
    perm.card              = def.card;
    if (card_number >= 0) { perm.card.m_number = card_number; }
    perm.controller_index  = state.active_player_index;
    perm.owner_index       = state.active_player_index;
    perm.entered_this_turn = true;
    perm.tapped            = tapped;
    if (def.params.enters_tapped_with_depletion > 0)
    {
        Counter dep;
        dep.type  = Counter::Type::Depletion;
        dep.count = def.params.enters_tapped_with_depletion;
        perm.counters.push_back(dep);
    }
    state.battlefield.push_back(perm);
    if (def.params.etb_scry > 0)    { ScryTop(state, def.params.etb_scry); }
    if (def.params.etb_surveil > 0) { SurveilTop(state, def.params.etb_surveil); }
}

// Fetchland target decision (Windswept Heath etc.): returns the NARROWED list of library
// land NAMES this fetchland should consider, best first. Another instance of the heuristic-
// then-search pattern (see TutorCandidates): when the heuristic has a CLEAR best it returns
// exactly ONE candidate (no search); only when several are genuinely equivalent on the things
// the heuristic can judge -- a real "which colour to commit to" tradeoff -- does it return
// more than one for the search to decide (Pass 2). Returns {} when no library land matches.
//
// Priority (user's rule for THIS deck -- a 4-colour shell that is EASY on colour: one source
// of each of W/B/R/G plus a Forest lets it cast everything; R/G/W must come from DISTINCT
// sources for Fiery Justice {R}{G}{W}):
//   (0) FOREST FIRST when we don't already control/hold one -- a Forest unlocks the mana dorks
//       ({G}) AND the free alt-cost spells ("If you control a Forest, ... ", modelled as
//       alt_cost_requires_subtype). Generalised: prefer a candidate carrying the subtype that
//       an alt-cost card in hand requires, if we don't already have that subtype in play/hand.
//   (1) a colour NEEDED THIS TURN (coloured pip of a nonland hand card we can't yet make);
//   (2) deck-wide fixing -- a colour the deck wants we are MISSING across battlefield + the
//       other (non-fetch) lands in hand (drives toward "one of each W/B/R/G");
//   (3) breadth toward colours not on our battlefield; multi-colour dual/shock over a basic.
// Dedups by name. The fetchland's own [W,B,R,G] `produces` and any OTHER fetchland in hand are
// excluded from "colours we have" (their colour is the thing being resolved / unknown).
inline std::vector<std::string> FetchCandidates(const GameState& state, int controller_index,
                                                const CardParams& fetch_pp)
{
    const Player& ap = state.players[controller_index];

    // Unpruned audit (MTG_UNPRUNED): return EVERY legal fetch target (distinct names of
    // library lands whose subtypes match the fetchland's target types) so the search
    // branches over all of them, instead of the heuristic-ranked pick. TurnSolver lifts
    // its fetch-target search cap in the same mode.
    if (DecisionUnpruned(UnprunedGate::Fetch))
    {
        std::vector<std::string>        all;
        std::unordered_set<std::string> seen;
        for (const Card& lc : ap.library)
        {
            const CardDefinition* d    = CardDatabase::Instance().LookupCached(lc);
            const Card&           card = d ? d->card : lc;
            if (!card.IsLand()) { continue; }
            bool match = false;
            for (const std::string& want : fetch_pp.fetch_land_types)
            {
                for (const std::string& s : card.m_subtypes) { if (s == want) { match = true; break; } }
                if (match) { break; }
            }
            if (match && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
        }
        return all;
    }

    constexpr int NC = 6;   // Color enum cardinality (W,U,B,R,G,C)
    using ColorSet = std::array<bool, NC>;   // stack-resident; avoids per-call vector<bool> allocs + bit-proxy cost
    auto add_colors = [](ColorSet& set, const std::vector<Color>& cs)
    {
        for (Color c : cs) { set[static_cast<int>(c)] = true; }
    };

    // Colours we already have on the battlefield (lands + mana dorks we control).
    ColorSet have{};
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock) { add_colors(have, d->params.produces); }
    }
    // Plus colours from OTHER (non-fetch) lands in hand -- part of the deck-fixing equation.
    ColorSet have_or_hand = have;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
        add_colors(have_or_hand, d->params.produces);
    }

    // Critical subtype (e.g. "Forest"): the subtype an alt-cost card in HAND requires
    // ("If you control a Forest, rather than pay ...") -- a Forest also makes {G} for the
    // dorks. We only weight it when we DON'T already control/hold that subtype.
    std::string crit_subtype;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && !d->params.alt_cost_requires_subtype.empty())
        { crit_subtype = d->params.alt_cost_requires_subtype; break; }
    }
    bool have_crit = crit_subtype.empty()
                  || ControlsSubtype(state, controller_index, crit_subtype);
    if (!have_crit)   // also satisfied by a non-fetch land of that subtype already in hand
    {
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
            for (const std::string& s : d->card.m_subtypes) { if (s == crit_subtype) { have_crit = true; break; } }
            if (have_crit) { break; }
        }
    }
    bool want_crit = !crit_subtype.empty() && !have_crit;

    // Colours wanted this turn (coloured pips of nonland cards in hand) and deck-wide.
    ColorSet want_turn{}, want_deck{};
    auto note_cost = [&](const Card& card, ColorSet& set)
    {
        const ManaCost& mc = card.m_mana_cost;
        if (mc.white > 0)  { set[static_cast<int>(Color::White)] = true; }
        if (mc.blue  > 0)  { set[static_cast<int>(Color::Blue)]  = true; }
        if (mc.black > 0)  { set[static_cast<int>(Color::Black)] = true; }
        if (mc.red   > 0)  { set[static_cast<int>(Color::Red)]   = true; }
        if (mc.green > 0)  { set[static_cast<int>(Color::Green)] = true; }
    };
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        if (card.IsLand()) { continue; }
        note_cost(card, want_turn);
        note_cost(card, want_deck);
    }
    for (const Card& c : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        if (card.IsLand()) { continue; }
        note_cost(card, want_deck);
    }

    // Score each distinct candidate land (subtype in fetch_land_types) and sort best-first.
    // dup_pref is the LOWEST-priority tiebreak: among otherwise-equal options prefer a White
    // source, then a Green one -- the only two colours this deck ever wants in MULTIPLES
    // (White for Fiery Justice {W} + Swords {W} in a turn; Green for the {G} dorks + the free
    // {G}-gated spells + Fiery Justice {G}). It rarely changes the win, but collapses the
    // common early-game "which Forest dual" tie to ONE candidate, so the search needn't branch.
    // Keep the single best candidate in ONE PASS over the library -- no candidate vector, dedup
    // set, or sort (this is on the per-fetch-decision hot path). A candidate beats the incumbent
    // on the first differing key (all "higher is better"); a full tie keeps the incumbent, which
    // -- because we scan the library in order -- is the earliest, exactly reproducing the old
    // sort-by-(keys desc, insertion order asc) + front(). Duplicate names tie their first
    // occurrence on every key and so never displace it, so an explicit dedup set is unnecessary
    // (it changes nothing about the winner). Keys, lowest-priority last:
    //   gives_crit (carries the critical subtype we still need, e.g. Forest unlock)
    //   s_turn (new colours wanted THIS turn) / s_deck (deck-wide) / s_breadth (new colours)
    //   multi (fixes >1 colour) / dup_pref (White=2 then Green=1, the only doubled colours)
    //   shock (prefer a shock dual over a basic -- life is irrelevant in a goldfish)
    bool        have_best = false;
    std::string best_name;
    int b_gc = 0, b_st = 0, b_sd = 0, b_sb = 0, b_multi = 0, b_dup = 0, b_shock = 0;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        const Card& card = d ? d->card : lc;
        if (!card.IsLand()) { continue; }
        bool type_ok = false;
        for (const std::string& want : fetch_pp.fetch_land_types)
        {
            for (const std::string& cs : card.m_subtypes) { if (cs == want) { type_ok = true; break; } }
            if (type_ok) { break; }
        }
        if (!type_ok) { continue; }

        int gives_crit = 0;
        if (want_crit)
        {
            for (const std::string& cs : card.m_subtypes) { if (cs == crit_subtype) { gives_crit = 1; break; } }
        }
        const std::vector<Color>& prod = d ? d->params.produces : std::vector<Color>{};
        int s_turn = 0, s_deck = 0, s_breadth = 0;
        for (Color c : prod)
        {
            int ci = static_cast<int>(c);
            if (want_turn[ci] && !have[ci])         { ++s_turn; }
            if (want_deck[ci] && !have_or_hand[ci]) { ++s_deck; }
            if (!have[ci] && ci != static_cast<int>(Color::Colorless)) { ++s_breadth; }
        }
        int multi = static_cast<int>(prod.size()) > 1 ? 1 : 0;
        bool pw = false, pg = false;
        for (Color c : prod)
        {
            if (c == Color::White) { pw = true; }
            if (c == Color::Green) { pg = true; }
        }
        int dup_pref = (pw ? 2 : 0) + (pg ? 1 : 0);
        int shock    = (d && d->params.etb_pay_life_to_untap > 0) ? 1 : 0;

        bool better;
        if      (gives_crit != b_gc)    { better = gives_crit > b_gc; }
        else if (s_turn     != b_st)    { better = s_turn     > b_st; }
        else if (s_deck     != b_sd)    { better = s_deck     > b_sd; }
        else if (s_breadth  != b_sb)    { better = s_breadth  > b_sb; }
        else if (multi      != b_multi) { better = multi      > b_multi; }
        else if (dup_pref   != b_dup)   { better = dup_pref   > b_dup; }
        else if (shock      != b_shock) { better = shock      > b_shock; }
        else                            { better = false; }  // full tie -> keep the earlier incumbent

        if (!have_best || better)
        {
            have_best = true;
            best_name = lc.m_name;
            b_gc = gives_crit; b_st = s_turn; b_sd = s_deck; b_sb = s_breadth;
            b_multi = multi;   b_dup = dup_pref; b_shock = shock;
        }
    }

    // Return exactly the single best (never a tied group) so a fetch is always decided by the
    // heuristic and the search never branches over fetch targets. Empty only on a true whiff.
    std::vector<std::string> out;
    if (have_best) { out.push_back(best_name); }
    return out;
}

// Execute a fetchland (Windswept Heath etc.): pull `target_name` (empty -> the heuristic's
// top FetchCandidates pick) from the library, make it enter the battlefield (resolving the
// fetched land's own enters-tapped/shock choice), and pay 1 life. The caller is responsible
// for removing the fetchland from hand, using its land drop, and sending it to the graveyard
// -- PerformFetch only resolves the search-and-put-into-play. The library order past the
// fetched card is a goldfish-irrelevant simplification (no shuffle modelled). Shared by the
// real engine (AIEngine) and the rollout (TurnSolver/ApplyPlanDirect) so both fetch alike.
inline void PerformFetch(GameState& state, int controller_index,
                         const CardParams& fetch_pp, const std::string& target_name = "")
{
    Player& ap = state.players[controller_index];
    ap.life -= 1;   // pay 1 life (irrelevant vs a passive opponent, but faithful)

    std::string want = target_name;
    if (want.empty())
    {
        std::vector<std::string> cands = ResolveProvider(state).FetchCandidates(state, controller_index, fetch_pp);
        if (cands.empty()) { return; }   // whiff: no legal target (life already paid)
        want = cands.front();
    }
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
    {
        if (ap.library[i].m_name == want) { idx = i; break; }
    }
    if (idx < 0) { return; }   // chosen target no longer present (search/real drift guard)
    Card lc = ap.library[idx];
    ap.library.erase(ap.library.begin() + idx);
    const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
    if (def) { EnterLand(state, *def, lc.m_number); }
    else
    {
        // Unknown card: still put it onto the battlefield as a plain untapped land.
        Permanent perm;
        perm.card              = lc;
        perm.controller_index  = state.active_player_index;
        perm.owner_index       = state.active_player_index;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);
    }
    // Searching the library shuffles it (CR 701.19). Deterministic + lockstep; no-op
    // unless MTG_SEARCH_SHUFFLE is set.
    ShuffleAfterSearch(state, controller_index);
}

// Removes one depletion counter from a land tapped for mana (e.g. Saprazzan Skerry,
// Sandstone Needle). Call right after marking the source tapped. A counter at 0 is
// left as a marker that SacrificeDepletedLands then cleans up.
inline void DecrementDepletionOnTap(Permanent& source)
{
    for (Counter& ctr : source.counters)
    {
        if (ctr.type == Counter::Type::Depletion && ctr.count > 0) { --ctr.count; return; }
    }
}

// Backtracking mana-payment FALLBACK for filter-heavy boards the per-pip greedy
// solvers (TurnSolver::TapForCostDirect / AIEngine::TapForCost) cannot solve. The
// greedy taps plain sources directly per pip and can strand filter lands (Cascade
// Bluffs `is_filter`, Ferrous Lake `ramp_filter`) that need a feeder, so a legal cost
// that requires a filter CHAIN (a plain seed -> filter -> filter -> ...) is wrongly
// rejected. This searches source ACTIVATIONS with backtracking, checking CanPay at each
// node, and finds a chaining solution if one exists. On success it leaves the chosen
// sources tapped in `state` (mirroring the greedy's tap side-effects: depletion decrement
// and tap self-damage); on failure it leaves `state` exactly as it found it. Intended to
// run ONLY after the greedy fails, so every payment the greedy already solves stays
// byte-identical. `floating` carries mana produced-but-unconsumed down the recursion.
// Hash for the failure-memo key (active player's tapped-source bitmask, packed floating pool).
struct TapBacktrackMemoHash
{
    std::size_t operator()(const std::pair<std::uint64_t, std::uint64_t>& p) const
    {
        return std::hash<std::uint64_t>{}(p.first)
             ^ (std::hash<std::uint64_t>{}(p.second) * 1099511628211ull);
    }
};
using TapBacktrackMemo =
    std::unordered_set<std::pair<std::uint64_t, std::uint64_t>, TapBacktrackMemoHash>;

// Backtracker-entry counter (MTG_TAP_STATS, off by default = zero cost). Counts top-level
// TapForCostBacktrack invocations -- i.e. how often the greedy stranded and fell to the exponential
// solver. The scarcity-first greedy (MTG_TAP_SCARCITY) aims to drive this toward zero; run OFF vs ON
// single-threaded and compare. Prints one line at exit.
namespace tapstats
{
    inline bool Enabled() { static const bool v = std::getenv("MTG_TAP_STATS") != nullptr; return v; }
    inline std::atomic<std::uint64_t> g_backtrack_entries{0};
    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            std::fprintf(stderr, "\n=== TAP STATS: TapForCostBacktrack top-level entries = %llu ===\n",
                         (unsigned long long)g_backtrack_entries.load());
        }
    };
    inline Dumper g_dumper;
}

// Branch-and-bound gate for the backtracker (MTG_NO_MAXMANA_GATE disables it; A/B perf lever).
// Default ON: it is LOSSLESS (an upper bound only ever short-circuits provably-unpayable costs).
inline bool MaxManaGateEnabled()
{ static const bool v = std::getenv("MTG_NO_MAXMANA_GATE") == nullptr; return v; }

// UPPER bound on the net mana one tap of `def` can add to the floating pool -- used to bound the
// total mana still extractable from a set of untapped sources. Deliberately over- (never under-)
// counts so the gate stays lossless: an unfed filter/ramp-filter or a solo Reflecting Pool really
// yields less, but a looser bound only fails to prune, it never prunes a payable cost.
//   - is_filter    : {T} -> {C} is +1 net; the "feed 1, add 2" branch is also +1 net. Max = 1.
//   - ramp_filter  : "feed 1, add one of each colour" -> net |produces|-1 (Ferrous Lake, 2c -> +1).
//   - everything else (basic land / dork / rock / Reflecting Pool) : its per-tap output.
inline int SourceMaxNet(const CardDefinition& def)
{
    if (def.params.is_filter)   { return 1; }
    if (def.params.ramp_filter) { const int p = static_cast<int>(def.params.produces.size());
                                  return p > 0 ? p - 1 : 0; }
    return ManaProducedPerTap(def);
}

// Reservation audit (see ReserveEnabled): the bitmask of the active player's untapped SPECIAL mana
// sources worth "leaving up" -- ones whose non-mana value (attacking, or a preserved depletion
// counter) is lost the moment they tap. The tap functions try to pay while holding these back and
// only spend them when the cost cannot be met without them (slack-only -> weakly dominant). Scope
// (matches the reservation handoff doc):
//   * a mana DORK that can attack this turn (untapped, not summoning-sick -> CanTap()): held to attack
//     (0-power dorks still matter -- an Invigorate pump target, or the lone attacker that switches on
//     exalted). The exalted/attack-declaration side is the ShouldAttackWith hook, not this mask.
//   * a {C}-only can_animate MANLAND that can attack once animated this turn (not entered this turn):
//     held to animate + attack.
//   * a DEPLETION land (enters_tapped_with_depletion): held so a counter is not wasted when unneeded.
// Deliberately NOT reserved: coloured/dual manlands and coloured depletion-free lands (normal mana),
// filters/rocks. Returns 0 when reservation is off or the battlefield exceeds 64 (bitmask limit,
// matching the backtracker's memo cap), so those cases pay exactly as before (byte-identical).
inline std::uint64_t ReservableSpecialMask(const GameState& state)
{
    if (!ReserveEnabled()) { return 0; }
    const int active = state.active_player_index;
    const int n      = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }
    std::uint64_t mask = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        bool reserve = false;
        if (d->tmpl == CardTemplate::ManaDork && p.CanTap())
        {
            // Only reserve a dork whose mana is INFLEXIBLE (<=1 colour): holding it costs no colour
            // fixing. A flexible (dual/tri/rainbow) dork is deliberately NOT reserved -- holding it
            // trades away fixing the turn's other casts may need, which is not a pure improvement
            // (it regressed Anti-Lifegain game_6: reserving rainbow Birds / tri Ignoble).
            const std::vector<Color>& mp = EffectiveProduces(state, active, *d);
            if (mp.size() <= 1) { reserve = true; }
        }
        else if (d->params.can_animate && !p.entered_this_turn)
        {
            bool colored = false;                             // {C}-only manland (mirror ManaSourceRank 60)
            for (Color c : EffectiveProduces(state, active, *d)) { if (c != Color::Colorless) { colored = true; break; } }
            if (!colored) { reserve = true; }
        }
        else if (d->params.enters_tapped_with_depletion > 0)
        {
            reserve = true;                                   // depletion land -> conserve the counter
        }
        if (reserve) { mask |= (1ull << i); }
    }
    return mask;
}

inline bool TapForCostBacktrack(GameState& state, const ManaCost& cost,
                                bool for_creature, ManaPool floating,
                                const std::vector<Color>* rp_colors = nullptr,
                                TapBacktrackMemo* fail_memo = nullptr,
                                ManaPool* out_leftover = nullptr,
                                std::uint64_t tapped_mask = 0,
                                int untapped_max = -1,
                                std::uint64_t reserved_mask = 0,
                                ManaPool* out_full_pool = nullptr)
{
    TapSpeculationScope _spec;   // suppress phantom drip-land life events from speculative taps
    if (floating.CanPay(cost))
    {
        // Surface the over-produced remainder (forced filter/depletion over-tap) so the
        // caller can float it for the rest of the main phase. SpendFloatingTowardCost drains
        // exactly the cost (CanPay is true), leaving the leftover in `lo`. nullptr -> no-op.
        if (out_leftover) { ManaPool lo = floating; ManaCost c = cost; SpendFloatingTowardCost(lo, c); *out_leftover = lo; }
        // Whole-turn batch pre-payment (BatchPrepayMainCasts) wants the FULL produced pool at the
        // solution -- the concrete mana the chosen tap set makes -- so it can pre-load floating and
        // pay every main cast from it. nullptr on every hot path -> byte-identical there.
        if (out_full_pool) { *out_full_pool = floating; }
        return true;
    }
    const int active = state.active_player_index;
    const int n      = static_cast<int>(state.battlefield.size());

    // Failure-state memo. The backtracker explores tap ORDERINGS, and many orderings converge on the
    // same (tapped-source set, floating pool) state -- once such a state is proven to admit no legal
    // payment, every other ordering reaching it also fails, so re-exploring it is pure waste. Caching
    // PROVEN FAILURES (only) collapses the permutation explosion toward the powerset (the combo-turn
    // blowup was 54% self-time, almost all deep re-exploration). Byte-identical: failures never yield
    // a payment, so the FIRST solution the DFS finds -- and the exact sources it leaves tapped -- is
    // unchanged; we only short-circuit revisits that would have returned false anyway. The key is the
    // active player's tapped-source bitmask (complete: untapped sources = the remaining choices; cost,
    // for_creature and the RP union are invariant per top-level call) plus the packed floating pool;
    // stored as the full pair (not a lossy hash) so a hash collision can never cause a false prune.
    // Set up once at the top-level call and threaded down; disabled when n>64 (bitmask won't fit).
    const bool top_level = (fail_memo == nullptr);
    if (top_level && tapstats::Enabled()) { tapstats::g_backtrack_entries.fetch_add(1, std::memory_order_relaxed); }
    TapBacktrackMemo memo_local;
    if (top_level && n <= 64) { fail_memo = &memo_local; }

    std::pair<std::uint64_t, std::uint64_t> key{0, 0};
    if (fail_memo)
    {
        // key.first = the active player's tapped-source bitmask. Computed ONCE by scanning at the
        // top-level call, then maintained incrementally as `tapped_mask` threaded through the
        // recursion (each activate() ORs in the bit of the source it taps) -- so deeper nodes skip
        // the O(n) battlefield rescan that used to run per node. Byte-identical: the top-level scan
        // captures the same already-tapped permanents, and the recursion only ever taps active-player
        // sources by index, so the running mask equals what the per-node scan would have produced.
        if (top_level)
        {
            for (int i = 0; i < n; ++i)
            {
                if (state.battlefield[i].controller_index == active && state.battlefield[i].tapped)
                { tapped_mask |= (1ull << i); }
            }
        }
        key.first = tapped_mask;
        auto cl = [](int v) -> std::uint64_t
        { return static_cast<std::uint64_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
        key.second = cl(floating.white) | (cl(floating.blue) << 8) | (cl(floating.black) << 16)
                   | (cl(floating.red) << 24) | (cl(floating.green) << 32)
                   | (cl(floating.colorless) << 40) | (cl(floating.wild) << 48);
        if (fail_memo->count(key)) { return false; }
    }

    // Branch-and-bound TOTAL-mana gate (lossless). `untapped_max` is an UPPER bound on the total
    // mana still extractable from the active player's untapped sources (SourceMaxNet summed).
    // Computed ONCE at the top-level call and threaded down -- activate() subtracts the tapped
    // source's bound, so every node's check is O(1). If floating + untapped_max cannot cover the
    // cost's total pips, NO tap ordering from here pays it, so prune the whole subtree now instead
    // of exploring it to prove failure. This targets the combo-turn tail directly: big-{X} probes
    // fail on TOTAL mana and otherwise walk the entire tree (why the fail-memo exists). Byte-
    // identical: an over-count only loosens the bound, so a payable cost is never pruned; on reject
    // we record the failure (like the loop's fall-through) so revisits short-circuit too.
    if (MaxManaGateEnabled())
    {
        if (untapped_max < 0)   // top-level: sum the board's remaining max output once
        {
            untapped_max = 0;
            for (int i = 0; i < n; ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != active || p.tapped) { continue; }
                if (reserved_mask & (1ull << i)) { continue; }   // reservation audit: held source unavailable
                const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                if (!d) { continue; }
                const bool is_src = (d->tmpl == CardTemplate::BasicLand)
                                 || (d->tmpl == CardTemplate::ManaDork && p.CanTap())
                                 || d->params.mana_rock;
                if (!is_src) { continue; }
                if (d->params.creature_mana_only && !for_creature) { continue; }
                untapped_max += SourceMaxNet(*d);
            }
        }
        if (floating.Total() + untapped_max < cost.ManaValue())
        {
            if (fail_memo) { fail_memo->insert(key); }
            return false;
        }
    }

    // Reflecting Pool's colour union is INVARIANT during a tap-backtrack (no land enters or leaves
    // while paying mana) and SHARED by all of the controller's Reflecting Pools. Compute it ONCE
    // per top-level call -- lazily, only when the first reflecting source is actually reached --
    // then thread the pointer through the recursion so it is never rescanned per node. Without this
    // it was an O(battlefield) rescan per RP per recursion node, ~9x on a Reality-Spasm combo turn
    // with two RPs in play (seed-7000 game 53). Every non-RP deck never hits the branch -> 0 cost.
    // We alias ReflectedColors' thread_local buffer DIRECTLY rather than copying it into a local
    // vector: nothing inside this recursion subtree calls ReflectedColors/EffectiveProduces again
    // (deeper nodes are guarded by rp_ready, and activate/CanPay touch no mana-colour scan), so the
    // buffer stays valid for the whole call -- and we save a heap vector alloc per top-level call
    // (the stl_vector alloc churn in the combo-turn callgrind).
    bool rp_ready = (rp_colors != nullptr);

    for (int i = 0; i < n; ++i)
    {
        if (state.battlefield[i].controller_index != active || state.battlefield[i].tapped)
        { continue; }
        if (reserved_mask & (1ull << i)) { continue; }   // reservation audit: this source is held (not tappable)
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!def) { continue; }
        const bool is_src = (def->tmpl == CardTemplate::BasicLand)
                         || (def->tmpl == CardTemplate::ManaDork && state.battlefield[i].CanTap())
                         || def->params.mana_rock;
        if (!is_src) { continue; }
        if (def->params.creature_mana_only && !for_creature) { continue; }

        // Reflecting Pool -> the shared, hoisted union (empty = solo RP = no mana); every other
        // source -> its static produces[]. (Inlined EffectiveProduces so the union is reused.)
        if (def->params.reflecting && !rp_ready)
        { rp_colors = &ReflectedColors(state, active, /*in_hand=*/false); rp_ready = true; }
        const std::vector<Color>& produces = def->params.reflecting ? *rp_colors : def->params.produces;
        // Undo across this source's options. `activate` only ever modifies THIS source (its
        // tapped flag + a depletion counter); deeper recursion taps OTHER sources but each level
        // self-restores on failure ("returns false => state unchanged", by induction over the
        // same activate-restore pattern), and the function never resizes the battlefield. So
        // snapshotting/restoring the single source permanent is byte-identical to snapshotting
        // the whole battlefield vector -- and avoids an O(battlefield) copy per recursion node
        // (this copy was ~59% of a combo-turn search; see hinata-profile-perf callgrind).
        // Narrower still: activate() mutates ONLY this source's `tapped` flag and (via
        // DecrementDepletionOnTap) at most one Depletion counter, so we snapshot just those --
        // avoiding even the single-Permanent copy (and its `counters` heap vector) per node. The
        // pre-tap `tapped` is always false here (the loop skips already-tapped sources). `counters`
        // is copied only when non-empty (depletion decks); a source with no counters -- every land
        // in a ritual-combo deck like Hinata -- restores with a plain bool assignment. Byte-identical.
        const bool tapped_snap = state.battlefield[i].tapped;
        const bool has_counters = !state.battlefield[i].counters.empty();
        std::vector<Counter> counters_snap;
        if (has_counters) { counters_snap = state.battlefield[i].counters; }
        const int life_snap = state.players[active].life;
        const int opp_life_snap = state.players[1 - active].life;   // for tap_opponent_lifegain undo
        const bool oll_snap = state.opponent_lost_life_this_turn;

        // Physically tap source i, recurse with `next` floating, undo on failure. `drip_ok` is
        // false for a Grove-style drip land's painless "{T}: Add {C}" branch (a generic pip absent
        // a Remedy) so it does not pay the opponent life; its {R}/{G} branches leave it true.
        auto activate = [&](const ManaPool& next, bool drip_ok = true) -> bool
        {
            state.battlefield[i].tapped = true;
            DecrementDepletionOnTap(state.battlefield[i]);
            if (def->params.tap_self_damage > 0)
            { state.players[active].life -= def->params.tap_self_damage; }
            // Grove of the Burnwillows drip (opponent gains -> loses with Remedy). Restored
            // below on failure alongside the active player's life.
            if (drip_ok && def->params.tap_opponent_lifegain > 0)
            { OpponentGainsLife(state, active, def->params.tap_opponent_lifegain); }
            if (TapForCostBacktrack(state, cost, for_creature, next, rp_colors, fail_memo, out_leftover,
                                    tapped_mask | (1ull << i),
                                    untapped_max < 0 ? -1 : untapped_max - SourceMaxNet(*def),
                                    reserved_mask, out_full_pool)) { return true; }
            state.battlefield[i].tapped = tapped_snap;   // only this source was touched at this level
            if (has_counters) { state.battlefield[i].counters = counters_snap; }
            state.players[active].life = life_snap;
            state.players[1 - active].life      = opp_life_snap;
            state.opponent_lost_life_this_turn  = oll_snap;
            return false;
        };

        if (def->params.is_filter)
        {
            { ManaPool f = floating; f.Add(Color::Colorless, 1); if (activate(f)) { return true; } }  // {T}: Add {C}
            if (floating.Total() >= 1 && !produces.empty())                                            // feed 1, Add 2
            {
                for (Color c1 : produces) for (Color c2 : produces)
                {
                    ManaPool f = floating; Color took;
                    if (!ConsumeFloatingAny(f, took)) { break; }
                    f.Add(c1, 1); f.Add(c2, 1);
                    if (activate(f)) { return true; }
                }
            }
        }
        else if (def->params.ramp_filter)
        {
            if (floating.Total() >= 1 && !produces.empty())   // {1},{T}: feed 1, Add one of each colour
            {
                ManaPool f = floating; Color took;
                if (ConsumeFloatingAny(f, took))
                {
                    for (Color c : produces) { f.Add(c, 1); }
                    if (activate(f)) { return true; }
                }
            }
        }
        else
        {
            const int amt = ManaProducedPerTap(*def);
            if (produces.empty())
            {
                // Empty colours: a {C}-only source taps for colourless. A reflecting source with
                // no other land, though, produces NOTHING -- don't let a solo Reflecting Pool tap
                // for {C} (it would falsely pay a generic pip). Skip it entirely.
                if (!def->params.reflecting)
                { ManaPool f = floating; f.Add(Color::Colorless, amt); if (activate(f)) { return true; } }
            }
            else
            {
                // Grove-style drip land: try the painless "{T}: Add {C}" mode FIRST (no drip) so a
                // GENERIC pip never pays the opponent life. A coloured pip falls through to the
                // {R}/{G} branches below (which drip -- the real cost of that colour). When the gift is
                // useful (OpponentLifegainUseful) the drip is +1 value, so skip {C} mode and keep only
                // the coloured branches (matches the TapDripLandsIfUseful sweep). Inert for non-drip lands.
                if (def->params.tap_opponent_lifegain > 0 && !ResolveProvider(state).OpponentLifegainUseful(state, active))
                { ManaPool f = floating; f.Add(Color::Colorless, amt); if (activate(f, /*drip_ok=*/false)) { return true; } }
                for (Color c : produces)
                { ManaPool f = floating; f.Add(c, amt); if (activate(f)) { return true; } }
            }
        }
    }
    // Every option from this (tapped-set, floating) state was exhausted without paying -> record the
    // proven failure so a different tap ordering reaching the same state short-circuits instead of
    // re-exploring. State is unchanged here (the invariant), so this is byte-identical.
    if (fail_memo) { fail_memo->insert(key); }
    return false;
}

// Sacrifices any land whose depletion counters have run out (count 0): the depletion
// lands' "If there are no depletion counters on it, sacrifice it." Safe to call after
// any batch of mana taps; iterates and erases its own way so it must not run while a
// caller holds a battlefield reference/iterator.
inline void SacrificeDepletedLands(GameState& state)
{
    for (std::vector<Permanent>::iterator it = state.battlefield.begin();
         it != state.battlefield.end(); )
    {
        bool depleted = false;
        for (const Counter& c : it->counters)
        {
            if (c.type == Counter::Type::Depletion && c.count <= 0) { depleted = true; break; }
        }
        if (depleted)
        {
            state.players[it->owner_index].graveyard.push_back(it->card);
            it = state.battlefield.erase(it);
        }
        else { ++it; }
    }
}

// Fires prowess triggers for all Prowess creatures the active player controls.
// Called at cast time (when a spell is pushed to the stack or applied directly in
// lookahead simulation), not at resolution — prowess reads "whenever you cast."
// Only noncreature spells trigger prowess (CR 702.107a).
inline void FireProwess(GameState& state, const CardDefinition& def)
{
    if (def.card.IsCreature()) { return; }

    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (!p.card.HasKeyword(Keyword::Prowess)) { continue; }
        ++p.temp_power_bonus;
        ++p.temp_tough_bonus;
    }
}

// Pump-then-Swords redirect (Anti-Lifegain combo). Just before Swords to Plowshares
// (`removal_def`, controller_lifegain_equals_power) exiles the opponent creature at battlefield
// index `target_bi`, redirect a FREE-alt Invigorate-style pump from `active`'s hand onto THAT
// creature instead of an own attacker. Invigorate's alt cost ("rather than pay {2}{G}, an
// opponent gains 3 life") is free (the enabler turns the 3 gain into 3 loss) and STILL grants
// the +N/+M, so the pump costs no mana. Pumping the Swords target makes the exile life-loss
// `power_bonus` larger, for `power + pump + alt_lifegain` total opponent loss.
//
// Value vs the default (pump an own attacker): EQUAL when the own creature could actually swing
// this turn (both +power_bonus opponent life loss -- combat vs a bigger exile), and STRICTLY
// BETTER whenever it cannot -- no own creature (Invigorate would be stuck uncastable, losing all
// of it), the only creature is a mana dork tapped for mana, or it is summoning-sick. So this is
// >= the current behaviour on every board. Free (no mana), so no search-budget change. See
// docs/design/antilifegain-swords-targeting.md.
//
// Fires a FULL alt-cost Invigorate cast (alt lifegain + on-cast triggers [Aria of Flame verse] +
// prowess + the pump), so it is value-identical to the normal safe-alt auto-fire -- only the pump
// TARGET differs. Consuming the pump here also removes it from the later safe-alt auto-fire pass
// (ApplyPlanDirect / AIEngine), so it is never double-fired. Gated on !DecisionUnpruned() (the
// same condition as that pass): fires for autonomous search and the engine's AI-hint rollout,
// suppressed for the play viewer / unpruned A/B where the pump is an enumerated decision the
// human/search owns. Shared by ResolveRemoval (executor) and the ApplyPlanDirect Removal branch
// (rollout) so both realise the identical combo.
inline void TryPumpThenSwordsRedirect(GameState& state, int active, int target_bi,
                                      const CardDefinition& removal_def)
{
    if (!removal_def.params.controller_lifegain_equals_power) { return; }
    if (DecisionUnpruned(UnprunedGate::Redirect))             { return; }
    if (target_bi < 0 || target_bi >= static_cast<int>(state.battlefield.size())) { return; }
    if (state.battlefield[target_bi].controller_index == active) { return; }  // opponent creatures only
    if (!RemedyActive(state, active))                         { return; }     // enabler in play

    Player& ap = state.players[active];
    for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!d) { continue; }
        const CardParams& pp = d->params;
        if (pp.alt_lifegain_cost <= 0)    { continue; }   // needs the free alt cost
        if (pp.destroy_all_enchantments)  { continue; }   // Reverent Silence: not a creature pump
        if (!pp.target_own_creature)      { continue; }   // Invigorate-style +N/+M on a creature
        if (pp.power_bonus <= 0)          { continue; }
        if (!ControlsSubtype(state, active, pp.alt_cost_requires_subtype)) { continue; }  // a Forest

        // Fire the pump onto the Swords target, mirroring a normal alt-cost Invigorate cast:
        // pull it from hand, pay the alt lifegain, fire on-cast triggers (Aria verse) + prowess,
        // apply +N/+M to the target, then send the spell to the graveyard. FireOnCastTriggers may
        // append tokens to the battlefield (never erase), so target_bi stays valid.
        Card inv = ap.hand[i];
        ap.hand.erase(ap.hand.begin() + i);
        OpponentGainsLife(state, active, pp.alt_lifegain_cost);
        FireOnCastTriggers(state, *d);
        FireProwess(state, *d);
        state.battlefield[target_bi].temp_power_bonus += pp.power_bonus;
        state.battlefield[target_bi].temp_tough_bonus += pp.tough_bonus;
        ap.graveyard.push_back(inv);
        return;   // one redirect per Swords cast
    }
}
